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

//�豸���ݿ�����ָ�����飬ÿ��ָ����ָ��ָ�����豸�ŵ��ܿ�������hd_size[]��
//�ܿ�������ÿһ���Ӧ���豸��ȷ����һ�����豸����ӵ�е����ݿ�������1���С=1KB��
extern int *blk_size[];

//�ڴ���i�ڵ��
struct m_inode inode_table[NR_INODE]={{0,},};

//��ָ��i�ڵ�ŵ�i�ڵ���Ϣ
static void read_inode(struct m_inode * inode);
//дi�ڵ���Ϣ�����ٻ�����
static void write_inode(struct m_inode * inode);

//�ȴ�ָ����i�ڵ����
//���i�ڵ��ѱ��������򽫵�ǰ������Ϊ�����жϵĵȴ�״̬������ӵ���i�ڵ�ĵȴ�����i_wait�У�
//һֱ����i�ڵ��������ȷ�Ļ��ѱ�����
static inline void wait_on_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	sti();
}
//��i�ڵ�����������ָ����i�ڵ㣩
static inline void lock_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	inode->i_lock=1;
	sti();
}

//��i�ڵ����
static inline void unlock_inode(struct m_inode * inode)
{
	inode->i_lock=0;
	wake_up(&inode->i_wait);
}

//�ͷ��豸dev���ڴ�i�ڵ���е�����i�ڵ�
void invalidate_inodes(int dev)
{
	int i;
	struct m_inode * inode;

	//ɨ��i�ڵ����飬�ͷŸ��豸�����е�i�ڵ�
	inode = 0+inode_table;
	for(i=0 ; i<NR_INODE ; i++,inode++) {
		wait_on_inode(inode);

		if (inode->i_dev == dev) {
			if (inode->i_count)
				printk("inode in use on removed disk\n\r");
			inode->i_dev = inode->i_dirt = 0;//�ͷ�i�ڵ㣨���豸��Ϊ0��
		}
	}
}

//ͬ�����е�i�ڵ�
//���ڴ�i�ڵ��������i�ڵ����豸��i�ڵ���ͬ������
void sync_inodes(void)
{
	int i;
	struct m_inode * inode;

	//ɨ���ڴ��е�i�ڵ����飬�����ѱ��޸Ĳ��Ҳ��ǹܵ���i�ڵ㣬д����ٻ�������
	//����������������ʱ��д������
	inode = 0+inode_table;
	for(i=0 ; i<NR_INODE ; i++,inode++) {
		wait_on_inode(inode);
		if (inode->i_dirt && !inode->i_pipe)
			write_inode(inode);
	}
}

