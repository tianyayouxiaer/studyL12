/*
 *  linux/fs/bitmap.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* bitmap.c contains the code that handles the inode and block bitmaps */
#include <string.h>

#include <linux/sched.h>
#include <linux/kernel.h>

//��ָ����ַaddr����һ��1024�ֽ��ڴ�����
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

//��addr��ʼ��λͼ��Ѱ�ҵ�1����0��λ��������addr��λƫ��ֵ����
//addr�ǻ������������ַ��ɨ��Ѱ�ҷ�Χ��1024�ֽڣ�8192bit
#define find_first_zero(addr) ({ \
int __res; \
__asm__("cld\n" \//�巽��λ
	"1:\tlodsl\n\t" \
	"notl %%eax\n\t" \//eax��ÿλȡ��
	"bsfl %%eax,%%edx\n\t" \//��λ0ɨ��eax����1�ĵ�1��λ
	"je 2f\n\t" \//���û���ҵ�������ת��2f��
	"addl %%edx,%%ecx\n\t" \//
	"jmp 3f\n" \
	"2:\taddl $32,%%ecx\n\t" \
	"cmpl $8192,%%ecx\n\t" \
	"jl 1b\n" \
	"3:" \
	:"=c" (__res):"c" (0),"S" (addr):"ax","dx","si"); \
__res;})

//�ͷ��豸dev���������е��߼���block
int free_block(int dev, int block)
{
	struct super_block * sb;
	struct buffer_head * bh;
	
	if (!(sb = get_super(dev)))
		panic("trying to free block on nonexistent device");
	//������������ʼ�߼��ź��ļ�ϵͳ���߼���������Ϣ�жϲ���block����Ч��
	if (block < sb->s_firstdatazone || block >= sb->s_nzones)
		panic("trying to free block not in datazone");
	//��hash����Ѱ�Ҹÿ����ݣ����ҵ����ж�����Ч�ԣ��������޸ĺ͸��±�־���ͷ����ݿ�
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
	//��λblock���߼���λͼ�е�λ
	block -= sb->s_firstdatazone - 1 ;
	if (clear_bit(block&8191,sb->s_zmap[block/8192]->b_data)) {
		printk("block (%04x:%d) ",dev,block+sb->s_firstdatazone-1);
		printk("free_block: bit already cleared\n");
	}
	sb->s_zmap[block/8192]->b_dirt = 1;
	return 1;
}

//���豸����һ���߼��飨�̿飬���飩
int new_block(int dev)
{
	struct buffer_head * bh;
	struct super_block * sb;
	int i,j;

	//��ȡ�豸������
	if (!(sb = get_super(dev)))
		panic("trying to get new block from nonexistant device");

	//�ڳ������е��߼���λͼ��Ѱ�ҵ�һ��0ֵλ������һ�����е��߼��飩
	j = 8192;
	for (i=0 ; i<8 ; i++)
		if (bh=sb->s_zmap[i])
			if ((j=find_first_zero(bh->b_data))<8192)
				break;
	if (i>=8 || !bh || j>=8192)
		return 0;

	//��λ��Ӧ�߼���λͼ�е�λ
	if (set_bit(j,bh->b_data))
		panic("new_block: bit already set");
	bh->b_dirt = 1;
	//�߼�λͼ����ʾ�������������߼����ռ����������߼���λͼ��λƫ��
	//ֵ��ʾ����������ʼ������Ŀ�ţ����������Ҫ������������һ���߼���
	//�Ŀ�ţ���jת�����߼����
	j += i*8192 + sb->s_firstdatazone-1;
	if (j >= sb->s_nzones)
		return 0;
	//Ȼ���ڸ��ٻ�������Ϊ���豸��ָ�����߼���ȡһ�������
	if (!(bh=getblk(dev,j)))
		panic("new_block: cannot get block");
	//�ո�ȡ�õ��߼��������ô���һ��Ϊ1
	if (bh->b_count != 1)
		panic("new block: count is != 1");
	//�����߼�����0
	clear_block(bh->b_data);
	//���ñ�־
	bh->b_uptodate = 1;
	bh->b_dirt = 1;
	//�ͷŶ�Ӧ�����
	brelse(bh);
	//�����߼����
	return j;
}

