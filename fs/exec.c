/*
 *  linux/fs/exec.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * #!-checking implemented by tytso.
 */

/*
 * Demand-loading implemented 01.12.91 - no need to read anything but
 * the header into memory. The inode of the executable is put into
 * "current->executable", and page faults do the actual loading. Clean.
 *
 * Once more I can proudly say that linux stood up to being changed: it
 * was less than 2 hours work to get demand-loading completely implemented.
 */

#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <a.out.h>

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/segment.h>

extern int sys_exit(int exit_code);
extern int sys_close(int fd);

/*
 * MAX_ARG_PAGES defines the number of pages allocated for arguments
 * and envelope for the new program. 32 should suffice, this gives
 * a maximum env+arg of 128kB !
 */
 //为新程序分配给参数和环境变量使用的最大内存页数
#define MAX_ARG_PAGES 32

//使用库文件系统调用
//为进程选择一个库文件，并替换进程当前库文件i节点字段值为这里指定库文件名的i节点指针
//如果library指针为空，则把进程当前的库文件释放掉
//参数： library - 库文件名
int sys_uselib(const char * library)
{
	struct m_inode * inode;
	unsigned long base;

	//首先通过查看当前进程空间长度来判断当前进程是否为普通进程
	//普通进程的空间长度被设置为TASK_SIZE(64MB).
	if (get_limit(0x17) != TASK_SIZE)
		return -EINVAL;
	//取库文件i节点inode
	if (library) {
		if (!(inode=namei(library)))		/* get library inode */
			return -ENOENT;
	} else
		inode = NULL;
		
/* we should check filetypes (headers etc), but we don't */
	//放回该库文件i节点，并预置进程库i节点字段为空
	iput(current->library);
	current->library = NULL;
	//取进程的库代码所在位置，并释放原库代码的也表和所占用的内存页面
	base = get_base(current->ldt[2]);
	base += LIBRARY_OFFSET;
	free_page_tables(base,LIBRARY_SIZE);
	//让进程库i节点字段指向新库i节点
	current->library = inode;
	return 0;
}

/*
 * create_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */
//在新任务栈中创建参数和环境变量指针表
//参数：p - 数据段中参数和环境信息偏移指针；argc - 参数个数；envc - 环境变量个数
//返回：栈指针值
static unsigned long * create_tables(char * p,int argc,int envc)
{
	unsigned long *argv,*envp;
	unsigned long * sp;

	//指针以4字节为边界进行寻址，因此这里让sp为4的整数倍值
	//此时sp位于参数环境表的末端。
	sp = (unsigned long *) (0xfffffffc & (unsigned long) p);
	//sp向下移动envc+1个字,多出的一个存放NULL值，在栈中空出环境变量指针占用的空间
	sp -= envc+1;
	//并让环境变量指针envp指向该处
	envp = sp;
	//sp继续下移argc+1个字,多出的一个存放NULL值
	sp -= argc+1;
	//并让argv指针指向该处
	argv = sp;
	//让后将环境参数块指针envp和命令行参数块指针已经命令行参数个数分别
	//压入栈中
	put_fs_long((unsigned long)envp,--sp);
	put_fs_long((unsigned long)argv,--sp);
	put_fs_long((unsigned long)argc,--sp);
	//再将命令行各参数指针和环境变量各指针分别放入前面空出的地方，最后放置一个NULL指针。
	while (argc-->0) {
		put_fs_long((unsigned long) p,argv++);
		while (get_fs_byte(p++)) /* nothing */ ;
	}
	put_fs_long(0,argv);//argv指向null
	while (envc-->0) {
		put_fs_long((unsigned long) p,envp++);
		while (get_fs_byte(p++)) /* nothing */ ;
	}
	put_fs_long(0,envp);//envp指向null
	//返回当前构造的新栈指针
	return sp;
}

/*
 * count() counts the number of arguments/envelopes
 */
 //用来计算参数个数
 //参数：argv - 参数指针数组,最后一个指针项为NULL
 //统计参数指针数组中指针的个数
