/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

/*
 * Real VM (paging to/from disk) started 18.12.91. Much more work and
 * thought has to go into this. Oh, well..
 * 19.12.91  -  works, somewhat. Sometimes I get faults, don't know why.
 *		Found it. Everything seems to work now.
 * 20.12.91  -  Ok, making the swap-device changeable like the root.
 */

#include <signal.h>

#include <asm/system.h>

#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>

#define CODE_SPACE(addr) ((((addr)+4095)&~4095) < \
current->start_code + current->end_code)

unsigned long HIGH_MEMORY = 0;

#define copy_page(from,to) \
__asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024):"cx","di","si")

/*
 *	内存位图
 */
unsigned char mem_map [ PAGING_PAGES ] = {0,}; //此种初始化啥意思

/*
 * Free a page of memory at physical address 'addr'. Used by
 * 'free_page_tables()'
 */
 
 /*
 *	功能: 释放addr处的一页内存，释放的方式是放位图置位为0	  
 *  返回: 
 *	参数:
 */
void free_page(unsigned long addr)
{
	if (addr < LOW_MEM) return; //1//1M以下的内存用于内核和缓冲，不作为非配页面的内存空间
	if (addr >= HIGH_MEMORY)
		panic("trying to free nonexistent page"); // 死机
		
	addr -= LOW_MEM;
	addr >>= 12; //根据内存物理地址换算出页面号
	if (mem_map[addr]--) return; //如果mem_map[addr]为0，则该页本来就没有被使用，死机
	mem_map[addr]=0; 
	panic("trying to free free page");
}

/*
 * This function frees a continuos block of page tables, as needed
 * by 'exit()'. As does copy_page_tables(), this handles only 4Mb blocks.
 */
 /*
 *	功能: 释放连续块，块必须4M对齐	  
 *  返回: 
 *	参数:
 */
int free_page_tables(unsigned long from,unsigned long size)
{
	unsigned long *pg_table;
	unsigned long * dir, nr;

	if (from & 0x3fffff)	//4									//4M对齐检测
		panic("free_page_tables called with wrong alignment");
	if (!from)													//0地址是内核驻留地址，不允许释放，内核和缓冲为前4M 
		panic("Trying to free up swapper memory space");
		
	size = (size + 0x3fffff) >> 22;  							//如果是4.1M，size取整为2
	dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */ //页目录起始地址
	
	for ( ; size-->0 ; dir++) {
		if (!(1 & *dir))						//如果页目录项未使用，跳过
			continue;
			
		pg_table = (unsigned long *) (0xfffff000 & *dir);	//取页表项
		for (nr=0 ; nr<1024 ; nr++) {						//每个页表项有1024个物理页
			if (*pg_table) {								//所指页表项内容不为0
				if (1 & *pg_table)							//位0等于1表示物理页有效
					free_page(0xfffff000 & *pg_table);		//释放物理页
				else
					swap_free(*pg_table >> 1);				//释放交换设备中对应项
				*pg_table = 0;								//页表项内容清0
			}
			pg_table++;
		}
		free_page(0xfffff000 & *dir);						//释放该页表所占内存页面					
		*dir = 0;											//页目录项内容清零
	}
	invalidate();											//刷新高速缓存 
	return 0;												//返回0表示操作成功
}

/*
 *  Well, here is one of the most complicated functions in mm. It
 * copies a range of linerar addresses by copying only the pages.
 * Let's hope this is bug-free, 'cause this one I don't want to debug :-)
 *
 * Note! We don't copy just any chunks of memory - addresses have to
 * be divisible by 4Mb (one page-directory entry), as this makes the
 * function easier. It's used only by fork anyway.
 *
 * NOTE 2!! When from==0 we are copying kernel space for the first
 * fork(). Then we DONT want to copy a full page-directory entry, as
 * that would lead to some serious memory waste - we just copy the
 * first 160 pages - 640kB. Even that is more than we need, but it
 * doesn't take any more memory - we don't copy-on-write in the low
 * 1 Mb-range, so the pages can be shared with the kernel. Thus the
 * special case for nr=xxxx.
 */
  /*
 *	功能: 释放addr处的一页内存，释放的方式是放位图置位为0	  
 *  返回: 
 *	参数: 从线性地址from到to共享size个字节内存长度。
 */
