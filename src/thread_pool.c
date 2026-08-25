#include "thread_pool.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//任务函数
static void *worker_thread(void *arg) 
{
    thread_pool_t *pool=(thread_pool_t*)arg;

    while(1){
        pthread_mutex_lock(&pool->lock);

        //队列为空且没有关闭，等待条件变量
        while(pool->queue_head==NULL&&!pool->shutdown)
        {
            pthread_cond_wait(&pool->notify,&pool->lock);
        }
        //收到关闭信号且队列为空时，退出
        if(pool->shutdown&&pool->queue_head==NULL)
        {
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        //取出队头任务
        thread_pool_task *task=pool->queue_head;
        pool->queue_head=task->next;
        if(pool->queue_head==NULL)
            pool->queue_tail=NULL;

        pthread_mutex_unlock(&pool->lock);
        task->function(task->arg);
        free(task);
    }

    log_info("工作线程结束");
    return NULL;
}

// 创建线程池
thread_pool_t*thread_pool_create(int thread_counts)
{
    if(thread_counts==0) return NULL;
    thread_pool_t *pool=malloc(sizeof(thread_pool_t));
    if(pool==NULL){
        return NULL;
    }
    memset(pool,0,sizeof(thread_pool_t));

    pool->thread_counts=thread_counts;
    pool->queue_head=NULL;
    pool->queue_tail=NULL;
    pool->shutdown=0;

    // 互斥锁
    if(pthread_mutex_init(&pool->lock,NULL)!=0)
    {
        free(pool);
        return NULL;
    }

    //条件变量
    if(pthread_cond_init(&pool->notify,NULL)!=0)
    {
        pthread_mutex_destroy(&pool->lock);
        free(pool);
        return NULL;
    }
    
    // 创建工作线程
    for(int i=0;i<thread_counts;i++){
        if(pthread_create(&pool[i],NULL,worker_thread,pool)!=0){
            pool->shutdown=1;
            pthread_cond_broadcast(&pool->notify);
            for(int j=0;j<i;j++){
                pthread_join(&pool->threads[j],NULL);
            }
            free(pool->threads);
            pthread_cond_destroy(&pool->notify);
            pthread_mutex_destroy(&pool->lock);
            free(pool);
        }
    }
    log_info("线程池创建成功，线程数%d",thread_counts); 
}

//向线程池添加任务
int thread_pool_add_task(thread_pool_t*pool,void(*function)(void*),void *arg)
{
    if(pool==NULL||function==NULL||pool->shutdown)
    {
        return -1;
    }

    //初始化一个任务
    thread_pool_task *task=malloc(sizeof(thread_pool_task));
    if(task==NULL) return -1;
    task->arg=arg;
    task->function=function;
    task->next=NULL;
    //加锁
    pthread_mutex_lock(&pool->lock);
    if(pool->queue_tail==NULL){
        pool->queue_head=task;
        pool->queue_tail=task;
    }else{
        pool->queue_tail->next=task;
        pool->queue_tail=task;
    }
    pthread_cond_signal(&pool->notify);
    pthread_mutex_unlock(&pool->lock);
    return 0;
}

// 销毁线程池
void thread_pool_destroy(thread_pool_t *pool)
{
    if(pool==NULL) return;
    pthread_mutex_lock(&pool->lock);
    //通知
    pool->shutdown=1;
    pthread_mutex_unlock(&pool->lock);
    //等待所有线程的结束
    for(int i=0;i<pool->thread_counts;i++){
        pthread_join(&pool->threads[i],NULL);
    }

    thread_pool_task *task=pool->queue_head;
    while(task){
        thread_pool_task *del=task;
        task=task->next;
        free(del);
        del=NULL;
    }
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->notify);
    free(pool->threads);
    free(pool);

    log_info("线程池已销毁");
}
