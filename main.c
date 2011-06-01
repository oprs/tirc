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
 * XXX  I tried to keep larger operations out of signal handlers, yet
 * some still call a larger function cascade with probably non-reentrant
 * stuff so in some very rare case, there could be problems.
 * This probably still needs some improvement.
 */

#ifndef lint
static char rcsid[] = "$Old: main.c,v 1.64 1998/02/22 19:04:49 mkb Exp $";
#endif

#include "tirc.h"
#include "v.h"

#ifdef	HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef	HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef	HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef	HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef	HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#ifdef	HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef	HAVE_STRING_H
#include <string.h>
#elif defined(HAVE_MEMORY_H)
#include <memory.h>
#endif
#ifdef	HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef	HAVE_PWD_H
#include <pwd.h>
#endif
#ifdef	HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef	HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef	HAVE_LOCALE_H
#include <locale.h>
#endif
#ifdef	HAVE_CTYPE_H
#include <ctype.h>
#endif

#include "compat.h"
#include "tty.h"
#include "ht.h"
#include "colour.h"
#include "dl.h"

static void	readrc __P((char *));
static int	linkup __P((void));
static void	splitas __P((char *, char *, char *));
static void	synterr __P((char *, int));
void		sig_exit __P((int));
void		sigint __P((int));
void		sigtstp __P((int));
void		sigchld __P((int));
void		sigalrm __P((int));

extern int	errno;
extern int	on_irc;
extern int	restart_irc;
extern char	ppre[], version[];
extern int	systempid, syspipe, closesyspipe;

char		*myname;
char		nick[NICKSZ+1], *pass;
char		*loginname, *realname;
char		*srvnm, *hostname, *clname, *srvdn;
char		osstring[80];
char		optsstring[120];
int		port;
int		sock;
struct aline	*rcca, *rcce, *ircca, *ircce;
int		sigint_destroys = 1;
char		**argv0;
sig_atomic_t	atomic_resize_screen;
time_t		t_uptime, t_connecttime;

static LIST_HEAD(, p_queue) pqh;
static int	global_options;
static char	usage[] =
"\nusage: tirc [-n nickname] [-s servername] [-p port] [-x password] [-h hostname] [-d] [-v]\
\n";
static time_t	lastmbmod;
static long	irc_s_addr;

static sig_atomic_t atomic_check4newmail;
static sig_atomic_t atomic_int_flag;
sig_atomic_t atomic_periodic;
static int startupdebug;

/* If the respective command line option was specified, the flag is set. */
static struct {
	unsigned int nick:1;
	unsigned int srvnm:1;
	unsigned int hostname:1;
	unsigned int port:1;
	unsigned int pass:1;
} cmdl_args;

