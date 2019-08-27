//
// assembler macros to create x86 segments
//

// 段描述符内容全为0
#define SEG_NULLASM                                             \
        .word 0, 0;                                             \
        .byte 0, 0, 0, 0


// The 0xC0 means the limit is in 4096-byte units
// and (for executable segments) 32-bit mode.
// 32位段描述符格式：
// 0~15位 段界限的0至15位，即(((lim) >> 12) & 0xffff)
// 16~31位 段基址的0至15位，即((base) & 0xffff)
// 32~39位 段基址的16至23位，即(((base) >> 16) & 0xff)
// 40~43位 描述符的子类型，共4bit
// 43位为X位，代表该段是否可执行，0x8代表可执行即为代码段，反之为数据段
// 42位，对于代码段来说为C位，代表是否为特权级依从，0x4代表是依从的
//        对于数据段来说为E位，代表扩展方向，0x4代表向下扩展，即向低地址方向扩展
// 41位，对于代码段来说为R位，代表是否可读，0x2代表可读，代码段皆不可写
//        对于数据段来说为W位，代表是否可写，0x2代表可写，数据段皆可读
// 40位为A位，代表是否访问过，0x1代表已访问
// 44~47位，在SEG_ASM中即为0x9
// 47位为P位，1代表描述符所代表的段处在内存中
// 45~46位为DPL，代表特权级0，1，2，3，0为最高，这里作为boot程序，特权级为最高级0
// 44位为S位，0为系统段，1为代码段或数据段，这里为数据段或代码段，故设为1
// 48~51位 段界限16~19位，即(((lim) >> 28) & 0xf)
// 52~55位 在SEG_ASM中为0xc
// 55位为G位，代表段界限的基本单位，0代表已字节为单位，1代表已4KB为单位，
//           这里设为1，故在计算段界限时，都要将参数右移12位
// 54位为D/B位，代表默认操作数大小或默认栈指针大小或上部边界的标记
//             对于代码段称为D位，0代表操作数和偏移量都是16位的，1代表32位的操作数和偏移量
//             对于数据段称为B位，0代表使用pop，push，call等操作时，使用sp寄存器，1则使用esp寄存器
//             在这里设为1
// 53位为L位，预留给64位处理器，这里设置为0
// 52位为AVL，供操作系统使用，这里直接设为0
// 故52~55位为1100即0xc
// 56~63位为段基址的24~31位，即(((base) >> 24) & 0xff)
#define SEG_ASM(type,base,lim)                                  \
        .word (((lim) >> 12) & 0xffff), ((base) & 0xffff);      \
        .byte (((base) >> 16) & 0xff), (0x90 | (type)),         \
                (0xC0 | (((lim) >> 28) & 0xf)), (((base) >> 24) & 0xff)

#define STA_X     0x8       // Executable segment
#define STA_W     0x2       // Writeable (non-executable segments)
#define STA_R     0x2       // Readable (executable segments)
