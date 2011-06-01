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
static char rcsid[] = "$Id: odlg.c,v 1.8 1998/02/22 19:04:49 mkb Exp $";
#endif

#include "tirc.h"

#ifdef	HAVE_STRING_H
#include <string.h>
#endif

#include "tty.h"
#include "colour.h"

static void	odlg_movebar __P((int, int));
static void	odlg_save_tircrc __P((void));
static void	odlg_update_options __P((void));
#ifdef	WITH_ANSI_COLOURS
static void	odlg_update_colours __P((void));
#endif

static struct options {
	char	*shortname;
	char	*description;
	unsigned bitmask;
	int	onoff;
} options[] = {
	{ "CTCP", "Client-to-client-protocol", CONF_CTCP, 0 },
	{ "STAMP", "Timestamp messages, join/parts, etc.", CONF_STAMP, 0 },
	{ "FLOODP", "Flood protection", CONF_FLOODP, 0 },
	{ "FLSTRICT", "Stricter flood protection", CONF_FLSTRICT, 0 },
	{ "FLIGNORE", "Ignore culprit on flooding", CONF_FLIGNORE, 0 },
	{ "KBIGNORE", "Ignore user when kickbanning", CONF_KBIGNORE, 0 },
	{ "BEEP", "Enable bell in messages", CONF_BEEP, 0 },
	{ "HIMSG", "Highlight message sender", CONF_HIMSG, 0 },
	{ "CLOCK", "Display clock in status line", CONF_CLOCK, 0 },
	{ "BIFF", "Notify about new local e-mail", CONF_BIFF, 0 },
	{ "XTERMT", "Reflect status in xterm title", CONF_XTERMT, 0 },
	{ "PGUPDATE", "Monitor changes on other pages", CONF_PGUPDATE, 0 },
	{ "OOD", "Enable the Op-On-Demand facility", CONF_OOD, 0 },
	{ "ATTRIBS", "ircII-style inline text attributes", CONF_ATTRIBS, 0 },
	{ "COLOUR", "ANSI char cell colourization", CONF_COLOUR, 0 },
	{ "STIPPLE", "Dash-on-off line in status lines", CONF_STIPPLE, 0 },
	{ "HIDEMCOL", "Filter out mIRC colour crap", CONF_HIDEMCOL, 0 },
	{ "NCOLOUR", "Nickname colourization", CONF_NCOLOUR, 0 },
	{ "NUMERICS", "Display IRC protocol numerics", CONF_NUMERICS, 0 },
	{ "TELEVIDEO", "Disable attribs on my old tvi9050", CONF_TELEVIDEO, 0 },
	{ "", "", 0, 0 }	/* delimiter */
};

static int	num_options = sizeof options / sizeof options[0];

#ifdef	WITH_ANSI_COLOURS
static struct colours {
	char	*shortname;
	char	*description;
	int	colno;
	int	fgcol;
	int	bgcol;
} colours[] = {
	{ "text", "Default text colour", COLI_TEXT, 0, 0 },
	{ "foc_status", "Focussed status bar", COLI_FOC_STATUS, 0, 0 },
	{ "nfoc_status", "Unfocussed status bar", COLI_NFOC_STATUS, 0, 0 },
	{ "prompt", "Editor line prompt", COLI_PROMPT, 0, 0 },
	{ "servmsg", "Server messages", COLI_SERVMSG, 0, 0 },
	{ "privmsg", "Private msgs and notices", COLI_PRIVMSG, 0, 0 },
	{ "chanmsg", "Privmsgs to channels", COLI_CHANMSG, 0, 0 },
	{ "ownmsg", "Private messages from us", COLI_OWNMSG, 0, 0 },
	{ "dccmsg", "DCC CHAT messages", COLI_DCCMSG, 0, 0 },
	{ "warn", "Warnings and errors", COLI_WARN, 0, 0 },
	{ "action", "CTCP ACTION messages", COLI_ACTION, 0, 0 },
	{ "boldnick", "Bold nickname colour", COLI_BOLDNICK, 0, 0 },
	{ "help", "Help text", COLI_HELP, 0, 0 }
};

static int	hfocus;
static int	num_colours = sizeof colours / sizeof colours[0];
#else
static int	num_colours = -1;
#endif	/* ! WITH_ANSI_COLOURS */

static int	focus;
static int	dstart;
static int	in_odlg;
static int	need_llclear;