int
main(argc, argv)
	int argc;
	char **argv;
{
	extern char *optarg;
	char *t;
	struct passwd *pwe;
	int c, ec, sig;
	char *newname;
#ifndef	HOSTTYPE
	char sysline[512];
	FILE *tf;
#endif
	int trylinkup = 1;
	struct itimerval it;
	char *home, *rcfn;

	myname = chkmem(Strdup(argv[0]));
	argv0 = argv;
	newname = chkmem(malloc(strlen(VERSTRING) + strlen(": ")
			+ strlen(myname) + 1));
	sprintf(newname, "%s: %s", VERSTRING, myname);
	argv[0] = newname;

	atomic_int_flag = atomic_periodic = 0;
	t_uptime = time(NULL);

#ifdef	HAVE_SETLOCALE
	setlocale(LC_ALL, "");
#endif

	/*
	 * Get Unix name and release.
	 * This has been deprecated by the autoconf system detection;
	 * only use it if autoconf was not used, that is if HOSTTYPE is
	 * not defined.
	 */
#ifndef	HOSTTYPE
	{
	char *osqtmp = chkmem(malloc(128));

	sprintf(osqtmp, "/tmp/tircosq.%d", (int)getpid()+(int)clock());

	sprintf(sysline, "echo `uname -sr` > %s", osqtmp);
	ec = system(sysline);

	if (ec < 0 || ec == 127) {
		fprintf(stderr, "%s: unable to get OS type and release.\n",
			myname);
		strcpy(osstring, "*IX");
	}
	else {
		/* XXX */
		if ((tf = fopen(osqtmp, "r")) == NULL) {
			fprintf(stderr, "%s: unable to open temp file, no OS \
info available.\n", myname);
			strcpy(osstring, "*IX");
		}
		else {
			fgets(osstring, 63, tf);
			fclose(tf);
			if ((t = strchr(osstring, '\n')) != NULL)
				*t = 0;
		}
		unlink(osqtmp);
	}
	free(osqtmp);
	}
#else
	strncpy(osstring, HOSTTYPE, 64);
	osstring[63] = '\0';
#endif

	sprintf(optsstring, "options");
#ifdef	WITH_DLMOD
	strcat(optsstring, " dlmod");
#endif
#ifdef	WITH_ANSI_COLOURS
	strcat(optsstring, " ansi-colours");
#endif

	/*
	 * Get luser options from the command line.  These override
	 * existing specifications in .tircrc.  If there's no .tircrc
	 * in the luser's homedir, we need some info specified on the
	 * command line.
	 */
	if (argc > 1) {
		while ((c = getopt(argc, argv, "n:h:s:p:x:dv")) != EOF) 
			switch(c) {
			case 'v':	/* print version and exit */
				printf("%s (%s) %s\n", version, osstring,
					optsstring);
				exit(0);
				/*NOTREACHED*/
			case 'n':	/* nickname specified */
				strncpy(nick, optarg, NICKSZ);
				nick[NICKSZ] = 0;
				cmdl_args.nick = 1;
				break;
			case 'h':	/* hostname specified */
				hostname = chkmem(Strdup(optarg));
				cmdl_args.hostname = 1;
				break;
			case 's':	/* server specified */
				srvnm = chkmem(Strdup(optarg));
				srvdn = chkmem(Strdup(srvnm));
				cmdl_args.srvnm = 1;
				break;
			case 'p':	/* port specified */
				port = atoi(optarg);
				cmdl_args.port = 1;
				break;
			case 'x':	/* password specified */
				pass = chkmem(Strdup(optarg));
				cmdl_args.pass = 1;
				break;
			case 'd':	/* debug dump in first window */
				startupdebug = 1;
				break;
			default:
				fprintf(stderr, "%s", usage);
				return 1;
			}
	}

	if ((pwe = getpwuid(getuid())) == NULL) {
		fprintf(stderr, "%s: cannot getpwuid()\n", myname);
		return 1;
	}
	if (!geteuid()) {
		fprintf(stderr, "%s: don't use IRC as superuser.\n", myname);
		return 1;
	}
	loginname = chkmem(Strdup(pwe->pw_name));
	if (!pwe->pw_gecos || !strlen(pwe->pw_gecos)) {
		fprintf(stderr, "%s: illegal GECOS field in password file.",
			myname);
		return 1;
	}
	realname = chkmem(Strdup(pwe->pw_gecos));
	if ((t = strchr(realname, ',')) != NULL)
		*t = 0;

	/* Print info. */
	fprintf(stderr, ">>> %s (%s) %s\n", version, osstring, optsstring);
#if	0
	fprintf(stderr, "Note: this is an alpha development snapshot. \
Be prepared to encounter bugs.\n");
#endif

#ifdef	DEBUGV
	fprintf(stderr, "DEBUG: visual debug output active.\n");
#endif
	/* Parse the startup file(s) */
	readrc("/etc/tirc/tircrc");

	if ((home = getenv("HOME")) == NULL)
		return 1;
	rcfn = chkmem(malloc(strlen(home)+strlen("/.tircrc")+1));
	sprintf(rcfn, "%s/.tircrc", home);
	readrc(rcfn);
	free(rcfn);

	if (srvnm == NULL || !strlen(srvnm)) {
		fprintf(stderr, "%s: server not specified.%s", myname, usage);
		return 1;
	}
	if (!port) {
		fprintf(stderr, "%s: port not specified.%s", myname, usage);
		return 1;
	}
	if (!strlen(nick))
		/* nickname not specified, use login name */
		strncpy(nick, loginname, NICKSZ);
		nick[NICKSZ] = 0;

	open_llog();

	/*
	 * Setup the screen layer.
	 */
	if (screeninit() < 0) {
		fprintf(stderr, "%s: screen init failed.\n", myname);
		return 1;
	}
	Atexit(screenend);

	/*
	 * Install signal handlers.
	 */
	sig = our_signal(SIGHUP, sig_exit);
	sig += our_signal(SIGINT, sigint);
	sig += our_signal(SIGPIPE, SIG_IGN);
	sig += our_signal(SIGTERM, sig_exit);
	sig += our_signal(SIGTSTP, sigtstp);
	sig += our_signal(SIGCHLD, sigchld);
	sig += our_signal(SIGALRM, sigalrm);

	if (sig) {
		iw_printf(COLI_WARN, "%sInstallation of signal handler(s) \
failed.\n", ppre);
		return 1;
	}

	/* Initialize periodic queue. */
	LIST_INIT(&pqh);
	it.it_interval.tv_sec = 60;
	it.it_interval.tv_usec = 0;
	it.it_value.tv_sec = 60;
	it.it_value.tv_usec = 0;
	setitimer(ITIMER_REAL, &it, NULL);

#ifdef	WITH_ANSI_COLOURS
	setup_colour();
#endif

	pq_add(exp_ignore);
	/* ctcp, flood protection and beeping are enabled by default. */
	set_conf(CONF_CTCP | CONF_FLOODP | CONF_BEEP, 1);
	init_system();
	dcc_init();
	ood_init();
#ifdef	WITH_DLMOD
	dlinit();
#endif
	reset_input();
	if (startupdebug)
		firstdebug();
	iw_draw();
	cmdinit();
	initirc();
#ifdef	WITH_ANSI_COLOURS
	ncol_init();
#endif

	rccommands(RC_NOT_CONNECTED);

	do {
		lastmbmod = time(NULL);
		atomic_check4newmail = 0;
		elrefr(1);
		flinit();

		if (trylinkup) {
		    sigint_destroys = 0;
		    iw_printf(COLI_TEXT, "%sTrying %s on %d/tcp, type ^%c to \
interrupt\n", ppre, srvdn, port, tck_intr+'@');
		    if (!(sock = linkup())) {
			iw_printf(COLI_WARN, "%sLinkup failed. Use /SERVER to \
establish a connection.\n", ppre);
			elhome();
			t_connecttime = 0;
		    }
		    else {
			iw_printf(COLI_TEXT, "%sConnected to ircd, registering \
client\n", ppre);
			t_connecttime = time(NULL);
		    }
		    sigint_destroys = 1;
		}
		else {
			iw_printf(COLI_TEXT, "%sUse /SERVER to establish a \
connection.\n", ppre);
			sock = 0;
			elhome();
			t_connecttime = 0;
		}

		prepareirc();
		ec = doirc();
		xterm_settitle();

		if (restart_irc > 1) {
			int a;
			/*
			 * Not restarted with /SERVER, ask if we want a
			 * new connection.
			 */
			a = askyn("Re-connect to server? ");
			elrefr(1);
			if (a == NO)
				trylinkup = 0;
		}
		else
			trylinkup = 1;
	} while (ec == 3);

	return ec;
}

