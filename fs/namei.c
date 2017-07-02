/*
 *  linux/fs/namei.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * Some corrections by tytso.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <const.h>
#include <sys/stat.h>

//���ļ������Ҷ�Ӧi�ڵ���ڲ�����
static struct m_inode * _namei(const char * filename, struct m_inode * base,
	int follow_links);

//O_ACCMOD - 00003
//8����
//\004 - �ַ�r��ascii���ʾ
//\002 - �ַ�w��ascii���ʾ
//\006 - �ַ�rw��ascii���ʾ
//\377 - �ַ�wxrwxrwx��ascii���ʾ

#define ACC_MODE(x) ("\004\002\006\377"[(x)&O_ACCMODE])

/*
 * comment out this line if you want names > NAME_LEN chars to be
 * truncated. Else they will be disallowed.
 */
/* #define NO_TRUNCATE */

#define MAY_EXEC 1//��ִ��
#define MAY_WRITE 2//��д
#define MAY_READ 4//�ɶ�

/*
 *	permission()
 *
 * is used to check for read/write/execute permissions on a file.
 * I don't know if we should look at just the euid or both euid and
 * uid, but that should be easily changed.
 */
 //����ļ��ɷ���Ȩ��
static int permission(struct m_inode * inode,int mask)
{
	int mode = inode->i_mode;

/* special case: not even root can read/write a deleted file */
	if (inode->i_dev && !inode->i_nlinks)
		return 0;
	else if (current->euid==inode->i_uid)
		mode >>= 6;
	else if (in_group_p(inode->i_gid))
		mode >>= 3;
	if (((mode & mask & 0007) == mask) || suser())
		return 1;
	return 0;
}

/*
 * ok, we cannot use strncmp, as the name is not in our data space.
 * Thus we'll have to use match. No big problem. Match also makes
 * some sanity tests.
 *
 * NOTE! unlike strncmp, match returns 1 for success, 0 for failure.
 */
 //ָ�������ַ����Ƚ�
static int match(int len,const char * name,struct dir_entry * de)
{
	register int same __asm__("ax");

	if (!de || !de->inode || len > NAME_LEN)
		return 0;
	/* "" means "." ---> so paths like "/usr/lib//libc.a" work */
	if (!len && (de->name[0]=='.') && (de->name[1]=='\0'))
		return 1;
	if (len < NAME_LEN && de->name[len])
		return 0;
	__asm__("cld\n\t"
		"fs ; repe ; cmpsb\n\t"
		"setz %%al"
		:"=a" (same)
		:"0" (0),"S" ((long) name),"D" ((long) de->name),"c" (len)
		:"cx","di","si");
	return same;
}

/*
 *	find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 *
 * This also takes care of the few special cases due to '..'-traversal
 * over a pseudo-root and a mount point.
 */
 //����ָ��Ŀ¼���ļ�����Ŀ¼��
 //dir-ָ��Ŀ¼i�ڵ�ָ��
 //name-�ļ���
 //namelen-�ļ�������
