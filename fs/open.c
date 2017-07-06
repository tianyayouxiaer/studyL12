/*
 *  linux/fs/open.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <utime.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>

#include <asm/segment.h>

//ȡ�ļ�ϵͳ��Ϣ
//������dev-�Ѱ�װ�ļ�ϵͳ���豸�ţ�ubuf - ustat�ṹ������ָ�룬���ڴ��ϵͳ���ظ��ļ�ϵͳ��Ϣ
int sys_ustat(int dev, struct ustat * ubuf)
{
	//��δʵ��
	return -ENOSYS;
}

//�����ļ����ʺ��޸�ʱ��
//������ filename - �ļ����� times - ���ʺ��޸�ʱ��ָ��
//���timesָ�벻ΪNULL����ȡutimbuf�ṹ�е�ʱ����Ϣ�������ļ��ķ��ʺ��޸�ʱ��
//���timesָ��ΪNULL����ȡϵͳ��ǰʱ��������ָ���ļ��ķ��ʺ��޸�ʱ����
int sys_utime(char * filename, struct utimbuf * times)
{
	struct m_inode * inode;
	long actime,modtime;

	//�ļ���ʱ����Ϣ��������i�ڵ��ӣ�ȡĿ¼����i�ڵ㣬���Ŀ¼����Ӧ��i�ڵ㲻���ڣ��򷵻ش�����
	if (!(inode=namei(filename)))
		return -ENOENT;
	//���times��Ϊnull����ӽṹ�ж�ȡ�û����õ�ʱ��ֵ
	//�������ϵͳ��ǰʱ���������ļ��ķ��ʺ��޸�ʱ��
	if (times) {
		actime = get_fs_long((unsigned long *) &times->actime);
		modtime = get_fs_long((unsigned long *) &times->modtime);
	} else
		actime = modtime = CURRENT_TIME;

	//�޸�i�ڵ��еķ����ֶκ��޸�ʱ���ֶΣ�����i�ڵ����޸ı�־���Żظ�i�ڵ㣬������
	inode->i_atime = actime;
	inode->i_mtime = modtime;
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

/*
 * XXX should we use the real or effective uid?  BSD uses the real uid,
 * so as to make this call useful to setuid programs.
 */
//����ļ�����Ȩ��
//������ filename - �ļ����� mode - ���ķ�������
//��3����Чλ��ɣ�R_OK(ֵ4)��W_OK(ֵ2)��X_OK(1)��F_OK(0)
//�ֱ��ʾ����ļ��Ƿ�ɶ�����д����ִ�к��ļ��Ƿ����

int sys_access(const char * filename,int mode)
{
	struct m_inode * inode;
	int res, i_mode;

	//���ķ��������е�3λ��ɣ������Ҫ�����λ
	mode &= 0007;
	//��ȡi�ڵ�
	if (!(inode=namei(filename)))
		return -EACCES;
	//ȡi�ڵ��ļ������룬���Żظ�i�ڵ�
	i_mode = res = inode->i_mode & 0777;
	iput(inode);

	//�����ǰ�����û��Ǹ��ļ���������ȡ�ļ���������
	//���������ǰ�����û�����ļ�����ͬ��һ�飬��ȡ�ļ�������
	//����res���3λΪ�����˷��ʸ��ļ����������
	if (current->uid == inode->i_uid)
		res >>= 6;
	else if (current->gid == inode->i_gid)
		res >>= 6;
	if ((res & 0007 & mode) == mode)
		return 0;
	/*
	 * XXX we are doing this test last because we really should be
	 * swapping the effective with the real user id (temporarily),
	 * and then calling suser() routine.  If we do call the
	 * suser() routine, it needs to be called last. 
	 */
	 //�����ǰ�û�idΪ0���������û���������������ִ��λ��0�������ļ����Ա��κ���ִ�С��������򷵻�0�����򷵻ش�����
	if ((!current->uid) &&
	    (!(mode & 1) || (i_mode & 0111)))
		return 0;
	return -EACCES;
}

