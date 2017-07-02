/*
 *  linux/fs/bitmap.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* bitmap.c contains the code that handles the inode and block bitmaps */
#include <string.h>

#include <linux/sched.h>
#include <linux/kernel.h>

//将指定地址addr处的一块1024字节内存清零
#define clear_block(addr) \
__asm__("cld\n\t" \
	"rep\n\t" \
	"stosl" \
	::"a" (0),"c" (BLOCK_SIZE/4),"D" ((long) (addr)):"cx","di")

#define set_bit(nr,addr) ({\
register int res __asm__("ax"); \
__asm__ __volatile__("btsl %2,%3\n\tsetb %%al": \
"=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
res;})

#define clear_bit(nr,addr) ({\
register int res __asm__("ax"); \
__asm__ __volatile__("btrl %2,%3\n\tsetnb %%al": \
"=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
res;})

//从addr开始的位图中寻找第1个是0的位，并将其addr的位偏移值返回
//addr是缓冲块数据区地址，扫描寻找范围是1024字节，8192bit
#define find_first_zero(addr) ({ \
int __res; \
__asm__("cld\n" \//清方向位
	"1:\tlodsl\n\t" \
	"notl %%eax\n\t" \//eax中每位取反
	"bsfl %%eax,%%edx\n\t" \//从位0扫描eax中是1的第1个位
	"je 2f\n\t" \//如果没有找到，则跳转到2f处
	"addl %%edx,%%ecx\n\t" \//
	"jmp 3f\n" \
	"2:\taddl $32,%%ecx\n\t" \
	"cmpl $8192,%%ecx\n\t" \
	"jl 1b\n" \
	"3:" \
	:"=c" (__res):"c" (0),"S" (addr):"ax","dx","si"); \
__res;})

//释放设备dev上数据区中的逻辑块block
int free_block(int dev, int block)
{
	struct super_block * sb;
	struct buffer_head * bh;
	
	if (!(sb = get_super(dev)))
		panic("trying to free block on nonexistent device");
	//根据数据区开始逻辑号和文件系统中逻辑块总数信息判断参数block的有效性
	if (block < sb->s_firstdatazone || block >= sb->s_nzones)
		panic("trying to free block not in datazone");
	//从hash表上寻找该块数据，若找到则判断其有效性，并清已修改和更新标志，释放数据块
	bh = get_hash_table(dev,block);
	if (bh) {
		if (bh->b_count > 1) {
			brelse(bh);
			return 0;
		}
		bh->b_dirt=0;
		bh->b_uptodate=0;
		if (bh->b_count)
			brelse(bh);
	}
	//复位block在逻辑块位图中的位
	block -= sb->s_firstdatazone - 1 ;
	if (clear_bit(block&8191,sb->s_zmap[block/8192]->b_data)) {
		printk("block (%04x:%d) ",dev,block+sb->s_firstdatazone-1);
		printk("free_block: bit already cleared\n");
	}
	sb->s_zmap[block/8192]->b_dirt = 1;
	return 1;
}

//向设备申请一个逻辑块（盘块，区块）
int new_block(int dev)
{
	struct buffer_head * bh;
	struct super_block * sb;
	int i,j;

	//获取设备超级块
	if (!(sb = get_super(dev)))
		panic("trying to get new block from nonexistant device");

	//在超级块中的逻辑块位图中寻找第一个0值位（代表一个空闲的逻辑块）
	j = 8192;
	for (i=0 ; i<8 ; i++)
		if (bh=sb->s_zmap[i])
			if ((j=find_first_zero(bh->b_data))<8192)
				break;
	if (i>=8 || !bh || j>=8192)
		return 0;

	//置位对应逻辑块位图中的位
	if (set_bit(j,bh->b_data))
		panic("new_block: bit already set");
	bh->b_dirt = 1;
	//逻辑位图仅表示盘上数据区中逻辑块的占用情况，即逻辑块位图中位偏移
	//值表示从数据区开始处算起的块号，因此这里需要加上数据区第一个逻辑块
	//的块号，把j转换成逻辑块号
	j += i*8192 + sb->s_firstdatazone-1;
	if (j >= sb->s_nzones)
		return 0;
	//然后在高速缓冲区中为该设备上指定的逻辑块取一个缓冲块
	if (!(bh=getblk(dev,j)))
		panic("new_block: cannot get block");
	//刚刚取得的逻辑块其引用次数一定为1
	if (bh->b_count != 1)
		panic("new block: count is != 1");
	//将新逻辑块清0
	clear_block(bh->b_data);
	//设置标志
	bh->b_uptodate = 1;
	bh->b_dirt = 1;
	//释放对应缓冲块
	brelse(bh);
	//返回逻辑块号
	return j;
}

