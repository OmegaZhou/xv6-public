# 底层硬件

## 键盘

### 键盘端口

* CPU可使用0x60端口来获取键盘输入

### 扫描码

* 0x60输入的数据为键盘按键的扫描码，当按下按键或松开按键时，键盘都会传送一个扫描码至0x60端口
* 同一按键的松开和按下的扫描码关系为**松开扫描码**=**按下扫描码**+**0x80**，如a的扫描码为0x1E和0x9E
* 扫描码只有按键有关，不区分大小写

* 部分扫描码在由两个字符构成，如小键盘的扫描码都以0xE0作为开头，如右箭头的扫描码为0xE0 0x4D和0xE0 0xCD

### xv6获取按键信息流程

* xv6使用kbdgetc函数从作为底层驱动直接从键盘获取信息

* 函数解析

  ``` c
  int
  kbdgetc(void)
  {
    static uint shift;
    static uchar *charcode[4] = {
      normalmap, shiftmap, ctlmap, ctlmap
    };
    uint st, data, c;
    // 检查是否有数据
    st = inb(KBSTATP);
    if((st & KBS_DIB) == 0)
      return -1;
    // 获取扫描码
    data = inb(KBDATAP);
  
    // 发送的第一个数据为0xE0，即代表该扫描码有两个数字构成
    // 启用E0标记位
    if(data == 0xE0){
      shift |= E0ESC;
      return 0;
    } else if(data & 0x80){
      // Key released
      // 按键松开，还原配置
      // shiftcode使用按下扫描码来作为映射
      // 当扫描码为E0扫描码时，使用松开扫描码以确保与其他键使用不同的映射
      data = (shift & E0ESC ? data : data & 0x7F);
      shift &= ~(shiftcode[data] | E0ESC);
      return 0;
    } else if(shift & E0ESC){
      // Last character was an E0 escape; or with 0x80
      // 若是E0扫描码的按键，使用松开扫描码进行映射
      // 并且取消E0标志位
      data |= 0x80;
      shift &= ~E0ESC;
    }
  
    // 使用如CTRL，ALT这样按下才生效的的功能键
    shift |= shiftcode[data];
    // 使用CapsLk，NumLock这样保持状态
    shift ^= togglecode[data];
    // 根据shift的状态位来得到当前按键使用的扫描码映射
    c = charcode[shift & (CTL | SHIFT)][data];
    // 若当前为大写锁定状态，将小写字母转大写，大写转小写
    if(shift & CAPSLOCK){
      if('a' <= c && c <= 'z')
        c += 'A' - 'a';
      else if('A' <= c && c <= 'Z')
        c += 'a' - 'A';
    }
    return c;
  }
  ```

## 显示

* xv6将显示内容输入显存中，从而显示在屏幕上