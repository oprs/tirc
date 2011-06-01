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
static char rcsid[] = "$Id: screen.c,v 1.85 1998/02/20 18:30:19 token Exp $";
#endif

#include "tirc.h"

#ifdef	HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef	HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef	HAVE_CTYPE_H
#include <ctype.h>
#endif
#ifdef	HAVE_STRING_H
#include <string.h>
#elif defined(HAVE_MEMORY_H)
#include <memory.h>
#endif
#ifdef	HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef	HAVE_REGEX_H
#include <regex.h>
#elif defined(HAVE_LIBGEN_H)
#include <libgen.h>
#endif

#include "compat.h"
#include "tty.h"
#include "colour.h"

#define	MAXPAGES	10	/* maximum number of screen pages */
#define	MAXPROMPTSZ	20	/* maximum length of editor line prompt */

struct ircwin {
	struct ircwin	*iw_next;
	struct ircwin	*iw_prev;
	int		iw_start;
	int		iw_end;
	struct channel	*iw_ch;
	unsigned	iw_flags;
	int		iw_type;
	struct aline	*iw_firstl; 
	struct aline	*iw_lastl;
	struct aline	*iw_line;
	struct aline	*iw_match;
	struct aline	*iw_savtop;
	int		iw_nlines;
	FILE		*iw_log;
	char		*iw_logname;
	struct tty_region *iw_reg;
}; 

struct ircpage {
	struct ircwin	*ipg_iwa;
	struct ircwin	*ipg_iwc;
	CIRCLEQ_ENTRY(ircpage) ipg_entries;
	int		ipg_upd;
}; 

extern void	(*othercmd) __P((char *));

static struct ircwin *create_iwin __P((void));
static void	del_iwin __P((struct ircwin *w));
static void	iw_addstr __P((struct ircwin *, char *, unsigned,
			struct ircpage *, int));
static struct aline *iw_repaint __P((struct ircwin *));
static void	iw_addbs __P((struct ircwin *, struct aline *));
static void	done_logfn __P((char *));
static void	searchre __P((char *));
static void	reformat_backscroll __P((struct ircwin *, int));
static void	reformat_all __P((void));
static void	ipg_switchtopg __P((struct ircpage *));
static void	ipg_get_updatelist __P((char *));
static int	ipg_check_updatelist __P((int));
static char	*iwlog_strip_attribs __P((char *));
static int	eline_offset __P((int, int *, int *));
static int	get_elw __P((void));
	
static struct ircwin *iwa, *iwc;
static struct ircpage *cpage;

static int	cpageno;
static int	otherpage;

static int	screen_is_setup;
static int	dodisplay;	/* if 0, window updates and clock won't be
				 * displayed (for example when in opt editor */

CIRCLEQ_HEAD(, ircpage)	pages_head;

#define	IW_USED		1
#define IW_VISIBLE	2
#define IW_LOGGING	4

extern char	*myname, nick[], *srvnm;
extern void	(*othercmd) __P((char *));
extern int	on_irc;
extern int	umd;
extern int	errno;
extern int	is_away;

char		*prompt;
static int	promptlen;
char		defprompt[] = "> ", defpprompt[] = "<P> ";
char		ppre[] = "*** ";
int		debugwin;

static char	top[] = "[ top of backscroll ]";

static int	dolog;
static int	eline;
#ifdef	HAVE_REGCOMP
static regex_t	sre;
#elif defined(HAVE_REGCMP)
static char	*sre;
#else
#error	Need either regcomp() or regcmp()
#endif
static char	matchstring[MAXINPUT];
static int	eline_clobbered;	/* XXX kludge, used by findre() */
static int	searchdir;
static int	fre_line;
static int	numpages;
static sig_atomic_t atomic_view_ipg_update;

size_t		vsnprintf_buffer_size = BUFSZ;

/*
 * Initialize our screen/window management and the tty interface.
 * Returns 0 on success, -1 on failure.
 */
int
screeninit()
{
	extern void sigwinch __P((int));

	debugwin = 0;
	dodisplay = 1;
	dolog = 1;
	set_prompt(defprompt);
	fre_line = -1;
	atomic_view_ipg_update = 0;

	if (tty_init() < 0)
		return -1;

	if (our_signal(SIGWINCH, sigwinch) < 0) {
		fprintf(stderr, "\nin screeninit(): failed to install signal \
handler\n");
		return -1;
	}

	tty_addbuff(tc_clear);
	tty_gotoxy(0, 0);
	tty_flush();

	CIRCLEQ_INIT(&pages_head);
	cpage = NULL;
	cpageno = 0;
	otherpage = 0;
	numpages = 0;
	screen_adjust();
	ipg_create();
	update_eline("", 0, 0, 0);
	move_eline(0);
	tty_cbreak();

	if (
#ifdef	HAVE_REGCOMP
	    regcomp(&sre, "^.*$", REG_EXTENDED | REG_NOSUB)
#elif defined(HAVE_REGCMP)
	    (sre = regcmp("^.*$", NULL)) == NULL
#endif
	)
	{
		setlog(0);
		iw_printf(COLI_TEXT, "%sUnable to compile regular expression\
dumm expression.  Duh.  Regex probably doesn't work at all\n", ppre);
		setlog(1);
	}

	screen_is_setup = 1;

	return 0;
}

/*
 * Adjust screen to current terminal window size.
 */
void
screen_adjust()
{
	eline = t_lines-1;
	reformat_all();
}

/*
 * Reset terminal.
 */
void
screenend()
{
	tty_gotoxy(0, t_lines-1);
	tty_addbuff(tc_clreol);
	tty_flush();
	tty_reset();

	if (our_signal(SIGWINCH, SIG_DFL) < 0) {
		fprintf(stderr, "\nin screenend(): failed to reset signal \
handler\n");
		Exit(1);
	}
	raise(SIGWINCH);
}

/*
 * Create a tirc window and hook it into the window list.
 * The window will be reset and is not visible/used.
 * Returns a pointer to the newly created window or NULL on failure.
 */
static struct ircwin *
create_iwin()
{
	struct ircwin *w;

	w = chkmem(calloc(1, sizeof (struct ircwin)));
	w->iw_next = w->iw_prev = NULL;
	w->iw_firstl = w->iw_lastl = w->iw_line = w->iw_match =
		w->iw_savtop = NULL;
	w->iw_log = NULL;
	w->iw_logname = NULL;
	w->iw_reg = NULL;
	if (iwa == NULL) {
		iwa = iwc = w;
		return w;
	}
	iwa->iw_prev = w;
	w->iw_next = iwa;
	iwa = w;
	return w;
}

/*
 * Delete a tirc window and remove it from the window list.
 */
static void
del_iwin(w)
	struct ircwin *w;
{
	if (w == NULL)
		return;
	if (w->iw_next != NULL)
		w->iw_next->iw_prev = w->iw_prev;
	if (w->iw_prev != NULL)
		w->iw_prev->iw_next = w->iw_next;
	if (iwa == w)
		iwa = w->iw_next;
	if (iwc == w) {
		if (w->iw_prev)
			iwc = w->iw_prev;
		else 
			iwc = w->iw_next;
	}
	free(w);
}

/*
 * Display the given iw_msg on the appropriate irc window(s).
 * This also checks for ipg page update.
 */
void
dispose_msg(im)
	struct iw_msg *im;
{
	struct ircwin *w;
	struct channel *c;
	struct ircpage *ipg, *savepg = cpage;

	/* If pending page update info, display now. */
	if (atomic_view_ipg_update)
		iw_draw();

	/*
	 * IMT_PMSG and IMT_CTRL want to be displayed on the
	 * current page in the current window, if possible.
	 */
	if (cpage == savepg && iwc != NULL &&
	    iwc->iw_flags & IW_USED && im->iwm_type & iwc->iw_type &&
	    (im->iwm_type & IMT_CTRL || im->iwm_type & IMT_PMSG)) {
		iw_addstr(iwc, im->iwm_text, im->iwm_type & IMT_FMT, savepg,
			im->iwm_chint);
		ipg_switchtopg(savepg);
		return;
	}

	/* Else try the other pages */
	for (ipg = pages_head.cqh_first; ipg != (struct ircpage *)&pages_head;
	    ipg = ipg->ipg_entries.cqe_next) {
	    ipg_switchtopg(ipg);

	    /*
	     * IMT_DEBUG and IMT_CMSG want to be displayed on the first
	     * matching window.
	     */
	    for (w = iwa; w != NULL; w = w->iw_next)
		if (w->iw_flags & IW_USED && im->iwm_type & w->iw_type) {

		    if ((im->iwm_type & IMT_CMSG) == 0 ||
			im->iwm_chn == NULL) {
			iw_addstr(w, im->iwm_text, im->iwm_type & IMT_FMT,
				savepg, im->iwm_chint);
			if (cpage != savepg) {
				ipg->ipg_upd = 1;
				ipg_switchtopg(savepg);
			}
			return;
		    }

		    if (im->iwm_type & IMT_CMSG) {
			/* channel privmsg, known channel */
			c = w->iw_ch;
			while (c) {
			    if (c->ch_hash == im->iwm_chn->ch_hash) {
				iw_addstr(w, im->iwm_text,
					im->iwm_type & IMT_FMT, savepg,
					im->iwm_chint);
				if (c->ch_logfile != NULL)
				    fprintf(c->ch_logfile, "%s%s", timestamp(),
					im->iwm_text);
				if (cpage != savepg) {
					ipg->ipg_upd = 1;
					ipg_switchtopg(savepg);
				}
				return;
			    }
			    c = c->ch_wnext;
			}
		    }
		}
	}
	ipg_switchtopg(savepg);
}

