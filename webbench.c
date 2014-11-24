/*
 * (C) Radim Kolar 1997-2004
 * This is free software, see GNU Public License version 2 for
 * details.
 *
 * Simple forking WWW Server benchmark:
 *
 * Usage:
 *   webbench --help
 *
 * Return codes:
 *    0 - sucess
 *    1 - benchmark failed (server is not on-line)
 *    2 - bad param
 *    3 - internal error, fork failed
 * 
 * Web Bench is very simple tool for benchmarking WWW or proxy servers. 
 * Uses fork() for simulating multiple clients and 
 * can use HTTP/0.9-HTTP/1.1 requests. 
 * This benchmark is not very realistic, 
 * but it can test if your HTTPD can realy handle that 
 * many clients at once (try to run some CGIs) 
 * without taking your machine down. 
 * Displays pages/min and bytes/sec. 
 * Can be used in more aggressive mode with -f switch.
 */ 

 /*ע����һЩʵ�ʵ�ֵ��������ʵ�� webbench -c 100 http://www.baidu.com������ֵ*/

 
#include "socket.c"
#include <unistd.h>
#include <sys/param.h>
#include <rpc/types.h>
#include <getopt.h>
#include <strings.h>
#include <time.h>
#include <signal.h>

/* values */
volatile int timerexpired=0;   //volatile�ؼ������Ż���أ��˱�����˼Ϊʱ�䵽
int speed=0;
int failed=0;
int bytes=0;
/* globals */
int http10=1; /* 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define PROGRAM_VERSION "1.5"
int method=METHOD_GET;
int clients=1;
int force=0;
int force_reload=0;
int proxyport=80;             //http��Ĭ�϶˿ں�Ϊ80
char *proxyhost=NULL;
int benchtime=30;
/* internal */
int mypipe[2];
char host[MAXHOSTNAMELEN];
#define REQUEST_SIZE 2048
char request[REQUEST_SIZE];

static const struct option long_options[]=
{
     {"force",no_argument,&force,1},
     {"reload",no_argument,&force_reload,1},
     {"time",required_argument,NULL,'t'},
     {"help",no_argument,NULL,'?'},
     {"http09",no_argument,NULL,'9'},
     {"http10",no_argument,NULL,'1'},
     {"http11",no_argument,NULL,'2'},
     {"get",no_argument,&method,METHOD_GET},
     {"head",no_argument,&method,METHOD_HEAD},
     {"options",no_argument,&method,METHOD_OPTIONS},
     {"trace",no_argument,&method,METHOD_TRACE},
     {"version",no_argument,NULL,'V'},
     {"proxy",required_argument,NULL,'p'},
     {"clients",required_argument,NULL,'c'},
     {NULL,0,NULL,0}
};


static void alarm_handler(int signal)
{
   timerexpired=1;     
}	

/*prototype ԭ��*/
//hostֵΪwww.baidu.com
//portֵΪ80
//reqֵΪGET / HTTP/1.0\r\nUser-Agent: WebBench 1.5\r\nHost: www.baidu.com\r\n\r\n
//req��ֵΪ��80�˿ڷ��͵������ֵ��httpЭ���Դ����������Ӧ
void benchcore(const char *host,const int port,const char *req)
{
     int rlen;
     char buf[1500];
     int s,i;
     struct sigaction sa;

     /* setup alarm signal handler */
     sa.sa_handler=alarm_handler;    //�źŴ�����
     sa.sa_flags=0;
     if(sigaction(SIGALRM,&sa,NULL))   //����SIGALRM�ź�
        exit(3);
     alarm(benchtime);      //���ö�ʱ����benchtime������SIGALRM�ź�

     rlen=strlen(req);      //rlen == 65
nexttry: while(1)
     {
        if(timerexpired)
        {
           if(failed>0)
           {
              /* fprintf(stderr,"Correcting failed by signal\n"); */
              failed--;
           }
           return;
        }
        s=Socket(host,port);                          
        if(s<0) { 
            failed++;
            continue;
        } 
        if(rlen!=write(s,req,rlen)) {
            failed++;
            close(s);
            continue;
        }
        if(http10==0) 
    	    if(shutdown(s,1)) { 
    	        failed++;
    	        close(s);
    	        continue;
    	    }
        if(force==0) 
        {
                /* read all available data from socket */
    	    while(1)
    	    {
                if(timerexpired) 
                    break; 
    	        i=read(s,buf,1500);
                  /* fprintf(stderr,"%d\n",i); */
    	        if(i<0) {     //��ʧ��
                    failed++;
                    close(s);
                    goto nexttry; //��������ѭ����������goto
                }
    	        else
    		        if(i==0) 
    		            break;
    		        else
    			       bytes+=i;
    	    }
        }
        if(close(s)) {
            failed++;
            continue;
        }
        speed++;
     }
}

