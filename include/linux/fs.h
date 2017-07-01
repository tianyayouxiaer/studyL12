/*
 * This file has definitions for some important file table
 * structures etc.
 */

#ifndef _FS_H
#define _FS_H

#include <sys/types.h>

/* devices are as follows: (same as minix, so we can use the minix
 * file system. These are major numbers.)
 *
 * 0 - unused (nodev)
 * 1 - /dev/mem
 * 2 - /dev/fd
 * 3 - /dev/hd
 * 4 - /dev/ttyx
 * 5 - /dev/tty
 * 6 - /dev/lp
 * 7 - unnamed pipes
 */

#define IS_SEEKABLE(x) ((x)>=1 && (x)<=3)

#define READ 0
#define WRITE 1
#define READA 2		/* read-ahead - don't pause */
#define WRITEA 3	/* "write-ahead" - silly, but somewhat useful */

void buffer_init(long buffer_end);

#define MAJOR(a) (((unsigned)(a))>>8)
#define MINOR(a) ((a)&0xff)

#define NAME_LEN 14
#define ROOT_INO 1

#define I_MAP_SLOTS 8
#define Z_MAP_SLOTS 8
#define SUPER_MAGIC 0x137F

#define NR_OPEN 20
#define NR_INODE 64
#define NR_FILE 64
#define NR_SUPER 8
#define NR_HASH 307 //缓冲区hash表数组项数值
#define NR_BUFFERS nr_buffers
#define BLOCK_SIZE 1024
#define BLOCK_SIZE_BITS 10
#ifndef NULL
#define NULL ((void *) 0)
#endif

#define INODES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct d_inode)))
#define DIR_ENTRIES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct dir_entry)))

#define PIPE_READ_WAIT(inode) ((inode).i_wait)
#define PIPE_WRITE_WAIT(inode) ((inode).i_wait2)
#define PIPE_HEAD(inode) ((inode).i_zone[0])
#define PIPE_TAIL(inode) ((inode).i_zone[1])
#define PIPE_SIZE(inode) ((PIPE_HEAD(inode)-PIPE_TAIL(inode))&(PAGE_SIZE-1))
#define PIPE_EMPTY(inode) (PIPE_HEAD(inode)==PIPE_TAIL(inode))
#define PIPE_FULL(inode) (PIPE_SIZE(inode)==(PAGE_SIZE-1))

#define NIL_FILP	((struct file *)0)
#define SEL_IN		1
#define SEL_OUT		2
#define SEL_EX		4

typedef char buffer_block[BLOCK_SIZE];

//缓冲块头结构，程序中常用bh缩写表示
//当数据被写入缓冲块，但还未写入设备时，b_dirt = 1，b_uptodate = 0
//当数据被写入块设备或刚从块设备读入缓冲区b_uptodate = 1
//新申请一个设备缓冲块时，b_dirt和b_uptodate都为1，表示缓冲区数据虽与块设备不同，但数据任然有效（更新的）
struct buffer_head {
	char * b_data;			    /* pointer to data block (1024 bytes) *///指针
	unsigned long b_blocknr;	/* block number *///快号
	unsigned short b_dev;		/* device (0 = free) *///数据源设备号
	unsigned char b_uptodate; 	//更新标志，：表示数据是否已更新，表示缓冲区中的数据是否有效
	unsigned char b_dirt;		/* 0-clean,1-dirty *///修改标志：0-未修改，1-已修改,若缓冲块中数据已被修改但未同步到块设备，则为1
	unsigned char b_count;		/* users using this block *///使用的用户数
	unsigned char b_lock;		/* 0 - ok, 1 -locked *///缓冲区是否被锁定
	//缓冲区管理
	struct task_struct * b_wait;//指向等待该缓冲区解锁任务
	struct buffer_head * b_prev;//hash队列上前一块
	struct buffer_head * b_next;//hash队列上后一块
	struct buffer_head * b_prev_free;//空闲表上前一块
	struct buffer_head * b_next_free;//空闲表上后一块
};

struct d_inode {
	unsigned short i_mode;
	unsigned short i_uid;
	unsigned long i_size;
	unsigned long i_time;
	unsigned char i_gid;
	unsigned char i_nlinks;
	unsigned short i_zone[9];
};

