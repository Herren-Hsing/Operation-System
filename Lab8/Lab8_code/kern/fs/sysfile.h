#ifndef __KERN_FS_SYSFILE_H__
#define __KERN_FS_SYSFILE_H__

#include <defs.h>

struct stat;
struct dirent;

// 打开或创建一个文件，open_flags 参数根据系统调用而定
int sysfile_open(const char *path, uint32_t open_flags);
// 关闭一个已打开的 vnode（虚拟节点）
int sysfile_close(int fd);
// 读取文件内容
int sysfile_read(int fd, void *base, size_t len);
// 写入文件内容
int sysfile_write(int fd, void *base, size_t len);
// 移动文件指针位置
int sysfile_seek(int fd, off_t pos, int whence);
// 获取文件状态信息
int sysfile_fstat(int fd, struct stat *stat);
// 同步文件，确保数据被写入到磁盘
int sysfile_fsync(int fd);
// 切换当前工作目录
int sysfile_chdir(const char *path);
// 创建目录
int sysfile_mkdir(const char *path);
// 创建文件硬链接
int sysfile_link(const char *path1, const char *path2);
// 重命名文件或目录
int sysfile_rename(const char *path1, const char *path2);
// 删除文件或目录
int sysfile_unlink(const char *path);
// 获取当前工作目录
int sysfile_getcwd(char *buf, size_t len);
// 获取目录中的文件条目
int sysfile_getdirentry(int fd, struct dirent *direntp);
// 复制文件描述符
int sysfile_dup(int fd1, int fd2);
// 创建管道
int sysfile_pipe(int *fd_store);
// 创建命名管道
int sysfile_mkfifo(const char *name, uint32_t open_flags);

#endif /* !__KERN_FS_SYSFILE_H__ */

