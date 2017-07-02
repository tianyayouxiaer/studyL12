/*
 *  linux/fs/super.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * super.c contains code to handle the super-block tables.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include <errno.h>
#include <sys/stat.h>

int sync_dev(int dev);
void wait_for_keypress(void);

/* set_bit uses setb, as gas doesn't recognize setc */
#define set_bit(bitnr,addr) ({ \
register int __res __asm__("ax"); \
__asm__("bt %2,%3;setb %%al":"=a" (__res):"a" (0),"r" (bitnr),"m" (*(addr))); \
__res; })

//������ṹ������
struct super_block super_block[NR_SUPER];
/* this is initialized in init/main.c */
int ROOT_DEV = 0;//���ļ�ϵͳ�豸��

static void lock_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sb->s_lock = 1;
	sti();
}

//��ָ���ĳ��������
static void free_super(struct super_block * sb)
{
	cli();
	sb->s_lock = 0;
	wake_up(&(sb->s_wait));
	sti();
}

//˯�ߵȴ����������
static void wait_on_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sti();
}

//ȡָ���豸�ĳ�����
//
struct super_block * get_super(int dev)
{
	struct super_block * s;

	//�ж��豸����Ч��
	if (!dev)
		return NULL;
		
	//�������������飬Ѱ��ָ���豸dev�ĳ�����	
	s = 0+super_block;
	while (s < NR_SUPER+super_block)
		if (s->s_dev == dev) {
			wait_on_super(s);
			if (s->s_dev == dev)
				return s;
			s = 0+super_block;//���±�������������
		} else
			s++;
	return NULL;
}

//�ͷţ��Żأ�ָ���ĳ�����
void put_super(int dev)
{
	struct super_block * sb;
	int i;

	
	if (dev == ROOT_DEV) {
		printk("root diskette changed: prepare for armageddon\n\r");
		return;
	}

	//�ڳ�����������Ѱ��ָ���豸�ŵ��ļ�ϵͳ�����飬�Ҳ������򷵻�
	if (!(sb = get_super(dev)))
		return;
	//�ҵ������飬���Ǹó�����ָ�����ļ�ϵͳ����װ����i�ڵ㻹û�б����������ʾ������Ϣ������
	//���ļ�ϵͳж��umount�У�s_imount���ȱ��ó�null��Ȼ��ŵ��ñ�����
	if (sb->s_imount) {
		printk("Mounted disk changed - tssk, tssk\n\r");
		return;
	}
	//�����ó�����
	lock_super(sb);
	sb->s_dev = 0;//�øó������Ӧ���豸���ֶ�Ϊ0�����ͷŸ��豸�ϵ��ļ�ϵͳ������
	for(i=0;i<I_MAP_SLOTS;i++)
		brelse(sb->s_imap[i]);//�ͷŸ��豸���ļ�ϵͳi�ڵ�λͼ���߼�λͼ����ռ�õĻ����
	for(i=0;i<Z_MAP_SLOTS;i++)
		brelse(sb->s_zmap[i]);
	free_super(sb);
	return;
}

//��ȡָ���豸�ĳ�����
static struct super_block * read_super(int dev)
{
	struct super_block * s;
	struct buffer_head * bh;
	int i,block;

	if (!dev)
		return NULL;

	//�����豸�Ǹ������̣��Ƿ�Ϊ�����豸
	//���������������ҪʧЧ�������ͷ�ԭ�����ص��ļ�ϵͳ
	check_disk_change(dev);
	if (s = get_super(dev))
		return s;
	//�ڳ������������ҵ�һ����ҳ
	for (s = 0+super_block ;; s++) {
		if (s >= NR_SUPER+super_block)
			return NULL;
		if (!s->s_dev)
			break;
	}
	//���ó�������ָ��dev�ϵ��ļ�ϵͳ
	//�Ըó�����ṹ�е��ڴ��ֶν��в��Ŵ���
	s->s_dev = dev;
	s->s_isup = NULL;
	s->s_imount = NULL;
	s->s_time = 0;
	s->s_rd_only = 0;
	s->s_dirt = 0;
	//�����ó����飬�����豸�϶�ȡ��������Ϣ��hbָ��Ļ������
	lock_super(s);
	//������λ�ڿ��豸�ĵ�2���߼����У���һ����������
	if (!(bh = bread(dev,1))) {
		s->s_dev=0;
		free_super(s);//����ʧ�ܣ����ͷ��ͷų����������еĸ���
		return NULL;
	}
	
