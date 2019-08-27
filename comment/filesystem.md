# 文件系统

## 文件系统的构成

xv6的文件系统一共分成5层，这五层分别是：

1. buffer层。与其他操作系统类似，vx6采用cache-主存-辅存 三级存储结构。所有接近CPU的操作都应先存放在buffer中。buffer层解决了，存储在buffer中的block，应该如何被读写和保护。

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

4. data的大小BSIZE可以在fs.h中看到，默认为512b。换言之整个buffer结构体的大小不止512b，这点需要注意。

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

2. ref指的是引用计数。ref的存在，在于unix系统设计时候，允许多个文件指向同一个inode。