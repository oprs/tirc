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
static char rcsid[] = "$Id: irc.c,v 1.70 1998/02/22 19:04:49 mkb Exp $";
#endif

#include "tirc.h"

#ifdef	HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
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
#endif

#include "compat.h"
#include "tty.h"
#include "num.h"
#include "v.h"
#include "colour.h"

extern void	ctldcc __P((fd_set *));
extern int	dcc_fd_set __P((fd_set *));

static int	splitline __P((char *, struct servmsg *));
static void	dgtrunc __P((int, int));
static void	parse_mode __P((struct servmsg *));
static void	join_chan __P((struct servmsg *));
static void	part_chan __P((struct servmsg *));
static void	ctcpmsg __P((struct servmsg *));
static void	ctcpnotice __P((struct servmsg *));
static void	check_sigwinch __P((void));
static void	newnick __P((char *));
static void	strip_mirc_colours __P((unsigned char *));
static char	*skipspace __P((char *));

#define TIMESTAMP	(is_away || check_conf(CONF_STAMP)) ? timestamp() : ""
#define NICKORPREFIX(n)	(*(n) != '\0') ? (n) : sm->sm_prefix
#define	PPREORNUM(sm)	((sm)->sm_anum)
#define ISCHANNEL(s)	(((*s)=='#')||((*s)=='&')||((*s)=='+')||((*s)=='!'))

static void	cmd_ignore __P((struct servmsg *));
static void	cmd_nonum __P((struct servmsg *));
static void	cmd_print __P((struct servmsg *));
static void	cmd_welcomed __P((struct servmsg *));
static void	cmd_nickused __P((struct servmsg *));
static void	cmd_cmode __P((struct servmsg *));
static void	cmd_names __P((struct servmsg *));
static void	cmd_generic __P((struct servmsg *));
static void	cmd_unavailable __P((struct servmsg *));

extern char	nick[], *loginname, *realname, *srvnm, *pass, ppre[];
extern char	*clname;
/* sock is the socket used for communicating with ircd */
extern int	sock, syspipe, systempid, debugwin;
extern void	(*othercmd) __P((char *));
extern FILE	*lastlog;
extern FILE	*msglog;

char	version[] = VERSTRING;
int	restart_irc;
int	on_irc;
int	is_away;
int	umd;
char	lastmsg[MSGNAMEHIST][NICKSZ+2];
int	lastmsgp;
struct timeval	last_ctcpmsg;
struct channel	*cha;
char	*our_address;
int	closesyspipe;
int	busy;
int	update_clock;
sig_atomic_t system_ecode;	/* /system spawned command exit code */
sig_atomic_t atomic_idleexceed;	/* if 1, exit with idle limit exceeded */
int	user_exit_flag;		/* if 1, user quitted */

struct inbuf_line {
	char	*il_line;
	int	il_size;
	TAILQ_ENTRY(inbuf_line) il_entries;
};

TAILQ_HEAD(, inbuf_line) ibhead;

static char	*dgbuf[DGMAXDESC];
static int	dgptr[DGMAXDESC];
static char	upctbl[256];
static int	use_ilbuf;
static struct p_queue *ilbuf_pqe;

/*
 * Init/reset this layer.
 */
void
initirc()
{
	int i;
	char *t = upctbl;

	/* Create lookup table for irc_strupr(). */

	for (i = 0; i < 256; i++, t++) {
		switch (i) {
		default:
			*t = toupper(i);
			break;
		case '{':
			*t = '[';
			break;
		case '}':
			*t = ']';
			break;
		case '|':
			*t = '\\';
			break;
		}
	}
}

/*
 * Prepare client for this connection.
 */
void
prepareirc()
{
	int i;

	closesyspipe = restart_irc = on_irc = is_away = umd = busy =
		update_clock = use_ilbuf = user_exit_flag = 0;
	msglog = NULL;
	cha = NULL;
	ilbuf_pqe = NULL;

	TAILQ_INIT(&ibhead);

	lastmsgp = 0;
	for (i = 0; i < MSGNAMEHIST; i++)
		lastmsg[i][0] = 0;

	if (gettimeofday(&last_ctcpmsg, NULL) < 0) {
		perror("gettimeofday() failed");
		Exit(1);
	}
	if (our_address != NULL)
		free(our_address);
	our_address = NULL;

	if (sock)
		if (dg_allocbuffer(sock) < 0)
			iw_printf(COLI_WARN, "%s%sdg_allocbuffer() failed%s\n",
				TBOLD, ppre, TBOLD);

	/* hook line-in buffer handler onto periodic queue. */
	ilbuf_pqe = pq_add(run_ilbuf);
}

/*
 * dprintf() is like fprintf() but works on file descriptors and not on
 * streams.  This may block until the other side is ready to receive data.
 * If we have debug logging windows, the output is copied to these windows.
 */
/*VARARGS2*/
int
#ifdef	__STDC__
dprintf(int d, const char *fmt, ...)
#else
dprintf(d, fmt, va_alist)
	int d;
	char *fmt;
	va_dcl
#endif
{
	va_list ap;
	char lbuf[BUFSZ], dl[BUFSZ+32];
	int ln;
	struct iw_msg msg;
	char *t;

#ifdef	__STDC__
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
	/* XXX overlarge string gets truncated here */
	ln = vsnprintf(lbuf, BUFSZ, fmt, ap);
	lbuf[BUFSZ-1] = '\0';
	va_end(ap);
	if (!d)
		return 0;
restartsc:
	if (write(d, lbuf, (unsigned)ln) == -1) {
		if (errno == EINTR)
			goto restartsc;
		return -1;
	}
	if (!use_ilbuf && d == sock && debugwin > 0) {
		if ((t = strchr(lbuf, '\r')) != NULL)
			*t = ' ';
		dl[0] = '>';
		dl[1] = 0;
		strcat(dl, lbuf);
		msg.iwm_text = dl;
		msg.iwm_type = IMT_DEBUG;
		msg.iwm_chn = NULL;
		msg.iwm_chint = COLI_TEXT;
		dispose_msg(&msg);
	}
	return ln;
}

/*
 * dg_allocbuffer() allocates a buffer for the specified descriptor to
 * be used then with dgets().  You must call this function before using
 * dgets on the specified descriptor!  Returns 0 if ok and the buffer
 * can be used, -1 otherwise.
 */
int
dg_allocbuffer(d)
	int d;
{
	if (d >= DGMAXDESC || d < 0) {
		fprintf(stderr, "dg_allocbuffer(): descriptor > DGMAXDESC\n");
		return -1;
	}
	dgbuf[d] = chkmem(malloc(BUFSZ));
	dgptr[d] = 0;
	return 0;
}

/*
 * dg_freebuffer() frees a previously allocated descriptor buffer.
 * Returns 0 on success, -1 on error.
 */
int
dg_freebuffer(d)
	int d;
{
	if (d >= DGMAXDESC || d < 0 || dgbuf[d] == NULL)
		return -1;
	free(dgbuf[d]);
	return 0;
}

/*
 * Get a line from the descriptor.  Reads up to '\n', sz or until 
 * BUFSZ is reached, returning the asciiz string in s and the length 
 * as return value.  Returns 0 if a timeout (parameter tout in secs) 
 * has occurred, -1 on error and -2 if the connection has been closed
 * by the other side.  dgets() does buffering, like fgets().
 * Use dg_allocbuffer() to allocate a buffer prior to using dgets() and
 * dg_freebuffer() to release that buffer after usage.
 */
int
dgets(d, sz, s, tout)
	int d, sz;
	char *s;
	int tout;
{
	int j;
	char *nrd;
	struct timeval sto;
	fd_set rfds;

	if (!sz || !s || d >= DGMAXDESC || d < 0 || dgbuf[d] == NULL) {
		errno = EINVAL;
		return -1;
	}

	/* Feed from buffer if it already contains a complete line. */
	if (dgptr[d] && (nrd = memchr(dgbuf[d], '\n',
	    (unsigned)dgptr[d])) != NULL) {
		j = min(nrd-(char *)dgbuf[d], sz);
		memmove(s, dgbuf[d], (unsigned)j);
		s[j] = 0;
		dgtrunc(d, j+1);
		return j;
	}

	/*
	 * Check if descriptor is ready to be read, for tout seconds.
	 * Return with failure if timeout or nothing read.
	 */
	sto.tv_sec = tout;
	sto.tv_usec = 0;
	FD_ZERO(&rfds);
	FD_SET(d, &rfds);
restartsc:
	if ((j = select(d+1, &rfds, NULL, NULL, &sto)) < 0) {
		if (errno == EINTR) {
			check_sigint();
			goto restartsc;
		}
		return -1;
	}
	if (!j)
		return 0;

	/*
	 * Read from the descriptor.  Return immediately on error and if 
	 * no bytes were read.  Otherwise check if newly read data contains
	 * newline.  If it does, return the buffer until that newline
	 * and truncate the buffer up to that line.  If there was no newline
	 * read, check if buffer is saturated and return up to sz or BUFSZ
	 * chars w/o a newline and truncate the buffer.
	 */
	if ((j = read(d, dgbuf[d]+dgptr[d], (unsigned)BUFSZ-dgptr[d]-1)) < 0)
		return -1;

	if (j > 0) {
		dgptr[d] += j;
		if ((nrd = memchr(dgbuf[d], '\n',
		    (unsigned)dgptr[d])) != NULL) {
			j = min(nrd-(char *)dgbuf[d], sz);
			memmove(s, dgbuf[d], (unsigned)j);
			s[j] = 0;
			dgtrunc(d, j+1);
			return j;
		}
		if (dgptr[d] >= BUFSZ-1) {
			j = min(dgptr[d], sz);
			memmove(s, dgbuf[d], (unsigned)j);
			s[j] = 0;
			dgtrunc(d, j);
			return j;
		}
	}
	else
		return -2;

	return 0;
}

static void
dgtrunc(d, t)
	int d, t;
{
	memmove(dgbuf[d], dgbuf[d]+t, (unsigned)dgptr[d]-t);
	dgptr[d] -= t;
}

/*
 * Registers the client with the server and then enters a loop in wich 
 * it processes user and server input.
 * Returns 0 if ok, -1 if error occurred.
 * Returns 3 if IRC session should start over, for example when the user
 * has specified a new server or an error occurred with the connection.
 */
