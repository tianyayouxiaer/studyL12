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

//¥Ê¥¢Õ∞Ω·ππ√Ë ˆ∑˚
struct bucket_desc {	/* 16 bytes */
	void			*page;	//∏√Õ∞√Ë ˆ∑˚∂‘”¶µƒƒ⁄¥Ê“≥√Ê÷∏’Î
	struct bucket_desc	*next; //œ¬“ª∏ˆ√Ë ˆ∑˚÷∏’Î
	void			*freeptr;	//÷∏œÚÕ∞÷–ø’œ–ƒ⁄¥ÊŒª÷√÷∏’Î
	unsigned short		refcnt;	//“˝”√º∆ ˝
	unsigned short		bucket_size;//±æ√Ë ˆ∑˚∂‘”¶¥Ê¥¢Õ∞µƒ¥Û–°
};

//¥Ê¥¢Õ∞√Ë ˆ∑˚ƒø¬ºΩ·ππ
struct _bucket_dir {	/* 8 bytes */
	int			size;	/∏√¥Ê¥¢Õ∞µƒ¥Û–°
	struct bucket_desc	*chain;	//∏√¥Ê¥¢Õ∞ƒø¬ºœÓµƒÕ∞√Ë ˆ∑˚¡¥±Ì÷∏’Î
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
// ¥Ê¥¢Õ∞ƒø¬º¡–( ˝◊È)
struct _bucket_dir bucket_dir[] = {
	{ 16,	(struct bucket_desc *) 0}, //16◊÷Ω⁄≥§∂»µƒƒ⁄¥ÊøÈ
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
 //ø’œ–Õ∞√Ë ˆ∑˚ƒ⁄¥ÊøÈµƒ¡¥±Ì
struct bucket_desc *free_bucket_desc = (struct bucket_desc *) 0;

/*
 * This routine initializes a bucket description page.
 */
/*
 *	π¶ƒ‹: ≥ı ºªØÕ∞√Ë ˆ∑˚£¨≤¢»√free_bucket_desc÷∏œÚµ⁄“ª∏ˆø’œ–Õ∞√Ë ˆ∑˚
 *	≤Œ ˝:
 */ 
static inline void init_bucket_desc()
{
	struct bucket_desc *bdesc, *first;
	int	i;
	
	first = bdesc = (struct bucket_desc *) get_free_page(); //…Í«Î“ª“≥ƒ⁄¥Ê£¨”√”⁄¥Ê∑≈Õ∞√Ë ˆ∑˚
	if (!bdesc)
		panic("Out of memory in init_bucket_desc()"); // ß∞‹£¨À¿ª˙
	for (i = PAGE_SIZE/sizeof(struct bucket_desc); i > 1; i--) { //º∆À„“ª“≥ƒ⁄¥Ê÷–ø…¥Ê∑≈Õ∞√Ë ˆ ˝¡ø
		bdesc->next = bdesc+1;//Ω®¡¢µ•œÚ¡¥Ω”
		bdesc++;
	}
	/*
	 * This is done last, to avoid race conditions in case 
	 * get_free_page() sleeps and this routine gets called again....
	 */
	bdesc->next = free_bucket_desc; //Ω´ø’œ–Õ∞√Ë ˆ∑˚÷∏’Îº”»Î¡¥±Ì
	free_bucket_desc = first; 
}

/*
 *	π¶ƒ‹: ∑÷≈‰∂ØÃ¨ƒ⁄¥Ê
 *	≤Œ ˝: len: «Î«Ûƒ⁄¥ÊøÈµƒ≥§∂»
 *  ∑µªÿ÷µ: ÷∏œÚ±ª∑÷≈‰ƒ⁄¥Ê÷∏’Î£¨»Áπ˚ ß∞‹‘Ú∑µªÿNULL
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
	for (bdir = bucket_dir; bdir->size; bdir++) //À—À˜¥Ê¥¢Õ∞ƒø¬º¿¥—∞’“  ∫œ«Î«ÛµƒÕ∞¥Û–°
		if (bdir->size >= len)
			break;
			
	if (!bdir->size) {						   //À¿ª˙
		printk("malloc called with impossibly large argument (%d)\n",
			len);
		panic("malloc: bad arg");
	}
	
	/*
	 * Now we search for a bucket descriptor which has free space
	 */
	cli();	/* Avoid race conditions */        //πÿ÷–∂œ
	
