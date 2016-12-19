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
 * ������:
 *    0 - �ɹ�
 *    1 - ����ʧ��(������������)
 *    2 - ����Ĳ���
 *    3 - �ڲ�����,forkʧ��
 *
 */
#include "socket.c"
#include <unistd.h>
#include <sys/param.h>
#include <rpc/types.h>
#include <getopt.h>
#include <strings.h>
#include <time.h>
#include <signal.h>

/* values */
volatile int timerexpired=0; //�Ƿ���
��//��ʱ����ֹ
int speed=0;	//���Թ����д����page��
int failed=0;	//���Թ���������ʧ�ܵĴ���
int bytes=0;	//���Թ����д�����ֽ���
/* globals */
int http10=1; /*ָ����ʹ�õ�httpЭ�� 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define PROGRAM_VERSION "1.5" //�汾��
int method=METHOD_GET; //���󷽷� Ĭ��ΪGET

int clients=1;	//ͬʱ���ӵ��������Ŀͻ��˸���
int force=0;	//���ṩ��forceѡ��ʱ�ñ���Ϊ��
int force_reload=0;
int proxyport=80;
char *proxyhost=NULL;
int benchtime=30;
��//����ʱ��(��)
/* internal */
int mypipe[2];
char host[MAXHOSTNAMELEN];
#define REQUEST_SIZE 2048
char request[REQUEST_SIZE];

//�����г�����ѡ��---����ṹ���ڴ���������ѡ������Ŀ⺯��getopt_long()���õ�
static const struct option long_options[]= {
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
    {NULL,0,NULL,0} //������Ϊ����
};

/* ����ԭ�� */
static void benchcore(const char* host,const int port, const char *request);
static int bench(void);
static void build_request(const char *url);

static void alarm_handler(int signal)
{
    timerexpired=1;
}

//��ʾ������Ϣ
//webbench ִ�е�ѡ��˵����-c �������� -t ���в���ʱ�䣬������
static void usage(void)
{
    fprintf(stderr,
            "webbench [option]... URL\n"
            "  -f|--force               Don't wait for reply from server.\n"	//���ȴ��������Ļظ�
            "  -r|--reload              Send reload request - Pragma: no-cache.\n"��//�������¼�������-����:no - cache
            "  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"	//���Գ�������ʱ��<��>��Ĭ��30��
            "  -p|--proxy <server:port> Use proxy server for request.\n"	//ʹ��proxy������������
            "  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"	//ָ��n��HTTP�ͻ���ͬʱ���С�Ĭ������һ��
            "  -9|--http09              Use HTTP/0.9 style requests.\n"	//ʹ��HTTP / 0.9��ʽ������
            "  -1|--http10              Use HTTP/1.0 protocol.\n"	//ʹ��HTTP / 1.0Э��
            "  -2|--http11              Use HTTP/1.1 protocol.\n"	//ʹ��HTTP / 1.1Э��
            "  --get                    Use GET request method.\n"	//ʹ��GET���󷽷���
            "  --head                   Use HEAD request method.\n"	//ʹ��HEAD���󷽷�
            "  --options                Use OPTIONS request method.\n"	//ʹ��OPTIONS���󷽷�
            "  --trace                  Use TRACE request method.\n"	//ʹ��TRACE���󷽷�
            "  -?|-h|--help             This information.\n"	//��ʾ������Ϣ
            "  -V|--version             Display program version.\n"��//��ʾ�汾��Ϣ
           );
};

