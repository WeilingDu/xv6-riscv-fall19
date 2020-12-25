// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

// 将kalloc的共享freelist改为每个CPU独立的freelist；
struct {
  struct spinlock lock[NCPU];
  struct run *freelist[NCPU];
} kmem;

// 在系统启动时，main()函数调用kinit()来初始化分配器
// kinit通过保存所有空闲页来初始化链表
// kinit调用freerange来把空闲内存加到链表里，freerange是把每个空闲页逐一加到链表里来实现此功能的。
void kinit()
{
  for (int i = 0; i < NCPU; i++)
    initlock(&kmem.lock[i], "kmem");  // 要初始化每个CPU的分配器
  freerange(end, (void*)PHYSTOP);  // 使用freerange为所有运行freerange的CPU分配空闲的内存
}

void freerange(void *pa_start, void *pa_end)
{
  push_off();

  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
  
  pop_off();
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  push_off();  // 获取cpu id之前要关闭中断，防止线程切换
  int id = cpuid();  // 获取cpu id
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock[id]);  // 获取锁
  r->next = kmem.freelist[id];
  kmem.freelist[id] = r;
  release(&kmem.lock[id]);  // 释放锁

  pop_off();  // 打开中断
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void)
{
  push_off();  // 关闭中断
  int id = cpuid();

  struct run *r;

  // 获取内存块时，优先分配当前CPU的freelist中的内存块
  acquire(&kmem.lock[id]);
  r = kmem.freelist[id];
  if(r)
    kmem.freelist[id] = r->next;
  release(&kmem.lock[id]);

  // 当前CPU没有空闲内存块，则从其他CPU的freelist中窃取内存块
  if(!r) {
    for(int i=0;i<NCPU;i++) {
      acquire(&kmem.lock[i]);
      r = kmem.freelist[i];
      if(r)
        kmem.freelist[i] = r->next;
      release(&kmem.lock[i]);

      if(r)
        break;
    }
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  
  pop_off();  //打开中断

  return (void*)r;
}