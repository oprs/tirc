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
static char rcsid[] = "$Id: input.c,v 1.36 1998/02/20 21:01:44 token Exp $";
#endif

#include "tirc.h"

#ifdef	HAVE_STRING_H
#include <string.h>
#elif defined(HAVE_MEMORY_H)
#include <memory.h>
#endif
#ifdef	HAVE_CTYPE_H
#include <ctype.h>
#endif
#ifdef	HAVE_SIGNAL_H
#include <signal.h>
#elif defined(HAVE_SYS_SIGNAL_H)
#include <sys/signal.h>
#endif

#include "compat.h"
#include "tty.h"
#include "colour.h"

#define ESC		0x1b

static int	forww __P((void));
static int	backw __P((void));
static int	endw __P((void));
static int	forwW __P((void));
static int	backW __P((void));
static int	endW __P((void));
static void	addtohist __P((unsigned char *));
static void	delfromhist __P((void));
static void	killtrailws __P((void));
static void	idlecheck __P((void));
static char	*lastnick __P((unsigned char **));

extern char	lastmsg[MSGNAMEHIST][NICKSZ+2];
extern int	lastmsgp;
extern char	cmdch;
extern int	pasting;	/* cmd.c */

struct aline	*ela, *elc, *ell;
int	lsz, lptr, history;

static int	del_from, del_to;
static int	mode, del, chg, yank, mark, gotomark, find, dodel, literal,
		replone;
static unsigned char *line;
static struct p_queue *pq_idle = NULL;
static time_t	laststroke;
static int	in_lastmsg;	/* whether TAB can produce a reply */
static int	lastmsgcomp;
static int	freehist_registered;
static int	cx_pending;
static int	pgnum_pending;
static char	*undobuf;

static char	*cutbuf;
static int	cutsize;

static char	casetbl['z'-'A'];	/* table for toggling case */
static int	marktbl[1+'z'-'a'];

/*
 * Reset all variables for the line input.
 */
void
reset_input()
{
	extern sig_atomic_t atomic_idleexceed;
	int i;

	del_from = del_to = lsz = lptr = del = chg = find = history = 0;
	dodel = literal = cx_pending = pgnum_pending = replone = 0;
	ela = elc = ell = NULL;
	if (NULL != line)
		free(line);
	line = chkmem(malloc(MAXINPUT+1));
	mode = MODE_INSERT;
	showmode(mode);
	laststroke = time(NULL);
	pq_del(pq_idle);
	atomic_idleexceed = 0;
	pq_idle = pq_add(idlecheck);
	elhome();

	if (!freehist_registered) {
		Atexit(free_history);
		freehist_registered = 1;
	}

	if (undobuf == NULL)
		undobuf = chkmem(malloc(MAXINPUT+1));
	if (cutbuf == NULL)
		cutbuf = chkmem(malloc(MAXINPUT+1));
	cutsize = 0;

	/* Creating case toggle table */
	for (i = 'A'; i <= 'z' ; i++) {
		if (isupper(i))
			casetbl[i-'A'] = tolower(i);
		else if (islower(i))
			casetbl[i-'A'] = toupper(i);
		else
			casetbl[i-'A'] = i;
	}

	/* clearing mark table */
	for (i = 0; i <= 'z'-'a'; i++)
		marktbl[i] = -1;
}

/*
 * Process user input.  This implements an editor line with commands
 * and handling similar to the vi(1) text editor.  For emacs users,
 * some basic stuff is supported.  Don't count the gotos, please.
 */
