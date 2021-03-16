/**
 * 常用的事件处理函数
 * 对文件描述符的操作
 * 信号捕捉
 * 主线程逻辑
 */

#ifndef SOCKIO_H
#define SOCKIO_H    1

#include <signal.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include "http_conn.h"

#define MAX_FD              65536       // 最大的文件描述符个数
#define MAX_EVENT_NUMBER    10000       // 监听的最大的事件数量

// 设置文件描述符非阻塞
void setnonblocking( int fd );
// 向epoll中添加需要监听的文件描述符
void addfd( int epollfd, int fd, bool one_shot );
// 从epoll中移除监听的文件描述符
void removefd( int epollfd, int fd );
// 修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev);
// 添加信号捕捉
void addsig(int sig, void( handler )(int));

// 实现对socket的绑定和监听,在绑定之前设置了端口复用
void setsocket(int listenfd, int port);

// 主线程处理
/**
 * 1. 创建具有最大连接数的users数组用于保存所有的http连接
 * 2. 创建epoll对象和具有最大监听数的event事件数组
 * 3. 将listenfd添加到epoll对象中进行监听
 * 4. 对所有监听到的文件描述符进行处理
 *      1. 如果监听到到的文件描述符是listenfd，说明有新的客户端连接，建立sockaddr_in地址，使用accept接收新的连接，并将新socket的文件描述符加入epoll中监听
 *      2. 如果检测到读事件，读取数据后将http连接对象放入请求队列中
 *      3. 如果检测到写事件，将事件写回客户端
**/
void do_process(void *pool, int listenfd);
#endif