/* 
 * Parse an existing .tircrc file in the user's home directory.
 */
static void
readrc(f)
	char *f;
{
	FILE *rc;
	char line[BUFSZ], left[BUFSZ], right[BUFSZ], *l, *m;
	char line2[BUFSZ];
	int rcl = 0, i;
	struct aline *al;
	int immediate;

	if ((rc = fopen(f, "r")) == NULL) {
		if (screen_setup_p())
			iw_printf(COLI_WARN, "%sCouldn't open %s\n", ppre, f);
		else
			fprintf(stderr, "Couldn't open %s\n", f);
		return;
	}

	if (screen_setup_p())
		iw_printf(COLI_TEXT, "%sSourcing %s\n", ppre, f);
	else
		fprintf(stderr, "Sourcing %s\n", f);

	while (!feof(rc)) {
		rcl++;

		if ((fgets(line, BUFSZ, rc)) == NULL)
			break;
		if ((l = strchr(line, '\n')) != NULL)
			*l = 0;

		l = skipws(line);

		/*
		 * Comments; everything after a '#' is treated as a comment
		 * and not evalutated.  Since '#' is commonly used in tirc
		 * commands, too, the user can escape it with a '\'.  The
		 * sequence '\#' will be interpreted as '#'.
		 */
		for (m = l, i = 0; *m; m++, i++)
			if (*m == '\\' && m[1] == '#') {
				line2[i] = '#';
				m++;
			}
			else
			if (*m == '#')
				break;
			else
				line2[i] = *m;
		line2[i] = 0;
		l = line2;

		/*
		 * Immediate commands select a different batch list.
		 */
		if (!strncmp(l, "immediate", 9)) {
			l += 9;
			if (!isspace(*l))
				synterr(f, rcl);
			l = skipws(l);
			immediate = 1;
		}
		else
			immediate = 0;

		/*
		 * Parse assignments and commands.
		 * If the first non-whitespace character in the line begins
		 * with '/', it is treated as a command and will be executed
		 * when we registered with the server.
		 * Otherwise, we will try to parse the line as an assignment
		 * of the form `lefttoken = righttext' where righttext is
		 * assigned to the variable lefttoken.
		 */
		if (*l == '/') {
			al = chkmem(malloc(sizeof *al));
			al->al_text = chkmem(Strdup(l));
			al->al_prev = al->al_next = NULL;

			if (!immediate) {
				if (rcca == NULL)
					rcca = rcce = al;
				else {
					al->al_prev = rcce;
					rcce->al_next = al;
					rcce = al;
				}
			}
			else {
				if (ircca == NULL)
					ircca = ircce = al;
				else {
					al->al_prev = ircce;
					ircce->al_next = al;
					ircce = al;
				}
			}
		}
		else {
			splitas(l, left, right);
			
			if (!strncmp("hostname", left, 8)) {
				if (cmdl_args.hostname)
					continue;
				if (!strlen(right))
					synterr(f, rcl);
				hostname = chkmem(Strdup(right));
				continue;
			}
			if (!strncmp("server", left, 6)) {
				if (cmdl_args.srvnm)
					continue;
				if (!strlen(right))
					synterr(f, rcl);
				srvnm = chkmem(Strdup(right));
				srvdn = chkmem(Strdup(srvnm));
				continue;
			}
			if (!strncmp("nick", left, 4)) {
				if (cmdl_args.nick)
					continue;
				if (!strlen(right))
					synterr(f, rcl);
				strncpy(nick, right, NICKSZ);
				nick[NICKSZ] = 0;
				continue;
			}
			if (!strncmp("port", left, 4)) {
				if (cmdl_args.port)
					continue;
				if (!strlen(right))
					synterr(f, rcl);
				port = atoi(right);
				continue;
			}
			if (!strncmp("pass", left, 4)) {
				if (cmdl_args.pass)
					continue;
				if (!strlen(right))
					synterr(f, rcl);
				pass = chkmem(Strdup(right));
				continue;
			}
			if (!strncmp("clname", left, 6)) {
				if (!strlen(right))
					synterr(f, rcl);
				clname = chkmem(Strdup(right));
				continue;
			}
			if (!strncmp("realname", left, 8)) {
				if (!strlen(right))
					synterr(f, rcl);
				if (realname)
					free(realname);
				realname = chkmem(Strdup(right));
				continue;
			}
			if (!strlen(left))
				continue;

			synterr(f, rcl);
		}
	}
	fclose(rc);
}

