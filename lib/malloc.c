/*
 * malloc.c --- a general purpose kernel memory allocator for Linux.
 * 
 * Written by Theodore Ts'o (tytso@mit.edu), 11/29/91
 *
 * This routine is written to be as fast as possible, so that it
 * can be called from the interrupt level.
 *
 * Limitations: maximum size of memory we can allocate using this routine
 *	is 4k, the size of a page in Linux.
 *
 * The general game plan is that each page (called a bucket) will only hold
 * objects of a given size.  When all of the object on a page are released,
 * the page can be returned to the general free pool.  When malloc() is
 * called, it looks for the smallest bucket size which will fulfill its
 * request, and allocate a piece of memory from that bucket pool.
 *
 * Each bucket has as its control block a bucket descriptor which keeps 
 * track of how many objects are in use on that page, and the free list
 * for that page.  Like the buckets themselves, bucket descriptors are
 * stored on pages requested from get_free_page().  However, unlike buckets,
 * pages devoted to bucket descriptor pages are never released back to the
 * system.  Fortunately, a system should probably only need 1 or 2 bucket
 * descriptor pages, since a page can hold 256 bucket descriptors (which
 * corresponds to 1 megabyte worth of bucket pages.)  If the kernel is using 
 * that much allocated memory, it's probably doing something wrong.  :-)
 *
 * Note: malloc() and free() both call get_free_page() and free_page()
 *	in sections of code where interrupts are turned off, to allow
 *	malloc() and free() to be safely called from an interrupt routine.
 *	(We will probably need this functionality when networking code,
 *	particularily things like NFS, is added to Linux.)  However, this
 *	presumes that get_free_page() and free_page() are interrupt-level
 *	safe, which they may not be once paging is added.  If this is the
 *	case, we will need to modify malloc() to keep a few unused pages
 *	"pre-allocated" so that it can safely draw upon those pages if
 * 	it is called from an interrupt routine.
 *
 * 	Another concern is that get_free_page() should not sleep; if it 
 *	does, the code is carefully ordered so as to avoid any race 
 *	conditions.  The catch is that if malloc() is called re-entrantly, 
 *	there is a chance that unecessary pages will be grabbed from the 
 *	system.  Except for the pages for the bucket descriptor page, the 
 *	extra pages will eventually get released back to the system, though,
 *	so it isn't all that bad.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/system.h>

//存储桶结构描述符
struct bucket_desc {	/* 16 bytes */
	void			*page;	//该桶描述符对应的内存页面指针
	struct bucket_desc	*next; //下一个描述符指针
	void			*freeptr;	//指向桶中空闲内存位置指针
	unsigned short		refcnt;	//引用计数
	unsigned short		bucket_size;//本描述符对应存储桶的大小
};

//存储桶描述符目录结构
struct _bucket_dir {	/* 8 bytes */
	int			size;	/该存储桶的大小
	struct bucket_desc	*chain;	//该存储桶目录项的桶描述符链表指针
};

/*
 * The following is the where we store a pointer to the first bucket
 * descriptor for a given size.  
 *
 * If it turns out that the Linux kernel allocates a lot of objects of a
 * specific size, then we may want to add that specific size to this list,
 * since that will allow the memory to be allocated more efficiently.
 * However, since an entire page must be dedicated to each specific size
 * on this list, some amount of temperance must be exercised here.
 *
 * Note that this list *must* be kept in order.
 */
// 存储桶目录列(数组)
struct _bucket_dir bucket_dir[] = {
	{ 16,	(struct bucket_desc *) 0}, //16字节长度的内存块
	{ 32,	(struct bucket_desc *) 0},
	{ 64,	(struct bucket_desc *) 0},
	{ 128,	(struct bucket_desc *) 0},
	{ 256,	(struct bucket_desc *) 0},
	{ 512,	(struct bucket_desc *) 0},
	{ 1024,	(struct bucket_desc *) 0},
	{ 2048, (struct bucket_desc *) 0},
	{ 4096, (struct bucket_desc *) 0},
	{ 0,    (struct bucket_desc *) 0}};   /* End of list marker */

/*
 * This contains a linked list of free bucket descriptor blocks
 */
 //空闲桶描述符内存块的链表
struct bucket_desc *free_bucket_desc = (struct bucket_desc *) 0;

/*
 * This routine initializes a bucket description page.
 */
/*
 *	功能: 初始化桶描述符，并让free_bucket_desc指向第一个空闲桶描述符
 *	参数:
 */ 
static inline void init_bucket_desc()
{
	struct bucket_desc *bdesc, *first;
	int	i;
	
	first = bdesc = (struct bucket_desc *) get_free_page(); //申请一页内存，用于存放桶描述符
	if (!bdesc)
		panic("Out of memory in init_bucket_desc()"); //失败，死机
	for (i = PAGE_SIZE/sizeof(struct bucket_desc); i > 1; i--) { //计算一页内存中可存放桶描述数量
		bdesc->next = bdesc+1;//建立单向链接
		bdesc++;
	}
	/*
	 * This is done last, to avoid race conditions in case 
	 * get_free_page() sleeps and this routine gets called again....
	 */
	bdesc->next = free_bucket_desc; //将空闲桶描述符指针加入链表
	free_bucket_desc = first; 
}

/*
 *	功能: 分配动态内存
 *	参数: len: 请求内存块的长度
 *  返回值: 指向被分配内存指针，如果失败则返回NULL
 */ 
