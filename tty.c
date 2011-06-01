/*
 * tirc -- client for the Internet Relay Chat
 *
 *	Copyright (c) 1996, 1997, 1998
 *		Matthias Buelow.  All rights reserved.
 *
 *	See the file ``COPYRIGHT'' for the usage and distribution
 *	license and warranty disclaimer.
 */

#ifndef lint
static char rcsid[] = "$Id: tty.c,v 1.33 1998/02/22 19:04:49 mkb Exp $";
#endif

#include "tirc.h"

#ifdef	HAVE_TERMIOS_H
#include <termios.h>
#elif defined (HAVE_SYS_TERMIOS_H)
#include <sys/termios.h>
#elif defined (HAVE_SGTTY_H)
#include <sgtty.h>
#endif
#include <sys/ioctl.h>
#ifdef	HAVE_SYS_TIME_H
#include <sys/time.h>
#elif defined(HAVE_TIME_H)
#include <time.h>
#endif
#ifdef	HAVE_STRING_H
#include <string.h>
#elif defined(HAVE_MEMORY_H)
#include <memory.h>
#endif
#ifdef	HAVE_CTYPE_H
#include <ctype.h>
#endif

#if defined(HAVE_TCGETATTR) && defined(HAVE_TCSETATTR) && \
    (defined(HAVE_TERMIOS_H) || defined(HAVE_SYS_TERMIOS_H))
#define	USE_TERMIOS	1
#elif defined(HAVE_GTTY) && defined(HAVE_STTY)
#define	USE_SGTTY	1
#ifdef	USE_TERMIOS
#undef	USE_TERMIOS
#endif
#endif

#define TTY_C
#include "tty.h"
#include "compat.h"
#include "colour.h"

static void	regaddln __P((struct tty_region *, char *));
static int	tty_putch __P((int));

extern int	errno;

static enum { STATE_RESET, STATE_RAW, STATE_CBREAK }
		ttystate = STATE_RESET;
#ifdef	USE_TERMIOS
static struct termios savetios;
#elif defined(USE_SGTTY)
static struct sgttyb savetios;
#endif
static int	cursx, cursy;
static int	koldn, koldi;
static char	kolds[64];

int		t_columns, t_lines, tc_maxcol;
char		*dumbs = "";
char		tc_entry[1024], tc_buff[1024];
unsigned int	fun_min, fun_max;
static int	gxy_sem; 
static int	tty_options;

static char	*outbuf;
static unsigned int obcnt;

/* General terminal capabilities */
char	*tc_csr, *tc_clear, *tc_clreol, *tc_clrbol, *tc_clreos, *tc_mvcurs,
	*tc_homecurs, *tc_blink, *tc_bold, *tc_dim, *tc_rev, *tc_uscore,
	*tc_noattr, *tc_insl, *tc_dell, *tc_scrb, *tc_scrf, *tc_bell;

/* Backup pointers, see tty_set_opt() */
static char *tc_blink_sv, *tc_bold_sv, *tc_dim_sv, *tc_rev_sv, *tc_uscore_sv,
	*tc_noattr_sv;

/* Special keycodes */
char	*tck_f1, *tck_f2, *tck_f3, *tck_f4, *tck_f5, *tck_f6, *tck_f7,
	*tck_f8, *tck_f9, *tck_f10, *tck_f11, *tck_f12, *tck_up,
	*tck_dn, *tck_left, *tck_right, *tck_home, *tck_end, *tck_pgup,
	*tck_pgdn, *tck_ins;
char	tck_erase, tck_kill, tck_werase, tck_reprint, tck_lnext, tck_intr;

/* 
 * Hardcoded ``standard'' cursor keys, as backup if those retrieved from
 * the termcap entry don't work (which happens far too often).
 */
char	tck_s_pgup[] = "[I", tck_s_pgdn[] = "[G", tck_s_up[] = "[A",
	tck_s_dn[] = "[B", tck_s_left[] = "[D", tck_s_right[] = "[C";
char	tck_s2_pgup[] = "OI", tck_s2_pgdn[] = "OG", tck_s2_up[] = "OA",
	tck_s2_dn[] = "OB", tck_s2_left[] = "OD", tck_s2_right[] = "OC";
char	tck_s3_pgup[] = "[5~", tck_s3_pgdn[] = "[6~",
	tck_dec_find[] = "[1~";

struct {
	size_t	size;
	char	*fkey;
} fkeys[NUMFKEYS];