static int count(char ** argv)
{
	int i=0;
	char ** tmp;

	if (tmp = argv)
		while (get_fs_long((unsigned long *) (tmp++)))
			i++;

	return i;
}

/*
 * 'copy_string()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 *
 * Modified by TYT, 11/24/91 to add the from_kmem argument, which specifies
 * whether the string and the string array are from user or kernel segments:
 * 
 * from_kmem     argv *        argv **
 *    0          user space    user space
 *    1          kernel space  user space
 *    2          kernel space  kernel space
 * 
 * We do this by playing games with the fs segment register.  Since it
 * it is expensive to load a segment register, we try to avoid calling
 * set_fs() unless we absolutely have to.
 */

//复制指定个数的参数字符串到参数和环境空间中（从用户内存空间复制参数/环境字符串到内核空闲页面中）
//参数：argc - 欲添加的参数个数；argv - 参数指针数组；page - 参数和环境空间页面指针数据
//		 p - 参数表空间中偏移指针，始终指向已复制串的头部；from_kmem - 字符串来源标志
// 		在do_execve()函数中，p 初始化为指向参数表(128kB)空间的最后一个长字处，参数字符串
// 		是以堆栈操作方式逆向往其中复制存放的，因此p 指针会始终指向参数字符串的头部。
// 		返回：参数和环境空间当前头部指针。
static unsigned long copy_strings(int argc,char ** argv,unsigned long *page,
		unsigned long p, int from_kmem)
{
	char *tmp, *pag;
	int len, offset = 0;
	unsigned long old_fs, new_fs;

	//偏移指针验证
	if (!p)
		return 0;	/* bullet-proofing */
	//取段寄存器ds（指向内核数据段）和寄存器fs值
	new_fs = get_ds();
	old_fs = get_fs();
	
	//若串（argv）及其指针（p）在内核空间则设置fs指向内核空间
	if (from_kmem==2)
		set_fs(new_fs);

	//循环处理各个参数，从最后一个参数逆向开始复制，复制到指定偏移地址处
	while (argc-- > 0) {
		//取需要复制的当前字符串指针
		//case1：若字符串在用户空间而字符串数组（字符串指针）在内核空间，则设置fs段寄存器指向内核数据段ds。
		//		  并在内核数据空间中取了字符串指针tmp之后立刻恢复fs段寄存器原值（fs再指回用户空间）。
		//case2：直接从用户空间去字符串指针到tmp
		if (from_kmem == 1)
			set_fs(new_fs);

		if (!(tmp = (char *)get_fs_long(((unsigned long *)argv)+argc)))
			panic("argc is wrong");
		if (from_kmem == 1)
			set_fs(old_fs);

		//从用户空间取该字符串，并计算该字符串长度len
		len=0;		/* remember zero-padding */
		do {
			len++;
		} while (get_fs_byte(tmp++));

		//如果该字符串长度超过此时参数和环境空间中还空闲的长度，则空间不够
		//于是恢复fs段寄存器值并返回0；但是参数和环境空间留有128KB空间，所以通常不
		//会发生这种情况
		if (p-len < 0) {	/* this shouldn't happen - 128kB */
			set_fs(old_fs);
			return 0;
		}

		//逆向逐个字符地把字符串复制到参数和环境空间末端处。
		while (len) {
			--p; --tmp; --len;
			if (--offset < 0) {//offset 为一个页面中当前指针偏移值，初始值为0
				offset = p % PAGE_SIZE;//offset被重新设置为在一个页面中的当前指针偏移值
				if (from_kmem==2)//若串在内核空间，则fs指回用户空间
					set_fs(old_fs);
				//若参数和环境空间中相应位置处没有内存页面，则为其先申请一页内存页面
				if (!(pag = (char *) page[p/PAGE_SIZE]) &&
				    !(pag = (char *) page[p/PAGE_SIZE] =
				      (unsigned long *) get_free_page())) 
					return 0;
				//若串在内核空间则fs指向内核空间
				if (from_kmem==2)
					set_fs(new_fs);

			}
			//从fs段中复制字符串的1个字节到参数和环境空间内存页面pag和offset处
			*(pag + offset) = get_fs_byte(tmp);
		}
	}
	//如果字符串和字符串数据在内核空间，则恢复fs段寄存器原值，最后，返回参数和
	//环境空间中已复制参数的头部偏移
	if (from_kmem==2)
		set_fs(old_fs);
	return p;
}