//�ı䵱ǰ����Ŀ¼���ѽ�������ṹ�ĵ�ǰ����Ŀ¼�ֶ�ָ�����Ŀ¼����i�ڵ�
int sys_chdir(const char * filename)
{
	struct m_inode * inode;
	//ȡĿ¼����i�ڵ㣬���Ŀ¼����Ӧ��i�ڵ㲻���ڣ��򷵻ش�����
	if (!(inode = namei(filename)))
		return -ENOENT;
	//�����i�ڵ㲻��Ŀ¼i�ڵ㣬��Żظ�i�ڵ�
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	//�ͷŽ���Ԫ����Ŀ¼i�ڵ㣬��ʹ��ָ�������õĹ���Ŀ¼i�ڵ�
	iput(current->pwd);
	current->pwd = inode;
	return (0);
}

//�ı��Ŀ¼���ã��ı䵱ǰ��������ṹ�еĸ�Ŀ¼�ֶ�root������ָ���������Ŀ¼����i�ڵ�
//��ָ����Ŀ¼���ó�Ϊ��ǰ���̵ĸ�Ŀ¼��/��
int sys_chroot(const char * filename)
{
	struct m_inode * inode;

	//���Ŀ¼����Ӧ��i�ڵ㲻���ڣ��򷵻ش�����
	if (!(inode=namei(filename)))
		return -ENOENT;
	//�����i�ڵ㲻��Ŀ¼i�ڵ㣬��Żظ�i�ڵ�
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	//�ͷŵ�ǰ���̸�Ŀ¼i�ڵ㣬����������Ϊָ��Ŀ¼����i�ڵ�
	iput(current->root);
	current->root = inode;
	return (0);
}

