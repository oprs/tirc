/*-
 * tirc -- client for the Internet Relay Chat
 *
 *	Copyright (c) 1996, 1997, 1998
 *		Matthias Buelow.  All rights reserved.
 *
 *	See the file ``COPYRIGHT'' for the usage and distribution
 *	license and warranty disclaimer.
 *
 * $Id: hook.h,v 1.5 1998/01/20 01:16:22 token Exp $
 */

#ifndef TIRC_HOOK_H
#define TIRC_HOOK_H	1

/* Definition of a function pointer for use with the hooks. */
typedef	void (*hookfunptr)();

struct hook_entry {
	hookfunptr hook_fun;
	LIST_ENTRY(hook_entry) hook_entries;
};


/* Definition of a function call hook. */
typedef	LIST_HEAD(, hook_entry) defhook;

/* hook utilities */
void	create_hook __P((defhook *));
void	add_to_hook __P((defhook *, hookfunptr));
int	del_from_hook __P((defhook *, hookfunptr));
int	run_hooks __P((defhook *));

#endif	/* ! TIRC_HOOK_H */

