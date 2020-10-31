#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


void primes(){
    int first_num;
    int num;
    int len;
    int fd[2];
    pipe(fd);

    // 当从标准输入读取时，它实际上是从管道读取的
    // 这里是递归终止条件
    // 注意：判断len的时候，已经从管道中读出了一个数，之后不能重复读取
    if ((len=read(0, &first_num, sizeof(int)) == 0)){
        close(1);
        exit();
    }
 
    printf("prime %d\n", first_num);  // 第一个数一定是素数
    
    if(fork() == 0){  // 子进程
        close(0);
        dup(fd[0]);
        close(fd[0]);
        close(fd[1]);
        primes();
    }else{
        close(1);
        dup(fd[1]);
        close(fd[0]);
        close(fd[1]);
        while ((len = read(0, &num, sizeof(int))) > 0)  // 当仍从管道中读出数据时
        {
            if((num % first_num) != 0){   // 当读出的数不能整除第一个读入的数
                write(1, &num, sizeof(int));
            }
        }
        if (len == 0)
        {
            close(1);
            exit();
        }
        wait();  // 要等子进程先回收
    }
    
}

int main(void){
    int i;
    int fd[2];

    pipe(fd);

    if(fork() == 0){
        close(0);  // 关闭标准输入
        dup(fd[0]);  // 将管道的读端口拷贝在描述符0上
        close(fd[0]);
        close(fd[1]);
        primes();
    }else{
        close(1);
        dup(fd[1]);
        close(fd[0]);
        close(fd[1]);
        for(i=2; i<36; i++){
            write(1, &i, sizeof(int));  // 向管道的写端口写入
        }
        close(1);
        wait();  // 要等子进程先回收
    }
    exit();
}