#ifndef _HEAD_H
#define _HEAD_H

//�����������ݽṹ������������8���ֽڹ��ɣ�ÿ����������256�
typedef struct desc_struct {
	unsigned long a,b;
} desc_table[256];

extern unsigned long pg_dir[1024];//�ڴ���ҳĿ¼���飬ÿ��Ŀ¼��4���ֽڣ��������ַ0��ʼ
extern desc_table idt,gdt;//�ж���������ȫ����������

#define GDT_NUL 0 //ȫ�����������0�����
#define GDT_CODE 1//�ں˴������������
#define GDT_DATA 2//�ں����ݶ���������
#define GDT_TMP 3//ϵͳ���������linux��û��ʹ��

#define LDT_NUL 0//ÿ���ֲ���������ĵ�0�����
#define LDT_CODE 1//��һ����û�����������������
#define LDT_DATA 2//�ڶ�����û��������ݶ���������

#endif
