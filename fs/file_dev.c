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

//ȡa��b�е���Сֵ
#define MIN(a,b) (((a)<(b))?(a):(b))
//ȡ����b�����ֵ
#define MAX(a,b) (((a)>(b))?(a):(b))

//�ļ������� - ����i�ڵ���ļ��ṹ����ȡ�ļ�������
//��i�ڵ㣬���ǿ���֪���豸�ţ���flip�ṹ����֪���ļ��еĶ�дλ�á�
//buf - ָ���û��ռ��л�����λ��
//count - ��Ҫ��ȡ���ֽ���
//���� - ʵ�ʶ�ȡ���ֽ������ߴ����
int file_read(struct m_inode * inode, struct file * filp, char * buf, int count)
{
	int left,chars,nr;
	struct buffer_head * bh;
	//�����жϲ�����Ч�ԣ����Ҫ��ȡ���ֽڼ���countС�ڵ����㣬�򷵻�0
	if ((left=count)<=0)
		return 0;
		
	//������Ҫ��ȡ���ֽ���������0����ѭ����ȡ
	while (left) {
		//����i�ڵ���ļ���ṹ��Ϣ��������bmap�õ������ļ���ǰ��дλ�õ����ݿ����豸�ϵĶ�Ӧ���߼����nr
		//(filp->f_pos)/BLOCK_SIZE)�����ļ���ǰָ���������ݿ��
		//��nr��Ϊ0�����i�ڵ�ָ�����豸�϶�ȡ���߼��飬�����ʧ�ܣ����˳�ѭ��
		//��nrΪ0����ʾָ�������ݿ鲻���ڣ��û����ָ��ΪNULL
		if (nr = bmap(inode, (filp->f_pos)/BLOCK_SIZE)) {
			if (!(bh=bread(inode->i_dev,nr)))
				break;
		} else
			bh = NULL;

		//�����ļ���дָ�������ݿ��е�ƫ��ֵnr�����ڸ����ݿ��У�����ϣ����ȡ���ֽ���λ��BLOCK_SIZE - nr��
		nr = filp->f_pos % BLOCK_SIZE;
		//����Ҫ��ȡ���ֽ�ΪBLOCK_SIZE-nr�ͻ����ȡ�ֽ���left���߽�Сֵ
		chars = MIN( BLOCK_SIZE-nr , left );//��BLOCK_SIZE-nr > left����˵���ÿ�����Ҫ��ȡ�����һ������
		filp->f_pos += chars;//������д�ļ�ָ��
		left -= chars;//����ʣ���ֽ���

		//�����豸�϶��������ݣ���pָ�򻺳�鿪ʼ��ȡ���ݵ�λ�ã�������chars���ֽڵ��û�������buf��
		//�������û�����������chars��0ֵ
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
	//�޸ĸ�i�ڵ�ķ���ʱ��Ϊ��ǰʱ��
	inode->i_atime = CURRENT_TIME;

	//���ض�ȡ���ֽ����������
	return (count-left)?(count-left):-ERROR;
}

//�ļ�д���� - ����i�ڵ���ļ��ṹ��Ϣ�����û�����д���ļ���
//��i�ڵ����֪���豸��
//��file�ṹ����֪���ļ��е�ǰ��дָ��λ��
//bufָ���û�̬������λ��
//countΪ��Ҫд����ֽ���
//����ֵΪʵ��д����ֽ���������
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
 	//���������ͬʱдʱ��
 	//����ȷ������д���ļ���λ�ã������Ҫ���ļ���������ݣ����ļ���дָ���Ƶ��ļ�β��
 	//����ͽ����ļ���ǰ��дָ�봦д��
	if (filp->f_flags & O_APPEND)
		pos = inode->i_size;
	else
		pos = filp->f_pos;

	//ѭ��д������
	while (i<count) {
		//ȡ�ļ����ݿ飨pos/BLOCK_SIZE�����豸�϶�Ӧ���߼����block�������Ӧ���߼���Ų����ڣ��ʹ���һ��
		if (!(block = create_block(inode,pos/BLOCK_SIZE)))//�߼����Ϊ0������ʧ��
			break;
		//�����߼���Ŷ�ȡ�豸�ϵ���Ӧ�߼���
		if (!(bh=bread(inode->i_dev,block)))
			break;

		c = pos % BLOCK_SIZE;//��ȡ�ļ���ǰ��дָ���ڸ����ݿ��е�ƫ��ֵc
		p = c + bh->b_data;//pָ�򻺳���п�ʼд�����ݵ�λ��
		bh->b_dirt = 1;//�û�������޸ı�־
		c = BLOCK_SIZE-c;
		if (c > count-i) c = count-i;//��c����ʣ�໹��д����ֽ���count-i����˴�ֻ����д��count-i
		pos += c;//��һ��ѭ������Ҫ��д�ļ��е�λ�ã�posָ��ǰ�ƴ˴���д����ֽ���
		//����ʱposλ�ó������ļ���ǰ���ȣ����޸�i�ڵ����ļ������ֶΣ�����i�ڵ����޸ı�־
		if (pos > inode->i_size) {
			inode->i_size = pos;
			inode->i_dirt = 1;
		}
		//�˴�Ҫд����ֽ���c�ۼӵ���д���ֽڼ���ֵi�У�
		i += c;
		while (c-->0)
			*(p++) = get_fs_byte(buf++);//�Ӹ��ٻ����и���c���ֽڵ����ٻ����pָ��Ŀ�ʼλ�ô�
		brelse(bh);//�ͷŸû����
	}

	//����ȫ��д����߲������̷������⣬���˳�����ѭ��
	inode->i_mtime = CURRENT_TIME;
	
	//����˴β����������ļ�β��������ݣ�����ļ���дָ���������ǰ��дλ��pos����
	//�������ļ�i�ڵ���޸�ʱ��Ϊ��ǰʱ��
	if (!(filp->f_flags & O_APPEND)) {
		filp->f_pos = pos;
		inode->i_ctime = CURRENT_TIME;
	}
	
	//����д����ֽ�����-1
	return (i?i:-1);
}