/* vraci system rc error kod */
static int bench(void)
{
      int i,j,k;	
      pid_t pid=0;
      FILE *f;

      /* check avaibility of target server */
      i=Socket(proxyhost==NULL?host:proxyhost,proxyport);
      if(i<0) { 
          fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
          return 1;
      }
      close(i);
      /* create pipe */
      //�ɲ���mypipe���������ļ���������mypipe[0]Ϊ�����򿪣�mypipe[1]Ϊд����
     
      if(pipe(mypipe)) {
    	  perror("pipe failed.");
    	  return 3;
      }

      /* not needed, since we have alarm() in childrens */
      /* wait 4 next system clock tick */
      /*
      cas=time(NULL);
      while(time(NULL)==cas)
            sched_yield();
      */

      /* fork childs */
      for(i=0;i<clients;i++)
      {
    	   pid=fork();
    	   if(pid <= (pid_t) 0)    //pid=0�����ӽ���
    	   {
    		   /* child process or error*/
    		   //�������ӳ���Ӧ��ִ�еĵط��������break���ӳ�������ѭ����
    		   //��Ȼ�ӳ���Ҳ��ִ��forѭ���������ӽ���
    	       //sleep(1); /* make childs faster */
    		   break;
    	   }
      }

      if(pid< (pid_t) 0)
      {
          fprintf(stderr,"problems forking worker no. %d\n",i);
    	  perror("fork failed.");
    	  return 3;
      }

      if(pid == (pid_t) 0)
      {
        /* I am a child */
        if(proxyhost==NULL)
          benchcore(host,proxyport,request);
        else
          benchcore(proxyhost,proxyport,request);

             /* write results to pipe */
    	 f=fdopen(mypipe[1],"w");
    	 if(f==NULL)
    	 {
    		 perror("open pipe for writing failed.");
    		 return 3;
    	 }
    	 /* fprintf(stderr,"Child - %d %d\n",speed,failed); */
    	 fprintf(f,"%d %d %d\n",speed,failed,bytes);
    	 fclose(f);
    	 return 0;
      } else
      {
          /*read results from pipe*/
    	  f=fdopen(mypipe[0],"r");
    	  if(f==NULL) 
    	  {
    		  perror("open pipe for reading failed.");
    		  return 3;
    	  }
    	  setvbuf(f,NULL,_IONBF,0);
    	  speed=0;
          failed=0;
          bytes=0;

    	  while(1)
    	  {
    		  pid=fscanf(f,"%d %d %d",&i,&j,&k);
    		  if(pid<2)
                      {
                           fprintf(stderr,"Some of our children died.\n");
                           break;
                      }
    		  speed+=i;
    		  failed+=j;
    		  bytes+=k;
    		  /* fprintf(stderr,"*Knock* %d %d read=%d\n",speed,failed,pid); */
    		  if(--clients==0) break;
    	  }
    	  fclose(f);

      printf("\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
    		  (int)((speed+failed)/(benchtime/60.0f)),
    		  (int)(bytes/(float)benchtime),
    		  speed,
    		  failed);
      }
      return i;
}