char	err_term[] = "tty.c: descriptor does not belong to a teletype\n";
char	err_taget[] = "tty.c: cannot get terminal attributes for descriptor\n";
char	err_taset[] = "tty.c: cannot set terminal attributes for descriptor\n";

#define VALIDSTR(s)	{ if (s == NULL || s == ((VOIDPTR)-1)) s = dumbs; }

#define TGETSTR(cap, a)	tgetstr((cap), &(a))
/*#define TGETSTR(cap, a) tgetstr((cap), (a))*/
#define TGETNUM(cap)	tgetnum((cap))

/*
 * Initialize our terminal primitives.
 * Returns 0 on success, -1 on failure.
 */
int
tty_init()
{
	int i;
	char *area = tc_buff;
#ifdef	USE_TERMIOS
	struct termios term;
#elif defined(USE_SGTTY)
	struct sgttyb term;
	struct ltchars ltc;
	struct tchars tc;
#endif

	if (outbuf == NULL)
		outbuf = chkmem(malloc(OUTBUFSIZE));

	cursx = cursy = -1;
	gxy_sem = 0;
	obcnt = 0;

	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
		fprintf(stderr, "%s", err_term);
		return -1;
	}

	if (
#ifdef	USE_TERMIOS
	tcgetattr(STDIN_FILENO, &term) < 0
#elif defined (USE_SGTTY)
	(gtty(STDIN_FILENO, &term) < 0
#endif
	) {
		fprintf(stderr, "%s", err_taget);
		return -1;
	}
#ifdef	USE_TERMIOS
#ifdef	VERASE
	tck_erase = term.c_cc[VERASE];
#endif
#ifdef	VKILL
	tck_kill = term.c_cc[VKILL];
#endif
#ifdef	VWERASE
	tck_werase = term.c_cc[VWERASE];
#endif
#ifdef	VREPRINT
	tck_reprint = term.c_cc[VREPRINT];
#endif
#ifdef	VLNEXT
	tck_lnext = term.c_cc[VLNEXT];
#endif
#ifdef	VINTR
	tck_intr = term.c_cc[VINTR];
#endif
#elif defined(USE_SGTTY)
	tck_erase = term.sg_erase;
	tck_kill = term.sg_kill;
	if (ioctl(STDIN_FILENO, TIOCGLTC, &ltc) != -1) {
		tck_werase = ltc.t_werasc;
		tck_reprint = ltc.t_rprntc;
		tck_lnext = ltc.t_lnextc;
	}
	else
		perror("tty_init: warning: ioctl TIOCGLTC failed");
	if (ioctl(STDIN_FILENO, TIOCGETC, &tc) != -1)
		tck_intr = tc.t_intrc;
	else
		perror("tty_init: warning: ioctl TIOCGETC failed");
#endif

	if (tgetent(tc_entry, getenv("TERM")) < 1)
		return -1;
	tty_getdim();

	tc_maxcol = TGETNUM("Co");
	tc_csr = TGETSTR("cs", area);
	VALIDSTR(tc_csr);
	tc_insl = TGETSTR("al", area);
	VALIDSTR(tc_insl);
	tc_dell = TGETSTR("dl", area);
	VALIDSTR(tc_dell);
	tc_scrb = TGETSTR("sr", area);
	VALIDSTR(tc_scrb);
	tc_scrf = TGETSTR("sf", area);
	VALIDSTR(tc_scrf);
	tc_clear = TGETSTR("cl", area);
	VALIDSTR(tc_clear);
	tc_clreol = TGETSTR("ce", area);
	VALIDSTR(tc_clreol);
	tc_clrbol = TGETSTR("cb", area);
	VALIDSTR(tc_clrbol);
	tc_clreos = TGETSTR("cd", area);
	VALIDSTR(tc_clreos);
	tc_mvcurs = TGETSTR("cm", area);
	VALIDSTR(tc_mvcurs);
	tc_homecurs = TGETSTR("ho", area);
	VALIDSTR(tc_homecurs);
	tc_blink = TGETSTR("mb", area);
	VALIDSTR(tc_blink);
	tc_bold = TGETSTR("md", area);
	if (tc_bold != NULL) {
		VALIDSTR(tc_bold);
	}
	else {
		/* Try standout */
		tc_bold = TGETSTR("ms", area);
		VALIDSTR(tc_bold);
	}
	tc_dim = TGETSTR("mh", area);
	VALIDSTR(tc_dim);
	tc_rev = TGETSTR("mr", area);
	VALIDSTR(tc_rev);
	tc_uscore = TGETSTR("us", area);
	VALIDSTR(tc_uscore);
	tc_noattr = TGETSTR("me", area);
	VALIDSTR(tc_noattr);
	tck_f1 = TGETSTR("k1", area);
	VALIDSTR(tck_f1);
	tck_f2 = TGETSTR("k2", area);
	VALIDSTR(tck_f2);
	tck_f3 = TGETSTR("k3", area);
	VALIDSTR(tck_f3);
	tck_f4 = TGETSTR("k4", area);
	VALIDSTR(tck_f4);
	tck_f5 = TGETSTR("k5", area);
	VALIDSTR(tck_f5);
	tck_f6 = TGETSTR("k6", area);
	VALIDSTR(tck_f6);
	tck_f7 = TGETSTR("k7", area);
	VALIDSTR(tck_f7);
	tck_f8 = TGETSTR("k8", area);
	VALIDSTR(tck_f8);
	tck_f9 = TGETSTR("k9", area);
	VALIDSTR(tck_f9);
	tck_f10 = TGETSTR("k ", area);
	VALIDSTR(tck_f10);
	tck_f11 = TGETSTR("k<", area);
	VALIDSTR(tck_f11);
	tck_f12 = TGETSTR("k>", area);
	VALIDSTR(tck_f12);
	tck_up = TGETSTR("ku", area);
	VALIDSTR(tck_up);
	tck_dn = TGETSTR("kd", area);
	VALIDSTR(tck_dn);
	tck_left = TGETSTR("kl", area);
	VALIDSTR(tck_left);
	tck_right = TGETSTR("kr", area);
	VALIDSTR(tck_right);
	tck_home = TGETSTR("kh", area);
	VALIDSTR(tck_home);
	tck_end = TGETSTR("@7", area);
	VALIDSTR(tck_end);
	tck_pgup = TGETSTR("kP", area);
	VALIDSTR(tck_pgup);
	tck_pgdn = TGETSTR("kN", area);
	VALIDSTR(tck_pgdn);
	tck_ins = TGETSTR("kI", area);
	VALIDSTR(tck_ins);
	tc_bell = TGETSTR("bl", area);
	VALIDSTR(tc_bell);

	fkeys[0].fkey = tck_up;
	fkeys[0].size = strlen(tck_up);
	fkeys[1].fkey = tck_dn;
	fkeys[1].size = strlen(tck_dn);
	fkeys[2].fkey = tck_left;
	fkeys[2].size = strlen(tck_left);
	fkeys[3].fkey = tck_right;
	fkeys[3].size = strlen(tck_right);
	fkeys[4].fkey = tck_f1;
	fkeys[4].size = strlen(tck_f1);
	fkeys[5].fkey = tck_f2;
	fkeys[5].size = strlen(tck_f2);
	fkeys[6].fkey = tck_f3;
	fkeys[6].size = strlen(tck_f3);
	fkeys[7].fkey = tck_f4;
	fkeys[7].size = strlen(tck_f4);
	fkeys[8].fkey = tck_f5;
	fkeys[8].size = strlen(tck_f5);
	fkeys[9].fkey = tck_f6;
	fkeys[9].size = strlen(tck_f6);
	fkeys[10].fkey = tck_f7;
	fkeys[10].size = strlen(tck_f7);
	fkeys[11].fkey = tck_f8;
	fkeys[11].size = strlen(tck_f8);
	fkeys[12].fkey = tck_f9;
	fkeys[12].size = strlen(tck_f9);
	fkeys[13].fkey = tck_f10;
	fkeys[13].size = strlen(tck_f10);
	fkeys[14].fkey = tck_f11;
	fkeys[14].size = strlen(tck_f11);
	fkeys[15].fkey = tck_f12;
	fkeys[15].size = strlen(tck_f12);
	fkeys[16].fkey = tck_home;
	fkeys[16].size = strlen(tck_home);
	fkeys[17].fkey = tck_end;
	fkeys[17].size = strlen(tck_end);
	fkeys[18].fkey = tck_pgup;
	fkeys[18].size = strlen(tck_pgup);
	fkeys[19].fkey = tck_pgdn;
	fkeys[19].size = strlen(tck_pgdn);
	fkeys[20].fkey = tck_ins;
	fkeys[20].size = strlen(tck_ins);
	fkeys[21].fkey = tck_s_pgup;
	fkeys[21].size = strlen(tck_s_pgup);
	fkeys[22].fkey = tck_s_pgdn;
	fkeys[22].size = strlen(tck_s_pgdn);
	fkeys[23].fkey = tck_s_up;
	fkeys[23].size = strlen(tck_s_up);
	fkeys[24].fkey = tck_s_dn;
	fkeys[24].size = strlen(tck_s_dn);
	fkeys[25].fkey = tck_s_left;
	fkeys[25].size = strlen(tck_s_left);
	fkeys[26].fkey = tck_s_right;
	fkeys[26].size = strlen(tck_s_right);
	fkeys[27].fkey = tck_s2_pgup;
	fkeys[27].size = strlen(tck_s2_pgup);
	fkeys[28].fkey = tck_s2_pgdn;
	fkeys[28].size = strlen(tck_s2_pgdn);
	fkeys[29].fkey = tck_s2_up;
	fkeys[29].size = strlen(tck_s2_up);
	fkeys[30].fkey = tck_s2_dn;
	fkeys[30].size = strlen(tck_s2_dn);
	fkeys[31].fkey = tck_s2_left;
	fkeys[31].size = strlen(tck_s2_left);
	fkeys[32].fkey = tck_s2_right;
	fkeys[32].size = strlen(tck_s2_right);
	fkeys[33].fkey = tck_s3_pgup;
	fkeys[33].size = strlen(tck_s3_pgup);
	fkeys[34].fkey = tck_s3_pgdn;
	fkeys[34].size = strlen(tck_s3_pgdn);
	fkeys[35].fkey = tck_dec_find;
	fkeys[35].size = strlen(tck_dec_find);

	koldn = koldi = 0;

	for (i = 1, fun_min = fkeys[0].size, fun_max = fkeys[0].size;
	    i < NUMFKEYS; i++) {
		if (fkeys[i].size < fun_min && fkeys[i].size > 1)
			fun_min = fkeys[i].size;
		if (fkeys[i].size > fun_max)
			fun_max = fkeys[i].size;
	}

	tc_blink_sv = tc_blink;
	tc_bold_sv = tc_bold;
	tc_dim_sv = tc_dim;
	tc_rev_sv = tc_rev;
	tc_uscore_sv = tc_uscore;
	tc_noattr_sv = tc_noattr;

	tty_set_opt(tty_options);
	
	return 0;
}

