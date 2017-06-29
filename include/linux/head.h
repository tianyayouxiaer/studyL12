#ifndef _HEAD_H
#define _HEAD_H

//段描述符数据结构，该描述符由8个字节构成，每个描述符表共256项。
typedef struct desc_struct {
	unsigned long a,b;
} desc_table[256];

extern unsigned long pg_dir[1024];//内存中页目录数组，每个目录项4个字节，从物理地址0开始
extern desc_table idt,gdt;//中断描述符表，全局描述符表

#define GDT_NUL 0 //全局描述符表第0项，不用
#define GDT_CODE 1//内核代码段描述符项
#define GDT_DATA 2//内核数据段描述符项
#define GDT_TMP 3//系统段描述符项，linux中没有使用

#define LDT_NUL 0//每个局部描述符表的第0项，不用
#define LDT_CODE 1//第一项，是用户程序代码段描述符项
#define LDT_DATA 2//第二项，是用户程序数据段描述符项

#endif
