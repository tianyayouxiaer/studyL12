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

//�ض�λ�ļ���дָ��ϵͳ����
//fd - �ļ������offset - �����ļ���дָ��ƫ�ƣ�
//origin - ƫ����ʼλ��:SEEK_SET(0,���ļ���ʼ��)��SEEK_SET(1,���ļ���дλ��)��SEEK_END(2,���ļ�β��)0
int sys_lseek(unsigned int fd,off_t offset, int origin)
{
	struct file * file;
	int tmp;

	//������Ч���жϣ��ļ�����Ƿ���ڳ��������ļ��������ļ�������ļ��ṹָ��Ϊ�գ���Ӧ�ļ��ṹ��i�ڵ�
	//�ֶ�Ϊ�գ�����ָ���豸�ļ�ָ���ǲ��ɶ�λ��
	if (fd >= NR_OPEN || !(file=current->filp[fd]) || !(file->f_inode)
	   || !IS_SEEKABLE(MAJOR(file->f_inode->i_dev)))
		return -EBADF;
	//�ܵ��ļ��ļ������ƶ���дָ��λ��
	if (file->f_inode->i_pipe)
		return -ESPIPE;
	//�������õĶ�λ��־���ֱ����¶�λ�ļ���дָ��
	switch (origin) {
		//origin = SEEK_SET,���ļ���ʼ����Ϊԭ�������ļ���дָ��
		case 0:
			if (offset<0) return -EINVAL;//ƫ��С��0���򷵻ش�����
			file->f_pos=offset;//�����ļ���дָ��Ϊoffset
			break;
		case 1:
			//origin = SEEK_CUR,���ļ���ǰ��дָ�봦��Ϊԭ���ض�λ��дָ��
			if (file->f_pos+offset<0) return -EINVAL;//�ļ���дָ���offsetС��0������
			file->f_pos += offset;//�ļ���дָ��ƫ��offset
			break;
		case 2:
			//origin = SEEK_END,���ļ�ĩβ��Ϊԭ���ض�λ��дָ��
			if ((tmp=file->f_inode->i_size+offset) < 0)//�ļ���С����offsetС��0������
				return -EINVAL;
			file->f_pos = tmp;//�����ض�λ��дָ��Ϊ�ʼ۳��ȼ�ƫ��ֵ
			break;
		default:
			return -EINVAL;
	}
	//�����ض�λ����ļ���дָ��ֵ
	return file->f_pos;
}

//���ļ�ϵͳ����
//fd - �ļ������buf - ��������count - ����д�ֽ���
int sys_read(unsigned int fd,char * buf,int count)
{
	struct file * file;
	struct m_inode * inode;
	
	//������Ч�Լ�飬������̾��ֵ���ڳ��������ļ�����NR_OPEN,������Ҫд����ֽڼ���
	//С��0�����߸þ�����ļ��ṹָ��Ϊ�գ��򷵻س����벢�˳�
	if (fd>=NR_OPEN || count<0 || !(file=current->filp[fd]))
		return -EINVAL;
		
	//�����Ҫ��ȡ���ֽ���count����0���򷵻�0�˳�
	if (!count)
		return 0;

	//��֤������ݵĻ������ڴ�����
	verify_area(buf,count);
	//ȡ�ļ�i�ڵ�
	inode = file->f_inode;
	//�ܵ��ļ���Ϊ���ܵ�ģʽ
	if (inode->i_pipe)
		return (file->f_mode&1)?read_pipe(inode,buf,count):-EIO;
	//�ַ��豸�ļ�
	if (S_ISCHR(inode->i_mode))
		return rw_char(READ,inode->i_zone[0],buf,count,&file->f_pos);
	//���豸�ļ�
	if (S_ISBLK(inode->i_mode))
		return block_read(inode->i_zone[0],&file->f_pos,buf,count);
	//Ŀ¼�ͳ����ļ�
	if (S_ISDIR(inode->i_mode) || S_ISREG(inode->i_mode)) {
		//��֤��ȡ�ֽ�����Ч�ԣ������е���
		if (count+file->f_pos > inode->i_size)
			count = inode->i_size - file->f_pos;
		//Ȼ��ִ���ļ�������
		if (count<=0)
			return 0;
		return file_read(inode,file,buf,count);
	}
	printk("(Read)inode->i_mode=%06o\n\r",inode->i_mode);
	return -EINVAL;
}

//д�ļ�ϵͳ����
//����fd���ļ������buf�ǻ�������count����д�ֽ���
int sys_write(unsigned int fd,char * buf,int count)
{
	struct file * file;
	struct m_inode * inode;

	//������Ч�Լ�飬������̾��ֵ���ڳ��������ļ�����NR_OPEN,������Ҫд����ֽڼ���
	//С��0�����߸þ�����ļ��ṹָ��Ϊ�գ��򷵻س����벢�˳�
	if (fd>=NR_OPEN || count <0 || !(file=current->filp[fd]))
		return -EINVAL;

	//�����Ҫ��ȡ���ֽ���count����0���򷵻�0�˳�
	if (!count)
		return 0;

	//��֤������ݻ����������ơ���ȡ�ļ�i�ڵ㡣����i�ڵ����ԣ��ֱ������Ӧ�Ķ�д��������
	inode=file->f_inode;
	if (inode->i_pipe)//�ܵ�
		return (file->f_mode&2)?write_pipe(inode,buf,count):-EIO;//�ܵ���Ϊд�ܵ��ļ�ģʽ�������д�ܵ�����
	if (S_ISCHR(inode->i_mode))//�ַ��豸�ļ�
		return rw_char(WRITE,inode->i_zone[0],buf,count,&file->f_pos);
	if (S_ISBLK(inode->i_mode))//���豸�ļ�
		return block_write(inode->i_zone[0],&file->f_pos,buf,count);
	if (S_ISREG(inode->i_mode))//�����ļ�
		return file_write(inode,file,buf,count);
	printk("(Write)inode->i_mode=%06o\n\r",inode->i_mode);
	return -EINVAL;
}