int copy_page_tables(unsigned long from,unsigned long to,long size)
{
	unsigned long * from_page_table;
	unsigned long * to_page_table;
	unsigned long this_page;
	unsigned long * from_dir, * to_dir;
	unsigned long new_page;
	unsigned long nr;

	if ((from&0x3fffff) || (to&0x3fffff)) //4//4M对齐
		panic("copy_page_tables called with wrong alignment");
		
	from_dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */ //源起始目录项指针
	to_dir = (unsigned long *) ((to>>20) & 0xffc);//	//目的起目录项起始指针
	size = ((unsigned) (size+0x3fffff)) >> 22;//要复制的页表个数

	for( ; size-->0 ; from_dir++,to_dir++) {
		if (1 & *to_dir) //页表项的最低位表示存在位
			panic("copy_page_tables: already exist");
		if (!(1 & *from_dir))
			continue;
			
		from_page_table = (unsigned long *) (0xfffff000 & *from_dir);
		if (!(to_page_table = (unsigned long *) get_free_page()))
			return -1;	/* Out of memory, see freeing */ //申请一页内存失败
			
		*to_dir = ((unsigned long) to_page_table) | 7;//设置也表项，用户级别读写权限
		nr = (from==0)?0xA0:1024; //如果在内核空间，只需复制头160页对应的也表，如果在用户空间，需要复制1024个页表项
		
		for ( ; nr-- > 0 ; from_page_table++,to_page_table++) {
			this_page = *from_page_table;
			if (!this_page)
				continue;
				
			if (!(1 & this_page)) {
				if (!(new_page = get_free_page()))
					return -1;
				read_swap_page(this_page>>1, (char *) new_page);
				*to_page_table = this_page;
				*from_page_table = new_page | (PAGE_DIRTY | 7);//设置页面脏
				continue;
			}
			this_page &= ~2;
			*to_page_table = this_page;
			if (this_page > LOW_MEM) { //非用户页面，内核页面，要更新页位图
				*from_page_table = this_page;
				this_page -= LOW_MEM;
				this_page >>= 12;
				mem_map[this_page]++;
			}
		}
	}
	invalidate();
	return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */
/*
 *	功能: 把一内存页面映射到指定的线性地址处，把线性地址空间指定地址address	  
 *  返回: 页面物理地址
 *	参数: page:内存中某一页面的指针，address是线性地址
 */
static unsigned long put_page(unsigned long page,unsigned long address)
{
	unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */

	if (page < LOW_MEM || page >= HIGH_MEMORY) 				//物理内存低于1M或者超出实际含有内存高端
		printk("Trying to put page %p at %p\n",page,address);
		
	if (mem_map[(page-LOW_MEM)>>12] != 1)					//检查page页面是不是被申请的页面
		printk("mem_map disagrees with %p at %p\n",	page,address);
		
	page_table = (unsigned long *) ((address>>20) & 0xffc);	//根据线性地址找到页目录表中页目录项指针
	if ((*page_table)&1)									//目录项有效，即指定的页表在内存中
		page_table = (unsigned long *) (0xfffff000 & *page_table);//页表地址
	else {
		if (!(tmp=get_free_page()))							//申请空闲页给页表使用
			return 0;
		*page_table = tmp | 7;								//设置相关页目录项
		page_table = (unsigned long *) tmp;
	}
	page_table[(address>>12) & 0x3ff] = page | 7;//设置相关页表项
/* no need for invalidate */
	return page;
}

/*
 * The previous function doesn't work very well if you also want to mark
 * the page dirty: exec.c wants this, as it has earlier changed the page,
 * and we want the dirty-status to be correct (for VM). Thus the same
 * routine, but this time we mark it dirty too.
 */
unsigned long put_dirty_page(unsigned long page, unsigned long address)
{
	unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */

	if (page < LOW_MEM || page >= HIGH_MEMORY)
		printk("Trying to put page %p at %p\n",page,address);
	if (mem_map[(page-LOW_MEM)>>12] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);
	page_table = (unsigned long *) ((address>>20) & 0xffc);
	if ((*page_table)&1)
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	else {
		if (!(tmp=get_free_page()))
			return 0;
		*page_table = tmp|7;
		page_table = (unsigned long *) tmp;
	}
	page_table[(address>>12) & 0x3ff] = page | (PAGE_DIRTY | 7);
/* no need for invalidate */
	return page;
}

void un_wp_page(unsigned long * table_entry)
{
	unsigned long old_page,new_page;

	old_page = 0xfffff000 & *table_entry;
	if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)]==1) {
		*table_entry |= 2;
		invalidate();
		return;
	}
	if (!(new_page=get_free_page()))
		oom();
	if (old_page >= LOW_MEM)
		mem_map[MAP_NR(old_page)]--;
	copy_page(old_page,new_page);
	*table_entry = new_page | 7;
	invalidate();
}	

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * If it's in code space we exit with a segment error.
 */
