/**
 * 服务器主入口程序
 * 模拟Proactor模式
 * 1.主线程往epoll内核事件表中注册scoket上的读就绪事件
 * 2.主线程调用epoll_wait等待socket上有数据可读
 * 3.当socket上有数据可读时，epoll_wait通知主线程；主线程从socket循环读取数据，直到没有更多数据可读，然后将读取到的数据封装成一个请求对象并插入请求队列
 * 4.睡眠在请求队列上的某个工作线程被唤醒，它获得请求对象，并处理客户请求，然后往epoll内核时间表中注册该socket上的写就绪事件
 * 5.主线程调用epoll_wait等待socket可写
 * 6.当socket可写时， epoll_wait通知主线程；主线程往socket上写入服务器处理客户请求的结果
**/

#include "headers/thread_pool.h"
#include "headers/sockio.h"

int main(int argc, char* argv[]) {
    // 设置端口
    int port;    
    if(argc <= 1) {
        port = 80;
        printf("waring:80端口，请务必使用超级用户权限启动程序\n");
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