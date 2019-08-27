# 内核初始化

## 涉及文件

* entry.S
* main.c



## 流程

### entry.S

1. 扩展页大小至4MB，使得一页能容纳整个内核程序
2. 设置页表目录项地址至CR3寄存器
3. 启动分页机制
4. 分配栈空间
5. 跳转至main.c中的main函数，使用c语言完成剩下启动工作

### main.C

``` c
int main(void)
{
  //内存初始化，设备初始化，子系统初始化
  kinit1(end, P2V(4*1024*1024)); // phys page allocator 分配物理页面
  kvmalloc();      // kernel page table 内核页表分配
  mpinit();        // detect other processors 检测其他处理器
  lapicinit();     // interrupt controller initialization 中断控制器初始化
  seginit();       // segment descriptors initialization 段描述符号初始化
  picinit();       // disable pic initialization 停用图片
  ioapicinit();    // another interrupt controller initialization 另一个中断控制器初始化
  consoleinit();   // console hardware initialization 控制台硬件初始化
  uartinit();      // serial port initialization 串行端口初始化
  pinit();         // process table initialization 进程表初始化
  tvinit();        // trap vectors initialization 中断向量初始化
  binit();         // buffer cache initialization 缓冲区缓存初始化
  fileinit();      // file table initialization 文件表初始化
  ideinit();       // disk initialization 磁盘初始化
  startothers();   // start other processors 启动其他处理器
  kinit2(P2V(4*1024*1024), P2V(PHYSTOP)); // must come after startothers()
  //创建第一个进程（所有进程的父进程），完成CPU设置并调度进程
  userinit();      //完成初始化后,调用userinit()创建第一个进程
  mpmain();        // finish this processor's setup 完成此处理器的设置，开始调度进程
}
```

如上述代码所示，main.c通过调用其他组件提供的初始化API完成整个内核的初始化工作