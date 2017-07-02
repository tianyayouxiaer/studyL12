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

//超级块结构表数组
struct super_block super_block[NR_SUPER];
/* this is initialized in init/main.c */
int ROOT_DEV = 0;//根文件系统设备号

static void lock_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sb->s_lock = 1;
	sti();
}

//对指定的超级块解锁
static void free_super(struct super_block * sb)
{
	cli();
	sb->s_lock = 0;
	wake_up(&(sb->s_wait));
	sti();
}

//睡眠等待超级块解锁
static void wait_on_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sti();
}

//取指定设备的超级块
//
struct super_block * get_super(int dev)
{
	struct super_block * s;

	//判断设备的有效性
	if (!dev)
		return NULL;
		
	//遍历超级块数组，寻找指定设备dev的超级块	
	s = 0+super_block;
	while (s < NR_SUPER+super_block)
		if (s->s_dev == dev) {
			wait_on_super(s);
			if (s->s_dev == dev)
				return s;
			s = 0+super_block;//重新遍历超级块数组
		} else
			s++;
	return NULL;
}

//释放（放回）指定的超级块
void put_super(int dev)
{
	struct super_block * sb;
	int i;

	
	if (dev == ROOT_DEV) {
		printk("root diskette changed: prepare for armageddon\n\r");
		return;
	}

	//在超级块数组中寻找指定设备号的文件系统超级块，找不到，则返回
	if (!(sb = get_super(dev)))
		return;
	//找到超级块，但是该超级块指明该文件系统所安装到的i节点还没有被处理过，显示警告信息并返回
	//在文件系统卸载umount中，s_imount会先被置成null，然后才调用本函数
	if (sb->s_imount) {
		printk("Mounted disk changed - tssk, tssk\n\r");
		return;
	}
	//锁定该超级块
	lock_super(sb);
	sb->s_dev = 0;//置该超级块对应的设备号字段为0，即释放该设备上的文件系统超级块
	for(i=0;i<I_MAP_SLOTS;i++)
		brelse(sb->s_imap[i]);//释放该设备上文件系统i节点位图和逻辑位图中所占用的缓冲块
	for(i=0;i<Z_MAP_SLOTS;i++)
		brelse(sb->s_zmap[i]);
	free_super(sb);
	return;
}

//读取指定设备的超级块
static struct super_block * read_super(int dev)
{
	struct super_block * s;
	struct buffer_head * bh;
	int i,block;

	if (!dev)
		return NULL;

	//检查该设备是更换过盘，是否为软盘设备
	//如果更换过，则需要失效处理，即释放原来加载的文件系统
	check_disk_change(dev);
	if (s = get_super(dev))
		return s;
	//在超级块数组中找到一个空页
	for (s = 0+super_block ;; s++) {
		if (s >= NR_SUPER+super_block)
			return NULL;
		if (!s->s_dev)
			break;
	}
	//将该超级块项指向dev上的文件系统
	//对该超级块结构中的内存字段进行部门处理
	s->s_dev = dev;
	s->s_isup = NULL;
	s->s_imount = NULL;
	s->s_time = 0;
	s->s_rd_only = 0;
	s->s_dirt = 0;
	//锁定该超级块，并从设备上读取超级块信息到hb指向的缓冲块中
	lock_super(s);
	//超级块位于块设备的第2个逻辑块中，第一个是引导块
	if (!(bh = bread(dev,1))) {
		s->s_dev=0;
		free_super(s);//付过失败，则释放释放超级块数组中的该项
		return NULL;
	}
	
	//将设备上读取的超级块信息从缓冲块数据区复制到超级块数组相应项结构中
	*((struct d_super_block *) s) =
		*((struct d_super_block *) bh->b_data);
		
	//释放粗放读取信息的高速缓冲块
	brelse(bh);
	
	//现在我们从设备dev上得到了文件系统的超级块
	//检查这个超级块的有效性
	
	if (s->s_magic != SUPER_MAGIC) {
		s->s_dev = 0;
		free_super(s);
		return NULL;
	}
	//读取设备上i节点位图和逻辑块位图数据。
	//首先初始化内存超级块结构中位图空间
	for (i=0;i<I_MAP_SLOTS;i++)
		s->s_imap[i] = NULL;
	for (i=0;i<Z_MAP_SLOTS;i++)
		s->s_zmap[i] = NULL;