//�ļ����ݿ�ӳ�䵽�̿�Ĵ��������blockλͼ��������
//inode - �ļ���i�ڵ�ָ��
//block - �ļ��е����ݿ��
//create - �������־
static int _bmap(struct m_inode * inode,int block,int create)
{
	struct buffer_head * bh;
	int i;

	//�ж��ļ����ݿ�ŵ���Ч��
	//���ļ�ϵͳ��ʾ��Χ��0 < block < ֱ�ӿ��� + ��ӿ��� + ���μ�ӿ���
	if (block<0)
		panic("_bmap: block<0");
	if (block >= 7+512+512*512)
		panic("_bmap: block>big");

	//block<7������ֱ�ӿ��ʾ
	if (block<7) {
		//���������־��λ �� i�ڵ��ж�Ӧ�ÿ���߼����ֶ�Ϊ0��������Ӧ�豸�������̿�
		if (create && !inode->i_zone[block])
			//���豸�����̿飬���������߼���ţ��̿�ţ������߼��ֶ���
			if (inode->i_zone[block]=new_block(inode->i_dev)) {
				inode->i_ctime=CURRENT_TIME;
				inode->i_dirt=1;//�������޸ı�־
			}
		return inode->i_zone[block];
	}

	//���7<=block<512����ʹ�õ���һ�μ�ӿ�
	block -= 7;
	if (block<512) {
		//���� �� i_zone[7] = 0,�����״�ʹ�ü�ӿ�
		if (create && !inode->i_zone[7])
			//��Ҫ����һ�����̿����ڴ�ż����Ϣ
			if (inode->i_zone[7]=new_block(inode->i_dev)) {
				inode->i_dirt=1;
				inode->i_ctime=CURRENT_TIME;
			}
		//�����ӿ�ʧ�ܣ���i_zone[7]Ϊ0
		if (!inode->i_zone[7])
			return 0;
		//��ȡ�豸�ϸ�i�ڵ��һ�μ�ӿ�
		if (!(bh = bread(inode->i_dev,inode->i_zone[7])))
			return 0;
			
		//ȡ�ü�ӿ��ϵ�block���е��߼���ţ��̿�ţ�i��ÿһ��ռ2���ֽ�
		i = ((unsigned short *) (bh->b_data))[block];
		//������� �� ��ӿ��block���е��߼����Ϊ0��������һ���̿�
		if (create && !i)
			if (i=new_block(inode->i_dev)) {
				//�ü�ӿ��еĵ�block����ڸ����߼����
				((unsigned short *) (bh->b_data))[block]=i;
				//��λ�޸ı�־
				bh->b_dirt=1;
			}
		//������Ǵ�������i������ҪѰ�ҵ��߼����
		brelse(bh);//�ͷŸü�ӿ�ռ�õĻ����
		return i;
	}

	//���ˣ�˵���Ƕ��μ�ӿ�
	//block��ȥ��ӿ������ɵĿ���
	block -= 512;
	//���ݴ�����־����
	if (create && !inode->i_zone[8])
		if (inode->i_zone[8]=new_block(inode->i_dev)) {
			inode->i_dirt=1;
			inode->i_ctime=CURRENT_TIME;
		}
	if (!inode->i_zone[8])
		return 0;
	//��ȡ���豸�ϸ�i�ڵ�Ķ��μ�ӿ�
	if (!(bh=bread(inode->i_dev,inode->i_zone[8])))
		return 0;
	//ȡ�ö��μ�ӿ��һ�����ϵ�block/512���е��߼����i
	i = ((unsigned short *)bh->b_data)[block>>9];
	//����������߼����Ϊ0������Ҫ�ڴ���������һ���̿���Ϊ���μ�ӿ�Ķ�����i
	if (create && !i)
		if (i=new_block(inode->i_dev)) {
			((unsigned short *) (bh->b_data))[block>>9]=i;
			bh->b_dirt=1;
		}
		
	brelse(bh);

	//������μ�ӿ�Ķ������Ϊ0����ʾ������̿�ʧ�ܻ�ԭ����Ӧ��ž�Ϊ0
	if (!i)
		return 0;
	//���豸�϶������μ�ӿ�Ķ����飬��ȡ�ö������ϵ�block��Ŀ�е��߼����
	if (!(bh=bread(inode->i_dev,i)))
		return 0;
	i = ((unsigned short *)bh->b_data)[block&511];
	//����������Ҷ�����ĵ�block�����߼����Ϊ0��������һ���̿飬��Ϊ���մ��������Ϣ��
	if (create && !i)
		if (i=new_block(inode->i_dev)) {
			((unsigned short *) (bh->b_data))[block&511]=i;
			bh->b_dirt=1;
		}
	brelse(bh);
	return i;
}

//ȡ�ļ����ݿ�block���豸�϶�Ӧ���߼����
//�����Ӧ���߼��鲻���ھʹ���һ�飬�������豸�϶�Ӧ���߼����
//block-�ļ��е����ݿ��
int bmap(struct m_inode * inode,int block)
{
	return _bmap(inode,block,0);
}

//ȡ�ļ����ݿ�block���豸�϶�Ӧ���߼����
//�����Ӧ���߼��鲻���ھʹ���һ�飬�������豸�϶�Ӧ���߼����
//block-�ļ��е����ݿ��
int create_block(struct m_inode * inode, int block)
{
	return _bmap(inode,block,1);
}