int
doirc()
{
	int r = 0, i, dcc_max;
	fd_set rfds;
	char *line;
	register unsigned char *t, *tt;
	struct servmsg sm;
	struct iw_msg msg;
	void (*reacttbl[MAXSCMD]) __P((struct servmsg *));
	char lbuf[MSGSZ*2];
	int maxarr[4], rmax;
	struct timeval sto;

	line = chkmem(malloc(BUFSZ));

	/*
	 * Initialize the command reaction table.
	 */

	for (i = 1; i < MAXSCMD; i++)
		reacttbl[i] = cmd_generic;

	reacttbl[0] = cmd_nonum;
	reacttbl[1] = cmd_welcomed;
	reacttbl[RPL_CHANNELMODEIS] = cmd_cmode;
	reacttbl[RPL_NAMREPLY] = cmd_names;
	reacttbl[ERR_NICKNAMEINUSE] = cmd_nickused;
	reacttbl[ERR_NICKCOLLISION] = cmd_nickused;
	reacttbl[ERR_ERRONEUSNICKNAME] = cmd_nickused;
	reacttbl[437] = cmd_unavailable;
		
#ifdef	SUPPRESS_ENDOF
	reacttbl[RPL_ENDOFWHOIS] = cmd_ignore;
	reacttbl[RPL_ENDOFWHOWAS] = cmd_ignore;
	reacttbl[RPL_ENDOFWHO] = cmd_ignore;
	reacttbl[RPL_ENDOFNAMES] = cmd_ignore;
	reacttbl[RPL_ENDOFLINKS] = cmd_ignore;
	reacttbl[RPL_ENDOFBANLIST] = cmd_ignore;
	reacttbl[RPL_ENDOFINFO] = cmd_ignore;
	reacttbl[RPL_ENDOFMOTD] = cmd_ignore;
	reacttbl[RPL_ENDOFUSERS] = cmd_ignore;
	reacttbl[RPL_ENDOFSTATS] = cmd_ignore;
	reacttbl[RPL_ENDOFSERVICES] = cmd_ignore;
	reacttbl[RPL_LISTEND] = cmd_ignore;
#endif

	/* Update the status lines. */
	iw_draw();
	
	/*
	 * Register with server.
	 */
	if (pass != NULL)
		dprintf(sock, "PASS %s\r\n", pass);
	dprintf(sock, "NICK %s\r\n", nick);
	dprintf(sock, "USER %s %s * :%s\r\n", loginname, (clname != NULL &&
		*clname != '\0') ? clname : "*", realname);

	/*
	 * Processing...
	 */
	for (;;) {
		setlog(1);

		FD_ZERO(&rfds);
		FD_SET(STDIN_FILENO, &rfds);
		if (sock > 0)
			FD_SET(sock, &rfds);
		if (syspipe > 0)
			FD_SET(syspipe, &rfds);
		dcc_max = dcc_fd_set(&rfds);

		maxarr[0] = sock;
		maxarr[1] = STDIN_FILENO;
		maxarr[2] = dcc_max;
		maxarr[3] = syspipe;
		rmax = getmax(maxarr, 4);

		sto.tv_sec = 1;
		sto.tv_usec = 0;

		busy = 0;
	restartsc:
		if (!in_buffered() && select(rmax+1, &rfds, NULL, NULL,
				system_out_waiting() ? &sto : NULL) < 0) {
			if (errno == EINTR) {
				check_sigint();
				check_sigwinch();
				FD_ZERO(&rfds);
			} else {
				busy = 1;
				iw_printf(COLI_WARN, "%s%s%s %s%s\n", TBOLD,
					ppre, Strerror(errno), timestamp(),
					TBOLD);
				restart_irc = 2;
				goto tryrestart;
			}
		}

		busy = 1;

		/* Check for window resizing */
		check_sigwinch();

		/* Don't accept input and update if screen gets too small */
		if (t_columns < 20)
			continue;

		/*
		 * Now act on luser input (not if we did not select,
		 * that is we are processing lines from the alternate
		 * in-buffer).
		 */
		if (FD_ISSET(STDIN_FILENO, &rfds))
			do
				if (!is_in_odlg())
					parsekey();
				else
					odlg_parsekey();
			while (tty_peek());

		/* 
		 * Then, parse the stuff that comes from the server
		 * (or has been injected with the /parse command).
		 */
		if (in_buffered() || (sock && FD_ISSET(sock, &rfds))) {
			r = 0;
			while ((r = get_buffered(line)) != -1
			    || (r = dgets(sock, BUFSZ, line, 0)) > 0) { 
				if (r > MSGSZ) {
					iw_printf(COLI_WARN,
"WARNING: server message length exceeds the protocol maximum of %d characters \
, line truncated and trailing characters discarded.", MSGSZ);
					line[MSGSZ-1] = '\0';
				}
				/*
				 * Strip any occurrences of mIRC inline
				 * colour crap.
				 */
				if (check_conf(CONF_HIDEMCOL))
					strip_mirc_colours(line);
				/*
				 * Substitute all unprintable chars except
				 * ^A to avoid flashing etc.  ^G is recognized
				 * and treated seperately.
				 * If CONF_ATTRIBS is enabled, ^B, ^_, ^V and
				 * ^O are not filtered out.
				 * Now this looks ugly again, time for tirc2.
				 */
				if (check_conf(CONF_ATTRIBS)) {
				    for (t = (unsigned char *)line, 
					tt = (unsigned char *)lbuf;
					*t != 0; t++)
					    if ((*t > 31 && *t < 128)
						|| *t > 160)
						    *tt++ = *t;
					    else
						switch (*t) {
						case '\b': case '\1': case '\r':
						case '\n': case '': case '':
						case '': case '': case '':
							*tt++ = *t;
							break;
						default:
							*tt++ = '^';
							*tt++ = *t+'@';
						}
				}
				else {
				    for (t = (unsigned char *)line, 
					tt = (unsigned char *)lbuf;
						*t != 0; t++)
					if ((*t > 31 && *t < 128)
					   || *t > 160 || *t == '\b'
					   || *t == '\1' || *t == '\r'
					   || *t == '\n' || *t == '')
						*tt++ = *t;
					else {
						*tt++ = '^';
						*tt++ = *t+'@';
					}
				}
				*tt = '\0';

				if (splitline(lbuf, &sm) < 0)
					/* line was broken, discard */
					continue;

				/* 
				 * Set off debug message if debug windows
				 * present.
				 */
				if (debugwin > 0) {
				    if ((t = (unsigned char *)
					strchr(lbuf, '\r')) != NULL)
					    *t = '\n';
				    msg.iwm_text = sm.sm_orig;
				    msg.iwm_type = IMT_DEBUG;
				    msg.iwm_chn = NULL;
				    msg.iwm_chint = COLI_TEXT;
				    dispose_msg(&msg);
				}

				/* React on command */
				(*reacttbl[sm.sm_num])(&sm);

				elhome();
			}
			if (r == -2) {
				iw_printf(COLI_WARN, "%s%sServer has closed \
the connection (EOF) %s%s\n", TBOLD, ppre, timestamp(), TBOLD);
				restart_irc = 2;
			}
			else if (r == -1) {
				iw_printf(COLI_WARN, "%s%sError with server \
connection: %s %s%s\n", TBOLD, ppre, Strerror(errno), timestamp(), TBOLD);
				restart_irc = 2;
			}
		}

		/*
		 * Check if any output from a spawned command is waiting...
		 */
		if (syspipe > 0 && FD_ISSET(syspipe, &rfds) &&
		    dgets(syspipe, BUFSZ, line, 0) > 0) {
			/*
			 * Substitute all tabs and ESC with spaces
			 * to prevent messing up the backscroll.
			 * Not nice but it avoids the worst.
			 */
			for (i = 0; (unsigned)i < strlen(line); i++)
				if (line[i] == '\t' || line[i] == 0x1b)
					line[i] = ' ';
			strcat(line, "\n");
			system_printline(line);
			elhome();
		}
		else if (closesyspipe) {
			extern char *s_target;	/* in system.c */

			close(syspipe);
			dg_freebuffer(syspipe);
			closesyspipe = syspipe = 0;
			if (s_target != NULL)
				free(s_target);
			iw_printf(COLI_TEXT, "%sProcess %d terminated with \
status %d\n", ppre, systempid, system_ecode);
		}

		/* Send queued /system privmsg */
		if (system_out_waiting())
			system_sendpmsg();

		/*
		 * Check DCC control and chat pipes.
		 */
		ctldcc(&rfds);

		/* Run periodic queues. */
		runpqueues();

		/* Update the clock if required. */
		if (update_clock) {
			update_clock = 0;
			busy = 0;
			disp_clock();
		}

		/* 
		 * Do we have to close the connection and
		 * restart the session?
		 */
		tryrestart:
		if (restart_irc) {
			system_dequeue();
			if (sock > 0) {
			    iw_printf(COLI_TEXT, "%sClosing connection with \
%s\n", ppre, srvnm);
			    on_irc = 0;
			    close(sock);
			    dg_freebuffer(sock);
			}
			free(line);
			umd = 0;
			delallchan();
			iw_draw();
			ood_clean();
			pq_del(ilbuf_pqe);
			if (atomic_idleexceed) {
				closemlog();
				Exit(1);
			}
			if (user_exit_flag) {
				closemlog();
				Exit(0);
			}
 			return 3;
		}

		/* Periodically look for new mail. */
		check_mailbox();

		/*
		 * Check if user idle time limit exceeded, to prevent
		 * forgotten clients hogging the system.
		 */
		if (atomic_idleexceed) {
			iw_printf(COLI_WARN, "%s%sUser idle limit exceeded \
%s%s\n", TBOLD, ppre, timestamp(), TBOLD);
			quitirc("User idle limit exceeded");
		}

		/* Hook for making a final cursor reposition. */
		iloop_cursor_hook();
	}
	/*NOTREACHED*/
}

static char *
skipspace(s)
	char *s;
{
	while (*s && *s == ' ')
		s++;
	return s;
}

/* Get the maximum value from an array of integer values. */
int
getmax(arr, count)
	int arr[], count;
{
	register int m = 0;

	while (--count >= 0)
		if (arr[count] > m)
			m = arr[count];
	return m;
}

