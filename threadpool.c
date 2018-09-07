#define _GNU_SOURCE

#include <uv.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <time.h>
#include <sys/time.h>
#include <sched.h>

#define STEP_TIME_USEC 1000000

#define container_of(ptr, type, member) \
  ((type *) ((char *) (ptr) - offsetof(type, member)))
#define gettid() syscall(SYS_gettid)


typedef struct
{
  int index;
  int done;
  unsigned long long timestamp;
  uv_timer_t timer;
  uv_work_t req;
  void (*run)(void);
}performance_model_t;

static uv_loop_t *g_loop_;

void bind_cpu_(pid_t pid,int cpu)
{
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(cpu,&mask);
  //printf("bind_cpu_ %d ahead %d\n",pid,sched_getcpu());
  sched_setaffinity(pid,sizeof(mask),&mask);
  //printf("bind_cpu_ %d after %d\n",pid,sched_getcpu());
}

int bind_cpu_checker_(pid_t pid,pid_t tid)
{
  char path[1024];
  char cmd[1024];
  char bf[1024]={0};
  sprintf(path,"/proc/%d/task/%d/stat",pid,tid);
  sprintf(cmd,"cat %s | awk '{print $39}'",path);
  FILE *fp=popen(cmd,"r");
  fgets(bf,sizeof(bf),fp);
  pclose(fp);
  return atoi(bf);
}

static void worker_run()
{
  int i;
  long int step;
  struct timeval tv;
  gettimeofday(&tv,NULL);
  srandom(tv.tv_sec+tv.tv_usec);
  for(i=0;i<20;i++)
  {
    step=random()%10000;
    if(step<=0)step=100000;
    usleep(step);
  }
}

static void worker_timer(uv_timer_t *handle);
void worker(uv_work_t *req)
{
  struct timeval tv;
  unsigned long long timestamp;
  performance_model_t *modp=container_of(req,performance_model_t,req);

  //int cpu=bind_cpu_checker_(getpid(),gettid());
  //printf("worker thread=%lu pid=%d tid=%ld index=%d bind=%d\n",uv_thread_self(),getpid(),gettid(),modp->index,cpu);

  gettimeofday(&tv,NULL);
  timestamp=tv.tv_sec*1000000+tv.tv_usec;
  //if(modp->timestamp>timestamp){printf("worker model %d ERROR %lld\n",modp->index,modp->timestamp-timestamp);usleep(100);return;}
  printf("worker model %d delay to run mod-now=%lld usec\n",modp->index,modp->timestamp-timestamp);

  if(modp->run)modp->run();

  gettimeofday(&tv,NULL);
  timestamp=tv.tv_sec*1000000+tv.tv_usec;
  modp->timestamp=timestamp+STEP_TIME_USEC;
  //printf("worker model %d run next %llu\n",modp->index,modp->timestamp);

}

void after_worker(uv_work_t *req,int status)
{
  performance_model_t *modp=(performance_model_t*)req->data;
  //printf("after worker thread=%lu pid=%d tid=%ld index=%d\n",uv_thread_self(),getpid(),gettid(),modp->index);
  uv_timer_start(&modp->timer,worker_timer,1000,0);
}

void worker_timer(uv_timer_t *handle)
{
  performance_model_t *modp=(performance_model_t*)handle->data;
  //printf("worker timer thread=%lu pid=%d tid=%ld index=%d\n",uv_thread_self(),getpid(),gettid(),modp->index);
  uv_queue_work(g_loop_,&modp->req,worker,after_worker);
}

typedef struct
{
  int index;
  int cpu;
  pid_t pid;
  pid_t tid;
  uv_thread_t uv_tid;
  uv_work_t req;
}thread_bind_t;

uv_barrier_t g_bind_cpu_barrier_;