/*
 * Skip leading whitespace.
 */
unsigned char *
skipws(s)
	unsigned char *s;
{
	while (*s && (*s == ' ' || *s == '\t'))
		s++;
	return s;
}

/*
 * Split "LEFT = RIGHT" assignment
 */
static void
splitas(line, left, right)
	char *line, *left, *right;
{
	char *sep;

	if ((sep = strchr(line, '=')) == NULL) {
		right[0] = 0;
		strcpy(left, line);
		return;
	}
	strncpy(left, line, (unsigned)(sep-line));
	strcpy(right, skipws(sep+1));
}

/*
 * Print syntax error message.
 */
static void
synterr(f, l)
	char *f;
	int l;
{
	if (screen_setup_p())
	    iw_printf(COLI_WARN, "%s%s:%d, error: syntax error\n", ppre, f, l);
	else
	    fprintf(stderr, "%s:%d, error: syntax error\n", f, l);

	Exit(1);
}

/*
 * Create the tcp-stream connection via a socket to the specified 
 * server on the specified tcp port.  Returns 0 if connection failed.
 */
static int
linkup()
{
	struct hostent *he;
	struct sockaddr_in sa, local;
	int loc_addrlen;
	struct protoent *pe;
	int s;

	if ((he = gethostbyname(srvdn)) == NULL) {
		iw_printf(COLI_WARN, "%s%s%s: %s%s\n", TBOLD, ppre, srvdn,
			Strerror(errno), TBOLD);
		return 0;
	}
	if ((pe = getprotobyname("tcp")) == NULL) {
		iw_printf(COLI_WARN, "%s%sCannot get protocol number for \
`tcp': %s%s\n", TBOLD, ppre, Strerror(errno), TBOLD);
		return 0;
	}
	if ((s = socket(AF_INET, SOCK_STREAM, pe->p_proto)) == -1) {
		iw_printf(COLI_WARN, "%s%sCannot create socket.%s:%s\n",
			TBOLD, ppre, myname, Strerror(errno), TBOLD);
		return 0;
	}

	memset(&sa, 0, sizeof sa);
	sa.sin_port = htons(port);
	sa.sin_family = AF_INET;
	memmove(&sa.sin_addr, he->h_addr_list[0], (unsigned)he->h_length);

	/* Bind to specified interface, if needed */
        loc_addrlen = sizeof local;
	if (hostname == NULL)
	   getsockname(s, (struct sockaddr *) &local, &loc_addrlen);
	else {
	   if ((he = gethostbyname(hostname)) == NULL) {
	      iw_printf(COLI_WARN, "%s%s%s: %s%s\n", TBOLD, ppre, hostname,
		    Strerror(errno), TBOLD);
	      return 0;
	   }
	   memset(&local, 0, sizeof local);
	   local.sin_family = AF_INET;
	   memmove(&local.sin_addr, he->h_addr_list[0], (unsigned)he->h_length);
	   
	   if (bind(s, (struct sockaddr *)&local, sizeof local) < 0) {
		iw_printf(COLI_WARN, "%s%sCannot bind to %s: %s%s\n", TBOLD, ppre,
			hostname, Strerror(errno), TBOLD);
		return 0;
	   }
	}

	irc_s_addr = local.sin_addr.s_addr;
	
	if (connect(s, (struct sockaddr *)&sa, sizeof sa) < 0) {
		iw_printf(COLI_WARN, "%s%s%s port %d: %s%s\n", TBOLD, ppre,
			srvdn, port, Strerror(errno), TBOLD);
		return 0;
	}

	return s;
}

