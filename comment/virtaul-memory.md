# 虚拟内存管理

## 涉及文件

- vm.c

## 页表

* 操作系统通过页表机制实现了对内存空间的控制。页表使得 xv6 能够让不同进程各自的地址空间映射到相同的物理内存上，还能够为不同进程的内存提供保护。 除此之外，我们还能够通过使用页表来间接地实现一些特殊功能。xv6 主要利用页表来区分多个地址空间，保护内存。另外，它也使用了一些简单的技巧，即把不同地址空间的多段内存映射到同一段物理内存（内核部分），在同一地址空间中多次映射同一段物理内存（用户部分的每一页都会映射到内核部分），以及通过一个没有映射的页保护用户栈。

* xv6使用x86架构提供的分页硬件来完成虚拟地址到物理地址转换

* 涉及寄存器

  * CR3寄存器
    该寄存器存储页表目录项的地址以及一些配置信息，且由于CR3寄存器只使用高20位来存放地址，故页表目录入口地址必须4K对齐
  * CR4寄存器
    将该寄存器的PSE位置1，可运行硬件使用超级页

* x86架构的页表结构

  * x86架构使用两级页表

    * 第一级为页表目录项，其指向存放页表项的物理地址，长度为4Kb，一个页表项占4byte，故一个页表目录项指向的内存可存放1024个页表项
    * 页表目录项同样存放在4Kb的内存中，一个页表目录项占4byte，共由1024个页表项，故32位x86计算机支持最大内存为1024\*1024\*4Kb=4Gb内存

  * x86硬件寻址方式

    ![](img\page-hardware.png)

    如图，对于一个32位的虚拟地址

    1. 分页硬件从CR3寄存器存储的页表目录入口地址找到页表目录，并根据虚拟地址高10位找到在页表目录中对应的页表目录项
    2. 分页硬件再根据虚拟地址中10位找到页表项，得到对应页的物理地址
    3. 最后根据虚拟地址低12位的偏移量确定具体内存地址

* 页表项格式
  ![page_table_format](img\page-table-format.PNG)
  如图为各种情况下页表项的格式

## xv6内存映射模型

### 定义常量及含义

* KERNBASE 0x80000000 代表此地址以上的虚拟地址由内核使用
* EXTMEM    0x100000      扩展内存的起始地址
* DEVSPACE 0xFE000000  其他设备使用的高位地址
* PHYSTOP   0xE000000    假定物理内存的大小
* data      在链接脚本中指定   内核数据段起始地址

### 内存映射

| 虚拟地址范围             | 映射的物理地址               | 功能                       |
| :----------------------- | ---------------------------- | -------------------------- |
| 0~KERNBASE               | 具体映射的物理内存由内核分配 | 供用户进程使用             |
| KERNBASE~KERNBASE+EXTMEM | 0~EXTMEM                     | 供IO设备使用               |
| KERNBASE+EXTMEM~data     | EXTMEM~V2P(data)             | 存放内核代码段及只读数据段 |
| data~KERNBASE+PHYSTOP    | V2P(data)~PHYSTOP            | 内核数据段及其他未分配内存 |
| 0xfe000000~0             | 直接映射                     | 供其他设备使用，如ioapic   |

## xv6内存管理机制

* xv6使用段页式存储机制进行内存管理
* 内核具有所有内存的权限，并对空闲内存进行分配
* 每一个进程拥有对应的页表，当切换到某一进程时，为该进程设置段描述符，并加载该进程使用的页表
* xv6使用kalloc和kfree函数完成物理内存分配，上层内存管理接口只需关注虚拟内存地址
* 内核根据用户进程的页表调用kalloc和kfree函数来完成内存分配和释放工作
* 每个用户进程的内存都是从0开始线性编址的
* KERNBASE以上的虚拟地址为内核所独有
* xv6内存管理系统还提供了loaduvm接口从磁盘文件加载数据至内存，作为文件系统与内存管理系统的桥梁
* xv6还提供了copyuvm接口，用于复制一个新的页表并分配布局和旧的完全一样的新内存，用于实现fork为父进程创建子进程

## 使用结构体

* 内核内存映射

  ``` c
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
  ```

  存放内核使用的虚拟内存到物理内存的映射方式

