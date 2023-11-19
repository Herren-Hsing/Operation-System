# OS Lab 4

Contributors：焦心雨(2112536)、李艺楠(2110246)、辛浩然(2112514)

- 包括练习的内容；
- Challenge1
- 相关的知识点总结

[GitHub链接](https://github.com/Herren-Hsing/Operation-System)

## 练习1：为新创建的内核线程分配资源（需要编码）

> - `alloc\_proc`函数（位于`kern/process/proc.c`中）负责分配并返回一个新的`struct proc\_struct`结构，用于存储新建立的内核线程的管理信息。`ucore`需要对这个结构进行最基本的初始化，你需要完成这个初始化过程。
>
>   > 【提示】在`alloc\_proc`函数的实现中，需要初始化的`proc\_struct`结构中的成员变量至少包括：`state/pid/runs/kstack/need\_resched/parent/mm/context/tf/cr3/flags/name`。
>
>   请在实验报告中简要说明你的设计实现过程。请回答如下问题：
>
>    - 请说明`proc_struct`中`struct context context`和`struct trapframe *tf`成员变量含义和在本实验中的作用是啥？（提示通过看代码和编程调试可以判断出来）

### 函数设计实现

#### **1.开辟空间**

`alloc\_proc`函数的功能是分配并返回一个新的`struct proc_struct`结构，那么首先需要使用`kmalloc`函数开辟一块`proc_struct`结构的内存块。

```C
struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
```

#### **2.初始化**

如果`kmalloc()`函数返回不为空，那么就说明分配空间成功，我们就可以进行结构体的初始化。

`struct proc_struct`结构定义如下：

```C
// kern/process/proc.h
struct proc_struct {
    enum proc_state state;            // 进程状态
    int pid;                          // 进程ID
    int runs;                         // 进程运行次数
    uintptr_t kstack;                 // 进程内核栈
    volatile bool need_resched;       // 布尔值：是否需要重新调度以释放CPU？
    struct proc_struct *parent;       // 父进程
    struct mm_struct *mm;             // 进程的内存管理字段
    struct context context;           // 切换至此以运行进程
    struct trapframe *tf;             // 当前中断的陷阱帧
    uintptr_t cr3;                    // CR3寄存器：页目录表(PDT)的基地址
    uint32_t flags;                   // 进程标志
    char name[PROC_NAME_LEN + 1];     // 进程名称
    list_entry_t list_link;           // 进程链接列表
    list_entry_t hash_link;           // 进程哈希列表
};
```

接下来我们需要对每一个成员变量进行初始化，对于大部分的成员变量都需要执行清零操作，在此针对一些特殊的成员变量进行分析。

**（1）state**

state是进程所处的状态，`uCore`中进程状态有四种：分别是`PROC_UNINIT`（未初始化），`PROC_SLEEPING`（休眠），`PROC_RUNNABLE`（运行就绪），`PROC_ZOMBIE`（僵尸进程，等待父进程回收资源），因此当我们新开辟一个`struct proc_struct`结构后，还没有对应的进程信息，状态应当设置为`PROC_UNINIT`。

```C
proc->state = PROC_UNINIT;
```

**（2）pid**

在刚分配`proc_struct`结构后，我们没有立即分配进程ID，因此进程ID应当是未初始化的值。需要注意进程中的ID号是从0开始的，0号进程即idle进程，因此未初始化的进程应当赋值为-1。

```C
proc->pid = -1;
```

**（3）cr3**

 cr3 保存页表的物理地址，目的就是进程切换的时候方便直接使用cr3实现页表切换，避免每次都根据 mm 来计算 cr3。mm数据结构是用来实现用户空间的虚存管理的，但是内核线程没有用户空间，它执行的只是内核中的一小段代码（通常是一小段函数），所以它没有mm 结构，也就是NULL。当某个进程是一个普通用户态进程的时候，PCB 中的 cr3 就是 mm 中页表（pgdir）的物理地址；而当它是内核线程的时候，cr3 等于boot_cr3。而boot_cr3指向了uCore启动时建立好的内核虚拟空间的页目录表首地址。

在本次实验中，我们新建的均为内核进程，因此`proc->cr3` 被设置为 `boot_cr3`。

```C
proc->cr3 = boot_cr3;
```

**（4）其他**

- `runs`：当进程初始化时，进程的运行次数为0，设置`runs=0`
- `kstack`：当进程初始化时，进程内核栈暂未分配，因此内核栈位置设为0
- `need_resched`：当进程初始化时，默认不需要立即重新调度
- `parent`：当进程初始化时，父进程暂无，设置为NULL
- `mm`：内核线程不需要考虑换页的问题，因此把之设置为NULL
- `context`：使用`memset`函数开辟一块全零的`struct context`空间大小赋值
- `tf` ：当进程初始化时，中断帧暂无，设置为NULL
- `flags`：进程初始化时，进程标志默认为0
- `name`：进程初始化时，名字使用0填充，长度为`PROC_NAME_LEN`

#### **3.验证**

在kern\process\proc.c中的proc_init()函数在调用alloc_proc函数为idleproc初始化后，进行了一系列检查，因此我们也可以根据这些条件判断我们的初始化操作是否正确：

```C++
if(idleproc->cr3 == boot_cr3 && idleproc->tf == NULL && !context_init_flag
   && idleproc->state == PROC_UNINIT && idleproc->pid == -1 && idleproc->runs == 0
   && idleproc->kstack == 0 && idleproc->need_resched == 0 && idleproc->parent == NULL
   && idleproc->mm == NULL && idleproc->flags == 0 && !proc_name_flag
  ){
    cprintf("alloc_proc() correct!\n");
}
```

#### **4.代码展示**

根据上述分析结果，我们最终编写的初始化代码如下：

```C++
// alloc_proc - 分配一个 proc_struct 结构体并初始化 proc_struct 的所有字段
static struct proc_struct *
alloc_proc(void) {
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));  // 分配一个 proc_struct 结构体

    if (proc != NULL) {  // 如果成功分配了内存
        proc->state = PROC_UNINIT;  // 将进程状态设置为未初始化
        proc->pid = -1;  // 将进程 ID 设置为 -1，表示未分配具体的进程 ID
        proc->runs = 0;  // 进程运行次数初始化为 0
        proc->kstack = 0;  // 进程内核栈地址设置为 0，表示未分配内核栈
        proc->need_resched = 0;  // 设置进程不需要重新调度
        proc->parent = NULL;  // 将父进程指针设为 NULL
        proc->mm = NULL;  // 将内存管理指针设为 NULL
        memset(&(proc->context), 0, sizeof(struct context));  // 使用 0 填充 proc 的 context 字段
        proc->tf = NULL;  // 将进程的 trapframe 指针设为 NULL
        proc->cr3 = boot_cr3;  // 将进程的 CR3 寄存器值设置为 boot_cr3
        proc->flags = 0;  // 进程标志位初始化为 0
        memset(proc->name, 0, PROC_NAME_LEN);  // 使用 0 填充进程的名称
    }

    return proc;  // 返回分配的 proc_struct 结构体指针
}
```



### 问题回答

> 请说明`proc_struct`中`struct context context`和`struct trapframe *tf`成员变量含义和在本实验中的作用是啥？（提示通过看代码和编程调试可以判断出来）

#### 1.`struct context context`

context结构体的定义如下：

```C
struct context {
    uintptr_t ra;// 返回地址
    uintptr_t sp;// 栈指针
    uintptr_t s0;// 保存的函数指针
    uintptr_t s1;// 保存函数参数
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
```

context结构体包含了`ra`，`sp`，`s0~s11`共14个被调用者保存寄存器。他的含义是进程上下文，作用是在内核态中能够进行上下文之间的切换。

在本次实验中，context的重要作用主要是在`copy_thread`以及`switch_to`函数中，我们分别分析如下：

**（1）`copy_thread`**

在`do_fork(clone_flags | CLONE_VM, 0, &tf)` 中调用`copy_thread()`的语句如下：

```C
`do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)`{
//创建一个proc_struct结构体
struct proc_struct *proc;
......
//设置进程的中断帧和上下文
copy_thread(proc, stack, tf);
}
```