/*�˺�������url��������ȡ����������Ҫ�ĸ���������ֵ��url=http://www.baidu.com/*/
void build_request(const char *url) 
{
    char tmp[10];
    int i;

    bzero(host,MAXHOSTNAMELEN);  //char host[MAXHSOTNAMELEN]���˺������host����
    bzero(request,REQUEST_SIZE); //���request����

    if(force_reload && proxyhost!=NULL && http10<1) http10=1;
    if(method==METHOD_HEAD && http10<1) http10=1;
    if(method==METHOD_OPTIONS && http10<2) http10=2;
    if(method==METHOD_TRACE && http10<2) http10=2;

    switch(method)  //Ĭ��ΪMETHOD_GRT(0)
    {
      default:
      case METHOD_GET: strcpy(request,"GET");break;
      case METHOD_HEAD: strcpy(request,"HEAD");break;
      case METHOD_OPTIONS: strcpy(request,"OPTIONS");break;
      case METHOD_TRACE: strcpy(request,"TRACE");break;
    }
    	  
    strcat(request," ");

    if(NULL==strstr(url,"://"))
    {
      fprintf(stderr, "\n%s: is not a valid URL.\n",url);
      exit(2);
    }
    if(strlen(url)>1500)
    {
         fprintf(stderr,"URL is too long.\n");
     exit(2);
    }
    if(proxyhost==NULL)
       if (0!=strncasecmp("http://",url,7))  //URL��������http://��ͷ��
       { fprintf(stderr,"\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
             exit(2);
           }
    /* protocol/host delimiter */
    i=strstr(url,"://")-url+3;  //�˱��ʽ�Ľ��Ϊ7��http://��7���ֽ�
    /* printf("%d\n",i); */

    if(strchr(url+i,'/')==NULL) //����Ҫ���Ե���ַ����Ҫ��/��β������http://baidu.com/
    {
        fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
        exit(2);
    }
    if(proxyhost==NULL)
    {
    /* get port from hostname */
    if(index(url+i,':')!=NULL &&
      index(url+i,':')<index(url+i,'/'))
    {
       strncpy(host,url+i,strchr(url+i,':')-url-i);
       bzero(tmp,10);
       strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);
       /* printf("tmp=%s\n",tmp); */
       proxyport=atoi(tmp);
       if(proxyport==0) proxyport=80;
    } else
    {
     strncpy(host,url+i,strcspn(url+i,"/"));   //strcspn������Ҫcopy��hostname�ĳ���
    }
    // printf("Host=%s\n",host);
    strcat(request+strlen(request),url+i+strcspn(url+i,"/"));
    } else
    {
    // printf("ProxyHost=%s\nProxyPort=%d\n",proxyhost,proxyport);
    strcat(request,url);
    }
    if(http10==1)
      strcat(request," HTTP/1.0");
    else if (http10==2)
      strcat(request," HTTP/1.1");
      strcat(request,"\r\n");
    if(http10>0)
      strcat(request,"User-Agent: WebBench "PROGRAM_VERSION"\r\n");
    if(proxyhost==NULL && http10>0)
    {
      strcat(request,"Host: ");
      strcat(request,host);
      strcat(request,"\r\n");
    }
    if(force_reload && proxyhost!=NULL)
    {
      strcat(request,"Pragma: no-cache\r\n");
    }
    if(http10>1)
      strcat(request,"Connection: close\r\n");
    /* add empty line at end */
    if(http10>0) strcat(request,"\r\n"); 
    // printf("Req=%s\n",request);
}

static void usage(void)
{
   fprintf(stderr,
	"webbench [option]... URL\n"
	"  -f|--force               Don't wait for reply from server.\n"
	"  -r|--reload              Send reload request - Pragma: no-cache.\n"
	"  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
	"  -p|--proxy <server:port> Use proxy server for request.\n"//����
	"  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
	"  -9|--http09              Use HTTP/0.9 style requests.\n"
	"  -1|--http10              Use HTTP/1.0 protocol.\n"
	"  -2|--http11              Use HTTP/1.1 protocol.\n"
	"  --get                    Use GET request method.\n"
	"  --head                   Use HEAD request method.\n"
	"  --options                Use OPTIONS request method.\n"
	"  --trace                  Use TRACE request method.\n"
	"  -?|-h|--help             This information.\n"
	"  -V|--version             Display program version.\n"
	);
};


