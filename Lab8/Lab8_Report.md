# OS Lab 8

Contributors：焦心雨(2112536)、李艺楠(2110246)、辛浩然(2112514)

- 包括练习+Challenge

[GitHub链接](https://github.com/Herren-Hsing/Operation-System)

## 练习0：填写已有实验

填写之前Lab中完成的代码，其中需要修改的部分包括：

### alloc_proc

由于在Lab6~8增加了进程控制块的成员变量：

```c
struct run_queue *rq;                   // 运行队列包含进程
list_entry_t run_link;                  // 在运行队列中链接的条目
int time_slice;                         // 占用CPU的时间片
skew_heap_entry_t lab6_run_pool;        // 仅用于实验6：运行池中的条目
uint32_t lab6_stride;                   // 仅用于实验6：进程的当前步幅
uint32_t lab6_priority;                 // 仅用于实验6：进程的优先级，由lab6_set_priority(uint32_t)设置
// Lab 8 新增
struct files_struct *filesp;            // 进程的与文件相关的信息（pwd、files_count、files_array、fs_semaphore）
```

为了防止出现问题，在`alloc_proc`对所有成员变量都进行了初始化，新增部分的初始化如下：

```c
proc->rq = NULL;
list_init(&(proc->run_link));
proc->time_slice = 0;
proc->lab6_run_pool.left = proc->lab6_run_pool.right = proc->lab6_run_pool.parent = NULL;
proc->lab6_stride = 0;
proc->lab6_priority = 0;
proc->filesp = NULL       // filesp 初始为 NULL
```

### proc_run

根据代码注释，在`proc_run`函数中，进行`switch_to`前刷新TLB。

```c
//LAB8 YOUR CODE : (update LAB4 steps)
/*
 * below fields(add in LAB6) in proc_struct need to be initialized
 *       before switch_to();you should flush the tlb
 *        MACROs or Functions:
 *       flush_tlb():          flush the tlb        
 */
bool intr_flag;
struct proc_struct *prev = current, *next = proc;
local_intr_save(intr_flag);
{
    current = proc;
    lcr3(next->cr3);
    flush_tlb();          // flush the tlb
    switch_to(&(prev->context), &(next->context));
}
local_intr_restore(intr_flag);
```

### do_fork

```c
// 调用 copy_files 函数从当前进程复制 files_struct 到新进程
if (copy_files(clone_flags, proc) != 0) { //for LAB8
    goto bad_fork_cleanup_kstack;
}
// ...
bad_fork_cleanup_fs:  //for LAB8
	put_files(proc);
```

在`do_fork`函数中，创建新进程时，调用 `copy_files` 函数从当前进程复制 `files_struct `到新进程。如果复制失败，则调用`put_files`进行错误处理、销毁资源。

### Round Robin 调度

由于没有进行Lab6，因此将进程调度算法更换为默认的Round Robin 调度算法。

## 练习1: 完成读文件操作的实现（需要编码）

> 首先了解打开文件的处理流程，然后参考本实验后续的文件读写操作的过程分析，填写在` kern/fs/sfs/sfs_inode.c`中 的`sfs_io_nolock()`函数，实现读文件中数据的代码。

### 打开文件的处理流程

用户调用接口函数`open`，进一步调用`open->sys_open->syscall`，引起系统调用进入到内核态。

到了内核态以后，通过中断处理例程，会调用到`sys_open`内核函数，并进一步调用`sysfile_open`内核函数。整体调用流程如下：

```c
sys_open(arg[])  -->    sysfile_open("/testfile",...)   -->   file_open("/testfile",....)
	                                                  fd_array_alloc()        <-|
    vfs_lookup("/testfile",&node)        <-|								 |
             |      vop_open(node,...)	 <-     vsf_open("/testfile",...)     <-|											    |-> vop_lookup() -> sfs_lookup(*node,*path,**node_store)  ->  sfs_lookup_once()				
																|-> sfs_direct_search_nolock()
																|-> sfs_load_inode()         
	                             	ide_read_secs <-  d_io  <-   sfs_rbuf   <-|
```

在`sysfile_open`内核函数中，需要把位于用户空间的字符串`__path`拷贝到内核空间中的字符串`path`中，然后调用了`file_open`。在`file_open`函数中，程序主要做了以下几个操作：

- 在当前进程的文件管理结构`filesp`中，获取一个空闲的`file`对象。
- 调用`vfs_open`函数，并存储该函数返回的`inode`结构。
- 根据上一步返回的`inode`，设置`file`对象的属性。如果打开方式是`append`，则还会设置`file`的`pos`成员为当前文件的大小。
- 最后返回`file->fd`。

调用的`vfs_open`函数需要完成两件事情：通过`vfs_lookup`找到`path`对应文件的`inode`；调用`vop_open`函数打开文件。主要完成以下操作：

- 调用`vfs_lookup`搜索给出的路径，判断是否存在该文件。如果存在，则`vfs_lookup`函数返回该文件所对应的`inode`节点。而如果给出的路径不存在，即文件不存在，则根据传入的flag，选择调用`vop_create`创建新文件或直接返回错误信息。
- 执行到此步时，当前函数中的局部变量`node`一定非空，此时进一步调用`vop_open`函数打开文件。
- 如果文件打开正常，则根据当前函数传入的`open_flags`参数来判断是否需要将当前文件截断至0（即清空）。如果需要截断，则执行`vop_truncate`函数。最后函数返回。

调用`vfs_lookup`的目的是进行路径查找。`vfs_lookup`函数是一个针对目录的操作函数，它会调用`vop_lookup`函数来找到SFS文件系统中的目录下的文件。为此，`vfs_lookup`函数首先调用`get_device`函数，并进一步调用`vfs_get_bootfs`函数来找到根目录“/”对应的`inode`。

通过调用`vop_lookup`函数来查找到根目录“/”下对应文件filetest的索引节点，如果找到就返回此索引节点。

这个流程中，以vop开头的函数通过一些宏和函数的转发，最后变成对inode结构体里的inode_ops结构体的“成员函数”的调用。对于SFS文件系统的inode来说，会变成对sfs文件系统的具体操作。

`sfs_lookup`有三个参数：`node`，`path`，`node_store`。其中`node`是根目录“/”所对应的inode节点；`path`是文件`sfs_filetest1`的绝对路径/filetest，而`node_store`是经过查找获得的filetest所对应的inode节点。

`sfs_lookup`函数以“/”为分割符，从左至右逐一分解`path`获得各个子目录和最终文件对应的`inode`节点。在本例中是调用`sfs_lookup_once`查找以根目录下的文件filetest所对应的`inode`节点。当无法分解`path`后，就意味着找到了filetest对应的`inode`节点，就可顺利返回了。

`sfs_lookup_once`将调用`sfs_dirent_search_nolock`函数来查找与路径名匹配的目录项，如果找到目录项，则调用`sfs_load_inode`函数根据目录项中记录的`inode`所处的数据块索引值找到路径名对应的SFS磁盘`inode`，并读入SFS磁盘`inode`对应的内容，创建SFS内存`inode`。

### 读文件流程分析

依次调用如下用户态函数：`read->sys_read->syscall`，从而引起系统调用进入到内核态。

到了内核态以后，通过中断处理例程，会调用到`sys_read`内核函数，并进一步调用`sysfile_read`内核函数，进入到文件系统抽象层处理流程完成进一步读文件的操作。接下来的工作包括：

- 测试当前待读取的文件是否存在**读权限**
- 在内核中创建一块缓冲区。
- 循环执行`file_read`函数读取数据至缓冲区中，并将该缓冲区中的数据复制至用户内存（即传入`sysfile_read`的base指针所指向的内存）

`file_read`函数是内核提供的一项文件读取函数。在`file_read`函数中，通过文件描述符查找到了相应文件对应的内存中的`inode`信息，然后转交给`vop_read`进行读取处理，事实上就是转交到了`sfs_read`函数进行处理（通过函数指针），然后调用了`sfs_io`函数，再进一步调用了`sfs_io_nolock`函数，这就是本次练习中需要完善的函数.

### `sfs_io_nolock()`函数编码

`sfs_io_nolock`函数从偏移位置到偏移位置+长度进行文件内容的读写。具体来说：

1. 先计算一些辅助变量，并处理一些特殊情况（比如越界），然后有`sfs_buf_op = sfs_rbuf`，`sfs_block_op = sfs_rblock`，设置读取的函数操作。
2. 接着进行实际操作，先处理起始的没有对齐到块的部分，再以块为单位循环处理中间的部分，最后处理末尾剩余的部分。
3. 每部分中都调用`sfs_bmap_load_nolock`函数得到`blkno`对应的`inode`编号，并调用`sfs_rbuf`或`sfs_rblock`函数读取数据（中间部分调用`sfs_rblock`，起始和末尾部分调用`sfs_rbuf`），调整相关变量。
4. 完成后如果`offset + alen > din->fileinfo.size`（写文件时会出现这种情况，读文件时不会出现这种情况，`alen`为实际读写的长度），则调整文件大小为`offset + alen`并设置`dirty`变量。

基本变量：

- `alen`：实际读取/写入的大小；
- `blkoff` ：读/写开始块的编号；
- `nblks` ：读/写块的数量；
- `offset`：偏移位置。

填写的部分基本思路如下：

- 如果 `offset` 与第一个块不对齐，需要先处理这部分内容。

- 处理未对齐的块：

  - 如果 `nblks` 为零，表示不足一块，将 `size` 设置为 `endpos - offset`。

  - 如果 `nblks` 不为零，将 `size` 设置为`SFS_BLKSIZE - blkoff`。

  - 调用 `sfs_bmap_load_nolock` 获取块的索引值，并通过 `sfs_buf_op` 进行读写操作。

  - 更新 `alen` 和 `buf`，开始块的编号`blkoff++`，块的数量 `nblks--`。

- 处理对齐的块：

  - 处理那些对齐的块，调用 `sfs_bmap_load_nolock` 获取块的索引值，并通过 `sfs_block_op` 进行读写操作。

  - 更新 `alen`、`buf`、`blkno` 和 `nblks`。

- 处理结束位置未对齐的块：

  - 如果 `endpos` 与最后一个块不对齐，计算出最后一部分的大小 `size`。

  - 调用 `sfs_bmap_load_nolock` 获取块的索引值，并通过 `sfs_buf_op` 进行读写操作。

  - 更新 `alen`。

```c
// 调用sfs_bmap_load_nolock、sfs_rbuf、sfs_rblock等函数，读取文件中的不同类型的块
/*
	(1) 如果偏移量与第一个块不对齐，则从偏移量到第一个块的末尾读/写一些内容
	读/写大小 = (nblks != 0) ? (SFS_BLKSIZE - blkoff) : (endpos - offset)
	(2) 读/写对齐的块
	(3) 如果结束位置与最后一个块不对齐，则从开头到最后一个块的(endpos % SFS_BLKSIZE)处读/写一些内容
*/
// 如果 offset 与第一个块不对齐，从offset到第一个块的末尾Rd/Wr一些内容
// SFS_BLKSIZE 是块大小
// blkoff 是Rd/Wr开始块的编号；nblks 是Rd/Wr块的数量
blkoff = offset % SFS_BLKSIZE;   // 计算出在第一块数据块中进行读或写操作的偏移量
if (blkoff != 0) {
    if(nblks == 0){ // 不满一块
        size = endpos - offset;  // 计算出在第一块数据块中进行读或写操作需要的数据长度
    }
    else{
        size = SFS_BLKSIZE - blkoff;
    }
    // blkno是开始块编号
    // 获取当前这个数据块对应到的磁盘上的数据块的编号
    if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
        goto out;
    }

    // 进行读/写，利用这个sfs_buf_op分别调用sfs_wbuf或sfs_rbuf，在前面设置的
    // 将数据写入到磁盘中
    if ((ret = sfs_buf_op(sfs, buf, size, ino, blkoff)) != 0) {
        goto out;
    }

    // 维护已经读写成功的数据长度信息
    alen += size;
    buf += size; // 维护缓冲区的偏移量
    
    // 如果没有块了
    if (nblks == 0) {
        goto out;
    }
    
    blkno++;
    nblks--;
}

// 接下来，处理那些对齐的块，还是一样的调用
if (nblks > 0) {
    if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
        goto out;
    }
    // 将这些磁盘上的这些数据块进行读或写操作
    if ((ret = sfs_block_op(sfs, buf, ino, nblks)) != 0) {
        goto out;
    }

    alen += nblks * SFS_BLKSIZE;
    buf += nblks * SFS_BLKSIZE;
    blkno += nblks;
    nblks -= nblks;
}

// 如果结束位置与最后一个块不对齐，从开始到最后一个块的(endpos % SFS_BLKSIZE)Rd/Wr一些内容
if (endpos % SFS_BLKSIZE != 0) {
    size = endpos % SFS_BLKSIZE; // 确定在这数据块中需要读写的长度
    if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
        goto out;
    }
    if ((ret = sfs_buf_op(sfs, buf, size, ino, 0)) != 0) {
        goto out;
    }
    alen += size;
}
```

## 练习2: 完成基于文件系统的执行程序机制的实现（需要编码）

> 改写`proc.c`中的`load_icode`函数和其他相关函数，实现基于文件系统的执行程序机制。执行：`make qemu`。如果能看看到sh用户程序的执行界面，则基本成功了。如果在sh用户界面上可以执行”ls”,”hello”等其他放置在sfs文件系统中的其他执行程序，则可以认为本实验基本成功。

`load_icode`函数用于从磁盘上读取可执行文件，并且加载到内存中，完成内存空间的初始化。

之前的实验仅仅将原先就加载到了内核内存空间中的ELF可执行文件加载到用户内存空间中，而没有涉及从磁盘读取数据的操作，而且之前的时候并没有对`execve`所执行的程序传入参数。

基本思路和Lab5近似，为：

- 为当前进程创建一个新的内存管理器

* 创建一个新的页目录表（PDT），并将`mm->pgdir`设置为PDT的内核虚拟地址
* 将二进制文件的TEXT/DATA/BSS部分复制到进程的内存空间中
     *    读取文件中的原始数据内容并解析`elfhdr`
     *    根据`elfhdr`中的信息，读取文件中的原始数据内容并解析`proghdr`
     *    调用`mm_map`构建与TEXT/DATA相关的vma
     *    调用`pgdir_alloc_page`为TEXT/DATA分配页，读取文件中的内容并将其复制到新分配的页中
     *    调用`pgdir_alloc_page`为BSS分配页，在这些页中使用`memset`将其置零
* 调用`mm_map`设置用户堆栈，并将参数放入用户堆栈中
* 设置当前进程的内存管理器、cr3，重置`pgidr`（使用`lcr3`宏）
* 在用户堆栈中设置`uargc`和`uargv`
* 为用户环境设置`trapframe`
* 如果上述步骤失败，应清理环境。

完整代码见代码文件。这里只列出Lab8修改的部分。

首先，当前进程创建一个新的 `mm` 结构；创建一个新的页目录表（PDT），并将 `mm->pgdir` 设置为页目录表的内核虚拟地址。

接下来，将二进制文件的TEXT/DATA/BSS部分复制到进程的内存空间中。首先读取文件中的原始数据内容并解析`elfhdr`，这里与之前实验不同，需要从文件中读取`elf header`。需要调用`load_icode_read`函数，在文件中读取ELF header。这个函数是用来从文件描述符fd指向的文件中读取数据到缓冲区中的。传入的参数包括`fd`（文件描述符）、`elf`（指向读取数据的缓冲区）、`sizeof(struct elfhdr)`（要读取的数据长度）和0（文件中的偏移量）。

```c
// 将二进制文件的TEXT/DATA/BSS部分复制到进程的内存空间中
// 读取文件中的原始数据内容并解析elfhdr
// LAB8 这里要从文件中读取ELF header
struct elfhdr __elf, *elf = &__elf;
if ((ret = load_icode_read(fd, elf, sizeof(struct elfhdr), 0)) != 0)
{
	goto bad_elf_cleanup_pgdir;
}
```

读取后，检查是否是一个合法的ELF文件。

之后，根据`elfhdr`中的信息，读取文件中的原始数据内容并解析`proghdr`。需要根据每一段的大小和基地址来分配不同的内存空间。这里再次调用`load_icode_read`函数从文件特定偏移处读取每个段的详细信息（包括大小、基地址等等）。

```c
// 根据elfhdr中的信息，读取文件中的原始数据内容并解析proghdr
// 根据每一段的大小和基地址来分配不同的内存空间
struct proghdr __ph, *ph = &__ph;
uint32_t vm_flags, perm, phnum;
for (phnum = 0; phnum < elf->e_phnum; phnum++)
{
    // LAB8 从文件特定偏移处读取每个段的详细信息（包括大小、基地址等等）
    off_t phoff = elf->e_phoff + sizeof(struct proghdr) * phnum;
    if ((ret = load_icode_read(fd, ph, sizeof(struct proghdr), phoff)) != 0)
    {
        goto bad_cleanup_mmap;
    }
    // 如果不是需要加载的段，直接跳过
    // 如果文件头标明的文件段大小大于所占用的内存大小(memsz可能包括了BSS，所以这是错误的程序段头)
```

调用` mm_map` 函数设置新的 VMA，设置相关权限，建立`ph->p_va`到`ph->va+ph->p_memsz`的合法虚拟地址空间段。

接下来，分配内存，并将每个程序段的内容（`from, from+end`）复制到进程的内存（`la, la+end`）。在复制 TEXT/DATA 段的内容的过程中，首先分配一个内存页，建立la对应页的虚实映射关系。然后调用`load_icode_read`函数读取elf对应段内的数据并写入至该内存中。之后，构建二进制程序的 BSS 段。

```c
    // LAB8 读取elf对应段内的数据并写入至该内存中
    if ((ret = load_icode_read(fd, page2kva(page) + off, size, offset)) != 0)
    {
        goto bad_cleanup_mmap;
    }
	// ...
}
```

上述步骤结束后，需要关闭读取的ELF文件。

```c
sysfile_close(fd);
```

之后，设置当前进程的` mm`、`sr3`，并设置 CR3 寄存器为页目录表的物理地址。

接下来，构建用户栈内存，为用户栈设置对应的合法虚拟内存空间，并将命令行参数和用户栈的布局写入用户空间。LAB8 需要处理用户栈中传入的参数， 在用户堆栈中设置`uargc`和`uargv`。

```c
// 为用户程序创建一个新的用户栈，并将用户程序的参数拷贝到新栈中。最后，将参数个数放置在用户栈的最顶部。
// LAB8 处理用户栈中传入的参数，其中 argc 对应参数个数，uargv[] 对应参数的具体内容的地址
// 用于存放所有参数的总长度
uint32_t argv_size = 0, i;

// 遍历用户程序的参数数组，计算所有参数的总长度
for (i = 0; i < argc; i++)
{
    // 先算出所有参数加起来的长度
    argv_size += strnlen(kargv[i], EXEC_MAX_ARG_LEN + 1) + 1;
}

// 计算新栈的位置
// USTACKTOP 是用户栈的顶部地址，这里通过减去参数总长度计算出新栈的底部地址
// 这里除以 sizeof(long) 是为了确保新栈的地址对齐，加 1 是为了确保有足够的空间存放参数结束的 null 字节
uintptr_t stacktop = USTACKTOP - (argv_size / sizeof(long) + 1) * sizeof(long);

// 将参数压入新栈
// 直接将传入的参数压入至新栈的底部
// uargv 是一个指向字符指针数组的指针，每个指针指向一个参数的起始地址
char **uargv = (char **)(stacktop - argc * sizeof(char *));

// 重新初始化参数长度
argv_size = 0;

// 遍历用户程序的参数数组，将参数拷贝到新栈中
for (i = 0; i < argc; i++)
{
    // 将所有参数取出来放置 uargv
    // 这里使用 strcpy 将用户程序的参数拷贝到新栈中，并更新参数长度
    uargv[i] = strcpy((char *)(stacktop + argv_size), kargv[i]);
    argv_size += strnlen(kargv[i], EXEC_MAX_ARG_LEN + 1) + 1;
}

// 更新用户栈顶的位置，存放参数个数
// 计算当前用户栈顶，将参数个数存放在用户栈的最顶部
stacktop = (uintptr_t)uargv - sizeof(int);
*(int *)stacktop = argc;
```

最后，为用户环境设置 `trapframe`。

完成编码后，`make grade`结果：

![image.png](https://s2.loli.net/2023/12/11/dIb8ChU9RBNOMTX.png)

`make qemu`结果：

![image.png](https://s2.loli.net/2023/12/11/LkIUDOXf945xshc.png)

## 扩展练习 Challenge1：完成基于“UNIX的PIPE机制”的设计方案

> 如果要在ucore里加入UNIX的管道（Pipe)机制，至少需要定义哪些数据结构和接口？（接口给出语义即可，不必具体实现。数据结构的设计应当给出一个(或多个）具体的C语言struct定义。在网络上查找相关的Linux资料和实现，请在实验报告中给出设计实现”UNIX的PIPE机制“的概要设方案，你的设计应当体现出对可能出现的同步互斥问题的处理。）

管道数据结构：

- 定义一个数据结构来表示管道。该数据结构包含缓冲区、读指针、写指针等信息。

- ```c
  struct Pipe {
      char buffer[MAX_PIPE_BUFFER_SIZE];  // 管道缓冲区
      size_t size;                        // 当前缓冲区中的数据大小
      size_t read_ptr;                    // 读指针，指示下一个要读取的位置
      size_t write_ptr;                   // 写指针，指示下一个要写入的位置
  };
  ```

在进程控制块中维护文件描述符表，以跟踪每个进程打开的文件。引入新的文件描述符类型用于表示管道的读端和写端。假设 `fd_read` 表示管道的读端，`fd_write` 表示管道的写端。这两个文件描述符将被用于进程间的通信。通过这两个文件描述符，进程可以向管道写入数据（使用写端），或从管道读取数据（使用读端）。

提供一个系统调用或库函数，用于创建管道。这个调用将返回两个文件描述符，一个用于读取管道，一个用于写入管道。

在创建管道后，进程可以使用这两个文件描述符进行读写操作，实现进程间的数据传输。当进程A需要将输出传递给进程B时，进程A的标准输出文件描述符被重定向到管道的写入端，而进程B的标准输入文件描述符被重定向到管道的读取端。

进程A通过写入其标准输出文件描述符将数据写入管道。这些数据被放入管道缓冲区。进程B通过读取其标准输入文件描述符从管道中读取数据。读取的数据从缓冲区中取出。

## 扩展练习 Challenge2：完成基于“UNIX的软连接和硬连接机制”的设计方案

> 如果要在ucore里加入UNIX的软连接和硬连接机制，至少需要定义哪些数据结构和接口？（接口给出语义即可，不必具体实现。数据结构的设计应当给出一个(或多个）具体的C语言struct定义。在网络上查找相关的Linux资料和实现，请在实验报告中给出设计实现”UNIX的软连接和硬连接机制“的概要设方案，你的设计应当体现出对可能出现的同步互斥问题的处理。）

硬链接是多个文件路径指向同一个磁盘索引节点，而软链接是一个文件路径指向另一个文件路径，通过新文件记录对旧路径的引用。删除硬链接只需减少引用计数，而删除软链接或文件则直接删除相应的结构。

硬链接机制的实现：

- 创建硬链接时：
  - 针对新路径（`new_path`），系统创建一个 `sfs_disk_entry` 结构。该结构内的 `ino` 成员指向旧路径（`old_path`）对应的磁盘索引节点。
  - 为了维护正确的引用计数，增加了旧路径对应磁盘索引节点的 `nlinks` 引用计数成员。这表示有多个文件路径指向相同的磁盘索引节点。
- 删除硬链接时：
  - 减少对应磁盘结点（`sfs_disk_inode`）中的 `nlinks` 引用计数。如果 `nlinks` 计数减至零，表示没有路径指向该磁盘索引节点，可以释放相关资源。
  - 删除硬链接的 `sfs_disk_entry` 结构。

软链接的实现：

- 创建软链接时：
  - 为新路径（`new_path`）创建一个全新的文件，建立一个新的 `sfs_disk_inode` 结构。这个新文件用于存储旧路径（`old_path`）的引用。
  - 将旧路径写入新文件中，并标记 `sfs_disk_inode` 的 `type` 为 `SFS_TYPE_LINK`，表示这是一个软链接。
- 删除软链接时：
  - 删除软链接时执行与删除普通文件相同的操作，直接删除对应的 `sfs_disk_entry` 和 `sfs_disk_inode` 结构。
- 软链接允许一个路径指向另一个路径，提供了一种灵活的文件关联方式。然而，软链接引入了两次文件查找的开销，因为系统需要先找到软链接文件，然后再根据其中的路径信息找到实际文件。

## 知识点

ucore的文件系统模型源于Havard的OS161的文件系统和Linux文件系统。但其实这二者都是源于传统的UNIX文件系统设计。UNIX提出了四个文件系统抽象概念：文件(file)、目录项(dentry)、索引节点(inode)和安装点(mount point)。

- **文件**：UNIX文件中的内容可理解为是一有序字节buffer，文件都有一个方便应用程序识别的文件名称（也称文件路径名）。典型的文件操作有读、写、创建和删除等。
- **目录项**：目录项不是目录（又称文件路径），而是目录的组成部分。在UNIX中目录被看作一种特定的文件，而目录项是文件路径中的一部分。如一个文件路径名是“/test/testfile”，则包含的目录项为：根目录“/”，目录“test”和文件“testfile”，这三个都是目录项。一般而言，目录项包含目录项的名字（文件名或目录名）和目录项的索引节点（见下面的描述）位置。
- **索引节点**：UNIX将文件的相关元数据信息（如访问控制权限、大小、拥有者、创建时间、数据内容等等信息）存储在一个单独的数据结构中，该结构被称为索引节点。
- **安装点**：在UNIX中，文件系统被安装在一个特定的文件路径位置，这个位置就是安装点。所有的已安装文件系统都作为根文件系统树中的叶子出现在系统中。

上述抽象概念形成了UNIX文件系统的逻辑数据结构，并需要通过一个具体文件系统的架构设计与实现把上述信息映射并储存到磁盘介质上，从而在具体文件系统的磁盘布局（即数据在磁盘上的物理组织）上具体体现出上述抽象概念。

#### ucore 文件系统总体介绍

实现一个**虚拟文件系统（virtual filesystem, VFS）**, 作为操作系统和更具体的文件系统之间的接口。

我们将在ucore里用虚拟文件系统管理三类设备：

- 硬盘，我们管理硬盘的具体文件系统是`Simple File System`（地位和Ext2等文件系统相同）（`tools/mksfs.c`构造）
- 标准输出（控制台输出），只能写不能读
- 标准输入（键盘输入），只能读不能写

其中，标准输入和标准输出都是比较简单的设备。管理硬盘的Simple File System相对而言比较复杂。

我们的“硬盘”依然需要**通过用一块内存来模拟**。

Lab8的Makefile和之前不同，我们分三段构建内核镜像。

- `sfs.img`: 一块符合SFS文件系统的硬盘，里面存储编译好的用户程序
- `swap.img`: 一段初始化为0的硬盘交换区
- `kernel objects`: ucore内核代码的目标文件

这三部分共同组成`ucore.img`， 加载到QEMU里运行。ucore代码中，我们通过链接时添加的首尾符号，把`swap.img`和`sfs.img`两段“硬盘”（实际上对应两段内存空间）找出来，然后作为“硬盘”进行管理。

#### 文件系统的访问处理过程

ucore 模仿了 UNIX 的文件系统设计，ucore 的文件系统架构主要由四部分组成：

- 通用文件系统访问接口层：该层提供了一个<u>从用户空间到文件系统的标准访问接口</u>。这一层访问接口让**应用程序**能够通过一个简单的接口获得 ucore **内核的文件系统服务**。
- 文件系统抽象层：**向上提供一个一致的接口给内核其他部分**（文件系统相关的系统调用实现模块和其他内核功能模块）访问。向下提供一个同样的**抽象函数指针列表和数据结构屏蔽不同文件系统的实现细节**。
- Simple FS 文件系统层：一个基于**索引**方式的简单文件系统实例。向上通过各种**具体函数实现**以对应文件系统抽象层提出的抽象函数。向下访问**外设接口**。
- 外设接口层：向上提供 **device 访问接口**屏蔽不同硬件细节。向下实现**访问各种具体设备驱动的接口**，比如 disk 设备接口/串口设备接口/键盘设备接口等。

假如应用程序操作文件（打开/创建/删除/读写），首先需要通过文件系统的通用文件系统访问接口层给用户空间提供的访问接口进入<u>文件系统内部</u>，接着由文件系统抽象层把访问请求<u>转发给某一具体文件系统</u>（比如 SFS 文件系统），具体文件系统（Simple FS 文件系统层）把应用程序的访问请求<u>转化为对磁盘上的 block 的处理请求</u>，并通过外设接口层<u>交给磁盘驱动例程</u>来完成具体的磁盘操作。

#### 数据结构

从 ucore 操作系统不同的角度来看，ucore 中的文件系统架构包含四类主要的数据结构，它们分别是：

- 超级块（SuperBlock），它主要从文件系统的全局角度描述特定文件系统的全局信息。它的作用范围是整个 OS 空间。
- 索引节点（inode）：它主要从文件系统的单个文件的角度它描述了文件的各种属性和数据所在位置。它的作用范围是整个 OS 空间。
- 目录项（dentry）：它主要从文件系统的文件路径的角度描述了文件路径中的一个特定的目录项（注：一系列目录项形成目录/文件路径）。它的作用范围是整个 OS 空间。对于 SFS 而言，inode(具体为 struct sfs_disk_inode)对应于物理磁盘上的具体对象，dentry（具体为 struct sfs_disk_entry）是一个内存实体，其中的 ino 成员指向对应的 inode number，另外一个成员是 file name(文件名).
- 文件（file），它主要从进程的角度描述了一个进程在访问文件时需要了解的文件标识，文件读写的位置，文件引用情况等信息。它的作用范围是某一具体进程。