/*-
 * Split a line from the server into its subparts.
 *	
 * According to RfC1459, a message format in pseudo-BNF looks like the
 * following:
 *
 * <message>  ::= [':' <prefix> <SPACE> ] <command> <params> <crlf>
 * <prefix>   ::= <servername> | <nick> [ '!' <user> ] [ '@' <host> ]
 * <command>  ::= <letter> { <letter> } | <number> <number> <number>
 * <SPACE>    ::= ' ' { ' ' }
 * <params>   ::= <SPACE> [ ':' <trailing> | <middle> <params> ]
 *
 * <middle>   ::= <Any *non-empty* sequence of octets not including SPACE
 *		   or NUL or CR or LF, the first of which may not be ':'>
 * <trailing> ::= <Any, possibly *empty*, sequence of octets not including
 *		   NUL or CR or LF>
 * <crlf>     ::= CR LF
 *
 * Splitline extracts the prefix as string, the numerical value of the 
 * command and an array of up to 15 parameter strings and stores them in
 * the servmsg structure.
 * Note: the pointers in servmsg should not be valid when passing to this
 * function. Splitline won't allocate memory to these pointers but lets
 * them point to the respective parts in a copy of the `line' line. 
 * Note: the contents of `sm' are only valid between calls to splitline
 * since each call clobbers the internal buffer.
 * If the command only consists of a string instead of a numerical value,
 * sm_num is set to 0 and sm_cmd points to the command string (example
 * `PING').
 *
 * Splitline returns -1 on error, which indicates a broken server message
 * line and 0 otherwise.
 */
static int
splitline(line, sm)
	char *line;
	struct servmsg *sm;
{
	char *t, *u;
	int i;
	static char *buff;

	/* Allocate buffer */
	if (buff != NULL)
		free(buff);
	t = buff = chkmem(malloc(BUFSZ));

	/* Make work copy of line */
	strncpy(buff, line, BUFSZ-1);
	buff[BUFSZ-1] = 0;
	sm->sm_orig = line;

	/* First, kill CR and LF if they exist and skip starting spaces */
	if ((u = strchr(t, '\r')) != NULL)
		*u = 0;
	if ((u = strchr(t, '\n')) != NULL)
		*u = 0;
	t = skipspace(t);

	/* Get prefix if it exists */
	if (*t == ':') {
		sm->sm_prefix = t+1;
		if ((t = strchr(t, ' ')) == NULL)
			return -1;
		*t++ = 0;
		t = skipspace(t);
	}
	else
		sm->sm_prefix = NULL;

	/* Extract command */
	if ((u = strchr(t, ' ')) == NULL)
		return -1;
	*u = 0;
	if (isdigit(*t)) {
		if (check_conf(CONF_NUMERICS)) {
			/* XXX numerics must be 3 chars (which they are in the
			 * current protocol) */
			strncpy(sm->sm_anum, t, 3);
			sm->sm_anum[3] = '\0';
			strcat(sm->sm_anum, " ");
		}
		else
			strcpy(sm->sm_anum, ppre);
		sm->sm_num = atoi(t);
		sm->sm_cmd = NULL;
	}
	else {
		strcpy(sm->sm_anum, ppre);
		sm->sm_num = 0;
		sm->sm_cmd = t;
	}
	t = skipspace(u+1);
		
	/* Get parameters */
	i = 0;
	while (strlen(t) && i < 15) {
		i++;
		if (*t == ':') {
			sm->sm_par[i-1] = t+1;
			break;
		}
		sm->sm_par[i-1] = t;
		if ((u = strchr(t, ' ')) == NULL)
			break;
		*u = 0;
		t = skipspace(u+1);
	}
	sm->sm_npar = i;
	return 0;
}

/*
 * Ignore the server command.
 */
/*ARGSUSED*/
static void
cmd_ignore(sm)
	struct servmsg *sm;
{
	/* nop */
}

/*
 * Get the source (nickname or address) from the sm_prefix field.
 */
void
from_nick(sm, nickname)
	struct servmsg *sm;
	char *nickname;
{
	char *t;

	if (sm->sm_prefix == NULL) {
		*nickname = '\0';
		return;
	}

	if ((t = strchr(sm->sm_prefix, '!')) != NULL) {
		if (t - sm->sm_prefix > NICKSZ) {
			iw_printf(COLI_WARN,
"WARNING: nick length in svr msg exceeds protocol maximum of %d characters. \
Nickname truncated.", NICKSZ);
			t = sm->sm_prefix + NICKSZ;
		}
		strncpy(nickname, sm->sm_prefix, (unsigned)(t-sm->sm_prefix));
		nickname[t-sm->sm_prefix] = '\0';
	}
	else
		*nickname = '\0';
}

/*
 * Reacts on commands that are not sent in a numerical format.
 */
static void
cmd_nonum(sm)
	struct servmsg *sm;
{
	static char nname[MSGSZ], t[MSGSZ], *b;
	struct iw_msg wm;
	struct channel *ch;

	if (!strcmp(sm->sm_cmd, "PRIVMSG") && sm->sm_npar > 1) {
		/*
		 * If the source is on our ignore-list, discard.
		 */
		if (check_ignore(sm))
			return;
		/*
		 * Regulate ^G (beep).
		 */
		if ((b = strchr(sm->sm_par[1], '')) != NULL) {
#ifdef	__STDC__
			*b = (signed char) 164;
#else
			*b = 164;
#endif
			if (check_conf(CONF_BEEP))
				dobell();
		}
		/*
		 * Check for ctcp stuff
		 */
		if (*sm->sm_par[1] == '\1') {
		    if (flregister(sm, 1, FL_MSG))
		    	return;
		    ctcpmsg(sm);
		    return;
		}

		/* 
		 * Is recipient a channel window?
		 */
		if (ISCHANNEL(sm->sm_par[0])) {
		    int fgc, bgc;
		    static char nocs[] = "";
		    char *colstr, *coloff;

		    if (flregister(sm, 1, FL_PUBLIC))
		    	return;

		    if ((ch = getchanbyname(sm->sm_par[0])) == NULL)
			return;

		    /* Check for URLs */
		    urlcheck(sm->sm_par[1], sm->sm_prefix);

		    from_nick(sm, nname);

#ifdef	WITH_ANSI_COLOURS
		    if (check_conf(CONF_NCOLOUR | CONF_COLOUR)
			&& ncol_match(nname, &fgc, &bgc))
			    colstr = set_colour(fgc, bgc);
		    else
			colstr = nocs;

		    if (wchanistop(ch->ch_name))
			sprintf(t, "<%s%s%d,%d;> %s\n", colstr, nname,
				get_fg_colour_for(COLI_CHANMSG),
				get_bg_colour_for(COLI_CHANMSG),
				sm->sm_par[1]);
		    else
			sprintf(t, "<%s%s%d,%d;:%s> %s\n", colstr, nname,
				get_fg_colour_for(COLI_CHANMSG),
				get_bg_colour_for(COLI_CHANMSG),
				ch->ch_name, sm->sm_par[1]);
#else
		    if (wchanistop(ch->ch_name))
			sprintf(t, "<%s> %s\n", nname, sm->sm_par[1]);
		    else
			sprintf(t, "<%s:%s> %s\n", nname, ch->ch_name,
				sm->sm_par[1]);
#endif	/* ! WITH_ANSI_COLOURS */

		    wm.iwm_text = t;
		    wm.iwm_type = IMT_CMSG | IMT_FMT;
		    wm.iwm_chn = ch;
		    wm.iwm_chint = COLI_CHANMSG;
		    dispose_msg(&wm);

		    return;
		}

		/* 
		 * Is it private for us?
		 */
		if (!irc_strcmp(sm->sm_par[0], nick)) {
		    if (flregister(sm, 1, FL_MSG))
		    	return;
		    if (checkspam(sm->sm_par[1]) > 0)
			return;

		    /* Check for URLs */
		    urlcheck(sm->sm_par[1], sm->sm_prefix);

		    from_nick(sm, nname);

		    /* Look if it is from user being queried */
		    ch = getquerybyname(nname);

		    if (check_conf(CONF_HIMSG) && ch == NULL)
			    sprintf(t, "%s*%s%s%s* %s\n", TIMESTAMP, TBOLD,
			   	nname, TBOLD, sm->sm_par[1]);
		    else
			    sprintf(t, "%s*%s* %s\n", TIMESTAMP, nname,
			    	sm->sm_par[1]);
		    if (lastlog != NULL)
			    fprintf(lastlog, "%s*%s* %s\n", timestamp(), nname,
				sm->sm_par[1]);
		    if (msglog != NULL)
			    fprintf(msglog, "%s*%s* %s\n", timestamp(), nname,
				sm->sm_par[1]);

		    wm.iwm_text = t;
		    if (ch == NULL) {
			wm.iwm_type = IMT_PMSG | IMT_FMT;
			wm.iwm_chint = COLI_PRIVMSG;
		    }
		    else {
			wm.iwm_type = IMT_CMSG | IMT_FMT;
			wm.iwm_chint = COLI_CHANMSG;
		    }
		    wm.iwm_chn = ch;
		    dispose_msg(&wm);

		    if (ch == NULL)
		    	add_nickhist(nname);
		    return;
		}

		/*
		 * Broadcast PRIVMSG, display with source and target.
		 */
		if (flregister(sm, 1, FL_MSG))
			return;
		if (checkspam(sm->sm_par[1]) > 0)
			return;

		/* Check for URLs */
		urlcheck(sm->sm_par[1], sm->sm_prefix);

		from_nick(sm, nname);
		if (check_conf(CONF_HIMSG))
			sprintf(t, "%s#%s%s:%s%s# %s\n", TIMESTAMP, TBOLD,
				NICKORPREFIX(nname), TBOLD, sm->sm_par[0],
				sm->sm_par[1]);
		else
			sprintf(t, "%s#%s:%s# %s\n", TIMESTAMP,
				NICKORPREFIX(nname),
				sm->sm_par[0], sm->sm_par[1]);
		if (lastlog != NULL)
			fprintf(lastlog, "%s#%s:%s# %s\n", timestamp(), 
				NICKORPREFIX(nname),
				sm->sm_par[0], sm->sm_par[1]);
		if (msglog != NULL)
			fprintf(msglog, "%s#%s:%s# %s\n", timestamp(), 
				NICKORPREFIX(nname),
				sm->sm_par[0], sm->sm_par[1]);

		wm.iwm_text = t;
		wm.iwm_type = IMT_PMSG | IMT_FMT;
		wm.iwm_chn = NULL;
		wm.iwm_chint = COLI_PRIVMSG;
		dispose_msg(&wm);

		if (*nname != '\0')
			add_nickhist(nname);

		return;
	}

