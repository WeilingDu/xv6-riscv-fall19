struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  // b.lock用于表示bcache缓存块数据结构中的当前缓存数据块buf是否被锁住
  // 当b.lock为1时，则调用sleep()睡眠等待锁重新可用
  // 为0则表示锁已经被释放
  struct sleeplock lock;  
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};