//��ָ��Ŀ¼���ļ�������ָ���ļ�����Ŀ¼�����ָ���ļ�����'..'�����
//�������⴦��
//�ɹ������ظ��ٻ�����ָ�룬����* res_dir�����ص�Ŀ¼��ṹָ�룬ʧ�ܷ���null
static struct buffer_head * find_entry(struct m_inode ** dir,
	const char * name, int namelen, struct dir_entry ** res_dir)
{
	int entries;
	int block,i;
	struct buffer_head * bh;
	struct dir_entry * de;
	struct super_block * sb;

#ifdef NO_TRUNCATE
	if (namelen > NAME_LEN)
		return NULL;
#else
	//�ļ����ض�
	if (namelen > NAME_LEN)
		namelen = NAME_LEN;
#endif

	//���㱾Ŀ¼�У�Ŀ¼����
	//i_size������Ŀ¼���������ݳ��ȣ�����Ŀ¼��ṹ���ȣ��õ�����
	entries = (*dir)->i_size / (sizeof (struct dir_entry));
	
	//�ÿշ��ص�Ŀ¼��ṹָ��
	*res_dir = NULL;

	//��ָ���ļ�����'..'������������⴦��
/* check for '..', as we might have to do some "magic" for it */
	//�ж��Ƿ���..�����
	if (namelen==2 && get_fs_byte(name)=='.' && get_fs_byte(name+1)=='.') {
/* '..' in a pseudo-root results in a faked '.' (just change namelen) */
		//α���е�'..'�����һ����'.'��ֻ��Ҫ�ı����ֳ���
		if ((*dir) == current->root)
			namelen=1;//������ֻ�ܷ��ʸ�Ŀ¼�е�������ܺ��˵��丸Ŀ¼��ȥ�����ڱ����̱�Ŀ¼����ͬ�ļ�ϵͳ��Ŀ¼
		else if ((*dir)->i_num == ROOT_INO) {//��Ŀ¼��i�ڵ�ŵ���1��˵��ȷʵ���ļ��ĸ�i�ڵ�
/* '..' over a mount-point results in 'dir' being exchanged for the mounted
   directory-inode. NOTE! We set mounted, so that we can iput the new dir */
   			//ȡ������
			sb=get_super((*dir)->i_dev);
			if (sb->s_imount) {//�������װ��i�ڵ���ڣ����ȷŻ�i�ڵ㣬Ȼ��Ա���װ����i�ڵ���д���
				iput(*dir);//������*dirָ��ñ���װ����i�ڵ㣬�����ø�i�ڵ����������1
				(*dir)=sb->s_imount;
				(*dir)->i_count++;
			}
		}
	}
	
	if (!(block = (*dir)->i_zone[0]))
		return NULL;
	if (!(bh = bread((*dir)->i_dev,block)))
		return NULL;
	i = 0;
	de = (struct dir_entry *) bh->b_data;
	while (i < entries) {
		if ((char *)de >= BLOCK_SIZE+bh->b_data) {
			brelse(bh);
			bh = NULL;
			if (!(block = bmap(*dir,i/DIR_ENTRIES_PER_BLOCK)) ||
			    !(bh = bread((*dir)->i_dev,block))) {
				i += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			de = (struct dir_entry *) bh->b_data;
		}
		if (match(namelen,name,de)) {
			*res_dir = de;
			return bh;
		}
		de++;
		i++;
	}
	brelse(bh);
	return NULL;
}

/*
 *	add_entry()
 *
 * adds a file entry to the specified directory, using the same
 * semantics as find_entry(). It returns NULL if it failed.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
 //����ָ����Ŀ¼���ļ������Ŀ¼��
static struct buffer_head * add_entry(struct m_inode * dir,
	const char * name, int namelen, struct dir_entry ** res_dir)
{
	int block,i;
	struct buffer_head * bh;
	struct dir_entry * de;

	*res_dir = NULL;
#ifdef NO_TRUNCATE
	if (namelen > NAME_LEN)
		return NULL;
#else
	if (namelen > NAME_LEN)
		namelen = NAME_LEN;
#endif
	if (!namelen)
		return NULL;
	if (!(block = dir->i_zone[0]))
		return NULL;
	if (!(bh = bread(dir->i_dev,block)))
		return NULL;
	i = 0;
	de = (struct dir_entry *) bh->b_data;
	while (1) {
		if ((char *)de >= BLOCK_SIZE+bh->b_data) {
			brelse(bh);
			bh = NULL;
			block = create_block(dir,i/DIR_ENTRIES_PER_BLOCK);
			if (!block)
				return NULL;
			if (!(bh = bread(dir->i_dev,block))) {
				i += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			de = (struct dir_entry *) bh->b_data;
		}
		if (i*sizeof(struct dir_entry) >= dir->i_size) {
			de->inode=0;
			dir->i_size = (i+1)*sizeof(struct dir_entry);
			dir->i_dirt = 1;
			dir->i_ctime = CURRENT_TIME;
		}
		if (!de->inode) {
			dir->i_mtime = CURRENT_TIME;
			for (i=0; i < NAME_LEN ; i++)
				de->name[i]=(i<namelen)?get_fs_byte(name+i):0;
			bh->b_dirt = 1;
			*res_dir = de;
			return bh;
		}
		de++;
		i++;
	}
	brelse(bh);
	return NULL;
}

//���ҷ������ӵ�i�ڵ�
static struct m_inode * follow_link(struct m_inode * dir, struct m_inode * inode)
{
	unsigned short fs;
	struct buffer_head * bh;

	if (!dir) {
		dir = current->root;
		dir->i_count++;
	}
	if (!inode) {
		iput(dir);
		return NULL;
	}
	if (!S_ISLNK(inode->i_mode)) {
		iput(dir);
		return inode;
	}
	__asm__("mov %%fs,%0":"=r" (fs));
	if (fs != 0x17 || !inode->i_zone[0] ||
	   !(bh = bread(inode->i_dev, inode->i_zone[0]))) {
		iput(dir);
		iput(inode);
		return NULL;
	}
	iput(inode);
	__asm__("mov %0,%%fs"::"r" ((unsigned short) 0x10));
	inode = _namei(bh->b_data,dir,0);
	__asm__("mov %0,%%fs"::"r" (fs));
	brelse(bh);
	return inode;
}

/*
 *	get_dir()
 *
 * Getdir traverses the pathname until it hits the topmost directory.
 * It returns NULL on failure.
 */
 //��ָ��Ŀ¼��ʼ��Ѱָ��·������Ŀ¼
 //inode-ָ����ʵĿ¼��i�ڵ�
static struct m_inode * get_dir(const char * pathname, struct m_inode * inode)
{
	char c;
	const char * thisname;
	struct buffer_head * bh;
	int namelen,inr;
	struct dir_entry * de;
	struct m_inode * dir;

	//������飬��inodeΪnull����ʹ�õ�ǰ���̵ĵ�ǰ����Ŀ¼i�ڵ�
	if (!inode) {
		inode = current->pwd;
		inode->i_count++;
	}
	
	//����û�·�����ĵ�1���ַ���'/'����˵���Ǿ���·����
	//����Ҫ�Ż�inodeָ�������趨��Ŀ¼i�ڵ㣬��ȡ�ý���ʹ�õĸ�i�ڵ�
	//�����Ϳ��Ա�֤��ǰ����ֻ�������趨�ĸ�i�ڵ���Ϊ���������
	if ((c=get_fs_byte(pathname))=='/') {
		iput(inode);//�Ż�ԭi�ڵ�
		inode = current->root;//Ϊ����ָ���ĸ�i�ڵ�
		//i�ڵ�����ü�����1����ɾ��·�����ĵ�1���ַ�'/'
		pathname++;
		inode->i_count++;
	}

	//���·�����еĸ���Ŀ¼���ֺ��ļ�������ѭ������
	while (1) {
		//�Ե�ǰ���ڴ����Ŀ¼�����ŵ�i�ڵ������Ч�жϣ�����thisnameָ��ǰ���ڴ����Ŀ¼������
		thisname = pathname;
		//����i�ڵ������ǰĿ¼�����ֲ���Ŀ¼���ͣ�����û�пɽ���ĸ�Ŀ¼�ķ���Ȩ�ޣ���Żظýڵ�
		//����null���˳�
		//�ս���ѭ����ʱ�򣬵�ǰĿ¼i�ڵ�inode���ǽ��̸�i�ڵ�����ǵ�ǰ����Ŀ¼��i�ڵ㣬�����ǲ���
		//ָ����ĳ��������ʼĿ¼��i�ڵ�
		if (!S_ISDIR(inode->i_mode) || !permission(inode,MAY_EXEC)) {
			iput(inode);
			return NULL;
		}

		//ÿ��ѭ��������·�����з��봦һ��Ŀ¼�������ļ���
		//c���ֿ��ܣ���β����NULL��, '/'
		for(namelen=0;(c=get_fs_byte(pathname++))&&(c!='/');namelen++)
			/* nothing */ ;

		//�ﵽ���ָ��Ŀ¼�����ļ������򷵻ظ�i�ڵ�ָ�벢�˳�
		if (!c)
			return inode;

		//�ڵõ���ǰĿ¼�������ļ����󣬵��ò���Ŀ¼���find_entry
		//�ڵ�ǰ�����Ŀ¼��Ѱ��ָ�����Ƶ�Ŀ¼�
		if (!(bh = find_entry(&inode,thisname,namelen,&de))) {
			iput(inode);
			return NULL;
		}
		
		inr = de->inode;
		brelse(bh);
		dir = inode;
		if (!(inode = iget(dir->i_dev,inr))) {
			iput(dir);
			return NULL;
		}
		if (!(inode = follow_link(dir,inode)))
			return NULL;
	}
}

/*
 *	dir_namei()
 *
 * dir_namei() returns the inode of the directory of the
 * specified name, and the name within that directory.
 */
 //��ȡָ��Ŀ¼����i�ڵ�ָ�룬�Լ�������Ŀ¼����
static struct m_inode * dir_namei(const char * pathname,
	int * namelen, const char ** name, struct m_inode * base)
{
	char c;
	const char * basename;
	struct m_inode * dir;

	//
	if (!(dir = get_dir(pathname,base)))
		return NULL;
	basename = pathname;
	while (c=get_fs_byte(pathname++))
		if (c=='/')
			basename=pathname;
	*namelen = pathname-basename-1;
	*name = basename;
	return dir;
}

//ȡָ��·��û�Ǹ���i�ڵ�
//path-·����
//base-�������Ŀ¼i�ڵ�
//follow_links-�Ƿ����������ӣ�1��Ҫ��0����Ҫ
struct m_inode * _namei(const char * pathname, struct m_inode * base,
	int follow_links)
{
	const char * basename;
	int inr,namelen;
	struct m_inode * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	//
	if (!(base = dir_namei(pathname,&namelen,&basename,base)))
		return NULL;
	if (!namelen)			/* special case: '/usr/' etc */
		return base;
	bh = find_entry(&base,basename,namelen,&de);
	if (!bh) {
		iput(base);
		return NULL;
	}
	inr = de->inode;
	brelse(bh);
	if (!(inode = iget(base->i_dev,inr))) {
		iput(base);
		return NULL;
	}
	if (follow_links)
		inode = follow_link(base,inode);
	else
		iput(base);
	inode->i_atime=CURRENT_TIME;
	inode->i_dirt=1;
	return inode;
}

//ȡָ��·�����Ƶ�i�ڵ㣬�������������
struct m_inode * lnamei(const char * pathname)
{
	return _namei(pathname, NULL, 0);
}

/*
 *	namei()
 *
 * is used by most simple commands to get the inode of a specified name.
 * Open, link etc use their own routines, but this is enough for things
 * like 'chmod' etc.
 */
//ȡָ��·�����Ƶ�i�ڵ㣬�����������
struct m_inode * namei(const char * pathname)
{
	return _namei(pathname,NULL,1);
}

/*
 *	open_namei()
 *
 * namei for open - this is in fact almost the whole open-routine.
 */
 //�ļ���namei����
int open_namei(const char * pathname, int flag, int mode,
	struct m_inode ** res_inode)
{
	const char * basename;
	int inr,dev,namelen;
	struct m_inode * dir, *inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	if ((flag & O_TRUNC) && !(flag & O_ACCMODE))
		flag |= O_WRONLY;
	mode &= 0777 & ~current->umask;
	mode |= I_REGULAR;
	if (!(dir = dir_namei(pathname,&namelen,&basename,NULL)))
		return -ENOENT;
	if (!namelen) {			/* special case: '/usr/' etc */
		if (!(flag & (O_ACCMODE|O_CREAT|O_TRUNC))) {
			*res_inode=dir;
			return 0;
		}
		iput(dir);
		return -EISDIR;
	}
	bh = find_entry(&dir,basename,namelen,&de);
	if (!bh) {
		if (!(flag & O_CREAT)) {
			iput(dir);
			return -ENOENT;
		}
		if (!permission(dir,MAY_WRITE)) {
			iput(dir);
			return -EACCES;
		}
		inode = new_inode(dir->i_dev);
		if (!inode) {
			iput(dir);
			return -ENOSPC;
		}
		inode->i_uid = current->euid;
		inode->i_mode = mode;
		inode->i_dirt = 1;
		bh = add_entry(dir,basename,namelen,&de);
		if (!bh) {
			inode->i_nlinks--;
			iput(inode);
			iput(dir);
			return -ENOSPC;
		}
		de->inode = inode->i_num;
		bh->b_dirt = 1;
		brelse(bh);
		iput(dir);
		*res_inode = inode;
		return 0;
	}
	inr = de->inode;
	dev = dir->i_dev;
	brelse(bh);
	if (flag & O_EXCL) {
		iput(dir);
		return -EEXIST;
	}
	if (!(inode = follow_link(dir,iget(dev,inr))))
		return -EACCES;
	if ((S_ISDIR(inode->i_mode) && (flag & O_ACCMODE)) ||
	    !permission(inode,ACC_MODE(flag))) {
		iput(inode);
		return -EPERM;
	}
	inode->i_atime = CURRENT_TIME;
	if (flag & O_TRUNC)
		truncate(inode);
	*res_inode = inode;
	return 0;
}

//����һ���豸�����ļ�����ͨ�ļ��ڵ�
int sys_mknod(const char * filename, int mode, int dev)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;
	
	if (!suser())
		return -EPERM;
	if (!(dir = dir_namei(filename,&namelen,&basename, NULL)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(&dir,basename,namelen,&de);
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	inode = new_inode(dir->i_dev);
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_mode = mode;
	if (S_ISBLK(mode) || S_ISCHR(mode))
		inode->i_zone[0] = dev;
	inode->i_mtime = inode->i_atime = CURRENT_TIME;
	inode->i_dirt = 1;
	bh = add_entry(dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		inode->i_nlinks=0;
		iput(inode);
		return -ENOSPC;
	}
	de->inode = inode->i_num;
	bh->b_dirt = 1;
	iput(dir);
	iput(inode);
	brelse(bh);
	return 0;
}

//����һ��Ŀ¼
int sys_mkdir(const char * pathname, int mode)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh, *dir_block;
	struct dir_entry * de;

	if (!(dir = dir_namei(pathname,&namelen,&basename, NULL)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(&dir,basename,namelen,&de);
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	inode = new_inode(dir->i_dev);
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_size = 32;
	inode->i_dirt = 1;
	inode->i_mtime = inode->i_atime = CURRENT_TIME;
	if (!(inode->i_zone[0]=new_block(inode->i_dev))) {
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ENOSPC;
	}
	inode->i_dirt = 1;
	if (!(dir_block=bread(inode->i_dev,inode->i_zone[0]))) {
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ERROR;
	}
	de = (struct dir_entry *) dir_block->b_data;
	de->inode=inode->i_num;
	strcpy(de->name,".");
	de++;
	de->inode = dir->i_num;
	strcpy(de->name,"..");
	inode->i_nlinks = 2;
	dir_block->b_dirt = 1;
	brelse(dir_block);
	inode->i_mode = I_DIRECTORY | (mode & 0777 & ~current->umask);
	inode->i_dirt = 1;
	bh = add_entry(dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		inode->i_nlinks=0;
		iput(inode);
		return -ENOSPC;
	}
	de->inode = inode->i_num;
	bh->b_dirt = 1;
	dir->i_nlinks++;
	dir->i_dirt = 1;
	iput(dir);
	iput(inode);
	brelse(bh);
	return 0;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
 //���ָ����Ŀ¼�Ƿ�Ϊ��
static int empty_dir(struct m_inode * inode)
{
	int nr,block;
	int len;
	struct buffer_head * bh;
	struct dir_entry * de;

	len = inode->i_size / sizeof (struct dir_entry);
	if (len<2 || !inode->i_zone[0] ||
	    !(bh=bread(inode->i_dev,inode->i_zone[0]))) {
	    	printk("warning - bad directory on dev %04x\n",inode->i_dev);
		return 0;
	}
	de = (struct dir_entry *) bh->b_data;
	if (de[0].inode != inode->i_num || !de[1].inode || 
	    strcmp(".",de[0].name) || strcmp("..",de[1].name)) {
	    	printk("warning - bad directory on dev %04x\n",inode->i_dev);
		return 0;
	}
	nr = 2;
	de += 2;
	while (nr<len) {
		if ((void *) de >= (void *) (bh->b_data+BLOCK_SIZE)) {
			brelse(bh);
			block=bmap(inode,nr/DIR_ENTRIES_PER_BLOCK);
			if (!block) {
				nr += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			if (!(bh=bread(inode->i_dev,block)))
				return 0;
			de = (struct dir_entry *) bh->b_data;
		}
		if (de->inode) {
			brelse(bh);
			return 0;
		}
		de++;
		nr++;
	}
	brelse(bh);
	return 1;
}

//ɾ��Ŀ¼
int sys_rmdir(const char * name)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	if (!(dir = dir_namei(name,&namelen,&basename, NULL)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(&dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	if (!(inode = iget(dir->i_dev, de->inode))) {
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if ((dir->i_mode & S_ISVTX) && current->euid &&
	    inode->i_uid != current->euid) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	if (inode->i_dev != dir->i_dev || inode->i_count>1) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	if (inode == dir) {	/* we may not delete ".", but "../dir" is ok */
		iput(inode);
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -ENOTDIR;
	}
	if (!empty_dir(inode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -ENOTEMPTY;
	}
	if (inode->i_nlinks != 2)
		printk("empty directory has nlink!=2 (%d)",inode->i_nlinks);
	de->inode = 0;
	bh->b_dirt = 1;
	brelse(bh);
	inode->i_nlinks=0;
	inode->i_dirt=1;
	dir->i_nlinks--;
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_dirt=1;
	iput(dir);
	iput(inode);
	return 0;
}

//ɾ���ļ�����Ӧ��Ŀ¼��
int sys_unlink(const char * name)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	if (!(dir = dir_namei(name,&namelen,&basename, NULL)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(&dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	if (!(inode = iget(dir->i_dev, de->inode))) {
		iput(dir);
		brelse(bh);
		return -ENOENT;
	}
	if ((dir->i_mode & S_ISVTX) && !suser() &&
	    current->euid != inode->i_uid &&
	    current->euid != dir->i_uid) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	if (S_ISDIR(inode->i_mode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if (!inode->i_nlinks) {
		printk("Deleting nonexistent file (%04x:%d), %d\n",
			inode->i_dev,inode->i_num,inode->i_nlinks);
		inode->i_nlinks=1;
	}
	de->inode = 0;
	bh->b_dirt = 1;
	brelse(bh);
	inode->i_nlinks--;
	inode->i_dirt = 1;
	inode->i_ctime = CURRENT_TIME;
	iput(inode);
	iput(dir);
	return 0;
}

//������������
int sys_symlink(const char * oldname, const char * newname)
{
	struct dir_entry * de;
	struct m_inode * dir, * inode;
	struct buffer_head * bh, * name_block;
	const char * basename;
	int namelen, i;
	char c;

	dir = dir_namei(newname,&namelen,&basename, NULL);
	if (!dir)
		return -EACCES;
	if (!namelen) {
		iput(dir);
		return -EPERM;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EACCES;
	}
	if (!(inode = new_inode(dir->i_dev))) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_mode = S_IFLNK | (0777 & ~current->umask);
	inode->i_dirt = 1;
	if (!(inode->i_zone[0]=new_block(inode->i_dev))) {
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ENOSPC;
	}
	inode->i_dirt = 1;
	if (!(name_block=bread(inode->i_dev,inode->i_zone[0]))) {
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ERROR;
	}
	i = 0;
	while (i < 1023 && (c=get_fs_byte(oldname++)))
		name_block->b_data[i++] = c;
	name_block->b_data[i] = 0;
	name_block->b_dirt = 1;
	brelse(name_block);
	inode->i_size = i;
	inode->i_dirt = 1;
	bh = find_entry(&dir,basename,namelen,&de);
	if (bh) {
		inode->i_nlinks--;
		iput(inode);
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	bh = add_entry(dir,basename,namelen,&de);
	if (!bh) {
		inode->i_nlinks--;
		iput(inode);
		iput(dir);
		return -ENOSPC;
	}
	de->inode = inode->i_num;
	bh->b_dirt = 1;
	brelse(bh);
	iput(dir);
	iput(inode);
	return 0;
}

//Ϊ�ļ�����һ���ļ���Ŀ¼��
int sys_link(const char * oldname, const char * newname)
{
	struct dir_entry * de;
	struct m_inode * oldinode, * dir;
	struct buffer_head * bh;
	const char * basename;
	int namelen;

	oldinode=namei(oldname);
	if (!oldinode)
		return -ENOENT;
	if (S_ISDIR(oldinode->i_mode)) {
		iput(oldinode);
		return -EPERM;
	}
	dir = dir_namei(newname,&namelen,&basename, NULL);
	if (!dir) {
		iput(oldinode);
		return -EACCES;
	}
	if (!namelen) {
		iput(oldinode);
		iput(dir);
		return -EPERM;
	}
	if (dir->i_dev != oldinode->i_dev) {
		iput(dir);
		iput(oldinode);
		return -EXDEV;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		iput(oldinode);
		return -EACCES;
	}
	bh = find_entry(&dir,basename,namelen,&de);
	if (bh) {
		brelse(bh);
		iput(dir);
		iput(oldinode);
		return -EEXIST;
	}
	bh = add_entry(dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		iput(oldinode);
		return -ENOSPC;
	}
	de->inode = oldinode->i_num;
	bh->b_dirt = 1;
	brelse(bh);
	iput(dir);
	oldinode->i_nlinks++;
	oldinode->i_ctime = CURRENT_TIME;
	oldinode->i_dirt = 1;
	iput(oldinode);
	return 0;
}