void bind_cpu(uv_work_t *req)
{
  thread_bind_t *tb=container_of(req,thread_bind_t,req);
  uv_barrier_wait(&g_bind_cpu_barrier_);
  tb->pid=getpid();
  tb->tid=gettid();
  tb->uv_tid=uv_thread_self();
  bind_cpu_(tb->tid,tb->cpu);
  //if(tb->cpu!=sched_getcpu())printf("bind_cpu error pid=%d tid=%d index=%d bind=%d %d\n",tb->pid,tb->tid,tb->index,tb->cpu,sched_getcpu());
  //printf("bind_cpu pid=%d tid=%d index=%d bind=%d vs %d\n",tb->pid,tb->tid,tb->index,tb->cpu,sched_getcpu());
}
void after_bind_cpu(uv_work_t *req,int status)
{
  thread_bind_t *tb=container_of(req,thread_bind_t,req);
  int cpu=bind_cpu_checker_(tb->pid,tb->tid);
  if(tb->cpu!=cpu)printf("after_bind_cpu error pid=%d tid=%d index=%d bind=%d vs %d\n",tb->pid,tb->tid,tb->index,tb->cpu,cpu);
  //printf("after_bind_cpu pid=%d tid=%d index=%d bind=%d vs %d\n",tb->pid,tb->tid,tb->index,tb->cpu,cpu);
}

void queue_work_mgr()
{
  #define MAX_UV_WORKER_THREAD  22
  #define MAX_QUEUE 6
  int i=0;
  unsigned long long timestamp=0;
  struct timeval tv;
  bind_cpu_(gettid(),1);
  printf("queue mgr thread=%lu pid=%d tid=%ld bind=%d\n",uv_thread_self(),getpid(),gettid(),bind_cpu_checker_(getpid(),gettid()));
  char wt[32];sprintf(wt,"%d",MAX_UV_WORKER_THREAD);
  setenv("UV_THREADPOOL_SIZE",wt,1);
  const char *var=getenv("UV_THREADPOOL_SIZE");
  printf("UV_THREADPOOL_SIZE: %s\n",var);

  performance_model_t mod[MAX_QUEUE];

  g_loop_=uv_default_loop();

  // Thread bind to CPU
  thread_bind_t tbind[MAX_UV_WORKER_THREAD];
  memset(&tbind,0,sizeof(tbind));
  uv_barrier_init(&g_bind_cpu_barrier_,MAX_UV_WORKER_THREAD+1);
  int nprocessor=sysconf(_SC_NPROCESSORS_CONF);
  for(i=0;i<MAX_UV_WORKER_THREAD;i++)
  {
    tbind[i].index=i;
    tbind[i].cpu=i%(nprocessor-2)+2;
    uv_queue_work(g_loop_,&tbind[i].req,bind_cpu,after_bind_cpu);
  }
  uv_barrier_wait(&g_bind_cpu_barrier_);
  uv_barrier_destroy(&g_bind_cpu_barrier_);

  // Thread run your work
  #if 1
  for(i=0;i<MAX_QUEUE;i++)
  {
    gettimeofday(&tv,NULL);
    timestamp=tv.tv_sec*1000000+tv.tv_usec;
    mod[i].index=i;
    mod[i].timestamp=timestamp;
    mod[i].timer.data=(void*)&mod[i];
    mod[i].req.data=(void*)&mod[i];
    mod[i].run=worker_run;
    uv_timer_init(g_loop_,&mod[i].timer);
    uv_timer_start(&mod[i].timer,worker_timer,10,0);
  }
  #endif

  #if 0
  // Thread debug
  uv_work_t req;
  void hold(uv_work_t *req)
  {
    while(1)sleep(100);
  }
  uv_queue_work(g_loop_,&req,hold,NULL);
  #endif

  uv_run(g_loop_,UV_RUN_DEFAULT);

  for(i=0;i<MAX_UV_WORKER_THREAD;i++)
  {
    printf("THREAD BIND tid=%d index=%d bind=%d\n",tbind[i].tid,tbind[i].index,tbind[i].cpu);
  }

  printf("queue_work_mgr end\n");
  #undef MAX_UV_WORKER_THREAD
  #undef MAX_QUEUE
}

int main(int argc,char **argv)
{
  bind_cpu_(getpid(),0);
  printf("main thread=%lu pid=%d tid=%ld bind=%d\n",uv_thread_self(),getpid(),gettid(),bind_cpu_checker_(getpid(),gettid()));

  uv_thread_t t1;
  uv_thread_create(&t1,queue_work_mgr,NULL);

  uv_thread_join(&t1);

  printf("main end\n");

  return 0;
}