//释放指定的i节点
void free_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;

	//1、判断i节点的合理性
	//参数判断
	if (!inode)
		return;

	//如果i节点上的设备号字段为0，则说明该节点没有使用
	//清空i节点所占内存并返回
	if (!inode->i_dev) {
		memset(inode,0,sizeof(*inode));
		return;
	}
	//如果i几点还有其它程序引用，则不能释放
	if (inode->i_count>1) {
		printk("trying to free inode with count=%d\n",inode->i_count);
		panic("free_inode");
	}

	//如果文件连接数部位0，则表示还有其它文件目录项在使用该节点，因此不能释放，而应该放回等待
	if (inode->i_nlinks)
		panic("trying to free inode with links");

	//2、
	//利用超级块信息对其中i节点位图进行操作
	if (!(sb = get_super(inode->i_dev)))
		panic("trying to free inode on nonexistent device");
	//判断i节点范围（1< i_num < s_ninodes）；0号i节点保留没有使用，s_ninodes:该设备i节点总数
	if (inode->i_num < 1 || inode->i_num > sb->s_ninodes)
		panic("trying to free inode 0 or nonexistant inode");
	//i节点对应的位图不存在，则出错
	//1个缓冲块的i节点位图有8192个字节
	if (!(bh=sb->s_imap[inode->i_num>>13]))
		panic("nonexistent imap in superblock");
	//复位i节点对应的节点位图中的位
	if (clear_bit(inode->i_num&8191,bh->b_data))
		printk("free_inode: bit already cleared.\n\r");
	bh->b_dirt = 1;
	//情况该i节点所占用的内存区
	memset(inode,0,sizeof(*inode));
}

//为设备dev建立一个新的i节点。初始化并返回该新i节点的指针
//从内存i节点表中获取一个空闲i节点表项，并从i节点位图中到一个空闲i节点
struct m_inode * new_inode(int dev)
{
	struct m_inode * inode;
	struct super_block * sb;
	struct buffer_head * bh;
	int i,j;

	//首先熊内存i节点表中获取一个空闲i节点项
	if (!(inode=get_empty_inode()))
		return NULL;

	//读取指定设备的超级块结构
	if (!(sb = get_super(dev)))
		panic("new_inode with unknown device");

	//扫描超级块中8块i节点位图，寻找第1个0位，寻找空闲节点，获取放置该i节点的节点号。
	j = 8192;
	for (i=0 ; i<8 ; i++)
		if (bh=sb->s_imap[i])
			if ((j=find_first_zero(bh->b_data))<8192)
				break;
	//没找到，或者位图缓冲块无效，则放回先申请的i节点表中的i节点
	if (!bh || j >= 8192 || j+i*8192 > sb->s_ninodes) {
		iput(inode);
		return NULL;
	}
	//找到还未使用的i节点号j，于是置位i节点j对应的i节点位图相应bit位
	if (set_bit(j,bh->b_data))
		panic("new_inode: bit already set");
	bh->b_dirt = 1;//已修改
	inode->i_count=1;
	inode->i_nlinks=1;
	inode->i_dev=dev;//i节点所在的设备号
	inode->i_uid=current->euid;
	inode->i_gid=current->egid;
	inode->i_dirt=1;
	inode->i_num = j + i*8192;//对应设备中的i节点号
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	return inode;
}
