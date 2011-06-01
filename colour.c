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
static char rcsid[] = "$Id: colour.c,v 1.11 1998/02/17 09:25:51 mkb Exp $";
#endif

#include "tirc.h"

#ifdef	HAVE_STRING_H
#include <string.h>
#endif

#include "colour.h"

#ifdef	WITH_ANSI_COLOURS

extern char ppre[];

static int	cfg[DISTINCT_COLOURS], cbg[DISTINCT_COLOURS];
static char	*colnames[MAX_COLOURS] = {
	"black", "red", "green", "yellow", "blue", "magenta", "cyan", "white",
	"brightblack", "brightred", "brightgreen", "brightyellow",
	"brightblue", "brightmagenta", "brightcyan", "brightwhite", "transparent" };

/* XXX must be the same order as the constants in colour.h! */
static char	*coltypes[DISTINCT_COLOURS] = {
	"text", "foc_status", "nfoc_status", "prompt", "servmsg",
	"privmsg", "chanmsg", "ownmsg", "dccmsg", "warn", "action",
	"boldnick", "help" };

/*
 * Setup colourization.
 */
void
setup_colour()
{
	/* Start with reasonable colours */

	cfg[COLI_TEXT] = COLOUR_WHITE;
	cbg[COLI_TEXT] = COLOUR_BLACK;
	cfg[COLI_FOC_STATUS] = COLOUR_BLACK;
	cbg[COLI_FOC_STATUS] = COLOUR_GREEN;
	cfg[COLI_NFOC_STATUS] = COLOUR_GREEN;
	cbg[COLI_NFOC_STATUS] = COLOUR_BLUE;
	cfg[COLI_PROMPT] = COLOUR_GREEN;
	cbg[COLI_PROMPT] = COLOUR_BLACK;
	cfg[COLI_SERVMSG] = COLOUR_CYAN;
	cbg[COLI_SERVMSG] = COLOUR_BLACK;
	cfg[COLI_PRIVMSG] = COLOUR_WHITE;
	cbg[COLI_PRIVMSG] = COLOUR_BLUE;
	cfg[COLI_CHANMSG] = COLOUR_WHITE;
	cbg[COLI_CHANMSG] = COLOUR_BLACK;
	cfg[COLI_OWNMSG] = COLOUR_YELLOW;
	cbg[COLI_OWNMSG] = COLOUR_BLACK;
	cfg[COLI_DCCMSG] = COLOUR_BLACK;
	cbg[COLI_DCCMSG] = COLOUR_CYAN;
	cfg[COLI_WARN] = COLOUR_RED_BRIGHT;
	cbg[COLI_WARN] = COLOUR_BLACK;
	cfg[COLI_ACTION] = COLOUR_MAGENTA_BRIGHT;
	cbg[COLI_ACTION] = COLOUR_BLACK;
	cfg[COLI_BOLDNICK] = COLOUR_WHITE_BRIGHT;
	cbg[COLI_BOLDNICK] = COLOUR_BLACK;
	cfg[COLI_HELP] = COLOUR_MAGENTA;
	cbg[COLI_HELP] = COLOUR_BLACK;
}

/*
 * get_fg_colour_for and get_bg_colour_for return the current colour
 * value for the desired message type.
 */
int
get_fg_colour_for(msgtype)
	int msgtype;
{
	return cfg[msgtype];
}

int
get_bg_colour_for(msgtype)
	int msgtype;
{
	return cbg[msgtype];
}

/*
 * Returns a pointer to a string which sets the tty's foreground colour
 * to the requested value.
 */
char *
set_colour(foreground, background)
	int foreground, background;
{
	static char attrs[32];

	sprintf(attrs, "\033[%d;%dm", foreground >> 3, (foreground & 7)+30);

	if (background != COLOUR_TRANSPARENT)
	   sprintf(&attrs[strlen(attrs)-1], ";%dm", (background & 7)+40);

	return attrs;
}

/*
 * Return to default attribute.
 */
char *
colour_off()
{
	static char nocol[] = "\033[0m";

	return nocol;
}

/*
 * Parses a user colour set command.
 */
void
colour_set(cn, fgcv, bgcv)
	char *cn, *fgcv, *bgcv;
{
	int ty, fg, bg;

	for (ty = 0; ty < DISTINCT_COLOURS; ty++)
		if (!strcmp(coltypes[ty], cn))
			break;
	if (ty == DISTINCT_COLOURS) {
		setlog(0);
		iw_printf(COLI_TEXT, "%sInvalid colour type: %s\n", ppre, cn);
		setlog(1);
		return;
	}

	if (colour_atoi(fgcv, bgcv, &fg, &bg) == -1)
		return;

	cfg[ty] = fg;
	cbg[ty] = bg;

	setlog(0);
	iw_printf(COLI_TEXT, "%sColour applied\n", ppre);
	setlog(1);
}

/*
 * Sets fg and bg colour values for colour strings.
 * Returns 0 on success, -1 on failure.
 */
int
colour_atoi(fgs, bgs, fgc, bgc)
	char *fgs, *bgs;
	int *fgc, *bgc;
{
	int fg, bg;

	for (fg = 0; fg < MAX_COLOURS; fg++)
		if (!strcmp(colnames[fg], fgs))
			break;
	if (fg == MAX_COLOURS) {
		iw_printf(COLI_TEXT, "%sInvalid foreground colour name: %s\n",
			ppre, fgs);
		return -1;
	}

	for (bg = 0; bg < MAX_COLOURS; bg++)
		if (!strcmp(colnames[bg], bgs))
			break;
	if (bg == MAX_COLOURS) {
		iw_printf(COLI_TEXT, "%sInvalid background colour name: %s\n",
			ppre, bgs);
		return -1;
	}
	*fgc = fg;
	*bgc = bg;
	return 0;
}

/*
 * Returns a string containing the name for the colour index specified in
 * the 'which parameter.
 */
const char *
getcolourname(which)
	int which;
{
	static char invcol[] = "invalid";

	if (which < 0 || which >= MAX_COLOURS)
		return invcol;
	return (const char *)colnames[which];
}

void
set_fg_colour_for(msgtype, val)
	int msgtype, val;
{
	cfg[msgtype] = val;
}

void
set_bg_colour_for(msgtype, val)
	int msgtype, val;
{
	cbg[msgtype] = val;
}

#endif	/* WITH_ANSI_COLOURS */

