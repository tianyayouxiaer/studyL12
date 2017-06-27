/*
 *  linux/kernel/signal.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <signal.h>
#include <errno.h>

 /*
 *	功能: 获取当前任务信号屏蔽位图 
 *  返回: 
 *	参数:
 */  
int sys_sgetmask()
{
	return current->blocked;
}

 /*
 *	功能: 设置当前任务信号屏蔽位图 ，SIGKILL和SIGSTOP不能被屏蔽
 *  返回: 原始信号屏蔽位图
 *	参数:
 */
int sys_ssetmask(int newmask)
{
	int old=current->blocked;

	current->blocked = newmask & ~(1<<(SIGKILL-1)) & ~(1<<(SIGSTOP-1));
	return old;
}

int sys_sigpending(sigset_t *set)
{
    /* fill in "set" with signals pending but blocked. */
    verify_area(set,4);
    put_fs_long(current->blocked & current->signal, (unsigned long *)set);
    return 0;
}

/* atomically swap in the new signal mask, and wait for a signal.
 *
 * we need to play some games with syscall restarting.  We get help
 * from the syscall library interface.  Note that we need to coordinate
 * the calling convention with the libc routine.
 *
 * "set" is just the sigmask as described in 1003.1-1988, 3.3.7.
 * 	It is assumed that sigset_t can be passed as a 32 bit quantity.
 *
 * "restart" holds a restart indication.  If it's non-zero, then we 
 * 	install the old mask, and return normally.  If it's zero, we store 
 * 	the current mask in old_mask and block until a signal comes in.
 */
int sys_sigsuspend(int restart, unsigned long old_mask, unsigned long set)
{
    extern int sys_pause(void);

    if (restart) {
	/* we're restarting */
	current->blocked = old_mask;
	return -EINTR;
    }
    /* we're not restarting.  do the work */
    *(&restart) = 1;
    *(&old_mask) = current->blocked;
    current->blocked = set;
    (void) sys_pause();			/* return after a signal arrives */
    return -ERESTARTNOINTR;		/* handle the signal, and come back */
}

 /*
 *	功能: 复制sigaction数据到fs段to处
 *  返回: 
 *	参数:
 */
static inline void save_old(char * from,char * to)
{
	int i;

	verify_area(to, sizeof(struct sigaction));//验证to处内存是否足够
	for (i=0 ; i< sizeof(struct sigaction) ; i++) {
		put_fs_byte(*from,to);//复制到fs段，一般是用户段
		from++;
		to++;
	}
}

 /*
 *	功能: 把sigacion数据段from位置复制到to处
 *  返回: 
 *	参数:
 */
static inline void get_new(char * from,char * to)
{
	int i;

	for (i=0 ; i< sizeof(struct sigaction) ; i++)
		*(to++) = get_fs_byte(from++);
}

 /*
 *	功能: 信号处理系统调用，为指定的信号安装信号处理程序 
 *  返回: 
 *	参数:signum - 指定的信号；handler-指定的句柄；restorer-恢复函数指针，由libc提供
 */
int sys_signal(int signum, long handler, long restorer)
{
	struct sigaction tmp;

	if (signum<1 || signum>32 || signum==SIGKILL || signum==SIGSTOP)
		return -EINVAL;
	tmp.sa_handler = (void (*)(int)) handler;
	tmp.sa_mask = 0;
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK;//该句柄只使用一次后就恢复到默认值；并允许信号在自己的处理句柄中收到
	tmp.sa_restorer = (void (*)(void)) restorer;//恢复函数
	handler = (long) current->sigaction[signum-1].sa_handler;//返回的是之前的handler，并更当前进程的sigaction
	current->sigaction[signum-1] = tmp;
	return handler;
}

 /*
 *	功能: 修改进程在收到特定信号时所采取的行动 
 *  返回: 
 *	参数:
 */
int sys_sigaction(int signum, const struct sigaction * action,
	struct sigaction * oldaction)
{
	struct sigaction tmp;