//�Ż�һ��i�ڵ㣨��д���豸��
void iput(struct m_inode * inode)
{
	//�����ж�
	if (!inode)
		return;

	//�ȴ���i�ڵ����
	wait_on_inode(inode);

	//������ü���Ϊ0����ʾ��i�ڵ��Ѿ��ǿ��е�
	if (!inode->i_count)
		panic("iput: trying to free free inode");

	//����ǹܵ�i�ڵ㣬���ѵȴ��ùܵ��Ľ��̣����ü�����1
	if (inode->i_pipe) {
		wake_up(&inode->i_wait);
		wake_up(&inode->i_wait2);
		//���ô�����1��������������򷵻�
		if (--inode->i_count)
			return;
		//�ͷŹܵ�ռ�õ��ڴ�ҳ�棬����λ�ýڵ����ü��������޸ı�־�͹ܵ���־
		//���ڹܵ��ڵ㣬inode->i_size������ڴ�ҳ��ַ
		free_page(inode->i_size);
		inode->i_count=0;
		inode->i_dirt=0;
		inode->i_pipe=0;
		return;
	}

	//���i�ڵ��Ӧ�豸��Ϊ0�������ü�����1������
	//�磺�ܵ�������i�ڵ㣬��i�ڵ���豸��Ϊ0
	if (!inode->i_dev) {
		inode->i_count--;
		return;
	}
	
	//����ǿ��豸�ļ���i�ڵ㣬��ʱ�߼����ֶ�0��i_zone[0]�������豸�ţ���ˢ�¸��豸
	if (S_ISBLK(inode->i_mode)) {
		sync_dev(inode->i_zone[0]);
		wait_on_inode(inode);
	}
	
repeat:
	//���i�ڵ����ü�������1��������ݼ���ֱ�ӷ��أ���Ϊi�ڵ㻹�������ã������ͷ�
	if (inode->i_count>1) {
		inode->i_count--;
		return;
	}

	//����i�ڵ�û��������
	//���i�ڵ��������Ϊ0����˵��i�ڵ��Ӧ�ļ���ɾ����
	if (!inode->i_nlinks) {
		truncate(inode);//�ͷŸ�i�ڵ������߼���
		free_inode(inode);//�ͷŸ�i�ڵ�
		return;
	}

	//���i�ڵ��������޸ģ����д���¸�i�ڵ㣬���ȴ���i�ڵ����
	if (inode->i_dirt) {
		write_inode(inode);	/* we can sleep - so do again */
		wait_on_inode(inode);//��Ϊ˯���ˣ�������Ҫ�ظ��ж�
		goto repeat;
	}
	//���ˣ�˵����i�ڵ����ü���Ϊ1����������λ0����������û�б��޸�
	//i_count=0����ʾ���ͷ�
	inode->i_count--;
	return;
}

//��i�ڵ��inode_table���л�ȡһ�����е�i�ڵ���
//Ѱ�����ü���countΪ0��i�ڵ㣬������ϣ���̺���0������ָ�룻
//���ü�����1
struct m_inode * get_empty_inode(void)
{
	//1��Ѱ��һ�����е�i�ڵ�
	struct m_inode * inode;
	//ָ��i�ڵ�����1��
	static struct m_inode * last_inode = inode_table;
	int i;
	
	do {
		inode = NULL;
		//����i�ڵ�����
		for (i = NR_INODE; i ; i--) {
			//���last_inode�Ѿ�ָ��i�ڵ������1�������������ָ��i�ڵ㿪ʼ��
			if (++last_inode >= inode_table + NR_INODE)
				last_inode = inode_table;

			//����ҵ�������
			if (!last_inode->i_count) {
				inode = last_inode;
				//�ýڵ�i_dirt��i_lock��־��δ���ϣ���ֱ���˳�
				if (!inode->i_dirt && !inode->i_lock)
					break;
			}
		}
		
		//�˳������ѭ�������ַ�ʽ��break�˳���i=0�˳�
		//���û���ҵ�����i�ڵ㣬��i�ڵ���ӡ��������
		if (!inode) {
			for (i=0 ; i<NR_INODE ; i++)
				printk("%04x: %6d\t",inode_table[i].i_dev,
					inode_table[i].i_num);
			panic("No free inodes in mem");
		}
		
		//�ȴ�i�ڵ��������������ϣ�
		wait_on_inode(inode);
		//�����i�ڵ����޸ı�־����λ���򽫸�i�ڵ�ˢ�¡�
		while (inode->i_dirt) {
			write_inode(inode);
			//��Ϊˢ���ǿ��ܻ�˯�ߣ��������ѭ���ȴ�i�ڵ����
			wait_on_inode(inode);
		}
	} while (inode->i_count);
	
