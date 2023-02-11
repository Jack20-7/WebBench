#include "socket.h"

#include <sys/param.h>
#include <rpc/types.h>
#include <getopt.h>
#include <strings.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <stdbool.h>

// http://47.97.102.181:8020/html/index.html

volatile int timerexpired = 0;
int speed = 0;
int failed = 0;
int bytes = 0;


//全局数据
/*http1.0 - 1   http0.9 - 0    http1.1 - 2*/
int http10 = 1;
#define METHOD_GET            0
#define METHOD_HEAD           1
#define METHOD_OPTIONS        2
#define METHOD_TRACE          3
#define PROGRAM_VERSION       "1.5"
int method = METHOD_GET;
int clients = 1;              //客户端数量
int force = 0;                
int force_reload = 0;
int proxyport = 80;           
char* proxyhost = NULL;       //代理服务器域名
int benchtime = 30;           //持续时间

bool keep_alive = false;      //是否保持长连接

//internal
int mypipe[2];                    //用于创建的子进程和父进程进行通信
char host[MAXHOSTNAMELEN];        //存放传入的目标域名
#define  REQUEST_SIZE   2048
char request[REQUEST_SIZE];       //存放生成的请求信息

//在调用getopt_long的时候,对于option结构体中的第三个值，如果 = NULL,那么会将第四个值返回
//如果!= NULL,那么会将第四个数的值填入到第三个参数中去
//注意，该结构体数组针对的是长选项
static const struct option long_options[] = {
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

//创建的子进程执行的函数
static void benchcore(const char* host,const int port,const char* request);
static int bench(void);
static void build_request(const char* url);  //根据命令行信息构造要发送的HTTP请求

//超时之后会被出发的函数
static void alarm_handler(int signal){
    timerexpired = 1;
}

static void usage(){
    fprintf(stderr,
            "webbench  [option]... URL\n"
            "   -f| --force                Don't wait for reply from server.\n"
            "   -r| --reload               Send reload request -Pragma:no-cache.\n"
            "   -t| --time<set>            Run benchmark for <sec> seconds.Default 30.\n"
            "   -p| --proxy<server:port>   Use proxy server for request.\n"
            "   -c| --clients<n>           Run <n> HTTP clients at once.Default one.\n"
            "   -k| --keep                 Keep-Alive.\n"
            "   -9| --http09               Use HTTP/0.9 protocol.\n"
            "   -1| --http10               Use HTTP/1.0 protocol.\n"
            "   -2| --http11               Use HTTP/1.1 protocol.\n"
            "       --get                  Use GET request method.\n"
            "       --head                 Use HEAD request method.\n"
            "       --options              Use OPTIONS request method.\n"
            "       --trace                Use TRACE request method.\n"
            "       -?|-h|--help           This information.\n"
            "       -V|--version           Display program version.\n"
            );
}


int main(int argc,char** argv){
    int opt = 0;
    int options_index = 0;
    char* tmp = NULL;
    if(argc == 1){ 
        //没传入参数
        usage();
        return 2;
    }

    //传入的第三个参数针对的是短选项，第四个参数针对的是长选项
    while((opt = getopt_long(argc,argv,"912vfrt:p:c:?hk",long_options,&options_index)) != EOF){
        switch(opt){
            case 0  :
                break;
            case 'f':
                force = 1;
                break;
            case 'r':
                 force_reload = 1;
                 break;
            case '9':
                 http10 = 0;
            case '1':
                 http10 = 1;
                 break;
            case '2':
                 http10 = 2;
                 break;
            case 'V':
                 printf(PROGRAM_VERSION"\n");
                 exit(0);
            case 't':
                 benchtime = atoi(optarg);
                 break;
            case 'k':
                 keep_alive = true;
                 break;
            case 'p':
                 {
                     //strrchr查询的是最后一次出现的位置
                     //strchr查询的是第一次出现额位置
                     tmp = strrchr(optarg,':');
                     proxyhost = optarg;
                     if(tmp == NULL){
                         break;
                     }
                     if(tmp == optarg){
                         fprintf(stderr,"Error in option --proxy %s:Missing hostname.\n",optarg);
                         return 2;
                     }
                     if(tmp == optarg + strlen(optarg) - 1){
                         fprintf(stderr,"Error in option --proxy %s: Port Number is missing.\n",optarg);
                         return 2;
                     }

                     *tmp = '\0';
                     proxyport = atoi(tmp + 1);
                 }
                 break;
            case ':':
            case 'h':
            case '?':
                 usage();
                 return 2;
            case 'c':
                 clients = atoi(optarg);
                 break;
        }
    }

    //optind表示的就是下一个要被处理的命令行参数在argv中的下表值
    if(optind == argc){
        fprintf(stderr,"webbench:Missing URL!\n");
        usage();
        return 2;
    }

    if(clients == 0){
        clients = 1;
    }
    if(benchtime == 0){
        benchtime = 30;
    }

    fprintf(stderr,"Webbench - Simple Web BenchMark "PROGRAM_VERSION"\n");

    //生成要发送的报文
    build_request(argv[optind]);

    printf("Running info:");
    if(clients == 1){
        printf("1 client");
    }else{
        printf("%d clients",clients);
    }
    printf(",running %d sec",benchtime);

    if(force){
        printf("early socket close");
    }else if(proxyhost != NULL){
        printf(",via proxy server %sL%d",proxyhost,proxyport);
    }
    if(force_reload){
        printf(",forcing reload");
    }
    printf(".\n");

    return bench();
}

//生成要发送的HTTP请求报文
void build_request(const char* url){
    char tmp[10];
    int i = 0;

    memset(host,0,MAXHOSTNAMELEN);
    memset(request,0,REQUEST_SIZE);

    if(force_reload && proxyhost != NULL && http10 < 1){
        http10 = 1;
    }
    if(method == METHOD_HEAD && http10 < 1){
        http10 = 1;
    }
    if(method == METHOD_OPTIONS && http10 < 2){
        http10 = 2;
    }
    if(method == METHOD_TRACE && http10 < 2){
        http10 = 2;
    }

    switch(method){
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
        default:
              break;
    }

    strcat(request," ");

    if(NULL == strstr(url,"://")){
        fprintf(stderr,"\n%s: is not a valid URL.\n",url);
        exit(2);
    }

    if(strlen(url) > 1500){
        fprintf(stderr,"URL is too long.\n");
        exit(2);
    }
    if(0 != strncasecmp("http://",url,7)){
        fprintf(stderr,"\nOnly HTTP protocol is directly supported,set --proxy for others.\n");
        exit(2);
    }

    //http://47.97.102.181:8020/html/index.html
    i = strstr(url,"://") - url + 3;
    if(strchr(url + i,'/') == NULL){
        fprintf(stderr,"\nInvalid URL syntax - hostname dont't ends with '/'.\n");
        exit(2);
    }

    if(proxyhost == NULL){
        if(index(url + i,':') != NULL && index(url + i,':') < index(url + i,'/')){
            strncpy(host,url + i,strchr(url + i,':') - url - i); //将域名到:之间的那一段拷贝到host里面去
            memset(tmp,0,10);
            strncpy(tmp,index(url + i,':') + 1,strchr(url + i,'/') - index(url + i,':') - 1);//拷贝port
            proxyport = atoi(tmp);
            if(proxyport == 0){
                proxyport = 80;  //默认是80端口
            }
        }else{
            //没有携带端口信息，所以只拷贝host
            strncpy(host,url + i,strcspn(url + i,"/"));
        }
        strcat(request + strlen(request),url + i + strcspn(url + i,"/"));
    }else{
        printf("ProxyHost = %s\nProxyPort=%d\n",proxyhost,proxyport);
        strcat(request,url);
    }

    if(http10 == 1){
        strcat(request," HTTP/1.0");
    }else if(http10 == 2){
        strcat(request," HTTP/1.1");
    }
    strcat(request,"\r\n");

    //拼接完成的请求行

    if(http10 > 0){
        strcat(request,"User-Agent:WebBench "PROGRAM_VERSION"\r\n");
    }

    if(proxyhost == NULL && http10 > 0){
        strcat(request,"Host: ");
        strcat(request,host);
        strcat(request,"\r\n");
    }

    if(force_reload && proxyhost != NULL){
        strcat(request,"Pragma:no-cache\r\n");
    }


    if(http10 > 1){
        if(!keep_alive){
            strcat(request,"Connection: close\r\n");
        }else{
            strcat(request,"Connection: Keep-Alive\r\n");
        }
    }

    if(http10 > 0){
        strcat(request,"\r\n");
    }

    printf("\nRequest:\n%s\n",request);
}


static int bench(){
    int i , j , k;
    pid_t pid = 0;
    FILE* f;
    i = Socket(proxyhost == NULL ? host:proxyhost,proxyport);
    if(i < 0){
        fprintf(stderr,"\nConnect to server failed.Aborting benchmark.\n");
        return 1;
    }
    close(i);

    if(pipe(mypipe)){
        perror("pipe failed.");
        return 3;
    }

    for(i = 0;i < clients;++i){
        pid = fork();
        if(pid <= 0){
            //子进程
            sleep(1);
            break;
        }
    }

    if(pid < 0){
        fprintf(stderr,"problems forking worker no.%d\n",i);
        perror("fork failed.");
        return 3;
    }

    if(pid == 0){
        //子进程会进入
        if(proxyhost == NULL){
            benchcore(host,proxyport,request);
        }else{
            benchcore(proxyhost,proxyport,request);
        }

        //父子进程应该都只保留管道的一个口才对
        close(mypipe[0]);
        f = fdopen(mypipe[1],"w");
        if(f == NULL){
            perror("open pipe for writing failed.");
            return 3;
        }
        fprintf(f,"%d %d %d\n",speed,failed,bytes);
        fclose(f);

        return 0;
    }else{
        close(mypipe[1]);
        //父进程
        f = fdopen(mypipe[0],"r");
        if(f == NULL){
            perror("open pipe for reading failed.");
            return 3;
        }
        setvbuf(f,NULL,_IONBF,0);

        speed = 0;       //每完成一次交互就++
        failed = 0;      //发送的过程中出现异常的次数
        bytes = 0;       //目前服务器读取到的字节数

        while(1){
            pid = fscanf(f,"%d %d %d",&i,&j,&k);
            if(pid < 2){
                fprintf(stderr,"Some of our childrens died.\n");
                break;
            }
            speed += i;
            failed += j;
            bytes += k;

            if(--clients == 0){
                break;
            }
        }
        fclose(f);
        //speed:每完成一次HTTP交互就++
        printf("\nSpeed=%d pages/min,%d bytes/sec.\nRequest:%d susceed,%d failed.\n",
                     (int)((speed + failed) / (benchtime / 60.0f)),
                     (int)(bytes/(float)benchtime),
                     speed,
                     failed);
    }
    return i;
}

//该函数就是用来向指定ip + port的服务器发送http请求报文
void benchcore(const char* host,const int port,const char* req){
    int rlen;
    char buf[1500];
    int s,i;
    struct sigaction sa;

    sa.sa_handler = alarm_handler;
    sa.sa_flags = 0;
    if(sigaction(SIGALRM,&sa,NULL)){
        exit(3);
    }
    alarm(benchtime);

    rlen = strlen(req);

    if(keep_alive){
        while((s = Socket(host,port)) == -1);
nextry1:
        while(1){
            if(timerexpired){
                if(failed > 0){
                    //该failed是由于 信号造成的，所以需要减去
                    failed--;
                }
                return ;
            }
            if(s < 0){
                failed++;
                continue;
            }
            if(rlen != write(s,req,rlen)){
                failed++;
                close(s);
                while((s = Socket(host,port)) == -1);
                continue;
            }

            if(force == 0){
                while(1){
                    if(timerexpired){
                        break;
                    }
                    i = read(s,buf,1500);
                    if(i < 0){
                        failed++;
                        close(s);
                        goto nextry1;
                    }else if(i == 0){
                        break;
                    }else{
                        bytes += i;
                        break;
                    }
                }
            }
            speed++;
        }
    }else{
nextry:
        while(1){
            if(timerexpired){
                if(failed > 0){
                    failed--;
                }
                return;
            }
            s = Socket(host,port);
            if(s < 0){
                failed++;
                continue;
            }

            if(rlen != write(s,req,rlen)){
                failed++;
                close(s);
                continue;
            }

            if(http10 == 0){
                if(shutdown(s,1)){
                    failed++;
                    continue;
                }
            }

            if(force == 0){
                while(1){
                    if(timerexpired){
                        break;
                    }
                    i = read(s,buf,1500);
                    if(i < 0){
                        failed++;
                        close(s);
                        goto nextry;
                    }else{
                        if(i == 0){
                            break;
                        }else{
                            bytes += i;
                        }
                    }
                }
            }
            //不是keep-alive，所以完成一次发送接收就关闭连接
            if(close(s)){
                failed++;
                continue;
            }
            speed++;
        }
    }

}