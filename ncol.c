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
static char rcsid[] = "$Id: ncol.c,v 1.10 1998/02/12 04:36:18 token Exp $";
#endif

#include "tirc.h"

#ifdef	HAVE_MALLOC_H
#include <malloc.h>
#endif
#ifdef	HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef	HAVE_STRING_H
#include <string.h>
#endif

#include "compat.h"
#include "colour.h"

#ifdef	WITH_ANSI_COLOURS

static int	cachecompare __P((const void *, const void *));
static void	ncol_reorder_cache __P((void));

extern char	ppre[];

#define	NCOL_CACHESZ	8	/* number of cache slots per list entry */

/*
 * A nickname colour list entry.
 */
struct ncol_entry {
	char	ncol_pattern[NICKSZ+1];		/* nick pattern */
	unsigned long ncol_hash;		/* pattern hash */
	int	ncol_degree;			/* matching degree */
	int	ncol_fgcol;			/* fg colour */
	int	ncol_bgcol;			/* bg colour */
	struct ncc_entry {
		unsigned long ncc_hash;		/* nickname hash value */
		unsigned long ncc_actr;		/* cache slot access counter */
	} ncol_cache[NCOL_CACHESZ];		/* nickname cache */
	sig_atomic_t ncol_cachelock;		/* mutual exclusion */
	int	ncol_cachefill;			/* cache fill counter */
	int	ncol_cachehits;			/* cache hit counter */
	int	ncol_hctr;			/* hit counter */
	LIST_ENTRY(ncol_entry) ncol_entries;	/* links */
};

static LIST_HEAD(, ncol_entry) ncol_list;
sig_atomic_t sem_changelist;

/*
 * Initialize the approximate nickname colourization.
 */
void
ncol_init()
{
	LIST_INIT(&ncol_list);
	pq_add(ncol_reorder_cache);
}

/*
 * Registers a new nickname-colour correlation to the list.
 */
void
ncol_register(npat, degree, forec, backc)
	char *npat;
	int degree;
	int forec, backc;
{
	struct ncol_entry *nc;
	unsigned long phash = irc_elf_hash(npat);
	int newnc = 0;
	
	for (nc = ncol_list.lh_first; nc != NULL;
	    nc = nc->ncol_entries.le_next)
	    	if (phash == nc->ncol_hash)
			break;
	
	if (nc == NULL) {
		nc = chkmem(calloc(1, sizeof *nc));
		newnc++;
		nc->ncol_hash = phash;
	}
	strncpy(nc->ncol_pattern, npat, NICKSZ);
	nc->ncol_pattern[NICKSZ] = '\0';
	nc->ncol_degree = degree;
	nc->ncol_fgcol = forec;
	nc->ncol_bgcol = backc;
	sem_changelist = 1;
	if (newnc)
		LIST_INSERT_HEAD(&ncol_list, nc, ncol_entries);
	sem_changelist = 0;
	setlog(0);
	iw_printf(COLI_TEXT, "%sNew nick colour binding registered\n", ppre);
	setlog(1);
}

/*
 * Print the ncol list.
 */
void
ncol_print()
{
	struct ncol_entry *nc;
	int i;

	setlog(0);
	iw_printf(COLI_TEXT, "%sNick colourization bindings:\n", ppre);

	for (i = 0, nc = ncol_list.lh_first; nc != NULL;
	    nc = nc->ncol_entries.le_next, i++)
		iw_printf(COLI_TEXT, "  [%2d]  %s  fg=%s  bg=%s  \
degree=%d  hits=%d  cachefill=%d  cachehits=%d\n", i, nc->ncol_pattern,
			getcolourname(nc->ncol_fgcol),
			getcolourname(nc->ncol_bgcol),
			nc->ncol_degree, nc->ncol_hctr,
			nc->ncol_cachefill, nc->ncol_cachehits);
	iw_printf(COLI_TEXT, "%sEnd of colour bindings\n", ppre);

	setlog(1);
}

/*
 * Remove an entry from the ncol list.
 */
void
ncol_remove(n)
	int n;
{
	struct ncol_entry *nc, *ncn;
	int i;

	sem_changelist = 1;

	for (i = 0, nc = ncol_list.lh_first; nc != NULL; nc = ncn, i++) {
		ncn = nc->ncol_entries.le_next;
		if (i == n) {
			LIST_REMOVE(nc, ncol_entries);
			setlog(0);
			iw_printf(COLI_TEXT,
				"%sNick colour binding %s removed\n", ppre,
				nc->ncol_pattern);
			setlog(1);
			free(nc);
			return;
		}
	}
	setlog(0);
	iw_printf(COLI_TEXT, "%s%d: no such nick colour binding\n", ppre, n);
	setlog(1);

	sem_changelist = 0;
	return;
}

/*
 * Try to find a approximate match in the nickname colourization list
 * for 'nname.
 * Returns 1 if matching pattern found and then puts the appropriate
 * colours values in fgc, bgc, and 0 if no match was found.
 */
