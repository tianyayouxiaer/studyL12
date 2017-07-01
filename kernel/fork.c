/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

extern void write_verify(unsigned long address);

long last_pid=0;

 /*
 *	功能: 进程空间写前验证函数，对addr到addr+size这段进程空间以页为单位执行写操作前的检测操作，
 *        若页面是只读的，则执行共享检验和cow
 *  返回: 
 *	参数:
 */
void verify_area(void * addr,int size)
{
	unsigned long start;

	start = (unsigned long) addr;
	size += start & 0xfff;
	start &= 0xfffff000;
	start += get_base(current->ldt[2]);
	while (size>0) {
		size -= 4096;
		write_verify(start);//若页面不可写，则复制页面
		start += 4096;
	}
}

//复制内存页表
//nr - 新任务号; p - 新任务数据结构
int copy_mem(int nr,struct task_struct * p)
{
	unsigned long old_data_base,new_data_base,data_limit;
	unsigned long old_code_base,new_code_base,code_limit;

	code_limit=get_limit(0x0f); //代码段描述符
	data_limit=get_limit(0x17);//数据段描述符
	old_code_base = get_base(current->ldt[1]);//取当前进程代码段线性空间基地址
	old_data_base = get_base(current->ldt[2]);//取当前进程数据段线性空间基地址
	if (old_data_base != old_code_base)
		panic("We don't support separate I&D");
	if (data_limit < code_limit)
		panic("Bad data_limit");
		
	//然后设置创建中的新进程在线性空间中的基地址等于(64MBX任务号)
	//并用该值设置新进程局部描述符表中段描述符中的基地址
	new_data_base = new_code_base = nr * TASK_SIZE;
	p->start_code = new_code_base;
	set_base(p->ldt[1],new_code_base);
	set_base(p->ldt[2],new_data_base);

	//设置进程页目录项和页表项，即复制当前进程（父进程页目录项和页表项）
	//此时，父进程和子进程共享内存页面
	if (copy_page_tables(old_data_base,new_data_base,data_limit)) {
		free_page_tables(new_data_base,data_limit);
		return -ENOMEM;
	}
	return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
 //复制进程
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx, long orig_eax, 
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
{
	struct task_struct *p;
	int i;
	struct file *f;

	//1、为新任务数据结构分配内存
	p = (struct task_struct *) get_free_page();
	if (!p)
		return -EAGAIN;
	//新任务指针放入任务数组项中
	task[nr] = p;
	//2、复制当前任务结构到新的任务数据结构中
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack */

	//修改任务结构
	p->state = TASK_UNINTERRUPTIBLE;//防止内核调度执行
	p->pid = last_pid;
	p->counter = p->priority;
	p->signal = 0;
	p->alarm = 0;
	p->leader = 0;		/* process leadership doesn't inherit，进程领导权是不能继承的 */
	p->utime = p->stime = 0;
	p->cutime = p->cstime = 0;
	p->start_time = jiffies;

	//修改任务状态段tss数据
	p->tss.back_link = 0;
	p->tss.esp0 = PAGE_SIZE + (long) p;//任务内核态指针，指向该页顶端ss0：esp0为程序在内核态执行时的栈
	p->tss.ss0 = 0x10;
	p->tss.eip = eip;
	p->tss.eflags = eflags;
	p->tss.eax = 0;
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr);//任务局部表描述符的选择符
	p->tss.trace_bitmap = 0x80000000;

	//3、如果当前任务使用了协助利器，就保存其上下文
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0 ; frstor %0"::"m" (p->tss.i387));

	//4、复制进程页表。即在线性地址空间中设置新任务代码段和数据段描述符中的基址
	//和限长，并复制页表。
	if (copy_mem(nr,p)) {
		task[nr] = NULL;
		free_page((long) p);
		return -EAGAIN;
	}

	//5、如果父进程中有文件是打开的，则对应文件的打开次数增1
	//父进程和子进程共享打开的文件
	for (i=0; i<NR_OPEN;i++)
		if (f=p->filp[i])
			f->f_count++;

	//将当前进程（父进程的pwd，root和executable引用次数增1）
	//子进程页应用了这些节点
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;
	if (current->library)
		current->library->i_count++;

	//在gdt表中设置新任务tss段和ldt段描述符项	
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));

	//设置进程间关系链表指针
	p->p_pptr = current;//设置新进程父进程指针
	p->p_cptr = 0;//复位新进程的最新子进程指针
	p->p_ysptr = 0;//复位新进程的比邻年轻兄弟进程指针
	p->p_osptr = current->p_cptr;//设置新进程的比邻年轻兄弟进程指针
	if (p->p_osptr)
		p->p_osptr->p_ysptr = p;//若新进程有老兄兄弟进程，则让其年轻进程兄弟指针执行新进程
	current->p_cptr = p;//让当前进程最新子进程指针指向新进程
	p->state = TASK_RUNNING;	/* do this last, just in case,置为就绪态 */
	return last_pid;//返回新进程号
}

//为新进程取得不重复的进程号last_pid。函数返回在任务数组中的任务号
int find_empty_process(void)
{
	int i;

	repeat:
		if ((++last_pid)<0) last_pid=1;
		for(i=0 ; i<NR_TASKS ; i++)
			if (task[i] && ((task[i]->pid == last_pid) ||
				        (task[i]->pgrp == last_pid)))
				goto repeat;
	for(i=1 ; i<NR_TASKS ; i++)//任务0被排除在外
		if (!task[i])
			return i;
	return -EAGAIN;
}
