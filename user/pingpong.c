#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void){
    char parent_buf[5]; // 父进程读取到的信息
    char child_buf[5];  // 子进程读取到的信息
    int parent_fd[2], child_fd[2];  
    int pid;
    pipe(parent_fd);  // 父进程的管道
    pipe(child_fd);  // 子进程的管道

    pid = fork();
    if (pid > 0)
    {
        write(parent_fd[1], "ping\n", 5);  // 父进程向管道中写入ping
        close(parent_fd[1]);  // 关闭管道的写端，否则读出端无法判断传输的结束
        read(child_fd[0], parent_buf, sizeof(parent_buf));  // 读取子进程的回复
        printf("%d: received %s", &pid, &parent_buf);
    } else
    {
        read(parent_fd[0], child_buf, sizeof(child_buf));  // 子进程从管道中读取
        write(child_fd[1], "pong\n", 5);  // 子进程向管道中写入pong
        close(child_fd[1]);
        printf("%d: received %s", &pid, &child_buf);
    }
    exit();
}