/* 
 * Handles signals on which we exit.
 */
void
sig_exit(s)
	int s;
{
	char t[64];

	if (on_irc) {
		sprintf(t, "QUIT :received signal %d\r\n", s);
		write(sock, t, strlen(t));
	}
	_exit(1);
}

/*
 * Handles SIGINT 
 * Asks luser if he really wants to exit.
 */
/*ARGSUSED*/
void
sigint(dummy)
	int dummy;
{
	if (sigint_destroys && syspipe <= 0)
		atomic_int_flag = 1;
}

void
check_sigint()
{
	if (atomic_int_flag) {
		if (askyn("Really leave IRC? ") == YES) {
			quitirc("bye");
			iw_printf(COLI_TEXT, "%sBailing out...\n", ppre);
			delallchan();
			dcc_killch();
			closemlog();
			Exit(1);
		}
		elrefr(1);
		atomic_int_flag = 0;
	}
}

/*
 * Check if a pointer is valid and returns that pointer.
 * If not, it issues a fatal "out of memory" error and exits.
 */
VOIDPTR
chkmem(p)
	VOIDPTR p;
{
	if (p != NULL)
		return p;

	fprintf(stderr, "\n%s: memory allocation failed, exiting\n", myname);
	Exit(1);
	/*NOTREACHED*/
	return NULL;
}

