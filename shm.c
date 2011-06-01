/*-
 * tirc -- client for the Internet Relay Chat
 *
 *	Copyright (c) 1996, 1997, 1998
 *		Matthias Buelow.  All rights reserved.
 *
 *	See the file ``COPYRIGHT'' for the usage and distribution
 *	license and warranty disclaimer.
 */

/*
 * This has been derived from 4.3BSD malloc/free.
 * Note that the same semantics as traditional malloc apply -- i.e., memory
 * once allocated from the system doesn't get freed until the process exits
 * (it is reused in the bucket list).
 * Also note, that if we use SVIPC shmem segments, the blocksize and number
 * of shared memory segments can be very limited, according to SHMMAX and
 * SHMSEG.  Thus, SVIPC segments are only a kludge[1] for a general shared
 * memory allocator like this one and make it much more complicated than
 * it needs to be.  To work around the problem of very few segments possible,
 * we try to allocate larger blocks and partition them, anyways it's not nice.
 * Use shared memory sparely and with caution.
 *
 * [1] ``pain in the arse'' is more appropriate.
 */

#ifndef lint
static char rcsid[] = "$Id: shm.c,v 1.18 1998/02/02 03:41:29 mkb Exp $";
#endif

#include "tirc.h"

#ifdef	HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef	HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#ifdef	HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef	HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef	HAVE_STRING_H
#include <string.h>
#elif defined(HAVE_MEMORY_H)
#include <memory.h>
#endif
#ifdef	HAVE_STRINGS_H
#include <strings.h>
#endif

#ifdef	HAVE_SVIPC_SHM
#ifdef	HAVE_SYS_IPC_H
#include <sys/ipc.h>
#endif
#ifdef	HAVE_SYS_SHM_H
#include <sys/shm.h>
#endif
#endif	/* HAVE_SVIPC_SHM */

#include "compat.h"
#include "colour.h"

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define	MAP_ANONYMOUS	MAP_ANON
#endif

/*
 * The overhead of a block is at least 4 bytes.  When free, this space
 * contains a pointer to the next free block, and the bottom two bits
 * must be zero.  When in use, the first byte is set to MAGIC, and the
 * second byte is the size index.  The remaining bytes are for alignment.
 * The order of elements is critical: ov_magic must overlay the low
 * order bits of ov_next, and ov_magic can not be a valid ov_next bit
 * pattern.
 */
union overhead {
	union overhead	*ov_next;	/* when free */
	struct {
		unsigned char	ovu_magic;	/* magic number */
		unsigned char	ovu_index;	/* bucket # */
	} ovu;
};

#define	ov_magic	ovu.ovu_magic
#define ov_index	ovu.ovu_index

#define	MAGIC	0xef	/* magic # on accounting info */

extern int	errno;
extern char	*myname;	/* in main.c */

static void	morecore __P((int));
static int	findbucket __P((union overhead *, int));
#ifdef	HAVE_SVIPC_SHM
static VOIDPTR	get_svipc_shmem __P((size_t));
static void	rm_all_shmem __P((void));
#endif

/*
 * nextf[i] is the pointer to the next free block of size 2^(i+3).
 * The smallest allocatable block is 8 bytes.  The overhead information
 * precedes the data area returned to the user.
 */
#define	NBUCKETS	30
static union overhead *nextf[NBUCKETS];
static unsigned int pagesz;	/* page size */
static int	pagebucket;	/* page size bucket */

#ifdef	HAVE_SVIPC_SHM

/* Maximum is 256 or SHMMIN shmem segments */
#if SHMSEG > 256
#define IDASIZE 256
#else
#define IDASIZE SHMSEG
#endif
static int	idarray[IDASIZE];	/* stores shmem identifiers */
static int	idaseq;		/* number of last used id in idarray */
static unsigned int segsize;	/* shmem segment size used (increased */
				/* progressively */
#endif	/* HAVE_SVIPC_SHM */

