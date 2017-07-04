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

	//дһ�����豸ʱ��д�������ݿ������ܳ���ָ���豸�������������ݿ�����
	if (blk_size[MAJOR(dev)])
		//ȡ��ָ���豸�Ŀ�����size
		size = blk_size[MAJOR(dev)][MINOR(dev)];
	else
		//���ϵͳ��û�ж��豸ָ�����ȣ���ʹ��Ĭ�ϳ��ȣ�2GB���飩
		size = 0x7fffffff;

	//���Ҫд����ֽ�����ѭ��������ֱ������ȫ��д��
	while (count>0) {
		//����ǰд�����ݵĿ���Ѿ����ڻ����ָ���豸���ܿ������򷵻��Ѿ�д����ֽ������˳�
		if (block >= size)
			return written?written:-EIO;

		//���㱾���д����ֽ���
		chars = BLOCK_SIZE - offset;//�����д����ֽ���
		//���Ҫд����ֽ���count���һ�飬��ô��ֻдcount���ֽ�
		if (chars > count)
			chars=count;
		//���Ҫд����ֽ���count�պ�Ϊһ�飬��ֱ������һ����ٻ�����
		if (chars == BLOCK_SIZE)
			bh = getblk(dev,block);
		else
		   //���뽫��д�����ݲ��ֵ����ݿ飬��Ԥ�����������ݿ�
			bh = breada(dev,block,block+1,block+2,-1);
			
		block++;
		if (!bh)
			return written?written:-EIO;
			
		//��ָ��pָ��������ݵĻ���鿪ʼд�����ݵ�λ�ô�
		p = offset + bh->b_data;
		//Ԥ������offsetΪ0
		offset = 0;
		//���ļ���ƫ��ָ��posǰ�ƴ˴ν�Ҫд���ֽ���chars
		*pos += chars;

		//�ۼ���ЩҪд����ֽ�����ͳ��ֵwritten��
		written += chars;
		//�ٰѻ���Ҫд�ļ���ֵcount��ȥ�˴�Ҫд���ֽ���chars
		count -= chars;

		//���û�����������chars���ֽڵ�pָ��ĸ��ٻ���鿪ʼд���λ��
		while (chars-->0)
			*(p++) = get_fs_byte(buf++);
		//����������øû����������޸ı�־
		bh->b_dirt = 1;
		//�ͷŸû������������������ü�����1
		brelse(bh);
	}
	return written;
}

//���ݿ������-��ָ���豸��λ�ô�����ָ���������ݵ��û�������
//dev - �豸��
//pos - �豸���ļ�ƫ��ָ��
//buf - �û��ռ��л�������ַ
//count - Ҫ������ֽ���
//�����Ѷ�����ֽ������ߴ�����
int block_read(int dev, unsigned long * pos, char * buf, int count)
{
	//���ļ���λ��pos����ɿ�ʼ��д�̿�Ŀ����block
	//��Ҫд��һ���ֽ��ڸÿ��е�ƫ��offset
	int block = *pos >> BLOCK_SIZE_BITS;
	int offset = *pos & (BLOCK_SIZE-1);
	int chars;
	int size;
	int read = 0;
	struct buffer_head * bh;
	register char * p;
	
	//��һ�����豸ʱ�����������ݿ������ܳ���ָ���豸�������������ݿ�����
	if (blk_size[MAJOR(dev)])
		size = blk_size[MAJOR(dev)][MINOR(dev)];
	else
		size = 0x7fffffff;

	//ѭ������count���ֽ�
	while (count>0) {
		//��ǰ�������ݵĿ���Ѿ����ڻ����ָ���豸���ܿ������򷵻��Ѷ�����ֽ���������
		if (block >= size)
			return read?read:-EIO;
		//�����ڵ�ǰ�������ݿ����������ֽ���
		//�����Ҫ������ֽ�������һ�飬��ô��ֻ��Ҫ��count���ֽ�
		chars = BLOCK_SIZE-offset;
		if (chars > count)
			chars = count;
		//���麯��breada������Ҫ�����ݿ飬��Ԥ�����������ݣ����ʧ�ܣ��򷵻��Ѷ��ֽ���
		if (!(bh = breada(dev,block,block+1,block+2,-1)))
			return read?read:-EIO;
		//��ŵ���1
		block++;

		//ָ��pָ������̿�Ļ�����п�ʼ��������λ�ô�
		p = offset + bh->b_data;
		//����һ��ѭ�������������ݲ����Կ飬����Ҫ�ӿ���ʼ����ȡ�����ֽ�
		offset = 0;
		*pos += chars;
		read += chars;//�ۼƶ�����ֽ���
		count -= chars;//��Ҫ���ļ���ֵcount��ȥ�˴�Ҫ�����ֽ���chars
		//�Ӹ��ٻ������pָ��Ŀ�ʼ����λ�ô�����chars���ֽڵ��û��������У�ͬʱ���û�������ǰ��
		while (chars-->0)
			put_fs_byte(*(p++), buf++);
		brelse(bh);//���θ�����ɺ��ͷŸû����
	}
	return read;
}
