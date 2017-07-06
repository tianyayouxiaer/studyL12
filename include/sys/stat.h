#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

struct stat {
	dev_t	st_dev;
	ino_t	st_ino;
	umode_t	st_mode;
	nlink_t	st_nlink;
	uid_t	st_uid;
	gid_t	st_gid;
	dev_t	st_rdev;
	off_t	st_size;
	time_t	st_atime;
	time_t	st_mtime;
	time_t	st_ctime;
};

#define S_IFMT  00170000
#define S_IFLNK	 0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000

#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)

//文件访问权限
#define S_IRWXU 00700//宿主可以读、写、执行/搜索（名称最后字母代表user）
#define S_IRUSR 00400//宿主读许可
#define S_IWUSR 00200//宿主写许可
#define S_IXUSR 00100//宿主执行/搜索许可

#define S_IRWXG 00070//组成员可以读、写、执行/搜索（名称最后字母代表group）
#define S_IRGRP 00040//组成员读许可
#define S_IWGRP 00020//组成员写许可
#define S_IXGRP 00010//组成员执行/搜索许可

#define S_IRWXO 00007//其它人可以读、写、执行/搜索（名称最后字母代表group）
#define S_IROTH 00004//其它人读许可
#define S_IWOTH 00002//其它人写许可
#define S_IXOTH 00001//其它人执行/搜索许可

extern int chmod(const char *_path, mode_t mode);
extern int fstat(int fildes, struct stat *stat_buf);
extern int mkdir(const char *_path, mode_t mode);
extern int mkfifo(const char *_path, mode_t mode);
extern int stat(const char *filename, struct stat *stat_buf);
extern mode_t umask(mode_t mask);

#endif
