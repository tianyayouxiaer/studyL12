/*
 *  linux/kernel/system_call.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  system_call.s  contains the system-call low-level handling routines.
 * This also contains the timer-interrupt handler, as some of the code is
 * the same. The hd- and flopppy-interrupts are also here.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call. Ordinary interrupts
 * don't handle signal-recognition, as that would clutter them up totally
 * unnecessarily.
 *
 * Stack layout in 'ret_from_system_call':
 *
 *	 0(%esp) - %eax
 *	 4(%esp) - %ebx
 *	 8(%esp) - %ecx
 *	 C(%esp) - %edx
 *	10(%esp) - original %eax	(-1 if not system call)
 *	14(%esp) - %fs
 *	18(%esp) - %es
 *	1C(%esp) - %ds
 *	20(%esp) - %eip
 *	24(%esp) - %cs
 *	28(%esp) - %eflags
 *	2C(%esp) - %oldesp
 *	30(%esp) - %oldss
 */

SIG_CHLD	= 17//父进程发出，停止或终止子进程。

EAX		= 0x00//堆栈中各个寄存器的偏移位置
EBX		= 0x04
ECX		= 0x08
EDX		= 0x0C
ORIG_EAX	= 0x10
FS		= 0x14
ES		= 0x18
DS		= 0x1C
EIP		= 0x20
CS		= 0x24
EFLAGS		= 0x28
OLDESP		= 0x2C //当有特权级变化时
OLDSS		= 0x30

state	= 0		# these are offsets into the task-struct.//进程状态
counter	= 4 //时间片
priority = 8
signal	= 12
sigaction = 16		# MUST be 16 (=len of sigaction)
blocked = (33*16)

# offsets within sigaction
sa_handler = 0
sa_mask = 4
sa_flags = 8
sa_restorer = 12

nr_system_calls = 82//系统调用总数

ENOSYS = 38

/*
 * Ok, I get parallel printer interrupts while using the floppy for some
 * strange reason. Urgel. Now I just ignore them.
 */
.globl _system_call,_sys_fork,_timer_interrupt,_sys_execve
.globl _hd_interrupt,_floppy_interrupt,_parallel_interrupt
.globl _device_not_available, _coprocessor_error

.align 2 //内存4字节对齐
bad_sys_call://错误系统调用号从这里返回
	pushl $-ENOSYS
	jmp ret_from_sys_call
.align 2
reschedule://重新执行调度程序人口
	pushl $ret_from_sys_call//ret_from_sys_call地址入栈
	jmp _schedule
.align 2
_system_call://int80 系统调用入口点
	push %ds
	push %es
	push %fs
	pushl %eax		# save the orig_eax//eax存放的是系统调用号
	pushl %edx		
	pushl %ecx		# push %ebx,%ecx,%edx as parameters
	pushl %ebx		# to the system call
	movl $0x10,%edx		# set up ds,es to kernel space
	mov %dx,%ds //ds，es指向内核数据段
	mov %dx,%es
	movl $0x17,%edx		# fs points to local data space
	mov %dx,%fs
	cmpl _NR_syscalls,%eax
	jae bad_sys_call
	call _sys_call_table(,%eax,4)//调用地址=_sys_call_table+%eax*4
	pushl %eax//系统调用返回值入栈
2:
	movl _current,%eax//取当前任务数据结构地址->eax
	cmpl $0,state(%eax)		# state//查看当前任务运行状态，如果不在就绪态就执行调度程序
	jne reschedule
	cmpl $0,counter(%eax)		# counter//如果该任务在就绪态，但时间片用完，也执行调度程序
	je reschedule
ret_from_sys_call://从系统调用返回后，对信号量进行识别和处理
	movl _current,%eax //task0不必对其进行信号量方面处理，直接返回
	cmpl _task,%eax			# task[0] cannot have signals
	je 3f
	cmpw $0x0f,CS(%esp)		# was old code segment supervisor //内核中任务直接退出中断，否则进行信号量处理
	jne 3f
	cmpw $0x17,OLDSS(%esp)		# was stack segment = 0x17 //原堆栈不在用户数据段中，则也退出
	jne 3f
	movl signal(%eax),%ebx//
	movl blocked(%eax),%ecx
	notl %ecx
	andl %ebx,%ecx
	bsfl %ecx,%ecx
	je 3f
	btrl %ecx,%ebx
	movl %ebx,signal(%eax)
	incl %ecx
	pushl %ecx
	call _do_signal
	popl %ecx
	testl %eax, %eax
	jne 2b		# see if we need to switch tasks, or do more signals
