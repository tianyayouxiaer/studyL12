/*
 *  linux/fs/file_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <fcntl.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

//取a，b中的最小值
#define MIN(a,b) (((a)<(b))?(a):(b))
//取啊，b中最大值
#define MAX(a,b) (((a)>(b))?(a):(b))

//文件读函数 - 根据i节点和文件结构，读取文件中数据
//由i节点，我们可以知道设备号，由flip结构可以知道文件中的读写位置。
//buf - 指定用户空间中缓冲区位置
//count - 需要读取的字节数
//返回 - 实际读取的字节数或者错误号
int file_read(struct m_inode * inode, struct file * filp, char * buf, int count)
{
	int left,chars,nr;
	struct buffer_head * bh;
	//首先判断参数有效性，如果要读取的字节计数count小于等于零，则返回0
	if ((left=count)<=0)
		return 0;
		
	//若还需要读取的字节数不等于0，就循环读取
	while (left) {
		//根据i节点和文件表结构信息，并利用bmap得到包含文件当前读写位置的数据块在设备上的对应的逻辑块号nr
		//(filp->f_pos)/BLOCK_SIZE)计算文件当前指针所在数据块号
		//若nr不为0，则从i节点指定的设备上读取该逻辑块，如果读失败，则退出循环
		//若nr为0，表示指定的数据块不存在，置缓冲块指针为NULL
		if (nr = bmap(inode, (filp->f_pos)/BLOCK_SIZE)) {
			if (!(bh=bread(inode->i_dev,nr)))
				break;
		} else
			bh = NULL;

		//计算文件读写指针在数据块中的偏移值nr，则在该数据块中，我们希望读取的字节数位（BLOCK_SIZE - nr）
		nr = filp->f_pos % BLOCK_SIZE;
		//本次要读取的字节为BLOCK_SIZE-nr和还需读取字节数left两者较小值
		chars = MIN( BLOCK_SIZE-nr , left );//若BLOCK_SIZE-nr > left，则说明该块是需要读取的最后一块数据
		filp->f_pos += chars;//调整读写文件指针
		left -= chars;//计算剩余字节数

		//若从设备上读到了数据，则将p指向缓冲块开始读取数据的位置，并复制chars个字节到用户缓冲区buf中
		//否则向用户缓冲区填入chars个0值
		if (bh) {
			char * p = nr + bh->b_data;
			while (chars-->0)
				put_fs_byte(*(p++),buf++);
			brelse(bh);
		} else {
			while (chars-->0)
				put_fs_byte(0,buf++);
		}
	}
	//修改该i节点的访问时间为当前时间
	inode->i_atime = CURRENT_TIME;

	//返回读取的字节数或出错码
	return (count-left)?(count-left):-ERROR;
}

//文件写函数 - 根据i节点和文件结构信息，将用户数据写入文件中
//由i节点可以知道设备号
//由file结构可以知道文件中当前读写指针位置
//buf指定用户态缓冲区位置
//count为需要写入的字节数
//返回值为实际写入的字节数或出错号
int file_write(struct m_inode * inode, struct file * filp, char * buf, int count)
{
	off_t pos;
	int block,c;
	struct buffer_head * bh;
	char * p;
	int i=0;

/*
 * ok, append may not work when many processes are writing at the same time
 * but so what. That way leads to madness anyway.
 */
 	//当多个进程同时写时？
 	//首先确定数据写入文件的位置，如果是要向文件后添加数据，则将文件读写指针移到文件尾部
 	//否则就将在文件当前读写指针处写入
	if (filp->f_flags & O_APPEND)
		pos = inode->i_size;
	else
		pos = filp->f_pos;

	//循环写入数据
	while (i<count) {
		//取文件数据块（pos/BLOCK_SIZE）在设备上对应的逻辑块号block，如果对应的逻辑块号不存在，就创建一块
		if (!(block = create_block(inode,pos/BLOCK_SIZE)))//逻辑块号为0，创建失败
			break;
		//根据逻辑块号读取设备上的相应逻辑块
		if (!(bh=bread(inode->i_dev,block)))
			break;

		c = pos % BLOCK_SIZE;//获取文件当前读写指针在该数据块中的偏移值c
		p = c + bh->b_data;//p指向缓冲块中开始写入数据的位置
		bh->b_dirt = 1;//置缓冲块已修改标志
		c = BLOCK_SIZE-c;
		if (c > count-i) c = count-i;//若c大于剩余还需写入的字节数count-i，则此次只需再写入count-i
		pos += c;//下一次循环操作要读写文件中的位置，pos指针前移此次需写入的字节数
		//若此时pos位置超过了文件当前长度，则修改i节点中文件长度字段，并置i节点已修改标志
		if (pos > inode->i_size) {
			inode->i_size = pos;
			inode->i_dirt = 1;
		}
		//此次要写入的字节数c累加到已写入字节计数值i中，
		i += c;
		while (c-->0)
			*(p++) = get_fs_byte(buf++);//从高速缓冲中复制c个字节到高速缓冲块p指向的开始位置处
		brelse(bh);//释放该缓冲块
	}

	//数据全部写入或者操作过程发生问题，会退出上面循环
	inode->i_mtime = CURRENT_TIME;
	
	//如果此次操作不是在文件尾部添加数据，则把文件读写指针调整到当前读写位置pos处，
	//并更改文件i节点的修改时间为当前时间
	if (!(filp->f_flags & O_APPEND)) {
		filp->f_pos = pos;
		inode->i_ctime = CURRENT_TIME;
	}
	
	//返回写入的字节数或-1
	return (i?i:-1);
}