int
our_signal(sig, hnd)
	int sig;
	void (*hnd) __P((int));
{
	struct sigaction sa;

	sa.sa_handler = hnd;
	sigemptyset(&sa.sa_mask);
/*#ifdef	SA_RESTART
	sa.sa_flags = SA_RESTART;
#else*/
	sa.sa_flags = 0;
/*#endif*/

	if (sigaction(sig, &sa, NULL) < 0) {
		fprintf(stderr, "%s: in our_signal(): ", myname);
		perror(NULL);
		return -1;
	}
	return 0;
}

/*
 * Handle program suspension/job control (SIGTSTP).
 */
/*ARGSUSED*/
void
sigtstp(dummy)
	int dummy;
{
	sigset_t sset;
	int tchg = 0;

#ifdef	DEBUGV
	iw_printf(COLI_TEXT, "%sHandling SIGTSTP\n", ppre);
#endif
#ifdef HAVE_GETSID
#ifdef DEBUGV
	iw_printf(COLI_TEXT, "%spid=%d, pgid=%d, sid=%d\n", ppre, getpid(),
		getpgrp(), getsid(getpid()));
#endif
	if (getpgrp() == getsid(getpid())) {
		iw_printf(COLI_TEXT, "%sI am session leader, no shell to suspend to\n", ppre);
		return;
	}
#endif	/* HAVE_GETSID */
yikes:
	tty_addbuff(tc_clear);
	tty_gotoxy(0, 0);
	tty_flush();

	if (on_irc)
		fprintf(stderr, "\n%sWarning: suspending tirc inhibits server \
PING answering.\nUse /SYSTEM for shell commands.%s\n", tc_bold, tc_noattr);

	if (!tty_isreset()) {
		tchg = 1;
		tty_reset();
	}
	our_signal(SIGTSTP, SIG_DFL);
	sigemptyset(&sset);
	sigaddset(&sset, SIGTSTP);
	sigprocmask(SIG_UNBLOCK, &sset, NULL);
	raise(SIGTSTP);

	/* Suspended... */

	if (tty_init() < 0) {
		tchg = 0;
		fprintf(stderr, "\nPlease fix your terminal\n");
		goto yikes;
	}
		
	if (tchg)
		tty_cbreak();
	our_signal(SIGTSTP, sigtstp);

	if (!is_in_odlg())
		repinsel();
	else
		odlg_printscreen();
}

/*
 * Handle window resizing.
 */
/*ARGSUSED*/
void
sigwinch(dummy)
	int dummy;
{
	atomic_resize_screen = 1;
}

/*
 * Execute any commands extracted from the rc file with cmdline().
 */
