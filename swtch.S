# Context switch
#   进程切换函数汇编代码
#   void swtch(struct context **old, struct context *new);
#   函数原型void swtch(struct context **old, struct context *new);
# Save the current registers on the stack, creating
# a struct context, and save its address in *old.
# Switch stacks to new and pop previously-saved registers.

.globl swtch
swtch: #寄存器参数赋值
  # 进程堆栈如下：
  #          +----------------+
  #          | context *new   |
  #  edx --> +----------------+
  #          | context **old  |
  #  eax --> +----------------+
  #          | eip            |
  #  esp --> +----------------+
  movl 4(%esp), %eax #将Reg[4+esp]的内容赋值给Reg[eax]
  movl 8(%esp), %edx #将Reg[8+esp]的内容赋值给Reg[edx]

  # Save old callee-saved registers
  #            +----------------+
  #            | context *new   |
  #  edx -->   +----------------+
  #            | context **old  |
  #  eax -->   +----------------+
  #          / | eip            |
  #          | +----------------+
  #          | | ebp            |
  #          | +----------------+
  #  context | | ebx            |
  #          | +----------------+
  #          | | esi            |
  #          | +----------------+
  #          \ | edi            |
  #  esp -->   +----------------+
  pushl %ebp #保存存放当前线程的栈底指针的寄存器，压入栈中
  pushl %ebx #保存基地址寄存器，压入栈中
  pushl %esi #保存源索引寄存器，压入栈中
  pushl %edi #保存目标索引寄存器，压入栈中

  # Switch stacks 切换堆栈环境
  movl %esp, (%eax)   #将Reg[esp]的内容赋值给Reg[eax]，保存原进程栈顶指针至eax寄存器，*old = old_proc->context = esp
  movl %edx, %esp     #将Reg[edx]的内容赋值给Reg[esp]，即建立新的进程栈环境，esp = new_proc->context

  # Load new callee-saved registers 恢复上下文环境
  popl %edi  #弹出新进程context上下文的寄存器内容，恢复。
  popl %esi
  popl %ebx
  popl %ebp
  ret #返回函数调用点