* 用户进程

  ``` c
  struct proc {
    uint sz;                     // Size of process memory (bytes)
    pde_t* pgdir;                // Page table
    char *kstack;                // Bottom of kernel stack for this process
    enum procstate state;        // Process state
    int pid;                     // Process ID
    struct proc *parent;         // Parent process
    struct trapframe *tf;        // Trap frame for current syscall
    struct context *context;     // swtch() here to run process
    void *chan;                  // If non-zero, sleeping on chan
    int killed;                  // If non-zero, have been killed
    struct file *ofile[NOFILE];  // Open files
    struct inode *cwd;           // Current directory
    char name[16];               // Process name (debugging)
  };
  ```

  存放用户进程信息

## 提供API

### 初始化API

* API列表

  ``` c
  void            seginit(void);
  void            kvmalloc(void);
  void            inituvm(pde_t*, char*, uint);
  ```

* seginit函数

  * 每个cpu核心启动时调用
  * 为每个cpu设置内核态的GDT表并加载它

* kvmalooc函数

  * 调用setupkvm为内核建立页表，将内核页表目录入口地址存至kpgdir
  * 调用switchkvm加载内核页表

* inituvm函数

  * 为第一个用户进程创建页表
  * 先为该进程分配内存并建立内存映射
  * 再将该进程代码赋值到分配的内存处

### 内存管理API

* API列表

  ``` c
  pde_t*          setupkvm(void);
  char*           uva2ka(pde_t*, char*);
  int             allocuvm(pde_t*, uint, uint);
  int             deallocuvm(pde_t*, uint, uint);
  void            freevm(pde_t*);
  int             loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz);
  pde_t*          copyuvm(pde_t*, uint);
  void            switchuvm(struct proc *p)
  void            switchkvm(void);
  int             copyout(pde_t*, uint, void*, uint);
  void            clearpteu(pde_t *pgdir, char *uva);
  ```

* setupkvm函数

  * 设置内核页表并返回内核页表目录入口地址
  * 通过调用mappages完成kmap内的所有内核映射关系

* uva2ka函数

  * 将用户进程的虚拟地址转换为内核虚拟地址
  * 物理地址到对应的内核虚拟地址可通过P2V宏来实现
  * 物理地址可通过用户进程的页表来得到
  * 从而实现将用户进程的虚拟地址到内核虚拟地址的转换

* allocuvm函数

  * 进程需求内存变多时为其分配内存

  * 函数解析

    ``` c
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
    ```

* deallocuvm函数

  * 进程释放部分内存时调用此函数，属于allocuvm的相反操作

  * 函数解析

    ``` c
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
    ```

* freevm函数

  * 释放整个页表以及分配的所有物理内存

  * 函数解析

    ``` c
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
    ```

* loaduvm函数

  * 将文件程序段装载至虚拟地址为addr的内存中

  * 函数解析

    ``` c
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
    ```

* copyuvm函数

  * 根据给定进程页表复制进程所有内容

  * 用于父进程创建子进程使用

  * 函数解析

    ``` c
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
    ```

    

* switchuvm函数

  * 切换用户进程时调用

  * 切换cpu的任务状态段

  * 切换至进程p使用的页表，向cr3寄存器加载储存进程页表目录入口地址

  * 页表入口地址储存在p->pgdir

  * 函数解析

    ``` c
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
    ```

    

* switchkvm函数

  * 切换至内核页表，向cr3寄存器加载储存内核页表目录入口地址的指针kpgdir

* copyout函数

  * 从虚拟地址p中拷贝len个字节至用户进程的va地址中

  * 函数解析

    ``` c
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
    ```

    

* clearpteu函数

  * 将虚拟地址uva对应的页表项的U位置0

  * 即让该页表仅运行特权级为0，1，2的用户访问

  * 用于创建不可被用户进程访问的页

## 辅助函数

* 函数列表

  ``` c
  static pte_t *
  walkpgdir(pde_t *pgdir, const void *va, int alloc);
  static int
  mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);
  ```

* walkpgdir函数

  * 返回虚拟地址所对应的页表项地址

  * 当alloc=1时，若虚拟地址对应的页表不存在，创建该页表项并分配内存

  * 函数解析

    ``` c
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
    ```

* mappages函数

  * 为页表创建某一段虚拟内存到物理内存的映射

  * 函数解析

    ``` c
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
    ```