	//���豸�϶�ȡ�ĳ�������Ϣ�ӻ�������������Ƶ�������������Ӧ��ṹ��
	*((struct d_super_block *) s) =
		*((struct d_super_block *) bh->b_data);
		
	//�ͷŴַŶ�ȡ��Ϣ�ĸ��ٻ����
	brelse(bh);
	
	//�������Ǵ��豸dev�ϵõ����ļ�ϵͳ�ĳ�����
	//���������������Ч��
	
	if (s->s_magic != SUPER_MAGIC) {
		s->s_dev = 0;
		free_super(s);
		return NULL;
	}
	//��ȡ�豸��i�ڵ�λͼ���߼���λͼ���ݡ�
	//���ȳ�ʼ���ڴ泬����ṹ��λͼ�ռ�
	for (i=0;i<I_MAP_SLOTS;i++)
		s->s_imap[i] = NULL;
	for (i=0;i<Z_MAP_SLOTS;i++)
		s->s_zmap[i] = NULL;

	//���豸�϶�ȡi�ڵ�λͼ���߼���λͼ��Ϣ��������ڳ������Ӧ�ֶ���
	//i�ڵ�λͼ�������豸��2�ſ鿪ʼ���߼�����,��ռ��s_imap_blocks����
	//�߼�λͼ��i�ڵ�λͼ���ڿ�ĺ������У���ռs_zmap_block����
	block=2;
	for (i=0 ; i < s->s_imap_blocks ; i++)//��ȡ�豸�ϵ�i�ڵ�λͼ
		if (s->s_imap[i]=bread(dev,block))
			block++;
		else
			break;
	for (i=0 ; i < s->s_zmap_blocks ; i++)//��ȡ�豸���߼�λͼ
		if (s->s_zmap[i]=bread(dev,block))
			block++;
		else
			break;

	//���������λͼ����������λͼӦ��ռ�õ��߼�������˵���ļ�ϵͳλͼ��Ϣ�����⣬�������ʼ��ʧ��
	//���ֻ���ͷ�ǰ�����벢ռ�õ�������Դ
	//���ͷ�i�ڵ�λͼ���߼���λͼռ�õĸ��ٻ����
	//�ͷ�����ѡ���ĳ���������������ó�������
	if (block != 2+s->s_imap_blocks+s->s_zmap_blocks) {
		for(i=0;i<I_MAP_SLOTS;i++)
			brelse(s->s_imap[i]);
		for(i=0;i<Z_MAP_SLOTS;i++)
			brelse(s->s_zmap[i]);
		s->s_dev=0;
		free_super(s);
		return NULL;
	}

	//һ�гɹ�������豸������i�ڵ��Ѿ�ȫ��ʹ�ã�����Һ����᷵��0ֵ��
	//0��i�ڵ㲻��ʹ�ã��������ｫλͼ�е�һ������λ������Ϊ1
	//��ֹ�ļ�ϵͳ����0��i�ڵ�
	s->s_imap[0]->b_data[0] |= 1;
	s->s_zmap[0]->b_data[0] |= 1;
	free_super(s);
	return s;
}

