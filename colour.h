/*-
 * tirc -- client for the Internet Relay Chat
 *
 *	Copyright (c) 1996, 1997, 1998
 *		Matthias Buelow.  All rights reserved.
 *
 *	See the file ``COPYRIGHT'' for the usage and distribution
 *	license and warranty disclaimer.
 *
 * $Id: colour.h,v 1.7 1998/02/11 01:07:22 token Exp $
 */

#ifndef TIRC_COLOUR_H
#define TIRC_COLOUR_H	1

#ifdef	WITH_ANSI_COLOURS

void	setup_colour __P((void));
int	get_fg_colour_for __P((int));
int	get_bg_colour_for __P((int));
void	set_fg_colour_for __P((int, int));
void	set_bg_colour_for __P((int, int));
char	*set_colour __P((int, int));
char	*colour_off __P((void));
void	colour_set __P((char *, char *, char *));

/* ANSI text colour order. */

#define	COLOUR_BLACK		0
#define	COLOUR_RED		1
#define	COLOUR_GREEN		2
#define	COLOUR_YELLOW		3
#define	COLOUR_BLUE		4
#define	COLOUR_MAGENTA		5
#define	COLOUR_CYAN		6
#define	COLOUR_WHITE		7
#define	COLOUR_BLACK_BRIGHT	8
#define	COLOUR_RED_BRIGHT	9
#define	COLOUR_GREEN_BRIGHT	10
#define	COLOUR_YELLOW_BRIGHT	11
#define	COLOUR_BLUE_BRIGHT	12
#define	COLOUR_MAGENTA_BRIGHT	13
#define	COLOUR_CYAN_BRIGHT	14
#define	COLOUR_WHITE_BRIGHT	15
#define	COLOUR_TRANSPARENT	16

#define	MAX_COLOURS		17

#endif	/* WITH_ANSI_COLOURS */

/* Possible colour indices. */

#define	COLI_TEXT		0	/* normal text */
#define	COLI_FOC_STATUS		1	/* focussed status bar */
#define	COLI_NFOC_STATUS	2	/* unfocussed status bar */
#define	COLI_PROMPT		3	/* editor line prompt */
#define	COLI_SERVMSG		4	/* server message */
#define	COLI_PRIVMSG		5	/* private message */
#define	COLI_CHANMSG		6	/* privmsg to channel */
#define	COLI_OWNMSG		7	/* own messages */
#define	COLI_DCCMSG		8	/* dcc message colour */
#define	COLI_WARN		9	/* error and warning messages */
#define	COLI_ACTION		10	/* action messages */
#define COLI_BOLDNICK		11	/* bold nickname */
#define	COLI_HELP		12	/* help text */

#define	DISTINCT_COLOURS	13

#endif	/* !TIRC_COLOUR_H */

