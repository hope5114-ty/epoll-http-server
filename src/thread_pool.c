#include "thread_pool.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// 工作线程主函数
// arg：线程池实例指针
static void *worker_thread(void *arg)
{
    thread_pool_t *pool = (thread_pool_t *)arg;
    while (1)
    {
        // 加锁访问任务队列
        pthread_mutex_lock(&pool->lock);

        // 队列空且未关闭，条件变量睡眠等待任务
        // pthread_cond_wait原子释放锁并阻塞；被唤醒后重新获取锁才返回
        while (pool->queue_head == NULL && !pool->shutdown)
        {
            pthread_cond_wait(&pool->notify, &pool->lock);
        }

        // 收到关闭标记，队列已经为空，线程退出
        if (pool->shutdown && pool->queue_head == NULL)
        {
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        // 从队列头部取出任务节点
        thread_pool_task *task = pool->queue_head;
        pool->queue_head = task->next;
        if (pool->queue_head == NULL)
        {
            pool->queue_tail = NULL;
        }
        pthread_mutex_unlock(&pool->lock);

        // 在锁外部执行任务，禁止持锁执行业务逻辑，否则队列会阻塞
        task->function(task->arg);
        free(task);
    }
    log_info("工作线程退出");
    return NULL;
}

// 创建线程池
// thread_counts：工作线程数量
// return：成功返回线程池指针，失败返回NULL
thread_pool_t *thread_pool_create(int thread_counts)
{
    if (thread_counts <= 0)
    {
        return NULL;
    }

    thread_pool_t *pool = (thread_pool_t *)malloc(sizeof(thread_pool_t));
    if (pool == NULL)
    {
        return NULL;
    }
    memset(pool, 0, sizeof(thread_pool_t));

    pool->thread_counts = thread_counts;
    pool->queue_head = NULL;
    pool->queue_tail = NULL;
    pool->shutdown = 0;

    // 初始化互斥锁
    if (pthread_mutex_init(&pool->lock, NULL) != 0)
    {
        free(pool);
        return NULL;
    }
    // 初始化条件变量
    if (pthread_cond_init(&pool->notify, NULL) != 0)
    {
        pthread_mutex_destroy(&pool->lock);
        free(pool);
        return NULL;
    }

    // 分配线程id数组
    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * thread_counts);
    if (pool->threads == NULL)
    {
        pthread_cond_destroy(&pool->notify);
        pthread_mutex_destroy(&pool->lock);
        free(pool);
        return NULL;
    }

    // 批量创建工作线程
    for (int i = 0; i < thread_counts; i++)
    {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0)
        {
            // 创建失败，标记关闭，唤醒已有线程做退出处理
            pool->shutdown = 1;
            pthread_cond_broadcast(&pool->notify);
            for (int j = 0; j < i; j++)
            {
                pthread_join(pool->threads[j], NULL);
            }
            free(pool->threads);
            pthread_cond_destroy(&pool->notify);
            pthread_mutex_destroy(&pool->lock);
            free(pool);
            return NULL;
        }
    }

    log_info("线程池创建成功，工作线程数: %d", thread_counts);
    return pool;
}

// 向线程池添加任务
// pool：线程池实例
// function：任务处理函数
// arg：任务函数入参
// return：成功返回0，失败返回-1
int thread_pool_add_task(thread_pool_t *pool, void (*function)(void *), void *arg)
{
    if (pool == NULL || function == NULL || pool->shutdown)
    {
        return -1;
    }

    // 创建任务节点（修复原代码错误：不要强转为thread_pool_t*）
    thread_pool_task *task = (thread_pool_task *)malloc(sizeof(thread_pool_task));
    if (task == NULL)
    {
        return -1;
    }
    task->function = function;
    task->arg = arg;
    task->next = NULL;

    // 加锁将任务插入队列尾部
    pthread_mutex_lock(&pool->lock);
    if (pool->queue_tail == NULL)
    {
        // 当前队列为空
        pool->queue_head = task;
        pool->queue_tail = task;
    }
    else
    {
        pool->queue_tail->next = task;
        pool->queue_tail = task;
    }
    // 唤醒一个阻塞的工作线程处理任务
    pthread_cond_signal(&pool->notify);
    pthread_mutex_unlock(&pool->lock);

    return 0;
}

// 销毁线程池，等待全部线程退出，释放全部资源
void thread_pool_destroy(thread_pool_t *pool)
{
    if (pool == NULL)
    {
        return;
    }

    pthread_mutex_lock(&pool->lock);
    pool->shutdown = 1;
    // 广播唤醒所有工作线程，全部检测shutdown标记退出
    pthread_cond_broadcast(&pool->notify);
    pthread_mutex_unlock(&pool->lock);

    // join阻塞等待所有工作线程结束
    for (int i = 0; i < pool->thread_counts; i++)
    {
        pthread_join(pool->threads[i], NULL);
    }

    // 释放队列遗留未执行的任务节点
    thread_pool_task *task = pool->queue_head;
    while (task != NULL)
    {
        thread_pool_task *next = task->next;
        free(task);
        task = next;
    }

    // 销毁锁、条件变量，释放内存
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->notify);
    free(pool->threads);
    free(pool);

    log_info("线程池已销毁");
}