/*
 * Put the terminal into cbreak mode.
 */
int
tty_cbreak()
{
#ifdef	USE_TERMIOS
	struct termios t;
#elif defined (USE_SGTTY)
	struct sgttyb t;
#endif

	if (ttystate == STATE_CBREAK)
		return 0;
	if (!isatty(STDIN_FILENO)) {
		fprintf(stderr, "%s", err_term);
		return -1;
	}

	if (
#ifdef	USE_TERMIOS
	tcgetattr(STDIN_FILENO, &savetios) < 0
#elif defined(USE_SGTTY)
	gtty(STDIN_FILENO, &savetios) < 0
#endif
	) {
		fprintf(stderr, "%s", err_taget);
		return -1;
	}

	memmove(&t, &savetios, sizeof savetios);
#ifdef	USE_TERMIOS
	t.c_lflag &= ~(ECHO | ICANON);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
#elif defined(USE_SGTTY)
	t.sg_flags &= ~ECHO;
	t.sg_flags |= CBREAK;
#endif

	if (
#ifdef	USE_TERMIOS
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) < 0
#elif defined(USE_SGTTY)
	stty(STDIN_FILENO, &t) < 0
#endif
	) {
		fprintf(stderr, "%s", err_taset);
		return -1;
	}
	ttystate = STATE_CBREAK;
	return 0;
}

