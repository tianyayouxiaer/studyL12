/*
 *  linux/fs/buffer.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'buffer.c' implements the buffer-cache functions. Race-conditions have
 * been avoided by NEVER letting a interrupt change a buffer (except for the
 * data, of course), but instead letting the caller do it. NOTE! As interrupts
 * can wake up a caller, some cli-sti sequences are needed to check for
 * sleep-on-calls. These should be extremely quick, though (I hope).
 */

/*
 * NOTE! There is one discordant note here: checking floppies for
 * disk change. This is where it fits best, I think, as it should
 * invalidate changed floppy-disk-caches.
 */

#include <stdarg.h>
 
#include <linux/config.h>//变参，用于vsprintf，vprintf，vfprintf函数
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/io.h>
//链接程序ld生成，用于表明内核代码段末端。即指明内核末端位置
//这里表明高速缓冲区开始于内核代码末端位置
extern int end;

struct buffer_head * start_buffer = (struct buffer_head *) &end;//start_buffer指向高速缓冲区起始位置
struct buffer_head * hash_table[NR_HASH];
static struct buffer_head * free_list;
//等待空闲缓冲块而睡眠的任务队列
//它和b_wait的区别：任务申请缓冲块而系统缺乏可用空闲缓冲块时，当前任务被添加到buffer_wait睡眠等待队列中
//b_wait专门供指定缓冲块的任务使用
static struct task_struct * buffer_wait = NULL;
int NR_BUFFERS = 0;

//等待指针缓冲块解锁
//如果缓冲块bh已经上锁，就让进程不可中断的睡在该缓冲块的等待队列b_wait上。
//在缓冲块解锁时，其等待队列上的所有进程都将被唤醒
//虽然关闭中断后睡眠，但不会影响其它进程上下文响应中断，应为每个进程都在自己的tss段
//中保存了标志寄存器eflags值，在进程切换时，cpu中当前eflags值页随之改变
//sleep_on进入睡眠状态需要wake_up明确的唤醒
static inline void wait_on_buffer(struct buffer_head * bh)
{
	cli();//关中断
	while (bh->b_lock)//如果已被上锁，则进程进睡眠，等待其解锁
		sleep_on(&bh->b_wait);
	sti();//开中断
}

//设备数据同步
int sys_sync(void)
{
	int i;
	struct buffer_head * bh;

	//i节点同步函数，把内存i节点表中所有修改过的i节点写入高速缓冲中。
	sync_inodes();		/* write out inodes into buffers */
	//扫描所有高速缓冲u，对已被修改的缓冲块产生写磁盘请求，将缓冲中的数据写入盘中，做到高速缓冲中数据和
	//设备中数据同步
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		wait_on_buffer(bh);
		if (bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
	return 0;
}

//对指定的设备进行高速缓冲数据与设备上数据的同步
int sync_dev(int dev)
{
	int i;
	struct buffer_head * bh;

	//1、对指定设备执行数据同步
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)//不是该设备，跳过
			continue;
		wait_on_buffer(bh);//对要同步的高速缓冲块，如果已经被上锁，则等待解锁
		if (bh->b_dev == dev && bh->b_dirt)//再判断一次设备和修改标志，若是，则执行写盘操作
			ll_rw_block(WRITE,bh);
	}
	
	//2、再将i节点数据写入高速缓冲。让i节点表inode_table中的inode与缓冲中的信息同步
	sync_inodes();

	//3、再执行一次数据同步
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
	return 0;
}

//使指定设备在高速缓冲区中数据无效
void inline invalidate_buffers(int dev)
{
	int i;
	struct buffer_head * bh;
	//扫描高速缓冲区所有缓冲块，对指定设的缓冲块复位其有效标志和已修改标志
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		if (bh->b_dev == dev)
			bh->b_uptodate = bh->b_dirt = 0;
	}
}

/*
 * This routine checks whether a floppy has been changed, and
 * invalidates all buffer-cache-entries in that case. This
 * is a relatively slow routine, so we have to try to minimize using
 * it. Thus it is called only upon a 'mount' or 'open'. This
 * is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle of an operation deserve
 * to loose :-)
 *
 * NOTE! Although currently this is only for floppies, the idea is
 * that any additional removable block-device will use this routine,
 * and that mount/open needn't know that floppies/whatever are
 * special.
 */
 //检测磁盘是否更换，如果已更换就使对应高速缓冲区无效
void check_disk_change(int dev)
{
	int i;

	//首先检查是否时软盘设备
	if (MAJOR(dev) != 2)
		return;
	if (!floppy_change(dev & 0x03))
		return;
	
	for (i=0 ; i<NR_SUPER ; i++)
		if (super_block[i].s_dev == dev)
			put_super(super_block[i].s_dev);
	invalidate_inodes(dev);
	invalidate_buffers(dev);
}