/*ר�Ž���һ�����������������в���*/
int parse_args(int argc, char **argv)
{
    int opt=0;
    int options_index=0;
    char *tmp=NULL;
    /*���������в�����#include<getopt.h>*/
    while((opt=getopt_long(argc,argv,"912Vfrt:p:c:?h",long_options,&options_index))!=EOF )
    {
      switch(opt)
      {
       case  0 : break;
       case 'f': force=1;break;
       case 'r': force_reload=1;break; 
       case '9': http10=0;break;
       case '1': http10=1;break;
       case '2': http10=2;break;
       case 'V': printf(PROGRAM_VERSION"\n");exit(0);
       case 't': benchtime=atoi(optarg);break;       
       case 'p': 
             /* proxy server parsing server:port */
             tmp=strrchr(optarg,':');
             proxyhost=optarg;
             if(tmp==NULL)
             {
                 break;
             }
             if(tmp==optarg)
             {
                 fprintf(stderr,"Error in option --proxy %s: Missing hostname.\n",optarg);
                 return 2;
             }
             if(tmp==optarg+strlen(optarg)-1)
             {
                 fprintf(stderr,"Error in option --proxy %s Port number is missing.\n",optarg);
                 return 2;
             }
             *tmp='\0';
             proxyport=atoi(tmp+1);break;
       case ':':
       case 'h':
       case '?': usage();return 2;break;
       case 'c': clients=atoi(optarg);break;   //-c��ʾ����ٸ��ͻ���������
      }
     }
 /*optind���޸�argv[]���飬��ѡ��Ͳ�����������ǰ�棬����ֵ���ں��棬
   optind�ڱ�������������ָ���һ����ѡ���������*/
 /*URL����Ϊ�������������optind==argc�Ļ�����ʾURLû�и���*/
     if(optind==argc) {
        fprintf(stderr,"webbench: Missing URL!\n");
		usage();
		return 2;
                    }
     if(clients==0) {
        printf("At least one client!\n");
        return 2;
     }
     if(benchtime==0) {
        printf("Bench time should not be 0!\n");
        return 2;
     }
     return 0;
}

void print_bench_info(char **argv)
{
      /* print bench info */
     printf("\nBenchmarking: ");
     switch(method)
     {
    	 case METHOD_GET:
    	 default:
    		 printf("GET");break;
    	 case METHOD_OPTIONS:
    		 printf("OPTIONS");break;
    	 case METHOD_HEAD:
    		 printf("HEAD");break;
    	 case METHOD_TRACE:
    		 printf("TRACE");break;
     }
     printf(" %s",argv[optind]);
     switch(http10)
     {
    	 case 0: printf(" (using HTTP/0.9)");break;
    	 case 2: printf(" (using HTTP/1.1)");break;
     }
     printf("\n");
     if(clients==1) printf("1 client");
     else
       printf("%d clients",clients);

     printf(", running %d sec", benchtime);
     if(force) printf(", early socket close");
     if(proxyhost!=NULL) printf(", via proxy server %s:%d",proxyhost,proxyport);
     if(force_reload) printf(", forcing reload");
     printf(".\n");  
}


void print_copyright()
{
    /* Copyright */
     fprintf(stderr,"Webbench - Simple Web Benchmark "PROGRAM_VERSION"\n"
    	 "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n"
    	 );
}


int main(int argc, char *argv[])
{
     int result;
     
     if(argc==1 || (parse_args(argc, argv) != 0))
     {
    	  usage();
          return 2;
     }
     
     print_copyright();
     print_bench_info(argv);
     build_request(argv[optind]);  //argv[optind] must be a URL
     result = bench();
     return result; 
}
