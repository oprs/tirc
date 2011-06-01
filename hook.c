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
static char rcsid[] = "$Id: hook.c,v 1.5 1998/02/02 03:41:29 mkb Exp $";
#endif

#include "tirc.h"
#include "hook.h"

/* Initialize a function hook defined with defhook. */
void
create_hook(hp)
	defhook *hp;
{
	assert(hp != NULL);
	LIST_INIT(hp);
}

/* Add a function to a hook. */
void
add_to_hook(hp, fun)
	defhook *hp;
	hookfunptr fun;
{
	struct hook_entry *h = chkmem(malloc(sizeof *h));

	assert(hp != NULL);
	assert(fun != NULL);
	h->hook_fun = fun;
	LIST_INSERT_HEAD(hp, h, hook_entries);
}

/*
 * Remove a function from a hook.  Returns 0 if successful, -1 otherwise
 * (function was not found on the hook specified).
 */
int
del_from_hook(hp, fun)
	defhook *hp;
	hookfunptr fun;
{
	struct hook_entry *hit;

	assert(hp != NULL);
	assert(fun != NULL);

	for (hit = hp->lh_first; hit != NULL && hit->hook_fun != fun;
	    hit = hit->hook_entries.le_next)
		;
	if (hit == NULL)
		return -1;
	LIST_REMOVE(hit, hook_entries);
	free(hit);
	return 0;
}

/*
 * Run the functions on the hook.  Returns the number of functions that
 * were run.
 */
int
run_hooks(hp)
	defhook *hp;
{
	struct hook_entry *he;
	int n;

	for (he = hp->lh_first, n = 0; he != NULL;
	    he = he->hook_entries.le_next, n++)
		(*he->hook_fun)();
	return n;
}

