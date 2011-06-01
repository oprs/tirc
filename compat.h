/*-
 * tirc -- client for the Internet Relay Chat
 *
 *	Copyright (c) 1996, 1997, 1998
 *		Matthias Buelow.  All rights reserved.
 *
 *	See the file ``COPYRIGHT'' for the usage and distribution
 *	license and warranty disclaimer.
 *
 * $Id: compat.h,v 1.12 1998/02/18 20:13:54 token Exp $
 */

#ifndef	TIRC_COMPAT_H
#define	TIRC_COMPAT_H	1

char	*Strdup __P((const char *));
int	Atexit __P((void (*) __P((void))));
void	Exit __P((int));

#ifndef	HAVE_BASENAME
char	*basename __P((char *));
#endif

int	Getpagesize __P((void));
char	*Strerror __P((int));

#ifndef	HAVE_RAISE
#define	raise(s)	kill(getpid(), s);
#endif

#ifndef	HAVE_LABS
long	labs __P((long));
#endif

#ifndef	HAVE_BZERO
#ifdef	HAVE_MEMSET
#define	bzero(b, l)	memset((b), 0, (l))
#endif
#endif

#ifndef	STDIN_FILENO
#define	STDIN_FILENO	0
#endif
#ifndef	STDOUT_FILENO
#define	STDOUT_FILENO	1
#endif
#ifndef	STDERR_FILENO
#define	STDERR_FILENO	2
#endif

#ifndef	NULL
#define	NULL	0
#endif

#ifndef	HAVE_MEMMOVE
#define	memmove(dst, src, len)	bcopy((src), (dst), (len))
#endif

#ifndef	SEEK_SET
#define	SEEK_SET	0
#endif
#ifndef	SEEK_CUR
#define	SEEK_CUR	1
#endif
#ifndef	SEEK_END
#define	SEEK_END	2
#endif

#ifndef	HAVE_ASSERT_H
#undef	assert
#undef	_assert
#define	assert(a)	((void) 0)
#define _assert(a)	((void) 0)
#endif

#ifdef  __STDC__
#include <stdarg.h>
#elif defined(HAVE_VARARGS_H)
#include <varargs.h>
#endif

#ifndef	HAVE_VSNPRINTF
int	snprintf __P((char *, size_t, const char *, ...));
int	vsnprintf __P((char *, size_t, const char *, va_list));
#endif

#endif	/* !TIRC_COMPAT_H */