//�޸��ļ�����,Ϊָ���ļ������µķ�������mode
//������filename - �ļ�����mode �ļ�����
int sys_chmod(const char * filename,int mode)
{
	struct m_inode * inode;

	//ȡ�ļ�����Ӧ��i�ڵ㣬���i�ڵ㲻���ڣ��򷵻ش�����
	if (!(inode=namei(filename)))
		return -ENOENT;
	//�����ǰ���̵���Ч�û�id���ļ�i�ڵ���û�id��ͬ������Ҳ���ǳ����û�����Żظ�i�ڵ㣬���س�����
	if ((current->euid != inode->i_uid) && !suser()) {
		iput(inode);
		return -EACCES;
	}
	//�������ø�i�ڵ���ļ����ԣ����ø�i�ڵ����޸ı�־���Żظ�i�ڵ㣬����0
	inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

//�޸��ļ�����ϵͳ����,���������ļ�i�ڵ��е��û�����id
//������filename - �ļ�����uid - �û�id��gid - ��id
int sys_chown(const char * filename,int uid,int gid)
{
	struct m_inode * inode;

	//ȡ�ø����ļ�����i�ڵ㣬��������ļ�����i�ڵ㲻���ڣ��򷵻س����루�ļ�����Ŀ¼�����ڣ�
	if (!(inode=namei(filename)))
		return -ENOENT;
	//�����ǰ���̲��ǳ����û�����Żظ�i�ڵ㣬�����ش����루û�з���Ȩ�ޣ�
	if (!suser()) {
		iput(inode);
		return -EACCES;
	}
	//�ò����ṩ��ֵ�������ļ�i�ڵ���û�id����id������i�ڵ����޸ı�־���Żظ�i�ڵ�
	inode->i_uid=uid;
	inode->i_gid=gid;
	inode->i_dirt=1;
	iput(inode);
	return 0;
}

//����ַ��豸����
//����sys_open�����ڼ�����򿪵��ļ���tty�ն��ַ��豸ʱ����Ҫ�Ե�ǰ���̵����úͶ�tty������
//���أ�0-��鴦��ɹ��� -1 ʧ�ܣ���Ӧ�ַ��豸���ܴ�
static int check_char_dev(struct m_inode * inode, int dev, int flag)
{
	struct tty_struct *tty;
	int min;

	//ֻ�������豸����4((/dev/ttyxx)��5(/dev/tty)�����;(/dev/tty)���豸����0
	if (MAJOR(dev) == 4 || MAJOR(dev) == 5) {
		//������ļ���/dev/tty����ômin=��������ṹ�е�tty�ֶΣ���ȡ4���豸�����豸�š�
		//���򿪵���ĳ��4���豸����ֱ��ȡ�����豸�ţ�
		if (MAJOR(dev) == 5)
			min = current->tty;
		else
		//���һ�������п����նˣ������ǽ��̿����ն��豸��ͬ����
		//��/dev/tty�豸��һ�������豸������Ӧ������ʵ��ʹ�õ�/dev/ttyxx�豸֮һ��
		//����һ��������˵�������п����նˣ���ô��������ṹ�е�tty�ֶν���4���豸��ĳһ�����豸
			min = MINOR(dev);
			
		//����õ���4���豸���豸��С��0����ô˵������û��
		//�����նˣ������豸�Ŵ����򷵻�-1����ʾ���ڽ���û�п����նˣ����߲��ܴ�����豸
		if (min < 0)
			return -1;O_NONBLOCK

		//��α�ն��豸�ļ�ֻ�ܱ����̶�ռʹ��
		//������豸�ű�����һ����α�ն��豸�����Ҹô��ļ�i�ڵ����ü�������1����˵�����豸��
		//����������ʹ�á�
		//��˲��ܴ򿪸��ַ��豸�ļ�����˷���-1
		if ((IS_A_PTY_MASTER(min)) && (inode->i_count>1))
			return -1;
		//ttyָ��tty���ж�Ӧ�ṹ�
		tty = TTY_TABLE(min);
		//�����ļ�������׼flag�в�����������ն˱�־O_NOCTTY��
		//���ҽ����ǽ��������죬���ҵ�ǰ����û�п����ն�,����tty�������κν�����Ŀ����ն�
		//��ô������Ϊ������������ն��豸minΪ������ն�
		if (!(flag & O_NOCTTY) &&
		    current->leader &&
		    current->tty<0 &&
		    tty->session==0) {
		    //�������ý�������ṹ�ն��豸���ֶ�ttyֵ����min�������ö�Ӧtty�ṹ�ĻỰ��session�ͽ������pgrp�ֱ���ڽ���
		    //�ĻỰ�źͽ������
			current->tty = min;
			tty->session= current->session;
			tty->pgrp = current->pgrp;
		}
		//������ļ���־flag����O_NONBLOCK����������Ҫ�Ը��ַ��ն��豸�����������
		//����Ϊ�����������Ҫ��ȡ�������ֽ���λ0����ʱʱ��Ϊ0��������������Ϊ�ǹ淶ģʽ��
		if (flag & O_NONBLOCK) {
			TTY_TABLE(min)->termios.c_cc[VMIN] =0;
			TTY_TABLE(min)->termios.c_cc[VTIME] =0;
			TTY_TABLE(min)->termios.c_lflag &= ~ICANON;
		}
	}
	return 0;
}

//�򿪻򴴽��ļ�ϵͳ����
//filename - �ļ���;
//flag - �򿪱�־����fs.h
//mode - �ļ�������� ��stat.h
//�����´������ļ�����Щ����ֻӦ���ڽ������ļ��ķ��ʣ�������ֻ���ļ��Ĵ򿪵���Ҳ������һ���ɶ�д���ļ����
//�ɹ��������ļ������ʧ�ܣ�������
int sys_open(const char * filename,int flag,int mode)
{
	struct m_inode * inode;
	struct file * f;
	int i,fd;

	//�û������ļ�ģʽ�����ģʽ���������룬������ɵ��ļ�ģʽ
	mode &= 0777 & ~current->umask;
	
	//Ϊ���ļ�����һ���ļ��������Ҫ�������̽ṹ���ļ��ṹָ�����飬�Բ���һ��������
	for(fd=0 ; fd<NR_OPEN ; fd++)
		if (!current->filp[fd])//�ҵ�������
			break;
	if (fd>=NR_OPEN)
		return -EINVAL;

	//���õ�ǰ���̵�ִ��ʱ�ر��ļ������close_on_exec��λͼ����λ��Ӧ��λ
	//close_on_exec�ǽ��������ļ������λͼ��־��ÿ��λ����һ�����ŵ��ļ�������������ȷ���ڵ���ϵͳ����
	//execve()ʱ��Ҫ�رյ��ļ����
	current->close_on_exec &= ~(1<<fd);//???

	f=0+file_table;// <=> file_table[0]

	//��ʱ�����ý��̶�Ӧ�ļ����fd���ļ��ṹָ��ָ�����������ļ��ṹ�������ļ����ü�������1
	for (i=0 ; i<NR_FILE ; i++,f++)
		if (!f->f_count) break;
	if (i>=NR_FILE)
		return -EINVAL;
	//���̶�Ӧ�ļ����fd���ļ��ṹָ��ָ�����������ļ��ṹ�������ļ����ü�������1
	(current->filp[fd]=f)->f_count++;
	//open_nameiִ�д򿪲�����������ֵС��0����˵������
	if ((i=open_namei(filename,flag,mode,&inode))<0) {
		//�ͷ����뵽���ļ��ṹ�����س�����
		current->filp[fd]=NULL;
		f->f_count=0;
		return i;
	}

	//���ļ��򿪲����ɹ�����inode���Ѵ��ļ�i�ڵ�ָ��
/* ttys are somewhat special (ttyxx major==4, tty major==5) */
	//�����Ѵ��ļ�i�ڵ�������ֶΣ�����֪���ļ����ͣ����ڲ�ͬ�����ļ�����Ҫ�����⴦��
	//������ַ��豸�ļ�������Ҫ��鵱ǰ�����Ƿ��ܴ�����ַ��豸�ļ�
	if (S_ISCHR(inode->i_mode))
		//����������ʹ�ø��ַ��豸�ļ�������Ҫ�ͷ�����������ļ���;�����棬���س�����
		if (check_char_dev(inode,inode->i_zone[0],flag)) {
			iput(inode);
			current->filp[fd]=NULL;
			f->f_count=0;
			return -EAGAIN;
		}
/* Likewise with block-devices: check for floppy_change */
	if (S_ISBLK(inode->i_mode))
		check_disk_change(inode->i_zone[0]);
	f->f_mode = inode->i_mode;
	f->f_flags = flag;
	f->f_count = 1;
	f->f_inode = inode;
	f->f_pos = 0;
	return (fd);
}

//�����ļ�ϵͳ����
int sys_creat(const char * pathname, int mode)
{
	return sys_open(pathname, O_CREAT | O_TRUNC, mode);
}

//�ر��ļ�ϵͳ����
//�ɹ����ؾ���ļ���ʧ�ܷ��ش�����
int sys_close(unsigned int fd)
{	
	struct file * filp;

	//��������Ч��
	if (fd >= NR_OPEN)
		return -EINVAL;

	//��λ����ִ��ʱ�ر��ļ����λͼ��Ӧλ
	current->close_on_exec &= ~(1<<fd);
	//�����ļ������Ӧ���ļ��ṹָ����null���򷵻ش�����
	if (!(filp = current->filp[fd]))
		return -EINVAL;
	//�ø��ļ�������ļ��ṹָ��ΪNULL���ڹر��ļ�ǰ������Ӧ�ļ��ṹ�еľ��Ӧ�ü���
	//�Ѿ�Ϊ0����˵���ں˴���
	current->filp[fd] = NULL;
	if (filp->f_count == 0)
		panic("Close: file count is 0");
	//���ļ��ṹ�����ü�����1�������ʱ����Ϊ0����˵����������������ʹ�ø��ļ������Ƿ���0
	if (--filp->f_count)
		return (0);
	//�����ü���Ϊ0����˵���ļ��Ѿ�û�н������ã����ļ��ṹ�Ա�Ϊ���У����ͷŸ��ļ�i�ڵ㣬����0
	iput(filp->f_inode);
	return (0);
}
