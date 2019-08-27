# 文件系统

## 文件系统的构成

xv6的文件系统一共分成5层，这五层分别是：

1. buffer层。与其他操作系统类似，xv6采用cache-主存-辅存 三级存储结构。所有接近CPU的操作都应先存放在buffer中。buffer层解决了，存储在buffer中的block，应该如何被读写和保护。

2. log层。每一次buffer和memory之间的数据交换，都会通过log进行。这是为了防止突然断电导致的数据不一致，保证了数据的安全性。

3. file层。包括inode分配，文件读写等一些元操作。

4. directory层。这里会设计一些特殊的inode，用来记录其他的inode以及一些特殊数据。

5. name层。最后我们是通过路径和文件名，来访问各个文件的。file和directory中的一些数据，比如在memory中的存储位置等信息，被封装了起来。

## 涉及的文件
vx6中，涉及文件系统的相关文件罗列如下。

源文件 | 头文件 | 描述
-|-|-
bio.c|buf.h|处理buffer层|
log.c||处理log层|
file.c|file.h|文件在内存中的处理层|
fs.c|fs.h|文件在磁盘中的存放层|
sysfile.c||宏观的文件系统操作层|

## 结构体

### buffer

buffer作为缓冲的基本单元，其结构体的定义如下。

```c
struct buf {
  int flags;
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  struct buf *qnext; // disk queue
  uchar data[BSIZE];
};
```
其中需要注意的几点

1. 这里使用blockno，而没有使用block所处在的位置（即buffer[xxx]）来呈现block的序号。首先这样可以考虑分成不同区域存储的buffer，使得block占用缓冲的方式更加便捷。另外是block在时间上，是可以被随机销毁和生成的，因此block在空间上也不具有连续性。

2. 正因为block在空间上是不连续的，我们采用链表的形式，使得程序可以遍历整个cache。并且为了方便，这里使用了双向链表。在实现上使用了prev和next两个变量。当然正向遍历更普遍，所以next使用的更多一些。

3. 这里设计了一个sleeplock，为的是在一个线程使用buffer的时候，其他线程不可以使用该buffer，对buffer起保护作用。

### log

log负责保护数据，安全的在cache和memory间交换。基本结构如下：

```c
struct log {
  struct spinlock lock;
  int start;
  int size;
  int outstanding; // how many FS sys calls are executing.
  int committing;  // in commit(), please wait.
  int dev;
  struct logheader lh;
};
```

log中的lh成员，全名叫log header。它相当于data block，是存储转移数据的中转站。其结构如下.

```c
struct logheader {
  int n;                                // 有效block的个数
  int block[LOGSIZE];
};
```

### file

file结构体作为文件块在kernel中的呈现形式，其基本结构如下。

```c
struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe;
  struct inode *ip;
  uint off;
};
```

其中需要注意的几点

1. 这里的type需要在以下三个中选择：FD_NONE(普通文件)、FD_PIPE(管道文件)、FD_INODE(索引节点文件)

2. ref指的是引用计数。ref的存在，在于unix系统设计时候，允许多个文件指向同一个inode。所以在删除的时候，并不是删除一个文件，就会在磁盘中删除相应的file，而是等所有的file都删除完成，ref=0的时候，才会清除文件。

### inode

inode结构体的数据，使用在内存中。结构体信息包括设备号，Inode编号，引用计数等。基本结构如下：

```c
struct inode {
  uint dev;           // 设备ID
  uint inum;          // Inode编号
  int ref;            // 引用计数
  struct sleeplock lock; // Inode锁
  int valid;          // inode是否被从磁盘中读入？

  short type;         // 从dinode结构体得到的拷贝
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};
```

事实上，inode结构体在磁盘中，以dinode形式存储。其基本结构如下。

```c
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses
};
```

几件事情需要注意

1. 我们可以看到，相比于inode结构体，dinode结构体的组成更加简洁。这主要是考虑到内存中inode需要存储和多线程引用和访问的问题的。

