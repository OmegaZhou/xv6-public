# 内存分配器

## 功能

* 对空闲内存进行管理
* 封装了内存分配和释放操作，用户只需使用虚拟内存即可完成对物理内存的分配

## 实现思想

* 使用结构体

  ``` c
  // 可使用内存链表节点
  // 同时也可作为分配内存的首地址
  // 未使用时储存下一内存链表节点地址
  struct run {
    struct run *next;
  };
  ```

  作为内存分配的基本单元

* 使用结构体

  ```c
  // 管理空闲内存的结构
  struct {
    struct spinlock lock;
    int use_lock;
    struct run *freelist;
  } kmem;
  ```

  作为管理内存的结构

  * 该结构维护一个空闲内存链表

  * 同时提供一个自旋锁保护空闲链表
  * 并且提供是否启用自旋锁的选项

## 提供API

* kinit1函数
  * 函数原型 <pre> void kinit1(void *vstart, void *vend)</pre>
  * 在未启动其他CPU核心时使用
  * 释放部分内存供初始化使用

* kinit2函数

  * 函数原型 <pre> void kinit2(void *vstart, void *vend)</pre>
  * 多核启动结束后以及加载完完整页表后调用
  * 释放剩余内存
  * 由于此时已启用多CPU核心，故启用自旋锁维护空闲内存链表

* kfree函数

  * 函数原型 <pre> void kfree(char *v)</pre>

  * 释放4kb内存

  * 函数解析

    ``` c
    //PAGEBREAK: 21
    // Free the page of physical memory pointed at by v,
    // which normally should have been returned by a
    // call to kalloc().  (The exception is when
    // initializing the allocator; see kinit above.)
    // 释放4kb内存
    void
    kfree(char *v)
    {
      struct run *r;
      // 判断释放内存地址是否合法，end为内核尾后虚拟地址，PHYSTOP为最大物理地址
      if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
        panic("kfree");
    
      // Fill with junk to catch dangling refs.
      // 清空内存内容
      memset(v, 1, PGSIZE);
    
      // 若使用自旋锁维护空闲内存列表
      // 在访问kmem需要完成申请自旋锁
      // 结束访问后释放
      if(kmem.use_lock)
        acquire(&kmem.lock);
      r = (struct run*)v;
      // 将释放的内存添加到列表头部
      r->next = kmem.freelist;
      kmem.freelist = r;
      if(kmem.use_lock)
        release(&kmem.lock);
    }
    ```

* kalloc函数

  * 函数原型 <pre> char* kalloc(void) </pre>

  * 分配4kb内存

  * 返回分配内存的首地址

  * 函数解析

    ``` c
    // Allocate one 4096-byte page of physical memory.
    // Returns a pointer that the kernel can use.
    // Returns 0 if the memory cannot be allocated.
    char*
    kalloc(void)
    {
      struct run *r;
    
      if(kmem.use_lock)
        acquire(&kmem.lock);
      // 从可使用列表中分配内存并返回内存首地址
      // 若无可使用内存，则返回0
      r = kmem.freelist;
      if(r)
        kmem.freelist = r->next;
      if(kmem.use_lock)
        release(&kmem.lock);
      return (char*)r;
    }
    ```

    