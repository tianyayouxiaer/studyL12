/*
 *  linux/fs/inode.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/system.h>

//设备数据块总数指针数组，每个指针项指向指定主设备号的总块数数组hd_size[]。
//总块数数组每一项对应子设备号确定的一个子设备上所拥有的数据块总数（1块大小=1KB）
extern int *blk_size[];

//内存中i节点表
struct m_inode inode_table[NR_INODE]={{0,},};

//读指定i节点号的i节点信息
static void read_inode(struct m_inode * inode);
//写i节点信息到高速缓冲中
static void write_inode(struct m_inode * inode);

//等待指定的i节点可用
//如果i节点已被锁定，则将当前任务置为不可中断的等待状态，并添加到该i节点的等待队列i_wait中，
//一直到该i节点解锁并明确的唤醒本任务
static inline void wait_on_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	sti();
}
//对i节点上锁（锁定指定的i节点）
static inline void lock_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	inode->i_lock=1;
	sti();
}

//对i节点解锁
static inline void unlock_inode(struct m_inode * inode)
{
	inode->i_lock=0;
	wake_up(&inode->i_wait);
}

//释放设备dev在内存i节点表中的所有i节点
void invalidate_inodes(int dev)
{
	int i;
	struct m_inode * inode;

	//扫描i节点数组，释放该设备的所有的i节点
	inode = 0+inode_table;
	for(i=0 ; i<NR_INODE ; i++,inode++) {
		wait_on_inode(inode);

		if (inode->i_dev == dev) {
			if (inode->i_count)
				printk("inode in use on removed disk\n\r");
			inode->i_dev = inode->i_dirt = 0;//释放i节点（置设备号为0）
		}
	}
}

//同步所有的i节点
//把内存i节点表中所有i节点与设备上i节点作同步操作
void sync_inodes(void)
{
	int i;
	struct m_inode * inode;

	//扫描内存中的i节点数组，对于已被修改并且不是管道的i节点，写入高速缓冲区，
	//缓冲区管理程序会在时机写入盘中
	inode = 0+inode_table;
	for(i=0 ; i<NR_INODE ; i++,inode++) {
		wait_on_inode(inode);
		if (inode->i_dirt && !inode->i_pipe)
			write_inode(inode);
	}
}

//文件数据块映射到盘块的处理操作（block位图处理函数）
//inode - 文件的i节点指针
//block - 文件中的数据块号
//create - 创建块标志
static int _bmap(struct m_inode * inode,int block,int create)
{
	struct buffer_head * bh;
	int i;

	//判断文件数据块号的有效性
	//（文件系统表示范围）0 < block < 直接块数 + 间接块数 + 二次间接块数
	if (block<0)
		panic("_bmap: block<0");
	if (block >= 7+512+512*512)
		panic("_bmap: block>big");

	//block<7，则用直接块表示
	if (block<7) {
		//如果创建标志置位 且 i节点中对应该块的逻辑块字段为0，则向相应设备申请已盘块
		if (create && !inode->i_zone[block])
			//向设备申请盘块，并将盘上逻辑块号（盘块号）填如逻辑字段中
			if (inode->i_zone[block]=new_block(inode->i_dev)) {
				inode->i_ctime=CURRENT_TIME;
				inode->i_dirt=1;//设置已修改标志
			}
		return inode->i_zone[block];
	}

	//如果7<=block<512，则使用的是一次间接块
	block -= 7;
	if (block<512) {
		//创建 且 i_zone[7] = 0,则是首次使用间接块
		if (create && !inode->i_zone[7])
			//需要申请一个磁盘块用于存放间接信息
			if (inode->i_zone[7]=new_block(inode->i_dev)) {
				inode->i_dirt=1;
				inode->i_ctime=CURRENT_TIME;
			}
		//申请间接块失败，则i_zone[7]为0
		if (!inode->i_zone[7])
			return 0;
		//读取设备上该i节点的一次间接块
		if (!(bh = bread(inode->i_dev,inode->i_zone[7])))
			return 0;
			
		//取该间接块上第block项中的逻辑块号（盘块号）i，每一项占2个字节
		i = ((unsigned short *) (bh->b_data))[block];
		//如果创建 且 间接块第block项中的逻辑块号为0，则申请一磁盘块
		if (create && !i)
			if (i=new_block(inode->i_dev)) {
				//让间接块中的第block项等于该新逻辑块号
				((unsigned short *) (bh->b_data))[block]=i;
				//置位修改标志
				bh->b_dirt=1;
			}
		//如果不是创建，则i就是需要寻找的逻辑块号
		brelse(bh);//释放该间接块占用的缓冲块
		return i;
	}

	//到此，说明是二次间接块
	//block减去间接块所容纳的块数
	block -= 512;
	//根据创建标志创建
	if (create && !inode->i_zone[8])
		if (inode->i_zone[8]=new_block(inode->i_dev)) {
			inode->i_dirt=1;
			inode->i_ctime=CURRENT_TIME;
		}
	if (!inode->i_zone[8])
		return 0;
	//读取该设备上该i节点的二次间接块
	if (!(bh=bread(inode->i_dev,inode->i_zone[8])))
		return 0;
	//取该二次间接块的一级块上第block/512项中的逻辑块号i
	i = ((unsigned short *)bh->b_data)[block>>9];
	//如果创建且逻辑块号为0，则需要在磁盘上申请一磁盘块作为二次间接块的二级块i
	if (create && !i)
		if (i=new_block(inode->i_dev)) {
			((unsigned short *) (bh->b_data))[block>>9]=i;
			bh->b_dirt=1;
		}
		
	brelse(bh);

	//如果二次间接块的二级块号为0，表示申请磁盘块失败或原来对应块号就为0
	if (!i)
		return 0;
	//从设备上读区二次间接块的二级块，并取该二级块上第block项目中的逻辑块号
	if (!(bh=bread(inode->i_dev,i)))
		return 0;
	i = ((unsigned short *)bh->b_data)[block&511];
	//如果穿件并且二级块的第block项中逻辑块号为0，则申请一磁盘块，最为最终存放数据信息块
	if (create && !i)
		if (i=new_block(inode->i_dev)) {
			((unsigned short *) (bh->b_data))[block&511]=i;
			bh->b_dirt=1;
		}
	brelse(bh);
	return i;
}

//取文件数据块block在设备上对应的逻辑块号
//如果对应的逻辑块不存在就创建一块，并返回设备上对应的逻辑块号
//block-文件中的数据块号
int bmap(struct m_inode * inode,int block)
{
	return _bmap(inode,block,0);
}

//取文件数据块block在设备上对应的逻辑块号
//如果对应的逻辑块不存在就创建一块，并返回设备上对应的逻辑块号
//block-文件中的数据块号
int create_block(struct m_inode * inode, int block)
{
	return _bmap(inode,block,1);
}

//放回一个i节点（回写入设备）
void iput(struct m_inode * inode)
{
	//参数判断
	if (!inode)
		return;

	//等待该i节点解锁
	wait_on_inode(inode);

	//如果引用计数为0，表示该i节点已经是空闲的
	if (!inode->i_count)
		panic("iput: trying to free free inode");

	//如果是管道i节点，则唤醒等待该管道的进程，引用计数减1
	if (inode->i_pipe) {
		wake_up(&inode->i_wait);
		wake_up(&inode->i_wait2);
		//引用次数减1，如果还有引用则返回
		if (--inode->i_count)
			return;
		//释放管道占用的内存页面，并复位该节点引用计数，已修改标志和管道标志
		//对于管道节点，inode->i_size存放着内存页地址
		free_page(inode->i_size);
		inode->i_count=0;
		inode->i_dirt=0;
		inode->i_pipe=0;
		return;
	}

	//如果i节点对应设备号为0，则引用计数减1，返回
	//如：管道操作的i节点，其i节点的设备号为0
	if (!inode->i_dev) {
		inode->i_count--;
		return;
	}
	
	//如果是块设备文件的i节点，此时逻辑块字段0（i_zone[0]）中是设备号，则刷新该设备
	if (S_ISBLK(inode->i_mode)) {
		sync_dev(inode->i_zone[0]);
		wait_on_inode(inode);
	}
	
repeat:
	//如果i节点引用计数大于1，则计数递减后直接返回，因为i节点还有人在用，不能释放
	if (inode->i_count>1) {
		inode->i_count--;
		return;
	}

	//否则，i节点没有人用了
	//如果i节点的链接数为0，则说明i节点对应文件被删除。
	if (!inode->i_nlinks) {
		truncate(inode);//释放该i节点所有逻辑块
		free_inode(inode);//释放该i节点
		return;
	}

	//如果i节点已作过修改，则会写更新该i节点，并等待该i节点解锁
	if (inode->i_dirt) {
		write_inode(inode);	/* we can sleep - so do again */
		wait_on_inode(inode);//因为睡眠了，所以需要重复判断
		goto repeat;
	}
	//到此，说明，i节点引用计数为1，链接数部位0，并且内容没有被修改
	//i_count=0，表示已释放
	inode->i_count--;
	return;
}

