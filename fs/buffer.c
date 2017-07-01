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
 
#include <linux/config.h>//��Σ�����vsprintf��vprintf��vfprintf����
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/io.h>
//���ӳ���ld���ɣ����ڱ����ں˴����ĩ�ˡ���ָ���ں�ĩ��λ��
//����������ٻ�������ʼ���ں˴���ĩ��λ��
extern int end;

struct buffer_head * start_buffer = (struct buffer_head *) &end;//start_bufferָ����ٻ�������ʼλ��
struct buffer_head * hash_table[NR_HASH];
static struct buffer_head * free_list;
//�ȴ����л�����˯�ߵ��������
//����b_wait�������������뻺����ϵͳȱ�����ÿ��л����ʱ����ǰ������ӵ�buffer_wait˯�ߵȴ�������
//b_waitר�Ź�ָ������������ʹ��
static struct task_struct * buffer_wait = NULL;
int NR_BUFFERS = 0;

//�ȴ�ָ�뻺������
//��������bh�Ѿ����������ý��̲����жϵ�˯�ڸû����ĵȴ�����b_wait�ϡ�
//�ڻ�������ʱ����ȴ������ϵ����н��̶���������
//��Ȼ�ر��жϺ�˯�ߣ�������Ӱ������������������Ӧ�жϣ�ӦΪÿ�����̶����Լ���tss��
//�б����˱�־�Ĵ���eflagsֵ���ڽ����л�ʱ��cpu�е�ǰeflagsֵҳ��֮�ı�
//sleep_on����˯��״̬��Ҫwake_up��ȷ�Ļ���
static inline void wait_on_buffer(struct buffer_head * bh)
{
	cli();//���ж�
	while (bh->b_lock)//����ѱ�����������̽�˯�ߣ��ȴ������
		sleep_on(&bh->b_wait);
	sti();//���ж�
}

//�豸����ͬ��
int sys_sync(void)
{
	int i;
	struct buffer_head * bh;

	//i�ڵ�ͬ�����������ڴ�i�ڵ���������޸Ĺ���i�ڵ�д����ٻ����С�
	sync_inodes();		/* write out inodes into buffers */
	//ɨ�����и��ٻ���u�����ѱ��޸ĵĻ�������д�������󣬽������е�����д�����У��������ٻ��������ݺ�
	//�豸������ͬ��
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		wait_on_buffer(bh);
		if (bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
	return 0;
}

//��ָ�����豸���и��ٻ����������豸�����ݵ�ͬ��
int sync_dev(int dev)
{
	int i;
	struct buffer_head * bh;

	//1����ָ���豸ִ������ͬ��
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)//���Ǹ��豸������
			continue;
		wait_on_buffer(bh);//��Ҫͬ���ĸ��ٻ���飬����Ѿ�����������ȴ�����
		if (bh->b_dev == dev && bh->b_dirt)//���ж�һ���豸���޸ı�־�����ǣ���ִ��д�̲���
			ll_rw_block(WRITE,bh);
	}
	
	//2���ٽ�i�ڵ�����д����ٻ��塣��i�ڵ��inode_table�е�inode�뻺���е���Ϣͬ��
	sync_inodes();

	//3����ִ��һ������ͬ��
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

//ʹָ���豸�ڸ��ٻ�������������Ч
void inline invalidate_buffers(int dev)
{
	int i;
	struct buffer_head * bh;
	//ɨ����ٻ��������л���飬��ָ����Ļ���鸴λ����Ч��־�����޸ı�־
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
 //�������Ƿ����������Ѹ�����ʹ��Ӧ���ٻ�������Ч
void check_disk_change(int dev)
{
	int i;

	//���ȼ���Ƿ�ʱ�����豸
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

//hash�����ٲ��ұȽ�Ԫ��������ʱ��,�ڴ洢λ�ú͹ؼ���֮�佨��һһ��Ӧ��ϵ��ͨ���������̲鵽ָ����Ԫ��
//hash�������壺���豸��^�߼���ţ�% 307
#define _hashfn(dev,block) (((unsigned)(dev^block))%NR_HASH)
//hash�������
#define hash(dev,block) hash_table[_hashfn(dev,block)]

//��hash���кͿ��л�����������߻����
//hash���кͿ��л���������˫������ṹ
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
	//�����������ͷָ�򱾻�������������ָ����һ��������
	if (free_list == bh)
		free_list = bh->b_next_free;
}


//�����������������β����ͬʱ����hash������
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
	bh->b_next->b_prev = bh;//��ǰӦ����if(bh->b_next)�ж�
}

//����hash���ڸ��ٻ�����Ѱ�Ҹ����豸��ָ����ŵĻ�������
//�ҵ����ػ����ָ�룬����NULL
static struct buffer_head * find_buffer(int dev, int block)
{		
	struct buffer_head * tmp;

