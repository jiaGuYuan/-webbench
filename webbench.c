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
 * 返回码:
 *    0 - 成功
 *    1 - 测试失败(服务器不在线)
 *    2 - 错误的参数
 *    3 - 内部错误,fork失败
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
volatile int timerexpired=0; //是否到期
　//定时器中止
int speed=0;	//测试过程中传输的page数
int failed=0;	//测试过程中请求失败的次数
int bytes=0;	//测试过程中传输的字节数
/* globals */
int http10=1; /*指定所使用的http协议 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define PROGRAM_VERSION "1.5" //版本号
int method=METHOD_GET; //请求方法 默认为GET

int clients=1;	//同时连接到服务器的客户端个数
int force=0;	//当提供了force选项时该变量为１
int force_reload=0;
int proxyport=80;
char *proxyhost=NULL;
int benchtime=30;
　//测试时长(秒)
/* internal */
int mypipe[2];
char host[MAXHOSTNAMELEN];
#define REQUEST_SIZE 2048
char request[REQUEST_SIZE];

//命令行长配置选项---这个结构体在处理命令行选项参数的库函数getopt_long()中用到
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
    {NULL,0,NULL,0} //以它作为结束
};

/* 函数原型 */
static void benchcore(const char* host,const int port, const char *request);
static int bench(void);
static void build_request(const char *url);

static void alarm_handler(int signal)
{
    timerexpired=1;
}

