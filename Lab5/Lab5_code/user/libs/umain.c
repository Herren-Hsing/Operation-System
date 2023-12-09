#include <ulib.h>

int main(void);

/*
 ** 这是所有应用程序执行的第一个C函数
 ** 它将调用应用程序的main函数，并在main函数结束后调用exit函数
 ** exit函数最终将调用sys_exit系统调用，让操作系统回收进程资源
 */
void umain(void) {
    // 调用main函数，并将返回值存储在ret变量中
    int ret = main();

    // 调用exit函数，传递main函数的返回值
    exit(ret);
}
