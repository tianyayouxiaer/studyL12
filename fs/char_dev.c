/*
 *  linux/fs/char_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <sys/types.h>

#include <linux/sched.h>
#include <linux/kernel.h>

#include <asm/segment.h>
#include <asm/io.h>

//�ն˶�
extern int tty_read(unsigned minor,char * buf,int count);
//�ն�д
extern int tty_write(unsigned minor,char * buf,int count);

//�ַ��豸��д����ָ������
typedef (*crw_ptr)(int rw,unsigned minor,char * buf,int count,off_t * pos);

//�����ն˶�д��������
//rw - ��д��� minor - �ն����豸�ţ�buf - ��������count - ��д�ֽ�����
//pos - ��д������ǰָ�룬�����ն˲�������ָ����Ч
//���أ�ʵ�ʶ�д�ֽ�������ʧ�ܣ��򷵻ش�����
static int rw_ttyx(int rw,unsigned minor,char * buf,int count,off_t * pos)
{
	return ((rw==READ)?tty_read(minor,buf,count):
		tty_write(minor,buf,count));
}

//�ն˶�д��������
//��rw_ttyx���ƣ�ֻ�������˶Խ����Ƿ��п����ն˵ļ��
static int rw_tty(int rw,unsigned minor,char * buf,int count, off_t * pos)
{
	if (current->tty<0)//С��0����ʾû��ʹ��
		return -EPERM;
	return rw_ttyx(rw,current->tty,buf,count,pos);
}

//�ڴ����ݶ�д��δʵ��
static int rw_ram(int rw,char * buf, int count, off_t *pos)
{
	return -EIO;
}

//�����ڴ����ݶ�д��δʵ��
static int rw_mem(int rw,char * buf, int count, off_t * pos)
{
	return -EIO;
}

//�ں������ڴ��д����,δʵ��
static int rw_kmem(int rw,char * buf, int count, off_t * pos)
{
	return -EIO;
}

//�˿ڶ�д��������
//������rw - ��д��� buf - ��������count - ��д�ֽ�����pos - �˿ڵ�ַ
static int rw_port(int rw,char * buf, int count, off_t * pos)
{
	int i=*pos;

	//������Ҫ��ȡ���ֽ��������Ҷ˿ڵ�ַС��64Kʱ��ѭ��ִ�е����ֽڵĶ�д����
	while (count-->0 && i<65536) {
		if (rw==READ)
			put_fs_byte(inb(i),buf++);//�Ӷ˿�i�ж�ȡһ���ֽ����ݷŵ��û���������
		else
			outb(get_fs_byte(buf++),i);//д������û����ݻ�������ȡһ���ֽ�������˿�i
		i++;
	}

	//�����д���ֽ�����������Ӧ��дָ�룬�����ض�/д���ֽ���
	i -= *pos;
	*pos += i;
	return i;
}

//�ڴ��д��������
static int rw_memory(int rw, unsigned minor, char * buf, int count, off_t * pos)
{
	//�����ڴ��豸���豸�ţ��ֱ���ò�ͬ�ڴ��д����
	switch(minor) {
		case 0:
			return rw_ram(rw,buf,count,pos);
		case 1:
			return rw_mem(rw,buf,count,pos);
		case 2:
			return rw_kmem(rw,buf,count,pos);
		case 3:
			return (rw==READ)?0:count;	/* rw_null */
		case 4:
			return rw_port(rw,buf,count,pos);
		default:
			return -EIO;
	}
}

//����ϵͳ���豸����
#define NRDEVS ((sizeof (crw_table))/(sizeof (crw_ptr)))

//�ַ��豸��д����ָ���
static crw_ptr crw_table[]={
	NULL,		/* nodev */ //���豸
	rw_memory,	/* /dev/mem etc  */
	NULL,		/* /dev/fd */
	NULL,		/* /dev/hd */
	rw_ttyx,	/* /dev/ttyx */
	rw_tty,		/* /dev/tty */
	NULL,		/* /dev/lp */
	NULL};		/* unnamed pipes */

//�ַ��豸��д��������
//rw - ��д��� dev - �豸�ţ�buf - ��������count - ��д�ֽ�����
//pos - ��д������ǰָ��
//���أ�ʵ�ʶ�д�ֽ�������ʧ�ܣ��򷵻ش�����

int rw_char(int rw,int dev, char * buf, int count, off_t * pos)
{
	crw_ptr call_addr;

	//����豸�ų���ϵͳ�豸�����򷵻ش����룻
	if (MAJOR(dev)>=NRDEVS)
		return -ENODEV;
	//������豸û�ж�Ӧ�Ķ�д���������ش�����
	if (!(call_addr=crw_table[MAJOR(dev)]))
		return -ENODEV;
	//���ö�Ӧ�豸�Ķ�д����������������ʵ�ʶ�/д�ֽ���
	return call_addr(rw,MINOR(dev),buf,count,pos);
}
