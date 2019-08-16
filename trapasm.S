#include "mmu.h"

  # vectors.S sends all traps here.
.globl alltraps
alltraps:
  # Build trap frame.
  pushl %ds
  pushl %es
  pushl %fs
  pushl %gs
  pushal
  
  # Set up data segments.
  movw $(SEG_KDATA<<3), %ax
  movw %ax, %ds
  movw %ax, %es

  # Call trap(tf), where tf=%esp
  pushl %esp
  call trap
  # 前面push了esp作为tf的地址参数
  # 这里可看作将栈中的esp弹出且不存放在任何位置
  addl $4, %esp

  # Return falls through to trapret...
.globl trapret
# 恢复现场
trapret:
  popal
  popl %gs
  popl %fs
  popl %es
  popl %ds
  # 这段空间为调用中断处理函数时主动开辟的用于存放trapno和errcode参数的
  # 故手动释放
  addl $0x8, %esp  # trapno and errcode
  iret
