# Initial process execs /init.
# This code runs in user space.

#include "syscall.h"
#include "traps.h"

#第一个进程运行程序的汇编代码
# exec(init, argv)
# 触发 exec 系统调用
#将 argv，init，0三个值压入栈
.globl start
start:
  pushl $argv
  pushl $init
  pushl $0  // where caller pc would be
  movl $SYS_exec, %eax #将系统调用编号存放在%eax寄存器中
  int $T_SYSCALL  # 调用编号为T_SYSCALL即64号中断进入系统调用

# for(;;) exit(); 
exit:
  movl $SYS_exit, %eax
  int $T_SYSCALL
  jmp exit

# char init[] = "/init\0";
init:
  .string "/init\0"

# char *argv[] = { init, 0 };
.p2align 2
argv:
  .long init
  .long 0

