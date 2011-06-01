/*-
 * tirc -- client for the Internet Relay Chat
 *
 *	Copyright (c) 1996, 1997, 1998
 *		Matthias Buelow.  All rights reserved.
 *
 *	See the file ``COPYRIGHT'' for the usage and distribution
 *	license and warranty disclaimer.
 */

#ifndef lint
static char rcsid[] = "$Id: compat.c,v 1.17 1998/02/02 03:41:29 mkb Exp $";
#endif

#include "tirc.h"

#ifdef	HAVE_STRING_H
#include <string.h>
#elif defined(HAVE_MEMORY_H)
#include <memory.h>
#endif
#ifdef	HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "compat.h"

#ifndef	__STDC__
#define	MAXFUNC	16	/* maximum number of functions to be registered */
void	(*funcs[MAXFUNC]) __P((void));
int	funcreg = 0;
#endif

/* Save a copy of a string */
char *
Strdup(str)
	const char *str;
{
#ifdef	HAVE_STRDUP
	return strdup(str);
#else
	char *s;
	int l;

	l = strlen(str);
	if ((s = malloc(l+1)) != NULL)
		memmove(s, str, l+1);
	return s;
#endif	/* HAVE_STRDUP */
}

/*
 * Register a function to be called at Exit().
 * This is a simple implementation that can only hold MAXFUNC functions.
 */
int
Atexit(func)
	void (*func) __P((void));
{
#ifdef	__STDC__
	return atexit(func);
#else
	if (funcreg < MAXFUNC) {
		funcs[funcreg++] = func;
		return 0;
	}
	errno = ENOMEM;
	return -1;
#endif
}

void
Exit(status)
	int status;
{
#ifdef	__STDC__
	exit(status);
#else
	int i;

	for (i = 0; i < funcreg; i++)
		(*funcs[i])();
	exit(status);
#endif
}

#ifndef	HAVE_BASENAME
char *
basename(s)
	char *s;
{
	static char def[] = ".";
	char *t;

	if (s == NULL || !strlen(s))
		return def;
	t = s + strlen(s);
	while (t > s && *t != '/')
		t--;
	if (*t == '/' && *(t+1))
		t++;
	return t;
}
#endif

/*
 * Returns system memory page size or -1 on error.
 */
int
Getpagesize()
{
#ifdef	HAVE_GETPAGESIZE
	return getpagesize();
#elif defined(HAVE_SYSCONF)
	return sysconf(_SC_PAGESIZE);
#elif defined(NBPG)
	return NBPG;
#else
	/* whoops, how shall we get the pagesize? */
	return -1;
#endif
}

/*
 * Returns the appropriate error message for errno.
 */
char *
Strerror(en)
	int en;
{
#ifdef	HAVE_STRERROR
	return strerror(en);
#else
	return sys_errlist[en];
#endif
}

#ifndef	HAVE_LABS
/*
 * Computes the absolute value of a long integer.
 */
long
labs(i)
	long i;
{
	return i < 0 ? -i : i;
}
#endif	/* !HAVE_LABS */

#ifndef	HAVE_ABS
/*
 * Same as labs for normal integers.
 */
int
abs(i)
	int i;
{
	return i < 0 ? -i : i;
}
#endif	/* !HAVE_ABS */
