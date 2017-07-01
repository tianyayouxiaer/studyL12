/*
 *  linux/kernel/sched.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'sched.c' is the main kernel file. It contains scheduling primitives
 * (sleep_on, wakeup, schedule etc) as well as a number of simple system
 * call functions (type getpid(), which just extracts a field from
 * current-task
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/sys.h>
#include <linux/fdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#include <signal.h>

//取信号nr在信号位图中对应位的二进制数
//信号5的位图位：1<<(5-1) = 16 = 0001 0000
#define _S(nr) (1<<((nr)-1))

//除了SIGKILL和SIGSTOP以外信号都可以阻塞，其它信号位图为1，这两位为0
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

//显示任务号nr的进程号、进程状态以及内核堆栈空闲字数
void show_task(int nr,struct task_struct * p)
{
	int i,j = 4096-sizeof(struct task_struct);

	printk("%d: pid=%d, state=%d, father=%d, child=%d, ",nr,p->pid,
		p->state, p->p_pptr->pid, p->p_cptr ? p->p_cptr->pid : -1);
	i=0;
	while (i<j && !((char *)(p+1))[i])//检测任务数据结构后等于0的字节数，即为堆栈空闲字节数
		i++;
	printk("%d/%d chars free in kstack\n\r",i,j);
	printk("   PC=%08X.", *(1019 + (unsigned long *) p));
	if (p->p_ysptr || p->p_osptr) 
		printk("   Younger sib=%d, older sib=%d\n\r", 
			p->p_ysptr ? p->p_ysptr->pid : -1,
			p->p_osptr ? p->p_osptr->pid : -1);
	else
		printk("\n\r");
}

//显示系统中所有进程的信息
void show_state(void)
{
	int i;

	printk("\rTask-info:\n\r");
	for (i=0;i<NR_TASKS;i++)
		if (task[i])
			show_task(i,task[i]);
}

#define LATCH (1193180/HZ) //定时芯片设置的值，使它输出100Hz

extern void mem_use(void);

extern int timer_interrupt(void);
extern int system_call(void);


//每个任务在内核态运行都有自己的内核态堆栈；
//任务数据结构和内核态堆栈放在同一页中；
//从堆栈段寄存器ss可以获得其数据段选择符
union task_union {
	struct task_struct task;
	char stack[PAGE_SIZE];
};

//设置初始任务的数据
static union task_union init_task = {INIT_TASK,};

unsigned long volatile jiffies=0;
unsigned long startup_time=0;
int jiffies_offset = 0;		/* # clock ticks to add to get "true
				   time".  Should always be less than
				   1 second's worth.  For time fanatics
				   who like to syncronize their machines
				   to WWV :-) */

struct task_struct *current = &(init_task.task);
struct task_struct *last_task_used_math = NULL;

struct task_struct * task[NR_TASKS] = {&(init_task.task), };

long user_stack [ PAGE_SIZE>>2 ] ;

struct {
	long * a;
	short b;
	} stack_start = { & user_stack [PAGE_SIZE>>2] , 0x10 };
/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 */
 //当任务被调度交换以后，该函数用以保存原任务的协处理器状态
void math_state_restore()
{
	if (last_task_used_math == current)
		return;
	__asm__("fwait");
	if (last_task_used_math) {
		__asm__("fnsave %0"::"m" (last_task_used_math->tss.i387));
	}
	last_task_used_math=current;
	if (current->used_math) {
		__asm__("frstor %0"::"m" (current->tss.i387));
	} else {
		__asm__("fninit"::);
		current->used_math=1;
	}
}

/*
 *  'schedule()' is the scheduler function. This is GOOD CODE! There
 * probably won't be any reason to change this, as it should work well
 * in all circumstances (ie gives IO-bound processes good response etc).
 * The one thing you might take a look at is the signal-handler code here.
 *
 *   NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 */
 //调度函数
