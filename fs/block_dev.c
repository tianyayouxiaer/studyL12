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

//�豸���ݿ�����ָ������
//ÿ��ָ����ָ�����豸�ŵ��ܿ�������hd_size[],
//���ܿ�������ÿһ���Ӧ���豸��ȷ����һ�����豸����ӵ�е����ݿ�������1���С=1KB��
extern int *blk_size[];

//���ݿ�д����-��ָ���豸�Ӹ���ƫ�ƴ�д��ָ����������
//������dev-�豸�ţ�pos-�豸�ļ���ƫ����ָ�룬buf-�û��ռ��л�������ַ��count-Ҫ���͵��ֽ���
//����ֵ�����͵��ֽ������ߴ����
//�����ں���˵��д����������ٻ�������д�����ݣ�ʲôʱ����������д���豸���ɸ��ٻ��������������
//�������豸���Կ�Ϊ��λ��д������дλ�ò�������ʼλ�ã����Ƚ���ʼ�ֽ����ڵ������������Ȼ����Ҫ
//д�����ݴ�д��ʼ����д���ÿ飬�ٽ�������һ������д�̣������ɸ��ٻ������������
int block_write(int dev, long * pos, char * buf, int count)
{
	//���ļ���λ��pos����ɿ�ʼ��д�̿�Ŀ����block
	//��Ҫд��һ���ֽ��ڸÿ��е�ƫ��offset
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