/*
 * Print a message of type IMT_CTRL to the appropriate window(s).
 */
int
/*VARARGS2*/
#ifdef	__STDC__
iw_printf(int colorhint, const char *fmt, ...)
#else
iw_printf(colorhint, fmt, va_alist)
	int colorhint;
	const char *fmt;
	va_dcl
#endif
{
	va_list ap;
	char *lbuf;
	int ln;
	struct iw_msg m;

#ifdef	__STDC__
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
	lbuf = chkmem(malloc(vsnprintf_buffer_size));

	for (;;) {
		ln = vsnprintf(lbuf, vsnprintf_buffer_size, fmt, ap);
		if (ln >= vsnprintf_buffer_size) {
			free(lbuf);
			vsnprintf_buffer_size += BUFSZ;
			lbuf = chkmem(malloc(vsnprintf_buffer_size));
		}
		else {
			if (vsnprintf_buffer_size > 4 * BUFSZ)
				/* pretty large; probably not needed for
				 * further vsnprintfs; reset. */
				vsnprintf_buffer_size = BUFSZ;
			break;
		}
	}
	va_end(ap);

	m.iwm_text = lbuf;
	m.iwm_type = IMT_CTRL | IMT_FMT;
	m.iwm_chn = NULL;
	m.iwm_chint = colorhint;
	dispose_msg(&m);

	free(lbuf);
	return ln;
}

/*
 * Redraws all currently visible irc window frames.
 */
void
iw_draw()
{
	struct ircwin *w = iwa;
	static char *title;
	char cmodes[32], umodes[32], type[8];
	static char foc[] = "%s%s%s%s%s%s%s%s on %s : %d%s "; 
	static char nfoc[] = "%s%s%s%s ";
	static char qfoc[] = "%s (query) : %s %s%s%s on %s : %d%s ";
	static char qnfoc[] = "%s (query) ";
	static char *t;
	char maxchanname[13];
	char maxservname[21];
	register unsigned i, m, mlen;
	int ox, oy;
	static char pglist[MAXPAGES+2];
#ifdef	WITH_ANSI_COLOURS
	register int docolor = check_conf(CONF_COLOUR);
	int slt;
#endif
	if (is_in_odlg())
		return;

	if (title != NULL)
		free(title);
	title = chkmem(malloc((unsigned)t_columns+1));
	if (t != NULL)
		free(t);
	t = chkmem(malloc((unsigned)t_columns+1));

	tty_getxy(&ox, &oy); 

	for (; w; w = w->iw_next) {
		if (!(w->iw_flags & (IW_USED | IW_VISIBLE)))
			continue;
		/*
		 * Compose the window footer title.
		 */
		i = 0;
		if (w->iw_type & IMT_CTRL)
			type[i++] = 's';
		if (w->iw_type & IMT_CMSG)
			type[i++] = 'c';
		if (w->iw_type & IMT_PMSG)
			type[i++] = 'p';
		if (w->iw_type & IMT_DEBUG)
			type[i++] = 'd';
		type[i] = 0;

		if (is_away)
			strcpy(umodes, "away+");
		else {
			i = 0;
			if (umd & UM_O)
				umodes[i++] = 'o';
			if (umd & UM_I)
				umodes[i++] = 'i';
			if (umd & UM_R)
				umodes[i++] = 'r';
			if (umd & UM_S)
				umodes[i++] = 's';
			if (umd & UM_W)
				umodes[i++] = 'w';
			umodes[i] = 0;
		}

		i = 0;
		m = (w->iw_ch != NULL) ? w->iw_ch->ch_modes : 0;
		if (m & MD_O)
			cmodes[i++] = 'o';
		if (m & MD_A)
			cmodes[i++] = 'a';
		if (m & MD_I)
			cmodes[i++] = 'i';
		if (m & MD_K)
			cmodes[i++] = 'k';
		if (m & MD_L)
			cmodes[i++] = 'l';
		if (m & MD_M)
			cmodes[i++] = 'm';
		if (m & MD_T)
			cmodes[i++] = 't';
		if (m & MD_N)
			cmodes[i++] = 'n';
		if (m & MD_P)
			cmodes[i++] = 'p';
		if (m & MD_S)
			cmodes[i++] = 's';
		if (m & MD_V)
			cmodes[i++] = 'v';
		if (m & MD_Q)
			cmodes[i++] = 'q';
		cmodes[i] = 0;

		strncpy(maxchanname, w->iw_ch ? w->iw_ch->ch_name : "", 12);
		maxchanname[12] = 0;
		strncpy(maxservname, on_irc ? srvnm : "<not connected>", 20);
		maxservname[20] = 0;

		if (check_conf(CONF_PGUPDATE) && ipg_check_updatelist(1)) {
			ipg_get_updatelist(pglist);
			atomic_view_ipg_update = 0;
		}
		else
			pglist[0] = '\0';

		if (w->iw_ch != NULL && w->iw_ch->ch_modes & MD_QUERY) {
		    if (w == iwc)
			sprintf(t, qfoc, maxchanname+1, nick,
				(umodes[0]) ? " (+" : "",
				umodes,
				(umodes[0]) ? ")" : "",
				maxservname, cpageno, pglist);
		    else
			sprintf(t, qnfoc, maxchanname+1);
		}
		else {
		    if (w == iwc)
			sprintf(t, foc, maxchanname, cmodes[0] ? " (+" : "",
			    cmodes, cmodes[0] ? ") : " : 
			    	maxchanname[0] ? " : " : "",
			    nick,
			    (umodes[0]) ? " (+" : "",
			    umodes,
			    (umodes[0]) ? ")" : "",
			    maxservname, cpageno, pglist);
		    else
			sprintf(t, nfoc, maxchanname, cmodes[0] ? " (+" : "",
			    cmodes, cmodes[0] ? ")" : "");
		}

#ifdef	WITH_ANSI_COLOURS
		slt = (w == iwc) ? COLI_FOC_STATUS : COLI_NFOC_STATUS;
#endif

		mlen = (w->iw_lastl != w->iw_line) ? t_columns-18 :
			(check_conf(CONF_CLOCK)) ? t_columns-14 :
			t_columns-6;
		if (strlen(t) > mlen)
			t[mlen-1] = 0;

		strncpy(title, t, (unsigned)t_columns-1);
		title[t_columns-1] = 0;
		tty_gotoxy(0, w->iw_end);
#ifdef	WITH_ANSI_COLOURS
		if (docolor)
		    tty_addbuff(set_colour(get_fg_colour_for(slt),
			get_bg_colour_for(slt)));
		else
#endif
		tty_addbuff(tc_rev);
		tty_puts(title);

		{
		int x, y, j; 
		static char fill[2];

		fill[0] = (w == iwc) ? '^' : 
			check_conf(CONF_STIPPLE) ? '-' : ' ';

		tty_getxy(&x, &y);
		for (j = x; j < t_columns; j++)
			tty_addbuff(fill);
		}
#ifdef	WITH_ANSI_COLOURS
		if (docolor)
		    tty_addbuff(colour_off());
		else
#endif
		tty_addbuff(tc_noattr);
/*		tty_addbuff(tc_clreol);*/

		if (w->iw_type != 0) {
			tty_gotoxy((int)(t_columns-strlen(type)-3), w->iw_end);
#ifdef	WITH_ANSI_COLOURS
			if (docolor)
			    tty_addbuff(set_colour(get_fg_colour_for(slt),
				get_bg_colour_for(slt)));
			else
#endif
			tty_addbuff(tc_rev);
			tty_printf(NULL, " +%s ", type);
#ifdef	WITH_ANSI_COLOURS
			if (docolor)
			    tty_addbuff(colour_off());
			else
#endif
			tty_addbuff(tc_noattr);
		}

		if (w->iw_lastl != w->iw_line) {
			tty_gotoxy(t_columns-18, w->iw_end);
#ifdef	WITH_ANSI_COLOURS
			if (docolor)
			    tty_addbuff(set_colour(get_fg_colour_for(slt),
				get_bg_colour_for(slt)));
			else
#endif
			tty_addbuff(tc_bold);
			tty_puts("[+]");
#ifdef	WITH_ANSI_COLOURS
			if (docolor)
			    tty_addbuff(colour_off());
			else
#endif
			tty_addbuff(tc_noattr);
		}
	}
	if (check_conf(CONF_CLOCK))
		disp_clock();
	tty_gotoxy(ox, oy);
	tty_flush();
}