	if (!strcmp(sm->sm_cmd, "NOTICE") && (sm->sm_npar > 1)) {
		/*
		 * If the source is on our ignore-list, discard.
		 * Undernet and efnet ircd send some server NOTICEs
		 * without cmd prefix.
		 */
		if (sm->sm_prefix != NULL) {
			if (check_ignore(sm))
				return;
			if (flregister(sm, 1, FL_NOTICE))
				return;
		}
		/*
		 * Regulate ^G (beep).
		 */
		if ((b = strchr(sm->sm_par[1], '')) != NULL) {
#ifdef	__STDC__
			*b = (signed char) 164;
#endif
			if (check_conf(CONF_BEEP))
				dobell();
		}

		/* ctcp stuff */
		if (*sm->sm_par[1] == '\1') {
		    ctcpnotice(sm);
		    return;
		}
		if (checkspam(sm->sm_par[1]) > 0)
			return;

		/*
		 * Check for URLs
		 */
		urlcheck(sm->sm_par[1], sm->sm_prefix);

		from_nick(sm, nname);
		if (!irc_strcmp(sm->sm_par[0], nick)) {
		    if (check_conf(CONF_HIMSG))
			sprintf(t, "%s-%s%s%s- %s\n", TIMESTAMP, TBOLD,
				NICKORPREFIX(nname), TBOLD, sm->sm_par[1]);
		    else
			sprintf(t, "%s-%s- %s\n", TIMESTAMP,
				NICKORPREFIX(nname),
				sm->sm_par[1]);
		    if (lastlog != NULL)
		    	fprintf(lastlog, "%s-%s- %s\n", timestamp(),
				NICKORPREFIX(nname),
				sm->sm_par[1]);
		    if (msglog != NULL)
		    	fprintf(msglog, "%s-%s- %s\n", timestamp(),
				NICKORPREFIX(nname),
				sm->sm_par[1]);
		}
		else {
		    if (*sm->sm_par[0] == '&') {
			/* Message to a local channel */
			ch = getchanbyname(sm->sm_par[0]);

			if (ch != NULL) {
			    if (wchanistop(ch->ch_name))
				sprintf(t, "-%s- %s\n", sm->sm_prefix,
					sm->sm_par[1]);
			    else
				sprintf(t, "-%s:%s- %s\n", sm->sm_prefix,
					ch->ch_name, sm->sm_par[1]);

			    wm.iwm_text = t;
			    wm.iwm_type = IMT_CMSG | IMT_FMT;
			    wm.iwm_chn = ch;
		    	    wm.iwm_chint = COLI_CHANMSG;
			    dispose_msg(&wm);
			    return;
			}
		    }

		    if (check_conf(CONF_HIMSG))
			sprintf(t, "%s-%s%s:%s%s- %s\n", TIMESTAMP, TBOLD,
				NICKORPREFIX(nname), TBOLD,
				sm->sm_par[0], sm->sm_par[1]);
		    else
			sprintf(t, "%s-%s:%s- %s\n", TIMESTAMP,
				NICKORPREFIX(nname),
				sm->sm_par[0], sm->sm_par[1]);
		    if (lastlog != NULL)
		    	fprintf(lastlog, "%s-%s:%s- %s\n", timestamp(),
				NICKORPREFIX(nname),
				sm->sm_par[0], sm->sm_par[1]);
		    if (msglog != NULL)
		    	fprintf(msglog, "%s-%s:%s- %s\n", timestamp(),
				NICKORPREFIX(nname),
				sm->sm_par[0], sm->sm_par[1]);
		}

		wm.iwm_text = t;
		wm.iwm_type = IMT_PMSG | IMT_FMT;
		wm.iwm_chn = NULL;
		wm.iwm_chint = COLI_PRIVMSG;
		dispose_msg(&wm);
		return;
	}

	if (!strcmp(sm->sm_cmd, "PING")) {
		dprintf(sock, "PONG :%s\r\n", sm->sm_par[0]);
		return;
	}

	if (!strcmp(sm->sm_cmd, "PONG")) {
		iw_printf(COLI_TEXT, "%sPONG from %s\n", ppre, sm->sm_prefix);
		return;
	}

	if (!strcmp(sm->sm_cmd, "ERROR")) {
		iw_printf(COLI_WARN, "%s%s%s %s%s\n", TBOLD, ppre,
			sm->sm_par[0], TIMESTAMP, TBOLD);
		restart_irc = 2;
		return;
	}

	if (!strcmp(sm->sm_cmd, "TOPIC")) {
		from_nick(sm, nname);
		sprintf(t, "%s%s changes topic for %s to %s %s\n", ppre,
			NICKORPREFIX(nname), sm->sm_par[0], sm->sm_par[1],
			TIMESTAMP);
		wm.iwm_text = t;
		wm.iwm_type = IMT_CMSG | IMT_FMT;
		wm.iwm_chn = getchanbyname(sm->sm_par[0]);
		wm.iwm_chint = COLI_SERVMSG;
		dispose_msg(&wm);
		return;
	}

	if (!strcmp(sm->sm_cmd, "INVITE")) {
		if (check_ignore(sm))
			return;
		if (flregister(sm, 1, FL_INVITE))
			return;

		from_nick(sm, nname);
		iw_printf(COLI_TEXT, "%s%s invites you to channel %s %s\n",
			ppre, nname, sm->sm_par[1], TIMESTAMP);
	}

	if (!strcmp(sm->sm_cmd, "MODE")) {
		update_cucache(sm);
		parse_mode(sm);
		return;
	}

	if (!strcmp(sm->sm_cmd, "JOIN")) {
		update_cucache(sm);
		join_chan(sm);
		return;
	}

	if (!strcmp(sm->sm_cmd, "PART")) {
		update_cucache(sm);
		part_chan(sm);
		return;
	}

	if (!strcmp(sm->sm_cmd, "QUIT")) {
		from_nick(sm, nname);
		if (strstr(sm->sm_par[0], "Local Kill by")
		    || strstr(sm->sm_par[0], "Killed")
		    || !strncmp(sm->sm_par[0], "Excess Flood", 12)
		    || !strncmp(sm->sm_par[0], "Ping Timeout", 12)
		    || !strncmp(sm->sm_par[0], "Ping timeout", 12)
		    || !strncmp(sm->sm_par[0], "Dead Socket", 11)
		    || !strncmp(sm->sm_par[0], "Connection timed out", 20)
		    || !strncmp(sm->sm_par[0], "No route to host", 16)
		    || !strncmp(sm->sm_par[0], "Idle time limit exceeded", 24))
		    	sprintf(t,
			    "%sSignoff: %s thrown out by server (%s) %s\n",
			    ppre, NICKORPREFIX(nname), sm->sm_par[0],
			    TIMESTAMP);
		else
			sprintf(t, "%sSignoff: %s has quit (%s) %s\n", ppre,
				nname, sm->sm_par[0], TIMESTAMP);
		wm.iwm_text = t;

		if ((ch = userchan_cpage(nname)) != NULL) {
			wm.iwm_type = IMT_CMSG | IMT_FMT;
			wm.iwm_chn = ch;
		    	wm.iwm_chint = COLI_SERVMSG;
			dispose_msg(&wm);
		}
		else if (getfromucache(nname, cha, &ch, 1) == NULL ||
		    ch == NULL) {
			wm.iwm_type = IMT_CTRL | IMT_FMT;
			wm.iwm_chn = NULL;
		    	wm.iwm_chint = COLI_SERVMSG;
			dispose_msg(&wm);
		}
		else {
			wm.iwm_type = IMT_CMSG | IMT_FMT;
			wm.iwm_chn = ch;
		    	wm.iwm_chint = COLI_SERVMSG;
			dispose_msg(&wm);
		}
		update_cucache(sm);
		dcc_update(sm);
		return;
	}

	if (!strcmp(sm->sm_cmd, "KICK")) {
		from_nick(sm, nname);
		ch = getchanbyname(sm->sm_par[0]);
		assert(ch != NULL);
		if (!irc_strcmp(sm->sm_par[1], nick) && ch != NULL) {
			sprintf(t, "%sYou have been kicked off channel %s by \
%s (%s) %s\n", ppre, sm->sm_par[0], nname, sm->sm_par[2],
				TIMESTAMP);
			wm.iwm_text = t;
			wm.iwm_type = IMT_CMSG | IMT_FMT;
			wm.iwm_chn = ch;
		    	wm.iwm_chint = COLI_SERVMSG;
			dispose_msg(&wm);

			iw_delchan(ch);
			delchan(ch);
			set_prompt(NULL);
			iw_draw();
			elrefr(0);
		}
		else {
			update_cucache(sm);
			sprintf(t, "%s%s kicks %s off channel %s (%s) %s\n",
				ppre, nname, sm->sm_par[1], sm->sm_par[0],
				sm->sm_par[2], TIMESTAMP);
			wm.iwm_text = t;
			wm.iwm_type = IMT_CMSG | IMT_FMT;
			wm.iwm_chn = ch = getchanbyname(sm->sm_par[0]);
		    	wm.iwm_chint = COLI_SERVMSG;
			dispose_msg(&wm);
		}

		return;
	}

	if (!strcmp(sm->sm_cmd, "NICK")) {
		if (check_ignore(sm)) {
			update_cucache(sm);
			dcc_update(sm);
			return;
		}
		if (flregister(sm, 0, FL_NICK)) {
			update_cucache(sm);
			dcc_update(sm);
			return;
		}

		from_nick(sm, nname);
		sprintf(t, "%s%s changes nickname to %s %s\n", ppre, nname,
			sm->sm_par[0], TIMESTAMP);
		wm.iwm_text = t;

		if ((ch = userchan_cpage(nname)) != NULL) {
			wm.iwm_type = IMT_CMSG | IMT_FMT;
			wm.iwm_chn = ch;
		    	wm.iwm_chint = COLI_SERVMSG;
			dispose_msg(&wm);
		}
		else if (!irc_strcmp(nname, nick) ||
		    getfromucache(nname, cha, &ch, 1) == NULL || ch == NULL) {
			wm.iwm_type = IMT_CTRL | IMT_FMT;
			wm.iwm_chn = NULL;
		    	wm.iwm_chint = COLI_SERVMSG;
			dispose_msg(&wm);
		}
		else {
			wm.iwm_type = IMT_CMSG | IMT_FMT;
			wm.iwm_chn = ch;
		    	wm.iwm_chint = COLI_SERVMSG;
			dispose_msg(&wm);
		}
		if (!irc_strcmp(nname, nick)) {
			strncpy(nick, sm->sm_par[0], NICKSZ);
			nick[NICKSZ] = 0;
			iw_draw();
			xterm_settitle();
		}

		update_cucache(sm);
		dcc_update(sm);

		/* Update query */
		if ((ch = getquerybyname(nname)) != NULL) {
			strncpy(ch->ch_name+1, sm->sm_par[0], NICKSZ);
			ch->ch_name[NICKSZ+1] = 0;
			iw_draw();
			elrefr(0);
		}
		return;
	}