`copy_thread()`用于设置进程的中断帧和上下文，他的定义如下：

```C++
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf) {
    // 设置子进程的上下文（context）中返回地址（ra）为 forkret 函数地址
    proc->context.ra = (uintptr_t)forkret;
    // 设置子进程的上下文（context）中栈指针（sp）为新进程的陷阱帧地址
    proc->context.sp = (uintptr_t)(proc->tf);
}
```

`ra`寄存器保存返回地址，在这里设置为`forkret` 函数地址，使进程经过switch_to进程切换后会返回到`forkret`函数；`sp`寄存器保存栈指针，这里设置为进程的中断帧，`__trapret`就可以直接从中断帧里面保存的trapframe信息恢复所有的寄存器，然后开始执行代码。

因此，在copy_thread函数中，context的作用是通过寄存器设置进程的上下文状态从而帮助CPU进程切换。

**（2）`switch_to`**

在`proc_run(struct proc_struct *proc)`中调用switch_to的语句如下：

```C
// 声明当前进程和下一个要运行的进程
struct proc_struct *prev = current, *next = proc;
......
// 执行进程上下文切换，从 prev 切换到 next    
switch_to(&(prev->context), &(next->context));
```

`switch_to`函数的主要功能就是保留当前线程的上下文，并且将即将运行进程中上下文结构中的内容加载到CPU 的各个寄存器中，恢复新线程的执行流上下文现场。

`switch_to`函数的定义在`kern\process\switch.S`中，代码如下：

```assembly
.text
# void switch_to(struct proc_struct* from, struct proc_struct* to)
.globl switch_to
switch_to:
    # save from's registers
    STORE ra, 0*REGBYTES(a0)
    STORE sp, 1*REGBYTES(a0)
    STORE s0, 2*REGBYTES(a0)
    STORE s1, 3*REGBYTES(a0)
    STORE s2, 4*REGBYTES(a0)
    STORE s3, 5*REGBYTES(a0)
    STORE s4, 6*REGBYTES(a0)
    STORE s5, 7*REGBYTES(a0)
    STORE s6, 8*REGBYTES(a0)
    STORE s7, 9*REGBYTES(a0)
    STORE s8, 10*REGBYTES(a0)
    STORE s9, 11*REGBYTES(a0)
    STORE s10, 12*REGBYTES(a0)
    STORE s11, 13*REGBYTES(a0)

    # restore to's registers
    LOAD ra, 0*REGBYTES(a1)
    LOAD sp, 1*REGBYTES(a1)
    LOAD s0, 2*REGBYTES(a1)
    LOAD s1, 3*REGBYTES(a1)
    LOAD s2, 4*REGBYTES(a1)
    LOAD s3, 5*REGBYTES(a1)
    LOAD s4, 6*REGBYTES(a1)
    LOAD s5, 7*REGBYTES(a1)
    LOAD s6, 8*REGBYTES(a1)
    LOAD s7, 9*REGBYTES(a1)
    LOAD s8, 10*REGBYTES(a1)
    LOAD s9, 11*REGBYTES(a1)
    LOAD s10, 12*REGBYTES(a1)
    LOAD s11, 13*REGBYTES(a1)

    ret
```