void schedule(void)
{
	int i,next,c;
	struct task_struct ** p;

/* check alarm, wake up any interruptible tasks that have got a signal */
	//从任务数组最后一个任务开始，检测alarm，循环时跳出空指针
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
		if (*p) {
			//如果设置过任务超时定时器timeout，并且已经超时，则复位超时定时器；
			//如果任务处于可中断睡眠状态，则将其置为就绪态
			if ((*p)->timeout && (*p)->timeout < jiffies) {
				(*p)->timeout = 0;
				if ((*p)->state == TASK_INTERRUPTIBLE)
					(*p)->state = TASK_RUNNING;
			}
			
			//如果设置过任务定时器alarm，并且已经过期，则在信号位图上置sigalm信号，即向任务发送sigalarm信号，
			//然后清alarm。该信号的默认操作是终止进程。
			if ((*p)->alarm && (*p)->alarm < jiffies) {
				(*p)->signal |= (1<<(SIGALRM-1));
				(*p)->alarm = 0;
			}
			
			//如果信号位图中除被阻塞的信号还有其它信号，并且任务处于可中断状态，则置任务为就绪状态
			if (((*p)->signal & ~(_BLOCKABLE & (*p)->blocked)) &&
			(*p)->state==TASK_INTERRUPTIBLE)
				(*p)->state=TASK_RUNNING;
		}

/* this is the scheduler proper: */

	while (1) {
		c = -1;
		next = 0;
		i = NR_TASKS;
		p = &task[NR_TASKS];
		//就绪态任务counter值，哪一个大，哪一个运行时间不长，next就指向哪个任务
		while (--i) {
			if (!*--p)
				continue;
			if ((*p)->state == TASK_RUNNING && (*p)->counter > c)
				c = (*p)->counter, next = i;
		}
		if (c) break; 
		//如果得到的counter值为0，即所有任务都执行完一遍，重新分配counter，然后再去比较
		for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
			if (*p)
				//counter的计算方式为counter / 2 + priority
				(*p)->counter = ((*p)->counter >> 1) +
						(*p)->priority;
	}
	//把当前任务指针counter指向任务号为next任务，并切换到该任务运行
	//next刚开始被初始化为0，若系统中没有其它任务运行，则next始终为0，
	//系统会在空闲时去执行任务0，
	switch_to(next);
}

//pause系统调用，转换当前的任务状态为可中断等待状态，并重新调度
int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return 0;
}

//不可中断睡眠的任务需要利用wake_up函数来明确唤醒
//可中断睡眠函数可以通过信号，任务超时等手段唤醒
static inline void __sleep_on(struct task_struct **p, int state)
{
	struct task_struct *tmp;

	if (!p)
		return;
	if (current == &(init_task.task))//当前任务为任务0，则死机？
		panic("task[0] trying to sleep");
	//让tmp指向已经在等待队列上的任务
	tmp = *p;
	*p = current;
	current->state = state;//*p指向原当前任务，它现在是新的等待任务；tmp指向原等待任务
	
repeat:	schedule();
	//只有当这个等待任务被唤醒时，程序才又会执行到这里，表示进程已经被明确的唤醒和执行。
	//如果等待队列中还有等待任务，并且队列头指针*p所指向的任务不为当前任务时，说明在本任务插入
	//等待队列后，还有任务插入等待队列。
	//则将等待队列头设置为就绪态，而自己设置为不可中断等待状态，即自己要等待这些后续进入队列
	//的任务被唤醒而执行时唤醒本任务。然后重新执行调度程序。
	if (*p && *p != current) {
		(**p).state = 0;
		current->state = TASK_UNINTERRUPTIBLE;
		goto repeat;
	}
	//执行到这里，说明本任务被真正唤醒执行，等待队列头即为本任务。然后我们把等待队列头
	//指向在我们前面进入队列的任务
	if (!*p)
		printk("Warning: *P = NULL\n\r");
	if (*p = tmp)
		tmp->state=0;
}

//设置当前任务为可中断的等待状态，并放入头指针*p指定的等待队列中
void interruptible_sleep_on(struct task_struct **p)
{
	__sleep_on(p,TASK_INTERRUPTIBLE);
}

//设置当前任务为可不中断的等待状态，并放入头指针*p指定的等待队列中
void sleep_on(struct task_struct **p)
{
	__sleep_on(p,TASK_UNINTERRUPTIBLE);
}