void
parsekey()
{
	int t, c;
	static int undolsz, undolptr;
	static int omode;
	char undotmp[MAXINPUT+1];

	c = tty_getch();
	laststroke = time(NULL);

	if (mark) {
		/* Pending set named mark */
		mark = 0;

		if (c >= 'a' && c <= 'z')
			marktbl[c] = lptr;
		else
			dobell();
		yank = chg = del = 0;
		return;
	}

	if (gotomark) {
		/* Pending go to named mark */
		gotomark = 0;

		if (c >= 'a' && c <= 'z' && marktbl[c] > 0
		    && marktbl[c] < lsz) {
			int lptrsv = lptr;

			lptr = marktbl[c];
			if (yank || chg || del) {
				dodel = 1;
				if (lptrsv <= lptr) {
					del_from = lptrsv;
					del_to = lptr-1;
				}
				else {
					del_from = lptr;
					del_to = lptrsv-1;
				}
				goto check4del;
			}
			else
				update_eline(line, lsz, lptr, 0);
		}
		else {
			yank = chg = del = 0;
			dobell();
		}
		return;
	}

	if (cx_pending) {
		/* Pending Ctrl-X */
		cx_pending = 0;

		switch (c) {
		case 'o':		/* switch to next window */
			switchwin(1);
			elrefr(1);
			return;
		case 'O':		/* switch to previous window */
			switchwin(-1);
			elrefr(1);
			return;
		case '0':		/* delete this window */ 
			delircwin();
			elrefr(1);
			return;
		case '2':		/* create new window */
			newircwin();
			elrefr(1);
			iwcmode(iwcmode(-1) | IMT_CTRL | IMT_PMSG);
			return;
		case 'b':		/* switch to page */
			pgnum_pending = 1;
			ipg_pgline();
			return;
		case 'n':		/* next page */
			{
			int pg = ipg_getpageno();

			if (ipg_switchto(pg + 1, 1))
				repinsel();
			else if (pg > 0) {
				ipg_switchto(0, 0);
				repinsel();
			}
			return;
			}
		case 'p':		/* previous page */
			if (ipg_switchto(ipg_getpageno() - 1, 1))
				repinsel();
			else {
				ipg_switchto(ipg_lastpageno(), 0);
				repinsel();
			}
			return;
		case K_CTRL('X'):	/* other page */
			if (ipg_switchto(ipg_setget_otherpg(-1), 1))
				repinsel();
			else {
				ipg_switchto(0, 0);
				repinsel();
			}
			return;
		default:
			dobell();
			elrefr(1);
			return;
		}
	}

	if (pgnum_pending) {
		/* Pending input for ^X-b */

		pgnum_pending = 0;

		if (!isdigit(c)) {
			dobell();
			elrefr(1);
			return;
		}
		if (ipg_switchto(c - '0', 0))
		    repinsel();
		return;
	}

	if (literal == MODE_OSTRIKE)
		goto osliteral;
	if (literal == MODE_INSERT)
		goto inliteral;

	if (c != '\t')
		in_lastmsg = 0;

	if (c == tck_reprint)
		goto reprint;
	if ((c == tck_kill) && (mode == MODE_OSTRIKE || mode == MODE_INSERT)) {
		/* Kill to start of line. */
		killtrailws();
		memcpy(undobuf, line, (unsigned)lsz);
		undolsz = lsz;
		undolptr = lptr;
		if (lptr < lsz-1) {
			memmove(line, line + lptr, (unsigned)lsz-lptr);
			lsz = lsz - lptr;
			lptr = 0;
		}
		else
			lsz = lptr = 0;
		line[lsz] = 0;
		update_eline(line, lsz, lptr, 0);
		return;
	}

	switch (c) {
	case K_CTRL('W'):	/* next window if in command mode */
		if (mode == MODE_COMMAND) {
			switchwin(1);
			return;
		}
		else {
			del = 1;
			chg = yank = 0;
			goto word_back;
		}
	case K_CTRL('N'):	/* turin */
		switchwin(1);
		elrefr(1);
		return;
	case K_CTRL('T'):	/* prev window if in command mode */
		if (mode == MODE_COMMAND)
			switchwin(-1);
		return;
	case K_CTRL('X'):	/* emacs ^X command */
		tty_gotoxy(0, t_lines-1);
		tty_printf(NULL, "%sWindow: ^X-o, ^X-O, ^X-2, ^X-0;  Page: \
^X-b, ^X-n, ^X-p, ^X-^X%s%s", tc_bold, tc_noattr, tc_clreol);
		tty_flush();
		cx_pending = 1;
		return;
	case K_CTRL('F'):	/* scroll down window by a screen */
	case K_PGDN:
	case K_S_PGDN:
	case K_S2_PGDN:
	case K_S3_PGDN:
		iw_scroll(1000, 1);
		return;
	case K_CTRL('U'):	/* unlike vi, this is equ to ctrl-b */
	case K_CTRL('B'):	/* scroll up window by a screen */
	case K_PGUP:
	case K_S_PGUP:
	case K_S2_PGUP:
	case K_S3_PGUP:
		iw_scroll(1000, -1);
		return;
	case K_CTRL('G'):	/* go to end of backscroll */
		iw_scroll(0, 0);
		return;
	case K_CTRL('L'):
	reprint:		/* force screen redraw */
		repinsel();
		return;
	case K_UP:		/* cursor keys */
	case K_S_UP:
	case K_S2_UP:
		goto histprev;
	case K_DN:
	case K_S_DN:
	case K_S2_DN:
		goto histnext;
	case K_LEFT:
	case K_S_LEFT:
	case K_S2_LEFT:
		goto cursleft;
	case K_RIGHT:
	case K_S_RIGHT:
	case K_S2_RIGHT:
		goto cursright;
	case K_DEC_FIND:
		search(BACKWARD);
		return;
	case K_CTRL('D'):	/* unlike vi, this is emacish delete-char */
		goto delcurrent;
	case K_CTRL('E'):	/* (emacsish) goto after end of line */
		if (lsz)
			lptr = lsz-1;
		if (!isspace(line[lptr]) && lsz < MAXINPUT) {
			line[lsz] = ' ';
			lptr++;
			lsz++;
		}
		mode = MODE_INSERT;
		update_eline(line, lsz, lptr, 0);
		goto skip_mode_checks;
	case K_CTRL('A'):	/* (emacsish) goto beginning of line */
		lptr = 0;
		mode = MODE_INSERT;
		update_eline(line, lsz, lptr, 0);
		goto skip_mode_checks;
	case K_CTRL('K'):	/* (emacsish) delete to end of line */
		goto deltoeol;
	case K_CTRL('P'):	/* toggle paste mode */
		del = chg = find = 0;
		pastemode();
		return;
	case '\r':
	case '\n':		/* line done */
		linedone(1);
		return;
	case '\t':
		/*
		 * Do not interpret TAB if paste mode is active.
		 * TAB is inserted as a single space.
		 */
		if (pasting) {
			c = '\040';
			goto inliteral;
		}
		/*
		 * TAB faciliates responding to the last message you
		 * received.  If pressed somewhere else, it tries to
		 * complete the current word as a nickname of the
		 * current channel.
		 */
		if (!in_lastmsg)	
			lastmsgcomp = lastmsgp;
		if (lsz == 0 || in_lastmsg) {
			if (--lastmsgcomp < 0)
				lastmsgcomp = MSGNAMEHIST-1;

			if (strlen(lastmsg[lastmsgcomp]) > 0)
				sprintf(line, "%cmsg %s ", cmdch,
					lastmsg[lastmsgcomp]);
			else
				sprintf(line, "%cmsg ", cmdch);

			lptr = lsz = strlen(line);
			mode = MODE_INSERT;
			update_eline(line, lsz, lptr, 0);
			in_lastmsg = 1;
		}
		else {
			unsigned char *xpand, *w;
			struct channel *ch;
			unsigned char *lw;

			if (mode == MODE_COMMAND) {
				dobell();
				return;
			}
			del = chg = find = 0;
			ch = iw_getchan();

			if (ch != NULL) {
				register int xlen;

				w = lastnick(&lw);
				xpand = complete_from_ucache(ch, w,
					(lw == skipws(line)));
				if (xpand == NULL) {
					dobell();
					return;
				}
				if (lsz + strlen(xpand) >= MAXINPUT) {
					dobell();
					return;
				}
				xlen = strlen(xpand);
				memmove(lw+xlen, line+lptr,
					(unsigned)lsz-(lptr-xlen));
				memmove(lw, xpand, (unsigned)xlen);
				lsz += xlen - strlen(w);
				lptr = (lw+xlen) - line;
				update_eline(line, lsz, lptr, 0);
				if (w != NULL)
					free(w);
			}
			else
				dobell();
		}
		return;
	}

	if (K_ISFKEY(c)) {
		del = chg = find = 0;
		dobell();
		return;
	}

	/* Find character? */
	if (find > 0) {
		int oldptr = lptr;

		del_from = lptr;
		while (++lptr < lsz-1 && line[lptr] != c)
			;
		if (line[lptr] != c)
			lptr = oldptr;
		del_to = lptr;
		find = 0;
		if ((del || chg) && (del_from != del_to))
			dodel = 1;
		del = 0;
		if (yank)
			lptr = oldptr;
		else
			update_eline(line, lsz, lptr, 0);
		goto check4del;
	}
	else if (find < 0) {
		int oldptr = lptr;

		if (!lptr)
			goto check4del;
		del_to = lptr-1;
		while (--lptr > 0 && line[lptr] != c)
			;
		if (line[lptr] != c)
			lptr = oldptr;
		del_from = lptr;
		find = 0;
		if (del || chg)
			dodel = 1;
		if (yank)
			lptr = oldptr;
		else
			update_eline(line, lsz, lptr, 0);
		goto check4del;
	}

	if (mode == MODE_INSERT) {
		if (c == tck_lnext) {
			literal = MODE_INSERT;
			return;
		}
		if (c == tck_erase || c == '\x7f' || c == '\x8')
			goto delbackc;
		switch (c) {
		case ESC:
 			mode = omode = MODE_COMMAND;
			showmode(mode);
			if (lptr && lptr < lsz-1)
				lptr--;
			else
				killtrailws();
			update_eline(line, lsz, lptr, 0);
			break;
		default:
		inskey:
			if (!isprint(c)) {
				dobell();
				break;
			}
		inliteral:
			literal = 0;
			if (lsz >= MAXINPUT) {
				dobell();
				break;
			}
			memmove(line+lptr+1, line+lptr, (unsigned)lsz-lptr);
			line[lptr++] = c;
			lsz++;
			update_eline(line, lsz, lptr, 0);
			break;
		}
	}

	else if (mode == MODE_OSTRIKE) {
		if (c == tck_lnext) {
			literal = MODE_OSTRIKE;
			return;
		}
		switch (c) {
		case ESC:
			mode = omode = MODE_COMMAND;
			showmode(mode);
			if (lptr)
				lptr--;
			update_eline(line, lsz, lptr, 0);
			break;
		default:
			if (!isprint(c)) {
				dobell();
				break;
			}
		osliteral:
			literal = 0;
			if (lptr < lsz) {
				line[lptr] = c;
				if (!replone)
					lptr++;
				replone = 0;
				update_eline(line, lsz, lptr, 0);
				break;
			}
			else {
				mode = MODE_INSERT;
				goto inskey;
			}
		}
	}

	else if (mode == MODE_COMMAND) {
		if (c == tck_erase || c == '\x7f' || c == '\x8')
			goto cursleft;

		switch (c) {
		case 'h':	/* cursor left */
		cursleft:
			if (lptr) {
				if (yank) {
					*cutbuf = line[lptr-1];
					cutsize = 1;
					yank = del = 0;
					return;
				}
				if (del)
					goto delbackc;
				else
					lptr--;
			}
			else
				dobell();
			del = chg = yank = 0;
			update_eline(line, lsz, lptr, 0);
			break;
		case 'l':	/* cursor right */
		case ' ':
		cursright:
			if (lptr < lsz-1) {
				if (yank) {
					*cutbuf = line[lptr+1];
					cutsize = 1;
					yank = del = 0;
					return;
				}
				if (del)
					goto delcurrent;
				else
					lptr++;
			}
			else
				dobell();
			del = chg = yank = 0;
			update_eline(line, lsz, lptr, 0);
			break;
		case 'a':	/* insert after current pos */
			if (lsz) {
				lptr++;
				if (lptr > lsz)
					lsz = lptr+1;
			}
			mode = MODE_INSERT;
			del = chg = yank = 0;
			update_eline(line, lsz, lptr, 0);
			break;
		case 'A':	/* append at end of line */
			if (lsz)
				lptr = lsz++;
			line[lptr] = ' ';
			mode = MODE_INSERT;
			del = chg = yank = 0;
			update_eline(line, lsz, lptr, 0);
			break;
		case 'i':	/* insert at current pos */
			mode = MODE_INSERT;
			del = chg = yank = 0;
			update_eline(line, lsz, lptr, 0);
			break;
		case 'I':	/* insert before line */
			lptr = 0;
			mode = MODE_INSERT;
			del = chg = yank = 0;
			update_eline(line, lsz, lptr, 0);
			break;
		case 'R':	/* replace (overstrike) mode */
			mode = MODE_OSTRIKE;
			del = chg = yank = 0;
			break;
		case 'r':	/* replace this character */
			del = chg = yank = 0;
			if (lptr > 0) {
				literal = MODE_OSTRIKE;
				replone = 1;
			}
			else {
				replone = literal = 0;
				dobell();
			}
			break;
		case '0':	/* move to position 0 */
			del_from = 0;
			del_to = lptr ? lptr-1 : 0;
			if (del || chg)
				dodel = 1;
			if (!yank) {
				lptr = 0;
				update_eline(line, lsz, lptr, 0);
			}
			break;
		case '$':	/* move to last position */
			del_from = lptr;
			del_to = (lsz) ? lsz-1 : 0;
			if (lsz && !yank)
				lptr = lsz-1;
			if (del || chg)
				dodel = 1;
			if (!yank)
				update_eline(line, lsz, lptr, 0);
			break;
		case 'W':	/* big word forward */
			del_to = endW();
			if (!yank)
				lptr = forwW();
			if (del || chg)
				dodel = 1;
			if (!yank)
				update_eline(line, lsz, lptr, 0);
			break;
		case 'w':	/* word forward */
			del_to = endw();
			if (!yank)
				lptr = forww();
			if (del || chg)
				dodel = 1;
			if (!yank)
				update_eline(line, lsz, lptr, 0);
			break;
		case 'B':	/* big word backward */
			del_to = (lptr) ? lptr-1 : 0;
			del_from = backW();
			if (!yank)
				lptr = del_from;
			if (del || chg)
				dodel = 1;
			if (!yank)
				update_eline(line, lsz, lptr, 0);
			break;
		case 'b':	/* word backward */
		word_back:
			del_to = (lptr) ? lptr-1 : 0;
			del_from = backw();
			if (!yank)
				lptr = del_from;
			if (del || chg)
				dodel = 1;
			if (!yank)
				update_eline(line, lsz, lptr, 0);
			break;
		case 'E':	/* next big word end */
			del_to = endW();
			if (!yank)
				lptr = del_to;
			if (del || chg)
				dodel = 1;
			if (!yank)
				update_eline(line, lsz, lptr, 0);
			break;
		case 'e':	/* next word end */
			del_to = endw();
			if (!yank)
				lptr = del_to;
			if (del || chg)
				dodel = 1;
			if (!yank)
				update_eline(line, lsz, lptr, 0);
			break;
		case 'x':	/* delete current char */
		delcurrent:
			if (lsz && lptr < lsz) {
				memcpy(undobuf, line, (unsigned)lsz);
				undolsz = lsz;
				undolptr = lptr;
				*cutbuf = line[lptr];
				cutsize = 1;
				memmove(line+lptr, line+lptr+1,
					(unsigned)--lsz-lptr);
				if (lptr == lsz && lsz)
					lptr = lsz-1;
				update_eline(line, lsz, lptr, 0);
			}

			del = chg = dodel = yank = 0;
			update_eline(line, lsz, lptr, 0);
			return;
		case 'X':	/* delete previous char */
		delbackc:
			if (lptr && lsz) {
				memcpy(undobuf, line, (unsigned)lsz);
				undolsz = lsz;
				undolptr = lptr;
				*cutbuf = line[lptr-1];
				cutsize = 1;
				memmove(line+lptr-1, line+lptr,
					(unsigned)lsz-lptr);
				lsz--;
				lptr--;
			}
			update_eline(line, lsz, lptr, 0);
			del = chg = dodel = yank = 0;
			return;
		case 'Y':	/* yank to end of line */
			yank = 1;
			goto deltoeol;
		case 'C':	/* change to end of line */
			mode = MODE_INSERT;
			/*FALLTHROUGH*/
		case 'D':	/* delete to end of line */
		deltoeol:
			memcpy(undobuf, line, (unsigned)lsz);
			undolsz = lsz;
			undolptr = lptr;

			if (lsz) {
				cutsize = lsz-lptr-1;
				if (!yank) {
					memcpy(cutbuf, line+lptr, cutsize);
					if (lptr)
						lsz = lptr--;
					else
						lsz = 0;
				}
			}
			if (mode == MODE_INSERT && !yank && lsz > 0) {
				line[lsz] = ' ';
				lptr++;
				lsz++;
			}
			if (!yank)
				update_eline(line, lsz, lptr, 0);
			del = chg = yank = 0;
			break;
		case 'y':
			yank = 1;
			goto delete_to_motion;
		case 'c':
			chg = 1;
			/*FALLTHROUGH*/
		case 'd':	/* set delete to motion or delete line. */
		delete_to_motion:
			if (!del) {
				del = 1;
				del_from = del_to = lptr;
				return;
			}
			else {
				del_from = 0;
				del_to = (lsz) ? lsz-1 : 0;
				dodel = 1;
			}
			break;
		case 'j':	/* next line in history */
		histnext:
			if (elc) {
				line[0] = 0;
				if (elc->al_next) {
					elc = elc->al_next;
					if (elc->al_text)
					    strcpy(line, elc->al_text);
					lsz = strlen(line);
					lptr = min(lptr, lsz);
					update_eline(line, lsz, lptr, 0);
				}
			}
			del = chg = yank = 0;
			break;
		case 'k':	/* previous line in history */
		histprev:
			if (elc) {
				line[0] = 0;
				if (elc->al_prev) {
					elc = elc->al_prev;
					if (elc->al_text)
					    strcpy(line, elc->al_text);
					lsz = strlen(line);
					lptr = min(lptr, lsz);
					update_eline(line, lsz, lptr, 0);
				}
			}
			del = chg = yank = 0;
			break;
		case 'U':
		case 'u':	/* Undo last major change */
			memcpy(undotmp, line, (unsigned)lsz);
			memcpy(line, undobuf, (unsigned)undolsz);
			memcpy(undobuf, undotmp, (unsigned)lsz);
			t = lsz;
			lsz = undolsz;
			undolsz = t;
			t = lptr;
			lptr = undolptr;
			undolptr = t;
			update_eline(line, lsz, lptr, 0);
			del = chg = yank = 0;
			break;
		case '_':
		case '^':	/* go to first non-whitespace char */
			{
			int lptrsv = lptr;

			t = lptr;
			lptr = 0;
			while (lptr < lsz && isspace(line[lptr]))
				lptr++;
			if (del || chg) {
				dodel = 1;
				del_from = min(t, lptr);
				del_to = max(t, lptr);
				if (del_to > 0)
					del_to--;
			}
			if (yank)
				lptr = lptrsv;
			else 
				update_eline(line, lsz, lptr, 0);
			}
			break;
		case 'f':	/* find forward */
			find = 1;
			break;
		case 'F':	/* find backward */
			find = -1;
			break;
		case '/':	/* searching function (forward) */
			search(FORWARD);
			return;
		case '?':	/* searching function (backward) */
			search(BACKWARD);
			return;
		case 'm':	/* set mark a-z */
			mark = 1;
			yank = chg = del = 0;
			return;
		case '`':
			/*FALLTHROUGH*/
		case '\'':	/* goto mark a-z */
			gotomark = 1;
			return;
		case 'n':	/* search next match */
			findre(FORWARD);
			return;
		case 'N':	/* search next match (reverse) */
			findre(BACKWARD);
			return;
		case '~':	/* toggle case, move one forward */
			if (line[lptr] >= 'A' && line[lptr] <= 'z')
				line[lptr] = casetbl[line[lptr]-'A'];
			if (lptr < lsz)
				lptr++;
			update_eline(line, lsz, lptr, 0);
			del = chg = yank = 0;
			return;
		case ':':	/* insert mode & add cmdch to beginning */
			del = chg = yank = 0;
			mode = MODE_INSERT;
			c = cmdch;
			lptr = 0;
			goto inliteral;
			/*NOTREACHED*/
		case 'o': /*FALLTHROUGH*/
		case 'O':	/* options editor */
			options_dialog();
			return;
		case 'p':	/* paste cut buffer after current char */
			if (lsz == 0)
				goto pastebefore;
			if (lsz+cutsize < MAXINPUT && cutsize > 0) {
				memcpy(undobuf, line, (unsigned)lsz);
				undolsz = lsz;
				undolptr = lptr;
				if (lptr < lsz-1)
					memmove(line+lptr+cutsize+1,
						line+lptr+1,
						(unsigned)lsz-lptr);
				memcpy(line+lptr+1, cutbuf, cutsize);
				lptr = lptr+cutsize;
				lsz = lsz+cutsize;
				update_eline(line, lsz, lptr, 0);
				del = chg = yank = 0;
			}
			else
				dobell();
			return;
		case 'P':	/* paste cut buffer before current char */
		pastebefore:
			if (lsz+cutsize < MAXINPUT && cutsize > 0) {
				memcpy(undobuf, line, (unsigned)lsz);
				undolsz = lsz;
				undolptr = lptr;
				memmove(line+lptr+cutsize, line+lptr,
					(unsigned)lsz-lptr);
				memcpy(line+lptr, cutbuf, cutsize);
				lptr = lptr+cutsize;
				lsz = lsz+cutsize;
				update_eline(line, lsz, lptr, 0);
				del = chg = yank = 0;
			}
			else
				dobell();
			return;
		case ESC:
			showmode(mode);
			elhome();
			/*FALLTHROUGH*/
		default:
			dobell();
			del = chg = yank = 0;
			break;
		}

	check4del:
		if (dodel) {
			/*
			 * Delete or change to motion.
			 * We make a backup copy that can be restored with
			 * the next u (undo) command.
			 */
			int i, j = 0;
			char string[MAXINPUT+1];
			int need_lastpos_adjust;

			if (!yank) {
				need_lastpos_adjust = (del_to == lsz-1);
					
				memcpy(undobuf, line, (unsigned)lsz);
				undolsz = lsz;
				undolptr = lptr;
			}

			cutsize = 0;

			if (!yank) {
				for (i = 0; i < lsz; i++)
					if (i < del_from || i > del_to)
						string[j++] = line[i];
					else
						cutbuf[cutsize++] = line[i];
			}
			else {
				for (i = 0; i < lsz; i++)
					if (i >= del_from && i <= del_to)
						cutbuf[cutsize++] = line[i];
				del_to = -1;
				del = dodel = yank = 0;
				return;
			}

			string[j] = 0;
			lsz = j;
			memmove(line, string, (unsigned)j);
			lptr = (del_from) ? del_from : 0;
			if (lptr && lptr >= lsz && mode == MODE_COMMAND)
				lptr--;
			if (chg) {
				mode = MODE_INSERT;
				chg = 0;
				if (need_lastpos_adjust) {
					line[lsz] = ' ';
					lptr++;
					lsz++;
				}
			}
			update_eline(line, lsz, lptr, 0);
			del_to = -1;
			del = dodel = 0;
		}
	}

skip_mode_checks:
	if (mode != omode) {
		showmode(mode);
		elhome();
	}
	omode = mode;
}

