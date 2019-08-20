# xv6管道



## 概念

<u>**管道**</u>是一个小的内核缓冲区，它以**<u>文件描述符对</u>**的形式提供给进程，一个用于写操作，一个用于读操作。从管道的一端写的数据可以从管道的另一端读取。管道提供了一种进程间交互的方式。

<u>**文件描述符**</u>是一个整数，它代表了一个进程可以读写的被内核管理的对象。进程可以通过多种方式获得一个文件描述符，如打开文件、目录、设备，或者创建一个管道（pipe），或者复制已经存在的文件描述符。简单起见，我们常常把文件描述符指向的对象称为“文件”。文件描述符的接口是对文件、管道、设备等的抽象，这种抽象使得它们看上去就是字节流。



## pipe实现概述

- Pipe的主要部分实际上是一小段规定长度的连续数据存储，读写操作将其视为无限循环长度的内存块。

- 初始化时，将给定的文件输入、输出流与该结构体关联。

- 关闭时，释放内存，解除文件占用。

- 读写操作时，则分别需要判断是否超出读写的范围，避免覆盖未读数据或者读取已读数据；如果写操作未执行完，则需通过睡眠唤醒的方式来完成大段数据的读取。

## 结构

```c
struct pipe {
  struct spinlock lock;//spinlock的作用相当与当进程请求得到一个正在被占用的锁时，将进程处于循环检查，等待锁被释放的状态
  char data[PIPESIZE];//保存pipe的内容，PIPESIZE为512
  uint nread;     // 读取的byte的长度
  uint nwrite;    // 写入的byte的长度
  int readopen;   // 是否正在读取
  int writeopen;  // 是否正在写入
};
```



## 函数

- pipealloc

  该函数实现了pipe的创建，并将pipe关联到两个文件f0, f1上。如果创建成功，返回0，否则返回-1

  ```c
  int
  pipealloc(struct file **f0, struct file **f1)
  {
    struct pipe *p;
  
    p = 0;
    *f0 = *f1 = 0;
      //如果f0, f1不存在则返回-1
    if((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
      goto bad;
    if((p = (struct pipe*)kalloc()) == 0)
      goto bad;
      //初始化
    p->readopen = 1;
    p->writeopen = 1;
    p->nwrite = 0;
    p->nread = 0;
    initlock(&p->lock, "pipe");
    (*f0)->type = FD_PIPE;
    (*f0)->readable = 1;
    (*f0)->writable = 0;
    (*f0)->pipe = p;
    (*f1)->type = FD_PIPE;
    (*f1)->readable = 0;
    (*f1)->writable = 1;
    (*f1)->pipe = p;
    return 0;
  
      //如果创建失败则将进度回滚，释放占用的内存，解除对文件的占用
  //PAGEBREAK: 20
   bad:
    if(p)
      kfree((char*)p);
    if(*f0)
      fileclose(*f0);
    if(*f1)
      fileclose(*f1);
    return -1;
  }
  ```

  



- pipeclose

  实现了关闭pipe的处理

  ```c
  void
  pipeclose(struct pipe *p, int writable)
  {
      //获取管道锁，避免在关闭的同时进行读写操作
    acquire(&p->lock);
      //判断是否有未被读取的数据
    if(writable){
        //如果存在，则唤醒pipe的读进程
      p->writeopen = 0;
      wakeup(&p->nread);
    } else {
        //不存在就唤醒pipe的写进程
      p->readopen = 0;
      wakeup(&p->nwrite);
    }
    if(p->readopen == 0 && p->writeopen == 0){
      release(&p->lock);
      kfree((char*)p);
    } else
      release(&p->lock);
  }
  ```





- pipewrite

  实现了管道的写操作

  ```c
  int
  pipewrite(struct pipe *p, char *addr, int n)
  {
    int i;
  
    acquire(&p->lock);
      //逐字节写入
    for(i = 0; i < n; i++){
        //如果pipe已经写满
      while(p->nwrite == p->nread + PIPESIZE){  //DOC: pipewrite-full
        if(p->readopen == 0 || myproc()->killed){
            //唤醒读进程，写进程进入睡眠，并返回-1
          release(&p->lock);
          return -1;
        }
        wakeup(&p->nread);
        sleep(&p->nwrite, &p->lock);  //DOC: pipewrite-sleep
      }
      p->data[p->nwrite++ % PIPESIZE] = addr[i];
    }
      //写完之后唤醒读进程
    wakeup(&p->nread);  //DOC: pipewrite-wakeup1
    release(&p->lock);
    return n;
  }
  ```

  



- piperead

  piperead实现了pipe的读操作

  ```c
  int
  piperead(struct pipe *p, char *addr, int n)
  {
    int i;
  
    acquire(&p->lock);
      //如果pipe已经读空，并且正在写入，则进入睡眠状态
    while(p->nread == p->nwrite && p->writeopen){  //DOC: pipe-empty
      if(myproc()->killed){
        release(&p->lock);
        return -1;
      }
      sleep(&p->nread, &p->lock); //DOC: piperead-sleep
    }
    for(i = 0; i < n; i++){  //DOC: piperead-copy
      if(p->nread == p->nwrite)
        break;
      addr[i] = p->data[p->nread++ % PIPESIZE];
    }
      //读取完毕，唤醒写进程
    wakeup(&p->nwrite);  //DOC: piperead-wakeup
    release(&p->lock);
      //返回读取的字节长度
    return i;
  }
  ```

  