	for (bdesc = bdir->chain; bdesc; bdesc = bdesc->next) //À—À˜∂‘”¶Õ∞ƒø¬ºœÓ÷–√Ë ˆ∑˚¡¥±Ì£¨≤È’“æﬂ”–ø’œ–ø’º‰µƒÕ∞√Ë ˆ∑˚°£
		if (bdesc->freeptr)
			break;
	/*
	 * If we didn't find a bucket with free space, then we'll 
	 * allocate a new one.
	 */ 
	 //»Áπ˚√ª”–’“µΩ£¨‘Ú“™∑÷≈‰“ª“≥Õ∞√Ë ˆ∑˚
	if (!bdesc) {
		char		*cp;
		int		i;

		if (!free_bucket_desc)	
			init_bucket_desc();//…Í«Î“ª“≥√Ë ˆ∑˚£¨Ω®¡¢ø’œ–Õ∞√Ë ˆ∑˚¡¥±Ì£¨≤¢»√free_bucket_desc÷∏œÚµ⁄“ª∏ˆø’œ–Õ∞√Ë ˆ∑˚

		/* ¥”ø’œ–µƒ¡¥±Ì…œ«¯“ª∏ˆÕ∞√Ë ˆ∑˚£¨»Áπ˚ø’œ–¡¥±Ì√ª”–ø…”√µƒÕ∞√Ë ˆ∑˚£¨‘Ú‘Ÿ…Í«Î“ª“≥Õ∞√Ë ˆ∑˚ */
		bdesc = free_bucket_desc;//÷∏œÚø’œ–Õ∞(»°“ª∏ˆ)
		free_bucket_desc = bdesc->next;//ø’œ–Õ∞÷∏œÚœ¬“ª∏ˆ√Ë ˆ∑˚

		/* ≥ı ºªØ“™ π”√µƒÕ∞√Ë ˆ∑˚ */
		bdesc->refcnt = 0;//«Âø’”¶”√º∆ ˝
		bdesc->bucket_size = bdir->size; //Õ∞ø’º‰¥Û–°µ»”⁄Õ∞ƒø¬º÷∏∂®µƒ¥Û–°
		bdesc->page = bdesc->freeptr = (void *) cp = get_free_page();//…Í«Î“ª“≥ƒ⁄¥Ê£¨”√”⁄¥Ê¥¢ ˝æ›£¨≤¢»√ø’œ–Õ∞µƒ√Ë ˆ∑˚µ»”⁄∏√“≥µƒŒÔ¿Ìµÿ÷∑
		if (!cp)
			panic("Out of memory in kernel malloc()");

		/* ≥ı ºªØƒ⁄¥Ê“≥øÈ£¨µÿ÷∑+ ˝æ›øÈ */
		/* Set up the chain of free objects */
		for (i=PAGE_SIZE/bdir->size; i > 1; i--) {
			*((char **) cp) = cp + bdir->size;
			cp += bdir->size;
		} //ŒÔ¿Ìµÿ÷∑
		
		*((char **) cp) = 0; //◊Ó∫ÛŒ™NULL
		
		bdesc->next = bdir->chain; /* OK, link it in! */ //œÚÕ∑…œ≤Â»Î
		bdir->chain = bdesc;
	}
	