/* Forward one word. */
static int
forww()
{
	register unsigned char *t = (unsigned char *) line + lptr;

	if (isspace(*t)) {
		while (((t-line) < lsz-1) && isspace(*t))
			t++;
		return t - line;
	}

	if (isalnum(*t))
		while (((t-line) < lsz-1) && (isalnum(*t) || *t == '_'))
			t++;
	else if (ispunct(*t))
		while (((t-line) < lsz-1) && ispunct(*t))
			t++;
	
	while (((t-line) < lsz-1) && isspace(*t))
		t++;

	return t - line;
}

/* Forward one big word. */
static int
forwW()
{
	unsigned char *t = line+lptr;
	char delim = 0;

	while ((t-line) < lsz-1) {
		if (isspace(*t)) {
			delim = 1;
			t++;
			continue;
		}
		if (!delim) {
			t++;
			continue;
		}
		return t-line;
	}
	return lsz-1;
}

/* Back one word. */
static int
backw()
{
	register unsigned char *t = line + lptr;

	if (t > line && *t == 0)
		t--;

	if (isspace(*t))
		while (t > line && isspace(*t))
			t--;
	else if (isalnum(*t))
		while (t > line+1 && (isalnum(*t) || *t == '_') &&
		    !isalnum(*(t-1)) && *(t-1) != '_')
			t--;
	else if (ispunct(*t))
		while (t > line+1 && ispunct(*t) && !ispunct(*(t-1)))
			t--;

	while (t > line && isspace(*t))
		t--;

	if (t == line+1)
		return 0;

	if (isalnum(*t))
		while (t > line && (isalnum(*(t-1)) || *(t-1) == '_'))
			t--;
	else if (ispunct(*t))
		while (t > line && ispunct(*(t-1)))
			t--;

	return t - line;
}

