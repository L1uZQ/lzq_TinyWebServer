#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<pthread.h>
#include<list>
#include"locker.h"
#include<exception>
#include<cstdio>


//线程池类，定义成模板类为了代码复用
template<typename T>
class threadpool{
public:
    threadpool(int thread_number = 8, int max_requests = 10000);

    ~threadpool();

    bool append(T* request);

private:
    static void * worker(void * arg);
    void run();

private:
    //线程的数量
    int m_thread_number;

    // 线程池数组，大小为 m_thread_number
    pthread_t * m_threads;

    //请求队列中最多允许的等待处理的请求数量
    int m_max_requests;

    //请求队列
    std::list <T *> m_workqueue;

    //互斥锁
    locker m_queuelocker;

    //信号量，用来判断是否有任务需要处理
    sem m_queuestat;

    //是否结束线程
    bool m_stop;
};


template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests):
    m_thread_number(thread_number), m_max_requests(max_requests),
     m_stop(false), m_threads(NULL){

    if((thread_number <= 0) || max_requests<=0){
        throw std:: exception();
    }

    //创建数组
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads){
        throw std:: exception();
    }

    //创建thread_number个线程，并将他们设置为线程脱离
    for(int i=0 ;i<thread_number; ++i){
        printf("正在创建第 %d 个线程\n", i);

        if(pthread_create(m_threads + i, NULL, worker, this) != 0){
            delete [] m_threads;
            throw std:: exception();
        }

        if(pthread_detach(m_threads[i])){
            delete [] m_threads;
            throw std:: exception();
        }
    }

}


template<typename T>
threadpool<T>::~threadpool(){
    delete [] m_threads;
    m_stop = true;
}


template<typename T>
bool threadpool<T>::append(T * request){
    m_queuelocker.lock();
    if(m_workqueue.size()> m_max_requests){
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); //信号量增加一个

    return true;
}


template<typename T>
//worker静态函数，不能访问非静态成员
void* threadpool<T>::worker(void * arg){
    threadpool * pool = (threadpool *) arg;
    pool->run();
    return pool;
}

//线程池运行函数，需要一直循环，直到m_stop为false
template<typename T>
void threadpool<T>::run(){
    while(!m_stop){
        m_queuestat.wait();
        m_queuelocker.lock();//上锁
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }

        T* request = m_workqueue.front();//获取第一个任务
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if(!request){
            continue;
        }

        request->process();
    }
}


#endif