	if (signum<1 || signum>32 || signum==SIGKILL || signum==SIGSTOP)
		return -EINVAL;
	tmp = current->sigaction[signum-1];//在信号segaction结构中设置新的动作
	get_new((char *) action,
		(char *) (signum-1+current->sigaction));
	if (oldaction)
		save_old((char *) &tmp,(char *) oldaction);
	if (current->sigaction[signum-1].sa_flags & SA_NOMASK)//如果允许信号在自己信号句柄中收到，则令屏蔽码为0，否则设备屏蔽本信号。
		current->sigaction[signum-1].sa_mask = 0;
	else
		current->sigaction[signum-1].sa_mask |= (1<<(signum-1));
	return 0;
}

/*
 * Routine writes a core dump image in the current directory.
 * Currently not implemented.
 */
int core_dump(long signr)
{
	return(0);	/* We didn't do a dump */
}
 /*
 *	功能: 系统调用中断处理程序；将信号的处理句柄插入到用户堆栈中，
 *		  并在系统调用结束发回后立刻执行信号句柄处理程序, 然后继续执行用户的程序。
 *  返回: 
 *	参数:
 */
int do_signal(long signr,long eax,long ebx, long ecx, long edx, long orig_eax,
	long fs, long es, long ds,
	long eip, long cs, long eflags,
	unsigned long * esp, long ss)
{
	unsigned long sa_handler;
	long old_eip=eip;
	struct sigaction * sa = current->sigaction + signr - 1;//current->sigaction[signr - 1]
	int longs;

	unsigned long * tmp_esp;

#ifdef notdef
	printk("pid: %d, signr: %x, eax=%d, oeax = %d, int=%d\n", 
		current->pid, signr, eax, orig_eax, 
		sa->sa_flags & SA_INTERRUPT);
#endif

	if ((orig_eax != -1) &&
	    ((eax == -ERESTARTSYS) || (eax == -ERESTARTNOINTR))) {
		if ((eax == -ERESTARTSYS) && ((sa->sa_flags & SA_INTERRUPT) ||
		    signr < SIGCONT || signr > SIGTTOU))
			*(&eax) = -EINTR;
		else {
			*(&eax) = orig_eax;
			*(&eip) = old_eip -= 2;
		}
	}
	sa_handler = (unsigned long) sa->sa_handler;
	if (sa_handler==1)
		return(1);   /* Ignore, see if there are more signals... */
	if (!sa_handler) {
		switch (signr) {
		case SIGCONT://子进程停止或结束
		case SIGCHLD://恢复进程的执行
			return(1);  /* Ignore, ... */

		case SIGSTOP://停止进程运行
		case SIGTSTP://停止进程运行
		case SIGTTIN://停止进程运行
		case SIGTTOU://停止进程运行
			current->state = TASK_STOPPED;
			current->exit_code = signr;
			if (!(current->p_pptr->sigaction[SIGCHLD-1].sa_flags & 
					SA_NOCLDSTOP))
				current->p_pptr->signal |= (1<<(SIGCHLD-1));
			return(1);  /* Reschedule another event */

		case SIGQUIT://进程被终止，并产生dump core文件
		case SIGILL:
		case SIGTRAP:
		case SIGIOT:
		case SIGFPE:
		case SIGSEGV:
			if (core_dump(signr))
				do_exit(signr|0x80);
			/* fall through */
		default:
			do_exit(signr);
		}
	}
	/*
	 * OK, we're invoking a handler 
	 */
	if (sa->sa_flags & SA_ONESHOT)//如果该句柄只被使用一次，则将句柄置空
		sa->sa_handler = NULL;
	*(&eip) = sa_handler;//将用户系统调用的代码指针eip指向该信号处理句柄
	longs = (sa->sa_flags & SA_NOMASK)?7:8;
	*(&esp) -= longs;//将原调用程序的用户堆栈指针向下扩展7或8个字节	
	verify_area(esp,longs*4);//用户堆栈中从下到上依次存放sa_restorer，signr...
	tmp_esp=esp;
	put_fs_long((long) sa->sa_restorer,tmp_esp++);
	put_fs_long(signr,tmp_esp++);
	if (!(sa->sa_flags & SA_NOMASK))
		put_fs_long(current->blocked,tmp_esp++);
	put_fs_long(eax,tmp_esp++);
	put_fs_long(ecx,tmp_esp++);
	put_fs_long(edx,tmp_esp++);
	put_fs_long(eflags,tmp_esp++);
	put_fs_long(old_eip,tmp_esp++);
	current->blocked |= sa->sa_mask;
	return(0);		/* Continue, execute handler */
}