void
rccommands(isconnected)
	int isconnected;
{
	struct aline *al = NULL,
	    *rcstart = (isconnected == RC_IS_CONNECTED) ? rcca : ircca;
	char cmdt[BUFSZ];

	if (rcstart != NULL)
		if (isconnected == RC_NOT_CONNECTED)
			iw_printf(COLI_TEXT, "%sImmediate RC commands\n", ppre);
		else
			iw_printf(COLI_TEXT, "%sConnect-delayed RC commands\n",
				ppre);
	else
		return;

#ifdef	DEBUGV
	iw_printf(COLI_TEXT, "DEBUG: executing stuff\n");
#endif

	for (al = rcstart; al != NULL && al->al_text != NULL;
	    al = al->al_next) {
#ifdef	DEBUGV
		iw_printf(COLI_TEXT, "DEBUG: rc-command \"%s\"\n", al->al_text);
#endif
		strncpy(cmdt, al->al_text, BUFSZ-1);
		cmdt[BUFSZ-1] = '\0';
		cmdline(cmdt);
	}
}

/*
 * Check if a configurable option is enabled.
 */
int
check_conf(option)
	int option;
{
	return (global_options & option) == option;
}

/*
 * Set/reset configurable option.  Returns the new value.
 */
int
set_conf(option, set)
	int option;
	int set;
{
	if (set)
		global_options |= option;
	else
		global_options &= ~option;
	return global_options;
}

/*
 * Handle notification about child process termination.
 */
/*ARGSUSED*/
void
sigchld(dummy)
	int dummy;
{
	extern sig_atomic_t system_ecode;

	dcc_wait_children();

	if (wait(&system_ecode) == systempid)
		closesyspipe = 1;
}

/*
 * Adds a function to the periodic list.  Returns a pointer to the entry
 * on success, NULL otherwise.
 */
struct p_queue *
pq_add(f)
	void (*f) __P((void));
{
	struct p_queue *pqe;

	if (f == NULL)
		return NULL;
	pqe = chkmem(malloc(sizeof (*pqe)));
	pqe->pq_fun = f;
	LIST_INSERT_HEAD(&pqh, pqe, pq_entries);

	return pqe;
}

/*
 * Removes an entry from the periodic list and free it.
 */
void
pq_del(pqe)
	struct p_queue *pqe;
{
	if (pqe != NULL) {
		LIST_REMOVE(pqe, pq_entries);
		free(pqe);
	}
}

/*
 * tirc catches a SIGALRM every minute and steps through the queue if
 * there are any periodic jobs waiting for execution.
 * The signal handler only sets a flag;  the actual queue stepping is
 * done in doirc().
 */
/*ARGSUSED*/
void
sigalrm(dummy)
	int dummy;
{

	atomic_periodic = 1;
}

/* Step through periodic queues and execute pending jobs. */
void
runpqueues()
{

	if (atomic_periodic) {
		struct p_queue *pqe;

		for (pqe = pqh.lh_first; pqe != NULL;
			pqe = pqe->pq_entries.le_next)
			(*pqe->pq_fun)();
	}
	atomic_periodic = 0;
}

/*
 * Called every minute, have the main stat the mailbox.
 */
void
checkmail()
{
	atomic_check4newmail = 1;
}

/*
 * This really does the mail checking.
 */
void
check_mailbox()
{
	char *mbfname;
	struct stat statbuf;

	if (!atomic_check4newmail || !check_conf(CONF_BIFF))
		return;
	atomic_check4newmail = 0;
	if ((mbfname = getenv("MAIL")) == NULL)
		return;
	if (stat(mbfname, &statbuf) == 0) {
		if (statbuf.st_mtime > lastmbmod && statbuf.st_size > 0) {
			iw_printf(COLI_TEXT, "%sThe mail daemon hits.  \
You have new mail.\n", ppre);
			lastmbmod = statbuf.st_mtime;
		}
	}
}

/*
 * Return the in-address of the socket used for server communication
 * as long integer in network byte order.
 */
unsigned long
get_irc_s_addr()
{
	return irc_s_addr;
}