void
options_dialog()
{
	int i;

	focus = dstart = need_llclear = 0;

	for (i = 0; i< num_options-1; i++)
		options[i].onoff = check_conf(options[i].bitmask);

#ifdef	WITH_ANSI_COLOURS
	for (i = 0; i < num_colours; i++) {
		colours[i].fgcol = get_fg_colour_for(colours[i].colno);
		colours[i].bgcol = get_bg_colour_for(colours[i].colno);
	}
#endif
 
	odlg_printscreen();
	en_disp(0);
	in_odlg = 1;
}

void
odlg_printscreen()
{
	int i;
#ifdef	WITH_ANSI_COLOURS
	int deffgc, defbgc;
#endif

	tty_addbuff(tc_clear);
	tty_gotoxy(t_columns - 16, 0);
	tty_puts("OPTIONS EDITOR\n\n");
	tty_puts(
"Select with hjkl or arrow keys, space/enter = toggle, +/- = change colour,\n\
x = exit, > = save to ~/.tircrc.\n");
	tty_gotoxy(0, 5);

	dstart = (focus / (t_lines-6)) * (t_lines-6);
		
#ifdef	WITH_ANSI_COLOURS
	deffgc = get_fg_colour_for(COLI_TEXT);
	defbgc = get_bg_colour_for(COLI_TEXT);
#endif

	for (i = dstart; i < num_options + num_colours
	    && i < dstart+t_lines-6; i++)
		if (i == num_options - 1)
			tty_printf(NULL, "%s\n", tc_noattr);
		else if (i < num_options)
#ifdef	WITH_ANSI_COLOURS
			tty_printf(NULL, "%d,%d; %-40s (%-10s):  %-3s %s\n",
				deffgc, defbgc,
				options[i].description,
				options[i].shortname,
				options[i].onoff ? "ON" : "OFF",
				tc_clreol);
#else
			tty_printf(NULL, " %-40s (%-10s):  %-3s %s\n",
				options[i].description,
				options[i].shortname,
				options[i].onoff ? "ON" : "OFF",
				tc_clreol);
#endif	/* ! WITH_ANSI_COLOURS */

#ifdef	WITH_ANSI_COLOURS
		else
			tty_printf(NULL,
			" %d,%d;%-25s%d,%d; (%-11s):  %-13s on %-13s %s\n",
				colours[i-num_options].fgcol,
				colours[i-num_options].bgcol,
				colours[i-num_options].description,
				deffgc, defbgc,
				colours[i-num_options].shortname,
				getcolourname(colours[i-num_options].fgcol),
				getcolourname(colours[i-num_options].bgcol),
				tc_clreol);
#endif	/* WITH_ANSI_COLOURS */

	odlg_movebar(-1, focus);
}

int
is_in_odlg()
{
	return in_odlg;
}

void
odlg_parsekey()
{
	int c;
	int oldfoc;

	c = tty_getch();

	if (need_llclear) {
		tty_gotoxy(0, t_lines-1);
		tty_addbuff(tc_noattr);
		tty_addbuff(tc_clreol);
		tty_flush();
		need_llclear = 0;
	}

	switch (c) {
	case K_UP:
	case K_S_UP:
	case K_S2_UP:
	case 'k':
		oldfoc = focus;
		if (focus > 0)
			focus--;
		if (focus == num_options-1)
			focus--;
		odlg_movebar(oldfoc, focus);
		break;
	case K_DN:
	case K_S_DN:
	case K_S2_DN:
	case 'j':
	case '\t':
		oldfoc = focus;
		if (focus < num_options + num_colours-1)
			focus++;
		if (focus == num_options-1)
			focus++;
		odlg_movebar(oldfoc, focus);
		break;
	case '0':
		oldfoc = focus;
		focus = 0;
		odlg_movebar(oldfoc, focus);
		break;
	case '$':
		oldfoc = focus;
		focus = num_options+num_colours-1;
		odlg_movebar(oldfoc, focus);
		break;
#ifdef	WITH_ANSI_COLOURS
	case K_LEFT:
	case K_S_LEFT:
	case K_S2_LEFT:
	case K_RIGHT:
	case K_S_RIGHT:
	case K_S2_RIGHT:
	case 'h':
	case 'l':
		if (focus >= num_options) {
			hfocus ^= 1;
			odlg_movebar(focus, focus);
		}
		break;
#endif
	case 'x':
	case 'X':
	case 'q':
	case 'Q':
		odlg_update_options();
#ifdef	WITH_ANSI_COLOURS
		odlg_update_colours();
#endif
		in_odlg = 0;
		en_disp(1);
		repinsel();
		break;
	case K_CTRL('R'):
	case K_CTRL('L'):
		odlg_printscreen();
		break;
	case '\r':
	case '\n':
	case ' ':
		if (focus < num_options) {
			options[focus].onoff ^= 1;
			odlg_movebar(-1, focus);
		}
		break;
	case '>':
		odlg_save_tircrc();
		break;
#ifdef	WITH_ANSI_COLOURS
	case '+':
		if (focus < num_options)
			break;
		if (hfocus) {
			if (colours[focus-num_options].bgcol < MAX_COLOURS/2-1)
				colours[focus-num_options].bgcol++;
		}
		else {
			if (colours[focus-num_options].fgcol < MAX_COLOURS-1)
				colours[focus-num_options].fgcol++;
		}
		odlg_movebar(-1, focus);
		break;
	case '-':
		if (focus < num_options)
			break;
		if (hfocus) {
			if (colours[focus-num_options].bgcol > 0)
				colours[focus-num_options].bgcol--;
		}
		else {
			if (colours[focus-num_options].fgcol > 0)
				colours[focus-num_options].fgcol--;
		}
		odlg_movebar(-1, focus);
		break;
#endif
	}
}

