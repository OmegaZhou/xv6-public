#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);//获取进程表锁 

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED) //寻找未使用的进程控制块
      goto found;

  release(&ptable.lock); //未找到，释放进程锁
  return 0; //返回0

found:
  p->state = EMBRYO; //设置进程状态为创建态
  p->pid = nextpid++; //设置进程ID（自增）

  release(&ptable.lock); //释放进程锁

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){ //从内存链表上分配4096字节的一页内存作为内核栈空间 
    p->state = UNUSED; //分配失败，重置进程状态
    return 0; //返回0，进程内存分配失败
  }
  sp = p->kstack + KSTACKSIZE; //将栈顶指针设置为内存高地址处 

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp; //设置trapframe的栈底指针 

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp; //设置进程上下文指针 
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret; //init进程将从forkret处开始执行 

  return p;
}

//PAGEBREAK: 32
// Set up first user process.创建第一个用户进程
//这个函数只调用一次, 创建的init process是所有进程的父进程
/*
 init进程的内存布局：
 +--------------------+ 4GB
 |                    |
 |                    |
 |                    |
 +--------------------+ KERNBASE+PHYSTOP(2GB+224MB)
 |                    |
 |   direct mapped    |
 |   kernel memory    |
 |                    |
 +--------------------+
 |    Kernel Data     |
 +--------------------+ data
 |    Kernel Code     |
 +--------------------+ KERNLINK(2GB+1MB)
 |   I/O Space(1MB)   |
 +--------------------+ KERNBASE(2GB)
 |                    |
 |                    |
 |                    |
 |                    |
 |                    |
 |                    |
 |                    |
 |                    |
 +---------+----------+ PGSIZE <-- %esp
 |         v          |
 |       stack        |
 |                    |
 |                    |
 |     initcode.S     |
 +--------------------+ 0  <-- %eip
*/
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];//这是initcode.S的加载位置和大小

  /* 分配进程数据结构并初始化 */
  p = allocproc(); //在内存中分配一个proc结构，并初始化进程内核堆栈以及一系列内核寄存器
  
  initproc = p;   //将新分配内存的进程p赋值给initproc，代表所有进程的父进程
    /* 创建页表，将进程的kernel部分页映射进来 */
  if((p->pgdir = setupkvm()) == 0) //创建页表
    panic("userinit: out of memory?"); //若创建失败则可能内存不够，抛出异常
  /*将initcode.S第一个进程的代码加载到进程p中，并分配一页物理内存，将虚拟地址0映射到该物理地址，实现虚拟地址初始化*/
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE; //设置进程大小为一页
  memset(p->tf, 0, sizeof(*p->tf));

    /* 设置初始的用户模式状态 */

  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;//cs寄存器指向代码段并处于用户模式 
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;//ds寄存器指向数据段并处于用户模式 
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;// 允许硬件中断 
  p->tf->esp = PGSIZE;// 用户栈大小为1页
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);//获取进程锁，确保原子操作

  p->state = RUNNABLE;  //将进程状态置RUNNABLE

  release(&ptable.lock);//释放进程锁
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc(); //获取当前进程curproc

  // Allocate process.
  if((np = allocproc()) == 0){ //分配proc结构体，初始化该proc
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){ //一次一页复制父进程地址空间
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED; //复制失败，回收空间
    return -1;
  }
  np->sz = curproc->sz; //继承父进程大小
  np->parent = curproc; //设置父进程为curproc
  *np->tf = *curproc->tf; //继承trapframe

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0; //清除寄存器%eax内容，中断返回0

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]); //继承打开文件
  np->cwd = idup(curproc->cwd);  //继承运行目录

  safestrcpy(np->name, curproc->name, sizeof(curproc->name)); //继承父进程名
 
  pid = np->pid;

  acquire(&ptable.lock); //获取进程表锁

  np->state = RUNNABLE; //置进程状态为就绪态

  release(&ptable.lock);

  return pid; //返回新进程pid
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc(); //获取当前进程
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){ //关闭当前进程打开的所有文件
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);		//把对当前目录的引用从内存中删除
  end_op();
  curproc->cwd = 0;			//当前运行目录置空

  acquire(&ptable.lock);	//获取进程表锁

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent); //唤醒当前进程的父进程，以及以上的进程

  // Pass abandoned children to init.
  //把当前进程的所有子进程都划归为用户初始进程initproc的子进程中。
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE; //把当前进程改为僵尸进程
  sched(); //陷入内核态，开始调度
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu(); //获取当前CPU
  c->proc = 0;
  for(;;){
    // Enable interrupts on this processor.
    sti(); // 在每次执行一个进程之前，需要调用sti()函数开启CPU的中断
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);  //获取进程表锁，与其他CPU的scheduler线程互斥
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE) //扫描进程表, 找到一个进程状态为RUNNABLE的进程
        continue;
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      /*切换到选择的进程，释放进程表锁，当进程结束时，再重新获取*/
      c->proc = p; //将找到的进程设置为CPU当前执行的进程
      switchuvm(p); //切换至目标进程页表，并通知硬件
      p->state = RUNNING; //切换进程状态为RUNNING，将进程p置为运行态
      /* 
       * this perform a context switch to the target process's kernel thread
       * the current context is not a process but rather a special per-cpu scheduler context,
       * so scheduler tells swtch() to save the current hardware registers in 
       * per-cpu storage(cpu->scheduler) rather than in any process's kernel thread context
       */ 
      swtch(&(c->scheduler), p->context);//从当前cpu->scheduler现场切换至进程p的上下文环境
      switchkvm();//切换至内核页表
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      //当前cpu没有正在运行的进程
      c->proc = 0;
    }
    release(&ptable.lock); //释放进程锁

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();
  if(!holding(&ptable.lock))   // 是否获取到了进程表锁
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)   // 是否执行过pushcli
    panic("sched locks");
  if(p->state == RUNNING)  // 执行的程序应该处于结束或者睡眠状态
    panic("sched running");
  if(readeflags()&FL_IF)  // 判断中断是否可以关闭
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);    // 上下文切换至当前cpu的scheduler
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock 获取进程表锁
  myproc()->state = RUNNABLE; //当前进程时间片用完，置就绪状态
  sched();  //请求调度
  release(&ptable.lock); //释放进程表锁
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc(); //获取当前进程
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING; //置进程状态为SLEEPING

  sched();	//调度进程，陷入内核态

  // Tidy up.
  p->chan = 0; //释放进程等待链

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}


//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
