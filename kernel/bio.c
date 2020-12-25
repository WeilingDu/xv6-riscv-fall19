// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 13
struct {
  struct buf buf[NBUF];
  struct buf head[NBUCKETS];
  // bcache.lock用于表示当前访问的bcache缓存块的数据结构是否被锁住
  // 当bcache.lock为0时表示未锁住，能够访问当前数据结构bcache
  // 如果为1，即暂时无法获得锁，则不断循环、自旋、等待锁重新可用
  struct spinlock lock[NBUCKETS];  
} bcache;

// 将块编号进行哈希
uint hash(uint blockno)
{
  return (blockno % NBUCKETS);
}

// main.c调用该函数来初始化缓存
void binit(void)
{
  struct buf *b;

  // 调用initlock()初始化bcache.lock
  for (int i = 0; i < NBUCKETS; i++)
    initlock(&bcache.lock[i], "bcache_bucket");

  // 循环遍历buf数组，采用头插法逐个链接到bcache.head后面
  for (int i = 0; i < NBUCKETS; i++)
  {
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }

  // 初始时把所有的buffer放到bucket#0
  for (b = bcache.buf; b < bcache.buf+NBUF; b++)
  {
    b->next = bcache.head[0].next;
    b->prev = &bcache.head[0];
    initsleeplock(&b->lock, "buffer");
    bcache.head[0].next->prev = b;
    bcache.head[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
// bget()检查请求的磁盘块是否在缓存中
static struct buf* bget(uint dev, uint blockno)
{
  struct buf *b;

  uint bucketno = hash(blockno);

  // 操作bcache数据结构（修改refcnt、dev、blockno、valid）时，需要获取到自旋锁 bcache.lock，操作完成后再释放该锁
  acquire(&bcache.lock[bucketno]);

  // Is the block already cached?
  for(b = bcache.head[bucketno].next; b != &bcache.head[bucketno]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[bucketno]);
      // // 在获取到缓存块（命中的缓存块，或者，未命中时通过LRU算法替换出来缓存中的缓存块）后，调用acquiresleep()获取睡眠锁
      acquiresleep(&b->lock); 
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.  
  int i = bucketno;
  while(1)
  {
    i = (i + 1) % NBUCKETS;
    if (i == bucketno)
      continue;
    
    acquire(&bcache.lock[i]);

    for (b = bcache.head[i].prev; b != &bcache.head[i]; b = b->prev)
    {
      if (b->refcnt == 0)
      {
        // move buffer from bucket#i to bucket#bucketno
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        b->prev->next = b->next;
        b->next->prev = b->prev;
        release(&bcache.lock[i]);
        b->prev = &bcache.head[bucketno];
        b->next = bcache.head[bucketno].next;
        b->next->prev = b;
        b->prev->next = b;
        release(&bcache.lock[bucketno]);
        acquiresleep(&b->lock);  
        return b;
      }
    }

    release(&bcache.lock[i]);
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
// 上层文件系统读磁盘时，调用bread()
struct buf* bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);  // 调用bget()检查请求的磁盘块是否在缓存中
  // 如果未命中，调用底层的virtio_disk_rw()函数先将此磁盘块从磁盘加载进缓存中，再返回此磁盘块
  if(!b->valid) {
    virtio_disk_rw(b->dev, b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
// 上层文件写磁盘时，调用bwrite()
void bwrite(struct buf *b)
{
  // 在写入到磁盘之前，先调用holdingsleep()查询是否已经获取到该睡眠锁，确保有带锁而入
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b->dev, b, 1);  // 调用virtio_disk_rw()函数直接将缓存中的数据写入磁盘
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
// 上层文件系统调用brelse()释放一块不再使用的缓存块
void brelse(struct buf *b)
{
  // 调用holdingsleep()查询是否已经获取到该睡眠锁
  // 确保有带锁后，才调用releasesleep()释放该锁
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint bucketno = hash(b->blockno);
  // 获取到自旋锁后，才能将refcnt（引用计数）减1
  acquire(&bcache.lock[bucketno]);
  b->refcnt--;
  // 只有在refcnt为0时，将该数据缓存块插入到bcache.head链表后面
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[bucketno].next;
    b->prev = &bcache.head[bucketno];
    bcache.head[bucketno].next->prev = b;
    bcache.head[bucketno].next = b;
  }
  // 操作完成后再释放该自旋锁
  release(&bcache.lock[bucketno]);
}

void bpin(struct buf *b) {
  uint bucketno = hash(b->blockno);
  // 获取到自旋锁后，才能修改refcnt
  acquire(&bcache.lock[bucketno]);
  b->refcnt++;
  // 操作完成后再释放该自旋锁
  release(&bcache.lock[bucketno]);
}

void bunpin(struct buf *b) {
  uint bucketno = hash(b->blockno);
  // 获取到自旋锁后，才能修改refcnt
  acquire(&bcache.lock[bucketno]);
  b->refcnt--;
  // 操作完成后再释放该自旋锁
  release(&bcache.lock[bucketno]);
}