/*
 *	功能: 写共享页面时，COW
 *	参数: error_code:进程写保护页由cpu产生异常而自动生成，错误类型
 *  address: 异常页面线性地址
 */
void do_wp_page(unsigned long error_code,unsigned long address)
{
	if (address < TASK_SIZE)//64M，页异常页面位置在内核或者任务0和任务1处的线性地址范围呢
		printk("\n\rBAD! KERNEL MEMORY WP-ERR!\n\r");
	if (address - current->start_code > TASK_SIZE) { //current->start_code 当前进程起始地址
		printk("Bad things happen: page error in do_wp_page\n\r");
		do_exit(SIGSEGV);
	}
#if 0
/* we cannot do this yet: the estdio library writes to code space */
/* stupid, stupid. I really want the libc.a from GNU */
	if (CODE_SPACE(address))
		do_exit(SIGSEGV);
#endif
	un_wp_page((unsigned long *)
		(((address>>10) & 0xffc) + (0xfffff000 &
		*((unsigned long *) ((address>>20) &0xffc)))));

}

void write_verify(unsigned long address)
{
	unsigned long page;

	if (!( (page = *((unsigned long *) ((address>>20) & 0xffc)) )&1))
		return;
	page &= 0xfffff000;
	page += ((address>>10) & 0xffc);
	if ((3 & *(unsigned long *) page) == 1)  /* non-writeable, present */
		un_wp_page((unsigned long *) page);
	return;
}

/*
 *	功能: 物理内存管理初始化:内核使用的1M以上高速缓冲和虚拟盘置为占用状态，
 *  	  而主内存区则为未占用状态。
 *	参数:
 * 		  start_mem: 4M	
 *		  end_mem:   16M
 */
void get_empty_page(unsigned long address)
{
	unsigned long tmp;

	if (!(tmp=get_free_page()) || !put_page(tmp,address)) {
		free_page(tmp);		/* 0 is ok - ignored */
		oom();
	}
}

/*
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same executable or library.
 */
static int try_to_share(unsigned long address, struct task_struct * p)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;

	from_page = to_page = ((address>>20) & 0xffc);
	from_page += ((p->start_code>>20) & 0xffc);
	to_page += ((current->start_code>>20) & 0xffc);
/* is there a page-directory at from? */
	from = *(unsigned long *) from_page;
	if (!(from & 1))
		return 0;
	from &= 0xfffff000;
	from_page = from + ((address>>10) & 0xffc);
	phys_addr = *(unsigned long *) from_page;
/* is the page clean and present? */
	if ((phys_addr & 0x41) != 0x01)
		return 0;
	phys_addr &= 0xfffff000;
	if (phys_addr >= HIGH_MEMORY || phys_addr < LOW_MEM)
		return 0;
	to = *(unsigned long *) to_page;
	if (!(to & 1))
		if (to = get_free_page())
			*(unsigned long *) to_page = to | 7;
		else
			oom();
	to &= 0xfffff000;
	to_page = to + ((address>>10) & 0xffc);
	if (1 & *(unsigned long *) to_page)
		panic("try_to_share: to_page already exists");
/* share them: write-protect */
	*(unsigned long *) from_page &= ~2;
	*(unsigned long *) to_page = *(unsigned long *) from_page;
	invalidate();
	phys_addr -= LOW_MEM;
	phys_addr >>= 12;
	mem_map[phys_addr]++;
	return 1;
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */
static int share_page(struct m_inode * inode, unsigned long address)
{
	struct task_struct ** p;

	if (inode->i_count < 2 || !inode)
		return 0;
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p)
			continue;
		if (current == *p)
			continue;
		if (address < LIBRARY_OFFSET) {
			if (inode != (*p)->executable)
				continue;
		} else {
			if (inode != (*p)->library)
				continue;
		}
		if (try_to_share(address,*p))
			return 1;
	}
	return 0;
}

