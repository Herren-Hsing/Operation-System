#include <stdio.h>
#include <ulib.h>

int magic = -0x10384;

// lab5/user/exit.c
int main(void)
{
    int pid, code;
    cprintf("I am the parent. Forking the child...\n");
    // 通过fork库函数执行do_fork系统调用复制一个子线程执行
    if ((pid = fork()) == 0)
    {
        // 子线程fork返回后的pid为0
        cprintf("I am the child.\n");
        yield();
        yield();
        yield();
        yield();
        yield();
        yield();
        yield();
        // 通过exit库函数执行do_exit系统调用，退出子线程
        exit(magic);
    }
    else
    {
        // 父线程会执行到这里，fork返回的pid为子线程的pid，不等于0
        cprintf("I am parent, fork a child pid %d\n", pid);
    }
    assert(pid > 0);
    cprintf("I am the parent, waiting now..\n");

    // 父线程通过wait库函数执行do_wait系统调用，等待子线程退出并回收(waitpid返回值=0代表回收成功)
    assert(waitpid(pid, &code) == 0 && code == magic);
    // 再次回收会失败waitpid返回值不等于0
    assert(waitpid(pid, &code) != 0 && wait() != 0);
    cprintf("waitpid %d ok.\n", pid);

    cprintf("exit pass.\n");
    return 0;
}
