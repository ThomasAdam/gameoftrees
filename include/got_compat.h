#ifndef _GOT_COMPAT_H
#define _GOT_COMPAT_H

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/uio.h>

#include <fnmatch.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>

/*
#include <termios.h>
#include <wchar.h>
*/

/* For flock. */
#ifndef O_EXLOCK
#define O_EXLOCK 0
#endif

#ifndef HAVE_FLOCK
#define LOCK_SH 0
#define LOCK_EX 0
#define LOCK_NB 0
#define flock(fd, op) (0)
#else
#include <sys/file.h>
#endif

#ifndef __dead
#define __dead __attribute__ ((__noreturn__))
#endif

#ifndef __OpenBSD__
#define pledge(s, p) (0)
#define unveil(s, p) (0)
#endif

#ifndef INFTIM
#define INFTIM -1
#endif

#ifndef HAVE_BSD_UUID
#include <uuid/uuid.h>
#define uuid_s_ok 0
#define uuid_s_bad_version  1
#define uuid_s_invalid_string_uuid 2
#define uuid_s_no_memory  3

/* Length of a node address (an IEEE 802 address). */
#define _UUID_NODE_LEN  6

struct uuid {
	uint32_t time_low;
	uint16_t time_mid;
	uint16_t time_hi_and_version;
	uint8_t  clock_seq_hi_and_reserved;
	uint8_t  clock_seq_low;
	uint8_t  node[_UUID_NODE_LEN];
};

int32_t uuid_equal(struct uuid *, struct uuid *, uint32_t *);
int32_t uuid_is_nil(struct uuid *, uint32_t *);
void uuid_create(uuid_t *, uint32_t *);
void uuid_create_nil(struct uuid *, uint32_t *);
void uuid_from_string(const char *, uuid_t *, uint32_t *);
void uuid_to_string(uuid_t *, char **, uint32_t *);
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#include <inttypes.h>
#endif

#ifdef HAVE_QUEUE_H
#include <sys/queue.h>
#else
#include "compat/queue.h"
#endif

#ifdef HAVE_TREE_H
#include <sys/tree.h>
#else
#include "compat/tree.h"
#endif

#ifdef HAVE_UTIL_H
#include <util.h>
#endif

#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif

#ifdef HAVE_IMSG
#else
#include "compat/imsg.h"
#endif

#ifndef HAVE_ASPRINTF
/* asprintf.c */
int		 asprintf(char **, const char *, ...);
int		 vasprintf(char **, const char *, va_list);
#endif

#ifndef HAVE_EXPLICIT_BZERO
/* explicit_bzero.c */
void		 explicit_bzero(void *, size_t);
#endif

#ifndef HAVE_GETDTABLECOUNT
/* getdtablecount.c */
int		 getdtablecount(void);
#endif

#ifndef HAVE_CLOSEFROM
/* closefrom.c */
//void		 closefrom(int);
#define closefrom(fd) (closefrom(fd), 0)
#endif

#ifndef HAVE_STRSEP
/* strsep.c */
char		*strsep(char **, const char *);
#endif

#ifndef HAVE_STRTONUM
/* strtonum.c */
long long	 strtonum(const char *, long long, long long, const char **);
#endif

#ifndef HAVE_STRLCPY
/* strlcpy.c */
size_t	 	 strlcpy(char *, const char *, size_t);
#endif

#ifndef HAVE_STRLCAT
/* strlcat.c */
size_t	 	 strlcat(char *, const char *, size_t);
#endif

#ifndef HAVE_STRNLEN
/* strnlen.c */
size_t		 strnlen(const char *, size_t);
#endif

#ifndef HAVE_STRNDUP
/* strndup.c */
char		*strndup(const char *, size_t);
#endif

#ifndef HAVE_GETPROGNAME
/* getprogname.c */
const char	*getprogname(void);
#endif

#ifndef HAVE_GETLINE
/* getline.c */
ssize_t		 getline(char **, size_t *, FILE *);
#endif

#ifndef HAVE_FREEZERO
/* freezero.c */
void		 freezero(void *, size_t);
#endif

#ifndef HAVE_GETDTABLECOUNT
/* getdtablecount.c */
int		 getdtablecount(void);
#endif

#ifndef HAVE_REALLOCARRAY
/* reallocarray.c */
void		*reallocarray(void *, size_t, size_t);
#endif

#ifndef HAVE_RECALLOCARRAY
/* recallocarray.c */
void		*recallocarray(void *, size_t, size_t, size_t);
#endif

#ifndef HAVE_FMT_SCALED
/* fmt_scaled.c */
int fmt_scaled(long long, char *);
int scan_scaled(char *, long long *);
#define FMT_SCALED_STRSIZE	7  /* minus sign, 4 digits, suffix, null byte */

#endif

#ifndef HAVE_LIBBSD
/* getopt.c */
extern int	BSDopterr;
extern int	BSDoptind;
extern int	BSDoptopt;
extern int	BSDoptreset;
extern char    *BSDoptarg;
int	BSDgetopt(int, char *const *, const char *);
#define getopt(ac, av, o)  BSDgetopt(ac, av, o)
#define opterr             BSDopterr
#define optind             BSDoptind
#define optopt             BSDoptopt
#define optreset           BSDoptreset
#define optarg             BSDoptarg
#endif
#endif

#ifndef HAVE_BSD_MERGESORT
/* mergesort.c */
int mergesort(void *, size_t, size_t, int (*)(const void *, const void *));
#endif