	//从设备上读取i节点位图和逻辑块位图信息，并存放在超级块对应字段中
	//i节点位图保存在设备上2号块开始的逻辑块中,共占用s_imap_blocks个块
	//逻辑位图在i节点位图所在块的后续块中，共占s_zmap_block个块
	block=2;
	for (i=0 ; i < s->s_imap_blocks ; i++)//读取设备上的i节点位图
		if (s->s_imap[i]=bread(dev,block))
			block++;
		else
			break;
	for (i=0 ; i < s->s_zmap_blocks ; i++)//读取设备上逻辑位图
		if (s->s_zmap[i]=bread(dev,block))
			block++;
		else
			break;

	//如果读出的位图块数不等与位图应该占用的逻辑块数，说明文件系统位图信息有问题，超级块初始化失败
	//因此只能释放前面申请并占用的所有资源
	//即释放i节点位图和逻辑块位图占用的高速缓冲块
	//释放上面选定的超级块数组项、解锁该超级块项
	if (block != 2+s->s_imap_blocks+s->s_zmap_blocks) {
		for(i=0;i<I_MAP_SLOTS;i++)
			brelse(s->s_imap[i]);
		for(i=0;i<Z_MAP_SLOTS;i++)
			brelse(s->s_zmap[i]);
		s->s_dev=0;
		free_super(s);
		return NULL;
	}

	//一切成功，如果设备上所有i节点已经全部使用，则查找函数会返回0值。
	//0号i节点不能使用，所以这里将位图中第一块的最低位置设置为1
	//防止文件系统分配0号i节点
	s->s_imap[0]->b_data[0] |= 1;
	s->s_zmap[0]->b_data[0] |= 1;
	free_super(s);
	return s;
}

//卸载文件系统
//dev_name为文件系统所在设备的设备文件名
int sys_umount(char * dev_name)
{
	struct m_inode * inode;
	struct super_block * sb;
	int dev;
	
	//根据文件名，找到对应i节点，以获得其中的设备号
	//对于块特殊设备文件，设备号在其i节点的i_zone[0]中
	if (!(inode=namei(dev_name)))
		return -ENOENT;
	dev = inode->i_zone[0];

	//文件系统必须在块设备中，因此，如果不是块设备文件，则放回刚取得的i节点dev_i
	if (!S_ISBLK(inode->i_mode)) {
		iput(inode);
		return -ENOTBLK;
	}
	
	//放回设备i节点	
	iput(inode);

	//如果设备是根文件系统，则不能卸载
	if (dev==ROOT_DEV)
		return -EBUSY;

	//如果没有找到该设备的超级块或者找到但该设备上文件系统没有安装过
	if (!(sb=get_super(dev)) || !(sb->s_imount))
		return -ENOENT;
	//如果超级块所指明的被安装到的i节点并没有置位其安装标志，则警告
	if (!sb->s_imount->i_mount)
		printk("Mounted inode has i_mount=0\n");

	//查找i节点表，看是否有进程在使用该设备上的文件
	for (inode=inode_table+0 ; inode<inode_table+NR_INODE ; inode++)
		if (inode->i_dev==dev && inode->i_count)
				return -EBUSY;

	//实施卸载
	sb->s_imount->i_mount=0;//复位被安装到的i节点的安装标志
	iput(sb->s_imount);//是否i节点
	sb->s_imount = NULL;//超级块中被安装i节点字段为空
	iput(sb->s_isup);//放回设备文件系统根i节点
	sb->s_isup = NULL;//超级块中被安装系统根i节点指针为空
	//释放该设备上的超级块以及位图占用的高速缓冲块，并对该设备执行高速缓冲与设备上数据的同步操作
	put_super(dev);
	sync_dev(dev);
	return 0;
}

