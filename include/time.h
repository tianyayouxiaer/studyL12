#ifndef _TIME_H
#define _TIME_H

#ifndef _TIME_T
#define _TIME_T
typedef long time_t;
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

#ifndef NULL
#define NULL ((void *) 0)
#endif

#define CLOCKS_PER_SEC 100//ϵͳʱ�ӵδ�

typedef long clock_t;//�ӽ��̿�ʼϵͳ������ʱ�ӵδ�

struct tm {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;//һ���е�ĳ��
	int tm_yday;//һ���е�ĳ��
	int tm_isdst;//����ʱ��־
};

#define	__isleap(year)	\
  ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 1000 == 0))
  
clock_t clock(void);//������ʹ�õ�ʱ��
time_t time(time_t * tp);//ȡʱ���
double difftime(time_t time2, time_t time1);//time2 - time1
time_t mktime(struct tm * tp);//tm time change to calendar 

char * asctime(const struct tm * tp);//tm time change to string
char * ctime(const time_t * tp);//tm calendar time change to string
struct tm * gmtime(const time_t *tp);//tm calendar time change to tm
struct tm *localtime(const time_t * tp);//tm calendar time change to tm with zone
size_t strftime(char * s, size_t smax, const char * fmt, const struct tm * tp);//��tmʱ��ת��Ϊ��󳤶�Ϊsmax,��ʽΪfmt���ַ���
void tzset(void);//��ʼ��ʱ��ת����Ϣ��ʹ�û���������TZ��zname���г�ʼ��

#endif