/*
 * Add a channel at the top of the current window's channel list.
 * The C bit is set in the window (receive channel msg).
 */
void
iw_addchan(ch)
	struct channel *ch;
{
	iwc->iw_type |= IMT_CMSG;
	ch->ch_wprev = NULL;
	if (iwc->iw_ch == NULL) {
		iwc->iw_ch = ch;
		ch->ch_wnext = NULL;
		return;
	}
	ch->ch_wnext = iwc->iw_ch;
	if (iwc->iw_ch != NULL)
		iwc->iw_ch->ch_wprev = ch;
	iwc->iw_ch = ch;
}

/*
 * Delete a channel from the window where it is linked to.
 */
void
iw_delchan(c)
	struct channel *c;
{
	struct ircwin *w;
	struct channel *ch;
	struct ircpage *ipg, *savepg = cpage;

	for (ipg = pages_head.cqh_first;
	    ipg != (struct ircpage *) &pages_head;
	    ipg = ipg->ipg_entries.cqe_next) {
	    ipg_switchtopg(ipg);
	    /*
	     * Traverse the window list. For each window, iterate through
	     * the channel list and if `c' is found, delete it.
	     */
	    for (w = iwa; w != NULL; w = w->iw_next) {
		    for (ch = w->iw_ch; ch != NULL; ch = ch->ch_wnext)
			    if (ch == c) {
				    if (ch->ch_wprev)
					ch->ch_wprev->ch_wnext = ch->ch_wnext;
				    if (ch->ch_wnext)
					ch->ch_wnext->ch_wprev = ch->ch_wprev;
				    if (w->iw_ch == ch)
					w->iw_ch = ch->ch_wnext;
				    ch->ch_wprev = ch->ch_wnext = NULL;
				    break;
			    }
		    if (w->iw_ch == NULL)
			    w->iw_type &= ~IMT_CMSG;
	    }
	}
	ipg_switchtopg(savepg);
}

/*
 * Returns the top channel of the current window.
 */
struct channel *
iw_getchan()
{
	return iwc->iw_ch;
}

/*
 * Moves the cursor to a position in the editor line.
 */
void
move_eline(p)
	int p;
{
	if (is_in_odlg())
		return;

	eline_offset(p, &p, NULL);

	tty_gotoxy(p+promptlen, eline);
	tty_flush();
}

/*
 * Get the width of the editor line.
 * Side-effect: updates prompt length (promptlen).
 */
static int
get_elw()
{
	promptlen = strlen(prompt);
	return t_columns - 5 - promptlen;
}

/*
 * Display the editor line at the bottom of the screen.
 */
void
update_eline(line, size, cursor, force_update)
	char *line;
	int size, cursor, force_update;
{
	static char *savedline, *savedprompt;
	static int savedsize, savedstart;
	int ellen;
	int start;
	int dispstart, displen;
	int diffoff;
	char *svlp, *lp;
	char ltmp[MAXINPUT+1];

	if (savedline == NULL)
		savedline = chkmem(calloc(MAXINPUT*2+1, 1));
	if (savedprompt == NULL)
		savedprompt = chkmem(calloc(MAXPROMPTSZ+1, 1));

	if (cursor > size)
		cursor = size;
	if ((ellen = get_elw()) == 0)
		ellen++;

	(void)eline_offset(cursor, &cursor, &start);

	if (eline_clobbered || strcmp(savedprompt, prompt))
		force_update = 1;

	/*
	 * Compare with the saved line.
	 */
	if (force_update || savedsize < start) {
		dispstart = start;
		displen = min(size-start, ellen);
		diffoff = 0;
	}
	else {
		int sl = savedsize-savedstart, l = size-start, i;

		lp = line + start;
		svlp = savedline + savedstart;

		for (i = 0, diffoff = 0; sl > 0 && l > 0 && *svlp == *lp
		    && i < ellen;
		    diffoff++, lp++, svlp++, sl--, l--, i++)
			;
		dispstart = start+diffoff;
		displen = min(size-dispstart, ellen-diffoff);
	}

	if (displen > 0)
		memmove(ltmp, line+dispstart, (unsigned)displen);
	ltmp[displen] = '\0';

	if (force_update) {
		tty_gotoxy(0, eline);
#ifdef	WITH_ANSI_COLOURS
		if (check_conf(CONF_COLOUR))
			tty_printf(NULL, "%s%s%s",
				set_colour(get_fg_colour_for(COLI_PROMPT),
					get_bg_colour_for(COLI_PROMPT)),
				prompt, colour_off());
		else
#endif
		tty_puts(prompt);
	}
	tty_gotoxy(diffoff+promptlen, eline);
	tty_addbuff(ltmp);
	tty_addbuff(tc_clreol);
	elhome();

	/* Save this line and prompt. */
	memmove(savedline, line, (unsigned)size);
	savedline[size] = '\0';
	savedsize = size;
	savedstart = start;
	memmove(savedprompt, prompt, (unsigned)promptlen);
	savedprompt[promptlen] = '\0';
}

/*
 * Sets newstart to the current offset added to the editor line start and
 * newcurs to the new cursor value.  newcurs and newst may be NULL.
 * Returns 1 if cursor movement requires editor line update and 0 otherwise.
 */
static int
eline_offset(pos, newcurs, newst)
	int pos, *newcurs, *newst;
{
	static int start;
	int ellen = get_elw(), newstart = start, update;

	/*
	 * See if we have to recalculate the viewport.
	 */
	if (pos > start+ellen) {
		while (newstart+ellen <= pos)
			newstart += ellen / 2;
		start = newstart;
		update = 1;
	}
	else if (pos < start) {
		if (pos == 0)
			newstart = 0;
		else
			while (newstart >= pos)
				newstart -= ellen / 2;
		start = newstart;
		update = 1;
	}
	else
		update = 0;
	
	if (newcurs != NULL)
		*newcurs = pos - start;
	if (newst != NULL)
		*newst = newstart;
	return update;
}

/*
 * Adjusts the prompt.
 */
void
set_prompt(pro)
	char *pro;
{
	struct channel *ch = cchan();
	static char p[MAXPROMPTSZ+1];
	extern int pasting;	/* paste mode */
	int copysz = pasting ? MAXPROMPTSZ - 4 : MAXPROMPTSZ - 2;

	if (pro != NULL)
		prompt = pro;
	else if (ch == NULL || ch->ch_name == NULL || *ch->ch_name == 0) {
		if (pasting)
			prompt = defpprompt;
		else
			prompt = defprompt;
	}
	else {
		if (! (ch->ch_modes & MD_QUERY)) {
			strncpy(p, ch->ch_name, (unsigned)copysz);
			p[copysz] = 0;
		}
		else {
			char buff[MAXPROMPTSZ+1];

			strncpy(buff, ch->ch_name+1, (unsigned)copysz-6);
			buff[copysz-6] = '\0';
			sprintf(p, "Query:%s", buff);
			p[copysz] = 0;
		}
		if (pasting)
			strcat(p, defpprompt);
		else
			strcat(p, defprompt);
		prompt = p;
	}
}

/*
 * Display the current editor line mode in the lower right corner.
 */
void
showmode(mode)
	int mode;
{
	tty_gotoxy(t_columns-4, eline);
	switch (mode) {
	case 0:
		tty_puts("CMD");
		break;
	case 1:
		tty_puts("INS");
		break;
	case 2:
		tty_puts("OVR");
		break;
	}
}

/*
 * Reads user input. If (s)he types `y' or `Y', return YES otherwise NO.
 */
int
askyn(m)
	char *m;
{
	tty_gotoxy(0, t_lines-1);
	tty_addbuff(tc_bold);
	tty_puts(m);
	tty_addbuff(tc_noattr);
	tty_addbuff(tc_clreol);
	tty_flush();
	use_ilb(1);

	if (toupper(tty_getch()) == 'Y') {
		tty_puts("Yes");
		use_ilb(0);
		return YES;
	}
	tty_puts("No");
	tty_flush();
	use_ilb(0);
	return NO;
}

/*
 * Clear the current window.
 */
void
clearwin()
{
	int i;

	iwc->iw_reg->ttr_line = iwc->iw_reg->ttr_cursor = 0;

	for (i = 0; i <= iwc->iw_reg->ttr_end-iwc->iw_reg->ttr_start; i++) {
		tty_gotoxy(0, iwc->iw_reg->ttr_start+i);
		tty_puts("~");
		tty_addbuff(tc_clreol);
	}
	tty_flush();
	iwc->iw_reg->ttr_line = iwc->iw_reg->ttr_cursor = 0;
	iwc->iw_reg->ttr_sdown = 0;
}

/*
 * Makes all visible windows the same size.
 * If the place cannot be shared equally among all windows, the 
 * first window gets an extra line.
 */