int
ncol_match(nname, fgc, bgc)
	const char *nname;
	int *fgc, *bgc;
{
	struct ncol_entry *nc;
	unsigned long nhash = irc_elf_hash(nname);
	int hit;

	/*
	 * First, traverse the ncol list, looking for matches in the cache.
	 * If one is found, add the hit to the counters, and return the
	 * foreground and background colour values.
	 */
	for (nc = ncol_list.lh_first; nc != NULL;
	    nc = nc->ncol_entries.le_next) {
	    	int i;

		hit = 0;

		/* Try exact match first. */
		if (nhash == nc->ncol_hash)
			hit++;
		else {
			/* No exact match, try cache. */
			if (nc->ncol_cachelock)	/* XXX check not required */
				continue;	/*	at the moment */
			nc->ncol_cachelock = 1;
			for (i = 0; i < nc->ncol_cachefill && !hit; i++)
				if (nhash == nc->ncol_cache[i].ncc_hash) {
					nc->ncol_cache[i].ncc_actr++;
					nc->ncol_cachehits++;
					hit++;
				}
			nc->ncol_cachelock = 0;
		}
		if (hit) {
			*fgc = nc->ncol_fgcol;
			*bgc = nc->ncol_bgcol;
			nc->ncol_hctr++;
#ifdef	DEBUGV
			iw_printf(COLI_TEXT, "DEBUG: ncol match direct or \
in cache\n");
#endif
			return 1;
		}
	}

	/*
	 * No matching entry found.  Now iterate through the list a second
	 * time, and search for approximate matches.  If one is found, add
	 * it to the respective entry's cache, increment the counters, and
	 * return the colour values.
	 */
	for (nc = ncol_list.lh_first; nc != NULL;
	    nc = nc->ncol_entries.le_next) {
		if (approx_match(nc->ncol_pattern, nname, nc->ncol_degree)) {
			nc->ncol_cachelock = 1;
			if (nc->ncol_cachefill > NCOL_CACHESZ-1) {
				nc->ncol_cachefill--;
				nc->ncol_cache[nc->ncol_cachefill].ncc_actr = 0;
			}
			nc->ncol_cache[nc->ncol_cachefill].ncc_hash = nhash;
			nc->ncol_cache[nc->ncol_cachefill].ncc_actr++;
			nc->ncol_cachefill++;
			nc->ncol_hctr++;
			nc->ncol_cachelock = 0;
			*fgc = nc->ncol_fgcol;
			*bgc = nc->ncol_bgcol;
#ifdef	DEBUGV
			iw_printf(COLI_TEXT, "DEBUG: ncol match found, added \
to cache\n");
#endif
			return 1;
		}
	}

#ifdef	DEBUGV
	iw_printf(COLI_TEXT, "DEBUG: no ncol match found\n");
#endif
	return 0;
}

/*
 * This function implements k-approximate string matching.
 * It compares two strings, pattern and text, and returns 1 if both
 * strings are different in less than 'degree places, 0 otherwise.
 */
int
approx_match(pattern, text, degree)
	const char *pattern, *text;
	int degree;
{
	const char *big, *small;
	int mismatch = 0, blen, slen;
	int biter;

	if ((blen = strlen(text)) > (slen = strlen(pattern))) {
		big = text;
		small = pattern;
	}
	else {
		int t = blen;

		blen = slen;
		slen = t;
		big = pattern;
		small = text;
	}

	if (blen - slen > degree)
		return 0;

	for (biter = 0; biter <= blen - slen && biter <= degree; biter++) {
		register const char *b = big+biter, *s = small;
		int siter;

		for (siter = 0, mismatch = biter; siter < slen; siter++, s++,
		    b++)
			if (irc_chrcmp(*s, *b))
				mismatch++;
		if (mismatch <= degree)
			return 1;
	}

	return (mismatch <= degree);
}

/*
 * This function rearranges all list entry caches in most-hits-first order.
 * Called periodically (usually every minute).
 */
static void
ncol_reorder_cache()
{
	struct ncol_entry *nc;

	if (!check_conf(CONF_COLOUR | CONF_NCOLOUR) || sem_changelist)
		return;

	for (nc = ncol_list.lh_first; nc != NULL;
	    nc = nc->ncol_entries.le_next)
		if (!nc->ncol_cachelock) {
			/* XXX locking unrequired; but good style */
			nc->ncol_cachelock = 1;
			if (nc->ncol_cachefill > 1)
				qsort(nc->ncol_cache,
					(unsigned)nc->ncol_cachefill,
					sizeof nc->ncol_cache[0], cachecompare);
			nc->ncol_cachelock = 0;
		}
}

static int
cachecompare(alpha, beta)
	const VOIDPTR alpha;
	const VOIDPTR beta;
{
	return ((int)((struct ncc_entry *)beta)->ncc_actr
		- (int)((struct ncc_entry *)alpha)->ncc_actr);
}

#endif	/* WITH_ANSI_COLOURS */