	retval = (void *) bdesc->freeptr;//∑µªÿ÷∏’Îº¥µ»”⁄∏√√Ë ˆ∑˚∂‘”¶“≥√Êµƒµ±«∞ø’œ–÷∏’Î
	bdesc->freeptr = *((void **) retval);//µ˜’˚∏√ø’œ–ø’º‰÷∏’Î÷∏œÚœ¬“ª∏ˆø’œ–∂‘œÛ£ª
	bdesc->refcnt++;//∏√√Ë ˆ∑˚∂‘”¶“≥√Ê÷–∂‘œÛ“˝”√º∆ ˝º”“ª
	sti();	/* OK, we're safe again */ //ø™÷–∂œ
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
 *	π¶ƒ‹: ∑÷≈‰ Õ∑≈ƒ⁄¥Ê
 *	≤Œ ˝: obj: ∂‘”¶∂‘œÛµƒ÷∏’Î
 *		  size: ¥Û–°
 */ 
void free_s(void *obj, int size)
{
	void		*page;
	struct _bucket_dir	*bdir;
	struct bucket_desc	*bdesc, *prev;

	/* Calculate what page this object lives in */
	page = (void *)  ((unsigned long) obj & 0xfffff000);//º∆À„∏√Õ∞À˘‘⁄µƒ“≥√Ê
	
	/* Now search the buckets looking for that page */
	for (bdir = bucket_dir; bdir->size; bdir++) { //À—À˜Õ∞ƒø¬º
		prev = 0;
		/* If size is zero then this conditional is always false */
		if (bdir->size < size)
			continue;  //À—À˜ Õ∑≈“≥√ÊÀ˘‘⁄µƒÕ∞ƒø¬º
			
		for (bdesc = bdir->chain; bdesc; bdesc = bdesc->next) { //À—À˜∂‘”¶ƒø¬ºœÓ÷–¡¥Ω”À˘”–√Ë ˆ∑˚£¨≤È’“∂‘”¶“≥√Ê°£
			if (bdesc->page == page) //»Áπ˚ƒ≥√Ë ˆ∑˚“≥√Ê÷∏’Îµ»”⁄page‘Ú±Ì æ’“µΩ¡Àœ‡”¶µƒ√Ë ˆ∑˚£
				goto found;
			prev = bdesc; // prevŒ™À—À˜µΩ√Ë ˆ∑˚«∞“ª∏ˆ√Ë ˆ∑˚
		}
	}
	
	panic("Bad address passed to kernel free_s()"); //À¿ª˙
	
found:
	cli(); /* To avoid race conditions */ //πÿ÷–∂œ
	*((void **)obj) = bdesc->freeptr; //≤Â»ÎµΩø’œ–¡¥±Ì÷–
	bdesc->freeptr = obj;
	
	bdesc->refcnt--; //√Ë ˆ∑˚∂‘œÛ“˝”√º∆ ˝ºı1
	if (bdesc->refcnt == 0) { //»Áπ˚∏√√Ë ˆ∑˚“˝”√º∆ ˝Œ™0£¨‘Ú Õ∑≈∂‘”¶µƒƒ⁄¥Ê“≥√Ê∫Õ∏√Õ∞√Ë ˆ∑˚
		/*
		 * We need to make sure that prev is still accurate.  It
		 * may not be, if someone rudely interrupted us....
		 */
		if ((prev && (prev->next != bdesc)) ||
		    (!prev && (bdir->chain != bdesc)))//prevµƒnext≤ªŒ™bdescªÚ’ﬂprev≤ª¥Ê‘⁄£¨µ´ «bdesc”÷≤ª «Õ∑Ω⁄µ„
		    
			for (prev = bdir->chain; prev; prev = prev->next) //÷ÿ–¬’“µΩbdescµƒprev
				if (prev->next == bdesc)
					break;
					
		if (prev)
			prev->next = bdesc->next; //…æ≥˝¡Àbdesc√Ë ˆ∑˚
		else {
			if (bdir->chain != bdesc) //bdescŒ™Õ∑Ω⁄µ„
				panic("malloc bucket chains corrupted");
			bdir->chain = bdesc->next;
		}
		
		free_page((unsigned long) bdesc->page); // Õ∑≈ŒÔ¿Ì“≥
		bdesc->next = free_bucket_desc; //bdesc≤Â»Îø’œ–√Ë ˆ∑˚¡¥±Ì÷–
		free_bucket_desc = bdesc;
	}
	
	sti();//ø™÷–∂œ£¨∑µªÿ
	return;
}