//安装文件系统
//dev_name-设备文件名
//dir_name-安装到的目录名
//rw_flag-被安装文件系统的可读写标志
int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
	struct m_inode * dev_i, * dir_i;
	struct super_block * sb;
	int dev;

	//根据文件名，找到对应i节点，以获得其中的设备号
	//对于块特殊设备文件，设备号在其i节点的i_zone[0]中
	if (!(dev_i=namei(dev_name)))
		return -ENOENT;
	dev = dev_i->i_zone[0];
	
	//文件系统必须在块设备中，因此，如果不是块设备文件，则放回刚取得的i节点dev_i
	if (!S_ISBLK(dev_i->i_mode)) {
		iput(dev_i);
		return -EPERM;
	}
	
	//放回设备i节点
	iput(dev_i);

	//检查文件系统安装到的目录名是否有效
	//根据对应的目录名找到对应的i节点dir_i
	if (!(dir_i=namei(dir_name)))
		return -ENOENT;

	//如果i节点的引用计数不为1，或者i节点的节点号是根文件系统的节点号1，则放回该i节点并返回错误码
	if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
		iput(dir_i);
		return -EBUSY;
	}

	//判断是否为目录文件
	if (!S_ISDIR(dir_i->i_mode)) {
		iput(dir_i);
		return -EPERM;
	}
	//读取要安装文件系统的超级块信息
	if (!(sb=read_super(dev))) {
		iput(dir_i);
		return -EBUSY;
	}
	//是否已经安装了文件系统
	if (sb->s_imount) {
		iput(dir_i);
		return -EBUSY;
	}
	if (dir_i->i_mount) {
		iput(dir_i);
		return -EPERM;
	}
	//安装文件系统的超级块“被安装到i节点”字段指向安装到的目录名的i节点
	//设置安装i节点的安装标志和节点已修改标志
	sb->s_imount=dir_i;
	dir_i->i_mount=1;
	dir_i->i_dirt=1;		/* NOTE! we don't iput(dir_i) */
	return 0;			/* we do that in umount */
}

//安装根文件系统
//开机初始化sys_setup中调用
void mount_root(void)
{
	int i,free;
	struct super_block * p;
	struct m_inode * mi;

	if (32 != sizeof (struct d_inode))
		panic("bad i-node size");

	//初始化文件表数组，此处共64项，即系统同时只能打开64个文件
	for(i=0;i<NR_FILE;i++)
		file_table[i].f_count=0;//文件应用计数设备为0（表示空闲）

	//如果根文件系统所在设备是软盘，提示插入软盘
	if (MAJOR(ROOT_DEV) == 2) {
		printk("Insert root floppy and press ENTER");
		wait_for_keypress();
	}
	//初始化超级块表，初始化为0，表示空闲
	for(p = &super_block[0] ; p < &super_block[NR_SUPER] ; p++) {
		p->s_dev = 0;
		p->s_lock = 0;
		p->s_wait = NULL;
	}

	//从根设备上读取文件系统超级块，并取得文件系统的根i节点（1号i节点）在内存i节点表的指针
	if (!(p=read_super(ROOT_DEV)))
		panic("Unable to mount root");
	if (!(mi=iget(ROOT_DEV,ROOT_INO)))
		panic("Unable to read root i-node");

	//现在对超级块和根i节点进行设置
	//把根i节点应用次数增加3
	
	mi->i_count += 3 ;	/* NOTE! it is logically used 4 times, not 1 */
	p->s_isup = p->s_imount = mi;
	current->pwd = mi;
	current->root = mi;
	
	//对根文件系统资源进行统计
	
	free=0;
	i=p->s_nzones;//超级块中表明的设备逻辑块总数
	//根据逻辑块位图中相应位的占用情况统计处空闲块数
	//i&8191 用于取得i节点号在当前位图块中对应的位偏移值
	//i>>13是将i除以8192，即一个磁盘块包含的位数
	while (-- i >= 0)
		if (!set_bit(i&8191,p->s_zmap[i>>13]->b_data))
			free++;
	printk("%d/%d free blocks\n\r",free,p->s_nzones);

	//统计设备空闲i节点数
	free=0;
	i=p->s_ninodes+1;//i等于超级块表中表明的设备上i节点总数+1.
	//加1是将0节点页统计进去
	//然后根据i节点位图中相应为占用情况计算空闲i节点数
	while (-- i >= 0)
		if (!set_bit(i&8191,p->s_imap[i>>13]->b_data))
			free++;
	printk("%d/%d free inodes\n\r",free,p->s_ninodes);
}
