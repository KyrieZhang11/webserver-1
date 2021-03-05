#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H    1

#include <sys/types.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include "thread_pool.h"
#include "sockio.h"

class http_conn
{
public:
    http_conn(){}
    ~http_conn(){}
    void init(int sockfd);                                  // 初始化新接受的连接
    void close_conn();                                      // 关闭连接
    bool read();                                            // 非阻塞读
    bool write();                                           // 非阻塞写, 写HTTP响应
    void process();                                         // 响应和处理客户端请求, 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
public:
    static const int    FILENAME_LEN       = 200;           // 文件名的最大长度
    static const int    READ_BUFFER_SIZE   = 2048;          // 读缓冲区的大小
    static const int    WRITE_BUFFER_SIZE  = 1024;          // 写缓冲区的大小
    static int          epollfd;                            // 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
    static int          user_count;                         // 统计用户的数量
private:
    int                 sockfd;                             // 该HTTP连接的socket和对方的socket地址
    char                read_buf[ READ_BUFFER_SIZE ];       // 读缓冲区
    int                 read_index;                         // 标识读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置
    int                 checked_index;                      // 当前正在分析的字符在读缓冲区中的位置
    int                 start_line;                         // 当前正在解析的行的起始位置
    char                real_file[ FILENAME_LEN ];          // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    char*               url;                                // 客户请求的目标文件的文件名
    char*               version;                            // HTTP协议版本号，我们仅支持HTTP1.1
    char*               host;                               // 主机名
    int                 content_length;                     // HTTP请求的消息总长度
    bool                linger;                             // HTTP请求是否要求保持连接
    char                write_buf[ WRITE_BUFFER_SIZE ];     // 写缓冲区
    int                 write_index;                        // 写缓冲区中待发送的字节数
    char*               file_address;                       // 客户请求的目标文件被mmap到内存中的起始位置
    struct stat         file_stat;                          // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct iovec        iv[2];                              // 我们将采用writev来执行写操作，所以定义下面两个成员
    int                 iv_count;                           // 被写内存块的数量
private:    
    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    
    /* 
        从状态机的三种可能状态，即行的读取状态，分别表示
        LINE_OK             ：  读取到一个完整的行
        LINE_BAD            :   行出错
        LINE_OPEN           :   行数据尚且不完整,没有遇到结束符
    */
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };
private:
    void init();                                            // 初始化连接
    HTTP_CODE process_read();                               // 主状态机，解析HTTP请求
    /* 下面这一组函数被process_read调用以分析HTTP请求
    */
    HTTP_CODE parse_request_line( char* text );             // 解析HTTP请求行，获得请求方法，目标URL,以及HTTP版本号
    HTTP_CODE parse_headers( char* text );                  // 解析HTTP请求的一个头部信息
    HTTP_CODE parse_content( char* text );                  // 解析HTTP请求体，判断它是否被完整的读入了
    LINE_STATUS parse_line();                               // 解析一行，判断依据\r\n
    char* get_line() { return read_buf + start_line; }      // 获取一行
    HTTP_CODE do_request();

    bool process_write( HTTP_CODE ret );                    // 填充HTTP应答, 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
    // 这一组函数被process_write调用以填充HTTP应答。
    bool add_response( const char* format, ... );           // 往写缓冲中写入待发送的数据
    bool add_content( const char* content );
    bool add_content_type();
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();
    void unmap();                                           // 对内存映射区执行munmap操作
private:
    CHECK_STATE check_state;                                // 主状态机当前所处的状态
    METHOD      method;                                     // 请求方法
};

#endif