void
equalwin()
{
	struct ircwin *w;
	int i = 0, height, start = 0;

	for (w = iwa; w != NULL; w = w->iw_next, i++)
		;
	height = (t_lines-2) / i;

	for (w = iwa; w != NULL; w = w->iw_next) {
		w->iw_start = w->iw_reg->ttr_start = start;
		w->iw_end = start+height;
  		w->iw_reg->ttr_end = w->iw_end-1;
		start = w->iw_end + 1;
		if (start+height > t_lines-2)
			height = t_lines-2-start;
		w->iw_savtop = NULL;
		iw_repaint(w);
	}
	iw_draw();
}

/*
 * equalwin() all pages.
 */
void
equalwins()
{
	struct ircpage *ipg, *savepg = cpage;

	for (ipg = pages_head.cqh_first; ipg != (struct ircpage *)&pages_head;
	    ipg = ipg->ipg_entries.cqe_next) {
		ipg_switchtopg(ipg);
		equalwin();
	}
	ipg_switchtopg(savepg);
}

/*
 * Create a new ircwin and makes all existing windows equal height.
 * The newly added window is made the current window.
 */
void
newircwin()
{
	struct ircwin *w;
	struct tty_region *r;
	struct aline *al;

	if ((w = create_iwin()) == NULL || (r = tty_rcreate(0, 0)) == NULL) {
		fprintf(stderr, "%s: cannot create window\n", myname);
		Exit(1);
	}

	if (iwa == NULL)
		iwa = w;
	iwc = w;

	w->iw_flags = IW_USED | IW_VISIBLE;
	w->iw_reg = r;

	al = chkmem(calloc(sizeof *al, 1));
	al->al_prev = al->al_next = NULL;
	al->al_text = chkmem(Strdup(top));
	iw_addbs(w, al);

	set_prompt(NULL);
	equalwin();
}

/*
 * Delete the current ircwin and resize the remaining windows.
 */
void
delircwin()
{
	if (iwc->iw_ch != NULL) {
		iw_printf(COLI_TEXT, "%sThere's a channel in this window\n",
			ppre);
		return;
	}
	if (iwa->iw_next == NULL) {
		iw_printf(COLI_TEXT, "%sCannot delete the only window\n", ppre);
		return;
	}
	if (iwc->iw_type & IMT_DEBUG)
		debugwin--;
	tty_rdestroy(iwc->iw_reg);
	del_iwin(iwc);
	set_prompt(NULL);
	equalwin();
}

/*
 * Make the next/previous window the active one. Wraps around if at last
 * window.  If dir >= 0, then go forward in window list, if < 0, go backward.
 */
void
switchwin(dir)
	int dir;
{
	if (dir >= 0) {
		if (iwc->iw_next != NULL)
			iwc = iwc->iw_next;
		else
			iwc = iwa;
	}
	else {
		if (iwc->iw_prev != NULL)
			iwc = iwc->iw_prev;
		else {
			struct ircwin *iw;

			for (iw = iwa; iw != NULL && iw->iw_next != NULL; 
			    iw = iw->iw_next)
				;
			if (iw != NULL)
				iwc = iw;
		}
	}
	set_prompt(NULL);
	iw_draw();
	elrefr(0);
}

/*
 * Check if a given channel is any of the top (active) channels 
 * of the visible windows.
 * Returns 1 if so, 0 otherwise.
 */
int
wchanistop(cname)
	char *cname;
{
	unsigned long h;
	char t[MSGSZ];
	struct ircwin *w;
	struct ircpage *ipg, *savepg = cpage;

	strcpy(t, cname);
	h = elf_hash(irc_strupr(t));

	for (ipg = pages_head.cqh_first; ipg != (struct ircpage *) &pages_head;
	    ipg = ipg->ipg_entries.cqe_next) {
		ipg_switchtopg(ipg);

		w = iwa;
		while (w) {
			if (w->iw_flags & (IW_USED | IW_VISIBLE))
				if (w->iw_ch != NULL)
					if (w->iw_ch->ch_hash == h) {
						ipg_switchtopg(savepg);
						return 1;
					}
			w = w->iw_next;
		}
	}
	ipg_switchtopg(savepg);
	return 0;
}

/*
 * Returns the top channel in the current window.
 */
struct channel *
cchan()
{
	if (iwc && iwc->iw_flags & (IW_USED | IW_VISIBLE))
		return iwc->iw_ch;
	return NULL;
}

/*
 * Add a string to the irc window.  Wraps the line according to
 * screen width, stores its parts in the backscroll list and logs
 * to a file if logging is enabled.
 */
static void
iw_addstr(win, line, format, pg, chint)
	struct ircwin *win;
	char *line;
	unsigned format;
	struct ircpage *pg;
	int chint;
{
	static char *buf, *bufptr;
	static int bufsize, buffill;
	int linelen = strlen(line);
	int physlinesz;
	char *bufiter;
	int paginate = 0, linescut = 0, pagincnt = 1;
	int i;

	/* If buffer was released, allocate new one. */
	if (buf == NULL) {
		bufptr = buf = chkmem(malloc(BUFSZ));
		bufsize = BUFSZ;
		buffill = 0;
	}

	/* Add line(s) text to the buffer. */
	if (buffill + linelen >= bufsize) {
		ptrdiff_t bufdelta = bufptr - buf;

		assert(bufdelta >= 0);
		buf = chkmem(realloc(buf, (bufsize += linelen+BUFSZ)));
		bufptr = buf+bufdelta;
	}

	/*
	 * Ugly, quick hack (krass scheissn, ey): copy, and replace TAB with
	 * space. (If the user has entered a TAB, incoming stuff is already
	 * filtered in irc.c).
	 */
	{
	register char *s1 = buf+buffill, *s2 = line;

	for (i = 0; i < linelen; i++, s1++, s2++) {
		if (*s2 != '\t')
			*s1 = *s2;
		else
			*s1 = ' ';
	}
	}
	buffill += linelen;

	/*
	 * Cut lines out of the buffer:
	 * Process current line up to the next newline.
	 * If it's too long for current screen width, split off earlier,
	 * at the last word border if possible, and add a newline and
	 * indentation if 'format requested.
	 * If buffer ends, with no newline read, do not add the last
	 * (truncated) line.  The buffer is released if all contained
	 * lines are cut out, that is if 'fill is zero.
	 */
	while (buffill > 0 && bufptr < buf+buffill) {
		char *ltmp;
		int formatoffs = linescut > 0 && format ? INDENT : 0;
		int coloffs;
		int maxcolumn = t_columns - win->iw_reg->ttr_cursor
			- formatoffs - 1;

		for (bufiter = bufptr, physlinesz = 0;
		    *bufiter != '\n' && bufiter < buf+buffill
		    && physlinesz < maxcolumn; bufiter++, physlinesz++)
			;
		if (bufiter == buf+buffill && *bufiter != '\n')
			break;
		if (physlinesz == maxcolumn && *bufiter != '\n') {
			ptrdiff_t backsteps = 0;

			while (physlinesz > t_columns - (t_columns/4)
			    && (!isspace(*bufiter) && !ispunct(*bufiter)
			    || iscntrl(*bufiter))) {
				bufiter--;
				physlinesz--;
				backsteps++;
			}
			if (physlinesz == t_columns - (t_columns/4)) {
				bufiter += backsteps;
				physlinesz += backsteps;
			}
			bufiter--;
		}

		ltmp = chkmem(malloc(physlinesz+formatoffs+16));
#ifdef	WITH_ANSI_COLOURS
		if (check_conf(CONF_COLOUR)) {
			coloffs = sprintf(ltmp, "%d,%d;",
				get_fg_colour_for(chint),
				get_bg_colour_for(chint));
		}
		else
#endif
			coloffs = 0;
		if (formatoffs > 0) {
			memset(ltmp+coloffs, ' ', (unsigned)formatoffs);
			ltmp[coloffs] = '+';
		}
		if (physlinesz > 0)
			memcpy(ltmp+formatoffs+coloffs, bufptr, physlinesz);
		ltmp[formatoffs+coloffs+physlinesz] = '\0';

		bufptr = bufiter + 1;
		linescut++;

		/* Display line in region */
		if (win->iw_lastl == win->iw_line && cpage == pg
		    && dodisplay) {
			tty_printf(win->iw_reg, "%s\n", ltmp);
			win->iw_savtop = NULL;
		}

		/* Save line to backscroll and log */
		if (dolog) {
			struct aline *al;

			al = chkmem(calloc(sizeof *al, 1));
			al->al_prev = al->al_next = NULL;
			al->al_text = ltmp;
			iw_addbs(win, al);

			if (win->iw_log != NULL)
				fprintf(win->iw_log, "%s\n",
				    iwlog_strip_attribs(ltmp));
		}
		else
			free(ltmp);

		/* Paginate output */
		if (win->iw_lastl == win->iw_line && cpage == pg && dodisplay
		    && linescut+2 > pagincnt * (win->iw_end - win->iw_start)) {
			use_ilb(1);
			paginate = 1;
			pagincnt++;
			tty_gotoxy(0, t_lines-1);
			tty_addbuff(tc_bold);
			tty_printf(NULL, "+MORE (q to abort)+");
			tty_addbuff(tc_noattr);
			tty_addbuff(tc_clreol);
			tty_flush();
			if (tty_getch() == 'q') {
				bufptr = buf+buffill;
				break;
			}
			use_ilb(0);
		}
	}

	/* Check if buffer is processed and can be released. */
	if (bufptr >= buf+buffill) {
		free(buf);
		buf = NULL;
	}

	/* Repaint editor line, if required, and flush the tty buffer. */
	if (paginate)
		elrefr(1);
	else
		tty_flush();
}