/*
 * Put the terminal into raw mode.
 */
int
tty_raw()
{
#ifdef	USE_TERMIOS
	struct termios t;
#elif defined (USE_SGTTY)
	struct sgttyb t;
#endif

	if (ttystate == STATE_RAW)
		return 0;
	if (!isatty(STDIN_FILENO)) {
		fprintf(stderr, "%s", err_term);
		return -1;
	}

	if (
#ifdef	USE_TERMIOS
	tcgetattr(STDIN_FILENO, &savetios) < 0
#elif defined(USE_SGTTY)
	if (gtty(STDIN_FILENO, &savetios) < 0
#endif
	) {
		fprintf(stderr, "%s", err_taget);
		return -1;
	}

	memmove(&t, &savetios, sizeof savetios);
#ifdef	USE_TERMIOS
	t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	t.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	t.c_cflag &= ~(CSIZE | PARENB);
	t.c_oflag &= ~(OPOST);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
#elif defined(USE_SGTTY)
	t.sg_flags &= ~(ECHO | CBREAK);
	t.sg_flags |= RAW;
#endif

	if (
#ifdef	USE_TERMIOS
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) < 0
#elif defined(USE_SGTTY)
	if (stty(STDIN_FILENO, &t) < 0
#endif
	) {
		fprintf(stderr, "%s", err_taset);
		return -1;
	}
	ttystate = STATE_RAW;
	return 0;
}

/* 
 * Reset terminal to previously saved state.
 * Returns 0 on success and -1 on failure.
 */
int
tty_reset()
{
	if (ttystate != STATE_CBREAK && ttystate != STATE_RAW)
		return 0;
	if (!isatty(STDIN_FILENO)) {
		fprintf(stderr, "%s", err_term);
		return -1;
	}

	if (
#ifdef	USE_TERMIOS
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &savetios) < 0
#elif defined(USE_SGTTY)
	if (stty(STDIN_FILENO, &savetios) < 0
#endif
	) {
		fprintf(stderr, "%s", err_taset);
		return -1;
	}
	ttystate = STATE_RESET;
	return 0;
}

/*
 * Get cursor position.
 */
void
tty_getxy(x, y)
	int *x, *y;
{
	*x = cursx;
	*y = cursy;
}

/*
 * Relocate cursor.
 */
void
tty_gotoxy(x, y)
	int x, y;
{
	if (gxy_sem)
		return;
	gxy_sem = 1;
	tputs(tgoto(tc_mvcurs, x, y), 0, tty_putch);
	cursx = x;
	cursy = y;
	gxy_sem = 0;
}

/* 
 * Get screen dimensions.
 */
void
tty_getdim()
{
#ifdef	HAVE_TIOCGWINSZ
	struct winsize ws;

	t_lines = t_columns = 0;

 	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1)
		perror("tty_getdim: TIOCGSIZE ioctl failed");
	else {
		t_columns = ws.ws_col;
		t_lines = ws.ws_row;
	}
#elif defined(HAVE_TIOCGSIZE)
	struct ttysize ws;

	if (ioctl(STDIN_FILENO, TIOCGSIZE, &ws) == -1)
		perror("tty_getdim: TIOCGSIZE ioctl failed");
	else {
		t_columns = ws.ts_cols;
		t_lines = ws.ts_lines;
	}
#endif
	if (t_columns <= 1 || t_lines <= 1) {
		/* No ioctl to query screen size available or disfunctional,
		 * try environment variables LINES and COLUMNS instead. */
		char *varval = getenv("LINES");

		if (varval != NULL)
			t_lines = atoi(varval);
		if ((varval = getenv("COLUMNS")) != NULL)
			t_columns = atoi(varval);
	}

	if (t_columns <= 1 || t_lines <= 1) {
		/* env variables didn't work; try termcap */
		t_columns = TGETNUM("co");
		t_lines = TGETNUM("li");
	}

	if (t_columns <= 1 || t_lines <= 1) {
		/* giving up; setting default size 80x24 */
		t_lines = 24;
		t_columns = 80;
	}
	return;
}