	if (!strcmp(sm->sm_cmd, "KILL")) {
		from_nick(sm, nname);
		sprintf(t, "%s%s has been killed by %s, %s %s\n", ppre,
			sm->sm_par[0],
			NICKORPREFIX(nname), sm->sm_par[1], TIMESTAMP);
		wm.iwm_text = t;

		if ((ch = userchan_cpage(sm->sm_par[0])) != NULL) {
			wm.iwm_type = IMT_CMSG | IMT_FMT;
			wm.iwm_chn = ch;
		    	wm.iwm_chint = COLI_SERVMSG;
			dispose_msg(&wm);
		}
		else if (!irc_strcmp(sm->sm_par[0], nick) ||
		    getfromucache(nname, cha, &ch, 1) == NULL || ch == NULL) {
			wm.iwm_type = IMT_CTRL | IMT_FMT;
			wm.iwm_chn = NULL;
		    	wm.iwm_chint = COLI_SERVMSG;
			dispose_msg(&wm);
		}
		else {
			wm.iwm_type = IMT_CMSG | IMT_FMT;
			wm.iwm_chn = ch;
		    	wm.iwm_chint = COLI_SERVMSG;
			dispose_msg(&wm);
		}
		update_cucache(sm);
		dcc_update(sm);
		return;
	}
}

/*
 * Simply print the parameters starting with the second one and do 
 * nothing else.
 */
static void
cmd_print(sm)
	struct servmsg *sm;
{
	int i;

	iw_printf(COLI_SERVMSG, PPREORNUM(sm));
	for (i = 1; i < sm->sm_npar; i++)
		iw_printf(COLI_TEXT, "%s ", sm->sm_par[i]);
	iw_printf(COLI_SERVMSG, "\n");
}

/*
 * Server accepted us.  Run the commands from the rc-file and get our
 * source address.
 */
static void
cmd_welcomed(sm)
	struct servmsg *sm;
{
	on_irc = 1;

	if (srvnm != NULL)
		free(srvnm);
	srvnm = Strdup(sm->sm_prefix);
	iw_printf(COLI_TEXT, "%s%s\n", PPREORNUM(sm), sm->sm_par[1]);
	if (irc_strcmp(sm->sm_par[0], nick)) {
		iw_printf(COLI_TEXT,
    "%sNote: server registered you with nickname \"%s\" instead of \"%s\"\n",
			ppre, sm->sm_par[0], nick);
		strncpy(nick, sm->sm_par[0], NICKSZ);
		nick[NICKSZ] = '\0';
	}
	iw_draw();
	rccommands(RC_IS_CONNECTED);
	dprintf(sock, "WHOIS %s\r\n", nick);
	xterm_settitle();
}

/*
 * Nickname is already in use.  Let the user change it.
 */
static void
cmd_nickused(sm)
	struct servmsg *sm;
{
	static char nuprompt[] = "Enter different nickname: ";

	cmd_print(sm);
	set_prompt(nuprompt);
	linedone(0);
	othercmd = newnick;
	f_elhome();
}

/*
 * New nick entered.  Use this one.
 */
static void
newnick(n)
	char *n;
{
	othercmd = NULL;
	dprintf(sock, "NICK %s\r\n", n);
	strncpy(nick, n, NICKSZ);
	nick[NICKSZ] = 0;
	set_prompt(NULL);
	iw_draw();
	elrefr(1);
}

/*
 * Act upon a mode change issued by the server.
 *
 * Channel modes:
 * Parameters: <channel> {[+|-]|o|p|s|i|t|n|b|v} [<limit>] [<user>]
 *		[<ban mask>]
 * User modes:
 * Parameters: <nickname> {[+|-]|i|w|s|o}
 */
#define CHUMOD(type)\
	{ if (mode) umd |= (type); else umd &= ~(type); }
#define CHCMOD(type)\
	{ if (mode) ch->ch_modes |= (type); else ch->ch_modes &= ~(type); }

static void
parse_mode(sm)
	struct servmsg *sm;
{
	char t[BUFSZ], *u, from[MSGSZ];
	int i, mode;
	struct channel *ch = NULL;
	struct iw_msg wm;

	if (!sm->sm_npar)
		return;
	from_nick(sm, from);

	if (ISCHANNEL(sm->sm_par[0])) {
		/*
		 * Change channel mode.
		 */
		if ((ch = getchanbyname(sm->sm_par[0])) == NULL)
			return;
		strcpy(t, sm->sm_par[0]);
		i = 2;
		u = sm->sm_par[1];
		while (*u)
			switch(*u++) {
			case '+':
				mode = 1;
				break;
			case '-':
				mode = 0;
				break;
			case 'o':
				if (!irc_strcmp(sm->sm_par[i], nick))
					CHCMOD(MD_O);
				changecacheumode(sm->sm_par[i], ch, MD_O,
					mode);
				i++;
				break;
			case 'p':
				CHCMOD(MD_P)
				break;
			case 's':
				CHCMOD(MD_S)
				break;
			case 'i':
				CHCMOD(MD_I)
				break;
			case 't':
				CHCMOD(MD_T)
				break;
			case 'n':
				CHCMOD(MD_N)
				break;
			case 'm':
				CHCMOD(MD_M)
				break;
			case 'q':
				CHCMOD(MD_Q)
				break;
			case 'k':
				CHCMOD(MD_K);
				i++;
				break;
			case 'v':
				if (!irc_strcmp(sm->sm_par[i], nick))
					CHCMOD(MD_V);
				changecacheumode(sm->sm_par[i], ch, MD_V,
					mode);
				i++;
				break;
			case 'l':
				CHCMOD(MD_L);
				i++;
				break;
			case 'b':
				i++;
				break;
			}
	}
	else {

		/*
		 * Change user mode.
		 */
		strcpy(t, sm->sm_par[0]);
		if (!irc_strcmp(t, nick)) {
			u = sm->sm_par[1];
			while (*u)
				switch (*u++) {
				case '+':
					mode = 1;
					break;
				case '-':
					mode = 0;
					break;
				case 'i':
					CHUMOD(UM_I)
					break;
				case 's':
					CHUMOD(UM_S)
					break;
				case 'w':
					CHUMOD(UM_W)
					break;
				case 'o':
					CHUMOD(UM_O)
					break;
				case 'r':
					CHUMOD(UM_R)
					break;
				}
		}
	}

	sprintf(t, "%s%s changed mode %s", PPREORNUM(sm), NICKORPREFIX(from),
		sm->sm_par[0]);

	for (i = 1; i < sm->sm_npar; i++) {
		strcat(t, " ");
		strcat(t, sm->sm_par[i]);
	}
	strcat(t, " ");
	strcat(t, TIMESTAMP);
	strcat(t, "\n");
	wm.iwm_text = t;
	wm.iwm_type = (ch == NULL ? IMT_CTRL : IMT_CMSG) | IMT_FMT;
	wm.iwm_chn = ch;
	wm.iwm_chint = COLI_SERVMSG;
	dispose_msg(&wm);
	iw_draw();
}

/*
 * Leave IRC with the given comment.
 */
void
quitirc(comment)
	char *comment;
{
	if (on_irc) {
		dprintf(sock, "QUIT :%s\r\n", comment);
		on_irc = 0;
		xterm_settitle();
	}
}

/*
 * Return a ptr to the channel structure of the channel referenced
 * to by name.  Returns NULL if channel couldn't be found.
 */
struct channel *
getchanbyname(name)
	char *name;
{
	struct channel *ch = cha;
	char chn[CNAMESZ];
	unsigned long h;

	strcpy(chn, name);
	h = elf_hash(irc_strupr(chn));

	while (ch != NULL) {
		if (h == ch->ch_hash)
			return ch;
		ch = ch->ch_next;
	}
	return NULL;
}

/*
 * Parse a JOIN message.  If it is me who joins, we add the channel
 * to the global channel list and to the channel list of the current
 * window.
 */
static void
join_chan(sm)
	struct servmsg *sm;
{
	char who[MSGSZ], joinmode[16];
	char chn[201], *t, *m;
	char nname[NICKSZ+1];
	struct channel *ch;
	struct iw_msg wm;

	if (!sm->sm_prefix || sm->sm_npar < 1)
		return;
	from_nick(sm, nname);
	/*
	 * ircd 2.9: JOIN #channel^Gov adds +ov after (re-)joining.
	 * It's a mode shortcut, not part of the channel name.
	 */
	if ((m = t = strchr(sm->sm_par[0], '')) != NULL)
		*t = 0;
	ch = getchanbyname(sm->sm_par[0]);

	if (t != NULL)
		sprintf(joinmode, "+%s", ++m);

	if (!irc_strcmp(nname, nick)) {
		if (ch == NULL) {
			ch = chkmem(calloc(1, sizeof (struct channel)));
			ch->ch_next = ch->ch_prev = ch->ch_wnext =
				ch->ch_wprev = NULL;
			ch->ch_logfile = NULL;
			ch->ch_logfname = NULL;
			strcpy(ch->ch_name, sm->sm_par[0]);
			strcpy(chn, ch->ch_name);
			ch->ch_hash = elf_hash(irc_strupr(chn));
			if (cha == NULL)
				cha = ch;
			else {
				cha->ch_prev = ch;
				ch->ch_next = cha;
				cha = ch;
			}
			iw_addchan(ch);
			/* 
			 * Now get current modes for channel.
			 * This will also update the display.
			 */
			dprintf(sock, "MODE %s\r\n", ch->ch_name);
		}
		else {
			iw_delchan(ch);
			iw_addchan(ch);
		}
		set_prompt(NULL);
	}

	if ((t = strchr(sm->sm_prefix, '!')) != NULL)
		t++;
	else
		t = "";

	if (m != NULL && strlen(m))
		sprintf(who, "%s%s (%s) has joined channel %s (mode %s) %s\n",
			PPREORNUM(sm), nname, t, sm->sm_par[0], joinmode,
			TIMESTAMP);
	else
		sprintf(who, "%s%s (%s) has joined channel %s %s\n", 
			PPREORNUM(sm),
			nname, t, sm->sm_par[0], TIMESTAMP);

	wm.iwm_text = who;
	wm.iwm_type = IMT_CMSG | IMT_FMT;
	wm.iwm_chn = ch;
	wm.iwm_chint = COLI_SERVMSG;
	dispose_msg(&wm);
	elrefr(0);
}

/*
 * Parse a PART message.  If the user leaves a channel he is on, remove
 * the channel from the respective window's channel list and then
 * remove it from the global channel list.
 */
static void
part_chan(sm)
	struct servmsg *sm;
{
	char who[MSGSZ], t[MSGSZ];
	struct channel *ch;
	struct iw_msg wm;

	if (!sm->sm_prefix || sm->sm_npar < 1)
		return;
	if ((ch = getchanbyname(sm->sm_par[0])) == NULL)
		return;
	from_nick(sm, who);

