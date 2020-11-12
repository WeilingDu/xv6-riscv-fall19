#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path, char *re) 
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  // 打开文件，获得文件描述符fd
  if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }
  // 获取文件描述符指向的文件的信息，存入st中
  if(fstat(fd, &st) < 0 || T_DIR != st.type){  // 如果该文件不存在或者该路径不是目录
    fprintf(2, "find: the first arg must be dir path\n");
    close(fd);
    return;
  }

  while(read(fd, &de, sizeof(de)) == sizeof(de)) {
    strcpy(buf, path);
    p = buf + strlen(buf);  // 将指针p移到指向buf的最后一个字符之后一个字符的位置
    *p++ = '/';  // 在buf之后加上斜杠，然后指针后移一位
    if(de.inum == 0) {  // 该条目的编号为0
      continue;
    }
    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;  // 条目的名字要以0结尾
    if(stat(buf, &st) < 0){
      printf("find: cannot stat %s\n", buf);
      continue;
    }
    switch(st.type) {
      case T_FILE:
        if (strcmp(re, de.name) == 0) {
          printf("%s\n", buf);
        }
        break;
      case T_DIR:
        // 递归
        if (strcmp(de.name, ".") != 0 && strcmp(de.name, "..") != 0) {
          find(buf, re);
        }
        break;
      }
  }
  close(fd);
}

int main(int argc, char *argv[])
{
  if(argc < 3){
    fprintf(2, "usage: find <path> <expression>\n");
    exit();
  }
  find(argv[1], argv[2]);
  exit();
}