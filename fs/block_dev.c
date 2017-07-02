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

	if (blk_size[MAJOR(dev)])
		size = blk_size[MAJOR(dev)][MINOR(dev)];
	else
		size = 0x7fffffff;
	while (count>0) {
		if (block >= size)
			return written?written:-EIO;
		chars = BLOCK_SIZE - offset;
		if (chars > count)
			chars=count;
		if (chars == BLOCK_SIZE)
			bh = getblk(dev,block);
		else
			bh = breada(dev,block,block+1,block+2,-1);
		block++;
		if (!bh)
			return written?written:-EIO;
		p = offset + bh->b_data;
		offset = 0;
		*pos += chars;
		written += chars;
		count -= chars;
		while (chars-->0)
			*(p++) = get_fs_byte(buf++);
		bh->b_dirt = 1;
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
