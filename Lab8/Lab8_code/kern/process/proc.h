#ifndef __KERN_PROCESS_PROC_H__
#define __KERN_PROCESS_PROC_H__

#include <defs.h>
#include <list.h>
#include <trap.h>
#include <memlayout.h>
#include <skew_heap.h>

// process's state in his life cycle
enum proc_state {
    PROC_UNINIT = 0,  // uninitialized
    PROC_SLEEPING,    // sleeping
    PROC_RUNNABLE,    // runnable(maybe running)
    PROC_ZOMBIE,      // almost dead, and wait parent proc to reclaim his resource
};

struct context {
    uintptr_t ra;
    uintptr_t sp;
    uintptr_t s0;
    uintptr_t s1;
    uintptr_t s2;
    uintptr_t s3;
    uintptr_t s4;
    uintptr_t s5;
    uintptr_t s6;
    uintptr_t s7;
    uintptr_t s8;
    uintptr_t s9;
    uintptr_t s10;
    uintptr_t s11;
};

#define PROC_NAME_LEN               15
#define MAX_PROCESS                 4096
#define MAX_PID                     (MAX_PROCESS * 2)

extern list_entry_t proc_list;

struct inode;

struct proc_struct {
    enum proc_state state;               // 进程状态
    int pid;                             // 进程ID
    int runs;                            // 进程运行次数
    uintptr_t kstack;                    // 进程内核栈
    volatile bool need_resched;          // 是否需要重新调度
    struct proc_struct *parent;          // 父进程
    struct mm_struct *mm;                // 进程内存管理字段
    struct context context;              // 在这里切换以运行进程
    struct trapframe *tf;                // 当前中断的陷阱帧
    uintptr_t cr3;                       // CR3 寄存器：页表基址
    uint32_t flags;                      // 进程标志
    char name[PROC_NAME_LEN + 1];        // 进程名称
    list_entry_t list_link;              // 进程链表链接
    list_entry_t hash_link;              // 进程哈希链表链接
    int exit_code;                       // 退出码（发送给父进程）
    uint32_t wait_state;                 // 等待状态
    struct proc_struct *cptr, *yptr, *optr;  // 进程间的关系
    struct run_queue *rq;                // 包含进程的运行队列
    list_entry_t run_link;               // 连接到运行队列的条目
    int time_slice;                      // 占用 CPU 的时间片
    skew_heap_entry_t lab6_run_pool;     // 仅用于实验6：在运行池中的条目
    uint32_t lab6_stride;                // 仅用于实验6：进程的当前步幅
    uint32_t lab6_priority;              // 仅用于实验6：进程的优先级，由 lab6_set_priority(uint32_t) 设置
    struct files_struct *filesp;         // 进程的文件相关信息（pwd、files_count、files_array、fs_semaphore）
};

#define PF_EXITING                  0x00000001      // getting shutdown

#define WT_CHILD                    (0x00000001 | WT_INTERRUPTED)
#define WT_INTERRUPTED               0x80000000                    // the wait state could be interrupted

#define WT_CHILD                    (0x00000001 | WT_INTERRUPTED)  // wait child process
#define WT_KSEM                      0x00000100                    // wait kernel semaphore
#define WT_TIMER                    (0x00000002 | WT_INTERRUPTED)  // wait timer
#define WT_KBD                      (0x00000004 | WT_INTERRUPTED)  // wait the input of keyboard

#define le2proc(le, member)         \
    to_struct((le), struct proc_struct, member)

extern struct proc_struct *idleproc, *initproc, *current;

void proc_init(void);
void proc_run(struct proc_struct *proc);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);

char *set_proc_name(struct proc_struct *proc, const char *name);
char *get_proc_name(struct proc_struct *proc);
void cpu_idle(void) __attribute__((noreturn));

//FOR LAB6, set the process's priority (bigger value will get more CPU time)
void lab6_set_priority(uint32_t priority);


struct proc_struct *find_proc(int pid);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
int do_exit(int error_code);
int do_yield(void);
int do_execve(const char *name, int argc, const char **argv);
int do_wait(int pid, int *code_store);
int do_kill(int pid);
int do_sleep(unsigned int time);
#endif /* !__KERN_PROCESS_PROC_H__ */

