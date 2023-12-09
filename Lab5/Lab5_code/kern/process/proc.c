#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc,
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:

  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  +
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT 10
#define HASH_LIST_SIZE (1 << HASH_SHIFT)
#define pid_hashfn(x) (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

// alloc_proc - alloc a proc_struct and init all fields of proc_struct
static struct proc_struct *
alloc_proc(void)
{
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL)
    {
        // LAB4:EXERCISE1 YOUR CODE
        /*
         * below fields in proc_struct need to be initialized
         *       enum proc_state state;                      // Process state
         *       int pid;                                    // Process ID
         *       int runs;                                   // the running times of Proces
         *       uintptr_t kstack;                           // Process kernel stack
         *       volatile bool need_resched;                 // bool value: need to be rescheduled to release CPU?
         *       struct proc_struct *parent;                 // the parent process
         *       struct mm_struct *mm;                       // Process's memory management field
         *       struct context context;                     // Switch here to run process
         *       struct trapframe *tf;                       // Trap frame for current interrupt
         *       uintptr_t cr3;                              // CR3 register: the base addr of Page Directroy Table(PDT)
         *       uint32_t flags;                             // Process flag
         *       char name[PROC_NAME_LEN + 1];               // Process name
         */
        proc->state = PROC_UNINIT;
        proc->pid = -1;
        proc->runs = 0;
        proc->kstack = 0;
        proc->need_resched = 0;
        proc->parent = NULL;
        proc->mm = NULL;
        memset(&(proc->context), 0, sizeof(struct context));
        proc->tf = NULL;
        proc->cr3 = boot_cr3;
        proc->flags = 0;
        memset(proc->name, 0, PROC_NAME_LEN);
        proc->wait_state = 0;                        // PCB新增的条目，初始化进程等待状态
        proc->cptr = proc->optr = proc->yptr = NULL; // 设置指针
    }
    return proc;
}

// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// set_links - set the relation links of process
static void
set_links(struct proc_struct *proc)
{
    list_add(&proc_list, &(proc->list_link));
    proc->yptr = NULL;
    if ((proc->optr = proc->parent->cptr) != NULL)
    {
        proc->optr->yptr = proc;
    }
    proc->parent->cptr = proc;
    nr_process++;
}

// remove_links - clean the relation links of process
static void
remove_links(struct proc_struct *proc)
{
    list_del(&(proc->list_link));
    if (proc->optr != NULL)
    {
        proc->optr->yptr = proc->yptr;
    }
    if (proc->yptr != NULL)
    {
        proc->yptr->optr = proc->optr;
    }
    else
    {
        proc->parent->cptr = proc->optr;
    }
    nr_process--;
}

// get_pid - alloc a unique pid for process
static int
get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++last_pid >= MAX_PID)
    {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe)
    {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid)
            {
                if (++last_pid >= next_safe)
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}

// proc_run - make process "proc" running on cpu
// NOTE: before call switch_to, should load  base addr of "proc"'s new PDT
void proc_run(struct proc_struct *proc)
{
    // LAB4:EXERCISE3 YOUR CODE
    /*
     * Some Useful MACROs, Functions and DEFINEs, you can use them in below implementation.
     * MACROs or Functions:
     *   local_intr_save():        Disable interrupts
     *   local_intr_restore():     Enable Interrupts
     *   lcr3():                   Modify the value of CR3 register
     *   switch_to():              Context switching between two processes
     */
    if (proc != current)
    {
        bool intr_flag;
        struct proc_struct *prev = current, *next = proc;
        local_intr_save(intr_flag);
        {
            current = proc;
            lcr3(next->cr3);
            switch_to(&(prev->context), &(next->context));
        }
        local_intr_restore(intr_flag);
    }
}

// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void)
{
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc)
{
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// unhash_proc - delete proc from proc hash_list
static void
unhash_proc(struct proc_struct *proc)
{
    list_del(&(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid)
{
    if (0 < pid && pid < MAX_PID)
    {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list)
        {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid)
            {
                return proc;
            }
        }
    }
    return NULL;
}

// kernel_thread - create a kernel thread using "fn" function
// NOTE: the contents of temp trapframe tf will be copied to
//       proc->tf in do_fork-->copy_thread function
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    struct trapframe tf;
    memset(&tf, 0, sizeof(struct trapframe));
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;
    tf.epc = (uintptr_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc)
{
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc)
{
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// setup_pgdir - alloc one page as PDT
static int
setup_pgdir(struct mm_struct *mm)
{
    struct Page *page;
    if ((page = alloc_page()) == NULL)
    {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir, PGSIZE);

    mm->pgdir = pgdir;
    return 0;
}

// put_pgdir - free the memory space of PDT
static void
put_pgdir(struct mm_struct *mm)
{
    free_page(kva2page(mm->pgdir));
}

// copy_mm - process "proc" duplicate OR share process "current"'s mm according clone_flags
//         - if clone_flags & CLONE_VM, then "share" ; else "duplicate"
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    struct mm_struct *mm, *oldmm = current->mm;

    /* current is a kernel thread */
    if (oldmm == NULL) //当前进程地址空间为 NULL
    {
        return 0;
    }
    if (clone_flags & CLONE_VM) //可以共享地址空间
    {
        mm = oldmm; //共享地址空间
        goto good_mm;
    }
    int ret = -E_NO_MEM;
    if ((mm = mm_create()) == NULL)  //创建地址空间未成功
    {
        goto bad_mm;
    }
    if (setup_pgdir(mm) != 0)
    {
        goto bad_pgdir_cleanup_mm;
    }
    lock_mm(oldmm);
    {
        ret = dup_mmap(mm, oldmm);//调用 dup_mmap 函数
    }
    unlock_mm(oldmm);

    if (ret != 0)
    {
        goto bad_dup_cleanup_mmap;
    }

good_mm:
    mm_count_inc(mm); //共享地址空间的进程数加一
    proc->mm = mm;  //复制空间地址
    proc->cr3 = PADDR(mm->pgdir); //复制页表地址
    return 0;
bad_dup_cleanup_mmap:
    exit_mmap(mm);
    put_pgdir(mm);
bad_pgdir_cleanup_mm:
    mm_destroy(mm);
bad_mm:
    return ret;
}

// copy_thread - setup the trapframe on the  process's kernel stack top and
//             - setup the kernel entry point and stack of process
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)
{
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
    *(proc->tf) = *tf;

    // Set a0 to 0 so a child process knows it's just forked
    proc->tf->gpr.a0 = 0;
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;

    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)(proc->tf);
}

/* do_fork -     parent process for a new child process
 * @clone_flags: used to guide how to clone the child process
 * @stack:       the parent's user stack pointer. if stack==0, It means to fork a kernel thread.
 * @tf:          the trapframe info, which will be copied to child process's proc->tf
 */
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;
    if (nr_process >= MAX_PROCESS)
    {
        goto fork_out;
    }
    ret = -E_NO_MEM;
    if ((proc = alloc_proc()) == NULL)
    {
        goto fork_out;
    }
    proc->parent = current;
    assert(current->wait_state == 0); // 确保进程在等待
    if (setup_kstack(proc) != 0)
    {
        goto bad_fork_cleanup_proc;
    }
    if (copy_mm(clone_flags, proc) != 0)
    {
        goto bad_fork_cleanup_kstack;
    }
    copy_thread(proc, stack, tf);
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        proc->pid = get_pid();
        hash_proc(proc);
        set_links(proc); // 设置进程链接
    }
    local_intr_restore(intr_flag);
    wakeup_proc(proc);
    ret = proc->pid;
fork_out:
    return ret;
bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    goto fork_out;
}