VOIDPTR
Shmalloc(nbytes)
	size_t nbytes;
{
	register union overhead *op;
	register int bucket;
	register unsigned amt, n;

#if !defined(HAVE_ANONSHRD_MMAP) && !defined(HAVE_DEVZERO_MMAP) && \
    !defined(HAVE_SVIPC_SHM)
	return malloc(nbytes);	/* not shared if no working mmap or svipc */
#endif
	/*
	 * First time malloc is called, setup page size.
	 */
	if (pagesz == 0) {
		pagesz = Getpagesize();
		bucket = 0;
		amt = 8;
		while (pagesz > amt) {
			amt <<= 1;
			bucket++;
		}
		pagebucket = bucket;
#ifdef	HAVE_SVIPC_SHM
		Atexit(rm_all_shmem);
#endif
	}
#ifdef	HAVE_SVIPC_SHM
	segsize = pagesz;
#endif

	/*
	 * Convert amount of memory requested into closest block size
	 * stored in hash buckets which satisfies request.
	 */
	if (nbytes <= (n = pagesz - sizeof (*op))) {
		amt = 16;	/* size of first bucket */
		bucket = 1;
		n = -(sizeof (*op));
	}
	else {
		amt = pagesz;
		bucket = pagebucket;
	}
	while (nbytes > amt + n) {
		amt <<= 1;
		if (amt == 0)
			return (NULL);
		bucket++;
	}

	/*
	 * If nothing in hash bucket, request more memory from the system.
	 */
	if ((op = nextf[bucket]) == NULL) {
		morecore(bucket);
		if ((op = nextf[bucket]) == NULL)
			return (NULL);
	}
	/* remove from linked list */
	nextf[bucket] = op->ov_next;
	op->ov_magic = MAGIC;
	op->ov_index = bucket;
	return ((char *)(op + 1));
}

/*
 * Allocate more memory to the indicated bucket.
 */
static void
morecore(bucket)
	int bucket;
{
	register union overhead *op;
	register int sz;		/* size of desired block */
	unsigned int amt;		/* amount to allocate */
	unsigned int nblks;		/* how many blocks we get */

	sz = 1 << (bucket + 3);
	if (sz <= 0)
		return;

#if defined(HAVE_ANONSHRD_MMAP) || defined(HAVE_DEVZERO_MMAP)
	if ((unsigned)sz < pagesz) {
		amt = pagesz;
		nblks = amt / (unsigned)sz;
	}
	else {
		amt = (unsigned)sz + pagesz;
		nblks = 1;
	}
#else
	if ((unsigned)sz < segsize) {
		amt = segsize;
		nblks = amt / segsize;
	}
	else if ((unsigned)sz + segsize < SHMMAX) {
		amt = (unsigned)sz + segsize;
		nblks = 1;
	}
	else 
		return;		/* out of mem */

	if (segsize * 2 < SHMMAX)
		segsize *= 2;
#endif

#ifdef	HAVE_ANONSHRD_MMAP
	/* BSD-style things */
	op = (union overhead *) mmap(0, amt, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_SHARED, -1, (unsigned)0);
#elif defined(HAVE_DEVZERO_MMAP)
	/* SVR4 */
	{
	int zerofd;

	if ((zerofd = open("/dev/zero", O_RDWR, 0600)) < 0) {
		fprintf(stderr, "%s: cannot open /dev/zero: %s\n", myname,
			Strerror(errno));
		return;
	}
	op = (union overhead *) mmap(0, amt, PROT_READ | PROT_WRITE,
		MAP_SHARED, zerofd, (unsigned)0);
	close(zerofd);
	}
#elif defined (HAVE_SVIPC_SHM)
	op = (union overhead *) get_svipc_shmem(amt);
#endif

	/* no more room! */
	if ((int)op == -1)
		return;
	/*
	 * Add new memory allocated to that on free list for this bucket.
	 */
	nextf[bucket] = op;
	while (--nblks > 0) {
		op->ov_next = (union overhead *)((caddr_t)op + (unsigned)sz);
		op = (union overhead *)((caddr_t)op + (unsigned)sz);
	}
}

void
Shfree(cp)
	VOIDPTR cp;
{
	register int size;
	register union overhead *op;

#if !defined(HAVE_ANONSHRD_MMAP) && !defined(HAVE_DEVZERO_MMAP) && \
    !defined(HAVE_SVIPC_SHM)
	free(cp);
#endif
	if (cp == NULL)
		return;
	op = (union overhead *)((caddr_t)cp - sizeof (union overhead));
	if (op->ov_magic != MAGIC)
		return;			/* sanity */
	size = op->ov_index;
	op->ov_next = nextf[size];	/* also clobbers ov_magic */
	nextf[size] = op;
}

