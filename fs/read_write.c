/*
 *  linux/fs/read_write.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/segment.h>

extern int rw_char(int rw,int dev, char * buf, int count, off_t * pos);
extern int read_pipe(struct m_inode * inode, char * buf, int count);
extern int write_pipe(struct m_inode * inode, char * buf, int count);
extern int block_read(int dev, off_t * pos, char * buf, int count);
extern int block_write(int dev, off_t * pos, char * buf, int count);
extern int file_read(struct m_inode * inode, struct file * filp,
		char * buf, int count);
extern int file_write(struct m_inode * inode, struct file * filp,
		char * buf, int count);

//重定位文件读写指针系统调用
//fd - 文件句柄；offset - 最新文件读写指针偏移；
//origin - 偏移起始位置:SEEK_SET(0,从文件开始处)、SEEK_SET(1,从文件读写位置)、SEEK_END(2,从文件尾处)0
int sys_lseek(unsigned int fd,off_t offset, int origin)
{
	struct file * file;
	int tmp;

	//参数有效性判断：文件句柄是否大于程序最多打开文件数；该文件句柄的文件结构指针为空？对应文件结构的i节点
	//字段为空？或者指定设备文件指针是不可定位的
	if (fd >= NR_OPEN || !(file=current->filp[fd]) || !(file->f_inode)
	   || !IS_SEEKABLE(MAJOR(file->f_inode->i_dev)))
		return -EBADF;
	//管道文件文件不能移动读写指针位置
	if (file->f_inode->i_pipe)
		return -ESPIPE;
	//根据设置的定位标志，分别重新定位文件读写指针
	switch (origin) {
		//origin = SEEK_SET,以文件起始处作为原点设置文件读写指针
		case 0:
			if (offset<0) return -EINVAL;//偏移小于0，则返回错误码
			file->f_pos=offset;//设置文件读写指针为offset
			break;
		case 1:
			//origin = SEEK_CUR,以文件当前读写指针处作为原点重定位读写指针
			if (file->f_pos+offset<0) return -EINVAL;//文件读写指针加offset小于0，错误
			file->f_pos += offset;//文件读写指针偏移offset
			break;
		case 2:
			//origin = SEEK_END,以文件末尾作为原点重定位读写指针
			if ((tmp=file->f_inode->i_size+offset) < 0)//文件大小加上offset小于0，错误
				return -EINVAL;
			file->f_pos = tmp;//否则重定位读写指针为问价长度加偏移值
			break;
		default:
			return -EINVAL;
	}
	//返回重定位后的文件读写指针值
	return file->f_pos;
}

//读文件系统调用
//fd - 文件句柄；buf - 缓冲区；count - 欲读写字节数
int sys_read(unsigned int fd,char * buf,int count)
{
	struct file * file;
	struct m_inode * inode;
	
	//参数有效性检查，如果进程句柄值大于程序最多打开文件个数NR_OPEN,或者需要写入的字节计数
	//小于0，或者该句柄的文件结构指针为空，则返回出错码并退出
	if (fd>=NR_OPEN || count<0 || !(file=current->filp[fd]))
		return -EINVAL;
		
	//如果需要读取的字节数count等于0，则返回0退出
	if (!count)
		return 0;

	//验证存放数据的缓冲区内存限制
	verify_area(buf,count);
	//取文件i节点
	inode = file->f_inode;
	//管道文件且为读管道模式
	if (inode->i_pipe)
		return (file->f_mode&1)?read_pipe(inode,buf,count):-EIO;
	//字符设备文件
	if (S_ISCHR(inode->i_mode))
		return rw_char(READ,inode->i_zone[0],buf,count,&file->f_pos);
	//块设备文件
	if (S_ISBLK(inode->i_mode))
		return block_read(inode->i_zone[0],&file->f_pos,buf,count);
	//目录和常规文件
	if (S_ISDIR(inode->i_mode) || S_ISREG(inode->i_mode)) {
		//验证读取字节数有效性，并进行调整
		if (count+file->f_pos > inode->i_size)
			count = inode->i_size - file->f_pos;
		//然后执行文件读操作
		if (count<=0)
			return 0;
		return file_read(inode,file,buf,count);
	}
	printk("(Read)inode->i_mode=%06o\n\r",inode->i_mode);
	return -EINVAL;
}

//写文件系统调用
//参数fd是文件句柄，buf是缓冲区，count是欲写字节数
int sys_write(unsigned int fd,char * buf,int count)
{
	struct file * file;
	struct m_inode * inode;

	//参数有效性检查，如果进程句柄值大于程序最多打开文件个数NR_OPEN,或者需要写入的字节计数
	//小于0，或者该句柄的文件结构指针为空，则返回出错码并退出
	if (fd>=NR_OPEN || count <0 || !(file=current->filp[fd]))
		return -EINVAL;

	//如果需要读取的字节数count等于0，则返回0退出
	if (!count)
		return 0;

	//验证存放数据缓冲区的限制。并取文件i节点。根据i节点属性，分别调用相应的读写操作函数
	inode=file->f_inode;
	if (inode->i_pipe)//管道
		return (file->f_mode&2)?write_pipe(inode,buf,count):-EIO;//管道且为写管道文件模式，则进行写管道操作
	if (S_ISCHR(inode->i_mode))//字符设备文件
		return rw_char(WRITE,inode->i_zone[0],buf,count,&file->f_pos);
	if (S_ISBLK(inode->i_mode))//块设备文件
		return block_write(inode->i_zone[0],&file->f_pos,buf,count);
	if (S_ISREG(inode->i_mode))//常规文件
		return file_write(inode,file,buf,count);
	printk("(Write)inode->i_mode=%06o\n\r",inode->i_mode);
	return -EINVAL;
}
