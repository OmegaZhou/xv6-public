// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

// 可使用内存链表节点
// 同时也可作为分配内存的首地址
// 未使用时储存下一内存链表节点地址
struct run {
  struct run *next;
};

// 管理空闲内存的结构
struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
// 此时未启动其他核心，不需要使用锁，部分供初始化使用
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}
// 多核启动结束，释放剩余内存使用，此时需要使用自旋锁来维护
void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

// 释放从vstart到vend之间的内存
void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// 释放4kb（一页）内存
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

