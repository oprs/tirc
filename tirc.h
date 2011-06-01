/*-
 * tirc -- client for the Internet Relay Chat
 *
 *	Copyright (c) 1996, 1997, 1998
 *		Matthias Buelow.  All rights reserved.
 *
 *	See the file ``COPYRIGHT'' for the usage and distribution
 *	license and warranty disclaimer.
 *
 * $Id: tirc.h,v 1.63 1998/02/22 19:04:49 mkb Exp $
 */

#ifndef TIRC_TIRC_H
#define TIRC_TIRC_H	1

#include "config.h"

#ifdef	HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef	HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef	HAVE_UNISTD_H
#include <unistd.h>
#endif
#if (defined(HAVE_TIME_H) && defined(TIME_WITH_SYS_TIME)) || \
    !defined(HAVE_SYS_TIME_H)
#include <time.h>
#endif
#ifdef	HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef	HAVE_SYS_ERRNO_H
#include <sys/errno.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef	HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef	HAVE_STDIO_H
#include <stdio.h>
#endif
#ifdef	HAVE_ASSERT_H
#include <assert.h>
#endif
#ifdef	HAVE_STDDEF_H
#include <stddef.h>
#endif

#include <sys/queue.h>

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#ifndef errno
extern int errno;
#endif

/*
 * Size constants etc.
 * Buffer sizes should be slightly less than a power of two, to prevent
 * slipping in the next-large bucket list and thus wasting memory on
 * traditional malloc implementations.
 */
#define	DGMAXDESC	256	/* highest possible descriptor for dgets()+1 */
#define	NICKSZ		33	/* size of nickname string \ NUL */
#define CNAMESZ		201	/* size of a channel name + NUL */
#define MSGSZ		513	/* size of an IRC message + NUL */
#define BUFSZ		4000	/* general buffer size */
#define MAXSCMD		999	/* highest command number in IRC protocol */
#define MAXINPUT	510	/* length of editor line */
#define HISTORY		100	/* number of lines in input history */
#define BACKSCROLL	1200	/* number of lines in window backscroll */
#define FLRINGSIZE	18	/* cached messages in flood detection ring */
#define	PINGSIZE	64	/* limit size of a ctcp PING notice */
#define	DCCBUFSZ	8000	/* size of a dcc file packet */
#define IDLELIMIT	168*3600 /* user input idle seconds before exiting */
#define INDENT		4	/* indention of continued messages */
#define MSGNAMEHIST	4	/* size of the message nickname history */
#undef	SUPPRESS_ENDOF	/*1*/	/* if defined, suppress end of blahblah msgs */
#define	MAXURLLINESIZE	1000	/* Maximum line size that is read from the */
				/* URL catcher file */
#define	TIRC_PATHMAX	255	/* Maximum size of pathname inside tirc, */
				/* including terminating NUL */
#define	DCCAGE		3*60-10	/* number of seconds until unaccepted */
				/* incoming dcc file/chat requests will be */
				/* deleted (this should be slightly lower */
				/* than flood auto-ignore expiration time */
#define	CF_SERV		1	/* for command parsing: command only valid */
				/* if connected to server */

/* Yeah, I know I should switch to mkstemp() eventually. */
#define	LLOGNAME	"/tmp/tircll"	/* pid gets appended */

enum { NO, YES };

#ifndef	__P
#ifdef	__STDC__
#define	__P(x)	x
#else
#define	__P(x)	()
#endif
#endif

#ifdef	HAVE_VOIDPTR
#define	VOIDPTR	void *
#else
#define	VOIDPTR	char *
#endif

/*
 * RC execution selector.
 */
#define RC_NOT_CONNECTED	0
#define	RC_IS_CONNECTED		1

/*
 * Find forward or backward.
 */
enum { FORWARD, BACKWARD };

#undef min
#define	min(a, b) ((a) > (b) ? (b) : (a))
#undef max
#define max(a, b) ((a) > (b) ? (a) : (b))

/*
 * Representation of a channel.
 */
struct channel {
	struct channel	*ch_next;
	struct channel	*ch_prev;
	struct channel	*ch_wnext;
	struct channel	*ch_wprev;
	char		ch_name[201];
	unsigned long	ch_hash;
	int		ch_lim;
	unsigned	ch_modes;
	FILE		*ch_logfile;
	char		*ch_logfname;
	LIST_HEAD(, cache_user) ch_cachehd;
};

