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

//�ܵ�����������
//����inode�ǹܵ���Ӧ��i�ڵ㣻buf - �û�������ָ�룻count - ��ȡ�ֽ���
int read_pipe(struct m_inode * inode, char * buf, int count)
{
	int chars, size, read = 0;

	//ѭ��ִ�ж�����
	while (count>0) {
		//���ܵ���û�����ݣ����ѵȴ��ýڵ�Ľ��̣��ý���ͨ����д�ܵ�����
		while (!(size=PIPE_SIZE(*inode))) {
			wake_up(& PIPE_WRITE_WAIT(*inode));//ȡ�ܵ������ݳ���
			//����Ѿ�û��д�ܵ��ߣ���i�ڵ����ü���ֵС��2���򷵻��Ѷ��ֽ����˳�
			if (inode->i_count != 2) /* are there any writers? */
				return read;
			//���Ŀǰ�յ��������źţ������̷����Ѷ�ȡ�ֽ������˳�
			if (current->signal & ~current->blocked)
				return read?read:-ERESTARTSYS;
			//�øý����ڹܵ���˯�ߣ����Եȴ���Ϣ�ĵ���
			interruptible_sleep_on(& PIPE_READ_WAIT(*inode));
		}
		//����˵���ܵ��������ݣ���������ȡ�ܵ�βָ�뵽������ĩ�˵��ֽ���chars��
		chars = PAGE_SIZE-PIPE_TAIL(*inode);
		if (chars > count)		//�������ڻ���Ҫ��ȡ���ֽ���count�����������count��
			chars = count;
		if (chars > size)		//�������ڵ�ǰ�ܵ��к������ݵĳ���size�����������size��
			chars = size;
		//��ȥ�˴ζ�ȡ���ֽ��������ۼ��Ѷ��ֽ���read
		count -= chars;
		read += chars;

		//sizeָ��ܵ�βָ�봦�������õ�ǰ�ܵ�βָ�루ǰ��chas���ֽڣ�
		size = PIPE_TAIL(*inode);
		PIPE_TAIL(*inode) += chars;
		//��βָ�볬���ܵ�ĩ�����ƻأ�
		PIPE_TAIL(*inode) &= (PAGE_SIZE-1);
		//���ܵ��е����ݸ��Ƶ��û���������
		while (chars-->0)
			put_fs_byte(((char *)inode->i_size)[size++],buf++);
	}
	//�˴ιܵ��������������ѵȴ��ùܵ��Ľ��̣������ض�ȡ���ֽ���
	wake_up(& PIPE_WRITE_WAIT(*inode));
	return read;
}

//д�ܵ���������
//����inode�ǹܵ���Ӧ��i�ڵ㣬buf - ���ݻ�����ָ�룬count - ��д��ܵ����ֽ���
int write_pipe(struct m_inode * inode, char * buf, int count)
{
	int chars, size, written = 0;

	//ѭ��д������
	while (count>0) {
		//�統ǰ�ܵ��Ѿ����ˣ����пռ�sizeΪ0��,���ѵ��ڸùܵ��Ľ��̣�ͨ�����ѵ���
		//������
		while (!(size=(PAGE_SIZE-1)-PIPE_SIZE(*inode))) {
			wake_up(& PIPE_READ_WAIT(*inode));
			//���û�ж��ܵ��ߣ�������̷���SIGPIPE�źţ���������д����ֽ���������
			if (inode->i_count != 2) { /* no readers */
				current->signal |= (1<<(SIGPIPE-1));
				return written?written:-1;
			}
			//�ж��ܵ��ߣ����õ�ǰ�����ڹܵ���˯�ߣ��Եȴ����ܵ�������������
			sleep_on(& PIPE_WRITE_WAIT(*inode));
		}

		//��ȡ�ܵ���д�ռ�size�����ܵ�ͷָ�뵽������ĩ�˿ռ��ֽ���chars
		//����count��written
		chars = PAGE_SIZE-PIPE_HEAD(*inode);
		if (chars > count)
			chars = count;
		if (chars > size)
			chars = size;
		count -= chars;
		written += chars;
		//sizeָ��ܵ�����ͷָ�봦����������ǰ�ܵ�����ͷָ�루ǰ��chars���ֽڣ�
		//��ͷָ�볬���ܵ�ĩ�����ƻأ��˴��ܾ���
		size = PIPE_HEAD(*inode);
		PIPE_HEAD(*inode) += chars;
		PIPE_HEAD(*inode) &= (PAGE_SIZE-1);
		//���û�����������chars���ֽڵ��ܵ�ͷָ�봦
		while (chars-->0)
			((char *)inode->i_size)[size++]=get_fs_byte(buf++);
	}
	//���˴�д�ܵ��������������ѵȴ��ܵ��Ľ��̣�������д����ֽ���
	wake_up(& PIPE_READ_WAIT(*inode));
	return written;
}