//ж���ļ�ϵͳ
//dev_nameΪ�ļ�ϵͳ�����豸���豸�ļ���
int sys_umount(char * dev_name)
{
	struct m_inode * inode;
	struct super_block * sb;
	int dev;
	
	//�����ļ������ҵ���Ӧi�ڵ㣬�Ի�����е��豸��
	//���ڿ������豸�ļ����豸������i�ڵ��i_zone[0]��
	if (!(inode=namei(dev_name)))
		return -ENOENT;
	dev = inode->i_zone[0];

	//�ļ�ϵͳ�����ڿ��豸�У���ˣ�������ǿ��豸�ļ�����Żظ�ȡ�õ�i�ڵ�dev_i
	if (!S_ISBLK(inode->i_mode)) {
		iput(inode);
		return -ENOTBLK;
	}
	
	//�Ż��豸i�ڵ�	
	iput(inode);

	//����豸�Ǹ��ļ�ϵͳ������ж��
	if (dev==ROOT_DEV)
		return -EBUSY;

	//���û���ҵ����豸�ĳ���������ҵ������豸���ļ�ϵͳû�а�װ��
	if (!(sb=get_super(dev)) || !(sb->s_imount))
		return -ENOENT;
	//�����������ָ���ı���װ����i�ڵ㲢û����λ�䰲װ��־���򾯸�
	if (!sb->s_imount->i_mount)
		printk("Mounted inode has i_mount=0\n");

	//����i�ڵ�����Ƿ��н�����ʹ�ø��豸�ϵ��ļ�
	for (inode=inode_table+0 ; inode<inode_table+NR_INODE ; inode++)
		if (inode->i_dev==dev && inode->i_count)
				return -EBUSY;

	//ʵʩж��
	sb->s_imount->i_mount=0;//��λ����װ����i�ڵ�İ�װ��־
	iput(sb->s_imount);//�Ƿ�i�ڵ�
	sb->s_imount = NULL;//�������б���װi�ڵ��ֶ�Ϊ��
	iput(sb->s_isup);//�Ż��豸�ļ�ϵͳ��i�ڵ�
	sb->s_isup = NULL;//�������б���װϵͳ��i�ڵ�ָ��Ϊ��
	//�ͷŸ��豸�ϵĳ������Լ�λͼռ�õĸ��ٻ���飬���Ը��豸ִ�и��ٻ������豸�����ݵ�ͬ������
	put_super(dev);
	sync_dev(dev);
	return 0;
}

//��װ�ļ�ϵͳ
//dev_name-�豸�ļ���
//dir_name-��װ����Ŀ¼��
//rw_flag-����װ�ļ�ϵͳ�Ŀɶ�д��־
int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
	struct m_inode * dev_i, * dir_i;
	struct super_block * sb;
	int dev;

	//�����ļ������ҵ���Ӧi�ڵ㣬�Ի�����е��豸��
	//���ڿ������豸�ļ����豸������i�ڵ��i_zone[0]��
	if (!(dev_i=namei(dev_name)))
		return -ENOENT;
	dev = dev_i->i_zone[0];
	
	//�ļ�ϵͳ�����ڿ��豸�У���ˣ�������ǿ��豸�ļ�����Żظ�ȡ�õ�i�ڵ�dev_i
	if (!S_ISBLK(dev_i->i_mode)) {
		iput(dev_i);
		return -EPERM;
	}
	
	//�Ż��豸i�ڵ�
	iput(dev_i);

	//����ļ�ϵͳ��װ����Ŀ¼���Ƿ���Ч
	//���ݶ�Ӧ��Ŀ¼���ҵ���Ӧ��i�ڵ�dir_i
	if (!(dir_i=namei(dir_name)))
		return -ENOENT;

	//���i�ڵ�����ü�����Ϊ1������i�ڵ�Ľڵ���Ǹ��ļ�ϵͳ�Ľڵ��1����Żظ�i�ڵ㲢���ش�����
	if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
		iput(dir_i);
		return -EBUSY;
	}

	//�ж��Ƿ�ΪĿ¼�ļ�
	if (!S_ISDIR(dir_i->i_mode)) {
		iput(dir_i);
		return -EPERM;
	}
	//��ȡҪ��װ�ļ�ϵͳ�ĳ�������Ϣ
	if (!(sb=read_super(dev))) {
		iput(dir_i);
		return -EBUSY;
	}
	//�Ƿ��Ѿ���װ���ļ�ϵͳ
	if (sb->s_imount) {
		iput(dir_i);
		return -EBUSY;
	}
	if (dir_i->i_mount) {
		iput(dir_i);
		return -EPERM;
	}
	//��װ�ļ�ϵͳ�ĳ����顰����װ��i�ڵ㡱�ֶ�ָ��װ����Ŀ¼����i�ڵ�
	//���ð�װi�ڵ�İ�װ��־�ͽڵ����޸ı�־
	sb->s_imount=dir_i;
	dir_i->i_mount=1;
	dir_i->i_dirt=1;		/* NOTE! we don't iput(dir_i) */
	return 0;			/* we do that in umount */
}