/*
 * Add a string to a window's backscroll list.
 */
static void
iw_addbs(win, line)
	struct ircwin *win;
	struct aline *line;
{
	if (win->iw_firstl == NULL)
		win->iw_line = win->iw_lastl = win->iw_firstl = line;
	else {
		line->al_prev = win->iw_lastl;
		win->iw_lastl->al_next = line;

		if (win->iw_lastl != win->iw_line)
			win->iw_lastl = line;
		else
			win->iw_lastl = win->iw_line = line;
	}
	/*
	 * If backscroll exceeds BACKSCROLL lines, kill off
	 * the first quarter.
	 */
	if (++win->iw_nlines > BACKSCROLL) {
		int n;
		struct aline *al, *l;

		for (n = 0; n < BACKSCROLL/4; n++, win->iw_nlines--) {
			l = win->iw_firstl;
			free(l->al_text);
			l = l->al_next;
			free(win->iw_firstl);
			l->al_prev = NULL;
			win->iw_firstl = l;
		}

		/* Delete another one and add our top marker string. */
		l = win->iw_firstl->al_next;
		free(win->iw_firstl->al_text);
		free(win->iw_firstl);
		al = chkmem(calloc(sizeof *al, 1));
		al->al_text = chkmem(Strdup(top));
		al->al_prev = NULL;
		al->al_next = l;
		win->iw_firstl = l->al_prev = al;

		if (win->iw_line != win->iw_lastl) {
			win->iw_line = win->iw_lastl;
			iw_repaint(win);
			iw_draw();
		}
	}
}

/*
 * Repaint a window from backscroll list.
 * Returns a pointer to the first line displayed in window or NULL.
 */
static struct aline *
iw_repaint(iw)
	struct ircwin *iw;
{
	int i, pos = 0;
	struct aline *line, *sline;

	if (iw == NULL || iw->iw_line == NULL)
		return NULL;

	iw->iw_reg->ttr_line = iw->iw_reg->ttr_cursor = 0;
	iw->iw_reg->ttr_sdown = 0;
	line = iw->iw_line;

	for (i = 0; i < iw->iw_reg->ttr_end - iw->iw_reg->ttr_start &&
	    line != NULL; i++)
		if (line->al_prev != NULL)
			line = line->al_prev;
	sline = line;

	if (sline == iw->iw_savtop && iw->iw_savtop != NULL)
		return sline;
	iw->iw_savtop = sline;

	for (i = 0; i <= iw->iw_reg->ttr_end - iw->iw_reg->ttr_start; i++) {
		if (line != NULL) {
			if (line->al_text != NULL) {
				tty_printf(iw->iw_reg, "%s\n", line->al_text);
				pos++;
			}
			else {
				tty_gotoxy(0, iw->iw_reg->ttr_start+i);
				tty_puts("~");
				tty_addbuff(tc_clreol);
			}
			line = line->al_next;
		}
		else {
			tty_gotoxy(0, iw->iw_reg->ttr_start+i);
			tty_puts("~");
			tty_addbuff(tc_clreol);
		}
	}

	if (pos > iw->iw_reg->ttr_end - iw->iw_reg->ttr_start) {
		pos = iw->iw_reg->ttr_end - iw->iw_reg->ttr_start;
		iw->iw_reg->ttr_sdown = 1;
	}
	else
		iw->iw_reg->ttr_sdown = 0;

	tty_flush();
	iw->iw_reg->ttr_line = pos;
	iw->iw_reg->ttr_cursor = 0;

	return sline;
}

/*
 * Scroll n backscroll lines of current window in direction dir.
 * If dir is 0, return to the last line.
 */
void
iw_scroll(n, dir)
	int n, dir;
{
	struct aline *savedline = iwc->iw_line;
	int attop = 0, atbottom = 0;
	int wasatbottom = (iwc->iw_line == iwc->iw_lastl) ? 1 : 0;

	if (iwc->iw_line == NULL)
		return;

	if (n > iwc->iw_reg->ttr_end-iwc->iw_reg->ttr_start)
		n = iwc->iw_reg->ttr_end - iwc->iw_reg->ttr_start;

	if (dir) {
		for (; n; n--)
			if (dir < 0) {
				if (iwc->iw_line->al_prev != NULL)
					iwc->iw_line = iwc->iw_line->al_prev;
				else {
					attop = 1;
					break;
				}
			}
			else
				if (iwc->iw_line->al_next != NULL)
					iwc->iw_line = iwc->iw_line->al_next;
				else {
					atbottom = 1;
					break;
				}
	}
	else {
		if (iwc->iw_line == iwc->iw_lastl)
			atbottom = 1;
		else
			iwc->iw_line = iwc->iw_lastl;
	}

	if (iwc->iw_line->al_prev == NULL)
		iwc->iw_line = savedline;

	if ((dir > 0 && atbottom) || (dir < 0 && attop) ||
	    (dir == 0 && atbottom)) {
		if (atbottom && !wasatbottom)
			iw_draw();
		return;
	}
	iwc->iw_savtop = NULL;
	iw_repaint(iwc);
	iw_draw();
	elhome();
}

/*
 * Ring the bell.
 */
void
dobell()
{
	tty_addbuff(tc_bell);
	tty_flush();
}

/*
 * Ask invisibly for a password. Returns a pointer to a buffer with
 * the string read.  The buffer is wiped on each invocation.
 * We use this instead of getpass().
 */
char *
askpass(p)
	char *p;
{
	static char pass[129];
	int i;

	use_ilb(1);
	tty_gotoxy(0, t_lines-1);
	tty_puts(p);
	tty_flush();

	memset(pass, 0, 128);
	for (i = 0; i < 128; i++) {
		pass[i] = tty_getch();
		if (pass[i] == '\r' || pass[i] == '\n')
			break;
	}
	pass[i] = 0;

	elrefr(1);
	use_ilb(0);
	return pass;
}

/*
 * Get/set the current window's mode.
 */
int
iwcmode(mode)
	int mode;
{
	if (mode != -1) {
		iwc->iw_type = mode;
		iw_draw();
	}
	return iwc->iw_type;
}

/*
 * Redraw the screen in entity.
 */
void
repinsel()
{
	struct ircwin *w;

	iw_draw();
	for (w = iwa; w != NULL; w = w->iw_next) {
		w->iw_savtop = NULL;
		iw_repaint(w);
	}
	elrefr(1);
}

/*
 * Get condition of focussed window in the structure pointed to by st.
 */
void
iwc_status(st)
	struct iwstatus *st;
{
	st->iws_start = iwc->iw_start;
	st->iws_end = iwc->iw_end;
	st->iws_ch = iwc->iw_ch;
	st->iws_nlines = iwc->iw_nlines;
	st->iws_log = iwc->iw_logname;
}

void
setlog(val)
	int val;
{
	dolog = val;
}

/*
 * Log current window to a file or close an existing window log.
 */
void
iw_logtofile(filename)
	char *filename;
{
	int a;
	static char nprompt[] = "Enter window logfile name: ";
	char t[512];

	if (iwc->iw_log != NULL) {
		sprintf(t, "Already logging to %s, close? ", iwc->iw_logname);
		a = askyn(t);
		elrefr(1);
		if (a == YES)
			iw_closelog();
		else
			iw_printf(COLI_TEXT, "%sYou hear the sound of one \
hand clapping\n", ppre);
	}
	else {
		if (filename != NULL && strlen(filename))
			done_logfn(filename);
		else {
			set_prompt(nprompt);
			linedone(0);
			othercmd = done_logfn;
		}
	}
}

/*
 * Close the logfile in the current window.
 */
void
iw_closelog()
{
	if (iwc->iw_log == NULL)
		return;

	fprintf(iwc->iw_log, "\n### Closing logfile, %s\n", timestamp());
	fclose(iwc->iw_log);
	iwc->iw_log = NULL;

	if (iwc->iw_logname != NULL) {
		iw_printf(COLI_TEXT, "%sLogfile %s closed.\n", ppre,
			iwc->iw_logname);
		free(iwc->iw_logname);
		iwc->iw_logname = NULL;
	}
}