//�����ܵ�ϵͳ����
//��fildes��ָ���������д���һ���ļ��������������������ļ�������ָ��һ�ܵ�i�ڵ�
//filedes[0]���ڶ��ܵ����ݣ�filedes[1]����д�ܵ�����
//����ֵ���ɹ� - 0�� ʧ�� - -1
int sys_pipe(unsigned long * fildes)
{
	struct m_inode * inode;
	struct file * f[2];//�ļ��ṹ����
	int fd[2];//�ļ��������
	int i,j;

	j=0;
	//���ȴ�ϵͳ�ļ�����ȡ����������ü����ֶ�Ϊ0��������ֱ�����Ӧ�ü���Ϊ0
	for(i=0;j<2 && i<NR_FILE;i++)
		if (!file_table[i].f_count)
			(f[j++]=i+file_table)->f_count++;//��������д��������
	//��ֻ��1����������ͷŸ�����ü�����λ��
	if (j==1)
		f[0]->f_count=0;
	//��û���ҵ�����������򷵻�-1
	if (j<2)
		return -1;

	//�������ȡ�õ������ļ���ṹ��ֱ����һ���ļ������
	//��ʹ�����ļ��ṹָ�����������ֱ�ָ���������ļ��ṹ
	//���ļ�������Ǹ������������
	j=0;
	for(i=0;j<2 && i<NR_OPEN;i++)
		if (!current->filp[i]) {
			current->filp[ fd[j]=i ] = f[j];
			j++;
		}
	//���ֻ��һ�������ļ���������ͷŸþ�����ÿ���Ӧ�����
	if (j==1)
		current->filp[fd[0]]=NULL;
	//���û���ҵ��������о�������ͷ������ȡ�������ļ��ṹ��
	if (j<2) {
		f[0]->f_count=f[1]->f_count=0;
		return -1;
	}

	//����һ���ܵ�ʹ�õ�i�ڵ㣬��δ�ܵ�����һҳ�ڴ���Ϊ������
	if (!(inode=get_pipe_inode())) {
		//������ɹ������ͷ������ļ�������ļ��ṹ��
		current->filp[fd[0]] =
			current->filp[fd[1]] = NULL;
		f[0]->f_count = f[1]->f_count = 0;
		return -1;
	}

	//����ܵ�i�ڵ�����ɹ�����������ļ��ṹ���г�ʼ������ 
	f[0]->f_inode = f[1]->f_inode = inode;//��ָ��ͬһ���ܵ�i�ڵ�
	f[0]->f_pos = f[1]->f_pos = 0;//��дָ�붼��0
	f[0]->f_mode = 1;		/* read */  //��
	f[1]->f_mode = 2;		/* write */ //д
	put_fs_long(fd[0],0+fildes);//���ļ�������鸴�Ƶ���Ӧ���û��ռ�������
	put_fs_long(fd[1],1+fildes);
	return 0;
}

//�ܵ�io���ƺ���
//pino - �ܵ�i�ڵ�ָ�룻cmd - ������� arg - ����
//����0��ʾִ�гɹ�
int pipe_ioctl(struct m_inode *pino, int cmd, int arg)
{
	switch (cmd) {
		case FIONREAD:
			//���������ȡ�ܵ��е�ǰ�ɶ����ݳ��ȣ���ѹܵ����ݳ���ֵ�����û�����ָ����λ�ô�,������0
			//���򷵻���Ч���������
			verify_area((void *) arg,4);
			put_fs_long(PIPE_SIZE(*pino),(unsigned long *) arg);
			return 0;
		default:
			return -EINVAL;
	}
}