//��װ���ļ�ϵͳ
//������ʼ��sys_setup�е���
void mount_root(void)
{
	int i,free;
	struct super_block * p;
	struct m_inode * mi;

	if (32 != sizeof (struct d_inode))
		panic("bad i-node size");

	//��ʼ���ļ������飬�˴���64���ϵͳͬʱֻ�ܴ�64���ļ�
	for(i=0;i<NR_FILE;i++)
		file_table[i].f_count=0;//�ļ�Ӧ�ü����豸Ϊ0����ʾ���У�

	//������ļ�ϵͳ�����豸�����̣���ʾ��������
	if (MAJOR(ROOT_DEV) == 2) {
		printk("Insert root floppy and press ENTER");
		wait_for_keypress();
	}
	//��ʼ�����������ʼ��Ϊ0����ʾ����
	for(p = &super_block[0] ; p < &super_block[NR_SUPER] ; p++) {
		p->s_dev = 0;
		p->s_lock = 0;
		p->s_wait = NULL;
	}

	//�Ӹ��豸�϶�ȡ�ļ�ϵͳ�����飬��ȡ���ļ�ϵͳ�ĸ�i�ڵ㣨1��i�ڵ㣩���ڴ�i�ڵ���ָ��
	if (!(p=read_super(ROOT_DEV)))
		panic("Unable to mount root");
	if (!(mi=iget(ROOT_DEV,ROOT_INO)))
		panic("Unable to read root i-node");

	//���ڶԳ�����͸�i�ڵ��������
	//�Ѹ�i�ڵ�Ӧ�ô�������3
	
	mi->i_count += 3 ;	/* NOTE! it is logically used 4 times, not 1 */
	p->s_isup = p->s_imount = mi;
	current->pwd = mi;
	current->root = mi;
	
	//�Ը��ļ�ϵͳ��Դ����ͳ��
	
	free=0;
	i=p->s_nzones;//�������б������豸�߼�������
	//�����߼���λͼ����Ӧλ��ռ�����ͳ�ƴ����п���
	//i&8191 ����ȡ��i�ڵ���ڵ�ǰλͼ���ж�Ӧ��λƫ��ֵ
	//i>>13�ǽ�i����8192����һ�����̿������λ��
	while (-- i >= 0)
		if (!set_bit(i&8191,p->s_zmap[i>>13]->b_data))
			free++;
	printk("%d/%d free blocks\n\r",free,p->s_nzones);

	//ͳ���豸����i�ڵ���
	free=0;
	i=p->s_ninodes+1;//i���ڳ�������б������豸��i�ڵ�����+1.
	//��1�ǽ�0�ڵ�ҳͳ�ƽ�ȥ
	//Ȼ�����i�ڵ�λͼ����ӦΪռ������������i�ڵ���
	while (-- i >= 0)
		if (!set_bit(i&8191,p->s_imap[i>>13]->b_data))
			free++;
	printk("%d/%d free inodes\n\r",free,p->s_ninodes);
}