`a0`指向原进程，`a1`指向目的进程。在这段代码中，首先使用STORE指令将当前线程中context结构体中的所有寄存器都保存到内存中的地址`0*REGBYTES(a0)`中，接下来使用LOAD指令将即将加载的线程中的context结构体中的所有寄存器从内存地址 `0*REGBYTES(a1)` 加载值到返回地址寄存器中。

**总结：**

保存context的作用是通过保存线程的上下文信息运行状态，在线程切换时能够被方便地恢复现场，继续执行。

#### 2.`struct trapframe *tf`

 `*tf`是中断帧的指针，总是指向内核栈的某个位置。当进程从用户空间跳到内核空间时，中断帧记录了进程在被中断前的状态。当内核需要跳回用户空间时，需要调整中断帧以恢复让进程继续执行的各寄存器值。

`trapframe`结构体的定义如下：

```C
struct trapframe {
    struct pushregs gpr;//包含众多寄存器
    uintptr_t status;//处理器的状态
    uintptr_t epc;//触发中断的指令的地址
    uintptr_t badvaddr;//最近一次导致发生地址错误的虚地址
    uintptr_t cause;//异常或中断的原因
};
```

参数的具体含义在`Lab1`中已经了解过，本次实验需要对`status`与`gpr`再进行进一步说明：

1. `status`：`status`寄存器的全称是`status register`，它包含了工作状态的许多标志位，如下图所示：