// do_exit - called by sys_exit
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//       调用exit_mmap & put_pgdir & mm_destroy去释放退出线程占用的几乎全部的内存空间(线程栈等需要父进程来回收)
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//       设置线程的状态为僵尸态PROC_ZOMBIE，然后唤醒父进程去回收退出的进程
//   3. call scheduler to switch to other process
//       调用调度器切换为其它线程
int
do_exit(int error_code) {
    // 检查当前进程是否为idleproc或initproc，如果是，发出panic
    if (current == idleproc) {
        panic("idleproc exit.\n");
    }
    if (current == initproc) {
        panic("initproc exit.\n");
    }

    // 获取当前进程的内存管理结构mm
    struct mm_struct *mm = current->mm;

    // 如果mm不为空，说明是用户进程
    if (mm != NULL) {
        // 切换到内核页表，确保接下来的操作在内核空间执行
        lcr3(boot_cr3);

        // 如果mm引用计数减到0，说明没有其他进程共享此mm
        if (mm_count_dec(mm) == 0) {
            // 释放用户虚拟内存空间相关的资源
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
        // 将当前进程的mm设置为NULL，表示资源已经释放
        current->mm = NULL;
    }

    // 设置进程状态为PROC_ZOMBIE，表示进程已退出
    current->state = PROC_ZOMBIE;
    current->exit_code = error_code;

    bool intr_flag;
    struct proc_struct *proc;

    // 关中断
    local_intr_save(intr_flag);
    {
        // 获取当前进程的父进程
        proc = current->parent;
        
        // 如果父进程处于等待子进程状态，则唤醒父进程
        if (proc->wait_state == WT_CHILD) {
            wakeup_proc(proc);
        }
        
        // 遍历当前进程的所有子进程
        while (current->cptr != NULL) {
            proc = current->cptr;
            current->cptr = proc->optr;

            // 设置子进程的父进程为initproc，并加入initproc的子进程链表
            proc->yptr = NULL;
            if ((proc->optr = initproc->cptr) != NULL) {
                initproc->cptr->yptr = proc;
            }
            proc->parent = initproc;
            initproc->cptr = proc;

            // 如果子进程也处于退出状态，唤醒initproc
            if (proc->state == PROC_ZOMBIE) {
                if (initproc->wait_state == WT_CHILD) {
                    wakeup_proc(initproc);
                }
            }
        }
    }
    // 开中断
    local_intr_restore(intr_flag);

    // 调用调度器，选择新的进程执行
    schedule();

    // 如果执行到这里，表示代码执行出现错误，发出panic
    panic("do_exit will not return!! %d.\n", current->pid);
}

/* load_icode - 将二进制程序（ELF格式）的内容加载为当前进程的新内容
 * @binary: 二进制程序内容的内存地址
 * @size: 二进制程序内容的大小
 */
static int
load_icode(unsigned char *binary, size_t size)
{
    // 如果当前进程已经有内存映射，则发生错误
    if (current->mm != NULL)
    {
        panic("load_icode: current->mm must be empty.\n");
    }

    int ret = -E_NO_MEM;
    struct mm_struct *mm;

    //(1) 为当前进程创建一个新的 mm 结构
    if ((mm = mm_create()) == NULL)
    {
        goto bad_mm;
    }

    //(2) 创建一个新的页目录表（PDT），并将 mm->pgdir 设置为页目录表的内核虚拟地址
    if (setup_pgdir(mm) != 0)
    {
        goto bad_pgdir_cleanup_mm;
    }

    //(3) 复制 TEXT/DATA 段，构建二进制的 BSS 部分到进程的内存空间
    // 从进程的二进制数据空间中分配内存，复制出对应的代码/数据段，建立BSS部分
    struct Page *page;

    //(3.1) 获取二进制程序的文件头（ELF格式）
    // 二进制程序数据的最开头的一部分是elf文件头,以elfhdr指针的形式将其映射、提取出来
    struct elfhdr *elf = (struct elfhdr *)binary;

    //(3.2) 获取二进制程序的程序段头入口（ELF格式）
    struct proghdr *ph = (struct proghdr *)(binary + elf->e_phoff);

    //(3.3) 检查程序是否有效，是否是一个合法的ELF文件
    if (elf->e_magic != ELF_MAGIC)
    {
        ret = -E_INVAL_ELF;
        goto bad_elf_cleanup_pgdir;
    }

    uint32_t vm_flags, perm;
    struct proghdr *ph_end = ph + elf->e_phnum;
    //(3.4) 遍历每个程序段头
    for (; ph < ph_end; ph++)
    {
        if (ph->p_type != ELF_PT_LOAD)
        {
            // 如果不是需要加载的段，直接跳过
            continue;
        }

        if (ph->p_filesz > ph->p_memsz)
        {
            // 如果文件头标明的文件段大小大于所占用的内存大小(memsz可能包括了BSS，所以这是错误的程序段头)
            ret = -E_INVAL_ELF;
            goto bad_cleanup_mmap;
        }

        if (ph->p_filesz == 0)
        {
            // 如果文件大小为0，继续下一个段
            // continue;
        }

        //(3.5) 调用 mm_map 函数设置新的 VMA（ph->p_va, ph->p_memsz）
        // vm_flags => VMA段的权限
        // perm => 对应物理页的权限(因为是用户程序，所以设置为PTE_U用户态)
        vm_flags = 0, perm = PTE_U | PTE_V;

        // 根据文件头中的配置，设置VMA段的权限
        if (ph->p_flags & ELF_PF_X)
            vm_flags |= VM_EXEC;
        if (ph->p_flags & ELF_PF_W)
            vm_flags |= VM_WRITE;
        if (ph->p_flags & ELF_PF_R)
            vm_flags |= VM_READ;

        // 修改 RISC-V 中的权限位
        if (vm_flags & VM_READ)
            perm |= PTE_R;
        if (vm_flags & VM_WRITE)
            perm |= (PTE_W | PTE_R);
        if (vm_flags & VM_EXEC)
            perm |= PTE_X;

        // 在mm中建立ph->p_va到ph->va+ph->p_memsz的合法虚拟地址空间段
        if ((ret = mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL)) != 0)
        {
            goto bad_cleanup_mmap;
        }

        unsigned char *from = binary + ph->p_offset;
        size_t off, size;
        uintptr_t start = ph->p_va, end, la = ROUNDDOWN(start, PGSIZE);

        ret = -E_NO_MEM;

        //(3.6) 分配内存，并将每个程序段的内容（from, from+end）复制到进程的内存（la, la+end）
        end = ph->p_va + ph->p_filesz;

        //(3.6.1) 复制 TEXT/DATA 段的内容
        // 上面建立了合法的虚拟地址段，现在为这个虚拟地址段分配实际的物理内存页
        while (start < end)
        {
            // 分配一个内存页，建立la对应页的虚实映射关系
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL)
            {
                goto bad_cleanup_mmap;
            }

            off = start - la, size = PGSIZE - off, la += PGSIZE;

            if (end < la)
            {
                size -= la - end;
            }

            // 根据elf中程序头的设置，将binary中的对应数据复制到新分配的物理页中
            memcpy(page2kva(page) + off, from, size);
            start += size, from += size;
        }

        //(3.6.2) 构建二进制程序的 BSS 段
        end = ph->p_va + ph->p_memsz;

        // start < la代表BSS段存在，且最后一个物理页没有被填满。剩下空间作为BSS段
        if (start < la)
        {
            if (start == end)
            {
                // 如果 start 等于 end，则继续下一个段
                continue;
            }

            off = start + PGSIZE - la, size = PGSIZE - off;

            if (end < la)
            {
                size -= la - end;
            }

            // 将BSS段所属的部分格式化清零
            memset(page2kva(page) + off, 0, size);
            start += size;

            assert((end < la && start == end) || (end >= la && start == la));
        }
        // start < end代表还需要为BSS段分配更多的物理空间
        while (start < end)
        {
            // 为BSS段分配更多的物理页
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL)
            {
                goto bad_cleanup_mmap;
            }

            off = start - la, size = PGSIZE - off, la += PGSIZE;

            if (end < la)
            {
                size -= la - end;
            }
            // 将BSS段所属的部分格式化清零
            memset(page2kva(page) + off, 0, size);
            start += size;
        }
    }
    //(4) 构建用户栈内存
    vm_flags = VM_READ | VM_WRITE | VM_STACK;
    // 为用户栈设置对应的合法虚拟内存空间
    if ((ret = mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE, vm_flags, NULL)) != 0)
    {
        goto bad_cleanup_mmap;
    }

    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - PGSIZE, PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - 2 * PGSIZE, PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - 3 * PGSIZE, PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - 4 * PGSIZE, PTE_USER) != NULL);

    //(5) 设置当前进程的 mm、sr3，并设置 CR3 寄存器为页目录表的物理地址
    mm_count_inc(mm);                // 当前mm被线程引用次数+1
    current->mm = mm;                // 设置当前线程的mm
    current->cr3 = PADDR(mm->pgdir); // 设置当前线程的cr3
    lcr3(PADDR(mm->pgdir));          // 将指定的页表地址mm->pgdir，加载进cr3寄存器

    //(6) 为用户环境设置 trapframe
    struct trapframe *tf = current->tf;

    // 保留 sstatus 寄存器的值
    uintptr_t sstatus = tf->status;
    memset(tf, 0, sizeof(struct trapframe));

    /* LAB5:EXERCISE1 YOURCODE
     * 应该设置 tf->gpr.sp, tf->epc, tf->status
     * 注意: 如果正确设置 trapframe，用户级别的进程可以从内核返回到用户模式。
     *       tf->gpr.sp 应该是用户栈的顶部（sp 的值）
     *       tf->epc 应该是用户程序的入口点（sepc 的值）
     *       tf->status 应该适合用户程序（sstatus 的值）
     *       提示: 检查 SPP、SPIE 在 SSTATUS 中的含义，使用宏 SSTATUS_SPP、SSTATUS_SPIE（在 risv.h 中定义）
     */
    tf->gpr.sp = USTACKTOP;                               // 设置用户态的栈顶指针
    tf->epc = elf->e_entry;                               // 设置系统调用中断返回后执行的程序入口，令其为elf头中设置的e_entry(中断返回后会复原中断栈帧中的eip)
    tf->status = sstatus & ~(SSTATUS_SPP | SSTATUS_SPIE); // 默认中断返回后，用户态执行时是开中断的

    ret = 0;

