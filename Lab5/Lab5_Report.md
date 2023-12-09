# OS Lab 5

Contributors：焦心雨(2112536)、李艺楠(2110246)、辛浩然(2112514)

- 包括练习的内容；
- Challenge1、Challenge2

[GitHub链接](https://github.com/Herren-Hsing/Operation-System)

## 练习 0：填写已有实验

由于Lab5在进程控制块`proc_sturct`中加入了`exit_code`、`wait_state`以及标识线程之间父子关系的链表节点`*cptr`, `*yptr`, `*optr`。

```c
// kern/process/proc.h
int exit_code;   // 退出码，当前线程退出时的原因(在回收子线程时会发送给父线程)
uint32_t wait_state;  // 等待状态
// cptr即child ptr，当前线程子线程(链表结构)
// yptr即younger sibling ptr；
// optr即older sibling ptr;
// cptr为当前线程的子线程双向链表头结点，通过yptr和optr可以找到关联的所有子线程
struct proc_struct *cptr, *yptr, *optr; // 进程之间的关系
```

因此，分配进程控制块的`alloc_proc`函数有所改变：

```c
// 新增的两行
proc->wait_state = 0;  // PCB 新增的条目，初始化进程等待状态
proc->cptr = proc->optr = proc->yptr = NULL; //指针初始化
```

而在`do_fork`函数中，还需要设置进程之间的关系，直接调用`set_links`函数即可。

```c
// do_fork 函数新增
set_links(proc);  // 设置进程链接
```

`set_links`函数将进程加入进程链表，设置进程关系，并将`nr_process`加1。

```c
static void set_links(struct proc_struct *proc) {
    list_add(&proc_list,&(proc->list_link)); // 进程加入进程链表
    proc->yptr = NULL;  // 当前进程的 younger sibling 为空
    if ((proc->optr = proc->parent->cptr) != NULL) {
        proc->optr->yptr = proc;  // 当前进程的 older sibling 为当前进程
    }
    proc->parent->cptr = proc;  // 父进程的子进程为当前进程
    nr_process ++;  //进程数加一
}
```

## 练习 1：加载应用程序并执行（需要编码）

> **do_execv**函数调用`load_icode`（位于kern/process/proc.c中）来加载并解析一个处于内存中的ELF执行文件格式的应用程序。你需要补充`load_icode`的第6步，建立相应的用户内存空间来放置应用程序的代码段、数据段等，且要设置好`proc_struct`结构中的成员变量trapframe中的内容，确保在执行此进程后，能够从应用程序设定的起始执行地址开始执行。需设置正确的trapframe内容。
>
> 请在实验报告中简要说明你的设计实现过程。
>
> - 请简要描述这个用户态进程被ucore选择占用CPU执行（RUNNING态）到具体执行应用程序第一条指令的整个经过。

### （一）设计实现过程

`load_icode`的六步工作内容如下：

- 第一步：为当前进程创建一个新的 `mm` 结构
- 第二步：创建一个新的页目录表（`PDT`），并将` mm->pgdir `设置为页目录表的内核虚拟地址。
- 第三步：构建二进制的 `BSS` 部分到进程的内存空间
- 第四步：构建用户栈内存
- 第五步： 设置当前进程的 `mm`、`sr3`，并设置 `CR3` 寄存器为页目录表的物理地址
- 第六步：为用户环境设置 `trapframe`。框架代码已经保存中断前的`sstatus`寄存器。

在第六步中，我们需要设置 `tf->gpr.sp`， `tf->epc`以及 `tf->status`。

-  `tf->gpr.sp`：用户进程的栈指针。每个用户进程会有两个栈，一个内核栈一个用户栈，在这里我们使用`kern\mm\memlayout.h`中的宏定义`USTACKTOP`即用户栈的顶部赋值给sp寄存器。
-  `tf->epc`：用户程序的入口点。在`ELF`格式中，文件头部的结构体`elfhdr`中有一个字段`e_entry`表示可执行文件的入口点，我们将其赋值给`epc`作为用户程序的入口点。
-  `tf->status`：用户程序中需要修改status寄存器的`SPP`位与`SPIE`位。`SPP`记录的是在中断之前处理器的特权级别，0表示`U-Mode`，1表示`S-Mode`。`SPIE`位记录的是在中断之前中断是否开启，0表示中断开启，1表示中断关闭。我们的目的是让CPU进入`U_mode`执行`do_execve()`加载的用户程序，在返回时要通过`SPP`位回到用户模式，因此需要把`SSTATUS_SPP`置0。默认中断返回后，用户态执行时是开中断的，因此`SPIE`位也要置零。总结来说我们需要把保留的中断前的寄存器`sstatus`中的`SSTATUS_SPP`以及`SSTATUS_SPIE`位清零。

最终我们编写的代码如下：

```C
tf->gpr.sp = USTACKTOP;//设置用户态的栈顶指针  
tf->epc = elf->e_entry;//设置系统调用中断返回后执行的程序入口为elf头中设置的e_entry
tf->status = sstatus & ~(SSTATUS_SPP | SSTATUS_SPIE);//设置sstatus寄存器清零SSTATUS_SPP位和SSTATUS_SPIE位
```

### （二）过程简述

本实验中第一个用户进程是由第二个内核线程`initproc`通过把应用程序执行码覆盖到`initproc`的用户虚拟内存空间来创建的。

（1）`kern\process\proc.c\init_main`中使用`kernel_thread`函数创建了一个子线程`user_main`。内核线程`initproc`在创建完成用户态进程`userproc`后调用了`do_wait`函数，`do_wait`检测到存在`RUNNABLE`的子进程后，调用`schedule`函数。

（2）`schedule`函数从进程链表中选中该`PROC_RUNNABLE`态的进程，调用`proc_run()`函数运行该进程。

（3）进入`user_main`线程后，通过宏`KERNEL_EXECVE`宏定义调用`__KERNEL_EXECVE`函数，从中调用`kernel_execve`函数。首先将系统调用号、函数名称、函数长度、代码地址、代码大小存储在寄存器中。由于目前我们在S mode下，所以不能通过`ecall`来产生中断，只能通过设置`a7`寄存器的值为`10`后用`ebreak`产生断点中断转发到`syscall()`来实现在内核态使用系统调用。之后将存储在`a0`寄存器中的系统调用号作为返回值返回。

（4）当`ucore`收到此系统调用后，首先进入`kern\trap\trapentry.S`中的`_alltraps`保存当前的寄存器状态，然后跳转到`trap`函数中根据发生的陷阱类型进行分发。`trap_dispatch`根据`tf->cause`将处理任务分发给`exception_handler`，之后根据寄存器`a7`的值为`10`调用函数`syscall()`。

（5）在内核态的函数`syscall()`中通过`trapframe`读取寄存器的值，将系统调用号以及其他参数传给函数指针的数组syscalls。

（6）在调用过程中`syscalls=SYS_exec`，因此会调用函数`sys_exec`。函数`sys_exec`对于四个参数进行处理后调用函数`do_execve(name, len, binary, size)`。

（7）`do_execve()`函数中首先替换掉当前线程中的`mm_struct`为加载新的执行码做好用户态内存空间清空准备，之后调用`load_icode()`函数把新的程序加载到当前进程，并设置好进程名字。

（8）回到`exception_handler()`函数中更新内核栈位置，然后回到`__trapret`函数中将寄存器设置为应用进程的相关状态，当`__trapret`执行iret指令时就会跳转到应用程序的入口中，并且特权级也由内核态跳转到用户态，接下来就开始执行用户程序的第一条指令。

## 练习2：父进程复制自己的内存空间给子进程(需要编码)

> 创建子进程的函数`do_fork`在执行中将拷贝当前进程(即父进程)的用户内存地址空间中的合法内容到新进程中(子进程)，完成内存资源的复制。具体是通过`copy_range`函数(位于`kern/mm/pmm.c`中)实现的，请补充`copy_range`的实现，确保能够正确执行。
>
> 请在实验报告中简要说明你的设计实现过程。
>
> - 如何设计实现`Copy on Write`机制？给出概要设计，鼓励给出详细设计。

`Copy on Write`机制见Challenge部分。

在`do_fork`函数中，通过调用`copy_mm`来执行内存空间的复制。在这一过程中，进一步调用了`dup_mmap`函数，该函数的核心操作是遍历父进程的所有合法虚拟内存空间，然后将这些空间的内容复制到子进程的内存空间中。具体而言，这一内存复制的实现是通过`copy_range`函数完成的。

`copy_range`函数的执行流程具体包括遍历父进程指定的某一段内存空间中的每一个虚拟页。在存在虚拟页的情况下，为子进程相同地址申请分配一个物理页，接着将父进程虚拟页中的所有内容复制到新分配的物理页中。随后，建立子进程的这个物理页和对应的虚拟地址之间的映射关系。

练习需要完成的部分是复制页面的内容到子进程需要被填充的物理页`npage`，建立`npage`的物理地址到线性地址`start`的映射。步骤如下：

1. 使用宏`page2kva(page)`  找到父进程需要复制的物理页的内核虚拟地址；
2. 使用宏`page2kva(npage)` 找到子进程需要被填充的物理页的内核虚拟地址；
3. 使用`memcpy(kva_dst, kva_src, PGSIZE)`将父进程的物理页的内容复制到子进程中去；
4. 通过`page_insert(to, npage, start, perm)`建立子进程的物理页与虚拟页的映射关系。

代码如下：

```c
void *kva_src = page2kva(page); 
void *kva_dst = page2kva(npage); 
memcpy(kva_dst, kva_src, PGSIZE); 
ret = page_insert(to, npage, start, perm);关系
```

完成所有编码后，`make grade`输出截图：

![image.png](https://s2.loli.net/2023/12/03/ul4IKiGJ2ebQXcV.png)



## 练习3：阅读分析源代码，理解进程执行 fork/exec/wait/exit 的实现，以及系统调用的实现（不需要编码）

> 请在实验报告中简要说明你对 fork/exec/wait/exit函数的分析。并回答如下问题：
>
> - 请分析fork/exec/wait/exit的执行流程。重点关注哪些操作是在用户态完成，哪些是在内核态完成？内核态与用户态程序是如何交错执行的？内核态执行结果是如何返回给用户程序的？
> - 请给出ucore中一个用户态进程的执行状态生命周期图（包括执行状态，执行状态之间的变换关系，以及产生变换的事件或函数调用）。（字符方式画即可）

### （一）ucore的系统调用:

- SYS_exit：进程退出                          -->do_exit

- SYS_fork：创建子进程，复制内存映像            -->do_fork-->wakeup_proc

- SYS_wait ：等待进程                            -->do_wait

- SYS_exec ：在fork之后，进程执行程序  -->load a program and refresh the mm

- SYS_clone ：创建子线程                    -->do_fork-->wakeup_proc

- SYS_yield：放弃 CPU 时间片，进程标志需要重新调度- -->proc->need_sched=1, then scheduler will rescheule this process

- SYS_sleep：进程休眠                           -->do_sleep 

- SYS_kill ：终止进程                           -->do_kill-->proc->flags |= PF_EXITING                                        

  ​                                                                            -->wakeup_proc-->do_wait-->do_exit   

- SYS_getpid ：获取进程的PID

### （二）问题回答

1. 请分析fork/exec/wait/exit的执行流程。重点关注哪些操作是在用户态完成，哪些是在内核态完成？内核态与用户态程序是如何交错执行的？内核态执行结果是如何返回给用户程序的？

   在用户态下，进程只能执行一般的指令，无法执行特权指令。因此，系统调用机制为用户进程提供了一个统一的接口层，使其能够获得操作系统的服务。通过调用系统调用接口，用户进程可以方便地请求操作系统提供各种功能和服务，从而简化了用户进程的实现。这种机制使得用户进程可以在受限的特权级别下安全地执行，并且提供了一种可控的方式来访问操作系统的功能。

   用户态使用`syscall`的逻辑是：

   (1) 实现用户态程序进行系统调用的接口，这些接口最终会去调用用户态的`syscall`函数(`users/libs/syscall.c`)。此时为U mode。

   - `user/libs/ulib.[ch]`：实现了最小的C函数库，除了一些与系统调用无关的函数，其他函数是对访问系统调用的包装。

     - 对访问系统调用的包装，提供了用户级应用程序常用的接口，如创建新进程(`fork`)、等待进程退出(`wait`、`waitpid`)、放弃 CPU 时间片(`yield`)、终止进程(`kill`、`exit`)、获取进程PID(`getpid`)等。

     - 比如：

     - ```c
       void yield(void) {  sys_yield();  }
       ```

   - `user/libs/syscall.[ch]`：用户层发出系统调用的具体实现。

     - 会去调用`syscall`函数，这个函数后面分析。比如：

     - ```c
       int  sys_yield(void) { return syscall(SYS_yield);  }
       ```

   (2) `users` 里的`syscall`，通过内联汇编进行`ecall`调用。这将产生一个trap, 此时由U mode 进入S mode进行异常处理。

   ```c
   // users 里的syscall，触发ecall调用kernal的syscall
   // users/libs/syscall.c 位于user目录下
   static inline int syscall(int num, ...) {
       //va_list, va_start, va_arg都是C语言处理参数个数不定的函数的宏
       //在stdarg.h里定义
       va_list ap; //ap: 参数列表(此时未初始化)
       va_start(ap, num); //初始化参数列表, 从num开始
       //First, va_start initializes the list of variable arguments as a va_list.
       uint64_t a[MAX_ARGS];
       int i, ret;
       for (i = 0; i < MAX_ARGS; i ++) { //把参数依次取出
              /*Subsequent executions of va_arg yield the values of the additional arguments 
              in the same order as passed to the function.*/
           a[i] = va_arg(ap, uint64_t);
       }
       va_end(ap); //Finally, va_end shall be executed before the function returns.
       asm volatile (
           "ld a0, %1\n"
           "ld a1, %2\n"
           "ld a2, %3\n"
           "ld a3, %4\n"
           "ld a4, %5\n"
           "ld a5, %6\n"
           "ecall\n"
           "sd a0, %0"
           : "=m" (ret)
           : "m"(num), "m"(a[0]), "m"(a[1]), "m"(a[2]), "m"(a[3]), "m"(a[4])
           :"memory");
       //num存到a0寄存器， a[0]存到a1寄存器
       //ecall的返回值存到ret
       return ret;
   }
   ```

   (3)`trap.c`转发系统调用，触发内核态的系统调用(`kern/syscall/syscall.c`)。此时为S mode。

   ```c
   // kern/trap/trap.c
   void exception_handler(struct trapframe *tf) {
       int ret;
       switch (tf->cause) { //通过中断帧里 scause寄存器的数值，判断出当前是来自USER_ECALL的异常
           case CAUSE_USER_ECALL:
               //cprintf("Environment call from U-mode\n");
               tf->epc += 4; 
               //sepc寄存器是产生异常的指令的位置，在异常处理结束后，会回到sepc的位置继续执行
               //对于ecall, 我们希望sepc寄存器要指向产生异常的指令(ecall)的下一条指令
               //否则就会回到ecall执行再执行一次ecall, 无限循环
               syscall();// 进行系统调用处理
               break;
           /*other cases .... */
       }
   }
   ```

   (4) 在内核态的`syscall`函数中，根据参数调用编号调用相应函数。此时为S mode。

   ```c
   // kern/syscall/syscall.c
   void syscall(void) {
       struct trapframe *tf = current->tf;
       uint64_t arg[5];
       int num = tf->gpr.a0;  // a0寄存器保存了系统调用编号
       if (num >= 0 && num < NUM_SYSCALLS) { //  防止syscalls[num]下标越界
           if (syscalls[num] != NULL) {
               arg[0] = tf->gpr.a1;
               arg[1] = tf->gpr.a2;
               arg[2] = tf->gpr.a3;
               arg[3] = tf->gpr.a4;
               arg[4] = tf->gpr.a5;
               tf->gpr.a0 = syscalls[num](arg); 
               // 把寄存器里的参数取出来，转发给系统调用编号对应的函数进行处理
               return ;
           }
       }
       //如果执行到这里，说明传入的系统调用编号还没有被实现，就崩掉了。
       print_trapframe(tf);
       panic("undefined syscall %d, pid = %d, name = %s.\n",
               num, current->pid, current->name);
   }
   ```

   其中`tf->gpr.a0 = syscalls[num](arg); `指令，去根据参数调用相应的对应函数。

   ```c
   // kern/syscall/syscall.c
   // 这里定义了函数指针的数组syscalls, 把每个系统调用编号的下标上初始化为对应的函数指针
   static int (*syscalls[])(uint64_t arg[]) = {
       [SYS_exit]              sys_exit,
       [SYS_fork]              sys_fork,
       [SYS_wait]              sys_wait,
       [SYS_exec]              sys_exec,
       [SYS_yield]             sys_yield,
       [SYS_kill]              sys_kill,
       [SYS_getpid]            sys_getpid,
       [SYS_putc]              sys_putc,
   };
   
   // syscall的数量
   #define NUM_SYSCALLS        ((sizeof(syscalls)) / (sizeof(syscalls[0])))
   
   // libs/unistd.h
   // 这些编号
   /* syscall number */
   #define SYS_exit            1
   #define SYS_fork            2
   #define SYS_wait            3
   #define SYS_exec            4
   #define SYS_clone           5
   #define SYS_yield           10
   #define SYS_sleep           11
   #define SYS_kill            12
   #define SYS_gettime         17
   #define SYS_getpid          18
   #define SYS_brk             19
   #define SYS_mmap            20
   #define SYS_munmap          21
   #define SYS_shmem           22
   #define SYS_putc            30
   #define SYS_pgdir           31
   ```

   (5) 相应函数执行流程，均在S mode中进行

   - **fork**

     调用过程：SYS_fork->do_fork + wakeup_proc

     ```c
     static int
     sys_fork(uint64_t arg[]) {
         struct trapframe *tf = current->tf;
         uintptr_t stack = tf->gpr.sp;
         return do_fork(0, stack, tf); // clone_flags 为0，不可以共享地址空间
     }
     ```

     do_fork执行流程：

     1、分配并初始化进程控制块(alloc_proc 函数);
     2、分配并初始化内核栈(setup_stack 函数);
     3、根据 clone_flag标志复制或共享进程内存管理结构(copy_mm 函数);
     4、设置进程在内核(将来也包括用户态)正常运行和调度所需的中断帧和执行上下文(copy_thread 函数);
     5、把设置好的进程控制块放入hash_list 和 proc_list 两个全局进程链表中;
     6、自此,进程已经准备好执行了,把进程状态设置为“就绪”态;
     7、设置返回码为子进程的 id 号。

   - **exec**

     调用过程： SYS_exec->do_execve

     ```c
     static int
     sys_exec(uint64_t arg[]) {
         const char *name = (const char *)arg[0];
         size_t len = (size_t)arg[1];
         unsigned char *binary = (unsigned char *)arg[2];
         size_t size = (size_t)arg[3];
         return do_execve(name, len, binary, size);
     }
     ```

     do_execve执行流程：

     1、首先为加载新的执行码做好用户态内存空间清空准备。如果mm不为NULL，则设置页表为内核空间页表，且进一步判断mm的引用计数减1后是否为0，如果为0，则表明没有进程再需要此进程所占用的内存空间，为此将根据mm中的记录，释放进程所占用户空间内存和进程页表本身所占空间。最后把当前进程的mm内存管理指针为空。
     2、接下来是加载应用程序执行码到当前进程的新创建的用户态虚拟空间中。之后就是调用load_icode从而使之准备好执行。

   - **wait**

     调用过程： SYS_wait->do_wait

     ```c
     static int
     sys_wait(uint64_t arg[]) {
         int pid = (int)arg[0];
         int *store = (int *)arg[1];
         return do_wait(pid, store);
     }
     ```

     `sys_wait `的核心逻辑在 `do_wait` 函数中，根据传入的参数 `pid` 决定是回收指定 `pid` 的子线程还是任意一个子线程。

     do_wait执行流程：

     1、 如果 pid!=0，表示只找一个进程 id 号为 pid 的退出状态的子进程，否则找任意一个处于退出状态的子进程;
     2、 如果此子进程的执行状态不为PROC_ZOMBIE，表明此子进程还没有退出，则当前进程设置执行状态为PROC_SLEEPING（睡眠），睡眠原因为WT_CHILD(即等待子进程退出)，调用schedule()函数选择新的进程执行，自己睡眠等待，如果被唤醒，则重复跳回步骤 1 处执行;
     3、 如果此子进程的执行状态为 PROC_ZOMBIE，表明此子进程处于退出状态，需要当前进程(即子进程的父进程)完成对子进程的最终回收工作，即首先把子进程控制块从两个进程队列proc_list和hash_list中删除，并释放子进程的内核堆栈和进程控制块。自此，子进程才彻底地结束了它的执行过程，它所占用的所有资源均已释放。

   - **exit**

     调用过程：sys_exit->do_exit

     ```c
     static int
     sys_exit(uint64_t arg[]) {
         int error_code = (int)arg[0];
         return do_exit(error_code);
     }
     ```

     do_exit执行流程：

     1、先判断是否是用户进程，如果是，则开始回收此用户进程所占用的用户态虚拟内存空间;（具体的回收过程不作详细说明）
     2、设置当前进程状态为PROC_ZOMBIE，然后设置当前进程的退出码为error_code。表明此时这个进程已经无法再被调度了，只能等待父进程来完成最后的回收工作（主要是回收该子进程的内核栈、进程控制块）
     3、如果当前父进程已经处于等待子进程的状态，即父进程的wait_state被置为WT_CHILD，则此时就可以唤醒父进程，让父进程来帮子进程完成最后的资源回收工作。
     4、如果当前进程还有子进程,则需要把这些子进程的父进程指针设置为内核线程init,且各个子进程指针需要插入到init的子进程链表中。如果某个子进程的执行状态是 PROC_ZOMBIE,则需要唤醒 init来完成对此子进程的最后回收工作。
     5、执行schedule()调度函数，选择新的进程执行。

   (6) 系统调用完中断返回，进入用户态进程，由S mode转为U mode 

   在`_trapret`处使用RESTORE_ALL恢复所有寄存器，如果是用户态产生的中断，此时sp恢复为用户栈指针。然后调用sret指令从S mode返回U mode。其中，sret指令将状态的程序计数器(PC)和特权级别寄存器(如状态寄存器)中保存的U态的值加回到对应的寄存器中。同时，sret指令也会将S态的特权级别切换回到U态，这样处理器就会从S态切换回到U态，并开始执行U态的指令。

   在用户态的syscall中，将ecall的返回值存到`ret`，并将`ret` 变量被存储到 `a0` 寄存器中，因此内核态执行结果通过寄存器 `a0` 返回给用户程序。

2. 请给出ucore中一个用户态进程的执行状态生命周期图（包括执行状态，执行状态之间的变换关系，以及产生变换的事件或函数调用）。（字符方式画即可）

   ```
   process state changing:
   
     alloc_proc                              RUNNING
         +                                +--<----<--+
         +                                + proc_run +
         V                                +-->---->--+
   PROC_UNINIT - proc_init/wakeup_proc -> PROC_RUNNABLE - try_free_pages/do_wait/do_sleep -> PROC_SLEEPING -
                                              A      +                                                     +   
                                              |      +--- do_exit --> PROC_ZOMBIE                          +   
                                              +                                                            +   
                                              -----------------------wakeup_proc----------------------------
   ```

   


## 扩展练习 Challenge1

> 实现 Copy on Write （COW）机制
>
> 给出实现源码，测试用例和设计报告（包括在cow情况下的各种状态转换（类似有限状态自动机）的说明）。
>
> 这个扩展练习涉及到本实验和上一个实验“虚拟内存管理”。请在ucore中实现这样的COW机制。
>
> 由于COW实现比较复杂，容易引入bug，请参考 https://dirtycow.ninja/ 看看能否在ucore的COW实现中模拟这个错误和解决方案。需要有解释。

COW基本机制为：在ucore操作系统中，当一个用户父进程创建自己的子进程时，父进程会把其申请的用户空间设置为只读，子进程可共享父进程占用的用户内存空间中的页面（这就是一个共享的资源）。当其中任何一个进程修改此用户内存空间中的某页面时，ucore会通过page fault异常获知该操作，并完成拷贝内存页面，使得两个进程都有各自的内存页面。这样一个进程所做的修改不会被另外一个进程可见了。

在`do_fork`函数中，内存复制通过调用`copy_mm`函数、进而调用`do_range`函数实现。`do_range`函数根据传入的参数`share`决定是否进行内存复制或共享。

如果`share`为0，则完整拷贝内存，与之前代码一致；如果`share`为1，则使用COW机制，进行物理页面共享，在两个进程页目录表中加入共享页面的映射关系，并设置只读。

```c
page_insert(from, page, start, perm & ~PTE_W);
ret = page_insert(to, page, start, perm & ~PTE_W);
```

当其中任何一个进程修改此用户内存空间中的某页面时，由于PTE上的`PTE_W`为0，所以会触发缺页异常。此时需要通过`page fault`异常获知该操作，并完成拷贝内存页面，使得两个进程都有各自的内存页面。

在`do_pgfault`函数内，会尝试获取这个地址对应的页表项，如果页表项不为空，且页表项有效，这说明缺页异常是因为试图在只读页面中写入而引起的。

在这种情况下，如果试图写入的只读页面只被一个进程使用，重设权限`PTE_W`为1并插入映射即可；

而如果被多个进程使用，需要调用`pgdir_alloc_page`函数，在该函数内分配页面并设置新地址映射。之后，将数据拷贝到新分配的页中，并将其加入全局虚拟内存交换管理器的管理。

```c
if (*ptep & PTE_V)
{
    cprintf("\n\nCOW: ptep 0x%x, pte 0x%x\n", ptep, *ptep);
    // 只读物理页
    page = pte2page(*ptep);
    // 如果该物理页面被多个进程引用
    if (page_ref(page) > 1)
    {
        // 分配页面并设置新地址映射
        // pgdir_alloc_page -> alloc_page()  page_insert()
        struct Page *newPage = pgdir_alloc_page(mm->pgdir, addr, perm);
        void *kva_src = page2kva(page);
        void *kva_dst = page2kva(newPage);
        // 拷贝数据
        memcpy(kva_dst, kva_src, PGSIZE);
    }
    // 如果该物理页面只被当前进程所引用
    else
    { 
        // page_insert，保留当前物理页，重设其PTE权限
        page_insert(mm->pgdir, page, addr, perm);
    }
}
else
{
    // Lab 3 中的代码
    // 页面被交换到了磁盘中
    // 将线性地址对应的物理页数据从磁盘交换到物理内存
}
swap_map_swappable(mm, addr, page, 1);
page->pra_vaddr = addr;
```

`make qemu`输出如下，验证了COW的正确性。

![image.png](https://s2.loli.net/2023/12/06/QIPBjC3bKxdgSvc.png)

### CVE-2016-5195 (Dirty COW) Linux本地提权漏洞

该漏洞是Linux中`get_user_page`内核函数在处理`Copy-on-Write`的过程中，可能产出竞态条件造成COW过程被破坏，导致出现写数据到进程地址空间内只读内存区域的机会。

结合Linux源码分析漏洞产生的原因。

当用`mmap`去映射文件到内存区域时使用了`MAP_PRIVATE`标记，写文件时会写到COW机制产生的内存区域中，原文件不受影响。其中获取用户进程内存页的过程如下：

1. 第一次调用`follow_page_mask`查找虚拟地址对应的page，带有`FOLL_WRITE`标记。因为所在page不在内存中，`follow_page_mask`返回NULL，第一次失败；进入`faultin_page`，最终进入`do_cow_fault`分配不带`_PAGE_RW`标记的匿名内存页，返回值为0。
2. 重新开始循环，第二次调用`follow_page_mask`，带有`FOLL_WRITE`标记。由于不满足`((flags & FOLL_WRITE) && !pte_write(pte))`条件，`follow_page_mask`返回NULL，第二次失败，进入`faultin_page`，最终进入`do_wp_page`函数分配COW页。并在上级函数`faultin_page`中去掉`FOLL_WRITE`标记，返回0。
3. 重新开始循环，第三次调用`follow_page_mask`，不带`FOLL_WRITE`标记。成功得到page。

`__get_user_pages`函数中每次查找page前会先调用`cond_resched()`线程调度一下，这样就引入了竞态条件的可能性。在第二次分配COW页成功后，`FOLL_WRITE`标记已经去掉，如果此时，另一个线程把page释放了，那么第三次由于page不在内存中，又会进行调页处理，由于不带`FOLL_WRITE`标记，不会进行COW操作，而会直接返回之前的**只读物理页**的地址，之后该**只读**页被添加到page数组，并在接下来的操作中被**成功修改**。

## 扩展练习 Challenge2

> 说明该用户程序是何时被预先加载到内存中的？与我们常用操作系统的加载有何区别，原因是什么？

### （一）实验中用户程序的加载方式

在Lab5的Makefile中会将用户程序编译到镜像中。Makefile中的user programs模块中的语句解析如下：

```assembly
# -------------------------------------------------------------------
# user programs
# 用户程序相关的头文件搜索路径
UINCLUDE	+= user/include/ \
			   user/libs/
# 用户程序源文件目录
USRCDIR		+= user
# 用户程序库目录
ULIBDIR		+= user/libs
# 用户程序编译选项
UCFLAGS		+= $(addprefix -I,$(UINCLUDE))
# 用户程序目标文件列表
USER_BINS	:=
# 添加用户库文件到编译目标中
$(call add_files_cc,$(call listf_cc,$(ULIBDIR)),ulibs,$(UCFLAGS))
# 添加用户源代码文件到编译目标中
$(call add_files_cc,$(call listf_cc,$(USRCDIR)),uprog,$(UCFLAGS))
# 生成用户程序目标文件列表
UOBJS	:= $(call read_packet,ulibs libs)
# 定义生成用户程序目标的规则
define uprog_ld
# 调用 ubinfile 宏生成的用户程序目标文件名
__user_bin__ := $$(call ubinfile,$(1))
# 存储了所有用户程序的目标文件名
USER_BINS += $$(__user_bin__)
$$(__user_bin__): tools/user.ld #依赖于链接脚本 tools/user.ld
$$(__user_bin__): $$(UOBJS)# 依赖于用户程序相关的目标文件列表 UOBJS
$$(__user_bin__): $(1) | $$$$(dir $$$$@)# 依赖于用户程序的源代码文件 ($(1))，并指定生成目标文件的路径。
	$(V)$(LD) $(LDFLAGS) -T tools/user.ld -o $$@ $$(UOBJS) $(1)# 使用 LD 进行链接，指定链接脚本为 tools/user.ld
	@$(OBJDUMP) -S $$@ > $$(call cgtype,$$<,o,asm)# 生成目标文件的反汇编文件 (.asm)
	@$(OBJDUMP) -t $$@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$$$/d' > $$(call cgtype,$$<,o,sym)# 生成目标文件的符号表文件 (.sym)
endef
# 遍历所有用户程序，生成目标
$(foreach p,$(call read_packet,uprog),$(eval $(call uprog_ld,$(p))))

```

在编译后会自动生成类似`_binary_obj___user_##x##_out_start`和`_binary_obj___user_##x##_out_size`这样的符号，按照C语言宏的语法，会直接把x的变量名代替\##x##。

`uprog_ld` 函数为每个用户程序生成目标文件后，通过链接脚本 `tools/user.ld` 来指导用户程序的链接过程。链接脚本 `tools/user.ld` 的具体解释如下：

```C
/* Simple linker script for ucore user-level programs.
   See the GNU ld 'info' manual ("info ld") to learn the syntax. */

OUTPUT_ARCH(riscv)  // 指定生成的可执行文件的体系结构为 RISC-V
ENTRY(_start)       // 指定程序的入口地址为 _start，这是用户程序的起始执行点

SECTIONS {           // 定义不同段的布局
    /* Load programs at this address: "." means the current address */
    . = 0x800020;    // 将程序加载到内存的地址 0x800020 处，这是用户程序的默认加载地址

    .text : {        // 包含代码段，用于存放程序的执行代码
        *(.text .stub .text.* .gnu.linkonce.t.*)
    }

    PROVIDE(etext = .);  // 定义符号 etext 的值为当前位置，即代码段的结束位置

    .rodata : {     // 包含只读数据段，用于存放只读的数据
        *(.rodata .rodata.* .gnu.linkonce.r.*)
    }

    /* Adjust the address for the data segment to the next page */
    . = ALIGN(0x1000);  // 调整地址，确保数据段从下一页开始

    .data : {       // 包含数据段，用于存放可读写的数据
        *(.data)
    }

    PROVIDE(edata = .);  // 定义符号 edata 的值为当前位置，即数据段的结束位置

    .bss : {        // 包含未初始化的数据段，用于存放未初始化的全局变量
        *(.bss)
    }

    PROVIDE(end = .);    // 定义符号 end 的值为当前位置，即 BSS 段的结束位置

    /DISCARD/ : {   // 丢弃一些不需要的段，包括 .eh_frame、.note.GNU-stack 和 .comment
        *(.eh_frame .note.GNU-stack .comment)
    }
}

```

通过连接器的加载，`$$(UOBJS)` 包含了用户程序的目标文件和用户库的目标文件，这些文件会被链接成最终的用户程序可执行文件 。之后根据连接器所决定的程序在内存中的布局，用户程序被加载到默认加载地址 `0x800020`，即各个段（如代码段、数据段、BSS 段等）从可执行文件复制到内存中的相应位置。

通过MakeFile的链接装入，用户程序就能够和ucore内核一起被 bootloader 加载到内存里中。

接下来在user_main()中，KERNEL_EXECVE宏会引入两个外部符号`_binary_obj___user_##x##_out_start`和`_binary_obj___user_##x##_out_size`，分别是ELF文件的开始地址和文件大小，之后作为`xstart`和`xsize`参数传递给`__KERNEL_EXECVE`用于调用`execve()`函数继续执行接下来的步骤。

### （二）操作系统的加载方式

在操作系统中装入内存有多种方式：

1. 绝对装入方式：预先知道装入的位置，编译过程中就将逻辑地址转为物理地址。
2. 可重定位装入方式：在装入的过程中，根据装入位置将装入模块中的逻辑地址修改为物理地址。
3. 动态运行装入方式：模块被装入内存后执行时通过基址寄存器来进行地址转换。

实际上机器的内存是有限的，如果同时运行多个进程，同时加载到内存时几乎不可能。因此操作系统会使用交换技术，把空闲的进程从内存中交换到磁盘上去，空余出的内存空间用于新进程的运行。如果换出去的空闲进程又需要被运行的时候，那么它就会被再次交换进内存中。

### （三）产生区别的原因

在Lab3页面置换的实验中我们分析可知QEMU里并没有真正模拟“硬盘”，我们实验中所用到的硬盘实际上是从内核的静态存储(static)区里面分出一块内存， 声称这块存储区域是”硬盘“，然后包裹一下给出”硬盘IO“的接口。 

我们目前还未实现文件系统，不能采用常用的利用文件系统的加载方式，因此只能采取makefile链接装入的形式。

## 实验中重要的知识点

用户进程在其执行过程中会存在很多种不同的执行状态，根据操作系统原理，一个用户进程一般的运行状态有五种：创建（new）态、就绪（ready）态、运行（running）态、等待（blocked）态、退出（exit）态。各个状态之间会由于发生了某事件而进行状态转换。

但在用户进程的执行过程中，具体在哪个时间段处于上述状态的呢？上述状态是如何转变的呢？首先，我们看创建（new）态，操作系统完成进程的创建工作，而体现进程存在的就是进程控制块，所以一旦操作系统创建了进程控制块，则可以认为此时进程就已经存在了，但由于进程能够运行的各种资源还没准备好，所以此时的进程处于创建（new）态。创建了进程控制块后，进程并不能就执行了，还需准备好各种资源，如果把进程执行所需要的虚拟内存空间，执行代码，要处理的数据等都准备好了，则此时进程已经可以执行了，但还没有被操作系统调度，需要等待操作系统选择这个进程执行，于是把这个做好“执行准备”的进程放入到一个队列中，并可以认为此时进程处于就绪（ready）态。当操作系统的调度器从就绪进程队列中选择了一个就绪进程后，通过执行进程切换，就让这个被选上的就绪进程执行了，此时进程就处于运行（running）态了。到了运行态后，会出现三种事件。如果进程需要等待某个事件（比如主动睡眠 10 秒钟，或进程访问某个内存空间，但此内存空间被换出到硬盘 swap 分区中了，进程不得不等待操作系统把缓慢的硬盘上的数据重新读回到内存中），那么操作系统会把 CPU 给其他进程执行，并把进程状态从运行（running）态转换为等待（blocked）态。如果用户进程的应用程序逻辑流程执行结束了，那么操作系统会把 CPU 给其他进程执行，并把进程状态从运行（running）态转换为退出（exit）态，并准备回收用户进程占用的各种资源，当把表示整个进程存在的进程控制块也回收了，这进程就不存在了。在这整个回收过程中，进程都处于退出（exit）态。2 考虑到在内存中存在多个处于就绪态的用户进程，但只有一个 CPU，所以为了公平起见，每个就绪态进程都只有有限的时间片段，当一个运行态的进程用完了它的时间片段后，操作系统会剥夺此进程的 CPU 使用权，并把此进程状态从运行（running）态转换为就绪（ready）态，最后把 CPU 给其他进程执行。如果某个处于等待（blocked）态的进程所等待的事件产生了（比如睡眠时间到，或需要访问的数据已经从硬盘换入到内存中），则操作系统会通过把等待此事件的进程状态从等待（blocked）态转到就绪（ready）态。这样进程的整个状态转换形成了一个有限状态自动机。

对于用户进程的管理，有四个系统调用比较重要。

`sys_fork()`：把当前的进程复制一份，创建一个子进程，原先的进程是父进程。接下来两个进程都会收到`sys_fork()`的返回值，如果返回0说明当前位于子进程中，返回一个非0的值（子进程的PID）说明当前位于父进程中。然后就可以根据返回值的不同，在两个进程里进行不同的处理。

`sys_exec()`：在当前的进程下，停止原先正在运行的程序，开始执行一个新程序。PID不变，但是内存空间要重新分配，执行的机器代码发生了改变。我们可以用`fork()`和`exec()`配合，在当前程序不停止的情况下，开始执行另一个程序。

`sys_exit()`：退出当前的进程。

`sys_wait()`：挂起当前的进程，等到特定条件满足的时候再继续执行。

