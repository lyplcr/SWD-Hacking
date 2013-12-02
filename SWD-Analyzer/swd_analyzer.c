
/*
    SWD Analyzer
    
    Dec 2, 2013 cpldcpu
    based on code by chris
   
    http://www.mikrocontroller.net/topic/309185    
    
    Based on:
    http://sourceforge.net/apps/mediawiki/stm32primer2swd/index.php?title=Main_Page#SWD_Packet_Construction
*/

#include <stdio.h>
#include <string.h>

typedef unsigned char 		byte;
typedef unsigned short 		word;
typedef unsigned int 		dword;
typedef unsigned long long 	qword;

FILE             *inputfile;
int              samplecount,totalbits,failbits;
unsigned char    lastbyte;

#define     CLKMASK     0x02
#define     DATMASK     0x01
#define     SAMPLERATE  16000000

#define     SWD_RESETCOND   0xFFFFFFFFFFFFC000

dword parity(dword dat) 
{ 
    int ret=0;  
    for(;dat;dat>>=1) ret+=dat&1;  
    return ret&1;
}


qword lsb(char len, char offset, qword dat) 
{
 qword ret=0;
 char i;
 while(len--) if(dat&((qword)1<<(63-offset-len))) ret|=(qword)1<<len;
 return ret;
}


int eof;
int pos;
int verbose;


void dump_(dword x) { 
int i=32;
printf(" ");
	for(i=8;i--;x>>=1) printf("%d",x&1);
printf("_");
	for(i=8;i--;x>>=1) printf("%d",x&1);
printf("_");
	for(i=8;i--;x>>=1) printf("%d",x&1);
printf("_");
	for(i=8;i--;x>>=1) printf("%d",x&1);
printf(" ");
}

void dump(dword x) { int i=32;
 x=lsb(32,32,x);
 dump_(x);
}

#define tblsize(tbl)	(sizeof(tbl)/sizeof(tbl[0]))

char* dap_id[]={ "IDCODE","ABORT","/STAT","DP-CTRL","WIRE-CTRL","WIRE-CTRL","READ_RESENT","SELECT","BUFFER","BUFFER"};
char* ap_0 []={ "CSW/Control", "TAR/Transfer" , "", "DRW/Data" };
char* ap_1 []={ "BD0/Bank"  , "BD1/Bank" , "BD2/Bank" ,"BD3/Bank" };
char* ap_ff[]={ "","CFG/Configuration","BASE/Base Address","IDR/Identification"};


// start - DAP/AP - W/R - adr[2:3] - Parity - Stop - Park - Trn - ACK W - Ack R - ACK Busy - Trn - Data32 - Parity --
//   1		X		X		XX         X       	0		1	 Z     X         X        X       Z     X32        X

int swd(qword dataRE,qword dataFE) 
{
 byte header,ack,ret=0,t,addr,read,dap,par,datre,parre; dword dat;
 char*id="";
 static byte bank;
 
 //if(!((data>>63)&1)) return 1; // start bit ?
// puts("DBG>");

/*
if (verbose&2) {
 dump(data>>32);  
 dump(data);  puts("");
}
*/

 header = dataRE>>(64-8);
 dap	= lsb(1,1,dataRE);
 read 	= lsb(1,2,dataRE);
 addr	= lsb(2,3,dataRE); 
 ack 	= lsb(3,8+1,dataFE);
 
 if (read==1)
 {          // Read from target Trn at end
    dat	= lsb(32,8+4,dataFE);
    par	= lsb(1,8+4+32,dataFE);
    datre	= lsb(32,8+4,dataRE);
    parre	= lsb(1,8+4+32,dataRE);
 } else
 {         // write to target Trn between ack and data
    dat	= lsb(32,8+4+1,dataRE);
    par	= lsb(1,8+4+1+32,dataRE);
 }

 //if(header&2) return puts("stop error"),1; // test stop bit
 
 //if(parity(header&0x7c)) return printf("parity error header\n"),1;
 
 if(!ack) return printf("Ack error %i\n",ack),1; 
 
 if (ack==7) return printf("Protocol error ack=7 \n"),1; 
 
 if(ack==2) return printf(" wait ack=2\n "),8+4+1; // wait
 if(ack==4) return printf(" fault ack=4\n"),8+4+1; // fault
 
// if(parity(dat)!=par) printf("data parity error");

 if(dap) 				id=dap_id[(addr<<1)|(!read)];
 else if(bank==0) 		id=ap_0 [addr];
 else if(bank==1) 		id=ap_1 [addr];
 else if(bank==0xff) 	id=ap_ff[addr];
 

 printf("ack:%i SWD(%d) %s %s  = %#x %s  ",ack,addr, dap?"APACC":"DPACC",read?"Read":"Write", dat,id);

 if(parity(dat)!=par) printf("Bad parity FE ");
 
 printf("dat RE=%#x ",datre);
 if(parity(datre)!=parre) printf("Bad parity RE ");
 
 
 printf("\n");
 
 if(dap&&addr==2&&!read) bank=(dat>>4)&0xf; // bank selection for AP access , should i check parity error ???
 
 return 8+4+1+32+1;
}