out:
    return ret;
bad_cleanup_mmap:
    exit_mmap(mm);
bad_elf_cleanup_pgdir:
    put_pgdir(mm);
bad_pgdir_cleanup_mm:
    mm_destroy(mm);
bad_mm:
    goto out;
}

// do_execve - call exit_mmap(mm)&put_pgdir(mm) to reclaim memory space of current process
//           - call load_icode to setup new memory space accroding binary prog.
int do_execve(const char *name, size_t len, unsigned char *binary, size_t size)
{
    struct mm_struct *mm = current->mm;
    if (!user_mem_check(mm, (uintptr_t)name, len, 0))
    {
        return -E_INVAL;
    }
    if (len > PROC_NAME_LEN)
    {
        len = PROC_NAME_LEN;
    }

    char local_name[PROC_NAME_LEN + 1];
    memset(local_name, 0, sizeof(local_name));
    memcpy(local_name, name, len);

    if (mm != NULL)
    {
        cputs("mm != NULL");
        lcr3(boot_cr3);
        if (mm_count_dec(mm) == 0)
        {
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
        current->mm = NULL;
    }
    int ret;
    if ((ret = load_icode(binary, size)) != 0)
    {
        goto execve_exit;
    }
    set_proc_name(current, local_name);
    return 0;

execve_exit:
    do_exit(ret);
    panic("already exit: %e.\n", ret);
}

// do_yield - ask the scheduler to reschedule
int do_yield(void)
{
    current->need_resched = 1;
    return 0;
}

// kern/process/proc.c
// do_wait - wait one OR any children with PROC_ZOMBIE state, and free memory space of kernel stack
//         - proc struct of this child.
// 令当前线程等待一个或多个子线程进入僵尸态，并且回收其内核栈和线程控制块
// NOTE: only after do_wait function, all resources of the child proces are free.
// 注意：只有在do_wait函数执行完成之后，子线程的所有资源才被完全释放
int do_wait(int pid, int *code_store)
{
    struct mm_struct *mm = current->mm;
    if (code_store != NULL)
    {
        if (!user_mem_check(mm, (uintptr_t)code_store, sizeof(int), 1))
        {
            return -E_INVAL;
        }
    }

    struct proc_struct *proc;
    bool intr_flag, haskid;
repeat:
    haskid = 0;
    if (pid != 0)
    {
        // 参数指定了pid(pid不为0)，代表回收pid对应的僵尸态线程
        proc = find_proc(pid);
        // 对应的线程必须是当前线程的子线程
        if (proc != NULL && proc->parent == current)
        {
            haskid = 1;
            // pid对应的线程确实是僵尸态，跳转found进行回收
            if (proc->state == PROC_ZOMBIE)
            {
                goto found;
            }
        }
    }
    else
    {
        // 参数未指定pid(pid为0)，代表回收当前线程的任意一个僵尸态子线程
        proc = current->cptr;
        // 遍历当前线程的所有子线程进行查找
        for (; proc != NULL; proc = proc->optr)
        {
            haskid = 1;
            if (proc->state == PROC_ZOMBIE)
            {
                // 找到了一个僵尸态子线程，跳转found进行回收
                goto found;
            }
        }
    }
    if (haskid)
    {
        // 当前线程需要回收僵尸态子线程，但是没有可以回收的僵尸态子线程(如果找到去执行found段会直接返回，不会执行到这里)
        // 令当前线程进入休眠态，让出CPU
        current->state = PROC_SLEEPING;
        // 令其等待状态置为等待子进程退出
        current->wait_state = WT_CHILD;
        // 进行一次线程调度(当有子线程退出进入僵尸态时，父线程会被唤醒)
        schedule();
        if (current->flags & PF_EXITING)
        {
            // 如果当前线程被杀了(do_kill),将自己退出
            do_exit(-E_KILLED);
        }
        // schedule调度完毕后当前线程被再次唤醒，跳转到repeat循环起始位置，继续尝试回收一个僵尸态子线程
        goto repeat;
    }
    return -E_BAD_PROC;

found:
    if (proc == idleproc || proc == initproc)
    {
        // idleproc和initproc是不应该被回收的
        panic("wait idleproc or initproc.\n");
    }
    if (code_store != NULL)
    {
        // 将子线程退出的原因保存在*code_store中返回
        *code_store = proc->exit_code;
    }
    local_intr_save(intr_flag);
    {
        // 暂时关中断，避免中断导致并发问题
        // 从线程控制块hash表中移除被回收的子线程
        unhash_proc(proc);
        // 从线程控制块链表中移除被回收的子线程
        remove_links(proc);
    }
    local_intr_restore(intr_flag);
    // 释放被回收的子线程的内核栈
    put_kstack(proc);
    // 释放被回收的子线程的线程控制块结构
    kfree(proc);
    return 0;
}

// do_kill - kill process with pid by set this process's flags with PF_EXITING
int do_kill(int pid)
{
    struct proc_struct *proc;
    if ((proc = find_proc(pid)) != NULL)
    {
        if (!(proc->flags & PF_EXITING))
        {
            proc->flags |= PF_EXITING;
            if (proc->wait_state & WT_INTERRUPTED)
            {
                wakeup_proc(proc);
            }
            return 0;
        }
        return -E_KILLED;
    }
    return -E_INVAL;
}

// kernel_execve - do SYS_exec syscall to exec a user program called by user_main kernel_thread
static int
kernel_execve(const char *name, unsigned char *binary, size_t size)
{
    int64_t ret = 0, len = strlen(name);
    //   ret = do_execve(name, len, binary, size);
    asm volatile(
        "li a0, %1\n"
        "lw a1, %2\n"
        "lw a2, %3\n"
        "lw a3, %4\n"
        "lw a4, %5\n"
        "li a7, 10\n"
        "ebreak\n"
        "sw a0, %0\n"
        : "=m"(ret)
        : "i"(SYS_exec), "m"(name), "m"(len), "m"(binary), "m"(size)
        : "memory"); // 这里内联汇编的格式，和用户态调用ecall的格式类似，只是ecall换成了ebreak
    cprintf("ret = %d\n", ret);
    return ret;
}

#define __KERNEL_EXECVE(name, binary, size) ({           \
    cprintf("kernel_execve: pid = %d, name = \"%s\".\n", \
            current->pid, name);                         \
    kernel_execve(name, binary, (size_t)(size));         \
})

