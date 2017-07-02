/*
 *  linux/fs/truncate.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>

#include <sys/stat.h>

//释放所有一次间接块
//dev - 文件系统所在设备的设备号
//block - 逻辑块号
static int free_ind(int dev,int block)
{
	struct buffer_head * bh;
	unsigned short * p;
	int i;
	int block_busy;//逻辑块没有被释放的标志

	//逻辑块0，则返回
	if (!block)
		return 1;

	//读取一次间接块，并释放放其上表明使用的所有逻辑块，然后释放该一次间接块的逻辑块
	block_busy = 0;
	if (bh=bread(dev,block)) {
		p = (unsigned short *) bh->b_data;//指向缓冲块数据区
		for (i=0;i<512;i++,p++)//每个逻辑块上可有512个块号
			if (*p)
				if (free_block(dev,*p)) {//释放指定的设备逻辑块
					*p = 0;
					bh->b_dirt = 1;
				} else
					block_busy = 1;
		brelse(bh); //释放间接块占用的缓冲块
	}
	//最后释放设备上的一次间接块，但如果其中有逻辑块没有被释放，则烦恼会0，失败
	if (block_busy)
		return 0;
	else
		return free_block(dev,block);
}

//释放所有二次间接块
//dev - 文件系统所在设备的设备号
//block - 逻辑块号
static int free_dind(int dev,int block)
{
	struct buffer_head * bh;
	unsigned short * p;
	int i;
	int block_busy;

	if (!block)
		return 1;

	//读取二次间接块的一级块，并释放其上表明使用的所有逻辑块，然后释放该一级块的缓冲块
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

//截断文件数据长度
//将节点对应的文件长度截为0，并释放占用的设备空间
void truncate(struct m_inode * inode)
{
	int i;
	int block_busy;

	//如果不是常规文件，目录文件或链接项，则返回
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	     S_ISLNK(inode->i_mode)))
		return;
repeat:
	//释放节点的7个直接逻辑块，并将这7个逻辑块全置0
	block_busy = 0;
	for (i=0;i<7;i++)
		if (inode->i_zone[i]) {
			//如果有逻辑块忙而没有被释放，则置块忙标志
			if (free_block(inode->i_dev,inode->i_zone[i]))
				inode->i_zone[i]=0;
			else
				block_busy = 1;
		}
	//释放所有的一次间接块
	if (free_ind(inode->i_dev,inode->i_zone[7]))
		inode->i_zone[7] = 0;
	else
		block_busy = 1;
	//释放所有的二次间接块
	if (free_dind(inode->i_dev,inode->i_zone[8]))
		inode->i_zone[8] = 0;
	else
		block_busy = 1;

	//设置i节点已修改标志，并且如果还有逻辑块由于忙么没有释放，则把
	//当前进程时间片置0，换出进程，稍等一会，再重新执行释放操作
	inode->i_dirt = 1;
	if (block_busy) {
		current->counter = 0;
		schedule();
		goto repeat;
	}
	inode->i_size = 0;//文件大小置0
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;//重新置文件修改时间
}