//从i节点表（inode_table）中获取一个空闲的i节点项
//寻找引用计数count为0的i节点，并将其希尔盘后清0，返回指针；
//引用计数加1
struct m_inode * get_empty_inode(void)
{
	//1、寻找一个空闲的i节点
	struct m_inode * inode;
	//指向i节点表项第1项
	static struct m_inode * last_inode = inode_table;
	int i;
	
	do {
		inode = NULL;
		//遍历i节点数组
		for (i = NR_INODE; i ; i--) {
			//如果last_inode已经指向i节点表的最后1项后，则让其重新指向i节点开始处
			if (++last_inode >= inode_table + NR_INODE)
				last_inode = inode_table;

			//如果找到空闲项
			if (!last_inode->i_count) {
				inode = last_inode;
				//该节点i_dirt和i_lock标志均未置上，则直接退出
				if (!inode->i_dirt && !inode->i_lock)
					break;
			}
		}
		
		//退出上面的循环有两种方式：break退出和i=0退出
		//如果没有找到空闲i节点，则将i节点表打印出来调试
		if (!inode) {
			for (i=0 ; i<NR_INODE ; i++)
				printk("%04x: %6d\t",inode_table[i].i_dev,
					inode_table[i].i_num);
			panic("No free inodes in mem");
		}
		
		//等待i节点解锁（如果被锁上）
		wait_on_inode(inode);
		//如果该i节点已修改标志被置位，则将该i节点刷新。
		while (inode->i_dirt) {
			write_inode(inode);
			//因为刷新是可能会睡眠，因此需再循环等待i节点解锁
			wait_on_inode(inode);
		}
	} while (inode->i_count);
	
