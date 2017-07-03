/*
 *  linux/fs/block_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

//设备数据块总数指针数组
//每个指针项指向主设备号的总块数数组hd_size[],
//该总块数数组每一项对应子设备号确定的一个子设备上所拥有的数据块总数（1块大小=1KB）
extern int *blk_size[];

//数据块写函数-向指定设备从给定偏移处写入指定长度数据
//参数：dev-设备号，pos-设备文件中偏移量指针，buf-用户空间中缓冲区地址，count-要传送的字节数
//返回值：传送的字节数或者错误号
//对于内核来说，写操作是向高速缓冲区中写入数据；什么时候数据最终写入设备是由高速缓冲管理程序决定并
//处理；块设备是以块为单位读写，对于写位置不处块起始位置，需先将开始字节所在的整个块读出，然后将需要
//写的数据从写开始处填写满该块，再将完整的一块数据写盘（即交由高速缓冲程序区处理）
int block_write(int dev, long * pos, char * buf, int count)
{
	//由文件中位置pos换算成开始读写盘块的块序号block
	//需要写第一个字节在该块中的偏移offset
	int block = *pos >> BLOCK_SIZE_BITS;
	int offset = *pos & (BLOCK_SIZE-1);
	int chars;
	int written = 0;
	int size;
	struct buffer_head * bh;
	register char * p;

	//写一个块设备时，写的总数据块数不能超过指定设备上容许的最大数据块总数
	if (blk_size[MAJOR(dev)])
		//取出指定设备的块总数size
		size = blk_size[MAJOR(dev)][MINOR(dev)];
	else
		//如果系统中没有对设备指定长度，就使用默认长度（2GB个块）
		size = 0x7fffffff;

	//针对要写入的字节数，循环操作，直到数据全部写入
	while (count>0) {
		//若当前写入数据的块号已经大于或等于指定设备的总块数，则返回已经写入的字节数并退出
		if (block >= size)
			return written?written:-EIO;

		//计算本快可写入的字节数
		chars = BLOCK_SIZE - offset;//本块可写入的字节数
		//如果要写入的字节数count填不满一块，那么就只写count个字节
		if (chars > count)
			chars=count;
		//如果要写入的字节数count刚好为一块，则直接申请一块高速缓冲区
		if (chars == BLOCK_SIZE)
			bh = getblk(dev,block);
		else
		   //读入将被写入数据部分的数据块，并预读下两个数据块
			bh = breada(dev,block,block+1,block+2,-1);
			
		block++;
		if (!bh)
			return written?written:-EIO;
			
		//把指针p指向读出数据的缓冲块开始写入数据的位置处
		p = offset + bh->b_data;
		//预先设置offset为0
		offset = 0;
		//将文件中偏移指针pos前移此次将要写的字节数chars
		*pos += chars;

		//累计这些要写入的字节数到统计值written中
		written += chars;
		//再把还需要写的计数值count减去此次要写的字节数chars
		count -= chars;

		//从用户缓冲区复制chars个字节到p指向的高速缓冲块开始写入的位置
		while (chars-->0)
			*(p++) = get_fs_byte(buf++);
		//复制完后，设置该缓冲区块已修改标志
		bh->b_dirt = 1;
		//释放该缓冲区，及缓冲区引用计数减1
		brelse(bh);
	}
	return written;
}

int block_read(int dev, unsigned long * pos, char * buf, int count)
{
	int block = *pos >> BLOCK_SIZE_BITS;
	int offset = *pos & (BLOCK_SIZE-1);
	int chars;
	int size;
	int read = 0;
	struct buffer_head * bh;
	register char * p;

	if (blk_size[MAJOR(dev)])
		size = blk_size[MAJOR(dev)][MINOR(dev)];
	else
		size = 0x7fffffff;
	while (count>0) {
		if (block >= size)
			return read?read:-EIO;
		chars = BLOCK_SIZE-offset;
		if (chars > count)
			chars = count;
		if (!(bh = breada(dev,block,block+1,block+2,-1)))
			return read?read:-EIO;
		block++;
		p = offset + bh->b_data;
		offset = 0;
		*pos += chars;
		read += chars;
		count -= chars;
		while (chars-->0)
			put_fs_byte(*(p++),buf++);
		brelse(bh);
	}
	return read;
}