/* 
 * User cache entry (per channel).
 */
struct cache_user {
	unsigned long cu_hash;
	char	cu_nick[NICKSZ+1];
	int	cu_mode;
	char	*cu_source;
	LIST_ENTRY(cache_user) cu_entries;
};

#define IMT_CMSG	1	/* channel traffic */
#define IMT_PMSG	2	/* private msg */
#define IMT_CTRL	4	/* server+client control msg */
#define IMT_DEBUG	8	/* debug traffic dump */
#define IMT_FMT		16	/* message should be formatted + indented */

/*
 * An irc-win message.
 */
struct iw_msg {
	unsigned	iwm_type;
	struct channel	*iwm_chn;
	char		*iwm_text;
	int		iwm_chint;
};

/*
 * Channel modes.
 */
#define MD_M		1	/* +m (moderated) */
#define MD_S		2	/* +s (secret) */
#define MD_P		4	/* +p (private) */
#define MD_L		8	/* +l (limited) */
#define MD_T		16	/* +t (topiclimited) */
#define MD_K		32	/* +k (require key) */
#define MD_A		64	/* +a (anonymous) */
#define MD_O		128	/* +o (chanop) */
#define MD_I		256	/* +i (inviteonly) */
#define MD_V		512	/* +v (voice) */
#define MD_N		1024	/* +n (msglimited) */
#define	MD_Q		2048	/* +q (? used in local server msg channels) */
#define MD_IUPR		4096	/* +I (2.10) */
#define MD_E		8192	/* +e (2.10) */
#define	MD_QUERY	65536	/* This is a faked channel, private (privmsg)
				 * conversation with a user (query) */

/*
 * User modes.
 */
#define UM_O		1	/* +o (ircop) */
#define UM_I		2	/* +i (invisible) */
#define UM_W		4	/* +w (wallops) */
#define UM_S		8	/* +s (server notices) */
#define UM_R		16	/* +r (restricted) */

/*
 * Server message dissected.
 */
struct servmsg {
	char	*sm_orig;
	char	*sm_prefix;
	int	sm_num;
	char	sm_anum[8];
	char	*sm_cmd;
	int	sm_npar;
	char	*sm_par[15];
};

/*
 * A line of text.
 */
struct aline {
	struct aline	*al_prev;
	struct aline	*al_next;
	char		*al_text;
}; 

/*
 * Window status.
 */
struct iwstatus {
	int	iws_start;
	int	iws_end;
	int	iws_nlines;
	char	*iws_log;
	struct channel *iws_ch;
};

/*
 * Periodic queue entry.
 */
struct p_queue {
	void	(*pq_fun) __P((void));
	LIST_ENTRY(p_queue) pq_entries;
};

/*
 * Configuration flags.
 */
#define	CONF_CTCP	1	/* whether CTCP is enabled */
#define CONF_STAMP	1<<1	/* if we should timestamp always */
#define	CONF_FLOODP	1<<2	/* if flood protection is enabled */
#define CONF_FLSTRICT	1<<3	/* if flood protection is strict */
#define CONF_FLIGNORE	1<<4	/* if we automatically ignore on flooding */
#define CONF_BEEP	1<<5	/* if lusers may beep you or not */
#define CONF_CLOCK	1<<6	/* if we display the current time */
#define CONF_KBIGNORE	1<<7	/* if we ignore the lamer after kickbanning */
#define CONF_HIMSG	1<<8	/* hilite private message and notice nicks */
#define CONF_BIFF	1<<9	/* whether we should check for new mail */
#define CONF_XTERMT	1<<10	/* if we should reflect status in xterm title */
#define CONF_PGUPDATE	1<<11	/* if we should show page update */
#define CONF_OOD	1<<12	/* enable chanop-on-demand */
#define CONF_ATTRIBS	1<<13	/* if ircII character attributes are allowed */
#define CONF_COLOUR	1<<14	/* if tty text colouring is wanted */
#define CONF_STIPPLE	1<<15	/* if status line should be drawn with "---" */
#define CONF_HIDEMCOL	1<<16	/* if we should hide mIRC ^C attribs entirely */
#define	CONF_NCOLOUR	1<<17	/* if nickname colourization should be done */
#define	CONF_NUMERICS	1<<18	/* display numerics instead of '***' */
#define CONF_TELEVIDEO	1<<19	/* disable reverse attrib on broken ttys */

/*
 * Flooding types.
 */