/*
 * Get a character through the standard input and translate some 
 * function keys.
 */
int
tty_getch()
{
	unsigned int i;
	int k, s, r;
	unsigned char tmp[64];
	fd_set rfds;
	struct timeval tout;

	if (koldn >= fun_min) {
		/* check if buffer contains function key */
		for (s = 0; s < NUMFKEYS; s++)
			if (fkeys[s].size == koldn &&
			    !strcmp(kolds+koldi, fkeys[s].fkey)) {
				koldn = koldi = 0;
				return FK(s+1);
			}
	}
	if (koldn > 0) {
		k = kolds[koldi++];
		koldn--;
		return k;
	}

restartsc0:
	if (read(STDIN_FILENO, tmp, 1) < 0) {
		if (errno == EINTR)
			goto restartsc0;
		perror("tty_getch: in read()");
		Exit(1);
	}
	k = *tmp;

	if (k == K_ESC) {
		tout.tv_sec = 0;
		tout.tv_usec = ESCTOUT;
		FD_ZERO(&rfds);
		FD_SET(STDIN_FILENO, &rfds);
	restartsc:
		if ((r = select(STDIN_FILENO+1, &rfds, NULL, NULL, &tout))
		    < 0) {
			if (errno == EINTR)
				goto restartsc;
			perror("tty_getch, in select");
			Exit(1);
		}
		if (!r)
			return k;
		
		tmp[0] = k;
	restartsc1:
		if ((r = read(STDIN_FILENO, tmp+1, fun_min-1)) < 0) {
			if (errno == EINTR)
				goto restartsc1;
			perror("tty_getch: in read");
			Exit(1);
		}
		tmp[r+1] = 0;
		
		if ((unsigned)r < fun_min-1) {
			/* Not a special key. */
			koldn = r;
			koldi = 0;
			strcpy(kolds, tmp+1);
			return *tmp;
		}

		i = fun_min;
		do {
			for (s = 0; s < NUMFKEYS; s++)
				if (fkeys[s].size == i &&
				    !strcmp(tmp, fkeys[s].fkey))
					return FK(s+1);

		restartsc2:
			tout.tv_sec = 0;
			tout.tv_usec = 0;
			FD_ZERO(&rfds);
			FD_SET(STDIN_FILENO, &rfds);
			if ((r = select(STDIN_FILENO+1, &rfds, NULL, NULL,
			    &tout)) < 0) {
				if (errno == EINTR)
					goto restartsc2;
				perror("tty_getch, in select");
				Exit(1);
			}
			if (r == 0)
				break;
		restartsc3:
			if ((r = read(STDIN_FILENO, tmp+i, 1)) < 0) {
				if (errno == EINTR)
					goto restartsc3;
				perror("tty_getch: in read");
				Exit(1);
			}
			tmp[i+1] = 0;
		} while (i++ <= fun_max);

		koldn = strlen(tmp)-1;
		koldi = 0;
		strcpy(kolds, tmp+1);
		return *tmp;
	}
	return k;
}

