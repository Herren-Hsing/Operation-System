# OS Lab 8

Contributors：焦心雨(2112536)、李艺楠(2110246)、辛浩然(2112514)

- 包括练习+Challenge

[GitHub链接](https://github.com/Herren-Hsing/Operation-System)

## 练习0：填写已有实验

填写之前Lab中完成的代码，其中需要修改的部分包括`proc.c` 中的相关函数以及`schedule`相关函数。

### `proc.c` 文件

此处修改细节详情见练习2。

### Round Robin 调度

由于没有进行Lab6，因此将进程调度算法更换为默认的Round Robin 调度算法。

## 练习1: 完成读文件操作的实现（需要编码）

> 首先了解打开文件的处理流程，然后参考本实验后续的文件读写操作的过程分析，填写在` kern/fs/sfs/sfs_inode.c`中 的`sfs_io_nolock()`函数，实现读文件中数据的代码。

首先从宏观上来梳理一个读写文件的流程：

- 某一个进程调用了用户程序相关的库的接口。
- 用户进程通过中断进入内核调用抽象层的接口。文件系统抽象层向上提供一个一致的接口给内核中文件系统相关的系统调用实现模块和其他内核功能模块访问，同时向下提供一个同样的抽象函数指针列表和数据结构屏蔽不同文件系统的实现细节。
- 从抽象层进入操作系统的真正的文件系统，即SFS。
- 文件系统调用硬盘io接口。

而在读写文件之前，在内核初始化的时候调用`fs_init`进行了文件系统的初始化，主要包括：

- `vfs_init`  初始化虚拟文件系统。具体为给引导文件系统bootfs的信号量置为1，让它能正常执行然后加载必要项，同时初始化vfs的设备列表，它的对应的信号量也置为1。
- `dev_init` 初始化设备模块。将这次实验用到的stdin、stdout和磁盘disk0初始化。
- `sfs_init` 初始化SFS文件系统。把disk0挂载，使其可以被访问和操作。

### 打开文件的处理流程

1. 通用文件系统访问接口层：

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

在`sysfile_open`内核函数中，需要把位于用户空间的字符串`__path`拷贝到内核空间中的字符串`path`中，然后调用了`file_open`。

2. 文件系统抽象层：

在`file_open`函数中，程序主要做了以下几个操作：

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

这个流程中，以vop开头的函数通过一些宏和函数的转发，最后变成对inode结构体里的inode_ops结构体的“成员函数”的调用。对于SFS文件系统的inode来说，会变成对sfs文件系统的具体操作，因此接下来会执行`sys_lookup`函数。

3. SFS 文件系统层

`sfs_lookup`有三个参数：`node`，`path`，`node_store`。其中`node`是根目录“/”所对应的inode节点；`path`是文件`sfs_filetest1`的绝对路径/filetest，而`node_store`是经过查找获得的filetest所对应的inode节点。

`sfs_lookup`函数以“/”为分割符，从左至右逐一分解`path`获得各个子目录和最终文件对应的`inode`节点。在本例中是调用`sfs_lookup_once`查找以根目录下的文件filetest所对应的`inode`节点。当无法分解`path`后，就意味着找到了filetest对应的`inode`节点，就可顺利返回了。

`sfs_lookup_once`将调用`sfs_dirent_search_nolock`函数来查找与路径名匹配的目录项，如果找到目录项，则调用`sfs_load_inode`函数根据目录项中记录的`inode`所处的数据块索引值找到路径名对应的SFS磁盘`inode`，并读入SFS磁盘`inode`对应的内容，创建SFS内存`inode`。

在打开文件的处理流程中，如果仅仅只是找到这个文件的描述符然后把它存起来，在这个过程中似乎不涉及到具体设备的交互。打开文件的操作也在sfs之后没有下文了。具体的文件操作在读写时会详细涉及。

### 读文件流程分析

1. 通用文件系统访问接口层：

依次调用如下用户态函数：`read->sys_read->syscall`，从而引起系统调用进入到内核态。

到了内核态以后，通过中断处理例程，会调用到`sys_read`内核函数，并进一步调用`sysfile_read`内核函数，进入到文件系统抽象层处理流程完成进一步读文件的操作。接下来的工作包括：

- 测试当前待读取的文件是否存在**读权限**
- 在内核中创建一块缓冲区。
- 循环执行`file_read`函数读取数据至缓冲区中，并将该缓冲区中的数据复制至用户内存（即传入`sysfile_read`的base指针所指向的内存）

2. 文件系统抽象层：

`file_read`函数是内核提供的一项文件读取函数。在`file_read`函数中，通过文件描述符查找到了相应文件对应的内存中的`inode`信息，然后转交给`vop_read`进行读取处理。

```c
// read file
int
file_read(int fd, void *base, size_t len, size_t *copied_store) {
    int ret; // 用于存储函数调用的返回值
    struct file *file; // 文件结构体指针
    *copied_store = 0; // 初始化已复制数据长度为0
    // 通过文件描述符获取文件结构体指针，若失败则返回相应错误码
    if ((ret = fd2file(fd, &file)) != 0) {
        return ret;
    }
    // 检查文件是否可读，若不可读则返回错误码 -E_INVAL
    if (!file->readable) {
        return -E_INVAL;
    }
    fd_array_acquire(file); // 获取文件的引用计数
    // 初始化缓冲区用于文件读取，设置缓冲区的起始位置为文件的当前位置
    struct iobuf __iob, *iob = iobuf_init(&__iob, base, len, file->pos);
    // 调用底层文件操作的读取函数，将数据读取到缓冲区中
    ret = vop_read(file->node, iob);
    // 获取实际已复制的数据长度
    size_t copied = iobuf_used(iob);
    // 若文件状态为打开状态，则更新文件的当前位置
    if (file->status == FD_OPENED) {
        file->pos += copied;
    }
    *copied_store = copied; // 更新已复制数据长度
    fd_array_release(file); // 释放文件的引用计数
    return ret; // 返回读取操作的结果
}
```

`file_read`函数完成了如下工作：

- 首先检查文件描述符是否有效，文件是否可读。

- 初始化文件IO缓冲区并调用vop_read（）进行底层文件系统的读取操作，将数据读取到缓冲区中。

- 更新文件的当前位置，并记录实际读取的数据长度。

 这里，我们就通过调用vop_read（）函数，到达了最关键的SFS层面，**实现真正的对硬盘操作，以此来进行文件的读入内存。**

3. S文件系统层：

在`vop_read`函数中，实际上通过先前的inode中函数指针的处理，使其从抽象方法具体到了我们的SFS文件系统中的sfs_read函数，事实上就是转交到了`sfs_read`函数进行处理（通过函数指针），然后调用了`sfs_io`函数。

```c
static int
sfs_read(struct inode *node, struct iobuf *iob) {
    return sfs_io(node, iob, 0);
}
```

sfs_io函数需要三个参数，分别含义如下：node是对应文件的inode，iob是缓存，write表示是读还是写的布尔值（0表示读，1表示写），这里是0。其具体实现为：

```c
static inline int
sfs_io(struct inode *node, struct iobuf *iob, bool write) {
    // 获取SFS文件系统信息和SFS inode信息
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    int ret;
    lock_sin(sin); // 锁定SFS inode
    {
        size_t alen = iob->io_resid; // 获取待处理的IO长度
        // 调用SFS文件系统的无锁IO操作函数
        ret = sfs_io_nolock(sfs, sin, iob->io_base, iob->io_offset, &alen, write);
        // 如果实际处理的IO长度不为0，更新iob以跳过已处理的部分
        if (alen != 0) {
            iobuf_skip(iob, alen);
        }
    }
    unlock_sin(sin); // 解锁SFS inode
    return ret; // 返回IO操作的结果
}
```

在`sfs_io`中再进一步调用了`sfs_io_nolock`函数，这就是本次练习中需要完善的函数.

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

在`sfs_io_nolock`的实现中，给出了对buf和对block的两种操作，即用sfs_buf_op和sfs_block_op两个函数指针进行读取，调用`sfs_rbuf`函数对磁盘上的一个磁盘块进行基本的块级 I/O 操作：

```c
/*
 * sfs_rbuf - 用于对磁盘上的一个磁盘块进行基本的块级 I/O 操作（非块、非对齐 I/O），
 * 使用 sfs->sfs_buffer 进行操作，同时提供锁保护以处理对磁盘块的互斥读取。
 * @sfs:    将要被处理的 sfs_fs 文件系统
 * @buf:    用于读取的缓冲区
 * @len:    需要读取的长度
 * @blkno:  磁盘块的编号
 * @offset: 磁盘块内容中的偏移量
 */
int
sfs_rbuf(struct sfs_fs *sfs, void *buf, size_t len, uint32_t blkno, off_t offset) {
    // 确保 offset 在有效范围内
    assert(offset >= 0 && offset < SFS_BLKSIZE && offset + len <= SFS_BLKSIZE);
    int ret;
    // 对 sfs_io 进行加锁，保证互斥读取磁盘块
    lock_sfs_io(sfs);
    {
        // 调用 sfs_rwblock_nolock 进行磁盘块读取
        if ((ret = sfs_rwblock_nolock(sfs, sfs->sfs_buffer, blkno, 0, 1)) == 0) {
            // 将从磁盘块中读取的数据复制到用户提供的缓冲区中
            memcpy(buf, sfs->sfs_buffer + offset, len);
        }
    }
    // 解锁 sfs_io
    unlock_sfs_io(sfs);
    // 返回操作结果
    return ret;
}

```

其中调用了`sfs_rwblock_nolock`,此函数会调用设备接口dop_io 执行块级 I/O 操作，将操作传递给底层设备，由此进入外设接口层。

```c
/*
 * sfs_rwblock_nolock - 用于基本的块级 I/O 操作，用于读/写一个磁盘块，
 *                      在读/写磁盘块时不使用锁进行保护
 * @sfs:   将要处理的 sfs_fs 结构
 * @buf:   用于读/写的缓冲区
 * @blkno: 磁盘块的编号
 * @write: 布尔值，表示读还是写操作
 * @check: 布尔值，如果为真，则检查磁盘块编号是否在有效范围内
 */
static int
sfs_rwblock_nolock(struct sfs_fs *sfs, void *buf, uint32_t blkno, bool write, bool check) {
    // 确保磁盘块编号在有效范围内（如果需要检查）
    assert((blkno != 0 || !check) && blkno < sfs->super.blocks);
    // 初始化 iobuf 结构
    struct iobuf __iob, *iob = iobuf_init(&__iob, buf, SFS_BLKSIZE, blkno * SFS_BLKSIZE);
    // 调用 dop_io 执行块级 I/O 操作，将操作传递给底层设备
    return dop_io(sfs->dev, iob, write);
}
```

4. 外设接口层：

dop_io函数封装了device类型中的函数指针d_io，对于我们的读写磁盘操作，实际上是对disk0对象进行了d_io的调用，也就是其函数指针所指向的具体函数，即如下初始化中指向的disk0_io，这部分才是真正完成磁盘级别操作的代码：

```c
    dev->d_open = disk0_open;
    dev->d_close = disk0_close;
    dev->d_io = disk0_io;
    dev->d_ioctl = disk0_ioctl;
```

disk0_io函数内部进行了大量的底层函数接口调用:

```c
/*
 * disk0_io - 处理磁盘 I/O 操作，读取或写入数据
 * @dev: 设备结构体指针
 * @iob: I/O 缓冲区结构体指针
 * @write: 标志是否为写操作
 *
 * 该函数处理磁盘 I/O 操作，根据参数决定是读取还是写入数据。它会将数据从 I/O 缓冲区移动到 disk0_buffer 中，
 * 然后调用相应的读取或写入函数进行实际的磁盘 I/O 操作。
 */
static int
disk0_io(struct device *dev, struct iobuf *iob, bool write) {
    // 获取偏移量和剩余长度
    off_t offset = iob->io_offset;
    size_t resid = iob->io_resid;
    // 计算起始块号和块数
    uint32_t blkno = offset / DISK0_BLKSIZE;
    uint32_t nblks = resid / DISK0_BLKSIZE;

    /* 不允许非块对齐的 I/O 操作 */
    if ((offset % DISK0_BLKSIZE) != 0 || (resid % DISK0_BLKSIZE) != 0) {
        return -E_INVAL;
    }

    /* 不允许超出磁盘边界的 I/O 操作 */
    if (blkno + nblks > dev->d_blocks) {
        return -E_INVAL;
    }

    /* 读/写的块数为零，无需进行操作 */
    if (nblks == 0) {
        return 0;
    }

    lock_disk0();
    while (resid != 0) {
        size_t copied, alen = DISK0_BUFSIZE;
        if (write) {
            // 对于写操作，将数据从 iob 移动到 disk0_buffer
            iobuf_move(iob, disk0_buffer, alen, 0, &copied);
            // 确保成功移动了一些数据且移动的数据是块对齐的
            assert(copied != 0 && copied <= resid && copied % DISK0_BLKSIZE == 0);
            // 计算块数并调用 disk0_write_blks_nolock 进行磁盘写操作
            nblks = copied / DISK0_BLKSIZE;
            disk0_write_blks_nolock(blkno, nblks);
        } else {
            // 对于读操作，从磁盘读取数据到 disk0_buffer
            if (alen > resid) {
                alen = resid;
            }
            nblks = alen / DISK0_BLKSIZE;
            disk0_read_blks_nolock(blkno, nblks);
            // 将数据从 disk0_buffer 移动到 iob
            iobuf_move(iob, disk0_buffer, alen, 1, &copied);
            // 确保成功移动了所有数据且移动的数据是块对齐的
            assert(copied == alen && copied % DISK0_BLKSIZE == 0);
        }
        resid -= copied, blkno += nblks;
    }
    unlock_disk0();
    return 0;
}
```

这里完成了对 `disk0` 设备执行IO操作。根据给定的 `iob` 信息，它会将数据从 `iob` 中移动到 `disk0_buffer` 或者将 `disk0_buffer` 中的数据移动到 `iob` 中。具体的读写过程还在在底层的`disk0_read_blks_nolock` 和 `disk0_write_blks_nolock` 两个函数和移动数据的` iobuf_move` 函数中完成。以读取的disk0_read_blks_nolock函数为例，实际上调用了ide的接口，完成了这一工作：

```c
/*
 * disk0_read_blks_nolock - 从磁盘读取指定块数的数据，不获取磁盘锁
 * @blkno: 起始块号
 * @nblks: 读取的块数
 *
 * 该函数通过调用 ide_read_secs 从磁盘读取指定块数的数据到 disk0_buffer 中。
 */
static void
disk0_read_blks_nolock(uint32_t blkno, uint32_t nblks) {
    int ret;
    // 计算起始扇区号和扇区数
    uint32_t sectno = blkno * DISK0_BLK_NSECT, nsecs = nblks * DISK0_BLK_NSECT;
    // 调用 ide_read_secs 读取数据
    if ((ret = ide_read_secs(DISK0_DEV_NO, sectno, disk0_buffer, nsecs)) != 0) {
        // 读取失败时触发 panic
        panic("disk0: read blkno = %d (sectno = %d), nblks = %d (nsecs = %d): 0x%08x.\n",
                blkno, sectno, nblks, nsecs, ret);
    }
}
```

跳过ide内部冗余繁杂的封装和函数指针处理，直接到最底层的处理接口，即ramdisk_read函数，可以看到最底层的实现，实际上只是一个简单的memcpy实现:

```c
/*
 * ramdisk_read - 从 RAM 磁盘设备读取数据
 * @dev:    RAM 磁盘设备的指针
 * @secno:  起始扇区编号
 * @dst:    存储读取数据的目标缓冲区
 * @nsecs:  读取扇区的数量
 */
static int ramdisk_read(struct ide_device *dev, size_t secno, void *dst,
                        size_t nsecs) {
    // 限制读取扇区数量不超过设备剩余的扇区数
    nsecs = MIN(nsecs, dev->size - secno);
    // 如果 nsecs 为负数，返回错误
    if (nsecs < 0) return -1;
    // 使用 memcpy 从 RAM 磁盘设备读取数据到目标缓冲区
    memcpy(dst, (void *)(dev->iobase + secno * SECTSIZE), nsecs * SECTSIZE);
    // 返回操作结果
    return 0;
}

```

由此，从通用文件访问层到外设接口层的文件读取过程分析完成。

## 练习2: 完成基于文件系统的执行程序机制的实现（需要编码）

> 改写`proc.c`中的`load_icode`函数和其他相关函数，实现基于文件系统的执行程序机制。执行：`make qemu`。如果能看看到sh用户程序的执行界面，则基本成功了。如果在sh用户界面上可以执行”ls”,”hello”等其他放置在sfs文件系统中的其他执行程序，则可以认为本实验基本成功。

在本次实验中，主要修改了proc.c中的以下函数：

```C
alloc_proc
proc_run
do_fork
load_icode
```

### （一）alloc_proc

`alloc_proc`函数用于分配一个 `proc_struct` 结构并初始化所有 `proc_struct` 的字段。

在本次实验中，Lab8中的进程控制块 `proc_struct` 相较于我们上一次完成的Lab5中新增了以下字段：

```C
struct proc_struct {
	......
	struct run_queue *rq;// 包含进程的运行队列
    list_entry_t run_link;// 连接到运行队列的条目
    int time_slice;// 占用 CPU 的时间片
    skew_heap_entry_t lab6_run_pool;//仅用于实验6：在运行池中的条目
    uint32_t lab6_stride; //仅用于实验6：进程的当前步幅
    uint32_t lab6_priority;//仅用于实验6：进程的优先级，由lab6_set_priority(uint32_t) 设置
    //Lab8新增
    struct files_struct *filesp;//指向进程的文件相关信息结构体的指针
}
```

因此相应的，我们也需要在初始化函数中增加对于这些新增字段的初始化方法：

```C
static struct proc_struct *alloc_proc(void) {
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
    ....
     proc->rq = NULL;//将rq初始化为 NULL，表示该进程没有被放入任何运行队列。
     list_init(&(proc->run_link));//将运行队列连接链表初始化为空
     proc->time_slice = 0;//占用 CPU 的时间片初始化为0
     proc->lab6_run_pool.left = proc->lab6_run_pool.right = proc->lab6_run_pool.parent = NULL;//Lab6中所使用的的二叉堆，将左子节点、右子节点和父节点初始化为 NULL
     proc->lab6_stride = 0;//进程的当前步幅初始化为 0。
     proc->lab6_priority = 0;//进程的优先级初始化为 0。
     proc->filesp = NULL;//Lab8新增，进程没有打开任何文件，没有指向具体结构体的指针
}
```

### （二）proc_run

`proc_run`函数用于将参数中的进程 `proc` 在 `CPU` 上设为运行状态，在本次实验中需要在切换页目录表后刷新TLB表。

```C
proc_run(struct proc_struct *proc) {
 	if (proc != current) {
 	...
 	        local_intr_save(intr_flag);
        {
            current = proc;
            lcr3(next->cr3);
            flush_tlb();//添加 flush_tlb 语句，以在切换页目录表后对 TLB 刷新。
            switch_to(&(prev->context), &(next->context));
        }
```

由于页目录表的切换可能导致 TLB 中的缓存的映射关系失效，因此需要刷新 TLB来确保新的进程的地址空间映射关系正确加载，避免出现地址转换错误。

### （三）do_fork

`do_fork`函数用于创建一个新的子进程，在本次实验中，我们使用copy_files来复制文件描述符。

```C
int  do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf) {
    ...
    if ((proc = alloc_proc()) == NULL) {
        goto fork_out;
    }
    proc->parent = current;
    assert(current->wait_state == 0);

    if (setup_kstack(proc) != 0) {
        goto bad_fork_cleanup_proc;
    }
  
    if (copy_files(clone_flags, proc) != 0) {//Lab8新增：copy_files用于复制文件描述符
        goto bad_fork_cleanup_kstack;//复制文件描述符时出现了错误后跳转
    }

    if (copy_mm(clone_flags, proc) != 0) {
        goto bad_fork_cleanup_fs;
    }
    ...
} 
```

`copy_files()`函数成功的话，返回值应该是0，如果返回值不为0，就跳转到`bad_fork_cleanup_kstack`标签上清理空间，释放当前资源。

### （四）load_icode

**`load_icode`函数作用：**用于从磁盘上读取可执行文件并加载到内存中，完成内存空间的初始化。

**Lab8与之前实验的区别**：之前的实验用户内存空间中所加载的ELF可执行文件是已经被加载到了内核内存空间中的，没有涉及从磁盘读取数据的操作，也没有对`execve`所执行的程序传入参数。而在Lab8中，我们需要模拟磁盘的读写操作，那么就需要读入原始的ELF文件数据。

**`load_icode()`函数编写的基本思路：**与Lab5大致相同，完整思路如下：

1. 为当前进程创建一个新的内存管理器
2. 创建一个新的页目录表（PDT），并将`mm->pgdir`设置为PDT的内核虚拟地址
3. 将二进制文件的TEXT/DATA/BSS部分复制到进程的内存空间中
   1. 读取文件中的原始数据内容并解析`elfhdr`
   2. 根据`elfhdr`中的信息，读取文件中的原始数据内容并解析`proghdr`
   3. 调用`mm_map`构建与TEXT/DATA相关的vma
   4. 调用`pgdir_alloc_page`为TEXT/DATA分配页，读取文件中的内容并将其复制到新分配的页中
   5. 调用`pgdir_alloc_page`为BSS分配页，在这些页中使用`memset`将其置零
4. 调用`mm_map`设置用户堆栈，并将参数放入用户堆栈中
5. 设置当前进程的内存管理器、cr3，重置`pgidr`（使用`lcr3`宏）
6. 在用户堆栈中设置`uargc`和`uargv`
7. 为用户环境设置`trapframe`
8. 如果上述步骤失败，应清理环境。

**load_icode()在Lab8修改的部分：**

**（1）**首先，当前进程创建一个新的 `mm` 结构`(mm_create())`；创建一个新的页目录表（PDT），并将 `mm->pgdir` 设置为页目录表的内核虚拟地址`(setup_pgdir(mm))`。

**（2）**接下来，将二进制文件的`TEXT/DATA/BSS`部分复制到进程的内存空间中。首先调用`load_icode_read`函数读取文件中的原始数据内容并解析`elfhdr`。这里与之前实验不同，需要从文件中读取`ELF header`。`load_icode_read`函数用来从文件描述符`fd`指向的文件中读取数据到缓冲区中，传入的参数包括`fd`（文件描述符）、`elf`（指向读取数据的缓冲区）、`sizeof(struct elfhdr)`（要读取的数据长度）和`0`（文件中的偏移量）。

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

**（3）**读取后，检查是否是一个合法的ELF文件。

合法的ELF文件中的魔数通常为“0X7FELF”，转化为小端序为”0x464C457FU”。宏定义ELF_MAGIC保存了这个值，因此我们直接使用这个宏定义来判断ELF文件的魔数是否正确。

```C
if (elf->e_magic != ELF_MAGIC){
    ret = -E_INVAL_ELF;
    goto bad_elf_cleanup_pgdir;
}
```

**（4）**根据`elfhdr`中的信息，读取文件中的原始数据内容并解析`proghdr`

再次调用`load_icode_read`函数从文件特定偏移处读取每个段的详细信息（包括大小、基地址等等），然后我们就可以根据每一段的大小和基地址来为其分配不同的内存空间。

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
```

在分配空间时我们解决了三种特殊情况：

- 如果不是需要加载的段，直接跳过

  ```C
  if (ph->p_type != ELF_PT_LOAD){//p_type不为加载类型
      continue; // 不是需要加载的段，直接跳过
  }
  ```

- 文件头标明的文件段大小大于所占用的内存大小，返回错误

  ```C
  if (ph->p_filesz > ph->p_memsz){文件头标明的文件段大小大于所占用的内存大小
  //memsz可能包括了BSS，所以这是错误的程序段头
      ret = -E_INVAL_ELF;//返回错误码
      goto bad_cleanup_mmap;//跳转清理标签
  }
  ```

- 文件大小为0，直接跳过

  ```C
  if (ph->p_filesz == 0) {//文件中的大小为0
      {
      // 继续执行；
      // 在这里什么都不做，因为静态变量可能不占用任何空间
      }
  }
  ```

**（5）**调用mm_map构建与TEXT/DATA相关的虚拟内存地址vma

首先根据程序段头的标志位 `p_flags` 设置相关权限，之后调用` mm_map` 函数设置新的 VMA，建立`ph->p_va`到`ph->va+ph->p_memsz`的合法虚拟地址空间段。

**（6）**逐页分配物理内存空间

接下来我们需要分配内存，并将每个程序段的内容（`from, from+end`）复制到进程的内存（`la, la+end`）。

在复制 TEXT/DATA 段的内容的while循环中，首先调用pgdir_alloc_page()函数为所需要复制的TEXT/DATA段分配一个内存页，建立la对应页的虚实映射关系。然后调用`load_icode_read`函数读取`elf`对应段内的`size`大小的数据块并写入至该内存中。

```C
while (start < end) {// 为 TEXT/DATA 段逐页分配物理内存空间
    if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
        ret = -E_NO_MEM;
        goto bad_cleanup_mmap;
    }

    off = start - la, size = PGSIZE - off, la += PGSIZE;//更新当前变量

    if (end < la) {
        size -= la - end;// 每次读取size大小的块，直至全部读完
    }

    if ((ret = load_icode_read(fd, page2kva(page) + off, size, offset)) != 0) { 
        goto bad_cleanup_mmap;
    }

    start += size, offset += size;//更新当前变量
}