//唤醒*p指向的任务（等待队列头指针）
//由于新等待任务时插入在等待队列头指针处，因此唤醒的时最后进入等待队列的任务。
void wake_up(struct task_struct **p)
{
	if (p && *p) {
		if ((**p).state == TASK_STOPPED)
			printk("wake_up: TASK_STOPPED");
		if ((**p).state == TASK_ZOMBIE)
			printk("wake_up: TASK_ZOMBIE");
		(**p).state=0;
	}
}

/*
 * OK, here are some floppy things that shouldn't be in the kernel
 * proper. They are here because the floppy needs a timer, and this
 * was the easiest way of doing it.
 */
static struct task_struct * wait_motor[4] = {NULL,NULL,NULL,NULL};
static int  mon_timer[4]={0,0,0,0};
static int moff_timer[4]={0,0,0,0};
unsigned char current_DOR = 0x0C;

int ticks_to_floppy_on(unsigned int nr)
{
	extern unsigned char selected;
	unsigned char mask = 0x10 << nr;

	if (nr>3)
		panic("floppy_on: nr>3");
	moff_timer[nr]=10000;		/* 100 s = very big :-) */
	cli();				/* use floppy_off to turn it off */
	mask |= current_DOR;
	if (!selected) {
		mask &= 0xFC;
		mask |= nr;
	}
	if (mask != current_DOR) {
		outb(mask,FD_DOR);
		if ((mask ^ current_DOR) & 0xf0)
			mon_timer[nr] = HZ/2;
		else if (mon_timer[nr] < 2)
			mon_timer[nr] = 2;
		current_DOR = mask;
	}
	sti();
	return mon_timer[nr];
}

void floppy_on(unsigned int nr)
{
	cli();
	while (ticks_to_floppy_on(nr))
		sleep_on(nr+wait_motor);
	sti();
}

void floppy_off(unsigned int nr)
{
	moff_timer[nr]=3*HZ;
}

void do_floppy_timer(void)
{
	int i;
	unsigned char mask = 0x10;

	for (i=0 ; i<4 ; i++,mask <<= 1) {
		if (!(mask & current_DOR))
			continue;
		if (mon_timer[i]) {
			if (!--mon_timer[i])
				wake_up(i+wait_motor);
		} else if (!moff_timer[i]) {
			current_DOR &= ~mask;
			outb(current_DOR,FD_DOR);
		} else
			moff_timer[i]--;
	}
}

//最多可有64个定时器
#define TIME_REQUESTS 64

static struct timer_list {
	long jiffies;
	void (*fn)();
	struct timer_list * next;
} timer_list[TIME_REQUESTS], * next_timer = NULL;

void add_timer(long jiffies, void (*fn)(void))
{
	struct timer_list * p;

	if (!fn)
		return;
	cli();
	
	if (jiffies <= 0)
		(fn)();//若定时值小于等于0，则立刻调用其处理程序，并且该定时器不加入链表中
	else {
		//从定时器数组中，找一个空闲的项
		for (p = timer_list ; p < timer_list + TIME_REQUESTS ; p++)
			if (!p->fn)
				break;
				
		if (p >= timer_list + TIME_REQUESTS)
			panic("No more time requests free");
		p->fn = fn;
		p->jiffies = jiffies;
		p->next = next_timer;//next_timer执行最后一个插入的节点了
		next_timer = p;

		//链表项按定时值从小到大排序，
		while (p->next && p->next->jiffies < p->jiffies) {
			p->jiffies -= p->next->jiffies; //jiffies差值
			fn = p->fn;
			p->fn = p->next->fn;//交换fn
			p->next->fn = fn;
			
			jiffies = p->jiffies;//交换jffies
			p->jiffies = p->next->jiffies;
			p->next->jiffies = jiffies;
			p = p->next;
		}
	}
	sti();
}