static void
done_logfn(filename)
	char *filename;
{
	struct stat statbuf;
	int r, a;
	struct aline *l;

	othercmd = NULL;
	set_prompt(NULL);

#ifdef	DEBUGV
	iw_printf(COLI_TEXT, "DEBUG: done_logfn(): filename == \"%s\"\n",
		filename);
#endif
	if (filename == NULL || !strlen(filename)) {
		iw_printf(COLI_TEXT, "%sNo logfile specified.\n", ppre);
		elrefr(1);
		return;
	}

	filename = exptilde(filename);
	r = stat(filename, &statbuf);
	
	if (r < 0 && errno != ENOENT) {
		/* Error. */
		/* XXX */
		iw_printf(COLI_WARN, "%sstat() returned error: %s\n", ppre,
			Strerror(errno));
		elrefr(1);
		return;
	}

	if (r < 0) {
		/* File does not exist, open it for writing. */
		if ((iwc->iw_log = fopen(filename, "w")) == NULL) {
			iw_printf(COLI_WARN, "%sCan't open %s: %s\n", ppre,
				filename, Strerror(errno));
			elrefr(1);
			return;
		}
	}
	else {
		/*
		 * File does already exist.  Ask if we may append to it
		 * or if we should abort.
		 */
		a = askyn("File does already exist.  Append to it? ");
		elrefr(1);
		if (a == YES) {
			if ((iwc->iw_log = fopen(filename, "a")) == NULL) {
				iw_printf(COLI_WARN, "%sCan't open %s: %s\n",
					ppre, filename, Strerror(errno));
				elrefr(1);
				return;
			}
		}
		else {
			iw_printf(COLI_TEXT, "%sFile left unchanged.\n", ppre);
			elrefr(1);
			return;
		}
	}

	iwc->iw_logname = chkmem(Strdup(filename));
	fprintf(iwc->iw_log, "\n### Opening logfile, %s\n\n", timestamp());
	iw_printf(COLI_TEXT, "%sNow logging window to %s\n", ppre, filename);

	if (askyn("Dump current window contents at start of this log?")
	    == YES) {
		fprintf(iwc->iw_log, ">>> Dumping backscroll\n");

		for (l = iwc->iw_firstl; l != NULL; l = l->al_next)
			if (l->al_text != NULL)
				fprintf(iwc->iw_log, "%s\n",
					iwlog_strip_attribs(l->al_text));
		
		fprintf(iwc->iw_log, ">>> End backscroll\n");
		setlog(0);
		iw_printf(COLI_TEXT, "%sBackscroll written out\n", ppre);
		setlog(1);
	}

	elrefr(1);
}

/*
 * Close all logfiles.
 */
void
iw_closelogs()
{
	struct ircwin *save = iwc;

	for (iwc = iwa; iwc != NULL; iwc = iwc->iw_next)
		iw_closelog();
	iwc = save;
}

/*
 * Print out the name cache for the top channel of the current window.
 */
void
iw_printcache()
{
	setlog(0);
	if (iwc->iw_ch != NULL)
		listcucache(iwc->iw_ch);
	else
		iw_printf(COLI_TEXT, "%sNo top channel in this window\n", ppre);
	setlog(1);
}

/*
 * Resize the current window, a positive value makes it grow, a negative
 * shrinks it.
 */
void
iw_resize(v)
	int v;
{
	struct ircwin *neighbour;
	int i;

	if (v == 0 || iwc == NULL || (iwc->iw_next == NULL &&
	    iwc->iw_prev == NULL))
		return;
	if (iwc->iw_next == NULL)
		neighbour = iwc->iw_prev;
	else
		neighbour = iwc->iw_next;

	if (v > 0) {
		for (i = 0; i < v && i < t_lines; i++)
			if (neighbour == iwc->iw_next &&
			    neighbour->iw_start < neighbour->iw_end-1) {
				iwc->iw_end++;
				iwc->iw_reg->ttr_end++;
				neighbour->iw_start++;
				neighbour->iw_reg->ttr_start++;
			}
			else
			if (neighbour == iwc->iw_prev &&
			    neighbour->iw_end > neighbour->iw_start+1) {
				iwc->iw_start--;
				iwc->iw_reg->ttr_start--;
				neighbour->iw_end--;
				neighbour->iw_reg->ttr_end--;
			}
		repinsel();
	}	
	else {
		for (i = ((-v) > t_lines) ? -t_lines : v; i < 0; i++)
			if (neighbour == iwc->iw_next &&
			    iwc->iw_end > iwc->iw_start+1) {
				iwc->iw_end--;
				iwc->iw_reg->ttr_end--;
				neighbour->iw_start--;
				neighbour->iw_reg->ttr_start--;
			}
			else
			if (neighbour == iwc->iw_prev && iwc->iw_start <
			    iwc->iw_end-1) {
				iwc->iw_start++;
				iwc->iw_reg->ttr_start++;
				neighbour->iw_end++;
				neighbour->iw_reg->ttr_end++;
			}
		repinsel();
	}
}

/*
 * Show the clock.
 */
void
disp_clock()
{
	extern int update_clock, busy;
	static char stamp[32];
	struct timeval tv;
	struct tm *tm;
#ifdef	WITH_ANSI_COLOURS
	int docolor = check_conf(CONF_COLOUR);
#endif

	if (!dodisplay)
		return;

	if (!busy) {
		busy = 1;

		gettimeofday(&tv, NULL);
		tm = localtime((time_t *)&tv.tv_sec);
		sprintf(stamp, " %02d:%02d ", tm->tm_hour, tm->tm_min);
		tty_gotoxy(t_columns-14, iwc->iw_end);
#ifdef	WITH_ANSI_COLOURS
		if (docolor)
		    tty_addbuff(set_colour(get_fg_colour_for(COLI_FOC_STATUS),
			get_bg_colour_for(COLI_FOC_STATUS)));
		else
#endif
		tty_addbuff(tc_rev);
		tty_puts(stamp);
#ifdef	WITH_ANSI_COLOURS
		if (docolor)
		    tty_addbuff(colour_off());
		else
#endif
		tty_addbuff(tc_noattr);
		f_elhome();
		busy = 0;
	}
	else
		update_clock = 1;
}

/*
 * Search hook for input line (for searching backscroll).
 */
void
search(dir)
	int dir;
{
	static char forwprompt[] = "(regex)/";
	static char backwprompt[] = "(backward regex)?";

	set_prompt((dir == FORWARD) ? forwprompt : backwprompt);
	allow_empty(1);
	linedone(0);
	othercmd = searchre;
	f_elhome();
	searchdir = dir;
}

static void
searchre(t)
	char *t;
{
	static int rex_used = 0;

	strncpy(matchstring, t, MAXINPUT-1);
	matchstring[MAXINPUT-1] = 0;

	othercmd = NULL;
	set_prompt(NULL);
	allow_empty(0);
	elrefr(1);

	if (!strlen(matchstring))
		return;

	if (rex_used) {
#ifdef	HAVE_REGFREE
		regfree(&sre);
#elif defined(HAVE_REGCMP)
		free(sre);
#endif
		rex_used = 0;
	}

	if (
#ifdef	HAVE_REGCOMP
	    regcomp(&sre, matchstring, REG_EXTENDED | REG_NOSUB)
#elif defined(HAVE_REGCMP)
	    (sre = regcmp(matchstring, NULL)) == NULL
#endif
	)
	{
		setlog(0);
		iw_printf(COLI_TEXT, "%sRegular expression syntax error\n",
			ppre);
		setlog(1);
		return;
	}
	rex_used = 1;
	iwc->iw_match = (searchdir == FORWARD) ? iwc->iw_firstl : iwc->iw_lastl;
	/* Put editor line into command mode for easier 'n'ext match typing. */
	el_setmode(MODE_COMMAND);
	findre(FORWARD);
}

/*
 * Find and display the next regular expression match in the current window's
 * backscroll buffer.
 * The dir parameter works different than that in search():
 * If dir is FORWARD, continue in the current direction.  If it is BACKWARD,
 * search in the opposite direction.
 */
