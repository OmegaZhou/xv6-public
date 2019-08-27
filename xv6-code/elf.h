// Format of an ELF executable file

// ELF文件头魔数，用于标识ELF文件
#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

//ELF 文件头格式
// File header
struct elfhdr {
  uint magic;  // must equal ELF_MAGIC
  uchar elf[12];
  ushort type;
  ushort machine;
  uint version;
  uint entry;  //程序入口地址
  uint phoff;  //第一个程序段头部在文件中的偏移位置
  uint shoff;
  uint flags;
  ushort ehsize;
  ushort phentsize;
  ushort phnum; //程序段数目
  ushort shentsize;
  ushort shnum;
  ushort shstrndx;
};

// Program section header
struct proghdr {
  uint type;
  uint off;    // 程序段在文件中的偏移量
  uint vaddr;  
  uint paddr;  // 程序段载入内核的内存物理地址
  uint filesz; // 程序段在文件中的大小
  uint memsz;  // 程序段在内存中的大小
  uint flags;
  uint align;
};

// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