/*
 * Return number of buffered characters in input.
 */
int
tty_peek()
{
	return koldn;
}

/*
 * Output a character to standard output.
 */
static int
tty_putch(c)
	int c;
{
	outbuf[obcnt++] = c;
	if (obcnt >= OUTBUFSIZE)
		tty_flush();
	return c;
}

/*
 * Write a string to the tty.  Expand certain inline attributes (ircII like)
 * and colour codes on the way.
 */
void
tty_puts(s)
	char *s;
{
	int l = 0;
#define	PUTS_BOLD	1
#define	PUTS_USCORE	2
#define	PUTS_REVERSE	4
#define	PUTS_USECOLOUR	8
	unsigned flags = 0;
#ifdef	WITH_ANSI_COLOURS
	int ofgc = 0, obgc = 0;
	int use_colour = check_conf(CONF_COLOUR);
#endif

	if (s == NULL)
		return;

	while (*s != '\0') {
		switch (*s) {
		case '':	/* bold */
			if (! (flags & PUTS_BOLD)) {
				tty_addbuff(tc_bold);
				flags |= PUTS_BOLD;
			}
			else {
				tty_addbuff(tc_noattr);
				flags &= ~PUTS_BOLD;
				goto reset_to_colour;
			}
			break;
		case '':	/* underscore */
			if (! (flags & PUTS_USCORE)) {
				tty_addbuff(tc_uscore);
				flags |= PUTS_USCORE;
			}
			else {
				tty_addbuff(tc_noattr);
				flags &= ~PUTS_USCORE;
				goto reset_to_colour;
			}
			break;
		case '':	/* reverse video */
			if (! (flags & PUTS_REVERSE)) {
				tty_addbuff(tc_rev);
				flags |= PUTS_REVERSE;
			}
			else {
				tty_addbuff(tc_noattr);
				flags &= ~PUTS_REVERSE;
				goto reset_to_colour;
			}
			break;
		case '':	/* attributes off */
			if (flags != 0) {
			    tty_addbuff(tc_noattr);
			    flags &= ~(PUTS_BOLD | PUTS_USCORE | PUTS_REVERSE);
		reset_to_colour:
			    ;
#ifdef	WITH_ANSI_COLOURS
			    if (flags & PUTS_USECOLOUR)
				tty_addbuff(set_colour(ofgc, obgc));
#endif
			}
			break;
#ifdef	WITH_ANSI_COLOURS
		case '':	/* colour pair ^C<foreground>,<background>; */
			{
			int f, b;

			f = atoi(++s);
			while (isdigit(*s))
				s++;
			if (*s != ',')
				break;
			b = atoi(++s);
			while (isdigit(*s))
				s++;
			if (*s != ';')
				break;
			if (use_colour) {
				tty_addbuff(set_colour(f, b));
				flags |= PUTS_USECOLOUR;
				ofgc = f;
				obgc = b;
			}
			break;
			}
#endif
		default:
			if (obcnt >= OUTBUFSIZE)
				tty_flush();
			outbuf[obcnt++] = *s;
			l++;
			break;
		}
		s++;
	}

	if (flags != 0)
		tty_addbuff(tc_noattr);
#ifdef	WITH_ANSI_COLOURS
	if (flags & PUTS_USECOLOUR)
		tty_addbuff(colour_off());
#endif
	cursx += l % t_columns;
	cursy += l / t_columns;
}