int main(int argc, char *argv[])
{
    int opt=0;
    int options_index=0;
    char *tmp=NULL;

    if(argc==1) {
        usage();
        return 2;
    }

//getopt_long()����������ѡ�������
    while((opt=getopt_long(argc,argv,"912Vfrt:p:c:?h",long_options,&options_index))!=EOF ) {
        switch(opt) {
            case  0 :
                break;
            case 'f':
                force=1;
                break;
            case 'r':
                force_reload=1;
                break;
            case '9':
                http10=0;
                break;
            case '1':
                http10=1;
                break;
            case '2':
                http10=2;
                break;
            case 'V':
                printf(PROGRAM_VERSION"\n");
                exit(0);
            case 't':
                benchtime=atoi(optarg);
                break;
            case 'p':
                /* ������������� :������ʽΪserver:port */
                tmp=strrchr(optarg,':');
                proxyhost=optarg;
                if(tmp==NULL)�� { //û�ҵ�:
                    break;
                }
                if(tmp==optarg) { //ȱ����������ֻ�ṩ��:port
                    fprintf(stderr,"Error in option --proxy %s: Missing hostname.\n",optarg);
                    return 2;
                }
                if(tmp==optarg+strlen(optarg)-1) { //ȱ�ٶ˿ںš�ֻ�ṩ��server:
                    fprintf(stderr,"Error in option --proxy %s Port number is missing.\n",optarg);
                    return 2;
                }
                *tmp='\0';
                proxyport=atoi(tmp+1);
                break;
            case ':':
            case 'h':
            case '?':
                usage();
                return 2;
                break;
            case 'c':
                clients=atoi(optarg);
                break;
        }
    }

    if(optind==argc) {//������ȱ��URL
        fprintf(stderr,"webbench: Missing URL!\n");
        usage();
        return 2;
    }

    if(clients==0) clients=1;
    if(benchtime==0) benchtime=60;

    /* ���Copyright��Ȩ��Ϣ */
    fprintf(stderr,"Webbench - Simple Web Benchmark "PROGRAM_VERSION"\n"
            "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n"
           );

    build_request(argv[optind]); //��������

    /* print bench info */
    printf("\nBenchmarking: ");
    switch(method) {
        case METHOD_GET:
        default:
            printf("GET");
            break;
        case METHOD_OPTIONS:
            printf("OPTIONS");
            break;
        case METHOD_HEAD:
            printf("HEAD");
            break;
        case METHOD_TRACE:
            printf("TRACE");
            break;
    }
    printf(" %s",argv[optind]);
    switch(http10) {
        case 0:
            printf(" (using HTTP/0.9)");
            break;
        case 2:
            printf(" (using HTTP/1.1)");
            break;
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
    return bench(); //��ʼѹ������
}


//��������
void build_request(const char *url)
{
    char tmp[10];
    int i;

    bzero(host,MAXHOSTNAMELEN);
    bzero(request,REQUEST_SIZE);

    if(force_reload && proxyhost!=NULL && http10<1) http10=1;
    if(method==METHOD_HEAD && http10<1) http10=1;
    if(method==METHOD_OPTIONS && http10<2) http10=2;
    if(method==METHOD_TRACE && http10<2) http10=2;

    switch(method) {
        default:
        case METHOD_GET:
            strcpy(request,"GET");
            break;
        case METHOD_HEAD:
            strcpy(request,"HEAD");
            break;
        case METHOD_OPTIONS:
            strcpy(request,"OPTIONS");
            break;
        case METHOD_TRACE:
            strcpy(request,"TRACE");
            break;
    }

    strcat(request," ");

    if(NULL==strstr(url,"://")) {
        fprintf(stderr, "\n%s: is not a valid URL.\n",url);
        exit(2);
    }
    if(strlen(url)>1500) {
        fprintf(stderr,"URL is too long.\n");
        exit(2);
    }
    if(proxyhost==NULL)
        if (0!=strncasecmp("http://",url,7)) { //strncasecmp()�����Ƚϲ���s1��s2�ַ���ǰn���ַ����Ƚ�ʱ���Զ����Դ�Сд�Ĳ���
            fprintf(stderr,"\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
            exit(2);
        }
    /* protocol/host delimiter */
    i=strstr(url,"://")-url+3;  //�ȵ�URL��Э���������ֽ�����±ꡣ��urlΪhttp://www.baidu.comʱִ�к�i=7
    /* printf("%d\n",i); */

    if(strchr(url+i,'/')==NULL) { //URL������Ҫ��'/'��β
        fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
        exit(2);
    }
    if(proxyhost==NULL) {
        /* get port from hostname */
        //index�����ַ�����ĳ���ַ��״γ��ֵ�λ��
        if(index(url+i,':')!=NULL &&
           index(url+i,':')<index(url+i,'/')) {
            strncpy(host,url+i,strchr(url+i,':')-url-i); //��ȡ������
            bzero(tmp,10);
            strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);//�õ��ַ�����ʽ�Ķ˿�
            /* printf("tmp=%s\n",tmp); */
            proxyport=atoi(tmp);
            if(proxyport==0) proxyport=80;
        } else {
            strncpy(host,url+i,strcspn(url+i,"/")); //strcspn(s, reject)�����ַ���s ��ͷ�����ж��ٸ��ַ������ַ���reject ��
        }
        // printf("Host=%s\n",host);
        strcat(request+strlen(request),url+i+strcspn(url+i,"/"));
    } else {
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
    if(proxyhost==NULL && http10>0) {
        strcat(request,"Host: ");
        strcat(request,host);
        strcat(request,"\r\n");
    }
    if(force_reload && proxyhost!=NULL) {
        strcat(request,"Pragma: no-cache\r\n");
    }
    if(http10>1)
        strcat(request,"Connection: close\r\n");
    /* add empty line at end */
    if(http10>0) strcat(request,"\r\n");
    // printf("Req=%s\n",request);
}

/* vraci system rc error kod */
static int bench(void)
{
    int i,j,k;
    pid_t pid=0;
    FILE *f;

    /* ���Ŀ��������Ŀ�����,���������,��̸���Ͻ�����ѹ�Բ���*/
    i=Socket(proxyhost==NULL?host:proxyhost,proxyport);
    if(i<0) {
        fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
        return 1;
    }
    close(i);

    /* �����ܵ� */
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

    /* �����ӽ��� */
    for(i=0; i<clients; i++) {
        pid=fork();
        if(pid <= (pid_t) 0) { //�ӽ��̴�������
            /* child process or error*/
            sleep(1); /* make childs faster */
            break;
        }
    }

    if( pid< (pid_t) 0) {
        fprintf(stderr,"problems forking worker no. %d\n",i);
        perror("fork failed.");
        return 3;
    }

    //���ӽ���ͨ���ܵ�ͨ��
    if(pid== (pid_t) 0) { //�ӽ���
        /* I am a child */
        if(proxyhost==NULL)
            benchcore(host,proxyport,request); //����
        else
            benchcore(proxyhost,proxyport,request);

        /* ���д��ܵ� */
        f=fdopen(mypipe[1],"w");
        if(f==NULL) {
            perror("open pipe for writing failed.");
            return 3;
        }
        /* fprintf(stderr,"Child - %d %d\n",speed,failed); */
        fprintf(f,"%d %d %d\n",speed,failed,bytes);
        fclose(f);
        return 0;
    } else { //������
        f=fdopen(mypipe[0],"r");
        if(f==NULL) {
            perror("open pipe for reading failed.");
            return 3;
        }
        setvbuf(f,NULL,_IONBF,0); //ֱ�Ӵ����ж������ݻ�ֱ��������д�����ݣ������û�����
        speed=0;
        failed=0;
        bytes=0;

		//ͳ���ӽ��̶���վ�Ĳ�������������̶�ͨ���ܵ���ȡ�ӽ��̴��ݵ���Ϣ��
        while(1) {
            pid=fscanf(f,"%d %d %d",&i,&j,&k); //�ӹܵ��ļ��л�ȡ����
            if(pid<2) {
                fprintf(stderr,"Some of our childrens died.\n");
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

//��ɶ���վѹ������
void benchcore(const char *host,const int port,const char *req)
{
    int rlen;
    char buf[1500];
    int s,i;
    struct sigaction sa;

    /* ���ñ����źŴ������ */
    sa.sa_handler=alarm_handler;
    sa.sa_flags=0;
    if(sigaction(SIGALRM,SIGALRM,NULL)) //���ź�SIGALRM�����µ��źŴ���
        exit(3);
    alarm(benchtime); //���ý���˽�����ӣ�ʱ��һ����ʱ�Ӿͷ���һ���ź�SIGALRM�����̡�

    rlen=strlen(req);
nexttry:
    while(1) {
        if(timerexpired) {//�жϲ���ʱ�䵽��û
            if(failed>0) {
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
		//ͨ���򿪵��׽�����������������
        if(rlen!=write(s,req,rlen)) {//����ʧ��
            failed++;
            close(s);
            continue;
        }
        if(http10==0)
			//��ֹ���׽ӿ��Ͻ������ݵĽ����뷢��
            if(shutdown(s,SHUT_WR)) {
                failed++;
                close(s);
                continue;
            }
        if(force==0) {
            /* ���׽��ֶ�ȡ���п��õ����� */
            while(1) {
                if(timerexpired) break;
                i=read(s,buf,1500);
                /* fprintf(stderr,"%d\n",i); */
                if(i<0) {//�׽��ֽ�������ʧ��
                    failed++;
                    close(s);
                    goto nexttry;
                } else if(i==0) break;
                else
                    bytes+=i;
            }
        }
        if(close(s)) {//�ر��׽���ʧ��
            failed++;
            continue;
        }
        speed++;
    }
}