static void
odlg_movebar(oldfoc, newfoc)
	int oldfoc, newfoc;
{
	static int recurse_sem;
#ifdef	WITH_ANSI_COLOURS
	int deffgc, defbgc;
#endif

	if (recurse_sem == 0)
		recurse_sem = 1;
	else {
		recurse_sem = 0;
		return;
	}

#ifdef	WITH_ANSI_COLOURS
	deffgc = get_fg_colour_for(COLI_TEXT);
	defbgc = get_bg_colour_for(COLI_TEXT);
#endif
		
	if (oldfoc / (t_lines-6) != newfoc / (t_lines-6))
		odlg_printscreen();
	else
	    if (oldfoc >= 0) {
		tty_gotoxy(0, 5+oldfoc-dstart);
		if (oldfoc < num_options)
#ifdef	WITH_ANSI_COLOURS
			tty_printf(NULL, "%s%d,%d; %-40s (%-10s):  %-3s %s%s",
				tc_noattr,
				deffgc, defbgc,
				options[oldfoc].description,
				options[oldfoc].shortname,
				options[oldfoc].onoff ? "ON" : "OFF",
				tc_noattr, tc_clreol);
#else
			tty_printf(NULL, "%s %-40s (%-10s):  %-3s %s%s",
				tc_noattr,
				options[oldfoc].description,
				options[oldfoc].shortname,
				options[oldfoc].onoff ? "ON" : "OFF",
				tc_noattr, tc_clreol);
#endif	/* ! WITH_ANSI_COLOURS */
#ifdef	WITH_ANSI_COLOURS
		else {
			oldfoc -= num_options;
			tty_printf(NULL,
			" %d,%d;%-25s%d,%d; (%-11s):  %-13s on %-13s %s",
				colours[oldfoc].fgcol,
				colours[oldfoc].bgcol,
				colours[oldfoc].description,
				deffgc, defbgc,
				colours[oldfoc].shortname,
				getcolourname(colours[oldfoc].fgcol),
				getcolourname(colours[oldfoc].bgcol),
				tc_clreol);
		}
#endif
	    }

	tty_gotoxy(0, 5+newfoc-dstart);
	if (newfoc < num_options)
#ifdef	WITH_ANSI_COLOURS
	    tty_printf(NULL, "%d,%d;%s %-40s (%-10s):  %-3s %s%s",
		    deffgc, defbgc,
		    tc_rev,
		    options[newfoc].description,
		    options[newfoc].shortname,
		    options[newfoc].onoff ? "ON" : "OFF",
		    tc_noattr, tc_clreol);
#else
	    tty_printf(NULL, "%s %-40s (%-10s):  %-3s %s%s",
		    tc_rev,
		    options[newfoc].description,
		    options[newfoc].shortname,
		    options[newfoc].onoff ? "ON" : "OFF",
		    tc_noattr, tc_clreol);
#endif	/* ! WITH_ANSI_COLOURS */
#ifdef	WITH_ANSI_COLOURS
	else {
	    newfoc -= num_options;
	    tty_printf(NULL,
	    " %d,%d;%-25s%d,%d; (%-11s):  %s%-13s%s%d,%d; on %s%-13s%s",
		    colours[newfoc].fgcol,
		    colours[newfoc].bgcol,
		    colours[newfoc].description,
		    deffgc, defbgc,
		    colours[newfoc].shortname,
		    !hfocus ? tc_rev : "",
		    getcolourname(colours[newfoc].fgcol),
		    !hfocus ? tc_noattr : "",
		    deffgc, defbgc,
		    hfocus ? tc_rev : "",
		    getcolourname(colours[newfoc].bgcol),
		    hfocus ? tc_noattr : "",
		    tc_clreol);
	    newfoc += num_options;
	    if (!hfocus)
		    tty_gotoxy(42, 5+newfoc-dstart);
	    else
		    tty_gotoxy(59, 5+newfoc-dstart);
	}
#endif	/* WITH_ANSI_COLOURS */

	tty_flush();
	recurse_sem = 0;
}

