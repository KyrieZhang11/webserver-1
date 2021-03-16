/**
 * 线程同步机制及线程池的实现
 * 请求队列为池的一部分
 * 线程池包含一组线程和一个任务队列，用信号量对任务队列进行管理，
 * 队列的增加和线程去队列中取任务执行都需要使用互斥锁以保证同步。
**/

#ifndef TREADPOOL_H
#define TREADPOOL_H  1

#include <pthread.h>
#include <exception>
#include <semaphore.h>
#include <list>

using namespace std;

// 模板参数T是任务类
template<typename T>
class Threadpool
{
private:
    pthread_mutex_t mutex;                  // 互斥锁
    sem_t           sem;                    // 任务的数量
    int             thread_number;
    pthread_t*      threads;                // 指向线程ID数组的指针
    int             max_request_number;     // 请求队列的最大长度
    list<T *>       workqueue;              // 指向工作队列的指针
    bool            stop;                   // 是否结束线程
public:
    Threadpool(int thread_number = 8, int max_request_number = 10000, int sem_num=0);
    ~Threadpool();
    bool append_request(T *request);
    static void *worker(void *arg);
    void run();
};

/**
 * 之前把.h和.cpp分离，无法链接
 * 参考https://blog.csdn.net/jinzeyu_cn/article/details/45795923
 * 因为模板类需要在使用到的地方利用声明模板的typename或者class参数的时候，才会即时生成代码。
 * 那么当我把模板声明和实现分开的时候，这个即时过程因为编译器只能通过代码include“看到”头文件而找不到模板实现代码，所以会产生链接问题。
 * 这也是为什么几乎都会建议模板类和声明和实现都写在头文件。
**/
template<typename T>
Threadpool<T>::Threadpool(int thread_num, int request_num, int sem_num) :
thread_number(thread_num), max_request_number(request_num), stop(false), threads(NULL)
{
    if(thread_number <= 0 || max_request_number <= 0) {
        throw exception();
    }
    if(pthread_mutex_init(&mutex, NULL)){
        throw exception();
    }
    if(sem_init(&sem, 0, sem_num)){
        throw exception();
    }

    threads = new pthread_t[thread_number];
    if(!threads) {
        throw exception();
    }
    for(int i=0; i<thread_number; i++){
        if(pthread_create(threads+i, NULL, worker, this)){
            delete []threads;
            throw exception();
        }
        if(pthread_detach(threads[i])){
            delete []threads;
            throw exception();
        }
    }
}

template<typename T>
Threadpool<T>::~Threadpool()
{
    pthread_mutex_destroy(&mutex);
    sem_destroy(&sem);
    delete []threads;
    stop = true;
}

template<typename T>
bool Threadpool<T>::append_request(T *request)
{
    pthread_mutex_lock(&mutex);
    workqueue.push_back(request);
    pthread_mutex_unlock(&mutex);
    sem_post(&sem);
    return true;
}

template<typename T>
void *Threadpool<T>::worker(void *arg)
{
    Threadpool *pool = (Threadpool *)arg;
    pool->run();
    return pool;
}

template<typename T>
void Threadpool<T>::run()
{
    while(!stop){
        sem_wait(&sem);
        pthread_mutex_lock(&mutex);
        T *request = workqueue.front();
        workqueue.pop_front();
        pthread_mutex_unlock(&mutex);
        request->process();
    }
}
#endif