void do_no_page(unsigned long error_code,unsigned long address)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	int block,i;
	struct m_inode * inode;

	if (address < TASK_SIZE)
		printk("\n\rBAD!! KERNEL PAGE MISSING\n\r");
	if (address - current->start_code > TASK_SIZE) {
		printk("Bad things happen: nonexistent page error in do_no_page\n\r");
		do_exit(SIGSEGV);
	}
	page = *(unsigned long *) ((address >> 20) & 0xffc);
	if (page & 1) {
		page &= 0xfffff000;
		page += (address >> 10) & 0xffc;
		tmp = *(unsigned long *) page;
		if (tmp && !(1 & tmp)) {
			swap_in((unsigned long *) page);
			return;
		}
	}
	address &= 0xfffff000;
	tmp = address - current->start_code;
	if (tmp >= LIBRARY_OFFSET ) {
		inode = current->library;
		block = 1 + (tmp-LIBRARY_OFFSET) / BLOCK_SIZE;
	} else if (tmp < current->end_data) {
		inode = current->executable;
		block = 1 + tmp / BLOCK_SIZE;
	} else {
		inode = NULL;
		block = 0;
	}
	if (!inode) {
		get_empty_page(address);
		return;
	}
	if (share_page(inode,tmp))
		return;
	if (!(page = get_free_page()))
		oom();
/* remember that 1 block is used for header */
	for (i=0 ; i<4 ; block++,i++)
		nr[i] = bmap(inode,block);
	bread_page(page,inode->i_dev,nr);
	i = tmp + 4096 - current->end_data;
	if (i>4095)
		i = 0;
	tmp = page + 4096;
	while (i-- > 0) {
		tmp--;
		*(char *)tmp = 0;
	}
	if (put_page(page,address))
		return;
	free_page(page);
	oom();
}

/*
 *	功能: 物理内存管理初始化:内核使用的1M以上高速缓冲和虚拟盘置为占用状态，
 *  	  而主内存区则为未占用状态。
 *	参数:
 * 		  start_mem: 4M	
 *		  end_mem:   16M
 */
void mem_init(long start_mem, long end_mem)
{
	int i;

	HIGH_MEMORY = end_mem;
	for (i=0 ; i<PAGING_PAGES ; i++)
		mem_map[i] = USED; //首先将1M - 16M内存内存页全部初始化为已用状态
		
	i = MAP_NR(start_mem); //主内存区起始页面号，共(16M - 4M)个页面
	end_mem -= start_mem; 
	end_mem >>= 12;	//主内存区中的页面总数
	while (end_mem-->0)
		mem_map[i++]=0; //4//4M - 16M主内存页面标志置为未使用,那么1M到4M其实为占用状态
}

void show_mem(void)
{
	int i,j,k,free=0,total=0;
	int shared=0;
	unsigned long * pg_tbl;

	printk("Mem-info:\n\r");
	for(i=0 ; i<PAGING_PAGES ; i++) {
		if (mem_map[i] == USED)
			continue;
		total++;
		if (!mem_map[i])
			free++;
		else
			shared += mem_map[i]-1; //计算共享的
	}
	
	printk("%d free pages of %d\n\r",free,total);
	printk("%d pages shared\n\r",shared);
	
	k = 0;
	for(i=4 ; i<1024 ;) {
		if (1&pg_dir[i]) {
			if (pg_dir[i]>HIGH_MEMORY) {
				printk("page directory[%d]: %08X\n\r",
					i,pg_dir[i]);
				continue;
			}
			if (pg_dir[i]>LOW_MEM)
				free++,k++;
			pg_tbl=(unsigned long *) (0xfffff000 & pg_dir[i]);
			for(j=0 ; j<1024 ; j++)
				if ((pg_tbl[j]&1) && pg_tbl[j]>LOW_MEM)
					if (pg_tbl[j]>HIGH_MEMORY)
						printk("page_dir[%d][%d]: %08X\n\r",
							i,j, pg_tbl[j]);
					else
						k++,free++;
		}
		
		i++;
		if (!(i&15) && k) {
			k++,free++;	/* one page/process for task_struct */
			printk("Process %d: %d pages\n\r",(i>>4)-1,k);
			k = 0;
		}
	}
	
	printk("Memory found: %d (%d)\n\r",free-shared,total);
}