//磁盘上索引节点数据结构
struct m_inode {
	unsigned short i_mode;//文件类型和属性（rwx位）
	unsigned short i_uid;//用户id（文件拥有者标识符）
	unsigned long i_size;//文件大小（字节数）
	unsigned long i_mtime;//修改时间
	unsigned char i_gid;//组id，文件拥有者所在组
	unsigned char i_nlinks;//链接数（多少个文件目录项指向该i节点）
	unsigned short i_zone[9];//直接(0-6),间接（7），双重间接（8）逻辑块号
/* these are in memory also */
	struct task_struct * i_wait;//等待该节点的进程
	struct task_struct * i_wait2;	/* for pipes */
	unsigned long i_atime;//最后访问的时间
	unsigned long i_ctime;//i节点自身修改时间
	unsigned short i_dev;//i节点所在设备号
	unsigned short i_num;//i节点号
	unsigned short i_count;//i节点被使用的次数，0表示该i节点空闲
	unsigned char i_lock;//锁定标志
	unsigned char i_dirt;//已修改（脏）标志
	unsigned char i_pipe;//管道标志
	unsigned char i_mount;//安装标志
	unsigned char i_seek;//搜寻标志
	unsigned char i_update;//更新标志
};

struct file {
	unsigned short f_mode;
	unsigned short f_flags;
	unsigned short f_count;
	struct m_inode * f_inode;
	off_t f_pos;
};

//内存中磁盘超级块结构
struct super_block {
	unsigned short s_ninodes;//节点数
	unsigned short s_nzones;//逻辑块数
	unsigned short s_imap_blocks;//i节点位图所占用的数据块数
	unsigned short s_zmap_blocks;//逻辑位图所占用的数据块数
	unsigned short s_firstdatazone;//第一个数据逻辑块数
	unsigned short s_log_zone_size;//log（数据块数/逻辑块）
	unsigned long s_max_size;//文件最大长度
	unsigned short s_magic;//文件系统魔数
/* These are only in memory */
	struct buffer_head * s_imap[8];//i节点位图缓冲块指针数组（占用8块，表示64MB）
	struct buffer_head * s_zmap[8];//逻辑块位图缓冲块指针数组（占用8块）
	unsigned short s_dev;//超级块所在的设备号
	struct m_inode * s_isup;//被安装的文件系统根目录的i节点
	struct m_inode * s_imount;//被安装到的i节点
	unsigned long s_time;//修改时间
	struct task_struct * s_wait;//等待该超级块的进程
	unsigned char s_lock;//被锁定标志
	unsigned char s_rd_only;//只读标志
	unsigned char s_dirt;//已修改标志
};

struct d_super_block {
	unsigned short s_ninodes;
	unsigned short s_nzones;
	unsigned short s_imap_blocks;
	unsigned short s_zmap_blocks;
	unsigned short s_firstdatazone;
	unsigned short s_log_zone_size;
	unsigned long s_max_size;
	unsigned short s_magic;
};

struct dir_entry {
	unsigned short inode;
	char name[NAME_LEN];
};

extern struct m_inode inode_table[NR_INODE];
extern struct file file_table[NR_FILE];
extern struct super_block super_block[NR_SUPER];
extern struct buffer_head * start_buffer;
extern int nr_buffers;

extern void check_disk_change(int dev);
extern int floppy_change(unsigned int nr);
extern int ticks_to_floppy_on(unsigned int dev);
extern void floppy_on(unsigned int dev);
extern void floppy_off(unsigned int dev);
extern void truncate(struct m_inode * inode);
extern void sync_inodes(void);
extern void wait_on(struct m_inode * inode);
extern int bmap(struct m_inode * inode,int block);
extern int create_block(struct m_inode * inode,int block);
extern struct m_inode * namei(const char * pathname);
extern struct m_inode * lnamei(const char * pathname);
extern int open_namei(const char * pathname, int flag, int mode,
	struct m_inode ** res_inode);
extern void iput(struct m_inode * inode);
extern struct m_inode * iget(int dev,int nr);
extern struct m_inode * get_empty_inode(void);
extern struct m_inode * get_pipe_inode(void);
extern struct buffer_head * get_hash_table(int dev, int block);
extern struct buffer_head * getblk(int dev, int block);
extern void ll_rw_block(int rw, struct buffer_head * bh);
extern void ll_rw_page(int rw, int dev, int nr, char * buffer);
extern void brelse(struct buffer_head * buf);
extern struct buffer_head * bread(int dev,int block);
extern void bread_page(unsigned long addr,int dev,int b[4]);
extern struct buffer_head * breada(int dev,int block,...);
extern int new_block(int dev);
extern int free_block(int dev, int block);
extern struct m_inode * new_inode(int dev);
extern void free_inode(struct m_inode * inode);
extern int sync_dev(int dev);
extern struct super_block * get_super(int dev);
extern int ROOT_DEV;

extern void mount_root(void);

#endif