![img](https://pic2.zhimg.com/v2-6e7ef91986b408cb38cb82a5ac4acb35_r.jpg)

​	在本次实验中，我们主要用到了`sstatus`寄存器的`SPP`、`SIE`、`SPIE`位，具体解释如下：	

- **`SPP` 位:**
  - `SPP` 记录进入 S-Mode 之前处理器的特权级别。值为 0 表示陷阱源自用户模式（U-Mode），值为 1 表示其他模式。在执行陷阱时，硬件会自动根据当前处理器状态自动设置 `SPP` 为 0 或 1。
  - 在执行 `SRET` 指令从陷阱中返回时，若 `SPP` 为 0，则处理器返回至 U-Mode；若 `SPP` 为 1，则返回至 S-Mode。最终，不论如何，`SPP` 都会被设置为 0。
- **`SIE` 位:**
  - `SIE` 位是 S-Mode 下中断的总开关。若 `SIE` 为 0，则在 S-Mode 下发生的任何中断都不会得到响应。但是，如果当前处理器运行在 U-Mode 下，那么不论 `SIE` 为 0 还是 1，在 S-Mode 下的中断都默认是打开的。换言之，在任何时候，S-Mode 都有权因为自身的中断而抢占运行在 U-Mode 下的处理器。
- **`SPIE` 位:**
  - `SPIE` 位记录进入 S-Mode 之前 S-Mode 中断是否开启。在进入陷阱时，硬件会自动将 `SIE` 位的值保存到 `SPIE` 位上，相当于记录原先 `SIE` 的值，并将 `SIE` 置为 0。这表示硬件不希望在处理一个陷阱的同时被其他中断打扰，从硬件实现逻辑上来说，不支持嵌套中断。
  - 当使用 `SRET` 指令从 S-Mode 返回时，`SPIE` 的值会重新放置到 `SIE` 位上来恢复原先的值，并将 `SPIE` 的值置为 1。

2. `pushregs gpr`

   `pushregs` 的结构体，包含了一系列寄存器如下：		

```C
struct pushregs {
    uintptr_t zero;  // 硬连线的零寄存器
    uintptr_t ra;    // 返回地址寄存器
    uintptr_t sp;    // 栈指针寄存器
    uintptr_t gp;    // 全局指针寄存器
    uintptr_t tp;    // 线程指针寄存器
    uintptr_t t0;    // 临时寄存器
    uintptr_t t1;    // 临时寄存器
    uintptr_t t2;    // 临时寄存器
    uintptr_t s0;    // 保存的寄存器/帧指针
    uintptr_t s1;    // 保存的寄存器
    uintptr_t a0;    // 函数参数/返回值
    uintptr_t a1;    // 函数参数/返回值
    uintptr_t a2;    // 函数参数
    uintptr_t a3;    // 函数参数
    uintptr_t a4;    // 函数参数
    uintptr_t a5;    // 函数参数
    uintptr_t a6;    // 函数参数
    uintptr_t a7;    // 函数参数
    uintptr_t s2;    // 保存的寄存器
    uintptr_t s3;    // 保存的寄存器
    uintptr_t s4;    // 保存的寄存器
    uintptr_t s5;    // 保存的寄存器
    uintptr_t s6;    // 保存的寄存器
    uintptr_t s7;    // 保存的寄存器
    uintptr_t s8;    // 保存的寄存器
    uintptr_t s9;    // 保存的寄存器
    uintptr_t s10;   // 保存的寄存器
    uintptr_t s11;   // 保存的寄存器
    uintptr_t t3;    // 临时寄存器
    uintptr_t t4;    // 临时寄存器
    uintptr_t t5;    // 临时寄存器
    uintptr_t t6;    // 临时寄存器
};
```

本次实验中一共有以下三处关键调用`trapframe`：

**（1）kernel_thread：**

```C
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags) {
    // 对trameframe，也就是我们程序的一些上下文进行一些初始化
    struct trapframe tf;
    memset(&tf, 0, sizeof(struct trapframe));

    // 设置内核线程的参数和函数指针
    tf.gpr.s0 = (uintptr_t)fn; // s0 寄存器保存函数指针，就是init_main函数
    tf.gpr.s1 = (uintptr_t)arg; // s1 寄存器保存函数参数

    // 设置 trapframe 中的 status 寄存器(SSTATUS)
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;

    // 将入口点(epc)设置为 kernel_thread_entry 函数
    tf.epc = (uintptr_t)kernel_thread_entry;

    // 使用 do_fork 创建一个新进程(内核线程)，这样才真正用设置的tf创建新进程。
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}
```

在新创建一个进程后，首先将`s0`设置为函数指针，并使用`s1` 寄存器保存函数参数。由于我们在内核中新建线程，所以将状态寄存器的`SPP`位写设置为1，由于之前中断开启，之前的`SIE`位为1，所以我们使用`SPIE`置一记录状态，之后我们将`SIE`置零，禁用中断。最后将

**（2）`copy_thread`**

```C
proc->tf->gpr.a0 = 0;// 将 a0 设置为 0区分子进程
// 设置子进程的栈指针（sp），若 esp 为 0 则指向新进程的陷阱帧，否则指向传入的 esp
proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;
```

**（3）`forkret`**

在copy_thread函数中设置每一个新线程的内核入口点为`forkret`的地址，并且在switch_to之后，当前进程将在这里执行。

```C
static void
forkret(void) {
    forkrets(current->tf);
}
```

在0号进程和1号进程切换时，栈指针指向的1号进程的中断帧，`ip`指向的是`forkret`，该函数做的就是把栈指针指向新进程的中断帧。`forkret` 相当于是起到一个中介的作用，`eip`是指令指针寄存器，当切换进程的时候，第一个要执行的指令就是`eip`指向的指令，而该指令恰好指向的就是`forkrets(current->tf)`，然后把当前进程的中断帧给设置为新进程的`esp`，也就是栈指针。

设置完当前进程的栈指针之后，跳转到`__trapret`，就开始把中断帧中保存的寄存器值都给加载到当前各寄存器上，相当于布置好了当前进程运行所需要的环境。

**总结：**

`trapframe`变量作用于在构造了新的线程的时候，如果要将控制权转交给这个线程，需要使用中断返回的方式进行（与`lab1`的切换特权级类似），因此需要构造一个伪造的中断返回现场，也就是`trapframe`（保存着用于特权级转换的栈esp寄存器，进程发生特权级转换时中断帧记录进入中断时任务的上下文），使得可以正确的将控制权转交给新的线程。



## 练习2：为新创建的内核线程分配资源（需要编码）

> 创建一个内核线程需要分配和设置好很多资源。`kernel_thread`函数通过调用`do_fork`函数完成具体内核线程的创建工作。`do_kernel`函数会调用`alloc_proc`函数来分配并初始化一个进程控制块，但`alloc_proc`只是找到了一小块内存用以记录进程的必要信息，并没有实际分配这些资源。ucore一般通过`do_fork`实际创建新的内核线程。`do_fork`的作用是，创建当前内核线程的一个副本，它们的执行上下文、代码、数据都一样，但是存储位置不同。因此，我们实际需要`fork`的东西就是`stack`和`trapframe`。在这个过程中，需要给新内核线程分配资源，并且复制原进程的状态。你需要完成在`kern/process/proc.c`中的`do_fork`函数中的处理过程。它的大致执行步骤包括：
>
> - 调用`alloc_proc`，首先获得一块用户信息块。
> - 为进程分配一个内核栈。
> - 复制原进程的内存管理信息到新进程（但内核线程不必做此事）
> - 复制原进程上下文到新进程
> - 将新进程添加到进程列表
> - 唤醒新进程
> - 返回新进程号
>
> 请在实验报告中简要说明你的设计实现过程。请回答如下问题：
>
> - 请说明ucore是否做到给每个新`fork`的线程一个唯一的`id`？请说明你的分析和理由。

### 函数设计实现

根据指导书中给出的执行步骤，实现步骤如下：

1. 调用`alloc_proc`函数，分配并初始化进程控制块；主要工作是通过`kmalloc`函数获得`proc_struct`结构的一块内存块，并把`proc`进行初步初始化。如果没有成功，跳转至`fork_out`处做对应的出错处理。

   ```c
   proc = alloc_proc(); // 分配并初始化进程控制块
   if (proc == NULL)
   {
       goto fork_out;
   }
   ```

2. 调用`setup_kstack`函数，分配并初始化内核栈。主要工作是调用 `alloc_pages` 函数来分配指定大小的页面，然后将分配的页面的虚拟地址赋给进程的 `kstack` 字段，表示该页面是进程的内核栈。如果分配成功，函数返回0表示成功，否则返回错误码 -E_NO_MEM 表示内存不足。如果内存不足，跳转至`bad_fork_cleanup_kstack`处做对应的出错处理。

   ```c
   ret = setup_kstack(proc); // 分配并初始化内核栈
   if (ret == -E_NO_MEM)
   {
       goto bad_fork_cleanup_kstack;
   }
   ```

3. 调用`copy_mm`函数，根据`clone_flags`决定是复制还是共享内存管理系统。由于目前在实验四中只能创建内核线程，所以`copy_mm`中不执行任何操作。

   ```c
   copy_mm(clone_flags, proc);
   ```

4. 调用`copy_thread`函数设置进程的中断帧和上下文。

   ```c
   copy_thread(proc, stack, tf);
   ```

5. 调用`get_pid`函数，为新进程分配PID。

   ```c
   const int pid = get_pid();
   proc->pid = pid;
   ```

6. 把设置好的进程加入进程链表，计算PID哈希值并加入到对应的哈希表。

   ```c
   list_add(hash_list + pid_hashfn(pid), &(proc->hash_link));
   list_add(&proc_list, &(proc->list_link));
   ```

7. 调用`wakeup_pro`函数，将新建的进程设为就绪态。

   ```c
   wakeup_proc(proc);
   ```

8. 总进程数加1。

   ```c
   nr_process++;
   ```

9. 将返回值设为线程id。

   ```c
   ret = pid;
   ```

### 问题回答

> 请说明ucore是否做到给每个新`fork`的线程一个唯一的`id`？请说明你的分析和理由。

ucore调用`get_pid`函数，为每个新线程分配PID，而分析`get_pid`的实现可知，它会返回一个唯一的未被使用的PID。

`get_pid`函数的基本思想是遍历进程列表，在遍历时维护一个区间`[last_pid,next_safe)`，一直保证此区间内始终为未使用的PID。具体方法如下：

- 首次调用时初始化**静态变量**`last_pid`与`next_safe`为最大PID`MAX_PID`，之后的调用会保留上一次调用结束时的值，之后每次调用时`last_pid`的意义是上次分配的PID。
- 如果`++last_pid`小于`next_safe`，直接分配`last_pid`；
- 如果`last_pid`大于等于`MAX_PID`，超出范围了，将`last_pid`重置为1；
- 如果`last_pid`大于等于`MAX_PID`或者`last_pid`大于等于`MAX_PID`，将`next_safe`置为`MAX_PID`，扩张区间范围，在后面的遍历中限缩。接下来就遍历进程链表，获取每个进程的已分配的PID：
  - 如果发现有进程的PID等于`last_pid`，则表明冲突，则增加`last_pid`，就是将区间右移一个。这确保了没有一个进程的`pid`与`last_pid`重合；
  - 如果发现一个进程的PID大于`last_pid`且小于`next_safe`，则将这个进程的PID赋值给`next_safe`，即缩小`next_safe`的范围。这能够保证遍历到目前来说`[last_pid,next_safe)`之间没有已用的PID；
  - 如果在遍历中，`last_pid>=next_safe`，需要将`next_safe`扩张到`MAX_PID`，形成新区间并继续在后面的遍历中限缩。
    - 如果在遍历中，`last_pid`还超出了`MAX_PID`，则还需要将`last_pid`重置为1，继续在后面的遍历中限缩区间。

通过以上的处理，能够保证最终`[last_pid,next_safe)`区间范围内为可用PID。返回`last_pid`即为为新进程分配的唯一PID。



## 练习3：编写proc_run 函数（需要编码）

> proc_run用于将指定的进程切换到CPU上运行。它的大致执行步骤包括：
>
> - 检查要切换的进程是否与当前正在运行的进程相同，如果相同则不需要切换。
> - 禁用中断。你可以使用`/kern/sync/sync.h`中定义好的宏`local_intr_save(x)`和`local_intr_restore(x)`来实现关、开中断。
> - 切换当前进程为要运行的进程。
> - 切换页表，以便使用新进程的地址空间。`/libs/riscv.h`中提供了`lcr3(unsigned int cr3)`函数，可实现修改CR3寄存器值的功能。
> - 实现上下文切换。`/kern/process`中已经预先编写好了`switch.S`，其中定义了`switch_to()`函数。可实现两个进程的context切换。
> - 允许中断。
>
> 请回答如下问题：
>
> - 在本实验的执行过程中，创建且运行了几个内核线程？

### 函数设计实现

1. 检查要切换的进程是否与当前正在运行的进程相同，如果相同则不需要切换。

   ```c#
   if (proc != current)
   ```

2. 为了在进行进程切换时避免中断干扰，禁用中断。先声明了一个布尔型变量 `intr_flag` 用于保存中断状态。再声明两个指向进程结构体的指针 `prev` 和 `next`，分别指向当前运行的进程和将要运行的进程。调用宏 `local_intr_save`，将当前中断状态保存到 `intr_flag` 中，并关闭中断。

   ```c#
           bool intr_flag;
           struct proc_struct *prev = current, *next = proc;
           local_intr_save(intr_flag);
   ```

3. 换当前进程为要运行的进程。将当前运行的进程指针 `current` 指向参数传入的 `proc`，表示切换到新的进程。

   ```c#
               current = proc;
   ```

4. 切换页表，以便使用新进程的地址空间。使用宏lcr3将 CR3 寄存器的值设置为新进程的页目录表地址 `next->cr3`，以切换到新进程的地址空间。

   ```c#
               lcr3(next->cr3);
   ```

5. 实现上下文切换。调用 `switch_to` 函数进行上下文切换，将从 `prev` 进程切换到 `next` 进程。

   ```c#
               switch_to(&(prev->context), &(next->context));
   ```

6. 调度结束后允许中断。使用宏`local_intr_restore`恢复之前保存的中断状态，即重新开启中断，允许中断处理器响应中断。

   ```c#
           local_intr_restore(intr_flag);
   ```

### 问题回答

> 在本实验的执行过程中，创建且运行了几个内核线程？

本实验中，创建并运行了2个内核线程`idleproc`及`intiproc`

1. `idleproc`

   `idleproc`表示空闲进程，其主要目的是在系统没有其他任务需要执行时，占用 CPU 时间，同时便于进程调度的统一化。

   在`proc_init`函数中，调用`alloc_proc`函数获得`proc_struct`结构的一块内存块，作为第0个进程控制块；并把`idleproc`进行初步初始化。然后对`idleproc`进行进一步的初始化，设置了`idleproc`的`pid`、`state`、`kstack`、`need_resched`等成员变量，将其`name`设置为`idle`。由此完成了内核线程`idleproc`的创建和初始化。相关代码如下：

   ```c#
       if ((idleproc = alloc_proc()) == NULL)
       {
           panic("cannot alloc idleproc.\n");
       }
       ...
           idleproc->pid = 0;                             // 将 idleproc 的进程 ID 设置为 0
       idleproc->state = PROC_RUNNABLE;               // 将 idleproc 的状态设置为可运行
       idleproc->kstack = (uintptr_t)bootstack;       // 将 idleproc 的内核栈设置为引导栈的地址
       idleproc->need_resched = 1;                    // 设置 idleproc 需要重新调度
       set_proc_name(idleproc, "idle");               // 为 idleproc 设置进程名为 "idle"
   ```

   在`uCore`执行完`proc_init`函数，`uCore`当前的执行现场就是`idleproc`，等到执行到`init`函数的最后一个函数`cpu_idle`之前，`uCore`的所有初始化工作就结束了，`idleproc`将通过执行`cpu_idle`函数让出CPU，给其它内核线程执行。

   ```c#
   // cpu_idle - 在 kern_init 结束时，第一个内核线程 idleproc 将执行以下操作
   void cpu_idle(void)
   {
       // 进入无限循环，表示该线程将一直运行
       while (1)
       {
           // 检查当前线程是否需要重新调度
           if (current->need_resched)
           {
               // 如果需要重新调度，调用 schedule 函数选择下一个要运行的进程
               schedule();
           }
       }
   }
   ```

2. `initproc`

   `idleproc`内核线程主要工作是完成内核中各个子系统的初始化，然后就通过执行`cpu_idle`函数让出CPU，给其它内核线程执行了。所以`uCore`需要创建`initproc`内核线程来完成各种工作。

   在`proc_init`函数中，会调用函数`kernel_thread`创建内核线程，并判断是否创建成功。创建成功后，根据其进程ID将其设置为 `initproc`，并为 `initproc` 设置进程名为 `init`。由此完成`initproc`内核线程的创建。相关代码如下：

   ```c#
   	int pid = kernel_thread(init_main, "Hello world!!", 0);  // 创建内核线程返回其进程 ID
       if (pid <= 0)
       {
           panic("create init_main failed.\n");     // 如果创建 init_main 失败，触发 panic
       }
   
       initproc = find_proc(pid);                    // 查找进程 ID 为 pid 的进程，将其设置为 initproc
       set_proc_name(initproc, "init");              // 为 initproc 设置进程名为 "init"
   ```

   ​	

   `idleproc`线程完成各个子系统的初始化后执行`cpu_idle`函数让出CPU，会马上调用`schedule`函数找其他处于就绪态的进程执行。

   在`schedule`函数中，会先设置当前内核线程`current->need_resched`为0，然后在`proc_list`队列中查找下一个处于“就绪”态的线程或进程next。找到这样的进程后，就调用`proc_run`函数，保存当前进程current的执行现场（进程上下文），恢复新进程的执行现场，完成进程切换。由于在`proc_list`中只有两个内核线程，且`idleproc`要让出CPU给`initproc`执行，因此schedule函数通过查找`proc_list`进程队列，只能找到一个处于“就绪”态的`initproc`内核线程。并通过`proc_run`和进一步的`switch_to`函数完成两个执行现场的切换。

   `switch_to`返回时，CPU开始执行`init_proc`的执行流，跳转至之前构造好的`forkret`处。`forkret`中，进行中断返回。将之前存放在内核栈中的中断栈帧中的数据依次弹出，最后跳转至`kernel_thread_entry`处。`kernel_thread_entry`中，利用之前在中断栈中设置好的`s0`和`s1`执行真正的`init_proc`业务逻辑的处理(`init_main`函数)，在`init_main`返回后，跳转至`do_exit`终止退出。


## 扩展练习 Challenge：

说明语句`local_intr_save(intr_flag);....local_intr_restore(intr_flag);`是如何实现开关中断的？

`schedule`函数中，会先关闭中断，避免调度的过程中被中断再度打断而出现并发问题。

1. **寄存器`sstatus`**

在中断控制时用到了寄存器`sstatus`的 `sie` 位，该二进制位在内核态下是控制是否启用中断的使能信号，其初始值为0，需要设置为1时才能启用中断。

**2.参数**

接下来我们对两个函数中传入的参数进行解释：

```C
bool intr_flag;//proc_run中定义的是否关闭中断的布尔值
```

了解完毕后，我们进入正式的函数分析。

**3.`local_intr_save(intr_flag)`**

这是一个宏定义，我们查看他的定义如下：

```C
// 宏，用于保存当前中断状态，并在需要时恢复状态
#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); // 调用 __intr_save 函数来保存中断状态，并将结果赋值给 x
    } while (0)
```

当我们传入一个需要保存的中断标志x时，他会首先执行一次__intr_save()操作，并将返回的值赋值给x。

宏定义中的`do{}while(0)`循环：它没有实际的循环，只是为了组织代码和确保 `x` 变量在 `__intr_save()` 调用后被赋值。这可以确保宏内的语句被视为一个单独的块，而不会受到外部代码块的干扰，确保宏的使用方式和语法在展开时是符合预期的，并且可以安全地嵌入到代码中，避免潜在的问题。

 `__intr_save()`函数的定义如下：

```C
static inline bool __intr_save(void) {  // 内联函数：保存中断状态
    if (read_csr(sstatus) & SSTATUS_SIE) {  
        // 如果当前状态寄存器 sstatus 中断位（SIE）为 1，表示中断允许
        intr_disable();  // 禁用中断
        return 1;  // 返回 1 表示中断被禁用
    }
    return 0;  // 返回 0 表示中断未被禁用
}
```

在这个函数中，首先通过`read_csr(sstatus)`函数来读取`sstatus` 寄存器，并且检查当前 CPU 核的中断状态。如果能够正常读取`sstatus` 寄存器并且`SIE`位置一，那么说明当前允许中断，那么就会调用`intr_disable()`函数禁用中断，如果不能够正常读取或者是SIE位置零表示已经禁用中断， `__intr_save()`函数返回0。

`intr_disable()`函数定义如下：

```C
void intr_disable(void) { clear_csr(sstatus, SSTATUS_SIE); }
```

我们在`riscv.h`中查看关于寄存器操作的函数如下：

```c
// clear_csr(reg, bit)宏，清除寄存器（reg）中的特定位（bit）
#define clear_csr(reg, bit) ({ unsigned long __tmp; \  
  asm volatile ("csrrc %0, " #reg ", %1" : "=r"(__tmp) : "rK"(bit)); \ 
  // 内联汇编语句，使用 RISC-V 指令 csrrc
  __tmp; })  // 返回清除特定位后的寄存器值
      
// read_csr(reg)宏，用于读取指定寄存器（reg）的值   
#define read_csr(reg) ({ unsigned long __tmp; \  
// 内联汇编语句，使用 RISC-V 指令 csrr
  asm volatile ("csrr %0, " #reg : "=r"(__tmp)); \ 
  __tmp; })  // 返回寄存器的值
```

根据查询相关资料可知，`csrrc` 是 RISC-V 架构指令之一。它用于对特定的控制寄存器进行读取、修改、写入的原子操作，因此`clear_csr(sstatus, SSTATUS_SIE)`可以原子地设置`SSTATUS_SIE`位，以防代码被打断，从而引起一些未预料到的错误。

因此，`local_intr_save(intr_flag)`的作用是如果SIE==1，就原子地将SIE清零禁止中断。

**4 local_intr_restore(intr_flag)**

同理，我们查看 `local_intr_restore(intr_flag)`的函数定义如下：

```C
#define local_intr_restore(x) __intr_restore(x);  
// 定义宏：局部恢复中断状态，根据 x 变量中的中断状态调用 __intr_restore 函数
```

`__intr_restore(x)`的函数定义如下：

```C
// 恢复中断状态
static inline void __intr_restore(bool flag) {
    if (flag) {
        // 如果传入的标志为真，表示之前中断状态被保存，启用中断
        intr_enable();
    }
}
```

`intr_enable()`的函数定义如下：

```
void intr_enable(void) { set_csr(sstatus, SSTATUS_SIE); }
```

`set_csr()`函数定义如下：

```C
#define set_csr(reg, bit) ({ unsigned long __tmp; \ 
// 定义一个宏，用于设置指定寄存器（reg）的特定位（bit）
  asm volatile ("csrrs %0, " #reg ", %1" : "=r"(__tmp) : "rK"(bit)); \ 
// 内联汇编语句，使用 RISC-V 指令 csrrs
  __tmp; })  // 返回寄存器的值
```

因此，`local_intr_restore(intr_flag)`的作用是，如果`intr_flag==1`就原子地将SIE置一恢复中断。

**5.总结**

假设`SIE==1`时，首先`local_intr_save()`函数会调用`__intr_save()`检查当前的中断状态，因为SIE为1，接下来就会调用`intr_disable()`禁用中断，使用`clear_csr()`将`sstatus`的`SIE`位清零。此时函数返回1并赋值给`intr_flag`。

当执行函数体结束完毕后，调用`local_intr_restore(intr_flag)`函数，此时调用 `__intr_restore(bool flag)`会检测到`intr_flag==1`，那么说明之前是允许中断状态被保存需要启用中断，调用`intr_enable()`函数并使用`set_csr()`函数将`sstatus`的`SIE`位置一，中断状态恢复完毕。

反之，如果`SIE`为0，那么`intr_save(void)`函数也会返回0，`intr_flag`被赋值为0，函数体执行完毕后，在调用`intr_restore(bool flag)`函数时不会执行任何操作，说明之前并不是允许中断的状态，自然也不需要恢复。

## 相关知识点总结

### 1.进程与线程

1. 进程：源代码经过编译器编译就变成可执行文件，这一类文件叫做程序。而当一个程序被用户或操作系统启动，分配资源，装载进内存开始执行后，它就成为了一个进程。

2. 线程：线程是进程的一部分，描述指令流执行状态。它是进程中的指令执行流的最小单元，是CPU调度的基本单位。

3. 进程与线程的比较:

   - 进程是资源分配单位，线程是CPU调度单位
   - 进程拥有一个完整的资源平台，而线程只独享指今流执行的必要资源，如寄存器和栈
   - 线程具有就绪、等待和运行三种基本状态和状态间的转换关系
   - 线程能减少并发执行的时间和空间开销
     - 线程的创建时间比进程短
     - 线程的终止时间比进程短
     - 同一进程内的线程切换时间比进程短
     - 由于同一进程的各线程间共享内存和文件资源可不通过内核进行直接通信

4. 为什么需要进程？

   进程的一个重要特点在于其可以调度。在我们操作系统启动的时候，操作系统相当是一个初始的进程。之后，操作系统会创建不同的进程负责不同的任务。用户可以通过命令行启动进程，从而使用计算机。想想如果没有进程会怎么样?所有的代码可能需要在操作系统编译的时候就打包在一块，安装软件将变成一件非常难的事情，这显然对于用户使用计算机是不利的。

   另一方面，从2000年开始，CPU越来越多的使用多核心的设计。这主要是因为芯片设计师们发现在一个核心上提高主频变得越来越难(这其中有许多原因，相信组成原理课上已经有过个绍)，所以采用多人核心，将利用多核性能的任务交给了程序员。在这种环境下，操作系统也需要进行相应的调整，以适应这种多核的趋势。使用进程的概念有助于各个进程同时的利用CPU的各人核心，这是单进程系统往往做不到的。

   但是，多进程的引入其实远早于多核心处理器。在计算机的远古时代，存在许多“巨无霸"计算机。但是，如果只让这些计算机服务于一个用户，有时候又有点浪费。有没有可能让一人计算机服务于多个用户呢(哪怕只有一个核心)?分时操作系统解决了这人问题，就是通过时间片轮转的方法使得多个用户可以“同时”使用计算资源。这个时候，引入进程的概念，成为操作系统调度的单元就显得十分必要了。

   综合以上可以看出，操作系统的确离不开进程管理。

### 2.进程的创建和管理

1. 进程的虚拟内存空间：每个进程有一个完全属于自己的地址空间（实验平台的处理器中有39位地址空间，512G）。这个空间由一个页表（多级页表）描述。每个进程有一个独立的页表，页表中描述了该进程的虚拟地址与该机器的物理地址的对应关系。进程的整个运行过程中，都在使用虚拟地址，应用程序无法触及物理地址，也就无法影响其他进程。
2. 进程的创建
   - 分配一级页表（页目录表）
   - 将一级页表的物理地址放入MMU的相应寄存器中（X86是CR3, RISCV是SATP）
   - 应用程序加载，在虚拟地址中载入分段信息和部分数据、指令
     - 依据编译链接的结果，这些信息放在程序二进制头中
     - 建立虚拟地址与文件内的物理偏移量的对应关系
   - 进程创建完成，将PC转去main以执行程序
   - 取指令数据中引发缺页，OS加载新的数据
     - 如果该虚拟地址的页表不存在，则需创建页表
3. 重要的系统调用：
   - Fork负责创建一个新的进程
     - Fork由父进程调用，创建一个新的进程为子进程
     - 新的进程与原进程共享所有的资源
     - 页表复用，写时复制
     - 新的进程为就绪态等待调度
   - Exec负责让进程执行一个特定的程序
     - Exec由子进程调用，改变其执行的内容
     - 依据二进制文件格式重新建立页表映射

### 3.进程的调度

- 占据CPU的进程
  - 进程的页目录表载入
  - MMU重新配置，TLB失效
  - Cache
- 一个完整的页面失效过程
  - 访问某个虚拟地址，发现cache未命中
  - 试图从内存中直接读取，发现该页面的页表不存在
  - 从OS中申请一个新的空白页框，并建立映射
  - 依照地址映射信息，从辅存中调取内容
- 未占据CPU的进程
  - 释放干净的内存，修改该进程的页表
  - 释放已修改过的内存，在释放前需将其中的内容写入可靠的区域
  - 在多级页表中，如果整个二级页表都已失效，也可以释放掉(通常不会发生)
  - 意外被杀死