	//2、初始化找到的空闲的i节点
	//对找到的i节点清空，引用计数加1
	memset(inode,0,sizeof(*inode));
	inode->i_count = 1;
	return inode;
}

//获取管道节点
struct m_inode * get_pipe_inode(void)
{
	struct m_inode * inode;

	//从内存i节点表获取一个空闲i节点
	if (!(inode = get_empty_inode()))
		return NULL;
	//为该i节点申请一页内存，并让i_size字段指向该页面
	if (!(inode->i_size=get_free_page())) {
		inode->i_count = 0;
		return NULL;
	}
	//读写两者总计
	inode->i_count = 2;	/* sum of readers/writers */
	PIPE_HEAD(*inode) = PIPE_TAIL(*inode) = 0;//复位管道头尾指针
	inode->i_pipe = 1;//置节点管道使用标志
	return inode;
}

//从设备dev上读取指定节点号nr的i节点
//nr - i节点号
struct m_inode * iget(int dev,int nr)
{
	struct m_inode * inode, * empty;

	//参数检查
	if (!dev)
		panic("iget with dev==0");

	//从i节点表中取一个空闲i节点备用
	empty = get_empty_inode();

	//扫描i节点表
	inode = inode_table;
	while (inode < NR_INODE+inode_table) {
		//寻找参数指定节点号为nr的i节点
		if (inode->i_dev != dev || inode->i_num != nr) {
			//如果不满足，扫描下一个
			inode++;
			continue;
		}

		//找到设备号dev和节点号nr的i节点，则等待该i节点解锁（如果上锁了）
		wait_on_inode(inode);

		//在等待i节点解锁的过程中，i节点可能发生变化
		//再次进行判断，如果变化，从i节点表从头开始扫描
		if (inode->i_dev != dev || inode->i_num != nr) {
			inode = inode_table;
			continue;
		}

		//到此，找到了i节点
		//i节点引用计数加1
		inode->i_count++;

		//再进一步检测，看它是否为另外一个系统的安装点
		if (inode->i_mount) {
			int i;

			//如该i节点是其它文件系统安装点，则在超级块表中搜寻安装此i节点的超级块
			for (i = 0 ; i<NR_SUPER ; i++)
				if (super_block[i].s_imount==inode)
					break;

			//如果没有找到，则出错
			if (i >= NR_SUPER) {
				printk("Mounted inode hasn't got sb\n");
				//放回函数开始后去的空闲节点empty，返回该i节点指针
				if (empty)
					iput(empty);
				return inode;
			}

			//到此，表示找到安装到inode节点的文件系统超级块
			//将i节点写盘放回
			iput(inode);
			//从安装在此i节点上的文件系统超级块中取设备号,并令i节点号为1
			//然后从新扫描整个i节点表，以获取该被安装文件系统的根i节点信息
			dev = super_block[i].s_dev;
			nr = ROOT_INO;
			inode = inode_table;
			continue;
		}

		//到此，我们找到了相应的i节点，因此，可以放弃申请的空间i节点，返回找到的i节点指针
		if (empty)
			iput(empty);
		return inode;
	}