void
findre(dir)
	int dir;
{
	struct aline *l, *ll, *oldmatch;
	int i;

	if (dir == BACKWARD)
		dir = (~searchdir) & 1;
	else
		dir = searchdir;

	if (iwc->iw_match == NULL && iwc->iw_firstl == iwc->iw_lastl) {
		tty_gotoxy(0, eline);
		tty_addbuff(tc_bold);
		tty_puts("Pattern not found");
		tty_addbuff(tc_noattr);
		tty_addbuff(tc_clreol);
		tty_flush();
		eline_clobbered = 1;
		return;
	}
	else {
		tty_gotoxy(0, eline);
		tty_printf(NULL, "%c%s", (searchdir == FORWARD) ? '/' : '?',
			matchstring);
		tty_addbuff(tc_clreol);
		tty_flush();
		eline_clobbered = 1;
	}

	oldmatch = iwc->iw_match;

	if (dir == FORWARD)
		if (iwc->iw_match != NULL && iwc->iw_match->al_next != NULL)
			l = iwc->iw_match->al_next;
		else {
			l = iwc->iw_firstl;
			tty_gotoxy(0, eline);
			tty_addbuff(tc_bold);
			tty_puts("Bottom reached, continuing at top");
			tty_addbuff(tc_noattr);
			tty_addbuff(tc_clreol);;
			tty_flush();
			eline_clobbered = 1;
			iwc->iw_savtop = NULL;
		}
	else
		if (iwc->iw_match != NULL && iwc->iw_match->al_prev != NULL)
			l = iwc->iw_match->al_prev;
		else {
			l = iwc->iw_lastl;
			tty_gotoxy(0, eline);
			tty_addbuff(tc_bold);
			tty_puts("Top reached, continuing at bottom");
			tty_addbuff(tc_noattr);
			tty_addbuff(tc_clreol);
			tty_flush();
			eline_clobbered = 1;
			iwc->iw_savtop = NULL;
		}

	for (; l; l = (dir == FORWARD) ? l->al_next : l->al_prev) {
		/* Compare lines. */
		if (
#ifdef	HAVE_REGEXEC
		    !regexec(&sre, l->al_text, 0, NULL, 0)
#elif defined(HAVE_REGEX)
		    regex(sre, l->al_text, NULL) != NULL
#endif
		)
		{
			int mcurs;

			iwc->iw_line = l;
			if ((ll = iw_repaint(iwc)) == NULL)
				return;
			iwc->iw_match = l;

			/* Go to position of this line and print it bold. */
			for (i = iwc->iw_start; ll != NULL &&
			    i < iwc->iw_end; ll = ll->al_next, i++) {
				if (ll == l) {
					/* Print matching line in bold. */
					tty_gotoxy(0, i);
					tty_addbuff(tc_bold);
					tty_puts(ll->al_text);
					tty_addbuff(tc_noattr);
					mcurs = i;
				}
				else if (ll == oldmatch) {
					/* Un-bold old match. */
					tty_gotoxy(0, i);
					tty_puts(ll->al_text);
				}
			}
			iw_draw();
			fre_line = mcurs;
			return;
		}
	}
	tty_gotoxy(0, eline);
	tty_addbuff(tc_bold);
	tty_puts("Pattern not found");
	tty_addbuff(tc_noattr);
	tty_addbuff(tc_clreol);
	tty_flush();
	eline_clobbered = 1;
	iwc->iw_match = NULL;
}

/*
 * This gets called in the main irc loop to perform some final cursor
 * movement.
 */
void
iloop_cursor_hook()
{
	/* Move cursor to last find. */
	if (fre_line != -1) {
		tty_gotoxy(0, fre_line);
		tty_flush();
		fre_line = -1;
	}
}

/*
 * Reformat the backscroll buffer of a given window to a new window
 * width.
 */
static void
reformat_backscroll(iw, newwidth)
	struct ircwin *iw;
	int newwidth;
{
	struct aline *newanchor = NULL, *newlast, *iter, *next, *new;
	char b[2*MSGSZ], *p, *newl;
	char contm[INDENT+1];
	int size, lastws, nlines = 0, l, iscont;

	if (iw->iw_firstl == NULL)
		return;

	memset(contm, ' ', INDENT);
	contm[0] = '+';
	contm[INDENT] = 0;

	/*
	 * Procedure:
	 * Step through the backscroll list.  Put the first line
	 * into a temp storage.  Then look at the next line.  If it
	 * begins with a continuation mark, remove the continuation
	 * mark and add this line to the buffer.  Repeat until a line
	 * without continuation mark is found which is not added to
	 * the buffer.  Then split the buffer on word boundaries,
	 * creating lines that fill the current screen width.
	 */
	for (iter = iw->iw_firstl; iter != NULL; iter = next) {
		next = iter->al_next;

		strcpy(b, iter->al_text);
		free(iter->al_text);
		free(iter);

		while (next != NULL &&
		    !strncmp(next->al_text, contm, INDENT)) {
			register struct aline *n;

			p = next->al_text + INDENT;
			strcat(b, p);
			free(next->al_text);
			n = next;
			next = next->al_next;
			free(n);
		}

		iscont = 0;

		while (b[0] != 0) {
			for (lastws = 0, size = 0;
			    ((iscont) ? size+INDENT : size) < newwidth-2 &&
			    b[size] != 0; size++)
				if (isspace(b[size]) || ispunct(b[size]))
					lastws = size;
			if (!isspace(b[size]) && b[size] != 0 && lastws > 0)
				size = lastws;
			
			if (iscont) {
				newl = chkmem(malloc((unsigned)INDENT+size+1));
				strcpy(newl, contm);
				memcpy(newl+INDENT, b, (unsigned)size);
				newl[size+INDENT] = 0;
			}
			else {
				newl = chkmem(malloc((unsigned)size+1));
				memcpy(newl, b, (unsigned)size);
				newl[size] = 0;
			}

			new = chkmem(calloc(sizeof *new, 1));
			new->al_text = newl;

			if (newanchor == NULL)
				newanchor = newlast = new;
			else {
				new->al_prev = newlast;
				newlast->al_next = new;
				newlast = new;

				/* crop backscroll if it gets too long */
				if (++nlines > BACKSCROLL) {
					int n;
					struct aline *al, *li;

					for (n = 0; n < BACKSCROLL/4; n++,
					    nlines--) {
						li = newanchor;
						free(li->al_text);
						li = li->al_next;
						free(newanchor);
						li->al_prev = NULL;
						newanchor = li;
					}

					li = newanchor->al_next;
					free(newanchor->al_text);
					free(newanchor);
					al = chkmem(calloc(sizeof *al, 1));
					al->al_text = chkmem(Strdup(top));
					al->al_prev = NULL;
					al->al_next = li;
					newanchor = li->al_prev = al;
				}
			}

			l = strlen(b) - size;
			memmove(b, b+size, (unsigned)l);
			b[l] = 0;
			iscont = 1;
		}
	}

	iw->iw_firstl = newanchor;
	iw->iw_lastl = iw->iw_line = newlast;
	iw->iw_match = iw->iw_savtop = NULL;
}

/*
 * Reformat all backscroll buffers and refresh display.
 */
static void
reformat_all()
{
	struct ircwin *iw;
	struct ircpage *ipg;

	for (ipg = pages_head.cqh_first; ipg != (struct ircpage *) &pages_head;
	    ipg = ipg->ipg_entries.cqe_next)
		for (iw = ipg->ipg_iwa; iw != NULL; iw = iw->iw_next)
			reformat_backscroll(iw, t_columns);
}

/*
 * Create new page, make root window and switch to it.
 */
void
ipg_create()
{
	struct ircpage *ipg;

	if (numpages >= MAXPAGES) {
		iw_printf(COLI_TEXT, "%syou can only have up to %d pages\n",
			ppre, MAXPAGES);
		return;
	}

	if (cpage != NULL) {
		cpage->ipg_iwa = iwa;
		cpage->ipg_iwc = iwc;
	}
	ipg = chkmem(malloc(sizeof *ipg));
	cpage = ipg;
	iwa = iwc = NULL;
	newircwin();

	iwc->iw_type = IMT_CTRL | IMT_PMSG;

	ipg->ipg_iwa = iwa;
	ipg->ipg_iwc = iwc;
	ipg->ipg_upd = 0;
	CIRCLEQ_INSERT_TAIL(&pages_head, ipg, ipg_entries);
	otherpage = cpageno;
	cpageno = numpages++;
	set_prompt(NULL);
	repinsel();
}

/*
 * Delete current page.
 */
void
ipg_delete()
{
	struct ircpage *ipg = cpage;
	int newpg;

	if (iwa == NULL || (iwa->iw_next == NULL && iwa->iw_ch == NULL)) {
		if (ipg->ipg_entries.cqe_prev !=
		    (struct ircpage *) &pages_head)
			newpg = cpageno - 1;
		else if (ipg->ipg_entries.cqe_next !=
		    (struct ircpage *) &pages_head)
			newpg = cpageno + 1;
		else {
			iw_printf(COLI_TEXT, "%sCannot remove the only page\n",
				ppre);
			return;
		}

		/* Delete window */
		if (iwa != NULL) {
			if (iwa->iw_type & IMT_DEBUG)
				debugwin--;
			tty_rdestroy(iwc->iw_reg);
			del_iwin(iwa);
		}

		ipg_switchto(newpg, 1);
		CIRCLEQ_REMOVE(&pages_head, ipg, ipg_entries);
		free(ipg);
		numpages--;
		set_prompt(NULL);
		repinsel();
	}
	else
		iw_printf(COLI_TEXT, "%sThis page is not empty, cannot \
delete\n", ppre);
}

/*
 * Switch to specified page.  Returns 1 if switch was successful, 0 otherwise.
 */