//hash：减少查找比较元素所花费时间,在存储位置和关键字之间建立一一对应关系，通过计算立刻查到指定的元素
//hash函数定义：（设备号^逻辑块号）% 307
#define _hashfn(dev,block) (((unsigned)(dev^block))%NR_HASH)
//hash表项计算
#define hash(dev,block) hash_table[_hashfn(dev,block)]

//从hash队列和空闲缓冲队列中移走缓冲块
//hash队列和空闲缓冲链表是双向链表结构
static inline void remove_from_queues(struct buffer_head * bh)
{
/* remove from hash-queue */
	if (bh->b_next)
		bh->b_next->b_prev = bh->b_prev;
	if (bh->b_prev)
		bh->b_prev->b_next = bh->b_next;
	if (hash(bh->b_dev,bh->b_blocknr) == bh)
		hash(bh->b_dev,bh->b_blocknr) = bh->b_next;
/* remove from free list */
	if (!(bh->b_prev_free) || !(bh->b_next_free))
		panic("Free block list corrupted");
	bh->b_prev_free->b_next_free = bh->b_next_free;
	bh->b_next_free->b_prev_free = bh->b_prev_free;
	//如果空闲链表头指向本缓冲区，则让其指向下一个缓冲区
	if (free_list == bh)
		free_list = bh->b_next_free;
}


//将缓冲块插入空闲链表尾部，同时放入hash队列中
static inline void insert_into_queues(struct buffer_head * bh)
{
/* put at end of free list */
	bh->b_next_free = free_list;
	bh->b_prev_free = free_list->b_prev_free;
	free_list->b_prev_free->b_next_free = bh;
	free_list->b_prev_free = bh;
/* put the buffer in new hash-queue if it has a device */
	bh->b_prev = NULL;
	bh->b_next = NULL;
	if (!bh->b_dev)
		return;
	bh->b_next = hash(bh->b_dev,bh->b_blocknr);
	hash(bh->b_dev,bh->b_blocknr) = bh;
	bh->b_next->b_prev = bh;//此前应加入if(bh->b_next)判断
}

//利用hash表在高速缓冲中寻找给定设备和指定块号的缓冲区块
//找到返回缓冲块指针，否则NULL
static struct buffer_head * find_buffer(int dev, int block)
{		
	struct buffer_head * tmp;

	//hash计算出具有相同散列值的缓冲块链接在散列数据同一项链表上
	for (tmp = hash(dev,block) ; tmp != NULL ; tmp = tmp->b_next)
		if (tmp->b_dev==dev && tmp->b_blocknr==block)
			return tmp;
	return NULL;
}

/*
 * Why like this, I hear you say... The reason is race-conditions.
 * As we don't lock buffers (unless we are readint them, that is),
 * something might happen to it while we sleep (ie a read-error
 * will force it bad). This shouldn't really happen currently, but
 * the code is ready.
 */
 //利用hash表在高速缓冲区中寻找指定的缓冲块。若找到，则丢该缓冲块加锁big返回块指针
struct buffer_head * get_hash_table(int dev, int block)
{
	struct buffer_head * bh;

	for (;;) {
		if (!(bh=find_buffer(dev,block)))
			return NULL;
		bh->b_count++;//对该缓冲块增加引用计数
		wait_on_buffer(bh);//等待该缓冲解锁（如果已被上锁）

		//由于经过了睡眠状态，因此有必要再验证该缓冲块的正确性并返回缓冲头指针
		if (bh->b_dev == dev && bh->b_blocknr == block)
			return bh;
			
		bh->b_count--;//如果睡眠时该缓冲块所属的设备号或块号发生了变化，则撤销对它的应用计数并重新寻找
	}
}

/*
 * Ok, this is getblk, and it isn't very clear, again to hinder
 * race-conditions. Most of the code is seldom used, (ie repeating),
 * so it should be much more efficient than it looks.
 *
 * The algoritm is changed: hopefully better, and an elusive bug removed.
 */
//判断缓冲区的修改标志和锁定标志，并且定义修改标志的权重要笔锁定标志大
//当数据被写入缓冲块，但还未写入设备时，b_dirt = 1，b_uptodate = 0
//当数据被写入块设备或刚从块设备读入缓冲区b_uptodate = 1
//新申请一个设备缓冲块时，b_dirt和b_uptodate都为1，表示缓冲区数据虽与块设备不同，但数据任然有效（更新的）
//程序使用该缓冲块时，锁定该缓冲块
#define BADNESS(bh) (((bh)->b_dirt<<1)+(bh)->b_lock)