	sprintf(t, "%sChange: %s has parted channel %s (%s) %s\n",
		PPREORNUM(sm), who, sm->sm_par[0], sm->sm_par[1], TIMESTAMP);
	wm.iwm_text = t;
	wm.iwm_type = IMT_CMSG | IMT_FMT;
	wm.iwm_chn = ch;
	wm.iwm_chint = COLI_SERVMSG;
	dispose_msg(&wm);

	if (!irc_strcmp(who, nick)) {
		iw_delchan(ch);
		delchan(ch);
		set_prompt(NULL);
	}
	iw_draw();
	elrefr(0);
}

/*
 * Delete the channel.  Remove it first from all windows with iw_delchan()!
 */
void
delchan(ch)
	struct channel *ch;
{
	clrucache(ch);
	if (ch->ch_logfile != NULL)
		ch_closelog(ch);
	if (ch->ch_prev != NULL)
		ch->ch_prev->ch_next = ch->ch_next;
	if (ch->ch_next != NULL)
		ch->ch_next->ch_prev = ch->ch_prev;
	if (cha == ch)
		cha = ch->ch_next;
	free(ch);
}

/*
 * elf_hash implements a hash function used in the SVR4 ELF format
 * for object files.
 * We use this to generate unique hash values for channel-, command-
 * and nicknames.
 */
unsigned long
elf_hash(name)
	const char *name;
{
	register unsigned long h = 0, g;

	while (*name) {
		h = (h << 4) + *(unsigned char *)name++;
		if ((g = h & 0xf0000000))
			h ^= g >> 24;
		h &= ~g;
	}
	return h;
}

/*
 * Same algorithm as elf_hash, but converts all chars to irc_upper rules
 * before using them for calculation.
 */
unsigned long
irc_elf_hash(name)
	const char *name;
{
	register unsigned long h = 0, g;

	while (*name) {
		h = (h << 4) + upctbl[*(unsigned char *)name++];
		if ((g = h & 0xf0000000))
			h ^= g >> 24;
		h &= ~g;
	}
	return h;
}

/*
 * Converts all characters in a string to uppercase obeying the
 * IRC lower/upper-case rules. Returns a pointer to the string.
 */
char *
irc_strupr(s)
	char *s;
{
	unsigned char *t = (unsigned char *) s;

	while (*t) {
		*t = upctbl[*t];
		t++;
	}
	return s;
}

/*
 * Compare two strings lexicographically according to the irc protocol
 * (case independent).
 */
int
irc_strcmp(s, t)
	char *s, *t;
{
	for (; upctbl[(unsigned)*s] == upctbl[(unsigned)*t]; s++, t++)
		if (*s == '\0')
			return 0;
	return upctbl[(unsigned)*s] - upctbl[(unsigned)*t];
}

/*
 * Compare two characters lexicographically.
 */
int
irc_chrcmp(a, b)
	int a, b;
{
	return upctbl[(unsigned)a] - upctbl[(unsigned)b];
}

/*
 * Does the same as irc_strcmp but limits comparison to first n chars.
 */
int
irc_strncmp(s, t, n)
	char *s, *t;
	size_t n;
{
	for (; upctbl[(unsigned)*s] == upctbl[(unsigned)*t] && n; s++, t++, n--)
		if (*s == '\0')
			return 0;
	if (n == 0)
		return 0;
	return upctbl[(unsigned)*s] - upctbl[(unsigned)*t];
}

/*
 * Gets mode for a channel from the server and stores it in the
 * channel structure.
 */
static void
cmd_cmode(sm)
	struct servmsg *sm;
{
	char *u, mode = 0;
	struct channel *ch;
	int i;
	char t[MSGSZ];
	struct iw_msg wm;

	if (sm->sm_npar < 2)
		return;
	t[0] = 0;

	sprintf(t, "%sChannel-mode for %s is ", PPREORNUM(sm), sm->sm_par[1]);
	for (i = 2; i < sm->sm_npar; i++)
		strcat(t, sm->sm_par[i]);
	strcat(t, "\n");
	
	wm.iwm_text = t;
	wm.iwm_chn = ch = getchanbyname(sm->sm_par[1]);
	if (ch == NULL)
		wm.iwm_type = IMT_CTRL | IMT_FMT;
	else
		wm.iwm_type = IMT_CMSG | IMT_FMT;
	wm.iwm_chint = COLI_SERVMSG;
	dispose_msg(&wm);

	if (ch == NULL)
		return;
	u = sm->sm_par[2];

	while (*u)
		switch(*u++) {
		case '+':
			mode = 1;
			break;
		case '-':
			mode = 0;
			break;
		case 'p':
			CHCMOD(MD_P)
			break;
		case 's':
			CHCMOD(MD_S)
			break;
		case 'i':
			CHCMOD(MD_I)
			break;
		case 't':
			CHCMOD(MD_T)
			break;
		case 'n':
			CHCMOD(MD_N)
			break;
		case 'm':
			CHCMOD(MD_M)
			break;
		case 'a':
			CHCMOD(MD_A)
			break;
		case 'k':
			CHCMOD(MD_K)
			break;
		}
	iw_draw();
}

/*
 * List the nicknames of lusers in a channel.
 */
static void
cmd_names(sm)
	struct servmsg *sm;
{
	char t[MSGSZ];
	struct iw_msg wm;
	struct channel *ch;
	struct cache_user *cu;

	ch = getchanbyname(sm->sm_par[2]);
#ifdef	DEBUGV
	if (ch == NULL)
		iw_printf(COLI_TEXT, "DEBUG: cmd_names(): getchanbyname() \
returns NULL for %s\n", sm->sm_par[2]);
#endif
	sprintf(t, "%sOn channel %s: %s\n", PPREORNUM(sm), sm->sm_par[2],
		sm->sm_par[3]);

	if (ch != NULL) {
		wm.iwm_type = IMT_CMSG | IMT_FMT;
		wm.iwm_chn = ch;
		cache_names(sm, ch);

		/* If we freshly joined, we look if we got chanop. */
		if (!(ch->ch_modes & MD_O))
			if ((cu = getfromucache(nick, ch, NULL, 0)) != NULL)
				if (cu->cu_mode & MD_O) {
					ch->ch_modes |= MD_O;
					iw_draw();
				}
	}
	else {
		wm.iwm_type = IMT_CTRL | IMT_FMT;
		wm.iwm_chn = NULL;
	}
	wm.iwm_text = t;
	wm.iwm_chint = COLI_SERVMSG;
	dispose_msg(&wm);
}

/*
 * Send a PRIVMSG.  Either to a channel or a user if specified or 
 * to the top channel in the current window.  Discard if not on_irc.
 * Empty messages are ignored.
 */
void
privmsg(target, txt, silent)
	char *target, *txt;
	int silent;
{
	static char t[MSGSZ];
	struct channel *ch;
	struct iw_msg wm;

	if (!strlen(txt))
		return;
	if (!on_irc) {
		iw_printf(COLI_TEXT, "%sNot connected to a server\n", ppre);
		return;
	}

	if (target != NULL && !ISCHANNEL(target)) {
		if (*target != '=') {
		    /* PRIVMSG to a user */
		    dprintf(sock, "PRIVMSG %s :%s\r\n", target, txt);

		    add_nickhist(target);

		    if (silent)
			return;
		    sprintf(t, "-> *%s* %s\n", target, txt);
		    wm.iwm_text = t;
		    wm.iwm_type = IMT_PMSG | IMT_FMT;
		    wm.iwm_chn = NULL;
		    wm.iwm_chint = COLI_OWNMSG;
		    dispose_msg(&wm);

		    if (msglog != NULL)
			fprintf(msglog, "%s -> *%s* %s\n", timestamp(), target,
				txt);
		}
		else {
		    /* DCC CHAT message */
		dcc_msg:

		    add_nickhist(target);
		    
		    if (dcc_chatmsg(target, txt) == 0 || silent)
		    	return;

		    sprintf(t, "-> %s= %s\n", target, txt);
		    wm.iwm_text = t;
		    wm.iwm_type = IMT_PMSG | IMT_FMT;
		    wm.iwm_chn = NULL;
		    wm.iwm_chint = COLI_OWNMSG;
		    dispose_msg(&wm);
		}
	}
	else {
		/* Channel or Query message */

		if (target && strlen(target))
			ch = getchanbyname(target);
		else
			if ((ch = iw_getchan()) == NULL) {
			    iw_printf(COLI_TEXT, "%sNo channel/query in this \
window\n", ppre);
			    return;
			}

		if (ch == NULL) {
			dprintf(sock, "PRIVMSG %s :%s\r\n", target, txt);
			if (silent)
				return;
			sprintf(t, "-> *%s* %s\n", target, txt);
			wm.iwm_text = t;
			wm.iwm_type = IMT_PMSG | IMT_FMT;
			wm.iwm_chn = NULL;
		    	wm.iwm_chint = COLI_OWNMSG;
			dispose_msg(&wm);

			if (msglog != NULL)
				fprintf(msglog, "%s -> *%s* %s\n", timestamp(),
					target, txt);
		}
		else {
			if (*ch->ch_name == '!' && *(ch->ch_name+1) == '=') {
				target = ch->ch_name+1;
				goto dcc_msg;
			}
			dprintf(sock, "PRIVMSG %s :%s\r\n",
				(*ch->ch_name == '!') ? ch->ch_name+1 :
				ch->ch_name, txt);
			if (silent)
				return;
			if (ch->ch_modes & MD_QUERY) {
				sprintf(t, "-> *%s* %s\n", ch->ch_name+1, txt);
				if (msglog != NULL)
				    fprintf(msglog, "%s -> *%s* %s\n",
					timestamp(), ch->ch_name+1, txt);
			}
			else
				sprintf(t, "%s> %s\n", ch->ch_name, txt);
			wm.iwm_text = t;
			wm.iwm_type = IMT_CMSG | IMT_FMT;
			wm.iwm_chn = ch;
			wm.iwm_chint = COLI_CHANMSG;
			dispose_msg(&wm);
		}
	}
}

/*
 * Send a NOTICE to a target.  Works similar to privmsg().
 */
void
notice(target, txt, silent)
	char *target, *txt;
	int silent;
{
	static char t[MSGSZ];
	struct iw_msg wm;

	if (!strlen(txt))
		return;
	if (!on_irc) {
		iw_printf(COLI_TEXT, "%sNot connected to a server\n", ppre);
		return;
	}

	if (target != NULL) {
		skipspace(target);
		dprintf(sock, "NOTICE %s :%s\r\n", target, txt);

		if (silent)
			return;
		if (!ISCHANNEL(target))
			sprintf(t, "-> -%s- %s\n", target, txt);
		else
			sprintf(t, "-%s-> %s\n", target, txt);
		wm.iwm_text = t;
		wm.iwm_type = IMT_PMSG | IMT_FMT;
		wm.iwm_chn = NULL;
		wm.iwm_chint = COLI_OWNMSG;
		dispose_msg(&wm);

		if (msglog != NULL)
			fprintf(msglog, "%s -> -%s- %s\n", timestamp(), target,
				txt);
	}
}

