#define move_to_user_mode() \
__asm__ ("movl %%esp,%%eax\n\t" \
	"pushl $0x17\n\t" \
	"pushl %%eax\n\t" \
	"pushfl\n\t" \
	"pushl $0x0f\n\t" \
	"pushl $1f\n\t" \
	"iret\n" \
	"1:\tmovl $0x17,%%eax\n\t" \
	"mov %%ax,%%ds\n\t" \
	"mov %%ax,%%es\n\t" \
	"mov %%ax,%%fs\n\t" \
	"mov %%ax,%%gs" \
	:::"ax")


//linux 0.12原子操作就是在函数入口前加cli(),再是函数return 的时候sti()
#define sti() __asm__ ("sti"::) //开中断
#define cli() __asm__ ("cli"::) //关中断
#define nop() __asm__ ("nop"::) //空操作

#define iret() __asm__ ("iret"::)

//设置门描述符：根据中断或异常回调函数地址，门描述符类型type，特权级dpl，设置位于gate_addr处的门描述符
#define _set_gate(gate_addr,type,dpl,addr) \
__asm__ ("movw %%dx,%%ax\n\t" \
	"movw %0,%%dx\n\t" \
	"movl %%eax,%1\n\t" \
	"movl %%edx,%2" \
	: \
	: "i" ((short) (0x8000+(dpl<<13)+(type<<8))), \
	"o" (*((char *) (gate_addr))), \
	"o" (*(4+(char *) (gate_addr))), \
	"d" ((char *) (addr)),"a" (0x00080000))

//设置中断门函数
#define set_intr_gate(n,addr) \
	_set_gate(&idt[n],14,0,addr)
//设置陷阱门函数
#define set_trap_gate(n,addr) \
	_set_gate(&idt[n],15,0,addr)
//设置系统陷阱门函数
#define set_system_gate(n,addr) \
	_set_gate(&idt[n],15,3,addr)

#define _set_seg_desc(gate_addr,type,dpl,base,limit) {\
	*(gate_addr) = ((base) & 0xff000000) | \
		(((base) & 0x00ff0000)>>16) | \
		((limit) & 0xf0000) | \
		((dpl)<<13) | \
		(0x00408000) | \
		((type)<<8); \
	*((gate_addr)+1) = (((base) & 0x0000ffff)<<16) | \
		((limit) & 0x0ffff); }

//在全局表中设置任务状态段/局部表描述符；状态段和局部表段均被设置成104个字节
//根据描述符地址找到要操作的描述符，然后对描述符进行赋值，主要有基地址，段限，type等。
#define _set_tssldt_desc(n,addr,type) \
__asm__ ("movw $104,%1\n\t" \ //将tss或ldt长度放入描述符长度域，tss最小为104,描述符第0-1个字节
	"movw %%ax,%2\n\t" \//将基地址低字放入描述符2-3个字节中
	"rorl $16,%%eax\n\t" \//将地址高字节右循环移入ax中
	"movb %%al,%3\n\t" \//将基地址高字中低字节移入描述符第4个字节中
	"movb $" type ",%4\n\t" \//将标志类型字节移入描述符的第五个字节
	"movb $0x00,%5\n\t" \//描述符的第6个字节置0
	"movb %%ah,%6\n\t" \//将基地址高字节移入描述符第7个字节
	"rorl $16,%%eax" \//再右循环16bit，恢复eax
	::"a" (addr), "m" (*(n)), "m" (*(n+2)), "m" (*(n+4)), \ //没有输出，输入
	 "m" (*(n+5)), "m" (*(n+6)), "m" (*(n+7)) \
	)

//在全局表中设置任务状态描述符
//n-描述符指针， addr-描述符项中段的基地址值，任务状态段的描述符类型是0x89
#define set_tss_desc(n,addr) _set_tssldt_desc(((char *) (n)),addr,"0x89")
//在全局描述符中设置局部描述符
#define set_ldt_desc(n,addr) _set_tssldt_desc(((char *) (n)),addr,"0x82")