2. 初始的addrs的大小，设置为NDIRECT。通过查询，我们可以知道NDIRECT=12。按照一个block0.5k来计算，一个文件初始的最大size是6k。如果超过6k，在addrs不扩容的情况下，是无法存储所有数据的。这一点需要注意。

### superblock

superblock超级块，存储总块数，以及各分类型块数等数据。结构如下。

```c
struct superblock {
  uint size;         // 文件系统中一共有多少blocks？
  uint nblocks;      // 数据block的块数
  uint ninodes;      // inode的块数
  uint nlog;         // log block的块数
  uint logstart;     // 第一块logblock的起点
  uint inodestart;   // 第一块inode的起点
  uint bmapstart;    // 第一块block map的起点
};
```

## 函数过程

### binit

这是一个针对bcache的操作函数。然而，更多的时候，这个函数在初始化buffer。为什么要初始化buffer呢？因为buffer的双向链表不是自然联通的。

我们可以看到，函数遍历bcache.buf，来设置其中的next和prev两个属性。

```c
void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
//PAGEBREAK!
  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    // buffer采用的都是休眠锁，可以参考buf.h
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}
```

### bget

这个函数要求输入设备号和block编号，来找到缓冲区中buffer的位置，并返回其指针。

值得玩味的是，这是一个把查询和创建buffer合二为一的函数。其函数逻辑如下：

1. 函数花了前10行去查询满足指定设备号和block编号的buffer。

2. 如果没有找到，则遍历bcache，去找是否有空闲的buffer来存储待查找的buffer.

3. 如果有，则分配和初始化该buffer，并返回buffer

4. 如果没有，返回panic报错。这时类似于stack overflow，需要扩容或停止执行当前任务解决。

```c
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock); 
  // 代表了现在的cache已经被占用。
  // 在此期间，其他所有对cache的请求都讲无法执行。

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);              // 解锁cache
      acquiresleep(&b->lock);             // 但还要锁住找到的buffer
      return b;       // 在目前的cache中已经找到了目标buffer，并返回
    }
  }

  // Not cached; recycle an unused buffer.
  // Even if refcnt==0, B_DIRTY indicates a buffer is in use
  // because log.c has modified it but not yet committed it.
  // 执行到这里，函数已经遍历了一遍buffer，没有找到
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0 && (b->flags & B_DIRTY) == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->flags = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;                         // 找到一个没有用的buffer。替代原有废置的buffer并返回
    }
  }
  panic("bget: no buffers");            // 没有可以用的buffer，panic报错
}
```

### bread和bwrite

bread读一个已经存在于cache中的buffer。函数如下：

```c
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if((b->flags & B_VALID) == 0) {
    iderw(b);
  }
  return b;
}
```

与bread函数对应的bwrite函数，把一个buffer的内容写回磁盘。函数如下：

```c
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  b->flags |= B_DIRTY;
  iderw(b);
}
```

### brelse

brelse针对已经使用完缓冲的情况。这时系统需要释放缓冲。

需要注意的是，释放完缓冲的程序，是不应该再次使用缓冲区的

函数如下：

```c
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}
```

### initlog

初始化log。函数如下。

```c
void
initlog(int dev)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  // 这里申请一个superblock，是为了获取superblock中start, size等信息
  // 参考fs.h，我们知道一共本操作系统中有4种block，分别是superblock, logblock, datablock, mapblock
  struct superblock sb;
  initlock(&log.lock, "log");
  readsb(dev, &sb);
  log.start = sb.logstart;              // 注意这里的start指的是磁盘地址
  log.size = sb.nlog;
  log.dev = dev;
  recover_from_log();
}
```

### install_trans

把log block的信息，提取到log结构体中

