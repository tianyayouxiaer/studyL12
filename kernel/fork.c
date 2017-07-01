/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

extern void write_verify(unsigned long address);

long last_pid=0;

 /*
 *	����: ���̿ռ�дǰ��֤��������addr��addr+size��ν��̿ռ���ҳΪ��λִ��д����ǰ�ļ�������
 *        ��ҳ����ֻ���ģ���ִ�й�������cow
 *  ����: 
 *	����:
 */
void verify_area(void * addr,int size)
{
	unsigned long start;

	start = (unsigned long) addr;
	size += start & 0xfff;
	start &= 0xfffff000;
	start += get_base(current->ldt[2]);
	while (size>0) {
		size -= 4096;
		write_verify(start);//��ҳ�治��д������ҳ��
		start += 4096;
	}
}

//�����ڴ�ҳ��
//nr - �������; p - ���������ݽṹ
int copy_mem(int nr,struct task_struct * p)
{
	unsigned long old_data_base,new_data_base,data_limit;
	unsigned long old_code_base,new_code_base,code_limit;

	code_limit=get_limit(0x0f); //�����������
	data_limit=get_limit(0x17);//���ݶ�������
	old_code_base = get_base(current->ldt[1]);//ȡ��ǰ���̴�������Կռ����ַ
	old_data_base = get_base(current->ldt[2]);//ȡ��ǰ�������ݶ����Կռ����ַ
	if (old_data_base != old_code_base)
		panic("We don't support separate I&D");
	if (data_limit < code_limit)
		panic("Bad data_limit");
		
	//Ȼ�����ô����е��½��������Կռ��еĻ���ַ����(64MBX�����)
	//���ø�ֵ�����½��ֲ̾����������ж��������еĻ���ַ
	new_data_base = new_code_base = nr * TASK_SIZE;
	p->start_code = new_code_base;
	set_base(p->ldt[1],new_code_base);
	set_base(p->ldt[2],new_data_base);

	//���ý���ҳĿ¼���ҳ��������Ƶ�ǰ���̣�������ҳĿ¼���ҳ���
	//��ʱ�������̺��ӽ��̹����ڴ�ҳ��
	if (copy_page_tables(old_data_base,new_data_base,data_limit)) {
		free_page_tables(new_data_base,data_limit);
		return -ENOMEM;
	}
	return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
 //���ƽ���
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx, long orig_eax, 
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
{
	struct task_struct *p;
	int i;
	struct file *f;

	//1��Ϊ���������ݽṹ�����ڴ�
	p = (struct task_struct *) get_free_page();
	if (!p)
		return -EAGAIN;
	//������ָ�����������������
	task[nr] = p;
	//2�����Ƶ�ǰ����ṹ���µ��������ݽṹ��
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack */

	//�޸�����ṹ
	p->state = TASK_UNINTERRUPTIBLE;//��ֹ�ں˵���ִ��
	p->pid = last_pid;
	p->counter = p->priority;
	p->signal = 0;
	p->alarm = 0;
	p->leader = 0;		/* process leadership doesn't inherit�������쵼Ȩ�ǲ��ܼ̳е� */
	p->utime = p->stime = 0;
	p->cutime = p->cstime = 0;
	p->start_time = jiffies;

	//�޸�����״̬��tss����
	p->tss.back_link = 0;
	p->tss.esp0 = PAGE_SIZE + (long) p;//�����ں�ָ̬�룬ָ���ҳ����ss0��esp0Ϊ�������ں�ִ̬��ʱ��ջ
	p->tss.ss0 = 0x10;
	p->tss.eip = eip;
	p->tss.eflags = eflags;
	p->tss.eax = 0;
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr);//����ֲ�����������ѡ���
	p->tss.trace_bitmap = 0x80000000;

	//3�������ǰ����ʹ����Э���������ͱ�����������
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0 ; frstor %0"::"m" (p->tss.i387));

	//4�����ƽ���ҳ���������Ե�ַ�ռ����������������κ����ݶ��������еĻ�ַ
	//���޳���������ҳ��
	if (copy_mem(nr,p)) {
		task[nr] = NULL;
		free_page((long) p);
		return -EAGAIN;
	}

	//5����������������ļ��Ǵ򿪵ģ����Ӧ�ļ��Ĵ򿪴�����1
	//�����̺��ӽ��̹���򿪵��ļ�
	for (i=0; i<NR_OPEN;i++)
		if (f=p->filp[i])
			f->f_count++;

	//����ǰ���̣������̵�pwd��root��executable���ô�����1��
	//�ӽ���ҳӦ������Щ�ڵ�
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;
	if (current->library)
		current->library->i_count++;

	//��gdt��������������tss�κ�ldt����������	
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));

	//���ý��̼��ϵ����ָ��
	p->p_pptr = current;//�����½��̸�����ָ��
	p->p_cptr = 0;//��λ�½��̵������ӽ���ָ��
	p->p_ysptr = 0;//��λ�½��̵ı��������ֵܽ���ָ��
	p->p_osptr = current->p_cptr;//�����½��̵ı��������ֵܽ���ָ��
	if (p->p_osptr)
		p->p_osptr->p_ysptr = p;//���½����������ֵܽ��̣���������������ֵ�ָ��ִ���½���
	current->p_cptr = p;//�õ�ǰ���������ӽ���ָ��ָ���½���
	p->state = TASK_RUNNING;	/* do this last, just in case,��Ϊ����̬ */
	return last_pid;//�����½��̺�
}

//Ϊ�½���ȡ�ò��ظ��Ľ��̺�last_pid���������������������е������
int find_empty_process(void)
{
	int i;

	repeat:
		if ((++last_pid)<0) last_pid=1;
		for(i=0 ; i<NR_TASKS ; i++)
			if (task[i] && ((task[i]->pid == last_pid) ||
				        (task[i]->pgrp == last_pid)))
				goto repeat;
	for(i=1 ; i<NR_TASKS ; i++)//����0���ų�����
		if (!task[i])
			return i;
	return -EAGAIN;
}