#define	FL_PUBLIC	1
#define	FL_MSG		2
#define	FL_NOTICE	4
#define	FL_NICK		8
#define	FL_INVITE	16

/*
 * Types of redo commands.
 */
enum redotype { REDO_USERHOST };

/*
 * Editor line modes.
 */
#define MODE_COMMAND	0
#define MODE_INSERT	1
#define MODE_OSTRIKE	2

/*
 * Global prototypes.
 */
void	initirc __P((void));
void	prepareirc __P((void));
int	our_signal __P((int, void (*) __P((int))));
int	dg_allocbuffer __P((int));
int	dg_freebuffer __P((int));
int	dgets __P((int, int, char *, int));
int	doirc __P((void));
int	getmax __P((int [], int));
int	dprintf __P((int, const char *, ...));
int	screeninit __P((void));
void	screenend __P((void));
void	parsekey __P((void));
void	dispose_msg __P((struct iw_msg *im));
int	iw_printf __P((int, const char *fmt, ...));
void	iw_draw __P((void));
void	from_nick __P((struct servmsg *, char *));
void	quitirc __P((char *));
struct channel *getchanbyname __P((char *));
unsigned long elf_hash __P((const char *));
unsigned long irc_elf_hash __P((const char *));
char	*irc_strupr __P((char *));
int	irc_strcmp __P((char *, char *));
int	irc_strncmp __P((char *, char *, size_t));
int	irc_chrcmp __P((int, int));
void	iw_addchan __P((struct channel *));
void	iw_delchan __P((struct channel *));
void	reset_input __P((void));
void	move_eline __P((int));
void	update_eline __P((char *, int, int, int));
void	set_prompt __P((char *));
void	showmode __P((int));
int	askyn __P((char *));
void	cmdinit __P((void));
void	cmdline __P((char *));
VOIDPTR	chkmem __P((VOIDPTR));
void	privmsg __P((char *, char *, int));
void	clearwin __P((void));
void	elrefr __P((int));
void	elhome __P((void));
void	f_elhome __P((void));
struct channel *iw_getchan __P((void));
void	equalwin __P((void));
void	equalwins __P((void));
void	newircwin __P((void));
void	delircwin __P((void));
void	switchwin __P((int));
int	wchanistop __P((char *));
struct channel *cchan __P((void));
void	iw_scroll __P((int, int));
void	dobell __P((void));
char	*askpass __P((char *));
int	iwcmode __P((int));
void	screen_adjust __P((void));
void	repinsel __P((void));
void	rccommands __P((int));
void	iwc_status __P((struct iwstatus *));
void	linedone __P((int));
void	add_ignore __P((char *, int));
void	del_ignore __P((int));
void	exp_ignore __P((void));
void	list_ignore __P((void));
int	check_ignore __P((struct servmsg *));
void	setlog __P((int));
void	notice __P((char *, char *, int));
int	check_conf __P((int));
int	set_conf __P((int, int));
char	*timestamp __P((void));
void	iw_closelog __P((void));
void	iw_closelogs __P((void));
void	iw_logtofile __P((char *));
void	cache_names __P((struct servmsg *, struct channel *));
void	listcucache __P((struct channel *));
void	clrucache __P((struct channel *));
void	iw_printcache __P((void));
void	update_cucache __P((struct servmsg *));
struct cache_user *getfromucache __P((char *, struct channel *,\
	struct channel **, int));
