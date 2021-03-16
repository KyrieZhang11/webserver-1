# C++实现的高并发服务器

## 服务器事件处理模式

### 模拟 Proactor 模式

使用同步 I/O 方式模拟出 Proactor 模式。原理是：主线程执行数据读写操作，读写完成之后，主线程向
工作线程通知这一”完成事件“。那么从工作线程的角度来看，它们就直接获得了数据读写的结果，接下
来要做的只是对读写的结果进行逻辑处理。

1. 主线程往epoll内核事件表中注册scoket上的读就绪事件
2. 线程调用epoll_wait等待socket上有数据可读
3. 当socket上有数据可读时，epoll_wait通知主线程；主线程从socket循环读取数据，直到没有更多数据可读，然后将读取到的数据封装成一个请求对象并插入请求队列
4. 睡眠在请求队列上的某个工作线程被唤醒，它获得请求对象，并处理客户请求，然后往epoll内核时间表中注册该socket上的写就绪事件
5. 主线程调用epoll_wait等待socket可写
6. 当socket可写时， epoll_wait通知主线程；主线程往socket上写入服务器处理客户请求的结果

![procedure](data/procedure.png)

## 主函数
1. 设置服务器端口
2. 设置SIGPIPE信号处理， 防止客户端断开连接导致服务器写入时进程终止
3. 初始化线程池
4. 设置监听socket（初始化，绑定，监听，接收）
5. 注册epoll对象，设置事件处理

## 主线程
1. 创建具有最大连接数的users数组用于保存所有的http连接
2. 创建epoll对象和具有最大监听数的event事件数组
3. 将listenfd添加到epoll对象中进行监听
4. 对所有监听到的文件描述符进行处理
   - 如果监听到到的文件描述符是listenfd，说明有新的客户端连接，建立sockaddr_in地址，使用accept接收新的连接，并将新socket的文件描述符加入epoll中监听
   - 如果检测到读事件，读取数据后将http连接对象放入请求队列中
   - 如果检测到写事件，将事件写回客户端

### 一次性读取所有数据
在任务对象http_conn中定义data members：
- 读缓冲区 read_buf
- 当前正在分析的字符在读缓冲区中的位置 read_index
- 将读取到的数据保存到read_buf+read_index为起始地址的内存中，read_index随着recv的字节数递增
### 将http_conn对象放入请求队列中
请求队列是线程池的一部分，线程池类中存放工作队列指针的list，push_back可以将http_conn的指针放入请求队列中。

## 线程池的实现
class thread_pool:线程池包含一组线程和一个任务队列，用信号量对任务队列进行管理，队列的增加和线程去队列中取任务执行都需要使用互斥锁以保证同步。由于任务队列是客户端连接、主线程处理后加入的，所以并不需要条件变量。
线程池中实现了子线程的处理逻辑：The new thread starts execution by invoking `worker(this)`;
由于C++的member functions会默认传入this，而pthread_create创建线程时执行的调用函数只能有一个参数，所以将worker设置为静态，将this pointer传入作为唯一参数。
### 子线程worker的逻辑
- 从任务队列中取出指向任务的指针，执行http_conn对象自身的处理逻辑process，即解析HTTP请求

## 响应和处理客户端请求
- 解析HTTP报文
- 生成响应
- 注册epoll的EPOLLOUT事件，使主线程将数据写入客户端
### 解析HTTP请求报文
使用有限状态机进行解析：协议包含数据包类型字段，每种类型可以映射为逻辑单元的一种执行状态，服务器根据它来编写相应的处理逻辑。整个报文使用主状态机处理， 每一行的解析使用从状态机处理。

HTTP请求方法，暂时只支持GET；

#### 服务器处理HTTP请求的可能结果，报文解析的结果

- NO_REQUEST         	 				    : 请求不完整，需要继续读取客户数据
- GET_REQUEST                                 : 表示获得了一个完成的客户请求
- BAD_REQUEST                                :  表示客户请求语法错误
- NO_RESOURCE                               :  表示服务器没有资源
- FORBIDDEN_REQUEST                  :  表示客户对资源没有足够的访问权限
- FILE_REQUEST                                :  文件请求,获取文件成功
- INTERNAL_ERROR                          :  表示服务器内部错误
- CLOSED_CONNECTION                 :  表示客户端已经关闭连接了

#### 解析客户端请求时，主状态机的状态

1. CHECK_STATE_REQUESTLINE 	  : 当前正在分析请求行
2. CHECK_STATE_HEADER                 : 当前正在分析头部字段
3. CHECK_STATE_CONTENT              : 当前正在解析请求体

#### 从状态机的三种可能状态，即行的读取状态

1. LINE_OK                                          ：读取到一个完整的行
2. LINE_BAD                                        :  行出错
3. LINE_OPEN                                     :  行数据尚且不完整，没有遇到结束符，即没有遇到\r\n

#### 解析逻辑：process_read()

在解析到完整的一行(\r\n)或者解析到请求体

- 解析HTTP请求行 HTTP_CODE parse_request_line( char *)
- 解析HTTP请求头 HTTP_CODE parse_headers( char *)
- 解析HTTP请求体 HTTP_CODE parse_content( char * )

使用buf+start+line获取一行数据，start_line随着check_index递增；

##### 解析完整的一行

### 响应请求
当得到一个完整、正确的HTTP请求时，分析目标文件的属性，如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其映射到内存地址file_address处，并告诉调用者获取文件成功。
