# 系统调用

## 涉及文件

* syscall.c
* sysproc.c
* sysfile.c

## 系统调用流程

* 用户先将要调用的系统调用编号存入eax寄存器中
* 调用中断指令 INT T_SYSCALL;进入中断处理程序
* 中断处理程序调用trap函数，trap函数对于系统调用执行syscall函数
* syscall函数根据当前进程trapframe属性获取系统调用编号，从而执行相应系统调用

## 系统调用实现

* xv6系统调用的参数均存放在栈中，由alltrap函数将其存放于结构体trapframe中
* xv6对访问参数的操作进行了封装，使用arint，arstr，arptr三个函数获取整型，字符串，指针三种不同类型的参数
* 每个系统调用都是对相应模块的API进行封装，通过调用arint，arstr，arptr函数获取参数，然后再调用相应模块提供的API

## 系统调用种类

* xv6主要提供了进程管理和文件管理两种类型的系统调用，分别位于sysproc.c和sysfile.c中
* sysproc.c中的系统调用封装了进程管理提供的API
* sysfile.c中的系统调用封装了文件系统提供的API