	//如果我们在i节点表中么有找到指定的i节点，则利用申请的空闲i节点empty在
	//i节点表中建立该i节点
	if (!empty)
		return (NULL);
	inode=empty;
	inode->i_dev = dev;
	inode->i_num = nr;

	//从设备上读取i节点信息，返回i节点指针
	read_inode(inode);
	return inode;
}

//读取指定的i节点信息
static void read_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	//锁定该i节点
	lock_inode(inode);

	//取该i节点所在设备的超级块
	if (!(sb=get_super(inode->i_dev)))
		panic("trying to read inode without dev");
	//获取该i节点所在设备的设备逻辑块号或缓冲块
	block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
		(inode->i_num-1)/INODES_PER_BLOCK;

	//确定逻辑块号后，把该逻辑块读入一缓冲块中
	if (!(bh=bread(inode->i_dev,block)))
		panic("unable to read i-node block");

	//复制指定i节点内容到inode指针所值位置处？？？
	*(struct d_inode *)inode =
		((struct d_inode *)bh->b_data)
			[(inode->i_num-1)%INODES_PER_BLOCK];
			
	//最后释放读入的缓冲块
	brelse(bh);

	//如果是块设备，设置i节点的文件最大长度值
	if (S_ISBLK(inode->i_mode)) {
		int i = inode->i_zone[0];//对于块设备，i_zone[0]中是设备号
		if (blk_size[MAJOR(i)])
			inode->i_size = 1024*blk_size[MAJOR(i)][MINOR(i)];
		else
			inode->i_size = 0x7fffffff;
	}
	unlock_inode(inode);
}

//将i节点信息写入缓冲区中
//该函数把指定的i节点写入缓冲区相应的缓冲块中，待缓冲区刷新时会写入盘中

static void write_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	//锁定i节点
	lock_inode(inode);
	if (!inode->i_dirt || !inode->i_dev) {
		//如果i节点没有被修改过或者i节点的设备号等于0，则解锁该i节点并退出
		//因为对于没有修改过的i节点，其内容与缓冲区中或设备中的相同
		unlock_inode(inode);
		return;
	}

	//为了确定所在设备缓冲块，必须先获取相应设备的超级块，以获取用于计算逻辑块号的每块
	//i节点信息INODES_PER_BLOCK
	if (!(sb=get_super(inode->i_dev)))
		panic("trying to write inode without device");

	//该i节点所在额设备逻辑块号 = （启动块+超级块       ）+ i节点位图所占用的块数 +
	//逻辑块位图占用的块数 + （i节点号 - 1）/每块含有的i节点数
	block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
		(inode->i_num-1)/INODES_PER_BLOCK;

	//从设备上读取该i节点所在的逻辑块，并将该i节点信息复制到逻辑块对应的i节点的项位置
	if (!(bh=bread(inode->i_dev,block)))
		panic("unable to read i-node block");
	((struct d_inode *)bh->b_data)
		[(inode->i_num-1)%INODES_PER_BLOCK] =
			*(struct d_inode *)inode;
	//置缓冲区已修改标志，
	bh->b_dirt=1;
	//i节点内容已经与缓冲区中的一致，因此修改标志置0
	inode->i_dirt=0;
	//释放该含有i节点的缓冲块
	brelse(bh);
	//解锁该i节点
	unlock_inode(inode);
}
