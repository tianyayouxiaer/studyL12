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
	
int write_pipe(struct m_inode * inode, char * buf, int count)
{
	int chars, size, written = 0;

	while (count>0) {
		while (!(size=(PAGE_SIZE-1)-PIPE_SIZE(*inode))) {
			wake_up(& PIPE_READ_WAIT(*inode));
			if (inode->i_count != 2) { /* no readers */
				current->signal |= (1<<(SIGPIPE-1));
				return written?written:-1;
			}
			sleep_on(& PIPE_WRITE_WAIT(*inode));
		}
		chars = PAGE_SIZE-PIPE_HEAD(*inode);
		if (chars > count)
			chars = count;
		if (chars > size)
			chars = size;
		count -= chars;
		written += chars;
		size = PIPE_HEAD(*inode);
		PIPE_HEAD(*inode) += chars;
		PIPE_HEAD(*inode) &= (PAGE_SIZE-1);
		while (chars-->0)
			((char *)inode->i_size)[size++]=get_fs_byte(buf++);
	}
	wake_up(& PIPE_READ_WAIT(*inode));
	return written;
}

int sys_pipe(unsigned long * fildes)
{
	struct m_inode * inode;
	struct file * f[2];
	int fd[2];
	int i,j;

	j=0;
	for(i=0;j<2 && i<NR_FILE;i++)
		if (!file_table[i].f_count)
			(f[j++]=i+file_table)->f_count++;
	if (j==1)
		f[0]->f_count=0;
	if (j<2)
		return -1;
	j=0;
	for(i=0;j<2 && i<NR_OPEN;i++)
		if (!current->filp[i]) {
			current->filp[ fd[j]=i ] = f[j];
			j++;
		}
	if (j==1)
		current->filp[fd[0]]=NULL;
	if (j<2) {
		f[0]->f_count=f[1]->f_count=0;
		return -1;
	}
	if (!(inode=get_pipe_inode())) {
		current->filp[fd[0]] =
			current->filp[fd[1]] = NULL;
		f[0]->f_count = f[1]->f_count = 0;
		return -1;
	}
	f[0]->f_inode = f[1]->f_inode = inode;
	f[0]->f_pos = f[1]->f_pos = 0;
	f[0]->f_mode = 1;		/* read */
	f[1]->f_mode = 2;		/* write */
	put_fs_long(fd[0],0+fildes);
	put_fs_long(fd[1],1+fildes);
	return 0;
}

int pipe_ioctl(struct m_inode *pino, int cmd, int arg)
{
	switch (cmd) {
		case FIONREAD:
			verify_area((void *) arg,4);
			put_fs_long(PIPE_SIZE(*pino),(unsigned long *) arg);
			return 0;
		default:
			return -EINVAL;
	}
}