/* Back one big word. */
static int
backW()
{
	unsigned char *t = line + lptr;

	if (t > line+1 && ((isspace(*t) || isspace(*(t-1)))))
		t--;
	while (t > line && (isspace(*t)))
		t--;
	while (t > line && !isspace(*t))
		t--;
	if (isspace(*t))
		return ++t-line;
	return 0;
}

/* End of next word. */
static int
endw()
{
	register unsigned char *t = line + lptr;

	if (((t-line) < lsz-1))
		t++;

	if (isalnum(*t)) {
		while (((t-line) < lsz-1) && (isalnum(*(t+1)) || *(t+1) == '_'))
			t++;
		return t - line;
	}
	else if (ispunct(*t)) {
		while (((t-line) < lsz-1) && ispunct(*(t+1)))
			t++;
		return t - line;
	}
	
	while (((t-line) < lsz-1) && isspace(*t))
		t++;

	if (isalnum(*t))
		while (((t-line) < lsz-1) && (isalnum(*(t+1)) || *(t+1) == '_'))
			t++;
	else if (ispunct(*t))
		while (((t-line) < lsz-1) && ispunct(*(t+1)))
			t++;
	
	return t-line;
}

/* End of next big word. */
static int
endW()
{
	unsigned char *t = line + lptr;

	if ((t-line) < lsz-1 && ((isspace(*t) || isspace(*(t+1)))))
		t = line+forww();
	while (t < line+lsz-1 && !isspace(*t))
		t++;
	if (isspace(*t))
		t--;
	return t-line;
}