void	changecacheumode __P((char *, struct channel *, int, int));
void	addsrctocache __P((char *, char *));
char	*buildban __P((char *, struct channel *, char **, int *));
void	flinit __P((void));
int	flregister __P((struct servmsg *, int, int));
void	iw_resize __P((int));
void	init_system __P((void));
void	irc_system __P((char *));
void	irc_kill __P((int));
struct p_queue *pq_add __P((void (*)__P((void))));
void	pq_del __P((struct p_queue *));
void	disp_clock __P((void));
void	buildspam __P((char *));
int	checkspam __P((char *));
void	dcc_init __P((void));
void	dcc_isend __P((char *, char *));
void	dcc_print __P((void));
void	parse_uhost __P((struct servmsg *));
void	open_llog __P((void));
void	ignmask __P((char *));
int	setredo __P((char *, enum redotype));
void	redo __P((enum redotype));
int	in_buffered __P((void));
int	get_buffered __P((char *));
void	run_ilbuf __P((void));
void	use_ilb __P((int));
void	add_to_ilbuf __P((char *));
void	search __P((int));
void	allow_empty __P((int));
void	findre __P((int));
void	el_setmode __P((int));
void	iloop_cursor_hook __P((void));
void	dcc_killch __P((void));
void	dcc_register __P((struct servmsg *));
void	checkmail __P((void));
void	check_mailbox __P((void));
void	dcc_close __P((char *, int));
void	dcc_iget __P((char *, int, int));
void	dcc_rename __P((int, char *));
void	dcc_wait_children __P((void));
void	check_sigint __P((void));
int	dcc_ischat __P((char *));
void	dcc_irchat __P((char *, int));
int	dcc_chatmsg __P((char *, char *));
void	dcc_update __P((struct servmsg *));
void	dcc_resume __P((char *, int));
int	dcc_fd_set __P((fd_set *));
void	ctldcc __P((fd_set *));
void	delchan __P((struct channel *));
struct channel *getquerybyname __P((char *));
void	add_nickhist __P((char *));
unsigned char	*skipws __P((unsigned char *s));
void	free_history __P((void));
void	logchannel __P((char *, char *));
void	ch_closelog __P((struct channel *));
void	delallchan __P((void));
char	*complete_from_ucache __P((struct channel *, char *, int));
void	system_printline __P((char *));
int	system_out_waiting __P((void)); 
void	system_sendpmsg __P((void));
void	system_dequeue __P((void));
void	ipg_create __P((void));
void	ipg_delete __P((void));
int	ipg_switchto __P((int, int));
void	ipg_pgline __P((void));
int	ipg_getpageno __P((void));
int	ipg_lastpageno __P((void));
int	ipg_setget_otherpg __P((int));
void	xterm_settitle __P((void));
int	isnickch __P((int));
struct channel *userchan_cpage __P((char *));
void	ipg_updatechk __P((void));
void	logmessage __P((char *));
void	closemlog __P((void));
void	ood_init __P((void));
void	ood_clean __P((void));
int	ood_add __P((char *));
int	ood_del __P((int));
int	ood_verify __P((char *, char *, char *));
void	ood_display __P((void));
void	ood_incoming __P((struct servmsg *, char *));
void	Shdestroy __P((void));
VOIDPTR	Shmalloc __P((size_t));
VOIDPTR	Shrealloc __P((VOIDPTR, size_t));
void	Shfree __P((VOIDPTR));
VOIDPTR	Shcalloc __P((size_t));
char	*exptilde __P((char *));
char	*globtore __P((char *));
void	urlstart __P((char *));
void	urlend __P((void));
void	urlcheck __P((char *, char *));
void	urlflush __P((void));
unsigned long get_irc_s_addr __P((void));
void	pastemode __P((void));
void	firstdebug __P((void));
int	screen_setup_p __P((void));
int	approx_match __P((const char *, const char *, int));
void	ncol_init __P((void));
int	ncol_match __P((const char *, int *, int *));
void	ncol_print __P((void));
void	ncol_register __P((char *, int, int, int));
void	ncol_remove __P((int));
const char *getcolourname __P((int));
int	colour_atoi __P((char *, char *, int *, int *));
void	sigwinch __P((int));
int	iw_buf_printf __P((int, const char *, ...));
void	iw_buf_flush __P((void));
void	en_disp __P((int));
void	options_dialog __P((void));
int	is_in_odlg __P((void));
void	odlg_parsekey __P((void));
void	odlg_printscreen __P((void));
void	setcmd __P((int, char *));
void runpqueues __P((void));

/*
 * These must only be used during a lint build.  It doesn't work in
 * executable code on machines that are not big endian.  This fixes
 * that lint bails out on gcc-stuff that BSD's htons() etc. use.
 */
#ifdef	lint
#undef	ntohl
#undef	ntohs
#undef	htonl
#undef	htons
#define	ntohl(x) (x)
#define ntohs(x) (x)
#define htonl(x) (x)
#define htons(x) (x)
#endif	/* lint */


/*
 * These are some missing #defines for linux/glibc systems. Yes, this
 * is a braindead fix, but it seems to work...
 */
#ifdef	BRAINDEAD_GLIBC
#define	SHMSEG		(1<<7)
#define	SHMMAX		0x1000000
#define	IPC_PRIVATE	((int) 0)
#define	SHM_R		0400
#define	SHM_W		0200
#define	IPC_RMID	0
#endif	/* BRAINDEAD_GLIBC */

#endif	/* !TIRC_TIRC_H */