/*
 * Handles PRIVMSG that contain a CTCP string (^ACOMMAND PARAMS^A).
 * Response is made with a NOTICE.
 * CTCP is the ``Client-To-Client-Protocol''.  It is not part of
 * RfC1459 but understood by many clients.  We support a subset.
 */
static void
ctcpmsg(sm)
	struct servmsg *sm;
{
	char nname[MSGSZ], *t, *def = "";
	char u[MSGSZ];
	struct iw_msg wm;
	struct channel *ch;
	struct timeval tv;

	if (!check_conf(CONF_CTCP))
		return;

	from_nick(sm, nname);

	/*
	 * ^AACTION^A indicates that the user does something.  It is
	 * displayed as "* luser:channel does some action" on the screen.
	 */
	if (!strncmp(sm->sm_par[1], "\1ACTION", 7)) {
		if ((t = strchr(sm->sm_par[1]+1, '\1')) != NULL)
			*t = 0;
		/* Is recipient a channel window? */
		if (ISCHANNEL(sm->sm_par[0])) {
			if ((ch = getchanbyname(sm->sm_par[0])) == NULL)
				return;
			t = strchr(sm->sm_par[1], ' ');
			t = (t != NULL) ? skipspace(t) : def;

			if (wchanistop(ch->ch_name))
			    sprintf(u, "* %s %s\n", nname, t);
			else
			    sprintf(u, "* %s:%s %s\n", nname, ch->ch_name, t);
			wm.iwm_text = u;
			wm.iwm_type = IMT_CMSG | IMT_FMT;
			wm.iwm_chn = ch;
		    	wm.iwm_chint = COLI_ACTION;
			dispose_msg(&wm);
			return;
		}
		/* Private action */
		else {
			t = strchr(sm->sm_par[1], ' ');
			t = (t != NULL) ? skipspace(t) : def;
			iw_printf(COLI_ACTION, "-*- %s %s\n", nname, t);
			return;
		}
	}

	/*
	 * If last CTCP message wasn't at least 3 seconds ago, we
	 * ignore the message and don't reply.
	 */
	gettimeofday(&tv, NULL);
	if (tv.tv_sec - last_ctcpmsg.tv_sec < 3) {
		last_ctcpmsg.tv_sec = tv.tv_sec;
		return;
	}
	last_ctcpmsg.tv_sec = tv.tv_sec;

	/* 
	 * A ^ACLIENTINFO^A requests a list of the CTCP commands we
	 * understand.
	 */
	if (!strcmp(sm->sm_par[1], "\1CLIENTINFO\1")) {
		notice(nname, "\1CLIENTINFO VERSION PING CLIENTINFO TIME \
ERRMSG DCC OP\1", 1);
		iw_printf(COLI_TEXT, "%s%s requested clientinfo %s\n", ppre,
			nname, TIMESTAMP);
		return;
	}

	/* On a ^AVERSION^A, we send the client and OS version info. */
	if (!strcmp(sm->sm_par[1], "\1VERSION\1")) {
		sprintf(u, "\1VERSION %s %s\1", version, osstring);
		notice(nname, u, 1);
		iw_printf(COLI_TEXT, "%s%s requested version %s\n", ppre,
			nname, TIMESTAMP);
		return;
	}

	/* 
	 * A ^APING *^A checks if we're still there.
	 * Answer with ping notice.
	 */
	if (!strncmp(sm->sm_par[1], "\1PING", 5)) {
		if (strlen(sm->sm_par[1]) > PINGSIZE)
			notice(nname, "\1PING :\1", 1);
		else
			notice(nname, sm->sm_par[1], 1);
		iw_printf(COLI_TEXT, "%s%s pinged you %s\n", ppre, nname,
			TIMESTAMP);
		return;
	}

	/* ^ATIME^A queries the local time. */
	if (!strncmp(sm->sm_par[1], "\1TIME", 5)) {
		sprintf(u, "\1TIME local time %s\1", timestamp());
		if ((t = strchr(u, '\n')) != NULL)
			*t = ' ';
		notice(nname, u, 1);
		iw_printf(COLI_TEXT, "%s%s queried time %s\n", ppre, nname,
			TIMESTAMP);
		return;
	}

	/* DCC offer. */
	if (!strncmp(sm->sm_par[1], "\1DCC SEND", 9) ||
	    !strncmp(sm->sm_par[1], "\1DCC CHAT", 9) ||
	    !strncmp(sm->sm_par[1], "\1DCC RESUME", 11) ||
	    !strncmp(sm->sm_par[1], "\1DCC ACCEPT", 11)) {
		dcc_register(sm);
		if ((t = strchr(sm->sm_par[1]+1, '\1')) != NULL)
			*t = 0;
		return;
	}

	/* ChanOp-On-Demand request */
	if (!strncmp(sm->sm_par[1], "\1OP ", 4)) {
		ood_incoming(sm, nname);
		return;
	}

	if ((t = strchr(sm->sm_par[1]+1, '\1')) != NULL)
		*t = 0;
	iw_printf(COLI_TEXT, "%sctcp privmsg from %s: %s %s\n", ppre, nname,
		sm->sm_par[1]+1, TIMESTAMP);
}

/*
 * Handles a NOTICE that contains a CTCP string (^ACOMMAND PARAMS^A).
 * No response is made.
 */
static void
ctcpnotice(sm)
	struct servmsg *sm;
{
	char nname[MSGSZ], *t, *t2;

	if (!check_conf(CONF_CTCP))
		return;

	from_nick(sm, nname);

	/* We got a reply to a CTCP VERSION request. Display it. */
	if (!strncmp(sm->sm_par[1], "\1VERSION", 8)) {
		if ((t = strchr(sm->sm_par[1]+8, ' ')) == NULL)
			iw_printf(COLI_TEXT, "%s%s version: n/a %s\n", ppre,
				nname, TIMESTAMP);
		else {
			if ((t2 = strchr(t, 1)) != NULL)
				*t2 = 0;
			iw_printf(COLI_TEXT, "%s%s version:%s %s\n", ppre,
				nname, t, TIMESTAMP);
		}
		return;
	}

	/* 
	 * Parse and display the answer to a PING message.
	 * We calculate the difference between current time and the timestamp
	 * delivered with our ping msg to get the elapsed time.
	 */
	if (!strncmp(sm->sm_par[1], "\1PING", 5)) {
		struct timeval tv;
		char *u;

		gettimeofday(&tv, NULL);
		u = strchr(sm->sm_par[1]+5, ' ');

		if (u == NULL)
			iw_printf(COLI_TEXT, "%sBroken ctcp ping reply from \
%s %s\n", ppre, nname, TIMESTAMP);
		else {
			iw_printf(COLI_TEXT, "%s%s ctcp ping reply (%d sec \
rtt) %s\n", ppre, nname, tv.tv_sec-atol(u), TIMESTAMP);
		}
		return;
	}

	/* Check for ctcp ERRMSG. */
	if (!strncmp(sm->sm_par[1], "\1ERRMSG", 7)) {
		if ((t = strchr(sm->sm_par[1]+7, ' ')) == NULL)
			iw_printf(COLI_TEXT, "%sUnspecified ctcp error from \
%s %s\n", ppre, nname, TIMESTAMP);
		else {
			if ((t2 = strchr(t, 1)) != NULL)
				*t2 = 0;
			iw_printf(COLI_TEXT, "%sctcp error from %s:%s %s\n",
				ppre, nname, t, TIMESTAMP);
		}
		return;
	}

	/* ctcp CLIENTINFO? */
	if (!strncmp(sm->sm_par[1], "\1CLIENTINFO", 10)) {
		if ((t = strchr(sm->sm_par[1]+10, ' ')) == NULL)
			iw_printf(COLI_TEXT, "%sctcp clientinfo from %s is \
broken %s\n", ppre, nname, TIMESTAMP);
		else {
			if ((t2 = strchr(t, 1)) != NULL)
				*t2 = 0;
			iw_printf(COLI_TEXT, "%sctcp clientinfo from %s:%s \
%s\n", ppre, nname, t, TIMESTAMP);
		}
		return;
	}

	if ((t = strchr(sm->sm_par[1]+1, '\1')) != NULL)
		*t = 0;
	iw_printf(COLI_TEXT, "%sctcp notice from %s: %s %s\n", ppre, nname,
		sm->sm_par[1]+1, TIMESTAMP);
}

/*
 * Parse generic numerical replies and error messages and display them.
 */
static void
cmd_generic(sm)
	struct servmsg *sm;
{
	struct iw_msg wm;
	struct channel *ch;
	static char *t;
	int i;

	if (t == NULL)
		t = chkmem(malloc(BUFSZ));