//取高速缓冲区中指定的缓冲块
struct buffer_head * getblk(int dev,int block)
{
	struct buffer_head * tmp, * bh;

repeat:
	//1、搜索hash表，如果指定块已经在高速缓冲区中，则返回对应缓冲区头指针，退出
	if (bh = get_hash_table(dev,block))
		return bh;

	//2、扫描空闲数据块链表，寻找空闲缓冲区
	tmp = free_list;//tmp指向空闲链表的第一个空闲缓冲区头
	do {
		if (tmp->b_count)//如果该缓冲区正在被使用，则扫描下一项
			continue;

		//case1:bh == NULL
		//case2:bh != NULL 并且 BADNESS(tmp)<BADNESS(bh)，
		//有可能b_count为0，但是b_dirt和b_lock并不为0

		//找到链表中权重最小的，这个算法简洁精妙
		if (!bh || BADNESS(tmp)<BADNESS(bh)) {
			bh = tmp;//bh指向tmp缓冲头
			if (!BADNESS(tmp))//如果该tmp缓冲头即没有修改也没有锁定，则说明已为指定设备上的块取得对应的高速缓冲块
				break;//退出，否则我们继续循环，找到BADNESS()最小的缓冲块
		}
/* and repeat until we find something good */
	} while ((tmp = tmp->b_next_free) != free_list);

	//3、如果所有缓冲块b_coun>0，则睡眠等待有空闲缓冲块可用
	//当有空闲缓冲块可用时本进程会被明确的唤醒
	//唤醒后重新查找空闲缓冲块
	if (!bh) {
		sleep_on(&buffer_wait);
		goto repeat;
	}
	//我们已经找到空闲缓冲块，先等待该缓冲块解锁（如果被上锁）
	wait_on_buffer(bh);
	if (bh->b_count)//如果睡眠阶段该缓冲区被其它任务使用的话，只好再去寻找一个缓冲块
		goto repeat;

	//如果该缓冲区已被修改，则将数据写盘，并再次等待缓冲区解锁。
	while (bh->b_dirt) {
		sync_dev(bh->b_dev);
		wait_on_buffer(bh);
		if (bh->b_count)//如果睡眠阶段该缓冲区被其它任务使用的话，只好再去寻找一个缓冲块
		if (bh->b_count)
			goto repeat;
	}
/* NOTE!! While we slept waiting for this block, somebody else might */
/* already have added "this" block to the cache. check it */
//在高速缓冲hash表中检查指定设备和块的缓冲块是否乘睡眠之际已经被加入进去，如果是，就再次重复上述寻找过程
	if (find_buffer(dev,block))
		goto repeat;
/* OK, FINALLY we know that this buffer is the only one of it's kind, */
/* and that it's unused (b_count=0), unlocked (b_lock=0), and clean */
	//最终找到没被占用的缓冲块（unused (b_count=0), unlocked (b_lock=0), and clean）
	bh->b_count=1;
	bh->b_dirt=0;//复位修改标志
	bh->b_uptodate=0;//复位更新标志
	//从hash队列和空闲块链表队列中移除该缓冲区头，让该缓冲区用于执行设备和其上的指定块
	remove_from_queues(bh);
	//然后根据此新的设备号可快号重新插入空闲链表hash队列新位置处
	bh->b_dev=dev;
	bh->b_blocknr=block;
	insert_into_queues(bh);
	return bh;
}

//释放指定的缓冲块
//等待该缓冲块解锁，然后引用计数减1，并明确唤醒等待空闲缓冲块的进程
void brelse(struct buffer_head * buf)
{
	if (!buf)
		return;
	wait_on_buffer(buf);
	if (!(buf->b_count--))
		panic("Trying to free free buffer");
	wake_up(&buffer_wait);
}

/*
 * bread() reads a specified block and returns the buffer that contains
 * it. It returns NULL if the block was unreadable.
 */
 //从指定设备读取指定的数据块并返回含有数据的缓冲区，如果指定的块不存在则返回NULL
struct buffer_head * bread(int dev,int block)
{
	struct buffer_head * bh;

	//1、根据dev和block在高速缓冲区申请一块缓冲块
	if (!(bh=getblk(dev,block)))
		panic("bread: getblk returned NULL\n");
	//如果缓冲块中数据时有效的（已更新）可以直接使用，则返回
	if (bh->b_uptodate)
		return bh;
	//否则块设备底层读写函数，产生读设备块请求
	ll_rw_block(READ,bh);
	//等待指定数据块被读入，并等待缓冲区解锁
	wait_on_buffer(bh);
	//醒来后如果缓冲区已被更新，则返回缓冲区头指针
	if (bh->b_uptodate)
		return bh;
	//否则，表明操作设备失败，释放该缓冲区，返回null
	brelse(bh);
	return NULL;
}

