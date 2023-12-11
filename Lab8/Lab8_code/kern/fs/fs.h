#ifndef __KERN_FS_FS_H__
#define __KERN_FS_FS_H__

#include <defs.h>
#include <mmu.h>
#include <sem.h>
#include <atomic.h>

#define SECTSIZE            512
#define PAGE_NSECT          (PGSIZE / SECTSIZE)

#define SWAP_DEV_NO         1
#define DISK0_DEV_NO        2
#define DISK1_DEV_NO        3

void fs_init(void);
void fs_cleanup(void);

struct inode;
struct file;

/*
 * 进程文件相关信息结构体
 */
struct files_struct {
    struct inode *pwd;                //进程当前执行目录的内存inode指针
    struct file *fd_array;            //进程打开文件的数组，文件数组后面分析
    int files_count;            	  //访问此文件的线程个数
    semaphore_t files_sem;            //确保对进程控制块中fs_struct的互斥访问
};

#define FILES_STRUCT_BUFSIZE                       (PGSIZE - sizeof(struct files_struct))
#define FILES_STRUCT_NENTRY                        (FILES_STRUCT_BUFSIZE / sizeof(struct file))

void lock_files(struct files_struct *filesp);
void unlock_files(struct files_struct *filesp);

struct files_struct *files_create(void);
void files_destroy(struct files_struct *filesp);
void files_closeall(struct files_struct *filesp);
int dup_files(struct files_struct *to, struct files_struct *from);

static inline int
files_count(struct files_struct *filesp) {
    return filesp->files_count;
}

static inline int
files_count_inc(struct files_struct *filesp) {
    filesp->files_count += 1;
    return filesp->files_count;
}

static inline int
files_count_dec(struct files_struct *filesp) {
    filesp->files_count -= 1;
    return filesp->files_count;
}

#endif /* !__KERN_FS_FS_H__ */