//�ͷ�ָ����i�ڵ�
void free_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;

	//1���ж�i�ڵ�ĺ�����
	//�����ж�
	if (!inode)
		return;

	//���i�ڵ��ϵ��豸���ֶ�Ϊ0����˵���ýڵ�û��ʹ��
	//���i�ڵ���ռ�ڴ沢����
	if (!inode->i_dev) {
		memset(inode,0,sizeof(*inode));
		return;
	}
	//���i���㻹�������������ã������ͷ�
	if (inode->i_count>1) {
		printk("trying to free inode with count=%d\n",inode->i_count);
		panic("free_inode");
	}

	//����ļ���������λ0�����ʾ���������ļ�Ŀ¼����ʹ�øýڵ㣬��˲����ͷţ���Ӧ�÷Żصȴ�
	if (inode->i_nlinks)
		panic("trying to free inode with links");

	//2��
	//���ó�������Ϣ������i�ڵ�λͼ���в���
	if (!(sb = get_super(inode->i_dev)))
		panic("trying to free inode on nonexistent device");
	//�ж�i�ڵ㷶Χ��1< i_num < s_ninodes����0��i�ڵ㱣��û��ʹ�ã�s_ninodes:���豸i�ڵ�����
	if (inode->i_num < 1 || inode->i_num > sb->s_ninodes)
		panic("trying to free inode 0 or nonexistant inode");
	//i�ڵ��Ӧ��λͼ�����ڣ������
	//1��������i�ڵ�λͼ��8192���ֽ�
	if (!(bh=sb->s_imap[inode->i_num>>13]))
		panic("nonexistent imap in superblock");
	//��λi�ڵ��Ӧ�Ľڵ�λͼ�е�λ
	if (clear_bit(inode->i_num&8191,bh->b_data))
		printk("free_inode: bit already cleared.\n\r");
	bh->b_dirt = 1;
	//�����i�ڵ���ռ�õ��ڴ���
	memset(inode,0,sizeof(*inode));
}

//Ϊ�豸dev����һ���µ�i�ڵ㡣��ʼ�������ظ���i�ڵ��ָ��
//���ڴ�i�ڵ���л�ȡһ������i�ڵ�������i�ڵ�λͼ�е�һ������i�ڵ�
struct m_inode * new_inode(int dev)
{
	struct m_inode * inode;
	struct super_block * sb;
	struct buffer_head * bh;
	int i,j;

	//�������ڴ�i�ڵ���л�ȡһ������i�ڵ���
	if (!(inode=get_empty_inode()))
		return NULL;

	//��ȡָ���豸�ĳ�����ṹ
	if (!(sb = get_super(dev)))
		panic("new_inode with unknown device");

	//ɨ�賬������8��i�ڵ�λͼ��Ѱ�ҵ�1��0λ��Ѱ�ҿ��нڵ㣬��ȡ���ø�i�ڵ�Ľڵ�š�
	j = 8192;
	for (i=0 ; i<8 ; i++)
		if (bh=sb->s_imap[i])
			if ((j=find_first_zero(bh->b_data))<8192)
				break;
	//û�ҵ�������λͼ�������Ч����Ż��������i�ڵ���е�i�ڵ�
	if (!bh || j >= 8192 || j+i*8192 > sb->s_ninodes) {
		iput(inode);
		return NULL;
	}
	//�ҵ���δʹ�õ�i�ڵ��j��������λi�ڵ�j��Ӧ��i�ڵ�λͼ��Ӧbitλ
	if (set_bit(j,bh->b_data))
		panic("new_inode: bit already set");
	bh->b_dirt = 1;//���޸�
	inode->i_count=1;
	inode->i_nlinks=1;
	inode->i_dev=dev;//i�ڵ����ڵ��豸��
	inode->i_uid=current->euid;
	inode->i_gid=current->egid;
	inode->i_dirt=1;
	inode->i_num = j + i*8192;//��Ӧ�豸�е�i�ڵ��
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	return inode;
}
