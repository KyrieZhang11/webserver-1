/**
 * 主函数流程
 * 1. 设置服务器端口
 * 2. 设置SIGPIPE信号处理， 防止客户端断开连接导致服务器写入时进程终止
 * 3. 初始化线程池
 * 4. 设置监听socket（初始化，绑定，监听，接收）
 * 5. 注册epoll对象，设置事件处理
**/

#include "headers/thread_pool.h"
#include "headers/sockio.h"

int main(int argc, char* argv[]) {
    // 设置端口
    int port;    
    if(argc <= 1) {
        port = 10000;
    } else{
        port = atoi(argv[1]);
    }
    addsig( SIGPIPE, SIG_IGN );
    // 初始化线程池
    Threadpool< http_conn >* pool = NULL;
    try {
        pool = new Threadpool<http_conn>;
    } catch( ... ) {
        return 1;
    }
    // 设置监听socket
    int listenfd = socket( AF_INET, SOCK_STREAM, 0 );
    setsocket(listenfd, port);
    // 主线程任务
    do_process(pool, listenfd);
    close( listenfd );
    delete pool;
    return 0;
}