//cpl:时钟中断发生时正被执行的代码选择符中的特权级
//0：表示中断发生时正在执行内核代码
//1:表示中断发生时正在执行用户代码
//
void do_timer(long cpl)
{
	static int blanked = 0;

	//屏幕
	if (blankcount || !blankinterval) {
		if (blanked)
			unblank_screen();//亮屏
		if (blankcount)
			blankcount--;
		blanked = 0;
	} else if (!blanked) {
		blank_screen();//黑屏
		blanked = 1;
	}

	//硬盘
	if (hd_timeout)
		if (!--hd_timeout)
			hd_times_out();//硬盘超时处理

	//扬声器
	if (beepcount)
		if (!--beepcount)
			sysbeepstop();

	//内核或者用户时间增加
	if (cpl)
		current->utime++;
	else
		current->stime++;

	//如果定时器存在，则将链表第一个定时器的值减1；如果以及等于0，则调用相应的处理程序
	//并去掉该定时器，next_timer（定时器链表头指针）指针指向下一个定时器；
	//此处值得学习
	if (next_timer) {
		next_timer->jiffies--;
		while (next_timer && next_timer->jiffies <= 0) {
			void (*fn)(void);
			
			fn = next_timer->fn;
			next_timer->fn = NULL;
			next_timer = next_timer->next;
			(fn)();
		}
	}
	//软盘定时程序
	if (current_DOR & 0xf0)
		do_floppy_timer();

	//如果进程时间还没运行完，则退出；否则置当前任务运行计数为0
	if ((--current->counter)>0) return;
	current->counter=0;
	if (!cpl) return;//若时钟中断发生时，在内核中运行，则直接返回，没有依赖counter进行调度
	schedule();//否则执行调度程序
}

//设置报警定时时间值
int sys_alarm(long seconds)
{
	int old = current->alarm;

	if (old)
		old = (old - jiffies) / HZ;
	current->alarm = (seconds>0)?(jiffies+HZ*seconds):0;
	return (old);
}

//获取当前进程pid
int sys_getpid(void)
{
	return current->pid;
}

//获取父进程ppiid
int sys_getppid(void)
{
	return current->p_pptr->pid;
}

//获取组进程ppiid
int sys_getuid(void)
{
	return current->uid;
}

//获取有效的用户进程号
int sys_geteuid(void)
{
	return current->euid;
}

//获取组号gid
int sys_getgid(void)
{
	return current->gid;
}

/获取有效组id
int sys_getegid(void)
{
	return current->egid;
}

//降低cpu的使用优先权
int sys_nice(long increment)
{
	if (current->priority-increment>0)
		current->priority -= increment;
	return 0;
}

/*
 *	功能: 内核调度初始化程序
 *	参数: error_code:进程写保护页由cpu产生异常而自动生成，错误类型
 *  address: 异常页面线性地址
 */
void sched_init(void)
{
	int i;
	struct desc_struct * p;//指向描述符表结构指针

	if (sizeof(struct sigaction) != 16)
		panic("Struct sigaction MUST be 16 bytes");

	//1、根据task0的配置参数在全局描述符表中初始化任务0的任务状态段描述符和局部数据描述符
	set_tss_desc(gdt+FIRST_TSS_ENTRY, &(init_task.task.tss));
	set_ldt_desc(gdt+FIRST_LDT_ENTRY, &(init_task.task.ldt));

	//清空任务数组和描述符表项
	p = gdt+2+FIRST_TSS_ENTRY;
	for(i=1;i<NR_TASKS;i++) {
		task[i] = NULL;
		p->a=p->b=0;
		p++;
		p->a=p->b=0;
		p++;
	}
	
/* Clear NT, so that we won't have troubles with that later on */
	__asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");//复位NT标志(嵌套调用)
	ltr(0); //将任务0的tss段选择符加载到tr寄存器中
	lldt(0);//将任务0的tldt段选择符加载到ldtr寄存器中

	//2、初始化8253定时器
	outb_p(0x36,0x43);		/* binary, mode 3, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */

	//3、设置时钟中断处理程序，并修改中断控制器屏蔽码，运行时钟中断
	set_intr_gate(0x20,&timer_interrupt);
	outb(inb_p(0x21)&~0x01,0x21);
	//设置系统调用中断门
	set_system_gate(0x80,&system_call);
}
