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
 * This is a very simple example program for demonstrating the dynamically
 * linkable modules interface.
 */

#ifndef	lint
static char rcsid[] = "$Id$";
#endif

#include <dlapi.h>		/* TIRC API definitions */

#include <stdio.h>

#ifdef	NEED_DYNAMIC_SYMBOL
volatile int	_DYNAMIC;	/* required for BSD a.out shared obj */
#endif

static struct tirc_sstat_info si;	/* will hold various strings queried */
					/* from the client */

/*
 * This function is required; it returns a short identifier string to
 * the invoking module support.  It also sets up some stuff.
 */
const char *
modid()
{
	static const char idstring[] = "example program to show the usage of \
dynamically loadable modules";

	tirc_get_strings(&si);

	return idstring;
}

/*
 * This is the function that is to be bound to a dynamic command in tirc.
 */
void
hello_world()
{
	iw_printf(COLI_TEXT, "%sHello, world!\nRunning on version \"%s\", \
host system type is \"%s\"\n", si.si_ppre, si.si_version, si.si_osstring);
}

