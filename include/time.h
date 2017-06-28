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

#define CLOCKS_PER_SEC 100//系统时钟滴答

typedef long clock_t;//从进程开始系统经过的时钟滴答

struct tm {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;//一周中的某天
	int tm_yday;//一年中的某天
	int tm_isdst;//夏令时标志
};

#define	__isleap(year)	\
  ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 1000 == 0))
  
clock_t clock(void);//处理器使用的时间
time_t time(time_t * tp);//取时间戳
double difftime(time_t time2, time_t time1);//time2 - time1
time_t mktime(struct tm * tp);//tm time change to calendar 

char * asctime(const struct tm * tp);//tm time change to string
char * ctime(const time_t * tp);//tm calendar time change to string
struct tm * gmtime(const time_t *tp);//tm calendar time change to tm
struct tm *localtime(const time_t * tp);//tm calendar time change to tm with zone
size_t strftime(char * s, size_t smax, const char * fmt, const struct tm * tp);//将tm时间转换为最大长度为smax,格式为fmt的字符串
void tzset(void);//初始化时间转换信息，使用环境变量，TZ对zname进行初始化

#endif