void *malloc(unsigned int len)
{
	struct _bucket_dir	*bdir;
	struct bucket_desc	*bdesc;
	void			*retval;

	/*
	 * First we search the bucket_dir to find the right bucket change
	 * for this request.
	 */
	for (bdir = bucket_dir; bdir->size; bdir++) //搜索存储桶目录来寻找适合请求的桶大小
		if (bdir->size >= len)
			break;
			
	if (!bdir->size) {						   //死机
		printk("malloc called with impossibly large argument (%d)\n",
			len);
		panic("malloc: bad arg");
	}
	
	/*
	 * Now we search for a bucket descriptor which has free space
	 */
	cli();	/* Avoid race conditions */        //关中断
	
	for (bdesc = bdir->chain; bdesc; bdesc = bdesc->next) //搜索对应桶目录项中描述符链表，查找具有空闲空间的桶描述符。
		if (bdesc->freeptr)
			break;
	/*
	 * If we didn't find a bucket with free space, then we'll 
	 * allocate a new one.
	 */ 
	 //如果没有找到，则要分配一页桶描述符
	if (!bdesc) {
		char		*cp;
		int		i;

		if (!free_bucket_desc)	
			init_bucket_desc();//申请一页描述符，建立空闲桶描述符链表，并让free_bucket_desc指向第一个空闲桶描述符

		/* 从空闲的链表上区一个桶描述符，如果空闲链表没有可用的桶描述符，则再申请一页桶描述符 */
		bdesc = free_bucket_desc;//指向空闲桶(取一个)
		free_bucket_desc = bdesc->next;//空闲桶指向下一个描述符

		/* 初始化要使用的桶描述符 */
		bdesc->refcnt = 0;//清空应用计数
		bdesc->bucket_size = bdir->size; //桶空间大小等于桶目录指定的大小
		bdesc->page = bdesc->freeptr = (void *) cp = get_free_page();//申请一页内存，用于存储数据，并让空闲桶的描述符等于该页的物理地址
		if (!cp)
			panic("Out of memory in kernel malloc()");

		/* 初始化内存页块，地址+数据块 */
		/* Set up the chain of free objects */
		for (i=PAGE_SIZE/bdir->size; i > 1; i--) {
			*((char **) cp) = cp + bdir->size;
			cp += bdir->size;
		} //物理地址
		
		*((char **) cp) = 0; //最后为NULL
		
		bdesc->next = bdir->chain; /* OK, link it in! */ //向头上插入
		bdir->chain = bdesc;
	}
	
	retval = (void *) bdesc->freeptr;//返回指针即等于该描述符对应页面的当前空闲指针
	bdesc->freeptr = *((void **) retval);//调整该空闲空间指针指向下一个空闲对象；
	bdesc->refcnt++;//该描述符对应页面中对象引用计数加一
	sti();	/* OK, we're safe again */ //开中断
	return(retval);
}

/*
 * Here is the free routine.  If you know the size of the object that you
 * are freeing, then free_s() will use that information to speed up the
 * search for the bucket descriptor.
 * 
 * We will #define a macro so that "free(x)" is becomes "free_s(x, 0)"
 */
 /*
 *	功能: 分配释放内存
 *	参数: obj: 对应对象的指针
 *		  size: 大小
 */ 
void free_s(void *obj, int size)
{
	void		*page;
	struct _bucket_dir	*bdir;
	struct bucket_desc	*bdesc, *prev;

	/* Calculate what page this object lives in */
	page = (void *)  ((unsigned long) obj & 0xfffff000);//计算该桶所在的页面
	
	/* Now search the buckets looking for that page */
	for (bdir = bucket_dir; bdir->size; bdir++) { //搜索桶目录
		prev = 0;
		/* If size is zero then this conditional is always false */
		if (bdir->size < size)
			continue;  //搜索释放页面所在的桶目录
			
		for (bdesc = bdir->chain; bdesc; bdesc = bdesc->next) { //搜索对应目录项中链接所有描述符，查找对应页面。
			if (bdesc->page == page) //如果某描述符页面指针等于page则表示找到了相应的描述符�
				goto found;
			prev = bdesc; // prev为搜索到描述符前一个描述符
		}
	}
	
	panic("Bad address passed to kernel free_s()"); //死机
	
found:
	cli(); /* To avoid race conditions */ //关中断
	*((void **)obj) = bdesc->freeptr; //插入到空闲链表中
	bdesc->freeptr = obj;
	
	bdesc->refcnt--; //描述符对象引用计数减1
	if (bdesc->refcnt == 0) { //如果该描述符引用计数为0，则释放对应的内存页面和该桶描述符
		/*
		 * We need to make sure that prev is still accurate.  It
		 * may not be, if someone rudely interrupted us....
		 */
		if ((prev && (prev->next != bdesc)) ||
		    (!prev && (bdir->chain != bdesc)))//prev的next不为bdesc或者prev不存在，但是bdesc又不是头节点
		    
			for (prev = bdir->chain; prev; prev = prev->next) //重新找到bdesc的prev
				if (prev->next == bdesc)
					break;
					
		if (prev)
			prev->next = bdesc->next; //删除了bdesc描述符
		else {
			if (bdir->chain != bdesc) //bdesc为头节点
				panic("malloc bucket chains corrupted");
			bdir->chain = bdesc->next;
		}
		
		free_page((unsigned long) bdesc->page); //释放物理页
		bdesc->next = free_bucket_desc; //bdesc插入空闲描述符链表中
		free_bucket_desc = bdesc;
	}
	
	sti();//开中断，返回
	return;
}

