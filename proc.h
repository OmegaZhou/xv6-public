
// Per-CPU state
//xv6可以运行多cpu的计算机上，struct cpu结构体来记录当前的CPU状态。
 struct cpu {
    uchar apicid;                // Local APIC ID 每个cpu都有一个唯一硬件ID，这个ID可以lapicid()函数进行获取，然后存放于这个字段中。
    struct context *scheduler;   // scheduler context，即scheduler运行环境信息
    struct taskstate ts;         // Used by x86 to find stack for interrupt
    struct segdesc gdt[NSEGS];   // x86 global descriptor table
    volatile uint started;       // Has the CPU started?
    int ncli;                    // Depth of pushcli nesting.
    int intena;                  // Were interrupts enabled before pushcli?
    struct proc *proc;           // The process running on this cpu or null
  };
   extern struct cpu cpus[NCPU]; //当前系统中存在的CPU(NCPU为8)
   extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
//进程上下文切换现场，保存被调用函数必须手动保存的寄存器的值
struct context {
  //变址寄存器
  //分别叫做"源/目标索引寄存器"
  //在很多字符串操作指令中, DS:ESI指向源串,而ES:EDI指向目标串.
  uint edi;
  uint esi;
  uint ebx;//基地址寄存器, 在内存寻址时存放基地址。
  uint ebp;//寄存器存放当前线程的栈底指针
  //寄存器存放下一个CPU指令存放的内存地址，当CPU执行完当前的指令后
  //从EIP寄存器中读取下一条指令的内存地址，然后继续执行。
  //EIP不会被显式设置，它在对swtch()函数的call和ret时被设置
  uint eip;
};

/*枚举类型procstate,分别代表一个进程的六种状态 未分配、创建态、阻塞态、就绪态、运行态、结束态*/
enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
//进程描述符
struct proc {
  uint sz; // 进程的内存大小（以byte计）
  pde_t* pgdir; // 进程页路径的线性地址。
  char *kstack; // 进程的内核栈底
  enum procstate state; // 进程状态（包括六种UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE）
  int pid; // 进程ID
  struct proc *parent; // 父进程
  struct trapframe *tf; // 位于x86.h，是中断进程后，需要恢复进程继续执行所保存的寄存器内容。
  struct context *context; // 切换进程所需要保存的进程状态。切换进程需要维护的寄存器内容，定义在proc.h中。
  void *chan;  // 不为0时，是进程睡眠时所挂的睡眠队列
  int killed; // 当非0时，表示已结束
  struct file *ofile[NOFILE]; // 打开的文件列表
  struct inode *cwd; // 进程当前路径
  char name[16]; // 进程名称
};
// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
