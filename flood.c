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
static char rcsid[] = "$Id: flood.c,v 1.11 1998/02/02 03:41:29 mkb Exp $";
#endif

#include "tirc.h"

#ifdef	HAVE_STRING_H
#include <string.h>
#elif defined(HAVE_MEMORY_H)
#include <memory.h>
#endif

#include "compat.h"
#include "colour.h"

static struct f_cache *flnextentry __P((void));

extern char	ppre[], nick[];

static struct f_cache {
	int	fc_used;
	long	fc_loginhash;
	long	fc_machinehash;
	long	fc_contentshash;
	long	fc_time;
	char	*fc_source;
} msgring[FLRINGSIZE];

static int	flptr;

/*
 * Initialize the flood protection.
 */
void
flinit()
{
	int i;

	for (i = 0; i < FLRINGSIZE; i++) {
		if (msgring[i].fc_used && msgring[i].fc_source != NULL)
			free(msgring[i].fc_source);
		msgring[i].fc_used = 0;
		msgring[i].fc_source = NULL;
	}
	flptr = 0;
}

/*
 * Register a message to the buffer ring of last recently received
 * messages.  Don't register messages that come from the server
 * directly, only those from users.  You must specify which server
 * message parameter counts as the contents of the message.
 * After that, calculate statistics about the messages gathered so
 * far and act if flooding has been detected.
 * flregister returns 1 if the message should be ignored.
 */
int
flregister(sm, contents, type)
	struct servmsg *sm;
	int contents, type;
{
	struct f_cache *fc;
	char *excl, *at;
	int i, strict = 0;
	int acnt = 0, tcnt = 0, ccnt = 0;
	register long x, y;
	struct timeval tv;
	char nname[NICKSZ+1], imask[MSGSZ];
	char ftype[32];
	static long saved_time;
	long now;

	if (!check_conf(CONF_FLOODP))
		return 0;

	gettimeofday(&tv, NULL);
	now = tv.tv_sec;
	/*
	 * If for longer than 15 seconds we have received nothing, clear the
	 * message ring.
	 */
	if (now-saved_time > 15)
		flinit();
	saved_time = now;

	from_nick(sm, nname);
	if (!irc_strcmp(nname, nick))
		return 0;

	if (sm->sm_prefix == NULL ||
	    (excl = strchr(sm->sm_prefix, '!')) == NULL ||
	    (at = strchr(sm->sm_prefix, '@')) == NULL)
		return 0;
	
	fc = flnextentry();
	fc->fc_source = chkmem(Strdup(sm->sm_prefix));
	fc->fc_loginhash = elf_hash(excl+1);
	fc->fc_machinehash = elf_hash(at+1);
	fc->fc_contentshash = elf_hash(sm->sm_par[contents]); 
	fc->fc_time = now;
	fc->fc_used = 1;

	/*
	 * Now iterate through the ring and accumulate statistics.
	 */
	if (check_conf(CONF_FLSTRICT)) {
		strict = 1;
		y = fc->fc_machinehash;
	}
	else
		y = fc->fc_loginhash;

	for (i = 0; i < FLRINGSIZE; i++) {
		if (msgring[i].fc_used) {
			x = (strict) ? msgring[i].fc_machinehash
				: msgring[i].fc_loginhash;
			if (x == y) {
				acnt++;
				/* More than one line / 2 sec? */
				if (labs(msgring[i].fc_time - fc->fc_time) <=
					FLRINGSIZE)
					tcnt++;
				if (msgring[i].fc_contentshash ==
					fc->fc_contentshash)
					ccnt++;
			}
		}
	}

	if (acnt >= FLRINGSIZE/2) {
		/*
		 * Flooding detcted.
		 */
		if (tcnt >= FLRINGSIZE/2 || ccnt >= FLRINGSIZE/2) {
			switch (type) {
			case FL_PUBLIC:
				strcpy(ftype, "Public");
				break;
			case FL_MSG:
				strcpy(ftype, "Msg");
				break;
			case FL_NOTICE:
				strcpy(ftype, "Notice");
				break;
			case FL_INVITE:
				strcpy(ftype, "Invite");
				break;
			default:
				ftype[0] = 0;
				break;
			}

			iw_printf(COLI_WARN, "%s%s flooding detected from %s\n",
				ppre, ftype, fc->fc_source);
			/*
			 * Remove the abuser's occurence from the ring
			 * to avoid constant warnings.
			 */
			x = (strict) ? fc->fc_machinehash : fc->fc_loginhash;
				
			for (i = 0; i < FLRINGSIZE; i++) {
				x = (strict) ? msgring[i].fc_machinehash
					: msgring[i].fc_loginhash;

				if (msgring[i].fc_used && x == y) {
					msgring[i].fc_used = 0;
					if (msgring[i].fc_source != NULL)
						free(msgring[i].fc_source);
				}
			}

			/* Ignore offending source when configured. */
			if (check_conf(CONF_FLIGNORE)) {
				if (strict)
					sprintf(imask, "*!*%s", at);
				else
					sprintf(imask, "*%s", excl);
				add_ignore(imask, 3);
				return 1;
			}
		}
	}
	return 0;
}

static struct f_cache *
flnextentry()
{
	flptr = (flptr+1) % FLRINGSIZE;

	if (msgring[flptr].fc_used && msgring[flptr].fc_source != NULL) {
		free(msgring[flptr].fc_source);
		msgring[flptr].fc_source = NULL;
	}
	msgring[flptr].fc_used = 0;
	return &msgring[flptr];
}