#ifdef	WITH_ANSI_COLOURS
static void
odlg_update_colours()
{
	int i;

	for (i = 0; i < num_colours; i++) {
		set_fg_colour_for(colours[i].colno, colours[i].fgcol);
		set_bg_colour_for(colours[i].colno, colours[i].bgcol);
	}
}
#endif	/* WITH_ANSI_COLOURS */

static void
odlg_update_options()
{
	int i;
	char sbuf[80];

	for (i = 0; i < num_options-1; i++) {
		sprintf(sbuf, "%s %s", options[i].shortname,
			options[i].onoff ? "ON" : "OFF");
		setcmd(-1, sbuf);
	}
	xterm_settitle();
}

static void
odlg_save_tircrc()
{
	FILE *rcf;
	char *path = exptilde("~/.tircrc");
	char buf[BUFSZ], *filebuf;
	int fill = 0, fbufsize = BUFSZ;
	int skipping = 0;
	static char startrcmark[] =
		"# *** tirc saved entries from options editor ***";
	static char endrcmark[] =
		"# *** end of saved lines ***";
	char slen, elen;
	int i;

	slen = strlen(startrcmark);
	elen = strlen(endrcmark);

	if (path == NULL) {
		tty_gotoxy(0, t_lines-1);
		tty_puts("Error while expanding \"~/.tircrc\"!");
		need_llclear = 1;
		tty_flush();
		return;
	}

	if ((rcf = fopen(path, "r+")) == NULL
	    && (rcf = fopen(path, "w+")) == NULL) {
		tty_gotoxy(0, t_lines-1);
		tty_puts("Could not open/create file!");
		need_llclear = 1;
		tty_flush();
		return;
	}

	filebuf = chkmem(malloc(fbufsize));
	*filebuf = '\0';
	
	while (!feof(rcf)) {
		if ((fgets(buf, BUFSZ, rcf)) == NULL)
			break;
		if (!strncmp(startrcmark, buf, slen))
			skipping = 1;
		if (!skipping) {
			int l;

			if (fill + (l = strlen(buf)) > fbufsize) {
				filebuf = chkmem(realloc(filebuf,
					fbufsize + BUFSZ));
				fbufsize += BUFSZ;
			}
			strcat(filebuf, buf);
			fill += l;
		}
		if (skipping && !strncmp(endrcmark, buf, elen))
			skipping = 0;
	}

	if ((rcf = freopen(path, "w", rcf)) == NULL) {
		free(filebuf);
		tty_gotoxy(0, t_lines-1);
		tty_puts("Could not reopen file for writing!");
		need_llclear = 1;
		tty_flush();
		return;
	}

	fwrite(filebuf, 1, fill, rcf);
	free(filebuf);
	fprintf(rcf, "%s\n", startrcmark);

	for (i = 0; i < num_options-1; i++)
		fprintf(rcf, "immediate /set %s %s\n", options[i].shortname,
			options[i].onoff ? "ON" : "OFF");

#ifdef	WITH_ANSI_COLOURS
	for (i = 0; i < num_colours; i++)
		fprintf(rcf, "immediate /colour %s %s %s\n",
			colours[i].shortname,
			getcolourname(colours[i].fgcol),
			getcolourname(colours[i].bgcol));
#endif
	fprintf(rcf, "%s\n", endrcmark);
	fclose(rcf);
	tty_gotoxy(0, t_lines-1);
	tty_puts("Successfully written.");
	need_llclear = 1;
	tty_flush();
}