end = ph->p_va + ph->p_memsz;// 计算终止地址，建立BSS段
    
if (start < la) {//存在 BSS 段,且TEXT/DATA 段分配的最后一页没有被完全占用
    /* ph->p_memsz == ph->p_filesz */
    if (start == end) {
        continue;
    }
    off = start + PGSIZE - la, size = PGSIZE - off;
    if (end < la) {
        size -= la - end;
    }
    memset(page2kva(page) + off, 0, size);//清零初始化用于存放BSS段
    start += size;
    assert((end < la && start == end) || (end >= la && start == la));
}
```

之后，构建二进制程序的 BSS 段，如果 BSS 段还需要更多的内存空间就进一步进行分配。

```c
while (start < end){
    if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL){
        ret = -E_NO_MEM;// 为 BSS 段分配新的物理内存页
        goto bad_cleanup_mmap;
    }
    off = start - la, size = PGSIZE - off, la += PGSIZE;
    if (end < la){
        size -= la - end;
    }
    memset(page2kva(page) + off, 0, size);
    start += size;
}
```

**（7）**关闭读取的ELF文件

```c
sysfile_close(fd);
```

**（8）**设置当前进程的内存管理器、cr3，重置pgidr

设置当前进程的` mm`、`sr3`，并设置 CR3 寄存器为页目录表的物理地址。

```c
mm_count_inc(mm);
current->mm = mm;
current->cr3 = PADDR(mm->pgdir);//把mm->pgdir赋值到cr3寄存器中,更新用户进程的虚拟内存空间
lcr3(PADDR(mm->pgdir));
```

**（9）**在用户堆栈中设置uargc和uargv

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

**（10）**为用户环境设置 `trapframe`。

```C
struct trapframe *tf = current->tf;
uintptr_t sstatus = tf->status;//设置状态寄存器 sstatus
memset(tf, 0, sizeof(struct trapframe));
tf->gpr.sp = stacktop;//设置用户栈指针（sp）
tf->epc = elf->e_entry;//用户程序入口地址（epc
tf->status = sstatus & ~(SSTATUS_SPP | SSTATUS_SPIE);
ret = 0;
```

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