	//hash�����������ͬɢ��ֵ�Ļ����������ɢ������ͬһ��������
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
 //����hash���ڸ��ٻ�������Ѱ��ָ���Ļ���顣���ҵ����򶪸û�������big���ؿ�ָ��
struct buffer_head * get_hash_table(int dev, int block)
{
	struct buffer_head * bh;

	for (;;) {
		if (!(bh=find_buffer(dev,block)))
			return NULL;
		bh->b_count++;//�Ըû�����������ü���
		wait_on_buffer(bh);//�ȴ��û������������ѱ�������

		//���ھ�����˯��״̬������б�Ҫ����֤�û�������ȷ�Բ����ػ���ͷָ��
		if (bh->b_dev == dev && bh->b_blocknr == block)
			return bh;
			
		bh->b_count--;//���˯��ʱ�û�����������豸�Ż��ŷ����˱仯������������Ӧ�ü���������Ѱ��
	}
}

/*
 * Ok, this is getblk, and it isn't very clear, again to hinder
 * race-conditions. Most of the code is seldom used, (ie repeating),
 * so it should be much more efficient than it looks.
 *
 * The algoritm is changed: hopefully better, and an elusive bug removed.
 */
//�жϻ��������޸ı�־��������־�����Ҷ����޸ı�־��Ȩ��Ҫ��������־��
//�����ݱ�д�뻺��飬����δд���豸ʱ��b_dirt = 1��b_uptodate = 0
//�����ݱ�д����豸��մӿ��豸���뻺����b_uptodate = 1
//������һ���豸�����ʱ��b_dirt��b_uptodate��Ϊ1����ʾ����������������豸��ͬ����������Ȼ��Ч�����µģ�
//����ʹ�øû����ʱ�������û����
#define BADNESS(bh) (((bh)->b_dirt<<1)+(bh)->b_lock)

//ȡ���ٻ�������ָ���Ļ����
struct buffer_head * getblk(int dev,int block)
{
	struct buffer_head * tmp, * bh;

repeat:
	//1������hash�����ָ�����Ѿ��ڸ��ٻ������У��򷵻ض�Ӧ������ͷָ�룬�˳�
	if (bh = get_hash_table(dev,block))
		return bh;

	//2��ɨ��������ݿ�����Ѱ�ҿ��л�����
	tmp = free_list;//tmpָ���������ĵ�һ�����л�����ͷ
	do {
		if (tmp->b_count)//����û��������ڱ�ʹ�ã���ɨ����һ��
			continue;

		//case1:bh == NULL
		//case2:bh != NULL ���� BADNESS(tmp)<BADNESS(bh)��
		//�п���b_countΪ0������b_dirt��b_lock����Ϊ0

		//�ҵ�������Ȩ����С�ģ�����㷨��ྫ��
		if (!bh || BADNESS(tmp)<BADNESS(bh)) {
			bh = tmp;//bhָ��tmp����ͷ
			if (!BADNESS(tmp))//�����tmp����ͷ��û���޸�Ҳû����������˵����Ϊָ���豸�ϵĿ�ȡ�ö�Ӧ�ĸ��ٻ����
				break;//�˳����������Ǽ���ѭ�����ҵ�BADNESS()��С�Ļ����
		}
/* and repeat until we find something good */
	} while ((tmp = tmp->b_next_free) != free_list);

	//3��������л����b_coun>0����˯�ߵȴ��п��л�������
	//���п��л�������ʱ�����̻ᱻ��ȷ�Ļ���
	//���Ѻ����²��ҿ��л����
	if (!bh) {
		sleep_on(&buffer_wait);
		goto repeat;
	}
	//�����Ѿ��ҵ����л���飬�ȵȴ��û��������������������
	wait_on_buffer(bh);
	if (bh->b_count)//���˯�߽׶θû���������������ʹ�õĻ���ֻ����ȥѰ��һ�������
		goto repeat;