void
tty_addbuff(s)
	char *s;
{
	while (*s) {
		if (obcnt >= OUTBUFSIZE)
			tty_flush();
		outbuf[obcnt++] = *s;
		s++;
	}
}

/*
 * Flush the output buffer.
 */
void
tty_flush()
{
	if (obcnt > 0) {
		write(STDOUT_FILENO, outbuf, obcnt);
		obcnt = 0;
	}
}

/*
 * Create a new tty region and return a pointer to it.
 */
struct tty_region *
tty_rcreate(start, end)
	int start, end;
{
	struct tty_region *r;
	int i;

	r = chkmem(calloc(1, sizeof (struct tty_region)));
	for (i = 0; i < REGLINEBUF; i++)
		r->ttr_lbuf[i] = NULL;
	r->ttr_start = start;
	r->ttr_end = end;
	return r;
}

/*
 * Delete a tty region and all its lines.
 */
void
tty_rdestroy(r)
	struct tty_region *r;
{
	int i = r->ttr_lbs;

	if (r == NULL)
		return;
	while (i != r->ttr_lbe) {
		free(r->ttr_lbuf[i]);
		i = (i+1) % REGLINEBUF;
	}
	free(r->ttr_lbuf[i]);
	free(r);
}

/*
 * Write a string to the specified scrolling region.  Performs scrolling
 * if it exceeds the lower line of the region.
 */
void
tty_rputs(l, r)
	char *l;
	struct tty_region *r;
{
	char *t, *tptr, *s, *sp, nline = 0, cr = 0;
	int cx;

	s = sp = chkmem(Strdup(l));
	tptr = t = chkmem(malloc(strlen(l)+1));
	*tptr = 0;
	cx = r->ttr_cursor;

	while (*s) {
		switch (*s) {
		case '\r':
			s++;
			cr = 1;
			break;
		case '\n':
			s++;
			nline = 1;
			break;
		default:
			/* Add char to line. */

			switch (*s) {
			case '':	/*FALLTHROUGH*/
			case '':	/*FALLTHROUGH*/
			case '':	/*FALLTHROUGH*/
			case '':
				break;
			case '':
				{
				register char *u = s;

				while (*u != '\0' && *u++ != ';')
					cx--;
				}
				break;
			default:
				cx++;
			}

			*tptr++ = *s++;
			break;
		}
		*tptr = 0;
		if (cx > t_columns || nline || cr) {
			if (r->ttr_sdown) {
			    tty_scrolldn(r);
			    r->ttr_sdown = 0;
			}
			tty_gotoxy(r->ttr_cursor, r->ttr_start+r->ttr_line);
			tty_puts(t);
			tty_addbuff(tc_clreol);
			regaddln(r, t);

			if (!cr && r->ttr_line >= r->ttr_end - r->ttr_start)
				r->ttr_sdown = 1;
			else {
			    if (!cr)
				r->ttr_line++;
			}
/*			if (nline || cr) {*/
				r->ttr_cursor = cx = 0;
				tptr = t;
				*tptr = 0;
/*			}*/
			nline = cr = 0;
		}
	}
	if (*tptr) {
		tty_gotoxy(r->ttr_cursor, r->ttr_start+r->ttr_line);
		tty_puts(s);
		tty_addbuff(tc_clreol);
		regaddln(r, s);
		r->ttr_cursor = cx;
	}
	r->ttr_cursor = cx;

	free(t);
	free(sp);
}

static void
regaddln(r, l)
	struct tty_region *r;
	char *l;
{
	/*
	 * Add a line to a tty region line buffer.
	 * The buffer is a static ring of pointers to lines.
	 * We traverse the ring and if we reach the start, we delete the 
	 * first line and move on with the start pointer.
	 */
	if (r->ttr_lbuf[r->ttr_lbe] != NULL)
		free(r->ttr_lbuf[r->ttr_lbe]);
	r->ttr_lbuf[r->ttr_lbe] = chkmem(Strdup(l));
	r->ttr_lbe++;
	r->ttr_lbe %= REGLINEBUF;
	if (r->ttr_lbe == r->ttr_lbs) {
		r->ttr_lbs++;
		r->ttr_lbs %= REGLINEBUF;
	}
}