/*
 * Re-paints the editor line.
 */
void
elrefr(force_redraw)
	int force_redraw;
{
	if (NULL != line) {
		line[lsz] = 0;
		update_eline(line, lsz, lptr, force_redraw);
	} else
		update_eline("", lsz, lptr, force_redraw);
	showmode(mode);
	elhome();
}

/*
 * Move cursor to current position on editor line if the editor line
 * isn't empty.
 */
void
elhome()
{
	f_elhome();
	tty_flush();
}

void
f_elhome()
{
	move_eline(lptr);
}

static void
addtohist(ln)
	unsigned char *ln;
{
	struct aline *al;
	int i;

	if (ln == NULL)
		return;
	if (ela == NULL) {
		ela = elc = ell = al =
			chkmem(calloc(1, sizeof (struct aline)));
		al->al_text = chkmem(Strdup(ln));
		al->al_next = al->al_prev = NULL;
		return;
	}
	if (++history > HISTORY) {
#ifdef	DEBUGV
		iw_printf(COLI_TEXT, "DEBUG: chopping editor line history, \
history = %d\n", history);
#endif
		/* Delete 1/4 of the history buffer to get some space. */
		for (i = 0; i < HISTORY/4; i++, history--) {
			free(ela->al_text);
			al = ela->al_next;
			free(ela);
			ela = al;
		}
		ela->al_prev = NULL;
	}
	al = chkmem(calloc(1, sizeof (struct aline)));
	al->al_text = chkmem(Strdup(ln));
	al->al_prev = ell;
	ell->al_next = al;
	ell = elc = al;
}

