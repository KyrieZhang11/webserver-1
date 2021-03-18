#include "headers/http_conn.h"
#include "headers/sockio.h"

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";
// 网站的根目录
const char* doc_root = "/home/fine/webserver/resources";
// 所有的客户数
int http_conn::user_count = 0;
// 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
int http_conn::epollfd = -1;

// 关闭连接
void http_conn::close_conn() {
    if(sockfd != -1) {
        removefd(epollfd, sockfd);
        sockfd = -1;
        user_count--;
    }
}

// 初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd){
    this->sockfd = sockfd;
    addfd( epollfd, sockfd, true );
    user_count++;
    init();
}

void http_conn::init()
{
    check_state = CHECK_STATE_REQUESTLINE;      // 初始状态为检查请求行
    linger = false;                             // 默认不保持链接  Connection : keep-alive保持连接
    method = GET;                               // 默认请求方式为GET
    url = 0;              
    version = 0;
    content_length = 0;
    host = 0;
    start_line = 0;
    checked_index = 0;
    read_index = 0;
    write_index = 0;
    bzero(read_buf, READ_BUFFER_SIZE);
    bzero(write_buf, READ_BUFFER_SIZE);
    bzero(real_file, FILENAME_LEN);
}

// 循环读取客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read() {
    // 缓冲区大小不够
    if( read_index >= READ_BUFFER_SIZE ) {
        return false;
    }
    // 每次读取到的字节
    int bytes_read;
    while(true) {
        // 从read_buf + read_index索引出开始保存数据，大小是READ_BUFFER_SIZE - read_index
        bytes_read = recv(sockfd, read_buf + read_index, READ_BUFFER_SIZE - read_index, 0 );
        if (bytes_read == -1) {
            if( errno == EAGAIN || errno == EWOULDBLOCK ) { // 没有数据
                break;
            }
            return false;   
        } else if (bytes_read == 0) {                       // 对方关闭连接
            return false;
        }
        read_index += bytes_read;
    }
    // printf("%s", read_buf);
    return true;
}

void http_conn::process() {
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if ( read_ret == NO_REQUEST ) {
        modfd( epollfd, sockfd, EPOLLIN );
        return;
    }
    
    // 生成响应
    bool write_ret = process_write( read_ret );
    if ( !write_ret ) {
        close_conn();
    }
    modfd( epollfd, sockfd, EPOLLOUT);
}

http_conn::HTTP_CODE http_conn::process_read() { 
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    // 一行数据
    char* text;
    while (((check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
                || ((line_status = parse_line()) == LINE_OK)) {
        /** 解析到了HTTP报文头部的一行数据或者解析到了报文内容
         *  检查完协议头部以后line_status的状态不会改变
        **/
        // 获取一行数据
        text = get_line();
        start_line = checked_index;
        printf( "%s\n", text );

        switch ( check_state ) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line( text );
                if ( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parse_headers( text );
                if ( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                } else if ( ret == GET_REQUEST ) {
                    // 没有请求体
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content( text );
                if ( ret == GET_REQUEST ) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for ( ; checked_index < read_index; checked_index++) {
        temp = read_buf[ checked_index ];
        if ( temp == '\r' ) {
            if ( ( checked_index + 1 ) == read_index ) {
                return LINE_OPEN;
            } else if ( read_buf[ checked_index + 1 ] == '\n' ) {
                read_buf[ checked_index++ ] = '\0';
                read_buf[ checked_index++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if( temp == '\n' )  {
            if( ( checked_index > 1) && ( read_buf[ checked_index - 1 ] == '\r' ) ) {
                read_buf[ checked_index-1 ] = '\0';
                read_buf[ checked_index++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    // GET /index.html HTTP/1.1
    url = strpbrk(text, " \t"); // 判断第二个参数中的字符哪个在text中最先出现
    if (!url) { 
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *url++ = '\0';    // 置位空字符，字符串结束符
    char* tmpmethod = text;
    if ( strcasecmp(tmpmethod, "GET") == 0 ) { // 忽略大小写比较
        method = GET;
    } else {
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    // 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
    version = strpbrk( url, " \t" );
    if (!version) {
        return BAD_REQUEST;
    }
    *version++ = '\0';
    if (strcasecmp( version, "HTTP/1.1") != 0 ) {
        return BAD_REQUEST;
    }
    /**
     * http://ip:port/index.html
    */
    if (strncasecmp(url, "http://", 7) == 0 ) {   
        url += 7;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        url = strchr( url, '/' );
    }
    // 设置默认主页
    if(strcmp(url, "/") == 0)
            url = (char*)("/index.html");
    if ( !url || url[0] != '/' ) {
        return BAD_REQUEST;
    }
    check_state = CHECK_STATE_HEADER; // 检查状态变成检查头
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char* text) {   
    // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( content_length != 0 ) {
            check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明没有请求体，我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            linger = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        content_length = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        host = text;
    } else {
        // printf( "%s\n", text );
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content( char* text ) {
    if ( read_index >= ( content_length + checked_index ) )
    {
        text[ content_length ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/**
 * 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
 * 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
 * 映射到内存地址file_address处，并告诉调用者获取文件成功
**/
http_conn::HTTP_CODE http_conn::do_request()
{
    // "/home/fine/webserver/resources"
    strcpy( real_file, doc_root );
    int len = strlen( doc_root );
    strncpy( real_file + len, url, FILENAME_LEN - len - 1 );
    // 获取real_file文件的相关的状态信息，-1失败，0成功
    if ( stat( real_file, &file_stat ) < 0 ) {
        return NO_RESOURCE;
    }
    // 判断访问权限
    if ( ! ( file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }
    // 判断是否是目录
    if ( S_ISDIR( file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }
    // 以只读方式打开文件
    int fd = open( real_file, O_RDONLY );
    // 创建内存映射
    file_address = ( char* )mmap( NULL, file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}
void http_conn::unmap() {
    if( file_address )
    {
        munmap( file_address, file_stat.st_size );
        file_address = NULL;
    }
}

bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;                // 已经发送的字节
    int bytes_to_send = write_index;        // 将要发送的字节 （write_idx）写缓冲区中待发送的字节数
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd( epollfd, sockfd, EPOLLIN ); 
        init();
        return true;
    }

    while(1) {
        // 分散写
        temp = writev(sockfd, iv, iv_count);
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd( epollfd, sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if ( bytes_to_send <= bytes_have_send ) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            if(linger) {
                init();
                modfd( epollfd, sockfd, EPOLLIN );
                return true;
            } else {
                modfd( epollfd, sockfd, EPOLLIN );
                return false;
            } 
        }
    }
}

bool http_conn::

add_response( const char* format, ... ) {
    if( write_index >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( write_buf + write_index, WRITE_BUFFER_SIZE - 1 - write_index, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - write_index ) ) {
        return false;
    }
    write_index += len;
    va_end( arg_list );
    return true;
}

bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
    return true;
}

bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title );
            add_headers(file_stat.st_size);
            iv[ 0 ].iov_base = write_buf;
            iv[ 0 ].iov_len = write_index;
            iv[ 1 ].iov_base = file_address;
            iv[ 1 ].iov_len = file_stat.st_size;
            iv_count = 2;
            return true;
        default:
            return false;
    }
    iv[ 0 ].iov_base = write_buf;
    iv[ 0 ].iov_len = write_index;
    iv_count = 1;
    return true;
}