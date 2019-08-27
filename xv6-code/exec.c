#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  // 获取当前cpu运行的进程
  struct proc *curproc = myproc();

  // 使用文件系统前调用
  // 内部使用自旋锁保护
  // 保证同一时间只有一个cpu调用文件系统接口
  begin_op();

  // 获取路径path所对应文件的inode
  if((ip = namei(path)) == 0){
    // 如果该文件不存在，结束文件系统调用
    end_op();
    // 抛出异常
    cprintf("exec: fail\n");
    return -1;
  }
  // 为inode加锁
  // 如果ip->valid==0，将inode加载至内存
  ilock(ip);
  pgdir = 0;

  // Check ELF header
  // 检查文件的ELF头，判断是否是可执行的ELF头文件
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  // 为新进程创建页表
  if((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.
  sz = 0;

  // 加载文件所有程序段至内存中
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    // 检测文件程序头是否损坏
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    // 检查文件程序头类型是否需要装载
    if(ph.type != ELF_PROG_LOAD)
      continue;

    // 检查程序头数据是否有误
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    // 为该段分配内存
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    // 检查段起始地址
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    // 将该段加载至分配的内存中
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  // 更新inode信息并解锁inode
  iunlockput(ip);
  // 结束对文件系统的调用
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  // 再分配两页内存给进程
  // 其中第一页不允许用户访问，避免栈溢出或是其他安全问题
  // 第二页作为用户使用的栈
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  // 栈是向下扩展的
  // 故栈指针指向最后一个地址
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  // 加载程序的命令行参数
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    // 将参数拷贝至对应内存位置
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  // 栈顶为0xffffffff(-1)，代表函数递归调用到头了
  ustack[0] = 0xffffffff;  // fake return PC
  // 保存命令行参数个数
  ustack[1] = argc;
  // 保存命令行参数argv起始地址
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  // 调整栈指针
  sp -= (3+argc+1) * 4;
  // 加载用户栈至内存中
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  // 保存程序名作为调试信息
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image.
  oldpgdir = curproc->pgdir;

  // 设置进程状态
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;
  // 切换进程
  switchuvm(curproc);
  // 释放旧页表内存
  freevm(oldpgdir);
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}