int
ipg_switchto(pageno, silent)
	int pageno, silent;
{
	struct ircpage *ipg;
	int pns = pageno;

	for (ipg = pages_head.cqh_first;
	    ipg != (struct ircpage *) &pages_head && pageno > 0;
	    ipg = ipg->ipg_entries.cqe_next, pageno--)
		;
	if (pageno == 0 && ipg != (struct ircpage *) &pages_head) {
		ipg_switchtopg(ipg);
		set_prompt(NULL);
		otherpage = cpageno;
		cpageno = pns;
		ipg->ipg_upd = 0;
		return 1;
	}	
	else {
		if (!silent)
			iw_printf(COLI_TEXT, "%s%d: no such page\n", ppre, pns);
		elrefr(1);
		return 0;
	}
}

static void
ipg_switchtopg(ipg)
	struct ircpage *ipg;
{
	cpage->ipg_iwa = iwa;
	cpage->ipg_iwc = iwc;
	iwa = ipg->ipg_iwa;
	iwc = ipg->ipg_iwc;
	cpage = ipg;
}

int
ipg_getpageno()
{
	return cpageno;
}

int
ipg_lastpageno()
{
	return numpages-1;
}

/*
 * Display a status line, indicating the current page.
 */
void
ipg_pgline()
{
	char pline[MAXPAGES*8+1];
	char t[16];
	int i;
	struct ircpage *ipg;

	pline[0] = 0;

	for (i = 0, ipg = pages_head.cqh_first; i < numpages &&
	    ipg != (struct ircpage *) &pages_head;
	    i++, ipg = ipg->ipg_entries.cqe_next) {
		if (ipg != cpage)
			sprintf(t, "-%d-", i);
		else
			sprintf(t, "[%d]", i);
		strcat(pline, t);
	}
	tty_gotoxy(0, t_lines-1);
	tty_addbuff(tc_bold);
	tty_printf(NULL, "Pages: %s", pline);
	tty_addbuff(tc_noattr);
	tty_addbuff(tc_clreol);
	tty_flush();
}

/*
 * Set (if p >= 0) and get otherpage.
 */
int
ipg_setget_otherpg(p)
	int p;
{
	if (p > -1)
		otherpage = p;
	return otherpage;
}

/*
 * Return a char string containing the numbers of unviewed pages with new
 * text.
 */
static void
ipg_get_updatelist(pglist)
	char *pglist;
{
	struct ircpage *ipg;
	int i, n = 1;
	
	for (i = 0, ipg = pages_head.cqh_first;
	    ipg != (struct ircpage *) &pages_head;
	    ipg = ipg->ipg_entries.cqe_next, i++)
		if (ipg->ipg_upd)
			pglist[n++] = i + '0';
	if (n > 1) {
		pglist[0] = '*';
	    	pglist[n] = 0;
	}
	else
		pglist[0] = 0;
}

/*
 * Returns 1 if status of unviewed pages with new text has changed, 0
 * otherwise.
 */
static int
ipg_check_updatelist(force_update)
	int force_update;
{
	static long lastpupd = 0;
	long pupd = 0;
	struct ircpage *ipg;
	int i;
	
	for (i = 0, ipg = pages_head.cqh_first;
	    ipg != (struct ircpage *) &pages_head;
	    ipg = ipg->ipg_entries.cqe_next, i++)
		if (ipg->ipg_upd)
			pupd |= 1 << i;
	if (force_update)
		return pupd != 0;
	if (pupd != lastpupd) {
		lastpupd = pupd;
		return 1;
	}
	return 0;
}

/*
 * Check update list and induce status line update if required.
 * Called periodically every minute.
 */
void
ipg_updatechk()
{
	atomic_view_ipg_update = ipg_check_updatelist(0);
}


/*
 * Change xterm's title to reflect current connection status.
 */
void
xterm_settitle()
{
	char titlestring[100];

	if (check_conf(CONF_XTERMT)) {
		sprintf(titlestring, "TIRC:  %s on ", nick);
		strncat(titlestring, on_irc ? srvnm : "<not connected>", 60);
		tty_puts("\033]2;");
		tty_puts(titlestring);
		tty_puts("");
		tty_flush();
	}
}

/*
 * Find alternate channel where nname is on and which is displayed on the
 * current page.  Returns channel if one exists, NULL otherwise.
 */
struct channel *
userchan_cpage(nname)
	char *nname;
{
	struct ircpage *ipg;
	struct channel *found;
	struct ircwin *w;
	struct channel *ch;

	for (ipg = pages_head.cqh_first; ipg != (struct ircpage *)&pages_head;
	    ipg = ipg->ipg_entries.cqe_next)
	    	for (w = iwa; w != NULL && w->iw_flags & IW_USED;
		    w = w->iw_next)
			for (ch = w->iw_ch; ch != NULL; ch = ch->ch_wnext)
			    if (getfromucache(nname, ch, &found, 0) != NULL)
				return found;
	return NULL;
}

/*
 * Strip attribute characters (^B, ^C, ^_ etc.) from the log line since
 * they shouldn't be in the log file.
 */
static char *
iwlog_strip_attribs(s)
	register char *s;
{
	register char *n = s;
	char *sv = s;

	while (*s != '\0') {
		switch (*s) {
		case '':	/*FALLTHROUGH*/
		case '':	/*FALLTHROUGH*/
		case '':	/*FALLTHROUGH*/
		case '':	/*FALLTHROUGH*/
			s++;
			break;
		case '':
			while (*s != '\0' && *s != ';')
				s++;
			if (*s == ';')
				s++;
			break;
		default:
			*n++ = *s++;
			break;
		}
	}
	*n = '\0';
	return sv;
}

/*
 * Set debug mode in first window, used by main.c when the -d command
 * line option is recognized.
 */
void
firstdebug()
{
	int m = iwcmode(-1);

	m |= IMT_DEBUG;
	iwcmode(m);

	debugwin++;
}

/*
 * Returns 1 if screen layer has been initialized, 0 otherwise.
 */
int
screen_setup_p()
{
	return screen_is_setup;
}

static char	*iwb_buf;
static int	iwb_size;
static int	iwb_len;
static int	iwb_colorhint;

/*
 * Buffered iw_printf() for composing long messages instead of several
 * unrelated iw_printf()s (which don't get paginated properly).
 * The latest colorhint stored in the buffer is of relevance.
 */
int
/*VARARGS2*/
#ifdef	__STDC__
iw_buf_printf(int colorhint, const char *fmt, ...)
#else
iw_buf_printf(colorhint, fmt, va_alist)
	int colorhint;
	char *fmt;
	va_dcl
#endif
{
	va_list ap;
	char *lbuf;
	int ln;

#ifdef	__STDC__
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
	lbuf = chkmem(malloc(vsnprintf_buffer_size));

	if (iwb_buf == NULL) {
		iwb_buf = chkmem(malloc(vsnprintf_buffer_size));
		*iwb_buf = '\0';
		iwb_size = vsnprintf_buffer_size;
		iwb_len = 0;
	}

	for (;;) {
		ln = vsnprintf(lbuf, vsnprintf_buffer_size, fmt, ap);
		if (ln >= vsnprintf_buffer_size) {
			free(lbuf);
			vsnprintf_buffer_size += BUFSZ;
			lbuf = chkmem(malloc(vsnprintf_buffer_size));
		}
		else {
			if (vsnprintf_buffer_size > 4 * BUFSZ)
				/* pretty large; probably not needed for
				 * further vsnprintfs; reset. */
				vsnprintf_buffer_size = BUFSZ;
			break;
		}
	}
	iwb_colorhint = colorhint;
	va_end(ap);
	
	if (vsnprintf_buffer_size > iwb_size) {
		iwb_buf = chkmem(realloc(iwb_buf, vsnprintf_buffer_size));
		iwb_size = vsnprintf_buffer_size;
	}
	if (iwb_len+ln >= vsnprintf_buffer_size)
		iw_buf_flush();
	iwb_len += ln;
	strcat(iwb_buf, lbuf);

	if (iwb_len == 0) {
		free(iwb_buf);
		iwb_size = 0;
	}
	else if (iwb_len < 4 * BUFSZ && iwb_size > 4 * BUFSZ) {
		/* reset to reasonable size */
		iwb_buf = chkmem(realloc(iwb_buf, 4 * BUFSZ));
		iwb_size = 4 * BUFSZ;
	}

	free(lbuf);
	return ln;
}

/*
 * Explicitly flush the iw_buf_printf() buffer.
 */
void
iw_buf_flush()
{
	if (iwb_buf != NULL && iwb_len > 0) {
		struct iw_msg m;

		m.iwm_text = iwb_buf;
		m.iwm_type = IMT_CTRL;	/* not IMT_FMT formatted */
		m.iwm_chn = NULL;
		m.iwm_chint = iwb_colorhint;
		dispose_msg(&m);
		iwb_len = 0;
		*iwb_buf = '\0';
	}
}

/*
 * Enable/display display update.
 */
void
en_disp(en)
	int en;
{
	dodisplay = en;
}