/*
    Read next clock cycle from input file.
    
    1) if last byte was not clk low, wait for clk low
    2) wait for rising edge, sample
    3) wait for falling edge, sample
    
    return value
    
    -1       error
    Bit 1    SWDIO on rising edge
    Bit 0    SWDIO on falling edge    
*/

int getnextperiod(void)
{
    int bitsout=0;
    int bitcount=0;
    unsigned char nextbyte=0;
    
    // Wait for low
    while (lastbyte&CLKMASK)
    {
        lastbyte=fgetc(inputfile);
        samplecount++;
        if (feof(inputfile)) return -1;
    }
    
    while (bitcount<2)
    {
        nextbyte=fgetc(inputfile);
        samplecount++;
        if (feof(inputfile)) return -1;
        
        if ((lastbyte^nextbyte)&CLKMASK)        // detect clk transition
        {
            bitsout=(bitsout<<1) | (!!(nextbyte&DATMASK));            
            bitcount++;        
            lastbyte=nextbyte;
        }            
    }
    
    return bitsout;
}




int main(int argc,char**argv) 
{ 
    verbose=2;

    
    if (argc<2) 
    {
        printf("Fatal error: no input file.\n");        
        return -1;
    }
    if (!strcmp(argv[1],"--help"))
    {
        printf("SWD Protocol Analyzer v0.1\n\n");
        printf("Usage: swd_analzyer [options] file...\n");
        printf("Options:\n");
        printf("  --help\t\tPrint this information\n");
        printf("\n\nThe input file contains a raw dump of the SWD bus traffic. Each sample is one byte SWDIO=bit 0, SWDCLK=bit 1.\n");
        return 0;
    }

    if (!(inputfile=fopen(argv[1],"r")))
    {
        printf("Fatal error: could not open input file.\n");
        return -1;
    }

    lastbyte=0;
    samplecount=0;
    totalbits=0;
    failbits=0;

     int                     discardbits=64;
     unsigned long long      bufferRE=0,bufferFE=0;
     while (1)
     {
        int inp;
        while (discardbits-->0)
        {
            inp=getnextperiod();
            if (verbose>2) printf("%i.%i-",inp,discardbits);           
            bufferRE=(bufferRE<<1)|((inp&0x02)>>1);   // rising edge
            bufferFE=(bufferFE<<1)|(inp&0x01);   // falling edge            
            totalbits++;
        }

        // error reading more bits   
        if (inp==-1) break;
        
  //     if (totalbits>5000) break;
        
        // detect reset = 50 cycles while swdio=1
        if ((bufferRE&SWD_RESETCOND)==SWD_RESETCOND)
        {
       //     if (verbose>1) printf("Reset close to: %i (%f s) - errorbits: %i\n",samplecount,(float)samplecount/SAMPLERATE,failbits);
            discardbits=50;
            //printf("next :%X\n",(bufferRE<<50)>>(64-8));
            failbits=0;
            continue;
        }
   
        // start - DAP/AP - W/R - adr[2:3] - Parity - Stop - Park - Trn - ACK W - Ack R - ACK Busy - Trn - Data32 - Parity --
        //   1		X		X		XX         P       	0		1	 Z     X         X        X       Z     X32        X
   
        unsigned int header=bufferRE>>(64-8);
  
        // Only process data if parity is correct and both start and stop bit are found.    
//        if ((!parity(header&0x7c)) && ((header&0x82)==0x80) && (failbits<10)) 
        if ((!parity(header&0x7c)) && ((header&0x82)==0x80)) 
//        if ((header&0x82)==0x81)
        {
  //            if (verbose>1) printf("Valid Header %X close to: %i (%f s) - Park %i - errorbits: %i - payload %X/%X\n",header,samplecount,(float)samplecount/SAMPLERATE,!!(header&1),failbits,(unsigned int)(bufferRE>>24),(unsigned int)(bufferFE>>24));        
                        
              discardbits=swd(bufferRE,bufferFE);
              continue;                    
        }
               
        // No idea what is going on, lets just read another bit
        failbits++;
        discardbits=1;       
     }
   
    fclose(inputfile);
}
