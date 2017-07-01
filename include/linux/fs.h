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
#define NR_HASH 307 //������hash����������ֵ
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

//�����ͷ�ṹ�������г���bh��д��ʾ
//�����ݱ�д�뻺��飬����δд���豸ʱ��b_dirt = 1��b_uptodate = 0
//�����ݱ�д����豸��մӿ��豸���뻺����b_uptodate = 1
//������һ���豸�����ʱ��b_dirt��b_uptodate��Ϊ1����ʾ����������������豸��ͬ����������Ȼ��Ч�����µģ�
struct buffer_head {
	char * b_data;			    /* pointer to data block (1024 bytes) *///ָ��
	unsigned long b_blocknr;	/* block number *///���
	unsigned short b_dev;		/* device (0 = free) *///����Դ�豸��
	unsigned char b_uptodate; 	//���±�־������ʾ�����Ƿ��Ѹ��£���ʾ�������е������Ƿ���Ч
	unsigned char b_dirt;		/* 0-clean,1-dirty *///�޸ı�־��0-δ�޸ģ�1-���޸�,��������������ѱ��޸ĵ�δͬ�������豸����Ϊ1
	unsigned char b_count;		/* users using this block *///ʹ�õ��û���
	unsigned char b_lock;		/* 0 - ok, 1 -locked *///�������Ƿ�����
	//����������
	struct task_struct * b_wait;//ָ��ȴ��û�������������
	struct buffer_head * b_prev;//hash������ǰһ��
	struct buffer_head * b_next;//hash�����Ϻ�һ��
	struct buffer_head * b_prev_free;//���б���ǰһ��
	struct buffer_head * b_next_free;//���б��Ϻ�һ��
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

//�����������ڵ����ݽṹ
struct m_inode {
	unsigned short i_mode;//�ļ����ͺ����ԣ�rwxλ��
	unsigned short i_uid;//�û�id���ļ�ӵ���߱�ʶ����
	unsigned long i_size;//�ļ���С���ֽ�����
	unsigned long i_mtime;//�޸�ʱ��
	unsigned char i_gid;//��id���ļ�ӵ����������
	unsigned char i_nlinks;//�����������ٸ��ļ�Ŀ¼��ָ���i�ڵ㣩
	unsigned short i_zone[9];//ֱ��(0-6),��ӣ�7����˫�ؼ�ӣ�8���߼����
/* these are in memory also */
	struct task_struct * i_wait;//�ȴ��ýڵ�Ľ���
	struct task_struct * i_wait2;	/* for pipes */
	unsigned long i_atime;//�����ʵ�ʱ��
	unsigned long i_ctime;//i�ڵ������޸�ʱ��
	unsigned short i_dev;//i�ڵ������豸��
	unsigned short i_num;//i�ڵ��
	unsigned short i_count;//i�ڵ㱻ʹ�õĴ�����0��ʾ��i�ڵ����
	unsigned char i_lock;//������־
	unsigned char i_dirt;//���޸ģ��ࣩ��־
	unsigned char i_pipe;//�ܵ���־
	unsigned char i_mount;//��װ��־
	unsigned char i_seek;//��Ѱ��־
	unsigned char i_update;//���±�־
};

struct file {
	unsigned short f_mode;
	unsigned short f_flags;
	unsigned short f_count;
	struct m_inode * f_inode;
	off_t f_pos;
};

//�ڴ��д��̳�����ṹ
struct super_block {
	unsigned short s_ninodes;//�ڵ���
	unsigned short s_nzones;//�߼�����
	unsigned short s_imap_blocks;//i�ڵ�λͼ��ռ�õ����ݿ���
	unsigned short s_zmap_blocks;//�߼�λͼ��ռ�õ����ݿ���
	unsigned short s_firstdatazone;//��һ�������߼�����
	unsigned short s_log_zone_size;//log�����ݿ���/�߼��飩
	unsigned long s_max_size;//�ļ���󳤶�
	unsigned short s_magic;//�ļ�ϵͳħ��
/* These are only in memory */
	struct buffer_head * s_imap[8];//i�ڵ�λͼ�����ָ�����飨ռ��8�飬��ʾ64MB��
	struct buffer_head * s_zmap[8];//�߼���λͼ�����ָ�����飨ռ��8�飩
	unsigned short s_dev;//���������ڵ��豸��
	struct m_inode * s_isup;//����װ���ļ�ϵͳ��Ŀ¼��i�ڵ�
	struct m_inode * s_imount;//����װ����i�ڵ�
	unsigned long s_time;//�޸�ʱ��
	struct task_struct * s_wait;//�ȴ��ó�����Ľ���
	unsigned char s_lock;//��������־
	unsigned char s_rd_only;//ֻ����־
	unsigned char s_dirt;//���޸ı�־
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