	//2����ʼ���ҵ��Ŀ��е�i�ڵ�
	//���ҵ���i�ڵ���գ����ü�����1
	memset(inode,0,sizeof(*inode));
	inode->i_count = 1;
	return inode;
}

//��ȡ�ܵ��ڵ�
struct m_inode * get_pipe_inode(void)
{
	struct m_inode * inode;

	//���ڴ�i�ڵ���ȡһ������i�ڵ�
	if (!(inode = get_empty_inode()))
		return NULL;
	//Ϊ��i�ڵ�����һҳ�ڴ棬����i_size�ֶ�ָ���ҳ��
	if (!(inode->i_size=get_free_page())) {
		inode->i_count = 0;
		return NULL;
	}
	//��д�����ܼ�
	inode->i_count = 2;	/* sum of readers/writers */
	PIPE_HEAD(*inode) = PIPE_TAIL(*inode) = 0;//��λ�ܵ�ͷβָ��
	inode->i_pipe = 1;//�ýڵ�ܵ�ʹ�ñ�־
	return inode;
}

//���豸dev�϶�ȡָ���ڵ��nr��i�ڵ�
//nr - i�ڵ��
struct m_inode * iget(int dev,int nr)
{
	struct m_inode * inode, * empty;

	//�������
	if (!dev)
		panic("iget with dev==0");

	//��i�ڵ����ȡһ������i�ڵ㱸��
	empty = get_empty_inode();

	//ɨ��i�ڵ��
	inode = inode_table;
	while (inode < NR_INODE+inode_table) {
		//Ѱ�Ҳ���ָ���ڵ��Ϊnr��i�ڵ�
		if (inode->i_dev != dev || inode->i_num != nr) {
			//��������㣬ɨ����һ��
			inode++;
			continue;
		}

		//�ҵ��豸��dev�ͽڵ��nr��i�ڵ㣬��ȴ���i�ڵ��������������ˣ�
		wait_on_inode(inode);

		//�ڵȴ�i�ڵ�����Ĺ����У�i�ڵ���ܷ����仯
		//�ٴν����жϣ�����仯����i�ڵ���ͷ��ʼɨ��
		if (inode->i_dev != dev || inode->i_num != nr) {
			inode = inode_table;
			continue;
		}

		//���ˣ��ҵ���i�ڵ�
		//i�ڵ����ü�����1
		inode->i_count++;

		//�ٽ�һ����⣬�����Ƿ�Ϊ����һ��ϵͳ�İ�װ��
		if (inode->i_mount) {
			int i;

			//���i�ڵ��������ļ�ϵͳ��װ�㣬���ڳ����������Ѱ��װ��i�ڵ�ĳ�����
			for (i = 0 ; i<NR_SUPER ; i++)
				if (super_block[i].s_imount==inode)
					break;

			//���û���ҵ��������
			if (i >= NR_SUPER) {
				printk("Mounted inode hasn't got sb\n");
				//�Żغ�����ʼ��ȥ�Ŀ��нڵ�empty�����ظ�i�ڵ�ָ��
				if (empty)
					iput(empty);
				return inode;
			}

			//���ˣ���ʾ�ҵ���װ��inode�ڵ���ļ�ϵͳ������
			//��i�ڵ�д�̷Ż�
			iput(inode);
			//�Ӱ�װ�ڴ�i�ڵ��ϵ��ļ�ϵͳ��������ȡ�豸��,����i�ڵ��Ϊ1
			//Ȼ�����ɨ������i�ڵ���Ի�ȡ�ñ���װ�ļ�ϵͳ�ĸ�i�ڵ���Ϣ
			dev = super_block[i].s_dev;
			nr = ROOT_INO;
			inode = inode_table;
			continue;
		}

		//���ˣ������ҵ�����Ӧ��i�ڵ㣬��ˣ����Է�������Ŀռ�i�ڵ㣬�����ҵ���i�ڵ�ָ��
		if (empty)
			iput(empty);
		return inode;
	}

