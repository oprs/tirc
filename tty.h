/*-
 * tirc -- client for the Internet Relay Chat
 *
 *	Copyright (c) 1996, 1997, 1998
 *		Matthias Buelow.  All rights reserved.
 *
 *	See the file ``COPYRIGHT'' for the usage and distribution
 *	license and warranty disclaimer.
 *
 * $Id: tty.h,v 1.17 1998/02/22 19:04:49 mkb Exp $
 */

#ifndef TIRC_TTY_H
#define TIRC_TTY_H	1

#ifndef	__P
#ifdef	__STDC__
#define	__P(x)	x
#else
#define	__P(x)	()
#endif
#endif

#ifdef	HAVE_TERMCAP
#ifdef	HAVE_TERMCAP_H
#include <termcap.h>
#endif

#elif defined(HAVE_TERMLIB)
#if defined(HAVE_TERM_H) && defined(INCLUDE_TERM_H)
#include <term.h>
#endif

#elif defined(HAVE_CURSES)
#ifdef	HAVE_CURSES_H
#include <curses.h>
#endif
#if defined(HAVE_TERM_H) && defined(INCLUDE_TERM_H)
#include <term.h>
#endif

#elif defined(HAVE_NCURSES)
#ifdef	HAVE_NCURSES_H
#include <ncurses.h>
#elif defined(HAVE_CURSES_H)
#include <curses.h>
#endif
#ifdef	HAVE_TERM_H
#include <term.h>
#endif
#endif	/* HAVE_TERMCAP ... */

/* Size of buffer for pending writes to the terminal, flushed with tty_flush. */
#define	OUTBUFSIZE	2048

/*
 * We probably have no prototypes for the termcap functions.
 * If this is the case, declare them here.
 */
#if !defined(INCLUDE_TERM_H) && !defined(HAVE_TERMCAP_H)
int     tgetent __P((char *, char *));
int     tgetnum __P((char *));
char    *tgetstr __P((char *, char *));
char    *tgoto __P((char *, int, int));
char    *tparm __P((char *, int, int));
int     tputs __P((char *, int, int (*) __P((int))));
#endif

#define K_ESC		0x1b
#define K_CTRL(c)	(c & 63)

#define	TTY_NOATTRIBS	1

#define	REGLINEBUF	512
#define PRINTFBUFSZ	4000

/* Timeout for function key interpretation after K_ESC was read, in usec. */
#define ESCTOUT		333333

/* Inline attributes */
#define	TBOLD		""
#define	TUSCORE		""
#define	TREVERSE	""
#define	TNOATTR		""

struct tty_region {
	int	ttr_start;
	int	ttr_end;
	int	ttr_line;
	int	ttr_cursor;
	char	*ttr_lbuf[REGLINEBUF];
	int	ttr_lbs;
	int	ttr_lbe;
	int	ttr_sdown;
};

/* Function key macros. */
#define FK(x)		((x) << 8)
#define K_ISFKEY(c)	(!((c) & 0xff))
#define K_UP		FK(1)
#define K_DN		FK(2)
#define K_LEFT		FK(3)
#define K_RIGHT		FK(4)
#define K_F1		FK(5)
#define K_F2		FK(6)
#define K_F3		FK(7)
#define K_F4		FK(8)
#define K_F5		FK(9)
#define K_F6		FK(10)
#define K_F7		FK(11)
#define K_F8		FK(12)
#define K_F9		FK(13)
#define K_F10		FK(14)
#define K_F11		FK(15)
#define K_F12		FK(16)
#define K_HOME		FK(17)
#define K_END		FK(18)
#define K_PGUP		FK(19)
#define K_PGDN		FK(20)
#define K_INS		FK(21)
#define K_S_PGUP	FK(22)
#define K_S_PGDN	FK(23)
#define K_S_UP		FK(24)
#define K_S_DN		FK(25)
#define K_S_LEFT	FK(26)
#define K_S_RIGHT	FK(27)
#define K_S2_PGUP	FK(28)
#define K_S2_PGDN	FK(29)
#define K_S2_UP		FK(30)
#define K_S2_DN		FK(31)
#define K_S2_LEFT	FK(32)
#define K_S2_RIGHT	FK(33)
#define K_S3_PGUP	FK(34)
#define K_S3_PGDN	FK(35)
#define K_DEC_FIND	FK(36)

#define NUMFKEYS	36

int	tty_init __P((void));
int	tty_cbreak __P((void));
int	tty_raw __P((void));
int	tty_reset __P((void));
void	tty_getxy __P((int *, int *));
void	tty_gotoxy __P((int, int));
void	tty_getdim __P((void));
int	tty_getch __P((void));
void	tty_puts __P((char *));
void	tty_flush __P((void));
void	tty_rputs __P((char *, struct tty_region *));
void	tty_rdestroy __P((struct tty_region *));
struct	tty_region *tty_rcreate __P((int, int));
void	tty_scrolldn __P((struct tty_region *));
int	tty_printf __P((struct tty_region *, const char *, ...));
int	tty_isreset __P((void));
int	tty_peek __P((void));
void	tty_addbuff __P((char *));
void	tty_set_opt __P((int));
int	tty_get_opt __P((void));

#ifndef TTY_C

extern int	t_columns, t_lines, tc_maxcol;
extern int	funk_sz, funk_xlat;

extern char *tc_csr, *tc_clear, *tc_clreol, *tc_clrbol, *tc_clreos, *tc_mvcurs,
	*tc_homecurs, *tc_blink, *tc_bold, *tc_dim, *tc_rev, *tc_uscore,
	*tc_noattr, *tc_insl, *tc_dell, *tc_scrb, *tc_scrf, *tc_bell;

extern char *tck_f1, *tck_f2, *tck_f3, *tck_f4, *tck_f5, *tck_f6, *tck_f7,
	*tck_f8, *tck_f9, *tck_f10, *tck_f11, *tck_f12, *tck_up,
	*tck_dn, *tck_left, *tck_right, *tck_home, *tck_end, *tck_pgup,
	*tck_pgdn, *tck_ins;
extern char tck_erase, tck_kill, tck_werase, tck_reprint, tck_lnext, tck_intr;

#else

/* Placed here to avoid warnings about redundant declaration */
VOIDPTR	chkmem __P((VOIDPTR));	/* provided by the application program */

#endif	/* !TTY_C */

#endif	/* !TIRC_TTY_H */