//显示帮助信息
//webbench 执行的选项说明。-c 并发数， -t 运行测试时间，。。。
static void usage(void)
{
    fprintf(stderr,
            "webbench [option]... URL\n"
            "  -f|--force               Don't wait for reply from server.\n"	//不等待服务器的回复
            "  -r|--reload              Send reload request - Pragma: no-cache.\n"　//发送重新加载请求-参数:no - cache
            "  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"	//测试程序运行时间<秒>。默认30秒
            "  -p|--proxy <server:port> Use proxy server for request.\n"	//使用proxy服务器的请求
            "  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"	//指定n个HTTP客户端同时运行。默认运行一个
            "  -9|--http09              Use HTTP/0.9 style requests.\n"	//使用HTTP / 0.9样式的请求
            "  -1|--http10              Use HTTP/1.0 protocol.\n"	//使用HTTP / 1.0协议
            "  -2|--http11              Use HTTP/1.1 protocol.\n"	//使用HTTP / 1.1协议
            "  --get                    Use GET request method.\n"	//使用GET请求方法。
            "  --head                   Use HEAD request method.\n"	//使用HEAD请求方法
            "  --options                Use OPTIONS request method.\n"	//使用OPTIONS请求方法
            "  --trace                  Use TRACE request method.\n"	//使用TRACE请求方法
            "  -?|-h|--help             This information.\n"	//显示帮助信息
            "  -V|--version             Display program version.\n"　//显示版本信息
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

//getopt_long()处理命令行选项参数。
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
                /* 代理服务器解析 :参数格式为server:port */
                tmp=strrchr(optarg,':');
                proxyhost=optarg;
                if(tmp==NULL)　 { //没找到:
                    break;
                }
                if(tmp==optarg) { //缺少主机名。只提供了:port
                    fprintf(stderr,"Error in option --proxy %s: Missing hostname.\n",optarg);
                    return 2;
                }
                if(tmp==optarg+strlen(optarg)-1) { //缺少端口号。只提供了server:
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

    if(optind==argc) {//参数中缺少URL
        fprintf(stderr,"webbench: Missing URL!\n");
        usage();
        return 2;
    }

    if(clients==0) clients=1;
    if(benchtime==0) benchtime=60;

    /* 输出Copyright版权信息 */
    fprintf(stderr,"Webbench - Simple Web Benchmark "PROGRAM_VERSION"\n"
            "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n"
           );

    build_request(argv[optind]); //构建请求

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
    return bench(); //开始压力测试
}


//构建请求
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
        if (0!=strncasecmp("http://",url,7)) { //strncasecmp()用来比较参数s1和s2字符串前n个字符，比较时会自动忽略大小写的差异
            fprintf(stderr,"\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
            exit(2);
        }
    /* protocol/host delimiter */
    i=strstr(url,"://")-url+3;  //等到URL中协议与主机分界符的下标。如url为http://www.baidu.com时执行后i=7
    /* printf("%d\n",i); */

    if(strchr(url+i,'/')==NULL) { //URL主机名要以'/'结尾
        fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
        exit(2);
    }
    if(proxyhost==NULL) {
        /* get port from hostname */
        //index查找字符串中某个字符首次出现的位置
        if(index(url+i,':')!=NULL &&
           index(url+i,':')<index(url+i,'/')) {
            strncpy(host,url+i,strchr(url+i,':')-url-i); //获取主机名
            bzero(tmp,10);
            strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);//得到字符串形式的端口
            /* printf("tmp=%s\n",tmp); */
            proxyport=atoi(tmp);
            if(proxyport==0) proxyport=80;
        } else {
            strncpy(host,url+i,strcspn(url+i,"/")); //strcspn(s, reject)返回字符串s 开头连续有多少个字符不在字符串reject 内
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

    /* 检查目标服务器的可用性,如果连不上,则谈不上进行耐压性测试*/
    i=Socket(proxyhost==NULL?host:proxyhost,proxyport);
    if(i<0) {
        fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
        return 1;
    }
    close(i);

    /* 创建管道 */
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

    /* 创建子进程 */
    for(i=0; i<clients; i++) {
        pid=fork();
        if(pid <= (pid_t) 0) { //子进程创建出错
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

    //父子进程通过管道通信
    if(pid== (pid_t) 0) { //子进程
        /* I am a child */
        if(proxyhost==NULL)
            benchcore(host,proxyport,request); //测试
        else
            benchcore(proxyhost,proxyport,request);

        /* 结果写入管道 */
        f=fdopen(mypipe[1],"w");
        if(f==NULL) {
            perror("open pipe for writing failed.");
            return 3;
        }
        /* fprintf(stderr,"Child - %d %d\n",speed,failed); */
        fprintf(f,"%d %d %d\n",speed,failed,bytes);
        fclose(f);
        return 0;
    } else { //父进程
        f=fdopen(mypipe[0],"r");
        if(f==NULL) {
            perror("open pipe for reading failed.");
            return 3;
        }
        setvbuf(f,NULL,_IONBF,0); //直接从流中读入数据或直接向流中写入数据，不设置缓冲区
        speed=0;
        failed=0;
        bytes=0;

		//统计子进程对网站的测试情况。父进程读通过管道获取子进程传递的信息　
        while(1) {
            pid=fscanf(f,"%d %d %d",&i,&j,&k); //从管道文件中获取数据
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

//完成对网站压力测试
void benchcore(const char *host,const int port,const char *req)
{
    int rlen;
    char buf[1500];
    int s,i;
    struct sigaction sa;

    /* 设置报警信号处理程序 */
    sa.sa_handler=alarm_handler;
    sa.sa_flags=0;
    if(sigaction(SIGALRM,SIGALRM,NULL)) //给信号SIGALRM设置新的信号处理
        exit(3);
    alarm(benchtime); //设置进程私有闹钟，时间一到，时钟就发送一个信号SIGALRM到进程。

    rlen=strlen(req);
nexttry:
    while(1) {
        if(timerexpired) {//判断测试时间到了没
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
		//通过打开的套接字描述符发送数据
        if(rlen!=write(s,req,rlen)) {//发送失败
            failed++;
            close(s);
            continue;
        }
        if(http10==0)
			//禁止在套接口上进行数据的接收与发送
            if(shutdown(s,SHUT_WR)) {
                failed++;
                close(s);
                continue;
            }
        if(force==0) {
            /* 从套接字读取所有可用的数据 */
            while(1) {
                if(timerexpired) break;
                i=read(s,buf,1500);
                /* fprintf(stderr,"%d\n",i); */
                if(i<0) {//套接字接收数据失败
                    failed++;
                    close(s);
                    goto nexttry;
                } else if(i==0) break;
                else
                    bytes+=i;
            }
        }
        if(close(s)) {//关闭套接字失败
            failed++;
            continue;
        }
        speed++;
    }
}
