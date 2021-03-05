#include "headers/sockio.h"

void setnonblocking( int fd ) {
    int flags = fcntl( fd, F_GETFL );
    flags |= O_NONBLOCK;
    fcntl( fd, F_SETFL, flags);
}

void addfd( int epollfd, int fd, bool one_shot ) {
    epoll_event event;
    event.data.fd = fd;
    // 在使用 2.6.17 之后版本内核的服务器系统中，对端连接断开触发的 epoll 事件会包含 EPOLLIN | EPOLLRDHUP
    event.events = EPOLLIN | EPOLLRDHUP;
    if(one_shot) 
    {
        // 防止epoll触发多次
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    
    setnonblocking(fd);  
}

void removefd( int epollfd, int fd ) {
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close(fd);
}

void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

void addsig(int sig, void(*handler )(int)){
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

void setsocket(int listenfd, int port)
{
    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons( port );

    // 端口复用
    int reuse = 1;
    setsockopt( listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    ret = listen( listenfd, 5 );
}

void do_process(void *arg, int listenfd)
{
    Threadpool< http_conn >* pool = (Threadpool< http_conn >*)arg;
    http_conn* users = new http_conn[ MAX_FD ];
    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 1 );
    addfd( epollfd, listenfd, false );
    http_conn::epollfd = epollfd;
    while(true) {    
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );        
        if ( ( number < 0 ) && ( errno != EINTR ) ) {
            printf( "epoll failure\n" );
            break;
        }
        // 循环遍历事件数组
        for ( int i = 0; i < number; i++ ) {
            int sockfd = events[i].data.fd; 
            if( sockfd == listenfd ) {
                // 有客户端连接进来    
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                if ( connfd < 0 ) {
                    printf( "errno is: %d\n", errno );
                    continue;
                } 
                if( http_conn::user_count >= MAX_FD ) {
                    close(connfd);
                    continue;
                }
                users[connfd].init(connfd);
            } else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) ) {
                users[sockfd].close_conn();
            } else if(events[i].events & EPOLLIN) {
                if(users[sockfd].read()) {
                    // 一次性把所有数据都读完
                    pool->append_request(users + sockfd);
                } else {
                    users[sockfd].close_conn();
                }
            }  else if( events[i].events & EPOLLOUT ) {

                if( !users[sockfd].write() ) {
                    // 一次性写完所有数据
                    users[sockfd].close_conn();
                }

            }
        }
    }
    close( epollfd );
    delete []users;
}