static void
delfromhist()
{
	struct aline *l;

	if (ell == NULL)
		return;
	l = ell;
	ell = l->al_prev;
	if (ell)
		ell->al_next = NULL;
	free(l->al_text);
	free(l);
	history--;
}

/*
 * Store the line in the history buffer.  If exec is true, the line
 * is parsed and executed.
 */
void
linedone(exec)
	int exec;
{
	killtrailws();
	line[lsz] = 0;

	if (lsz > 0) {
		delfromhist();
		addtohist(line);
		addtohist("");
	}
	lsz = lptr = 0;
	update_eline(line, lsz, 0, 0);
	mode = MODE_INSERT;
	showmode(mode);
	elc = ell;
	del_to = -1;
	if (exec)
		cmdline(line);
}

/*
 * Delete all trailing whitespace on line.
 */
static void
killtrailws()
{
	while (lsz > 0 && line[lsz-1] == ' ')
		lsz--;
	if (lptr >= lsz)
		lptr = lsz-1;
	if (lptr < 0)
		lptr = 0;
}

/*
 * Check if client idle time exceeded and exit if yes.
 */
static void
idlecheck()
{
	time_t now = time(NULL);
	extern sig_atomic_t atomic_idleexceed;

	if (now - laststroke > IDLELIMIT)
		atomic_idleexceed = 1;
}

/*
 * Set editor line mode to m.
 * Use with caution and very rarely!
 */
void
el_setmode(m)
	int m;
{
	mode = m;
}

/*
 * Deletes the history and frees all memory connected to the history
 * list (for memory leak detection, at program exit).
 */
void
free_history()
{
	struct aline *al;
	int i;

	for (i = 0; i < history; i++) {
		free(ela->al_text);
		al = ela->al_next;
		free(ela);
		ela = al;
	}
}

/*
 * Return a pointer to the last nick/word before or up to the cursor.  The
 * word is extracted and stored in a storage with dynamically allocated
 * memory (caller must free memory on its own).
 * The pointer linep is set to the beginning of the word in the editor
 * line.
 * Returns NULL if not successful.
 */
static char *
lastnick(linep)
	unsigned char **linep;
{
	unsigned char *word, *upto;
	register unsigned char *p;

	if (lptr < 1)
		return NULL;
	upto = p = line+lptr;

	while (p > line && isnickch(*((char *)p-1)))
		(char *)p--;
	word = chkmem(malloc((unsigned)((char *)upto-(char *)p+1)));
	memcpy(word, p, (unsigned)(upto-p));
	word[upto-p] = 0;

	*linep = p;
	return word;
}

