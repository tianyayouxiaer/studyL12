/*
 *  linux/fs/open.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <utime.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>

#include <asm/segment.h>

//取文件系统信息
//参数：dev-已安装文件系统的设备号；ubuf - ustat结构缓冲区指针，用于存放系统返回给文件系统信息
int sys_ustat(int dev, struct ustat * ubuf)
{
	//还未实现
	return -ENOSYS;
}

//设置文件访问和修改时间
//参数： filename - 文件名， times - 访问和修改时间指针
//如果times指针不为NULL，则取utimbuf结构中的时间信息来设置文件的访问和修改时间
//如果times指针为NULL，则取系统当前时间来设置指定文件的访问和修改时间域
int sys_utime(char * filename, struct utimbuf * times)
{
	struct m_inode * inode;
	long actime,modtime;

	//文件的时间信息保存在其i节点钟，取目录名的i节点，如果目录名对应的i节点不存在，则返回错误码
	if (!(inode=namei(filename)))
		return -ENOENT;
	//如果times不为null，则从结构中读取用户设置的时间值
	//否则就用系统当前时间来设置文件的访问和修改时间
	if (times) {
		actime = get_fs_long((unsigned long *) &times->actime);
		modtime = get_fs_long((unsigned long *) &times->modtime);
	} else
		actime = modtime = CURRENT_TIME;

	//修改i节点中的访问字段和修改时间字段；再置i节点已修改标志，放回该i节点，并返回
	inode->i_atime = actime;
	inode->i_mtime = modtime;
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

/*
 * XXX should we use the real or effective uid?  BSD uses the real uid,
 * so as to make this call useful to setuid programs.
 */
//检查文件访问权限
//参数： filename - 文件名， mode - 检查的访问属性
//由3个有效位组成：R_OK(值4)，W_OK(值2)，X_OK(1)和F_OK(0)
//分别表示检测文件是否可读，可写、可执行和文件是否存在

int sys_access(const char * filename,int mode)
{
	struct m_inode * inode;
	int res, i_mode;

	//检测的访问属性有低3位组成，因此需要清除高位
	mode &= 0007;
	//获取i节点
	if (!(inode=namei(filename)))
		return -EACCES;
	//取i节点文件属性码，并放回该i节点
	i_mode = res = inode->i_mode & 0777;
	iput(inode);

	//如果当前进程用户是该文件宿主，则取文件宿主属性
	//否则如果当前进程用户与该文件宿主同属一组，则取文件组属性
	//否则，res最低3位为其它人访问该文件的许可属性
	if (current->uid == inode->i_uid)
		res >>= 6;
	else if (current->gid == inode->i_gid)
		res >>= 6;
	if ((res & 0007 & mode) == mode)
		return 0;
	/*
	 * XXX we are doing this test last because we really should be
	 * swapping the effective with the real user id (temporarily),
	 * and then calling suser() routine.  If we do call the
	 * suser() routine, it needs to be called last. 
	 */
	 //如果当前用户id为0，即超级用户，并且其屏蔽码执行位是0，或者文件可以被任何人执行、搜索，则返回0，否则返回错误码
	if ((!current->uid) &&
	    (!(mode & 1) || (i_mode & 0111)))
		return 0;
	return -EACCES;
}

//改变当前工作目录，把进程任务结构的当前工作目录字段指向给定目录名的i节点
int sys_chdir(const char * filename)
{
	struct m_inode * inode;
	//取目录名的i节点，如果目录名对应的i节点不存在，则返回错误码
	if (!(inode = namei(filename)))
		return -ENOENT;
	//如果该i节点不是目录i节点，则放回该i节点
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	//释放进程元工作目录i节点，并使其指向新设置的工作目录i节点
	iput(current->pwd);
	current->pwd = inode;
	return (0);
}

//改变根目录调用，改变当前进程任务结构中的根目录字段root，让其指向参数给定目录名的i节点
//把指定的目录设置成为当前进程的根目录“/”
int sys_chroot(const char * filename)
{
	struct m_inode * inode;

	//如果目录名对应的i节点不存在，则返回错误码
	if (!(inode=namei(filename)))
		return -ENOENT;
	//如果该i节点不是目录i节点，则放回该i节点
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	//释放当前进程根目录i节点，并重新设置为指定目录名的i节点
	iput(current->root);
	current->root = inode;
	return (0);
}