static unsigned long change_ldt(unsigned long text_size,unsigned long * page)
{
	unsigned long code_limit,data_limit,code_base,data_base;
	int i;

	code_limit = TASK_SIZE;
	data_limit = TASK_SIZE;
	code_base = get_base(current->ldt[1]);
	data_base = code_base;
	set_base(current->ldt[1],code_base);
	set_limit(current->ldt[1],code_limit);
	set_base(current->ldt[2],data_base);
	set_limit(current->ldt[2],data_limit);
/* make sure fs points to the NEW data segment */
	__asm__("pushl $0x17\n\tpop %%fs"::);
	data_base += data_limit - LIBRARY_SIZE;
	for (i=MAX_ARG_PAGES-1 ; i>=0 ; i--) {
		data_base -= PAGE_SIZE;
		if (page[i])
			put_dirty_page(page[i],data_base);
	}
	return data_limit;
}

/*
 * 'do_execve()' executes a new program.
 *
 * NOTE! We leave 4MB free at the top of the data-area for a loadable
 * library.
 */
 //函数执行一个新程序，加载并执行子进程
 //
 
int do_execve(unsigned long * eip,long tmp,char * filename,
	char ** argv, char ** envp)
{
	struct m_inode * inode;
	struct buffer_head * bh;
	struct exec ex;
	unsigned long page[MAX_ARG_PAGES];
	int i,argc,envc;
	int e_uid, e_gid;
	int retval;
	int sh_bang = 0;
	unsigned long p=PAGE_SIZE*MAX_ARG_PAGES-4;

	if ((0xffff & eip[1]) != 0x000f)
		panic("execve called from supervisor mode");
	for (i=0 ; i<MAX_ARG_PAGES ; i++)	/* clear page-table */
		page[i]=0;
	if (!(inode=namei(filename)))		/* get executables inode */
		return -ENOENT;
	argc = count(argv);
	envc = count(envp);
	
restart_interp:
	if (!S_ISREG(inode->i_mode)) {	/* must be regular file */
		retval = -EACCES;
		goto exec_error2;
	}
	i = inode->i_mode;
	e_uid = (i & S_ISUID) ? inode->i_uid : current->euid;
	e_gid = (i & S_ISGID) ? inode->i_gid : current->egid;
	if (current->euid == inode->i_uid)
		i >>= 6;
	else if (in_group_p(inode->i_gid))
		i >>= 3;
	if (!(i & 1) &&
	    !((inode->i_mode & 0111) && suser())) {
		retval = -ENOEXEC;
		goto exec_error2;
	}
	if (!(bh = bread(inode->i_dev,inode->i_zone[0]))) {
		retval = -EACCES;
		goto exec_error2;
	}
	ex = *((struct exec *) bh->b_data);	/* read exec-header */
	if ((bh->b_data[0] == '#') && (bh->b_data[1] == '!') && (!sh_bang)) {
		/*
		 * This section does the #! interpretation.
		 * Sorta complicated, but hopefully it will work.  -TYT
		 */

		char buf[128], *cp, *interp, *i_name, *i_arg;
		unsigned long old_fs;

		strncpy(buf, bh->b_data+2, 127);
		brelse(bh);
		iput(inode);
		buf[127] = '\0';
		if (cp = strchr(buf, '\n')) {
			*cp = '\0';
			for (cp = buf; (*cp == ' ') || (*cp == '\t'); cp++);
		}
		if (!cp || *cp == '\0') {
			retval = -ENOEXEC; /* No interpreter name found */
			goto exec_error1;
		}
		interp = i_name = cp;
		i_arg = 0;
		for ( ; *cp && (*cp != ' ') && (*cp != '\t'); cp++) {
 			if (*cp == '/')
				i_name = cp+1;
		}
		if (*cp) {
			*cp++ = '\0';
			i_arg = cp;
		}
		/*
		 * OK, we've parsed out the interpreter name and
		 * (optional) argument.
		 */
		if (sh_bang++ == 0) {
			p = copy_strings(envc, envp, page, p, 0);
			p = copy_strings(--argc, argv+1, page, p, 0);
		}
		/*
		 * Splice in (1) the interpreter's name for argv[0]
		 *           (2) (optional) argument to interpreter
		 *           (3) filename of shell script
		 *
		 * This is done in reverse order, because of how the
		 * user environment and arguments are stored.
		 */
		p = copy_strings(1, &filename, page, p, 1);
		argc++;
		if (i_arg) {
			p = copy_strings(1, &i_arg, page, p, 2);
			argc++;
		}
		p = copy_strings(1, &i_name, page, p, 2);
		argc++;
		if (!p) {
			retval = -ENOMEM;
			goto exec_error1;
		}
		/*
		 * OK, now restart the process with the interpreter's inode.
		 */
		old_fs = get_fs();
		set_fs(get_ds());
		if (!(inode=namei(interp))) { /* get executables inode */
			set_fs(old_fs);
			retval = -ENOENT;
			goto exec_error1;
		}
		set_fs(old_fs);
		goto restart_interp;
	}
	brelse(bh);
	if (N_MAGIC(ex) != ZMAGIC || ex.a_trsize || ex.a_drsize ||
		ex.a_text+ex.a_data+ex.a_bss>0x3000000 ||
		inode->i_size < ex.a_text+ex.a_data+ex.a_syms+N_TXTOFF(ex)) {
		retval = -ENOEXEC;
		goto exec_error2;
	}
	if (N_TXTOFF(ex) != BLOCK_SIZE) {
		printk("%s: N_TXTOFF != BLOCK_SIZE. See a.out.h.", filename);
		retval = -ENOEXEC;
		goto exec_error2;
	}
	if (!sh_bang) {
		p = copy_strings(envc,envp,page,p,0);
		p = copy_strings(argc,argv,page,p,0);
		if (!p) {
			retval = -ENOMEM;
			goto exec_error2;
		}
	}
/* OK, This is the point of no return */
/* note that current->library stays unchanged by an exec */
	if (current->executable)
		iput(current->executable);
	current->executable = inode;
	current->signal = 0;
	for (i=0 ; i<32 ; i++) {
		current->sigaction[i].sa_mask = 0;
		current->sigaction[i].sa_flags = 0;
		if (current->sigaction[i].sa_handler != SIG_IGN)
			current->sigaction[i].sa_handler = NULL;
	}
	for (i=0 ; i<NR_OPEN ; i++)
		if ((current->close_on_exec>>i)&1)
			sys_close(i);
	current->close_on_exec = 0;
	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
	if (last_task_used_math == current)
		last_task_used_math = NULL;
	current->used_math = 0;
	p += change_ldt(ex.a_text,page);
	p -= LIBRARY_SIZE + MAX_ARG_PAGES*PAGE_SIZE;
	p = (unsigned long) create_tables((char *)p,argc,envc);
	current->brk = ex.a_bss +
		(current->end_data = ex.a_data +
		(current->end_code = ex.a_text));
	current->start_stack = p & 0xfffff000;
	current->suid = current->euid = e_uid;
	current->sgid = current->egid = e_gid;
	eip[0] = ex.a_entry;		/* eip, magic happens :-) */
	eip[3] = p;			/* stack pointer */
	return 0;
exec_error2:
	iput(inode);
exec_error1:
	for (i=0 ; i<MAX_ARG_PAGES ; i++)
		free_page(page[i]);
	return(retval);
}
