#ifndef THREAD_pool_H
#define THREAD_pool_H

#include <pthread.h>

// 线程池任务节点
typedef struct thread_pool_task
{
    void (*function)(void *arg);   // 任务函数指针
    void *arg;                     // 传给任务函数的参数
    struct thread_pool_task *next; // 指向下一个节点
} thread_pool_task;

// 线程池结构体
typedef struct
{
    pthread_t *threads;           // 线程数组
    int thread_counts;            // 线程数量
    thread_pool_task *queue_head; // 任务队列头
    thread_pool_task *queue_tail; // 任务队列尾
    pthread_mutex_t lock;         // 队列互斥锁
    pthread_cond_t notify;        // 条件变量
    int shutdown;                 // 0=运行，1=关闭
} thread_pool_t;

// 创建线程池
thread_pool_t* thread_pool_create(int thread_counts);

// 向线程池添加任务
int thread_pool_add_task(thread_pool_t*pool,void(*function)(void*),void *arg);

// 销毁线程池
void thread_pool_destroy(thread_pool_t *pool);

#endif