```c
static void
install_trans(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *lbuf = bread(log.dev, log.start+tail+1); 
    // read log block起点，是superblock指向的log位置
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); 
    // read dst终点，取lh第tail个block的地址
    memmove(dbuf->data, lbuf->data, BSIZE);  // copy block to dst
    bwrite(dbuf);  // write dst to disk
    brelse(lbuf);
    brelse(dbuf);
  }
}
```

### read_head

把磁盘中的数据读入logheader中

```c
static void
read_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *lh = (struct logheader *) (buf->data);
  int i;
  log.lh.n = lh->n;
  for (i = 0; i < log.lh.n; i++) {
    log.lh.block[i] = lh->block[i];       // 把buffer中的block地址，拷贝给log header
  }
  brelse(buf);
}
```

在把block通过logheader写入磁盘的过程中，这个函数真正在执行写操作

```c
static void
write_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  hb->n = log.lh.n;
  for (i = 0; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];       // 把log header地址，拷贝给buffer中的block
  }
  bwrite(buf);
  brelse(buf);
}
```

### begin_op

开始对log进行读写操作，锁住log，并在outstanding成员上做标记，令其为1

这样如果断电重启，我们可以通过outstanging是否非0来判断，这次的operation是否完成。

```c
void
begin_op(void)
{
  acquire(&log.lock);
  while(1){
    if(log.committing){
      sleep(&log, &log.lock);             // 设置log的锁
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){
      // this op might exhaust log space; wait for commit.
      sleep(&log, &log.lock);
    } else {
      log.outstanding += 1;               // 对outstanding做上锁标记
      release(&log.lock);
      break;
    }
  }
}
```

### end_op和commit

log读写结束，解除锁定。

需要注意的是，只有当outstanding=0的时候，才将其提交。因为此时所有的log任务都已完成

相反的，如果发现当前log正在commiting，也就是说在该log读写结束前outstanding已经为0了，代表现在outstanding--后，其值将小于0。需要panic报错。

而如果其他log还处在挂起状态，则可以唤醒他们执行。

```c
void
end_op(void)
{
  int do_commit = 0;

  acquire(&log.lock);
  log.outstanding -= 1;
  if(log.committing)
    panic("log.committing");
  if(log.outstanding == 0){
    do_commit = 1;
    log.committing = 1;
  } else {
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    // 还有其他的begin_op()在排队等待执行
    wakeup(&log);
  }
  release(&log.lock);

  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    // 现在outstanding=0, 可以提交了
    commit();
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }
}
```

commit函数是一个看起来非常简洁的函数，但其调用了其他功能复杂的函数，完成了commit功能。其函数如下。

```c
static void
commit()
{
  if (log.lh.n > 0) {
    write_log();     // 将修改后的块从缓存写入日志
    write_head();    // 将标头写入磁盘 (这里是真正的提交)
    install_trans(); // 调用install_trans函数
    log.lh.n = 0;
    write_head();    // 从日志中删除事务
  }
}
```

### write_log

把buffer从cache向log转移的函数。

```c
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *to = bread(log.dev, log.start+tail+1);      // 终点是log block
    struct buf *from = bread(log.dev, log.lh.block[tail]);  // 起点是cache block
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // write the log
    brelse(from);
    brelse(to);
  }
}
```

### log_write

乍看名字，你会感觉log_write和write_log两个函数一模一样。但事实上，他们在执行不同的功能。

我们可以从两个函数的输入和输出来分析

两个函数的输出都是void。区别的是log_write的输入是一个struct buf类型的数据，而write_log的输入是void。

这表明，log_write是在buffer层级上与log互动的函数。官方给出的注释告诉我们，log_write函数可以代替bwrite函数，其基本用法是：

* bp = bread(...)

* *modify bp->data[]

* log_write(bp)

* brelse(bp)

```c
void
log_write(struct buf *b)
{
  int i;

  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  acquire(&log.lock);
  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // buffer的数据读入log header
      break;
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n)
    log.lh.n++;
  b->flags |= B_DIRTY; // prevent eviction
  release(&log.lock);
}
```