/*
 * Scroll down one line in the specified region.  Makes advantage of
 * terminal scroll support.  This does _not_ flush the output buffer!
 */
void
tty_scrolldn(r)
	struct tty_region *r;
{
	int i, j, ox, oy;

	tty_getxy(&ox, &oy);

#ifdef	HAVE_TPARM
	if (*tc_csr) {
		/*
		 * Use changeable scrolling region and scroll-forward
		 * capability (or delete-line and insert-line if forward
		 * scrolling is not available).  This is only applicable
		 * if we have the tparm() function.
		 */
		if (*tc_scrf) {
		    tputs(tparm(tc_csr, r->ttr_start, r->ttr_end), 0,
			tty_putch);
		    tputs(tgoto(tc_mvcurs, 0, r->ttr_end), 0, tty_putch);
		    tputs(tc_scrf, 0, tty_putch);
		}
		else if (*tc_dell && *tc_insl) {
		    tputs(tparm(tc_csr, r->ttr_start, r->ttr_end), 0,
			tty_putch);
		    tputs(tgoto(tc_mvcurs, 0, r->ttr_start), 0, tty_putch);
		    tputs(tc_dell, 0, tty_putch);
		}
		else
			goto brutescroll;
		tputs(tc_clreol, 0, tty_putch);
		tputs(tgoto(tc_mvcurs, 0, r->ttr_end), 0, tty_putch);
		tputs(tparm(tc_csr, 0, t_lines-1), 0, tty_putch);
	}
	else
#endif	/* HAVE_TPARM */
	if (*tc_dell && *tc_insl) {
		/*
		 * No changeable scrolling reagion here, but insert-line
		 * and delete-line are available.
		 * This might eventually cause ``Berkeley-curses-style''
		 * flickering of the lower part (because that's the method
		 * BSD curses favours for some reasons lost in history),
		 * that's why we prefer changeable scrolling regions.
		 */
		tputs(tgoto(tc_mvcurs, 0, r->ttr_start), 0, tty_putch);
		tputs(tc_dell, 0, tty_putch);
		tputs(tgoto(tc_mvcurs, 0, r->ttr_end), 0, tty_putch);
		tputs(tc_insl, 0, tty_putch);
	}
	else {
		/*
		 * Silly terminal, we have to repaint the region for
		 * scrolling.
		 */
	brutescroll:
		i = r->ttr_lbe - (r->ttr_end - r->ttr_start);
		if (i < 0)
			i = REGLINEBUF + (i % REGLINEBUF);
		for (j = 0; i < r->ttr_lbe; i++, j++) {
			tputs(tgoto(tc_mvcurs, 0, r->ttr_start+j), 0,
				tty_putch);
			tty_puts(r->ttr_lbuf[i]);
			tputs(tc_clreol, 0, tty_putch);
		}
	}
	tty_gotoxy(ox, oy);
}

/*
 * printf() interface for tty output.  Writes to the specified tty_region
 * or, if parameter r is NULL, to the current cursor position on the
 * screen.
 */
int
/*VARARGS2*/
#ifdef	__STDC__
tty_printf(struct tty_region *r, const char *fmt, ...)
#else
tty_printf(r, fmt, va_alist)
	struct tty_region *r;
	char *fmt;
	va_dcl
#endif
{
	extern size_t vsnprintf_buffer_size;
	va_list ap;
	char *lbuf;
	int ln;

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

	if (r != NULL)
		tty_rputs(lbuf, r);
	else
		tty_puts(lbuf);

	free(lbuf);
	return ln;
}

/*
 * Return 1 if tty is in original state, 0 otherwise.
 */
int
tty_isreset()
{
	return (ttystate == STATE_RESET);
}

void
tty_set_opt(opt)
	int opt;
{
	static char voidattr[] = "";

	if (opt & TTY_NOATTRIBS) {
		tc_blink = tc_bold = tc_dim = tc_rev = tc_uscore = tc_noattr
			= voidattr;
	}
	else {
		tc_blink = tc_blink_sv;
		tc_bold = tc_bold_sv;
		tc_dim = tc_dim_sv;
		tc_rev = tc_rev_sv;
		tc_uscore = tc_uscore_sv;
		tc_noattr = tc_noattr_sv;
	}

	tty_options = opt;
}

int
tty_get_opt()
{
	return tty_options;
}