#define KERNEL_EXECVE(x) ({                                    \
    extern unsigned char _binary_obj___user_##x##_out_start[], \
        _binary_obj___user_##x##_out_size[];                   \
    __KERNEL_EXECVE(#x, _binary_obj___user_##x##_out_start,    \
                    _binary_obj___user_##x##_out_size);        \
})

#define __KERNEL_EXECVE2(x, xstart, xsize) ({   \
    extern unsigned char xstart[], xsize[];     \
    __KERNEL_EXECVE(#x, xstart, (size_t)xsize); \
})

#define KERNEL_EXECVE2(x, xstart, xsize) __KERNEL_EXECVE2(x, xstart, xsize)

// user_main - kernel thread used to exec a user program
static int
user_main(void *arg)
{
#ifdef TEST
    KERNEL_EXECVE2(TEST, TESTSTART, TESTSIZE);
#else
    KERNEL_EXECVE(exit);
#endif
    panic("user_main execve failed.\n");
}

// init_main - the second kernel thread used to create user_main kernel threads
static int init_main(void *arg)
{
    size_t nr_free_pages_store = nr_free_pages();
    size_t kernel_allocated_store = kallocated();

    // fork创建一个线程执行user_main
    int pid = kernel_thread(user_main, NULL, 0);
    if (pid <= 0)
    {
        panic("create user_main failed.\n");
    }

    // do_wait等待回收僵尸态子线程(第一个参数pid为0代表回收任意僵尸子线程)
    while (do_wait(0, NULL) == 0)
    {
        // 回收一个僵尸子线程后，进行调度
        schedule();
    }

    // 跳出了上述循环代表init_main的所有子线程都退出并回收完了
    cprintf("all user-mode processes have quit.\n");
    assert(initproc->cptr == NULL && initproc->yptr == NULL && initproc->optr == NULL);
    assert(nr_process == 2);
    assert(list_next(&proc_list) == &(initproc->list_link));
    assert(list_prev(&proc_list) == &(initproc->list_link));

    cprintf("init check memory pass.\n");
    return 0;
}

// proc_init - set up the first kernel thread idleproc "idle" by itself and
//           - create the second kernel thread init_main
void proc_init(void)
{
    int i;

    list_init(&proc_list);
    for (i = 0; i < HASH_LIST_SIZE; i++)
    {
        list_init(hash_list + i);
    }

    if ((idleproc = alloc_proc()) == NULL)
    {
        panic("cannot alloc idleproc.\n");
    }

    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;
    set_proc_name(idleproc, "idle");
    nr_process++;

    current = idleproc;

    int pid = kernel_thread(init_main, NULL, 0);
    if (pid <= 0)
    {
        panic("create init_main failed.\n");
    }

    initproc = find_proc(pid);
    set_proc_name(initproc, "init");

    assert(idleproc != NULL && idleproc->pid == 0);
    assert(initproc != NULL && initproc->pid == 1);
}

// cpu_idle - at the end of kern_init, the first kernel thread idleproc will do below works
void cpu_idle(void)
{
    while (1)
    {
        if (current->need_resched)
        {
            schedule();
        }
    }
}