	//����û������ѱ��޸ģ�������д�̣����ٴεȴ�������������
	while (bh->b_dirt) {
		sync_dev(bh->b_dev);
		wait_on_buffer(bh);
		if (bh->b_count)//���˯�߽׶θû���������������ʹ�õĻ���ֻ����ȥѰ��һ�������
		if (bh->b_count)
			goto repeat;
	}
/* NOTE!! While we slept waiting for this block, somebody else might */
/* already have added "this" block to the cache. check it */
//�ڸ��ٻ���hash���м��ָ���豸�Ϳ�Ļ�����Ƿ��˯��֮���Ѿ��������ȥ������ǣ����ٴ��ظ�����Ѱ�ҹ���
	if (find_buffer(dev,block))
		goto repeat;
/* OK, FINALLY we know that this buffer is the only one of it's kind, */
/* and that it's unused (b_count=0), unlocked (b_lock=0), and clean */
	//�����ҵ�û��ռ�õĻ���飨unused (b_count=0), unlocked (b_lock=0), and clean��
	bh->b_count=1;
	bh->b_dirt=0;//��λ�޸ı�־
	bh->b_uptodate=0;//��λ���±�־
	//��hash���кͿ��п�����������Ƴ��û�����ͷ���øû���������ִ���豸�����ϵ�ָ����
	remove_from_queues(bh);
	//Ȼ����ݴ��µ��豸�ſɿ�����²����������hash������λ�ô�
	bh->b_dev=dev;
	bh->b_blocknr=block;
	insert_into_queues(bh);
	return bh;
}

//�ͷ�ָ���Ļ����
//�ȴ��û���������Ȼ�����ü�����1������ȷ���ѵȴ����л����Ľ���
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
 //��ָ���豸��ȡָ�������ݿ鲢���غ������ݵĻ����������ָ���Ŀ鲻�����򷵻�NULL
struct buffer_head * bread(int dev,int block)
{
	struct buffer_head * bh;

	//1������dev��block�ڸ��ٻ���������һ�黺���
	if (!(bh=getblk(dev,block)))
		panic("bread: getblk returned NULL\n");
	//��������������ʱ��Ч�ģ��Ѹ��£�����ֱ��ʹ�ã��򷵻�
	if (bh->b_uptodate)
		return bh;
	//������豸�ײ��д�������������豸������
	ll_rw_block(READ,bh);
	//�ȴ�ָ�����ݿ鱻���룬���ȴ�����������
	wait_on_buffer(bh);
	//����������������ѱ����£��򷵻ػ�����ͷָ��
	if (bh->b_uptodate)
		return bh;
	//���򣬱��������豸ʧ�ܣ��ͷŸû�����������null
	brelse(bh);
	return NULL;
}

//�����ڴ�飬��from��ַ����һ�飨1024�ֽڣ����ݵ�toλ��
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
 //һ�ζ��ĸ���������ݵ��ڴ�ָ����ַ�������Ŀ飬��������ٶ�
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
 //��ָ���豸��ȡָ����һЩ��
 //���������ɱ䣬ʱһϵ��ָ���Ŀ�š��ɹ�ʱ���ص�һ��Ļ����ͷָ�룬���򷵻�NULL
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

//��������ʼ��������main�е��ã��������Ϊ���ٻ������ڴ�ĩ�˵�ַ
void buffer_init(long buffer_end)
{
	struct buffer_head * h = start_buffer;
	void * b;
	int i;

	//1��ȷ�����ٻ��������ס�β��ַ
	//������ٻ�����ĩ�˵�ַΪ1M,����ٻ�����ʵ�ʵ�ĩ��Ϊ640*1024
	//640*1024 - 1024*1024Ϊ�ֳɺ�bios rom
	if (buffer_end == 1<<20)
		b = (void *) (640*1024);
	else
		b = (void *) buffer_end;

	//2����ʼ�����������������л��������
	//�ӻ������߶˿�ʼ����BLOCK_SIZE��С�Ļ���飬���ͬʱ���ڻ������Ͷ˽��������û����Ľṹbuffer_head
	while ( (b -= BLOCK_SIZE) >= ((void *) (h+1)) ) {
		h->b_dev = 0;//ʹ�øû������豸��
		h->b_dirt = 0;//���־�����������޸ı�־
		h->b_count = 0;//��������ü���
		h->b_lock = 0;//�����������־
		h->b_uptodate = 0;//�������±�־
		h->b_wait = NULL;//ָ��ȴ��û�����������
		h->b_next = NULL;//ָ�������ͬhashֵ����һ������ͷ
		h->b_prev = NULL;//ָ�������ͬhashֵ��ǰһ������ͷ
		h->b_data = (char *) b;//ָ���Ӧ�Ļ�������ݣ�BLOCK_SIZE���ֽ�
		h->b_prev_free = h-1;//ָ������ǰһ��
		h->b_next_free = h+1;//ָ�������һ��
		h++;//hָ����һ������ͷλ��
		NR_BUFFERS++;
		if (b == (void *) 0x100000)//��b�ݼ�������1MBλ�ã�������384KB
			b = (void *) 0xA0000;//��bָ��640x1024��
	}
	
	h--;//hָ�����һ����Ч����ͷ
	free_list = start_buffer;//��������ͷָ��ָ���һ������ͷ
	free_list->b_prev_free = h;//��������ͷָ��ָ���һ������ͷ
	h->b_next_free = free_list;//�γɻ���

	//3����ʼ��hash���ñ�������ָ��NULL
	for (i=0;i<NR_HASH;i++)
		hash_table[i]=NULL;
}	
