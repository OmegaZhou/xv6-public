# Boot

## 涉及文件

* bootasm.S
* bootmain.c
* asm.h
* mmu.h
* xv6.h

## Boot程序流程

### bootasm.S:完成保护模式的开启

1. 关闭中断
2. 为段寄存器赋初值
3. 通过键盘端口启用A20地址线，让内存突破1Mb的限制
4. 加载GDT段描述符
5. 开启保护模式
6. 使用跳转指令让cs寄存器加载段选择子，并根据段选择子选择GDT段描述符，完成保护模式的开启
7. 为其他段寄存器赋值
8. 设置esp寄存器的值，跳转至c程序bootmain

### bootmain.c:完成内核的加载并跳转至内核

1. 读取内核文件的ELF头部到指定位置
2. 获取ELF文件的程序段表起始位置即程序段数目
3. 根据程序头表加载所有内核文件至内存中
4. 根据ELF文件头获取内核入口地址
5. 跳转至内核入口，结束引导程序，进入内核程序

## 总结

* 引导程序位于硬盘的第一个扇区。
* 在计算机启动时，BIOS读取引导分区的第一个扇区，将其加载进内存0x7c00处，将控制器转交给引导程序
* 引导程序完成加载内核前的一系列引导工作，如开启保护模式，将内核加载进内存中等
* 若一个扇区512byte的容量不足以让引导程序完成这些工作，可在硬盘中再增加个程序，令引导程序先加载该程序，再由该程序完成剩余工作
* 引导程序完成工作后，跳转进内核程序的入口处，将计算机控制权交予内核程序

## 附录

### 其他A20地址线开启方式

* 键盘控制器 
  Keyboard Controller:
  This is the most common method of enabling A20 Gate.The keyboard micro-controller provides functions for disabling and enabling A20.Before enabling A20 we need to disable interrupts to prevent our kernel from getting messed up.The port 0x64 is used to send the command byte.

  **Command Bytes and ports**
  0xDD Enable A20 Address Line
  0xDF Disable A20 Address Line 

  0x64  Port of the 8042 micro-controller for sending commands

  **Using  the keyboard to enable A20:**

  ```assembly
  EnableA20_KB:
  cli                ;Disables interrupts
  push	ax         ;Saves AX
  mov	al, 0xdd  ;Look at the command list 
  out	0x64, al   ;Command Register 
  pop	ax          ;Restore's AX
  sti                ;Enables interrupts
  ret   
  ```

* 中断处理函数  **Using the BIOS functions to enable the A20 Gate:**
  The INT 15 2400,2401,2402 are used to disable,enable,return status of the A20 Gate respectively.

  Return status of the commands 2400 and 2401(Disabling,Enabling)
  CF = clear if success
  AH = 0
  CF = set on error
  AH = status (01=keyboard controller is in secure mode, 0x86=function not supported)

   Return Status of the command 2402
  CF = clear if success
  AH = status (01: keyboard controller is in secure mode; 0x86: function not supported)
  AL = current state (00: disabled, 01: enabled)
  CX = set to 0xffff is keyboard controller is no ready in 0xc000 read attempts
  CF = set on error 

  ``` assembly
  Disabling_A20:
  push ax
  mov ax, 0x2400 
  int 0x15 
  pop ax
  
  Enabling_A20:
  push ax
  mov ax, 0x2401 
  int 0x15 
  pop ax
  
  Checking_A20:
  push ax
  push cx
  mov ax, 0x2402 
  int 0x15 
  pop cx
  pop ax
  ```

  

* 系统端口
  **Using System Port 0x92**
  This method is quite dangerous because it may cause conflicts with some hardware devices forcing the system to halt.
  **Port 0x92 Bits**

  - **Bit 0** - Setting to 1 causes a fast reset 
  - **Bit 1** - 0: disable A20, 1: enable A20
  - **Bit 2** - Manufacturer defined
  - **Bit 3** - power on password bytes. 0: accessible, 1: inaccessible
  - **Bits 4-5** - Manufacturer defined
  - **Bits 6-7** - 00: HDD activity LED off, 01 or any value is "on"

  ``` assembly
  enable_A20_through_0x92:
  push ax
  mov	al, 2 out	0x92, al pop ax
  ```

  

### 32位GDT段描述符格式