	if (sm->sm_num > 400) {
		/* ERR_BLAHBLAH error message. */
		sprintf(t, "%s%s: ", PPREORNUM(sm), sm->sm_prefix);

		for (i = 1; i < sm->sm_npar; i++) {
			strcat(t, sm->sm_par[i]);
			strcat(t, " ");
		}
		strcat(t, " ");
		strcat(t, TIMESTAMP);
		strcat(t, "\n");
		wm.iwm_text = t;
		wm.iwm_type = IMT_CTRL | IMT_FMT;
		wm.iwm_chn = NULL;
		wm.iwm_chint = COLI_SERVMSG;
		dispose_msg(&wm);
	}
	else {
		int want_prefix = 0;	/* if 1, prefix field is printed */

		/* 
		 * RPL_BLAHBLAH reply message.
		 * Not all replies are formatted the same way.  We use
		 * a default format to print all parameters and otherwise
		 * display only parts or with additional text.
		 */
		switch (sm->sm_num) {
		default:	/* This is used for one-param rpls and */
		genrepl:	/* those which need no extra formatting */
			if (want_prefix)
				sprintf(t, "%s%s: ", PPREORNUM(sm),
					sm->sm_prefix);
			else
				strcpy(t, PPREORNUM(sm));

			for (i = 1; i < sm->sm_npar; i++) {
				strcat(t, sm->sm_par[i]);
				strcat(t, " ");
			}

			strcat(t, "\n");
			wm.iwm_text = t;
			wm.iwm_type = IMT_CTRL | IMT_FMT;
			wm.iwm_chn = NULL;
		    	wm.iwm_chint = COLI_SERVMSG;
			dispose_msg(&wm);
			break;
		case RPL_AWAY:
			if (check_ignore(sm))
				return;
			iw_printf(COLI_SERVMSG, "%s%s is away (%s)\n",
				PPREORNUM(sm), sm->sm_par[1], sm->sm_par[2]);
			break;
		case RPL_WHOISUSER:
			iw_printf(COLI_SERVMSG, "%s%s is %s@%s (%s)\n",
				PPREORNUM(sm),
				sm->sm_par[1],
				sm->sm_par[2], sm->sm_par[3], sm->sm_par[5]);
			sprintf(t, "%s!%s@%s", sm->sm_par[1], sm->sm_par[2],
				sm->sm_par[3]);
			addsrctocache(sm->sm_par[1], t);

			if (our_address == NULL &&
			    !irc_strcmp(sm->sm_par[1], nick)) {
				/* Get our source address. */
				sprintf(t, "%s@%s", sm->sm_par[2],
					sm->sm_par[3]);
				our_address = chkmem(Strdup(t));
#ifdef	DEBUGV
				iw_printf(COLI_TEXT, "DEBUG: our_address = \
\"%s\"\n", our_address);
#endif
			}
			break;
		case RPL_WHOISSERVER:
			iw_printf(COLI_SERVMSG, "%sVia server %s (%s)\n",
				PPREORNUM(sm),
				sm->sm_par[2], sm->sm_par[3]);
			break;
		case RPL_WHOISIDLE:
			{
			    int sec = atoi(sm->sm_par[2]);

			    iw_printf(COLI_SERVMSG,
					"%sIdle for %d min, %d sec\n",
					PPREORNUM(sm), sec / 60, sec % 60);
			}
			break;
		case RPL_WHOISCHANNELS:
			iw_printf(COLI_SERVMSG, "%sOn channels %s\n",
				PPREORNUM(sm), sm->sm_par[2]);
			break;
		case RPL_WHOWASUSER:
			iw_printf(COLI_SERVMSG, "%s%s was %s@%s (%s)\n",
				PPREORNUM(sm), sm->sm_par[1],
				sm->sm_par[2], sm->sm_par[3], sm->sm_par[5]);
			break;
		case RPL_LIST:
			iw_printf(COLI_SERVMSG, "%s%7s %5s  %s\n", 
				PPREORNUM(sm),
				sm->sm_par[1], sm->sm_par[2], sm->sm_par[3]);
			break;
		case RPL_INVITING:
			if (check_ignore(sm))
				return;
			sprintf(t, "%sInviting %s to channel %s\n",
				PPREORNUM(sm), sm->sm_par[1], sm->sm_par[2]);
			wm.iwm_text = t;
			wm.iwm_type = IMT_CMSG | IMT_FMT;
			wm.iwm_chn = getchanbyname(sm->sm_par[2]);
		    	wm.iwm_chint = COLI_SERVMSG;
			dispose_msg(&wm);
			break;
		case RPL_WHOREPLY:
			sprintf(t, "%s %9s %4s %-22s %s@%s\n", sm->sm_par[1],
				sm->sm_par[5], sm->sm_par[6], sm->sm_par[7],
				sm->sm_par[2], sm->sm_par[3]); 
			
			if ((ch = getchanbyname(sm->sm_par[2])) != NULL) {
				wm.iwm_type = IMT_CMSG | IMT_FMT;
				wm.iwm_chn = ch;
			}
			else {
				wm.iwm_type = IMT_CTRL | IMT_FMT;
				wm.iwm_chn = NULL;
			}
			wm.iwm_text = t;
		    	wm.iwm_chint = COLI_SERVMSG;
			dispose_msg(&wm);

			sprintf(t, "%s!%s@%s", sm->sm_par[5], sm->sm_par[2],
				sm->sm_par[3]);
			addsrctocache(sm->sm_par[5], t);
			break;
		case RPL_YOUREOPER:
			iw_printf(COLI_SERVMSG,
				"%sWelcome to the Twilight Zone\n", 
				PPREORNUM(sm));
			umd |= UM_O;
			iw_draw();
			break;
		case RPL_ISON:
			iw_printf(COLI_SERVMSG, "%sOn IRC (ISON): %s\n",
				PPREORNUM(sm),
				strlen(sm->sm_par[1]) ? sm->sm_par[1] : 
					"<no match>");
			break;
		case RPL_UMODEIS:
			iw_printf(COLI_SERVMSG, "%sUser-mode for %s is %s\n",
				PPREORNUM(sm), sm->sm_par[0], sm->sm_par[1]);
			break;
		case RPL_USERHOST:
			parse_uhost(sm);
			redo(REDO_USERHOST);
			break;
		case RPL_TOPIC:
			iw_printf(COLI_SERVMSG, "%sTopic for %s is %s\n", 
				PPREORNUM(sm), sm->sm_par[1], sm->sm_par[2]);
			break;
		case RPL_NOWAWAY:
			is_away = 1;
			set_conf(CONF_BEEP, 1);
			iw_draw();
			goto genrepl;
		case RPL_UNAWAY:
			is_away = 0;
			iw_draw();
			goto genrepl;
		case RPL_STATSLINKINFO:
		case RPL_STATSCOMMANDS:
		case RPL_STATSCLINE:
		case RPL_STATSNLINE:
		case RPL_STATSILINE:
		case RPL_STATSKLINE:
		case RPL_STATSYLINE:
		case RPL_STATSLLINE:
		case RPL_STATSUPTIME:
		case RPL_STATSOLINE:
		case RPL_STATSHLINE:
		case RPL_STATSQLINE:
			want_prefix++;
			goto genrepl;
		}
	}
}

/*
 * Produce a temporary timestamp string.
 */
char *
timestamp()
{
	static char stamp[41];
	time_t c = time(NULL);
	char *t;

	sprintf(stamp, "[%s", ctime(&c));
	if ((t = strchr(stamp, '\n')) != NULL)
		*t = ']';
	return stamp;
}

/*
 * 2.9 nick/channel-delay.  If it's a nickname, allow the user to change
 * it and try again.
 */
static void
cmd_unavailable(sm)
	struct servmsg *sm;
{
	static char nuprompt[MSGSZ];

	cmd_print(sm);

	if (ISCHANNEL(sm->sm_par[1]))
		return;

	sprintf(nuprompt, "Enter nickname: ");
	set_prompt(nuprompt);
	linedone(0);
	othercmd = newnick;
	f_elhome();
}

/*
 * Returns 1 if the server line-in-buffer contains at least one active
 * line.
 */
int
in_buffered()
{
	return (ibhead.tqh_first != NULL);
}

/*
 * Gets a the next line from the line-in-buffer.
 * Returns -1 if no lines in buffer, length of line otherwise.
 */
int
get_buffered(line)
	char *line;
{
	struct inbuf_line *ib = ibhead.tqh_first;
	int len;

	if (ib == NULL)
		return -1;

	strncpy(line, ib->il_line, BUFSZ-1);
	line[BUFSZ-1] = 0;
	len = ib->il_size;
	TAILQ_REMOVE(&ibhead, ib, il_entries);
	free(ib->il_line);
	free(ib);
	return len;
}

/*
 * Suck in pending text from the server connection and check for a server
 * ping.  Server pings are answered immediately.  Any other lines are
 * stored unchanged in the in-line buffer.
 */
void
run_ilbuf()
{
	char line[BUFSZ];
	struct inbuf_line *ib;

	if (!use_ilbuf || !sock)
		return;

	while (dgets(sock, BUFSZ, line, 0) > 0)
		if (!strncmp(line, "PING ", 5)) {
			dprintf(sock, "PONG :*\r\n");
#ifdef	DEBUGV
			iw_printf(COLI_TEXT, "DEBUG: run_ilbuf(): ping - \
pong\n");
#endif
		}
		else
			add_to_ilbuf(line);
}

/* Insert sth into the inline buffer. */
void
add_to_ilbuf(l)
	char *l;
{
	struct inbuf_line *ib;

	if (l == NULL || strlen(l) == 0)
		return;

	ib = chkmem(malloc(sizeof *ib));
	ib->il_size = strlen(l);
	ib->il_line = chkmem(malloc(ib->il_size+1));
	strcpy(ib->il_line, l);
	TAILQ_INSERT_TAIL(&ibhead, ib, il_entries);
}

void
use_ilb(use)
	int use;
{
	use_ilbuf = use;
}

/*
 * Return the channel structure doing query for nickname or NULL if it
 * doesn't exist.
 */
struct channel *
getquerybyname(nname)
	char *nname;
{
	struct channel *ch = cha;
	char chn[CNAMESZ];
	unsigned long h;

	chn[0] = '!';
	strcpy(chn+1, nname);
	h = elf_hash(irc_strupr(chn));

	while (ch != NULL) {
		if (ch->ch_modes & MD_QUERY && h == ch->ch_hash)
			return ch;
		ch = ch->ch_next;
	}
	return NULL;
}

/*
 * Add a name to the message nickname history.
 */
void
add_nickhist(nname)
	char *nname;
{
	strncpy(lastmsg[lastmsgp], nname, NICKSZ+1);
	lastmsg[lastmsgp][NICKSZ+1] = 0;
#ifdef	DEBUGV
	iw_printf(COLI_TEXT, "DEBUG: lastmsgp = %d\n", lastmsgp);
#endif
	lastmsgp = (lastmsgp + 1) % MSGNAMEHIST;
}

/*
 * Destroy all channels.
 */
void
delallchan()
{
	struct channel *ch, *c;

	for (ch = cha; ch != NULL; ch = c) {
		iw_delchan(ch);
		c = ch->ch_next;
		delchan(ch);
	}
	cha = NULL;
}

static void
check_sigwinch()
{
	extern sig_atomic_t atomic_resize_screen;

	if (atomic_resize_screen == 1) {
		atomic_resize_screen = 0;
		tty_getdim();
		if (t_columns < INDENT+20) {
			dobell();
			fprintf(stderr, "%stirc: screen width too small\
, please resize%s\n", tc_bold, tc_noattr);
			return;
		}
		screen_adjust();
		equalwins();
		repinsel();
	}
}

/*
 * Strip all mIRC-style colour codes (^Cnumber[,number]) from the line.
 */
static void
strip_mirc_colours(lb)
	register unsigned char *lb;
{
	register unsigned char *lbp = lb;

	while (*lbp != '\0')
		if (*lbp == '') {
			/*
			 * Fast, not necessarily completely correct hack, but
			 * should have the desired effect most of the time.
			 */
			lbp++;
			while ((isdigit(*lbp) || *lbp == ',') && *lbp != '\0')
				lbp++;
		} else
			*lb++ = *lbp++;
	*lb = '\0';
}
