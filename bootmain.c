// Boot loader.
//
// Part of the boot block, along with bootasm.S, which calls bootmain().
// bootasm.S has put the processor into protected 32-bit mode.
// bootmain() loads an ELF kernel image from the disk starting at
// sector 1 and then jumps to the kernel entry routine.

#include "types.h"
#include "elf.h"
#include "x86.h"
#include "memlayout.h"

#define SECTSIZE  512

void readseg(uchar*, uint, uint);

void
bootmain(void)
{
  struct elfhdr *elf;
  struct proghdr *ph, *eph;
  void (*entry)(void);
  uchar* pa;

  //将ELF文件头指针指向0x10000内存
  //表示0x10000为内核将存放的位置
  elf = (struct elfhdr*)0x10000;  // scratch space

  //从磁盘中读取内核文件的前4096byte至elf指针指向的位置
  // Read 1st page off disk
  readseg((uchar*)elf, 4096, 0);

  //读取失败，返回
  // Is this an ELF executable?
  if(elf->magic != ELF_MAGIC)
    return;  // let bootasm.S handle error

  // Load each program segment (ignores ph flags).
  // 令ph指向elf文件中的proghdr段的起始位置
  ph = (struct proghdr*)((uchar*)elf + elf->phoff);
  // eph为ph段表的尾后节点
  eph = ph + elf->phnum;
  // 将所有程序加载至program header指定的内存位置
  for(; ph < eph; ph++){
    pa = (uchar*)ph->paddr;
    readseg(pa, ph->filesz, ph->off);
    // 内存容量大于文件容量，用0填充剩余部分
    if(ph->memsz > ph->filesz)
      stosb(pa + ph->filesz, 0, ph->memsz - ph->filesz);
  }

  // Call the entry point from the ELF header.
  // Does not return!
  // 找到内核入口地址
  // 跳转至内核
  entry = (void(*)(void))(elf->entry);
  entry();
}

void
waitdisk(void)
{
  // 0x1f7同时可作为状态端口查看硬盘状态
  // Wait for disk ready.
  while((inb(0x1F7) & 0xC0) != 0x40)
    ;
}

//使用LBA28逻辑编址方式从硬盘中读取扇区
//扇区地址为28位
// Read a single sector at offset into dst.
void
readsect(void *dst, uint offset)
{
  // Issue command.
  waitdisk();
  // 0x1f2端口，8位输入，控制读取扇区数目
  outb(0x1F2, 1);   // count = 1
  // 0x1f3端口，输入LBA编址的0~7位
  outb(0x1F3, offset);
  // 0x1f4端口，输入LBA编址的8~15位
  outb(0x1F4, offset >> 8);
  // 0x1f5端口，输入LBA编址的16~23位
  outb(0x1F5, offset >> 16);
  // 0x1f6端口，低四位为输入LBA编址的24~27位，高四位为读取方式
  // 高3位111代表使用LBA，101代表使用CHS，第4位为0代表主硬盘，为1代表从硬盘
  // 这里0xe代表使用LBA编址并从主硬盘读取数据
  outb(0x1F6, (offset >> 24) | 0xE0);
  // 0x1f7端口，硬盘控制端口，0x20为读硬盘指令
  outb(0x1F7, 0x20);  // cmd 0x20 - read sectors

  // Read data.
  waitdisk();
  // 读取一扇区的数据放入dst中
  // insl相当于insd，一双字为单位进行读取，即一次读取32位，故参数cnt为SECTSIZE/4
  insl(0x1F0, dst, SECTSIZE/4);
}

//从kernel中读取count byte大小的文件至pa指针所在的位置，
//偏移值offset为文件在kernel的起始位置
// Read 'count' bytes at 'offset' from kernel into physical address 'pa'.
// Might copy more than asked.
void
readseg(uchar* pa, uint count, uint offset)
{
  uchar* epa;

//epa为终点
  epa = pa + count;

  //调整边界使得pa边界恰好为扇区边界
  // Round down to sector boundary.
  pa -= offset % SECTSIZE;
  // 将以byte为单位的offset转换为以扇区为单位，kernel起始扇区编号为1
  // Translate from bytes to sectors; kernel starts at sector 1.
  offset = (offset / SECTSIZE) + 1;

  // If this is too slow, we could read lots of sectors at a time.
  // We'd write more to memory than asked, but it doesn't matter --
  // we load in increasing order.
  // 每次读取1个扇区
  for(; pa < epa; pa += SECTSIZE, offset++)
    readsect(pa, offset);
}