//修改文件属性,为指定文件设置新的访问属性mode
//参数：filename - 文件名；mode 文件属性
int sys_chmod(const char * filename,int mode)
{
	struct m_inode * inode;

	//取文件名对应的i节点，如果i节点不存在，则返回错误码
	if (!(inode=namei(filename)))
		return -ENOENT;
	//如果当前进程的有效用户id与文件i节点的用户id不同，并且也不是超级用户，则放回该i节点，返回出错码
	if ((current->euid != inode->i_uid) && !suser()) {
		iput(inode);
		return -EACCES;
	}
	//重新设置该i节点的文件属性，并置该i节点已修改标志，放回该i节点，返回0
	inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

//修改文件宿主系统调用,用于设置文件i节点中的用户和组id
//参数：filename - 文件名；uid - 用户id；gid - 组id
int sys_chown(const char * filename,int uid,int gid)
{
	struct m_inode * inode;

	//取得给定文件名的i节点，如果给定文件名的i节点不存在，则返回出错码（文件或者目录不存在）
	if (!(inode=namei(filename)))
		return -ENOENT;
	//如果当前进程不是超级用户，则放回该i节点，并返回错误码（没有访问权限）
	if (!suser()) {
		iput(inode);
		return -EACCES;
	}
	//用参数提供的值来设置文件i节点的用户id和组id，并置i节点已修改标志，放回该i节点
	inode->i_uid=uid;
	inode->i_gid=gid;
	inode->i_dirt=1;
	iput(inode);
	return 0;
}

//检查字符设备类型
//用于sys_open，用于检查若打开的文件是tty终端字符设备时，需要对当前进程的设置和对tty表设置
//返回：0-检查处理成功； -1 失败，对应字符设备不能打开
static int check_char_dev(struct m_inode * inode, int dev, int flag)
{
	struct tty_struct *tty;
	int min;

	//只处理主设备号是4((/dev/ttyxx)或5(/dev/tty)的情况;(/dev/tty)的设备号是0
	if (MAJOR(dev) == 4 || MAJOR(dev) == 5) {
		//如果打开文件是/dev/tty，那么min=进程任务结构中的tty字段，即取4号设备的子设备号。
		//若打开的是某个4号设备，则直接取其子设备号，
		if (MAJOR(dev) == 5)
			min = current->tty;
		else
		//如果一个进程有控制终端，则它是进程控制终端设备的同意名
		//即/dev/tty设备是一个虚拟设备，它对应到进程实际使用的/dev/ttyxx设备之一。
		//对于一个进程来说，若其有控制终端，那么它的任务结构中的tty字段将是4号设备的某一个子设备
			min = MINOR(dev);
			
		//如果得到的4号设备子设备号小于0，那么说明进程没有
		//控制终端，或者设备号错误，则返回-1，表示由于进程没有控制终端，或者不能打开这个设备
		if (min < 0)
			return -1;O_NONBLOCK

		//主伪终端设备文件只能被进程独占使用
		//如果子设备号表面是一个主伪终端设备，并且该打开文件i节点引用计数大于1，则说明该设备已
		//被其它进程使用。
		//因此不能打开该字符设备文件，因此返回-1
		if ((IS_A_PTY_MASTER(min)) && (inode->i_count>1))
			return -1;
		//tty指向tty表中对应结构项，
		tty = TTY_TABLE(min);
		//若打开文件操作标准flag中不含无需控制终端标志O_NOCTTY，
		//并且进程是进程组首领，并且当前进程没有控制终端,并且tty还不是任何进程组的控制终端
		//那么就允许为进程设置这个终端设备min为其控制终端
		if (!(flag & O_NOCTTY) &&
		    current->leader &&
		    current->tty<0 &&
		    tty->session==0) {
		    //于是设置进程任务结构终端设备号字段tty值等于min，并设置对应tty结构的会话号session和进程组号pgrp分别等于进程
		    //的会话号和进程组号
			current->tty = min;
			tty->session= current->session;
			tty->pgrp = current->pgrp;
		}
		//如果打开文件标志flag包含O_NONBLOCK，则我们需要对该字符终端设备进行相关设置
		//设置为满足读操作需要读取的最少字节数位0，超时时间为0，并把争端设置为非规范模式。
		if (flag & O_NONBLOCK) {
			TTY_TABLE(min)->termios.c_cc[VMIN] =0;
			TTY_TABLE(min)->termios.c_cc[VTIME] =0;
			TTY_TABLE(min)->termios.c_lflag &= ~ICANON;
		}
	}
	return 0;
}

//打开或创建文件系统调用
//filename - 文件名;
//flag - 打开标志；见fs.h
//mode - 文件许可属性 见stat.h
//对于新创建的文件，这些属性只应用于将来对文件的访问，创建了只读文件的打开调用也将返回一个可读写的文件句柄
//成功：返回文件句柄，失败，错误码
int sys_open(const char * filename,int flag,int mode)
{
	struct m_inode * inode;
	struct file * f;
	int i,fd;

	//用户设置文件模式与进程模式屏蔽码相与，产生许可的文件模式
	mode &= 0777 & ~current->umask;
	
	//为打开文件建立一个文件句柄，需要搜索进程结构中文件结构指针数组，以查找一个空闲项
	for(fd=0 ; fd<NR_OPEN ; fd++)
		if (!current->filp[fd])//找到空闲项
			break;
	if (fd>=NR_OPEN)
		return -EINVAL;

	//设置当前进程的执行时关闭文件句柄（close_on_exec）位图，复位对应的位
	//close_on_exec是进程所有文件句柄的位图标志，每个位代表一个打开着的文件描述符，用于确定在调用系统调用
	//execve()时需要关闭的文件句柄
	current->close_on_exec &= ~(1<<fd);//???

	f=0+file_table;// <=> file_table[0]

	//此时我们让进程对应文件句柄fd的文件结构指针指向搜索到的文件结构，并让文件引用计数递增1
	for (i=0 ; i<NR_FILE ; i++,f++)
		if (!f->f_count) break;
	if (i>=NR_FILE)
		return -EINVAL;
	//进程对应文件句柄fd的文件结构指针指向搜索到的文件结构，并令文件引用计数递增1
	(current->filp[fd]=f)->f_count++;
	//open_namei执行打开操作，若返回值小于0，则说明出错
	if ((i=open_namei(filename,flag,mode,&inode))<0) {
		//释放申请到的文件结构，返回出错码
		current->filp[fd]=NULL;
		f->f_count=0;
		return i;
	}

	//若文件打开操作成功，则inode是已打开文件i节点指针
/* ttys are somewhat special (ttyxx major==4, tty major==5) */
	//根据已打开文件i节点的属性字段，可以知道文件类型，对于不同类型文件，需要作特殊处理
	//如果是字符设备文件，则需要检查当前进程是否能打开这个字符设备文件
	if (S_ISCHR(inode->i_mode))
		//如果不允许打开使用该字符设备文件，则需要释放上面申请的文件项和句柄字面，返回出错码
		if (check_char_dev(inode,inode->i_zone[0],flag)) {
			iput(inode);
			current->filp[fd]=NULL;
			f->f_count=0;
			return -EAGAIN;
		}
/* Likewise with block-devices: check for floppy_change */
	if (S_ISBLK(inode->i_mode))
		check_disk_change(inode->i_zone[0]);
	f->f_mode = inode->i_mode;
	f->f_flags = flag;
	f->f_count = 1;
	f->f_inode = inode;
	f->f_pos = 0;
	return (fd);
}

//创建文件系统调用
int sys_creat(const char * pathname, int mode)
{
	return sys_open(pathname, O_CREAT | O_TRUNC, mode);
}

//关闭文件系统调用
//成功返回句柄文件，失败返回错误码
int sys_close(unsigned int fd)
{	
	struct file * filp;

	//检查参数有效性
	if (fd >= NR_OPEN)
		return -EINVAL;

	//复位进程执行时关闭文件句柄位图对应位
	current->close_on_exec &= ~(1<<fd);
	//若该文件句柄对应的文件结构指针是null，则返回错误码
	if (!(filp = current->filp[fd]))
		return -EINVAL;
	//置该文件句柄的文件结构指针为NULL，在关闭文件前，若对应文件结构中的句柄应用计数
	//已经为0，则说明内核错误
	current->filp[fd] = NULL;
	if (filp->f_count == 0)
		panic("Close: file count is 0");
	//对文件结构的引用计数减1，如果此时还不为0，则说明有其它进程正在使用该文件，于是返回0
	if (--filp->f_count)
		return (0);
	//若引用计数为0，则说明文件已经没有进程引用，该文件结构以变为空闲，则释放该文件i节点，返回0
	iput(filp->f_inode);
	return (0);
}