* 每个描述符占8byte

  ``` c
  // The 0xC0 means the limit is in 4096-byte units
  // and (for executable segments) 32-bit mode.
  // 32位段描述符格式：
  // 0~15位 段界限的0至15位，即(((lim) >> 12) & 0xffff)
  // 16~31位 段基址的0至15位，即((base) & 0xffff)
  // 32~39位 段基址的16至23位，即(((base) >> 16) & 0xff)
  // 40~43位 描述符的子类型，共4bit
  // 43位为X位，代表该段是否可执行，0x8代表可执行即为代码段，反之为数据段
  // 42位，对于代码段来说为C位，代表是否为特权级依从，0x4代表是依从的
  //        对于数据段来说为E位，代表扩展方向，0x4代表向下扩展，即向低地址方向扩展
  // 41位，对于代码段来说为R位，代表是否可读，0x2代表可读，代码段皆不可写
  //        对于数据段来说为W位，代表是否可写，0x2代表可写，数据段皆可读
  // 40位为A位，代表是否访问过，0x1代表已访问
  // 44~47位，在SEG_ASM中即为0x9
  // 47位为P位，1代表描述符所代表的段处在内存中
  // 45~46位为DPL，代表特权级0，1，2，3，0为最高，这里作为boot程序，特权级为最高级0
  // 44位为S位，0为系统段，1为代码段或数据段，这里为数据段或代码段，故设为1
  // 48~51位 段界限16~19位，即(((lim) >> 28) & 0xf)
  // 52~55位 在SEG_ASM中为0xc
  // 55位为G位，代表段界限的基本单位，0代表已字节为单位，1代表已4KB为单位，
  //           这里设为1，故在计算段界限时，都要将参数右移12位
  // 54位为D/B位，代表默认操作数大小或默认栈指针大小或上部边界的标记
  //             对于代码段称为D位，0代表操作数和偏移量都是16位的，1代表32位的操作数和偏移量
  //             对于数据段称为B位，0代表使用pop，push，call等操作时，使用sp寄存器，1则使用esp寄存器
  //             在这里设为1
  // 53位为L位，预留给64位处理器，这里设置为0
  // 52位为AVL，供操作系统使用，这里直接设为0
  // 故52~55位为1100即0xc
  // 56~63位为段基址的24~31位，即(((base) >> 24) & 0xff)
  ```

  

### 32位段选择子格式

``` assembly
  # 段选择子格式：
  # 段选择子共16位
  # 高13位为选择子编号
  # 第0~1位为PRL，代表权限级别
  # 第2位为TI位，0代表查找GDT，1代表查找LDT
```



### 通过IO端口读取硬盘数据

* 样例程序

  ``` c
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
  ```



### Boot部分所需ELF文件头的数据解析

* ELF文件头格式解析

  ``` c
  //ELF 文件头格式
  // File header
  struct elfhdr {
    uint magic;  // must equal ELF_MAGIC
    uchar elf[12];
    ushort type;
    ushort machine;
    uint version;
    uint entry;  //程序入口地址
    uint phoff;  //第一个程序段头部在文件中的偏移位置
    uint shoff;
    uint flags;
    ushort ehsize;
    ushort phentsize;
    ushort phnum; //程序段数目
    ushort shentsize;
    ushort shnum;
    ushort shstrndx;
  };
  ```

  

* 程序段头部格式解析

  ``` c
  // Program section header
  struct proghdr {
    uint type;
    uint off;    // 程序段在文件中的偏移量
    uint vaddr;  
    uint paddr;  // 程序段载入内核的内存物理地址
    uint filesz; // 程序段在文件中的大小
    uint memsz;  // 程序段在内存中的大小
    uint flags;
    uint align;
  };
  ```


### 关于内核ELF文件内存地址的一些总结

* 内核程序各代码地址由链接脚本指定，使用重定向符号0x80100000，即每条语句的地址为偏移量+0x80100000
* 在链接脚本中指定了入口地址为_start，程序实际入口为entry
* 引导程序将内核载入物理地址0x100000
* _start=V2P_WO(entry)，宏V2P_WO作用是将输入参数减去0x80000000，在分页机制启动前完成内存地址的转换
* 故内核程序的入口地址并非生成的虚拟地址，而是物理地址，从而实现了在还未开启分页功能时引导程序能够正确跳转进入口程序