	//���������i�ڵ����ô���ҵ�ָ����i�ڵ㣬����������Ŀ���i�ڵ�empty��
	//i�ڵ���н�����i�ڵ�
	if (!empty)
		return (NULL);
	inode=empty;
	inode->i_dev = dev;
	inode->i_num = nr;

	//���豸�϶�ȡi�ڵ���Ϣ������i�ڵ�ָ��
	read_inode(inode);
	return inode;
}

//��ȡָ����i�ڵ���Ϣ
static void read_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	//������i�ڵ�
	lock_inode(inode);

	//ȡ��i�ڵ������豸�ĳ�����
	if (!(sb=get_super(inode->i_dev)))
		panic("trying to read inode without dev");
	//��ȡ��i�ڵ������豸���豸�߼���Ż򻺳��
	block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
		(inode->i_num-1)/INODES_PER_BLOCK;

	//ȷ���߼���ź󣬰Ѹ��߼������һ�������
	if (!(bh=bread(inode->i_dev,block)))
		panic("unable to read i-node block");

	//����ָ��i�ڵ����ݵ�inodeָ����ֵλ�ô�������
	*(struct d_inode *)inode =
		((struct d_inode *)bh->b_data)
			[(inode->i_num-1)%INODES_PER_BLOCK];
			
	//����ͷŶ���Ļ����
	brelse(bh);

	//����ǿ��豸������i�ڵ���ļ���󳤶�ֵ
	if (S_ISBLK(inode->i_mode)) {
		int i = inode->i_zone[0];//���ڿ��豸��i_zone[0]�����豸��
		if (blk_size[MAJOR(i)])
			inode->i_size = 1024*blk_size[MAJOR(i)][MINOR(i)];
		else
			inode->i_size = 0x7fffffff;
	}
	unlock_inode(inode);
}

//��i�ڵ���Ϣд�뻺������
//�ú�����ָ����i�ڵ�д�뻺������Ӧ�Ļ�����У���������ˢ��ʱ��д������

static void write_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	//����i�ڵ�
	lock_inode(inode);
	if (!inode->i_dirt || !inode->i_dev) {
		//���i�ڵ�û�б��޸Ĺ�����i�ڵ���豸�ŵ���0���������i�ڵ㲢�˳�
		//��Ϊ����û���޸Ĺ���i�ڵ㣬�������뻺�����л��豸�е���ͬ
		unlock_inode(inode);
		return;
	}

	//Ϊ��ȷ�������豸����飬�����Ȼ�ȡ��Ӧ�豸�ĳ����飬�Ի�ȡ���ڼ����߼���ŵ�ÿ��
	//i�ڵ���ϢINODES_PER_BLOCK
	if (!(sb=get_super(inode->i_dev)))
		panic("trying to write inode without device");

	//��i�ڵ����ڶ��豸�߼���� = ��������+������       ��+ i�ڵ�λͼ��ռ�õĿ��� +
	//�߼���λͼռ�õĿ��� + ��i�ڵ�� - 1��/ÿ�麬�е�i�ڵ���
	block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
		(inode->i_num-1)/INODES_PER_BLOCK;

	//���豸�϶�ȡ��i�ڵ����ڵ��߼��飬������i�ڵ���Ϣ���Ƶ��߼����Ӧ��i�ڵ����λ��
	if (!(bh=bread(inode->i_dev,block)))
		panic("unable to read i-node block");
	((struct d_inode *)bh->b_data)
		[(inode->i_num-1)%INODES_PER_BLOCK] =
			*(struct d_inode *)inode;
	//�û��������޸ı�־��
	bh->b_dirt=1;
	//i�ڵ������Ѿ��뻺�����е�һ�£�����޸ı�־��0
	inode->i_dirt=0;
	//�ͷŸú���i�ڵ�Ļ����
	brelse(bh);
	//������i�ڵ�
	unlock_inode(inode);
}