/*
 * When a program attempts "storage compaction" as mentioned in the
 * old malloc man page, it realloc's an already freed block.  Usually
 * this is the last block it freed; occasionally it might be farther
 * back.  We have to search all the free lists for the block in order
 * to determine the bucket: 1st we make one pass thru the lists checking
 * only the first block in each; if that fails we search ``realloc_srchlen''
 * blocks in each list for a match (the variable is extern so the caller
 * can modify it).  If that fails we just copy however many bytes was
 * given to realloc() and hope it's not huge.
 */
int	realloc_srchlen = 4;	/* 4 should be plenty, -1 =>'s whole list */

VOIDPTR
Shrealloc(cp, nbytes)
	VOIDPTR cp;
	size_t nbytes;
{
	register unsigned int onb;
	register int i;
	union overhead *op;
	char *res;
	int was_alloced = 0;

	if (cp == NULL)
		return (Shmalloc(nbytes));
	op = (union overhead *)((caddr_t)cp - sizeof (union overhead));
	if (op->ov_magic == MAGIC) {
		was_alloced++;
		i = op->ov_index;
	}
	/*
	 * Already free, doing "compaction".  Search for the old block of
	 * memory on the free list.  First, check the most common case
	 * (last element free'd), then (this failing) the last
	 * ``realloc_srchlen'' items free'd.  If all lookups fail, then
	 * assume the size of the memory block being realloc'd is the
	 * largest possible (so that all "nbytes" of new memory are copied
	 * into).  Note that this could cause a memory fault if the old
	 * area was tiny, and the moon is gibbeous.  However, that is very
	 * unlikely.
	 */
	else {
		if ((i = findbucket(op, 1)) < 0 && (i = findbucket(op,
		    realloc_srchlen)) < 0)
			i = NBUCKETS;
	}
	onb = 1 << (i + 3);
	if (onb < pagesz)
		onb -= sizeof (*op);
	else
		onb += pagesz - sizeof (*op);
	/* avoid the copy if same size block */
	if (was_alloced) {
		if (i) {
			i = 1 << (i + 2);
			if ((unsigned)i < pagesz)
				i -= sizeof (*op);
			else
				i += pagesz - sizeof (*op);
		}
		if (nbytes <= onb && nbytes > (unsigned)i) {
			return (cp);
		}
		else
			free(cp);
	}
	if ((res = Shmalloc(nbytes)) == NULL)
		return (NULL);
	if (cp != res)	/* common optimization if "compacting" */
		memmove(cp, res, (nbytes < onb) ? nbytes : onb);
	return (res);
}

/*
 * Search ``srchlen'' elements of each free list for a block whose
 * header starts at ``freep''.  If srchlen is -1 search the whole list.
 * Return bucket number, or -1 if not found.
 */
static int
findbucket(freep, srchlen)
	union overhead *freep;
	int srchlen;
{
	register union overhead *p;
	register int i, j;

	for (i = 0; i < NBUCKETS; i++) {
		j = 0;
		for (p = nextf[i]; p && j != srchlen; p = p->ov_next) {
			if (p == freep)
				return (i);
			j++;
		}
	}
	return (-1);
}

/*
 * Zero-Fill Shmalloc().  This is called like malloc(), not like calloc().
 */
VOIDPTR
Shcalloc(nbytes)
	size_t nbytes;
{
	void *p = Shmalloc(nbytes);

	if (p != NULL)
		bzero(p, nbytes);
	return p;
}

#ifdef	HAVE_SVIPC_SHM

/*
 * This crufts a block of shared memory out of the System V shmem mechanism.
 */
static VOIDPTR
get_svipc_shmem(size)
	size_t size;
{
	int shmid;
	VOIDPTR shmptr;

	if ((shmid = shmget(IPC_PRIVATE, size, (SHM_R | SHM_W))) < 0 ||
	    (shmptr = shmat(shmid, 0, 0)) == (VOIDPTR) -1)
		return NULL;

	idarray[idaseq++] = shmid;

	return shmptr;
}

/*
 * This should be called when TIRC exits.
 */
static void
rm_all_shmem()
{
	int i;

	for (i = 0; i < idaseq; i++)
		shmctl(idarray[i], IPC_RMID, 0);
}

#endif	/* HAVE_SVIPC_SHM */

