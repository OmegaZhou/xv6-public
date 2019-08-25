# 文件系统

## 文件系统的构成

xv6的文件系统一共分成5层，这五层分别是：

1. buffer层。与其他操作系统类似，vx6采用cache-主存-辅存 三级存储结构。所有接近CPU的操作都应先存放在buffer中。buffer层解决了，存储在buffer中的block，应该如何被读写和保护。

2. log层。每一次buffer和memory之间的数据交换，都会通过log进行。这是为了防止突然断电导致的数据不一致，保证了数据的安全性。

3. file层。包括inode分配，文件读写等一些元操作。

4. directory层。这里会设计一些特殊的inode，用来记录其他的inode以及一些特殊数据。

5. name层。最后我们是通过路径和文件名，来访问各个文件的。file和directory中的一些数据，比如在memory中的存储位置等信息，被封装了起来。

## 涉及的文件
vx6中，涉及文件系统的相关文件罗列如下。

源文件 | 头文件 | 描述
-|-|-
bio.c|buf.h|处理buffer层|
log.c||处理log层|
file.c|file.h|文件的