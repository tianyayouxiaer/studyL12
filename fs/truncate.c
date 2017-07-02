/*
 *  linux/fs/truncate.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>

#include <sys/stat.h>

//�ͷ�����һ�μ�ӿ�
//dev - �ļ�ϵͳ�����豸���豸��
//block - �߼����
static int free_ind(int dev,int block)
{
	struct buffer_head * bh;
	unsigned short * p;
	int i;
	int block_busy;//�߼���û�б��ͷŵı�־

	//�߼���0���򷵻�
	if (!block)
		return 1;

	//��ȡһ�μ�ӿ飬���ͷŷ����ϱ���ʹ�õ������߼��飬Ȼ���ͷŸ�һ�μ�ӿ���߼���
	block_busy = 0;
	if (bh=bread(dev,block)) {
		p = (unsigned short *) bh->b_data;//ָ�򻺳��������
		for (i=0;i<512;i++,p++)//ÿ���߼����Ͽ���512�����
			if (*p)
				if (free_block(dev,*p)) {//�ͷ�ָ�����豸�߼���
					*p = 0;
					bh->b_dirt = 1;
				} else
					block_busy = 1;
		brelse(bh); //�ͷż�ӿ�ռ�õĻ����
	}
	//����ͷ��豸�ϵ�һ�μ�ӿ飬������������߼���û�б��ͷţ����ջ�0��ʧ��
	if (block_busy)
		return 0;
	else
		return free_block(dev,block);
}

//�ͷ����ж��μ�ӿ�
//dev - �ļ�ϵͳ�����豸���豸��
//block - �߼����
static int free_dind(int dev,int block)
{
	struct buffer_head * bh;
	unsigned short * p;
	int i;
	int block_busy;

	if (!block)
		return 1;

	//��ȡ���μ�ӿ��һ���飬���ͷ����ϱ���ʹ�õ������߼��飬Ȼ���ͷŸ�һ����Ļ����
	block_busy = 0;
	if (bh=bread(dev,block)) {
		p = (unsigned short *) bh->b_data;
		for (i=0;i<512;i++,p++)
			if (*p)
				if (free_ind(dev,*p)) {
					*p = 0;
					bh->b_dirt = 1;
				} else
					block_busy = 1;
		brelse(bh);
	}
	if (block_busy)
		return 0;
	else
		return free_block(dev,block);
}

//�ض��ļ����ݳ���
//���ڵ��Ӧ���ļ����Ƚ�Ϊ0�����ͷ�ռ�õ��豸�ռ�
void truncate(struct m_inode * inode)
{
	int i;
	int block_busy;

	//������ǳ����ļ���Ŀ¼�ļ���������򷵻�
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	     S_ISLNK(inode->i_mode)))
		return;
repeat:
	//�ͷŽڵ��7��ֱ���߼��飬������7���߼���ȫ��0
	block_busy = 0;
	for (i=0;i<7;i++)
		if (inode->i_zone[i]) {
			//������߼���æ��û�б��ͷţ����ÿ�æ��־
			if (free_block(inode->i_dev,inode->i_zone[i]))
				inode->i_zone[i]=0;
			else
				block_busy = 1;
		}
	//�ͷ����е�һ�μ�ӿ�
	if (free_ind(inode->i_dev,inode->i_zone[7]))
		inode->i_zone[7] = 0;
	else
		block_busy = 1;
	//�ͷ����еĶ��μ�ӿ�
	if (free_dind(inode->i_dev,inode->i_zone[8]))
		inode->i_zone[8] = 0;
	else
		block_busy = 1;

	//����i�ڵ����޸ı�־��������������߼�������æôû���ͷţ����
	//��ǰ����ʱ��Ƭ��0���������̣��Ե�һ�ᣬ������ִ���ͷŲ���
	inode->i_dirt = 1;
	if (block_busy) {
		current->counter = 0;
		schedule();
		goto repeat;
	}
	inode->i_size = 0;//�ļ���С��0
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;//�������ļ��޸�ʱ��
}

