#include "asm.h"
#include "memlayout.h"
#include "mmu.h"

# Start the first CPU: switch to 32-bit protected mode, jump into C.
# The BIOS loads this code from the first sector of the hard disk into
# memory at physical address 0x7c00 and starts executing in real mode
# with %cs=0 %ip=7c00.

# 在MAKEFILE中指定起始位置为0x7c00即%ip=7c00
# 使实模式下cs:ip即为第一条指令的位置
.code16                       # Assemble for 16-bit mode
.globl start
start:
# 关闭中断
  cli                         # BIOS enabled interrupts; disable

# 将数据段寄存器，附加寄存器和栈寄存器都赋值为0
  # Zero data segment registers DS, ES, and SS.
  xorw    %ax,%ax             # Set %ax to zero
  movw    %ax,%ds             # -> Data Segment
  movw    %ax,%es             # -> Extra Segment
  movw    %ax,%ss             # -> Stack Segment

# 开启A20地址线，使内存地址突破1mb
# 开启方式有三种：
# 1.操作键盘端口0x64，0x60
# 2.BIOS中断函数
# 3.直接操作系统端口0x92
# 本程序使用键盘端口
# 0x64为键盘命令端口，0x60为键盘数据端口
# 如果键盘控制器输出端口的第2位是低位，则物理地址的第21位被清零
# 否则，第21位可以正常使用
# 引导加载器用 I/O 指令控制端口 0x64 和 0x60 上的键盘控制器，使其输出端口的第2位为高位，来使第21位地址正常工作
  # Physical address line A20 is tied to zero so that the first PCs 
  # with 2 MB would run software that assumed 1 MB.  Undo that.
seta20.1:
  inb     $0x64,%al               # Wait for not busy
  testb   $0x2,%al
  jnz     seta20.1

  movb    $0xd1,%al               # 0xd1 -> port 0x64
  outb    %al,$0x64

seta20.2:
  inb     $0x64,%al               # Wait for not busy
  testb   $0x2,%al
  jnz     seta20.2

  movb    $0xdf,%al               # 0xdf -> port 0x60
  outb    %al,$0x60


  # Switch from real to protected mode.  Use a bootstrap GDT that makes
  # virtual addresses map directly to physical addresses so that the
  # effective memory map doesn't change during the transition.
  # 加载gdt至GDTR寄存器
  lgdt    gdtdesc
  # CR0_PE 为宏定义1，用于开启保护模式
  # cr0寄存器为控制寄存器，第0位为1时开启保护模式
  # 该段代码则是开启保护模式
  movl    %cr0, %eax
  orl     $CR0_PE, %eax
  movl    %eax, %cr0

//PAGEBREAK!
  # Complete the transition to 32-bit protected mode by using a long jmp
  # to reload %cs and %eip.  The segment descriptors are set up with no
  # translation, so that the mapping is still the identity mapping.

  # 段选择子格式：
  # 段选择子共16位
  # 高13位为选择子编号
  # 第0~1位为PRL，代表权限级别
  # 第2位为TI位，0代表查找GDT，1代表查找LDT

  # 使用跳转指令让cs寄存器加载段选择子，并根据段选择子选择GDT段描述符
  # (SEG_KCODE<<3)为段选择子，SEG_KCODE=1
  # 代表选择第二项GDT段描述符
  ljmp    $(SEG_KCODE<<3), $start32

.code32  # Tell assembler to generate 32-bit code now.
start32:
  # 用段选择子设置在保护模式下的段寄存器的值
  # Set up the protected-mode data segment registers
  movw    $(SEG_KDATA<<3), %ax    # Our data segment selector
  movw    %ax, %ds                # -> DS: Data Segment
  movw    %ax, %es                # -> ES: Extra Segment
  movw    %ax, %ss                # -> SS: Stack Segment
  # 将fs和gs两个辅助寄存器设置为0
  movw    $0, %ax                 # Zero segments not ready for use
  movw    %ax, %fs                # -> FS
  movw    %ax, %gs                # -> GS

  # Set up the stack pointer and call into C.
  # 设置栈指针为bootasm.S的首条指令，跳转至c程序函数bootmain
  movl    $start, %esp
  call    bootmain

# boot失败时触发bochs模拟器断点
  # If bootmain returns (it shouldn't), trigger a Bochs
  # breakpoint if running under Bochs, then loop.
  movw    $0x8a00, %ax            # 0x8a00 -> port 0x8a00
  movw    %ax, %dx
  outw    %ax, %dx
  movw    $0x8ae0, %ax            # 0x8ae0 -> port 0x8a00
  outw    %ax, %dx
# boot失败，死循环
spin:
  jmp     spin


# Bootstrap GDT
.p2align 2                                # force 4 byte alignment
gdt:
  # 三个GDT项
  SEG_NULLASM                             # null seg
  SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff)   # code seg
  SEG_ASM(STA_W, 0x0, 0xffffffff)         # data seg

gdtdesc:
  # GDT所占大小-1
  .word   (gdtdesc - gdt - 1)             # sizeof(gdt) - 1
  # 起始GDT项
  .long   gdt                             # address gdt