//复制内存块，从from地址复制一块（1024字节）数据到to位置
#define COPYBLK(from,to) \
__asm__("cld\n\t" \
	"rep\n\t" \
	"movsl\n\t" \
	::"c" (BLOCK_SIZE/4),"S" (from),"D" (to) \
	:"cx","di","si")

/*
 * bread_page reads four buffers into memory at the desired address. It's
 * a function of its own, as there is some speed to be got by reading them
 * all at the same time, not waiting for one to be read, and then another
 * etc.
 */
 //一次读四个缓冲块数据到内存指定地址处，读四块，可以提高速度
void bread_page(unsigned long address,int dev,int b[4])
{
	struct buffer_head * bh[4];
	int i;

	for (i=0 ; i<4 ; i++)
		if (b[i]) {
			if (bh[i] = getblk(dev,b[i]))
				if (!bh[i]->b_uptodate)
					ll_rw_block(READ,bh[i]);
		} else
			bh[i] = NULL;
	for (i=0 ; i<4 ; i++,address += BLOCK_SIZE)
		if (bh[i]) {
			wait_on_buffer(bh[i]);
			if (bh[i]->b_uptodate)
				COPYBLK((unsigned long) bh[i]->b_data,address);
			brelse(bh[i]);
		}
}

/*
 * Ok, breada can be used as bread, but additionally to mark other
 * blocks for reading as well. End the argument list with a negative
 * number.
 */
 //从指定设备读取指定的一些块
 //函数参数可变，时一系列指定的块号。成功时返回第一块的缓冲块头指针，否则返回NULL
struct buffer_head * breada(int dev,int first, ...)
{
	va_list args;
	struct buffer_head * bh, *tmp;

	va_start(args,first);
	if (!(bh=getblk(dev,first)))
		panic("bread: getblk returned NULL\n");
	if (!bh->b_uptodate)
		ll_rw_block(READ,bh);
	while ((first=va_arg(args,int))>=0) {
		tmp=getblk(dev,first);
		if (tmp) {
			if (!tmp->b_uptodate)
				ll_rw_block(READA,bh);
			tmp->b_count--;
		}
	}
	va_end(args);
	wait_on_buffer(bh);
	if (bh->b_uptodate)
		return bh;
	brelse(bh);
	return (NULL);
}

//缓冲区初始化函数，main中调用，传入参数为高速缓冲区内存末端地址
void buffer_init(long buffer_end)
{
	struct buffer_head * h = start_buffer;
	void * b;
	int i;

	//1、确定高速缓冲区的首、尾地址
	//如果高速缓冲区末端地址为1M,则高速缓冲区实际的末端为640*1024
	//640*1024 - 1024*1024为现成和bios rom
	if (buffer_end == 1<<20)
		b = (void *) (640*1024);
	else
		b = (void *) buffer_end;

	//2、初始化缓冲区，建立空闲缓冲块链表
	//从缓冲区高端开始划分BLOCK_SIZE大小的缓冲块，与此同时，在缓冲区低端建立描述该缓冲块的结构buffer_head
	while ( (b -= BLOCK_SIZE) >= ((void *) (h+1)) ) {
		h->b_dev = 0;//使用该缓冲块的设备号
		h->b_dirt = 0;//脏标志，即缓冲块的修改标志
		h->b_count = 0;//缓冲块引用计数
		h->b_lock = 0;//缓冲块锁定标志
		h->b_uptodate = 0;//缓冲块更新标志
		h->b_wait = NULL;//指向等待该缓冲块解锁进程
		h->b_next = NULL;//指向具有相同hash值的下一个缓冲头
		h->b_prev = NULL;//指向具有相同hash值的前一个缓冲头
		h->b_data = (char *) b;//指向对应的缓冲块数据，BLOCK_SIZE个字节
		h->b_prev_free = h-1;//指向链表前一项
		h->b_next_free = h+1;//指向链表后一项
		h++;//h指向下一个缓冲头位置
		NR_BUFFERS++;
		if (b == (void *) 0x100000)//若b递减到等于1MB位置，则跳过384KB
			b = (void *) 0xA0000;//让b指向640x1024处
	}
	
	h--;//h指向最后一个有效缓冲头
	free_list = start_buffer;//空闲链表头指针指向第一个缓冲头
	free_list->b_prev_free = h;//空闲链表头指针指向第一个缓冲头
	h->b_next_free = free_list;//形成还链

	//3、初始化hash表置表中所有指针NULL
	for (i=0;i<NR_HASH;i++)
		hash_table[i]=NULL;
}	
