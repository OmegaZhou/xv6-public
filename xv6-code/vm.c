#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
// 初始化GDTR寄存器当每个cpu核心启动时
// 将当前cpu的GDT表设置为内核态的GDT表
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
// 返回页表项所在地址
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  // 页表项属于第几个页目录项
  pde = &pgdir[PDX(va)];
  // 若当前页目录项有效，直接得到对应页表项地址
  if(*pde & PTE_P){
    // 页表项起始位置对应的物理内存的虚拟地址
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    // 如果alloc=1，为页表项分配内存
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    // 初始化内存
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    // 完成页目录项初始化
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
// 为具有一段长度的内存创建页表项
// 参数说明
// pgdir:页表项地址
// va:虚拟地址
// size:内存大小
// pa:映射的起始物理内存
// perm:设置页表项的属性
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  // 完成4k对齐，舍去虚拟地址中的低位值
  a = (char*)PGROUNDDOWN((uint)va);
  // 同样对最末一项完成4k对齐
  // last为最后一项的起始地址
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  // 使用向下对齐完成4k对齐的原因：
  //      分配充足的内存避免内存不足
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    //配置页表项属性及对应物理地址，这里使用4kb页表，故没有将PS位置1
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  // 分配4kb内存存放页表目录项
  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  // 初始化内存
  memset(pgdir, 0, PGSIZE);
  // 如果物理内存上限范围越过了其他设备地址范围，抛出panic异常
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  // 完成对每一段内存在页表中的映射转换
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
// 创建并加载完整页表
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  // 加载页表
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  // 判断该进程指针是否合法
  if(p == 0)
    panic("switchuvm: no process");
  // 判断栈指针是否合法
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  // 判断页表目录入口地址是否设置
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");
  // 关闭中断
  pushcli();
  // 为该进程建立任务状态段的段描述符
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  // 将该段的s位设置为系统位
  mycpu()->gdt[SEG_TSS].s = 0;
  // 设置cpu目前的任务状态值
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  // 加载段描述符
  ltr(SEG_TSS << 3);
  // 加载进程的页表
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
// 将init代码移动到页面中执行
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  // 分配一页物理地址
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  // 创建虚拟地址0到分配地址mem的映射
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  // 将init代码移动至mem所指向的内存中
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
// 加载程序段进内存中
// 参数说明
// pgdir：页表目录项入口地址
// addr：加载的目标虚拟地址，需要4k对齐
// ip；待加载文件的inode
// offset：读取文件的起始位置偏移值
// sz：读取的大小
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  // 创建页表项
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    // 获取页表项指向的物理地址
    pa = PTE_ADDR(*pte);
    // 剩余数据大小未满PGSIZE，就读取剩余的数据
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    // 从磁盘中读取文件数据存放至物理地址pa处
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
// 为用户进程分配更多内存，将进程内存从oldsz增加至newsz
// 该函数基于以下假定：
// 1. 进程内存虚拟地址从0开始编址
// 2. 进程内存呈线性编址
// 3. 进程使用内存大小等同与进程使用的内存的最高地址
// 参数说明：
// pdgdir：用户进程的页表目录入口地址
// oldsz：进程原大小
// newsz：新的进程大小
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  // 对进程新大小合法性进行检测
  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  // 由于内存是以4kb为单位分配的
  // 故若oldsz不是4k对齐的，则必定多分配了内存
  // 故向上4k对齐
  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    // 分配新的4k内存
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    // 初始化内存
    memset(mem, 0, PGSIZE);
    // 为页表目录项建立虚拟内存到新分配的内存间的映射关系
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  // 返回新的进程大小以便判断内存是否分配成功
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
// 释放进程内存，将进程内存从oldsz降至newsz，allocuvm的反操作
// 该函数基于以下假定：
// 1. 进程内存虚拟地址从0开始编址
// 2. 进程内存呈线性编址
// 3. 进程使用内存大小等同与进程使用的内存的最高地址
// 参数说明：
// pdgir：进程页表目录入口地址
// oldsz：进程原大小
// newsz：新的进程大小
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  // 由于释放内存也是以4k为单位的
  // 若向下对齐，会将还需要的内存释放
  // 向上4k对齐
  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    // 获取待释放的页表项地址
    pte = walkpgdir(pgdir, (char*)a, 0);
    // 返回0代表当前页表目录项对应的页表不存在
    // 故直接跳到下一条目录项
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    // 若该页表项处于使用状态，将其对应的物理内存释放
    // 并将该页表项置空
    else if((*pte & PTE_P) != 0){
      // 获取页表项对应的物理地址
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      // 将物理地址转换为虚拟地址并释放
      char *v = P2V(pa);
      kfree(v);
      // 清空页表项
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
// 释放整个页表以及分配的所有物理内存
// 参数说明：
// pgdir：待释放的页表目录入口地址
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  // 释放页表分配的所有内存
  // 由于在函数内有对内存是否分配进行检测，保证内存不会重复释放
  // 故deallocuvm的oldsz可直接为虚拟地址最大值
  deallocuvm(pgdir, KERNBASE, 0);
  // 释放所有页表项占据的内存
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      // 获取页表项的物理地址
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  // 释放页表目录的物理地址
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
// 将虚拟地址uva对应的页表项的U位置0
// 即让该页表仅运行特权级为0，1，2的用户访问
// 用于创建不可被用户进程访问的页
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;
  // 获取页表项
  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  // 将PTE_U位置0
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
// 根据给定进程页表复制进程所有内容
// 用于父进程创建子进程使用
// 参数说明：
// pgdir：父进程页表
// sz：进程大小
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;
  
  // 创建新的页表目录项
  // 此时页表目录映射关系为内核的映射关系
  // 之后重新建立页表的映射关系
  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    // 获取父进程页表项地址
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    // 获取父进程页表项对应物理地址
    pa = PTE_ADDR(*pte);
    // 获取父进程页表项属性信息
    flags = PTE_FLAGS(*pte);
    // 为子进程分配内存
    if((mem = kalloc()) == 0)
      goto bad;
    // 将父进程页表项对应的内存内的数据复制到子进程的对应内存中
    memmove(mem, (char*)P2V(pa), PGSIZE);
    // 建立子进程的内存映射关系
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
  }
  return d;
// 内存分配错误，释放子进程已分配内存
bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
// 将用户进程的虚拟地址转化为内核虚拟地址
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
// 从虚拟地址p中拷贝len个字节至用户进程的va地址中
// 参数说明：
// pgdir：用户进程页表
// va：用户进程虚拟地址
// p：待复制数据的虚拟地址
// len：复制字节长度
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    // 使用uva2ka函数保证若va地址运行在内核态，返回0
    // 从而避免向内核拷贝内容，破坏内核
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    // 计算要拷贝的字节长度
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    
    // va - va0为拷贝内存地址在该页中的偏移量
    // pa0为该页的物理地址
    // 通过memmove函数从buf（p）指针处拷贝n字节数据到va对应的内核虚拟内存上
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

