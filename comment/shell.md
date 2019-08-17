# xv6 Shell

**<u>shell</u>** 是一个普通的程序，它接受用户输入的命令并且执行它们，它也是传统 Unix 系统中最基本的用户界面。shell 作为一个普通程序，而不是内核的一部分，充分说明了系统调用接口的强大：shell 并不是一个特别的用户程序。这也意味着 shell 是很容易被替代的，实际上这导致了现代 Unix 系统有着各种各样的 shell，每一个都有着自己的用户界面和脚本特性。xv6 shell 本质上是一个 Unix Bourne shell 的简单实现。

主循环通过 `getcmd` 读取命令行的输入，然后它调用 `fork` 生成一个 shell 进程的副本。父 shell 调用 `wait`，而子进程执行用户命令。

```c
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
// Simplifed xv6 shell.
 
#define MAXARGS 10
 
// All commands have at least a type. Have looked at the type, the code
// typically casts the *cmd to some specific cmd type.
struct cmd {
  int type;          //  ' ' (exec), | (pipe), '<' or '>' for redirection
};
 
struct execcmd {//最基本命令
  int type;              // ' '
  char *argv[MAXARGS];   // arguments to the command to be exec-ed
};
 
struct redircmd {
  int type;          // < or > 
  struct cmd *cmd;   // the command to be run (e.g., an execcmd)
  char *file;        // the input/output file
  int mode;          // the mode to open the file with
  int fd;            // the file descriptor number to use for the file
};
 
struct pipecmd {
  int type;          // |
  struct cmd *left;  // left side of pipe，输入
  struct cmd *right; // right side of pipe，输出
};
 
int fork1(void);  // Fork but exits on failure.
struct cmd *parsecmd(char*);
 
// Execute cmd.  Never returns.
void runcmd(struct cmd *cmd)
{
  int p[2], r;
  struct execcmd *ecmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;
 
  if(cmd == 0)
    exit(0);
  
  switch(cmd->type){
  default:
    fprintf(stderr, "unknown runcmd\n");
    exit(-1);
 
  case ' '://可执行文件
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit(0);
//    fprintf(stderr, "exec not implemented\n");
    // Your code here ..
    if (access(ecmd->argv[0], F_OK) == 0)//access作用检查能否对某个文件执行某个操作，mode_>R_OK(测试可读），W_OK(测试可写），X_OK(测试可执行），F_OK(测试是否存在）成功返回0，失败返回-1,execv会停止当前进程，并以pathname应用进程替换被停止的进程进程ID没有变,execv(pathname,argv[])
    {
        execv(ecmd->argv[0], ecmd->argv);//pathname文件路径,argv传给应用程序的参数列表，第一个为应用程序名字本身，最后一个为NULL
    }
    else
    {
	const char *binPath = "/bin/";
	int pathLen = strlen(binPath) + strlen(ecmd->argv[0]);
	char *abs_path = (char *)malloc((pathLen+1)*sizeof(char));
	strcpy(abs_path, binPath);//后面的字符串复制到前面
	strcat(abs_path, ecmd->argv[0]);//后面的字符串追加到前面
	if(access(abs_path, F_OK)==0)
	{
		execv(abs_path, ecmd->argv);
	}
	else
		fprintf(stderr, "%s: Command not found\n", ecmd->argv[0]);
	
    }
    break;
 
  case '>':
  case '<':
    rcmd = (struct redircmd*)cmd;
    //fprintf(stderr, "redir not implemented\n");
    // Your code here ...
    close(rcmd->fd);//关闭标准的输入输出，fd为文件描述符
    if(open(rcmd->file, rcmd->mode, 0644) < 0)//打开新的文件作为新的标准输入输出
    {
	    fprintf(stderr, "Unable to open file: %s\n", rcmd->file);
	    exit(0);
    }
    runcmd(rcmd->cmd);
    break;
 
  case '|':
    pcmd = (struct pipecmd*)cmd;
    //fprintf(stderr, "pipe not implemented\n");
    // Your code here ...
    // pipe建立一个缓冲区，并把缓冲区通过fd形似给程序调用，它将p[0]修改为缓冲区的读取端，p[1]修改为缓存区的写如端
    // dup（old_fd)产生一个fd，指向old-fd指向的文件,并返回这个fd
    if(pipe(p) < 0)
    {
        fprintf(stderr, "pipe failed\n");
        //exit(0);
    }
    if(fork1() == 0)
    {
        close(1);//关闭标准输出
        dup(p[1]);//把标准输出定向到p[1]所指文件，即管道写入端
	//去掉管道对端口的引用
        close(p[0]);
        close(p[1]);
	//left的标准输入不变，标准输入流入管道
        runcmd(pcmd->left);
    }
 
    if(fork1() == 0)
    {
        close(0);
        dup(p[0]);
        close(p[0]);
        close(p[1]);
        runcmd(pcmd->right);
    }
 
    close(p[0]);
    close(p[1]);
    wait(&r);
    wait(&r);
    break;
  }    
  exit(0);
}
 
int
getcmd(char *buf, int nbuf)//读入命令
{
  
  if (isatty(fileno(stdin)))//判断标准输入是否为终端
    fprintf(stdout, "$ ");//是终端则显示提示符
  memset(buf, 0, nbuf);
  fgets(buf, nbuf, stdin);//从标准输入读入nbuf个字符到buf中
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}
 
int
main(void)
{
  static char buf[100];
  int fd, r;
 
  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0){
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      //如果是cd命令，切换命令后继续等待读入
      // Clumsy but will have to do for now.
      // Chdir has no effect on the parent if run in the child.
      //一般写完命令敲回车，这里把回车改成‘\0’
      buf[strlen(buf)-1] = 0;  // chop \n，把回车改为0
      if(chdir(buf+3) < 0)//chdir is the same as cd,when sucess, it will return 0, if not, it will return -1 
        fprintf(stderr, "cannot cd %s\n", buf+3);//print the message to the file sterr(stream)
      continue;
    }
      //若不是cd命令，则fork出子程序尝试运行命令
    if(fork1() == 0)//the conmad is not cd, then fork
      runcmd(parsecmd(buf));//child process，parsecmd解析buf，结果送入runcmd
    wait(&r);//等待子进程的结束
  }
  exit(0);
}
 
int
fork1(void)
{
  int pid;
  
  pid = fork();
  if(pid == -1)
    perror("fork");
  return pid;
}
 
struct cmd*
execcmd(void)
{
  struct execcmd *cmd;
 
  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = ' ';
  return (struct cmd*)cmd;
}
 
struct cmd*
redircmd(struct cmd *subcmd, char *file, int type)
{
  struct redircmd *cmd;
 
  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = type;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->mode = (type == '<') ?  O_RDONLY : O_WRONLY|O_CREAT|O_TRUNC;
//o_rdonly read only 只读
//o_wronly write only 只写
//o_crEAT 若文件不存在则建新文件
//o_trunc 若文件存在则长度被截为0(属性不变) 
  cmd->fd = (type == '<') ? 0 : 1;
  return (struct cmd*)cmd;
}
 
struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;
 
  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = '|';
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}
 
// Parsing
 
char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>";
 
int
gettoken(char **ps, char *es, char **q, char **eq)//提取出一段字符串
{
  char *s;
  int ret;
  
  s = *ps;
  while(s < es && strchr(whitespace, *s))//找到非空，非换行回车的第一个字符
    s++;
  if(q)
    *q = s;
  ret = *s;//返回第一个不是空格的字符指针
  switch(*s){
  case 0://到尾部了
    break;
  case '|':
  case '<':
    s++;
    break;
  case '>'://忽略此字符
    s++;
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;//s指向空格或symbols字符
    break;
  }
  if(eq)
    *eq = s;
//q和eq分别指向非空非symbols字符串的首尾  
  while(s < es && strchr(whitespace, *s))
    s++;//找到下一个非空字符
  *ps = s;
  return ret;
}
 
int
peek(char **ps, char *es, char *toks)//找到字符串中非空的第一个字符，若toks中则返回1
{
  char *s;
  
  s = *ps;
  while(s < es && strchr(whitespace, *s))//找到不是空格的第一个字符
    s++;
  *ps = s;
  return *s && strchr(toks, *s);//检查该字符是不是tock中的字符
}
 
struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);
 
// make a copy of the characters in the input buffer, starting from s through es.
// null-terminate the copy to make it a string.
char 
*mkcopy(char *s, char *es)
{
  int n = es - s;
  char *c = malloc(n+1);
  assert(c);
  strncpy(c, s, n);
  c[n] = 0;
  return c;
}
 
struct cmd*
parsecmd(char *s)
{//命令构造
  char *es;
  struct cmd *cmd;
 
  es = s + strlen(s);//es points to the last blank char
  cmd = parseline(&s, es);
  peek(&s, es, "");//s指向第一个非空字符
  if(s != es){
    fprintf(stderr, "leftovers: %s\n", s);
    exit(-1);
  }
  return cmd;
}
 
struct cmd*
parseline(char **ps, char *es)//参数字符串的头指针和末尾指针
{//将字符串转化为命令
  struct cmd *cmd;
  cmd = parsepipe(ps, es);
  return cmd;
}
 
struct cmd*
parsepipe(char **ps, char *es)//处理管道命令
{
  struct cmd *cmd;
 
  cmd = parseexec(ps, es);//不同？
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}
 
struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)//重定向文件
{
  int tok;
  char *q, *eq;
 
  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);//提取出重定向的字符
    if(gettoken(ps, es, &q, &eq) != 'a') {//提取出重定向之后的文件名
      fprintf(stderr, "missing file for redirection\n");
      exit(-1);
    }
    switch(tok){
    case '<':
      cmd = redircmd(cmd, mkcopy(q, eq), '<');
      break;
    case '>':
      cmd = redircmd(cmd, mkcopy(q, eq), '>');
      break;
    }
  }
  return cmd;
}
 
struct cmd*
parseexec(char **ps, char *es)//管道左右字符串的提取
{//
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;
  
  ret = execcmd();
  cmd = (struct execcmd*)ret;
 
  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a') {
      fprintf(stderr, "syntax error\n");
      exit(-1);
    }
    cmd->argv[argc] = mkcopy(q, eq);
    argc++;
    if(argc >= MAXARGS) {
      fprintf(stderr, "too many args\n");
      exit(-1);
    }
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  return ret;
}

```