3:	popl %eax
	popl %ebx
	popl %ecx
	popl %edx
	addl $4, %esp	# skip orig_eax
	pop %fs
	pop %es
	pop %ds
	iret

.align 2
_coprocessor_error:
	push %ds
	push %es
	push %fs
	pushl $-1		# fill in -1 for orig_eax
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	pushl $ret_from_sys_call
	jmp _math_error

.align 2
_device_not_available:
	push %ds
	push %es
	push %fs
	pushl $-1		# fill in -1 for orig_eax
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	pushl $ret_from_sys_call
	clts				# clear TS so that we can use math
	movl %cr0,%eax
	testl $0x4,%eax			# EM (math emulation bit)
	je _math_state_restore
	pushl %ebp
	pushl %esi
	pushl %edi
	pushl $0		# temporary storage for ORIG_EIP
	call _math_emulate
	addl $4,%esp
	popl %edi
	popl %esi
	popl %ebp
	ret

//时钟中断处理程序，中断频率为100Hz
//jiffies加1
//用当前特权级调用do_timer
.align 2
_timer_interrupt:
	push %ds		# save ds,es and put kernel data space
	push %es		# into them. %fs is used by _system_call
	push %fs
	pushl $-1		# fill in -1 for orig_eax //填-1，表示不是系统调用
	
	pushl %edx		# we save %eax,%ecx,%edx as gcc doesnt
	pushl %ecx		# save those across function calls. %ebx
	pushl %ebx		# is saved as we use that in ret_sys_call
	pushl %eax
	
	movl $0x10,%eax  //ds和es置为指向内核数据段，gdtr+0x10，排布情况是NULL，代码段描述符，数据段描述符，每个描述符8个字节
	mov %ax,%ds
	mov %ax,%es
	
	movl $0x17,%eax //fs置为指向局部数据段，ldtr+0x ????
	mov %ax,%fs
	
	incl _jiffies//jiffies加1

	//结束时钟中断
	movb $0x20,%al		# EOI to interrupt controller #1
	outb %al,$0x20

	//从堆栈中取出执行系统调用代码选择符中特权级
	movl CS(%esp),%eax
	andl $3,%eax		# %eax is CPL (0 or 3, 0=supervisor)
	pushl %eax
	call _do_timer		# 'do_timer(long CPL)' does everything from
	addl $4,%esp		# task switching to accounting ...
	jmp ret_from_sys_call

.align 2
_sys_execve:
	lea EIP(%esp),%eax//eax指向向堆栈中保存用户程序eip指针处
	pushl %eax //调用号
	call _do_execve
	addl $4,%esp//上一个栈帧的函数返回地址
	ret

.align 2
_sys_fork:
	call _find_empty_process
	testl %eax,%eax
	js 1f
	push %gs
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %eax
	call _copy_process
	addl $20,%esp
1:	ret

_hd_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0xA0		# EOI to interrupt controller #1
	jmp 1f			# give port chance to breathe
1:	jmp 1f
1:	xorl %edx,%edx
	movl %edx,_hd_timeout
	xchgl _do_hd,%edx
	testl %edx,%edx
	jne 1f
	movl $_unexpected_hd_interrupt,%edx
1:	outb %al,$0x20
	call *%edx		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

_floppy_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0x20		# EOI to interrupt controller #1
	xorl %eax,%eax
	xchgl _do_floppy,%eax
	testl %eax,%eax
	jne 1f
	movl $_unexpected_floppy_interrupt,%eax
1:	call *%eax		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

_parallel_interrupt:
	pushl %eax
	movb $0x20,%al
	outb %al,$0x20
	popl %eax
	iret
