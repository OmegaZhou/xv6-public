// Mutual exclusion spin locks.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
// Holding a lock for a long time may cause
// other CPUs to waste time spinning to acquire it.
void
acquire(struct spinlock *lk)
{
  // 关闭中断
  pushcli(); // disable interrupts to avoid deadlock.
  // 如果锁lk已被当前cpu占有，抛出panic异常
  if(holding(lk))
    panic("acquire");

  // 进入自旋状态直到锁被释放
  // xchg函数是原子性操作，交换两个参数的值，并返回第一个参数的旧值
  // 当返回值为0时，代表锁已释放，并通过与1交换上锁
  // The xchg is atomic.
  while(xchg(&lk->locked, 1) != 0)
    ;

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen after the lock is acquired.
  // 告知编译器避免对这段代码进行乱序优化，保证临界资源在上锁后访问
  __sync_synchronize();

  // 获取当前使用的cpu
  // Record info about lock acquisition for debugging.
  lk->cpu = mycpu();
  // 获取递归调用栈
  getcallerpcs(&lk, lk->pcs);
}

// Release the lock.
void
release(struct spinlock *lk)
{
  // 若锁已被释放，抛出panic异常
  if(!holding(lk))
    panic("release");
  // 清空占有锁的cpu及递归调用栈
  lk->pcs[0] = 0;
  lk->cpu = 0;

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other cores before the lock is released.
  // Both the C compiler and the hardware may re-order loads and
  // stores; __sync_synchronize() tells them both not to.
  __sync_synchronize();

  // Release the lock, equivalent to lk->locked = 0.
  // This code can't use a C assignment, since it might
  // not be atomic. A real OS would use C atomics here.
  // 使用汇编保证解锁操作是原子的
  asm volatile("movl $0, %0" : "+m" (lk->locked) : );
  // 开启中断
  popcli();
}

// Record the current call stack in pcs[] by following the %ebp chain.
void
getcallerpcs(void *v, uint pcs[])
{
  uint *ebp;
  int i;
  // 函数调用时栈指针结构
  // ebp栈底指针
  // esp栈顶指针
  // 本函数栈指针结构
  // 
  //      较早的栈
  // +12  参数pcs
  // +8   参数v
  // +4   返回地址
  // 0    旧的ebp
  //      当前函数使用的栈
  //      esp
  // 
  // 故地址v=epb+8byte
  // 又由于uint* 偏移一个单位即偏移8byte
  // 故 ebp = (uint*)v - 2;
  ebp = (uint*)v - 2;
  for(i = 0; i < 10; i++){
    if(ebp == 0 || ebp < (uint*)KERNBASE || ebp == (uint*)0xffffffff)
      break;
    // ebp[1]即为当前函数的返回地址
    // ebp[0]为返回的函数的ebp指针
    // 故使用该循环可以自底向上获取递归调用顺序
    pcs[i] = ebp[1];     // saved %eip
    ebp = (uint*)ebp[0]; // saved %ebp
  }
  // 剩余部分用0填充
  for(; i < 10; i++)
    pcs[i] = 0;
}

// Check whether this cpu is holding the lock.
int
holding(struct spinlock *lock)
{
  int r;
  pushcli();
  r = lock->locked && lock->cpu == mycpu();
  popcli();
  return r;
}


// Pushcli/popcli are like cli/sti except that they are matched:
// it takes two popcli to undo two pushcli.  Also, if interrupts
// are off, then pushcli, popcli leaves them off.

void
pushcli(void)
{
  int eflags;

  // 获取eflags
  eflags = readeflags();
  // 关闭中断，将eflags寄存器的IF位置0
  cli();
  if(mycpu()->ncli == 0)
    // 当第一次调用cli指令且IF位未置0时，对mycpu()->intena做上标记
    mycpu()->intena = eflags & FL_IF;
  // cli次数加1
  mycpu()->ncli += 1;
}

void
popcli(void)
{
  // 若中断已经开启，IF标志位为1，抛出panic异常
  if(readeflags()&FL_IF)
    panic("popcli - interruptible");
  // popcli操作次数大于pushcli次数，抛出panic异常
  if(--mycpu()->ncli < 0)
    panic("popcli");
  // 保证了当pushcli指令和popcli指令一一对应且当前cpu为实现关闭中断的cpu时才进行sti操作
  // 避免了在自旋锁还处在申请状态下打开中断
  if(mycpu()->ncli == 0 && mycpu()->intena)
    sti();
}

