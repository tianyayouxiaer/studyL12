/*
 *  linux/fs/pipe.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <signal.h>
#include <errno.h>
#include <termios.h>

#include <linux/sched.h>
#include <linux/mm.h>	/* for get_free_page */
#include <asm/segment.h>
#include <linux/kernel.h>

//管道读操作函数
//参数inode是管道对应的i节点；buf - 用户缓冲区指针；count - 读取字节数
int read_pipe(struct m_inode * inode, char * buf, int count)
{
	int chars, size, read = 0;

	//循环执行读操作
	while (count>0) {
		//若管道中没有数据，则唤醒等待该节点的进程，该进程通常是写管道进程
		while (!(size=PIPE_SIZE(*inode))) {
			wake_up(& PIPE_WRITE_WAIT(*inode));//取管道中数据长度
			//如果已经没有写管道者，即i节点引用计数值小于2，则返回已读字节数退出
			if (inode->i_count != 2) /* are there any writers? */
				return read;
			//如果目前收到非阻塞信号，则立刻返回已读取字节数并退出
			if (current->signal & ~current->blocked)
				return read?read:-ERESTARTSYS;
			//让该进程在管道上睡眠，用以等待消息的到来
			interruptible_sleep_on(& PIPE_READ_WAIT(*inode));
		}
		//到此说明管道中有数据，于是我们取管道尾指针到缓冲区末端的字节数chars。
		chars = PAGE_SIZE-PIPE_TAIL(*inode);
		if (chars > count)		//如果其大于还需要读取的字节数count，则令其等于count。
			chars = count;
		if (chars > size)		//如果其大于当前管道中含有数据的长度size，则令其等于size。
			chars = size;
		//减去此次读取的字节数，并累加已读字节数read
		count -= chars;
		read += chars;

		//size指向管道尾指针处，并调用当前管道尾指针（前移chas个字节）
		size = PIPE_TAIL(*inode);
		PIPE_TAIL(*inode) += chars;
		//若尾指针超过管道末端则绕回，
		PIPE_TAIL(*inode) &= (PAGE_SIZE-1);
		//将管道中的数据复制到用户缓冲区中
		while (chars-->0)
			put_fs_byte(((char *)inode->i_size)[size++],buf++);
	}
	//此次管道操作结束，则唤醒等待该管道的进程，并返回读取的字节数
	wake_up(& PIPE_WRITE_WAIT(*inode));
	return read;
}

//写管道操作函数
//参数inode是管道对应的i节点，buf - 数据缓冲区指针，count - 将写入管道的字节数
int write_pipe(struct m_inode * inode, char * buf, int count)
{
	int chars, size, written = 0;

	//循环写入数据
	while (count>0) {
		//如当前管道已经满了（空闲空间size为0）,则唤醒等在该管道的进程，通常唤醒的是
		//读进程
		while (!(size=(PAGE_SIZE-1)-PIPE_SIZE(*inode))) {
			wake_up(& PIPE_READ_WAIT(*inode));
			//如果没有读管道者，则向进程发送SIGPIPE信号，并返回已写入的字节数并返回
			if (inode->i_count != 2) { /* no readers */
				current->signal |= (1<<(SIGPIPE-1));
				return written?written:-1;
			}
			//有读管道者，则让当前进程在管道上睡眠，以等待读管道进程来读数据
			sleep_on(& PIPE_WRITE_WAIT(*inode));
		}

		//获取管道可写空间size，即管道头指针到缓冲区末端空间字节数chars
		//更新count和written
		chars = PAGE_SIZE-PIPE_HEAD(*inode);
		if (chars > count)
			chars = count;
		if (chars > size)
			chars = size;
		count -= chars;
		written += chars;
		//size指向管道数据头指针处，并调整当前管道数据头指针（前移chars个字节）
		//若头指针超过管道末端则绕回，此处很精妙
		size = PIPE_HEAD(*inode);
		PIPE_HEAD(*inode) += chars;
		PIPE_HEAD(*inode) &= (PAGE_SIZE-1);
		//从用户缓冲区复制chars个字节到管道头指针处
		while (chars-->0)
			((char *)inode->i_size)[size++]=get_fs_byte(buf++);
	}
	//当此次写管道操作结束，则唤醒等待管道的进程，返回已写入的字节数
	wake_up(& PIPE_READ_WAIT(*inode));
	return written;
}

//创建管道系统调用
//在fildes所指定的数组中创建一对文件句柄（描述符），这对文件描述符指向一管道i节点
//filedes[0]用于读管道数据，filedes[1]用于写管道数据
//返回值：成功 - 0； 失败 - -1
int sys_pipe(unsigned long * fildes)
{
	struct m_inode * inode;
	struct file * f[2];//文件结构数组
	int fd[2];//文件句柄数组
	int i,j;

	j=0;
	//首先从系统文件表中取两空闲项（引用计数字段为0的项），并分别设置应用计数为0
	for(i=0;j<2 && i<NR_FILE;i++)
		if (!file_table[i].f_count)
			(f[j++]=i+file_table)->f_count++;//还有这种写法！！！
	//若只有1个空闲项，则释放该项（引用计数复位）
	if (j==1)
		f[0]->f_count=0;
	//若没有找到两个空闲项，则返回-1
	if (j<2)
		return -1;

	//针对上面取得的两个文件表结构项，分别分配一个文件句柄号
	//并使进程文件结构指针数组的两项分别指向这两个文件结构
	//而文件句柄即是该数组的索引号
	j=0;
	for(i=0;j<2 && i<NR_OPEN;i++)
		if (!current->filp[i]) {
			current->filp[ fd[j]=i ] = f[j];
			j++;
		}
	//如果只有一个空闲文件句柄，则释放该句柄（置空相应数组项）
	if (j==1)
		current->filp[fd[0]]=NULL;
	//如果没有找到两个空闲句柄，则释放上面获取的两个文件结构项
	if (j<2) {
		f[0]->f_count=f[1]->f_count=0;
		return -1;
	}

	//申请一个管道使用的i节点，并未管道分配一页内存作为缓冲区
	if (!(inode=get_pipe_inode())) {
		//如果不成功，则释放两个文件句柄和文件结构项
		current->filp[fd[0]] =
			current->filp[fd[1]] = NULL;
		f[0]->f_count = f[1]->f_count = 0;
		return -1;
	}

	//如果管道i节点申请成功，则对两个文件结构进行初始化操作 
	f[0]->f_inode = f[1]->f_inode = inode;//都指向同一个管道i节点
	f[0]->f_pos = f[1]->f_pos = 0;//读写指针都置0
	f[0]->f_mode = 1;		/* read */  //读
	f[1]->f_mode = 2;		/* write */ //写
	put_fs_long(fd[0],0+fildes);//将文件句柄数组复制到对应的用户空间数组中
	put_fs_long(fd[1],1+fildes);
	return 0;
}

//管道io控制函数
//pino - 管道i节点指针；cmd - 控制命令； arg - 参数
//返回0表示执行成功
int pipe_ioctl(struct m_inode *pino, int cmd, int arg)
{
	switch (cmd) {
		case FIONREAD:
			//如果命令是取管道中当前可读数据长度，则把管道数据长度值添入用户参数指定的位置处,并返回0
			//否则返回无效命令错误码
			verify_area((void *) arg,4);
			put_fs_long(PIPE_SIZE(*pino),(unsigned long *) arg);
			return 0;
		default:
			return -EINVAL;
	}
}
