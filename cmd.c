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
static char rcsid[] = "$Id: cmd.c,v 1.65 1998/02/24 18:30:16 mkb Exp $";
#endif

#include "tirc.h"

#ifdef	HAVE_CTYPE_H
#include <ctype.h>
#endif
#ifdef	HAVE_STRING_H
#include <string.h>
#elif defined(HAVE_MEMORY_H)
#include <memory.h>
#endif
#ifdef	HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#include "compat.h"
#include "help.h"
#include "v.h"
#include "tty.h"
#include "colour.h"
#include "dl.h"

static char	*defchanname __P((void));
static void	split_commands __P((char *));
static char	*expand_alias __P((char *));

#define	GETCHAN(s)	\
	{ if (s != NULL) if (!strcmp(s, "*")) s = defchanname(); }

static void	dumbcmd __P((int, char *));
static void	quitcmd __P((int, char *));
static void	awaycmd __P((int, char *));
static void	cmdchcmd __P((int, char *));
static void	helpcmd __P((int, char *));
static void	joincmd __P((int, char *));
static void	kickcmd __P((int, char *));
static void	clearcmd __P((int, char *));
static void	partcmd __P((int, char *));
static void	msgcmd __P((int, char *));
static void	wincmd __P((int, char *));
static void	ctcpcmd __P((int, char *));
static void	mecmd __P((int, char *));
static void	pingcmd __P((int, char *));
static void	rawcmd __P((int, char *));
static void	servercmd __P((int, char *));
static void	tirccmd __P((int, char *));
static void	topiccmd __P((int, char *));
static void	opercmd __P((int, char *));
static void	killcmd __P((int, char *));
static void	timecmd __P((int, char *));
static void	systemcmd __P((int, char *));
static void	dumbpcmd __P((int, char *));
static void	clistcmd __P((int, char *));
static void	ignorecmd __P((int, char *));
static void	wallopscmd __P((int, char *));
static void	noticecmd __P((int, char *));
static void	cnamescmd __P((int, char *));
static void	opcmd __P((int, char *));
static void	qkcmd __P((int, char *));
static void	kbcmd __P((int, char *));
static void	invitecmd __P((int, char *));
static void	signalcmd __P((int, char *));
static void	lamecmd __P((int, char *));
static void	spamcmd __P((int, char *));
static void	dcccmd __P((int, char *));
static void	whoiscmd __P((int, char *));
static void	namescmd __P((int, char *));
static void	closecmd __P((int, char *));
static void	uhostcmd __P((int, char *));
static void	lastlogcmd __P((int, char *));
static void	umodecmd __P((int, char *));
static void	igncmd __P((int, char *));
static void	desccmd __P((int, char *));
static void	keyscmd __P((int, char *));
static void	nickcmd __P((int, char *));
static void	querycmd __P((int, char *));
static void	aliascmd __P((int, char *));
static void	saycmd __P((int, char *));
static void	logcmd __P((int, char *));
static void	pagecmd __P((int, char *));
static void	oodcmd __P((int, char *));
static void	parsecmd __P((int, char *));
static void	urlcmd __P((int, char *));
static void	uptimecmd __P((int, char *));
static void	colourcmd __P((int, char *));
static void	sourcecmd __P((int, char *));
static void	ncolcmd __P((int, char *));

#define INVSYNTAX	{ setlog(0); iw_printf(COLI_TEXT, "%sInvalid command \
syntax for /%s\n", ppre, ctbl[i].c_name); setlog(1); }

struct alias_entry {
	unsigned long	a_hash;
	char		*a_name;
	char		*a_expansion;
	LIST_ENTRY(alias_entry) a_entries;
};

struct command_entry {
	char		*c_string;
	TAILQ_ENTRY(command_entry) c_entries;
};

extern char	ppre[], nick[];
extern int	sock, debugwin;
extern int	on_irc;
extern FILE	*lastlog;
extern int	errno;

struct cmdtbl {
	const char	*c_name;
	unsigned long	c_hash;
	void 		(*c_fun) __P((int, char *));
	unsigned	c_flags;
	const char	*c_help;
} ctbl[] = {
{ "ADMIN",	0,	dumbcmd,	CF_SERV,	adminhelp	},
{ "ALIAS",	0,	aliascmd,	0,		aliashelp	},
{ "AWAY",	0,	awaycmd,	CF_SERV,	awayhelp	},
{ "BYE",	0,	quitcmd,	0,		quithelp	},
{ "CLEAR",	0,	clearcmd,	0,		clearhelp	},
{ "CLIST",	0,	clistcmd,	0,		clisthelp	},
{ "CMDCH",	0,	cmdchcmd,	0,		cmdchhelp	},
{ "CONNECT",	0,	dumbcmd,	CF_SERV,	connecthelp	},
{ "DATE",	0,	timecmd,	CF_SERV,	datehelp	},
{ "DIE",	0,	dumbcmd,	CF_SERV,	diehelp		},
{ "EXIT",	0,	quitcmd,	0,		quithelp	},
{ "HELP",	0,	helpcmd,	0,		helphelp	},
{ "IGNORE",	0,	ignorecmd,	0,		ignorehelp	},
{ "INFO",	0,	dumbcmd,	CF_SERV,	infohelp	},
{ "INVITE",	0,	invitecmd,	CF_SERV,	invitehelp	},
{ "JOIN",	0,	joincmd,	CF_SERV,	joinhelp	},
{ "KICK",	0,	kickcmd,	CF_SERV,	kickhelp	},
{ "KILL",	0,	killcmd,	CF_SERV,	killhelp	},
{ "LEAVE",	0,	partcmd,	CF_SERV,	parthelp	},
{ "LINKS",	0,	dumbcmd,	CF_SERV,	linkshelp	},
{ "LIST",	0,	dumbpcmd,	CF_SERV,	listhelp	},
{ "LUSERS",	0,	dumbcmd,	CF_SERV,	lusershelp	},
{ "M",		0,	msgcmd,		CF_SERV,	msghelp		},
{ "ME",		0,	mecmd,		CF_SERV,	mehelp		},
{ "MSG",	0,	msgcmd,		CF_SERV,	msghelp		},
{ "MODE",	0,	dumbcmd,	CF_SERV,	modehelp	},
{ "MOTD",	0,	dumbcmd,	CF_SERV,	motdhelp	},
{ "N",		0,	namescmd,	CF_SERV,	nameshelp	},
{ "NAMES",	0,	namescmd,	CF_SERV,	nameshelp	},
{ "NICK",	0,	nickcmd,	0,		nickhelp	},
{ "NOTICE",	0,	noticecmd,	CF_SERV,	noticehelp	},
{ "OPER",	0,	opercmd,	CF_SERV,	operhelp	},
{ "PART",	0,	partcmd,	CF_SERV,	parthelp	},
{ "PING",	0,	pingcmd,	CF_SERV,	pinghelp	},
{ "QUERY",	0,	querycmd,	CF_SERV,	queryhelp	},
{ "QUIT",	0,	quitcmd,	0,		quithelp	},
{ "CLOSE",	0,	closecmd,	0,		closehelp	},
{ "RAW",	0,	rawcmd,		CF_SERV,	rawhelp		},
{ "REHASH",	0,	dumbcmd,	CF_SERV,	rehashhelp	},
{ "RESTART",	0,	dumbcmd,	CF_SERV,	restarthelp	},
{ "SET",	0,	setcmd,		0,		sethelp		},
{ "SERVER",	0,	servercmd,	0,		serverhelp	},
{ "SIGNOFF",	0,	quitcmd,	0,		quithelp	},
{ "SQUIT",	0,	dumbcmd,	CF_SERV,	squithelp	},
{ "STATS",	0,	dumbcmd,	CF_SERV,	statshelp	},
{ "SUMMON",	0,	dumbcmd,	CF_SERV,	summonhelp	},
{ "SYSTEM",	0,	systemcmd,	0,		systemhelp	},
{ "TIME",	0,	timecmd,	CF_SERV,	datehelp	},
{ "TIRC",	0,	tirccmd,	0,		tirchelp	},
{ "TOPIC",	0,	topiccmd,	CF_SERV,	topichelp	},
{ "TRACE",	0,	dumbcmd,	CF_SERV,	tracehelp	},
{ "USERS",	0,	dumbcmd,	CF_SERV,	usershelp	},
{ "VERSION",	0,	dumbcmd,	CF_SERV,	versionhelp	},
{ "W",		0,	whoiscmd,	CF_SERV,	whoishelp	},
{ "WALLOPS",	0,	wallopscmd,	CF_SERV,	wallopshelp	},
{ "WHO",	0,	dumbpcmd,	CF_SERV,	whohelp		},
{ "WHOIS",	0,	whoiscmd,	CF_SERV,	whoishelp	},
{ "WHOWAS",	0,	dumbcmd,	CF_SERV,	whowashelp	},
{ "WIN",	0,	wincmd,		0,		winhelp		},
{ "CTCP",	0,	ctcpcmd,	CF_SERV,	ctcphelp	},
{ "CNAMES",	0,	cnamescmd,	0,		cnameshelp	},
{ "OP",		0,	opcmd,		CF_SERV,	ophelp		},
{ "DEOP",	0,	opcmd,		CF_SERV,	deophelp	},
{ "QK",		0,	qkcmd,		CF_SERV,	qkhelp		},
{ "KB",		0,	kbcmd,		CF_SERV,	kbhelp		},
{ "ISON",	0,	dumbcmd,	CF_SERV,	isonhelp	},
{ "SIGNAL",	0,	signalcmd,	0,		signalhelp	},
{ "LAME",	0,	lamecmd,	CF_SERV,	lamehelp	},
{ "LART",	0,	kbcmd,		CF_SERV,	kbhelp		},
{ "SPAM",	0,	spamcmd,	0,		spamhelp	},
{ "DCC",	0,	dcccmd,		0,		dcchelp		},
{ "USERHOST",	0,	uhostcmd,	CF_SERV,	uhosthelp	},
{ "UHOST",	0,	uhostcmd,	CF_SERV,	uhosthelp	},
{ "LASTLOG",	0,	lastlogcmd,	0,		lastloghelp	},
{ "UMODE",	0,	umodecmd,	CF_SERV,	umodehelp	},
{ "IGN",	0,	igncmd,		CF_SERV,	ignhelp		},
{ "DESC",	0,	desccmd,	CF_SERV,	deschelp	},
{ "KEYS",	0,	keyscmd,	0,		keyshelp	},
{ "SAY",	0,	saycmd,		CF_SERV,	sayhelp		},
{ "LOG",	0,	logcmd,		0,		loghelp		},
{ "PAGE",	0,	pagecmd,	0,		pagehelp	},
{ "OOD",	0,	oodcmd,		0,		oodhelp		},
{ "PARSE",	0,	parsecmd,	CF_SERV,	parsehelp	},
{ "URL",	0,	urlcmd,		0,		urlhelp		},
{ "UPTIME",	0,	uptimecmd,	0,		uptimehelp	},
{ "COLOUR",	0,	colourcmd,	0,		colourhelp	},
{ "SOURCE",	0,	sourcecmd,	0,		sourcehelp	},
{ "NCOL",	0,	ncolcmd,	0,		ncolhelp	},
#ifdef	WITH_DLMOD
{ "DLMOD",	0,	dlmodcmd,	0,		dlmodhelp	},
#endif
{ "SQUERY",	0,	dumbcmd,	CF_SERV,	squeryhelp	},
{ "SERVLIST",	0,	dumbcmd,	CF_SERV,	servlisthelp	},
};

char	cmdch;
void	(*othercmd) __P((char *));

static struct p_queue *clockpq;
static struct p_queue *biffpq;
static struct p_queue *ipguppq;
static char	*redoc;
static enum redotype redot;
static int	allow_emptycmd;
static LIST_HEAD(, alias_entry) alias_list;
static TAILQ_HEAD(, command_entry) command_queue;

int	pasting;	/* ref: set_prompt */
int	numcmd;		/* dl.c */

/*
 * Init the command parsing.
 */
void
cmdinit()
{
	int i;

	numcmd = sizeof (ctbl) / sizeof (ctbl[0]);

	for (i = 0; i < numcmd; i++)
		ctbl[i].c_hash = elf_hash(ctbl[i].c_name);
	cmdch = '/';
	othercmd = NULL;
	redoc = NULL;
	allow_emptycmd = 0;
	LIST_INIT(&alias_list);
	TAILQ_INIT(&command_queue);
	pasting = 0;
}

/*
 * Parses & executes a user command line.
 */
void
cmdline(l)
	char *l;
{
	int i;
	unsigned long h;
	char cmd[MSGSZ], *t, *u;
	struct command_entry *ce, *cf;

	if (!strlen(l) && !allow_emptycmd) {
		elhome();
		return;
	}

	/*
	 * If we have a special parsing function, execute this, otherwise
	 * try to parse it as a tirc command sequence or send it as a channel
	 * PRIVMSG.
	 */
	if (othercmd != NULL) {
		(*othercmd)(l);
		elhome();
		return;
	}
		
	if (*l == cmdch && *(l+1) != cmdch && !isspace(*(l+1))
	    && !pasting) {
	    split_commands(l);

	    for (ce = command_queue.tqh_first; ce != NULL; ce = cf) {
		cf = ce->c_entries.tqe_next;

		/* Ignore channel privmsg in composite command. */
		if (*ce->c_string != cmdch) {
	    		TAILQ_REMOVE(&command_queue, ce, c_entries);
			free(ce->c_string);
			free(ce);
			continue;
		}

		/*
		 * Seperate the command name, get it's hash value and
		 * execute it.
		 */
		if ((t = expand_alias(ce->c_string + 1)) == NULL) {
	    		TAILQ_REMOVE(&command_queue, ce, c_entries);
			free(ce->c_string);
			free(ce);
			continue;
		}
		u = cmd;

		while (*t && !isspace(*t))
			*u++ = *t++;
		*u = 0;
		while (*t && isspace(*t))
			t++;
		h = elf_hash(irc_strupr(cmd));
		for (i = 0; i < numcmd; i++)
			if (ctbl[i].c_hash == h
			    && 0 == strcmp(ctbl[i].c_name, cmd)) {
				if (ctbl[i].c_flags & CF_SERV && !on_irc)
				    iw_printf(COLI_TEXT, "%sNot connected to a \
server\n", ppre);
				else
				    ctbl[i].c_fun(i, t);
				elhome();
				break;
			}
		if (i == numcmd) {
#ifdef	WITH_DLMOD
			if (dlfuncrun(cmd, i, t) != 0)
#endif
				iw_printf(COLI_TEXT,
    "%s\"%s\" is unknown command to me.  /help lists all available commands.\n",
					    ppre, cmd);
			elhome();
		}

		TAILQ_REMOVE(&command_queue, ce, c_entries);
		free(ce->c_string);
		free(ce);
	    }
	}
	else {
		if (!pasting && *l == cmdch && (*(l+1) == cmdch
		    || isspace(*(l+1))))
			privmsg(NULL, l+1, 0);
		else
			privmsg(NULL, l, 0);
		elhome();
	}
}

/*
 * Simply send the command name and its parameters to the server.
 * The parameters are not escaped with ':' so that each word is
 * parsed by ircd as an single parameter.
 */
static void
dumbcmd(i, pars)
	int i;
	char *pars;
{
	char *first, *rest;
	static char dummy[] = "";

	if ((first = strtok(pars, " \t")) != NULL) {
		GETCHAN(first)
		rest = strtok(NULL, "");
	}
	else
		first = rest = dummy;

	dprintf(sock, "%s %s %s\r\n", ctbl[i].c_name, first,
		(rest != NULL) ? rest : "");
}

/*
 * Quit tirc.
 */
/*ARGSUSED*/
static void
quitcmd(i, pars)
	int i;
	char *pars;
{
	extern user_exit_flag;
	static char def[] = "bye";
	int a;

	a = askyn("Quit from IRC and exit? ");
	elrefr(1);
	if (a == NO)
		return;
	if (!strlen(pars))
		pars = def;

	iw_closelogs();
	iw_printf(COLI_TEXT, "%sExiting...\n", ppre);
	if (on_irc) {
		quitirc(pars);
		user_exit_flag = 1;
	}
	else
		Exit(0);
}

/*
 * Close connection.
 */
/*ARGSUSED*/
static void
closecmd(i, pars)
	int i;
	char *pars;
{
	static char def[] = "bye";
	int a;

	a = askyn("Quit from IRC? ");
	elrefr(1);
	if (a == NO)
		return;
	if (!strlen(pars))
		pars = def;

	iw_closelogs();
	iw_printf(COLI_SERVMSG, "%sSignoff (%s)\n", ppre, (on_irc) ? pars : "");
	quitirc(pars);
}

/*
 * Toggle away/here.  If no parameter is given, the user is unset away.
 */
static void
awaycmd(i, pars)
	int i;
	char *pars;
{
	dprintf(sock, "%s :%s\r\n", ctbl[i].c_name, pars);
}

/*
 * Change the command prefix char.  By default this is '/' so that
 * commands look like /command {parameters}.
 */
/*ARGSUSED*/
static void
cmdchcmd(i, pars)
	int i;
	char *pars;
{
	if (strlen(pars) != 1 || !isprint(*pars)) {
		iw_printf(COLI_TEXT, "%sIllegal cmdch specified '%s'\n", ppre,
			pars);
	}
	else {
		cmdch = *pars;
		iw_printf(COLI_TEXT, "%sCommand prefix character is now '%c'\n",
			ppre, cmdch);
	}
}

/*
 * Online help system.
 */
static void
helpcmd(i, pars)
	int i;
	char *pars;
{
	unsigned long h;
	char *p;

	p = chkmem(Strdup(pars));
	h = elf_hash(irc_strupr(p));
	free(p);
	setlog(0);

	for (i = 0; i < numcmd; i++)
		if (ctbl[i].c_hash == h)
			break;
	if (i == numcmd) {
		iw_printf(COLI_HELP, "%sNo help available on this command\n",
			ppre);
		iw_printf(COLI_HELP, "%sUsage: %s\n", ppre, helphelp);
	}
	else
		iw_printf(COLI_HELP, "%sUsage: %s\n", ppre, ctbl[i].c_help);
	iw_printf(COLI_HELP, "%sEnd of help\n", ppre);

	setlog(1);
	return;
}

/*
 * Kick someone off a channel with an optional comment.
 */
static void
kickcmd(i, pars)
	int i;
	char *pars;
{
	char *chan, *luser, *comment;
	static char sep[] = " \t", defc[] = "No comment.";

	chan = strtok(pars, sep);
	GETCHAN(chan)
	luser = strtok(NULL, sep);
	comment = strtok(NULL, "");
	if (chan == NULL || luser == NULL) {
		INVSYNTAX
		return;
	}
	if (comment == NULL)
		comment = defc;
	dprintf(sock, "KICK %s %s :%s\r\n", chan, luser, comment);
}

/*
 * Clear current window.
 */
/*ARGSUSED*/
static void
clearcmd(i, pars)
	int i;
	char *pars;
{
	clearwin();
}

/*
 * Join a channel.
 * Works only for one channel at the same time.
 */
static void
joincmd(i, pars)
	int i;
	char *pars;
{
	struct channel *ch;
	char *chan, *key;
	static char sep[] = " \t";

	chan = strtok(pars, sep);
	key = strtok(NULL, sep);

	if (chan == NULL || !strlen(chan)) {
		INVSYNTAX
		return;
	}

	if ((ch = getchanbyname(chan)) != NULL) {
		/*
		 * We are already on that channel.  Move it to the top 
		 * of the current window.  No new JOIN will be issued.
		 */
		iw_delchan(ch);
		iw_addchan(ch);
		set_prompt(NULL);
		iw_printf(COLI_TEXT, "%sNow talking to %s\n", ppre, pars);
		iw_draw();
		elrefr(0);
		return;
	}

	iw_printf(COLI_TEXT, "%sJoining %s\n", ppre, chan);
	dprintf(sock, "JOIN %s %s\r\n", pars, (key != NULL) ? key : "");
}

/*
 * Part (leave) a channel with an optional comment.
 * Works only for one channel at the same time.
 */
static void
partcmd(i, pars)
	int i;
	char *pars;
{
	char *chan, *comment;
	static char sep[] = " \t", defc[] = "";

	chan = strtok(pars, sep);
	GETCHAN(chan)
	comment = strtok(NULL, "");
	if (chan == NULL) {
		INVSYNTAX
		return;
	}
	if (comment == NULL)
		comment = defc;
	dprintf(sock, "PART %s :%s\r\n", chan, comment);
}

/*
 * Send a PRIVMSG to a channel or a user.
 * Empty messages are ignored.
 */
static void
msgcmd(i, pars)
	int i;
	char *pars;
{
	char *target, *txt;
	static char sep[] = " \t";

	target = strtok(pars, sep);
	txt = strtok(NULL, "");
	if (target == NULL || txt == NULL) {
		INVSYNTAX
		return;
	}
	if (!strlen(txt))
		return;
	privmsg(target, txt, 0);
}

/*
 * Window operations.
 */
static void
wincmd(i, pars)
	int i;
	char *pars;
{
	char *func, *arg;
	static char sep[] = " \t";
	struct iwstatus st;
	struct channel *ch;

	if ((func = strtok(pars, sep)) == NULL) {
		INVSYNTAX
		return;
	}
	irc_strupr(func);

	if (!strcmp("NEW", func)) {
		newircwin();
		iwcmode(iwcmode(-1) | IMT_CTRL | IMT_PMSG);
		elrefr(0);
		return;
	}

	if (!strcmp("DEL", func)) {
		delircwin();
		elrefr(0);
		return;
	}

	if (!strcmp("RES", func)) {
		if ((arg = strtok(NULL, sep)) == NULL) {
			INVSYNTAX
			return;
		}
		switch(*arg) {
		case '+':
			iw_resize(atoi(arg+1));
			break;
		case '-':
			iw_resize(-(atoi(arg+1)));
			break;
		case '=':
			equalwin();
			break;
		default:
			INVSYNTAX
			break;
		}
		return;
	}

	if (!strcmp("STATUS", func)) {
		setlog(0);
		iwc_status(&st);
		iw_printf(COLI_TEXT, "%sCurrent window: start %d, end %d, %d \
lines\n%sChannel list: ", ppre, st.iws_start, st.iws_end, st.iws_nlines, ppre);
		if (st.iws_ch == NULL)
			iw_printf(COLI_TEXT, "no channels\n");
		else {
			for (ch = st.iws_ch; ch != NULL; ch = ch->ch_wnext)
				iw_printf(COLI_TEXT, "%s ", ch->ch_name);
			iw_printf(COLI_TEXT, "\n");
		}
		if (st.iws_log != NULL)
			iw_printf(COLI_TEXT, "%sLogging to %s\n", ppre,
				st.iws_log);

		setlog(1);
		return;
	}

	if (!strcmp("MODE", func)) {
#define ADJMODE(a) if (mm < 0) m &= ~(a); else m |= (a);
		int mm = 0, m = iwcmode(-1);

		if ((arg = strtok(NULL, sep)) == NULL) {
			INVSYNTAX
			return;
		}

		while (*arg)
			switch (toupper(*arg++)) {
			case '+':
				mm = 1;
				break;
			case '-':
				mm = -1;
				break;
			case 'S':
				ADJMODE(IMT_CTRL)
				break;
			case 'C':
				ADJMODE(IMT_CMSG)
				break;
			case 'P':
				ADJMODE(IMT_PMSG)
				break;
			case 'D':
				if (!(m & IMT_DEBUG) && mm > 0)
					debugwin++;
				else if (m & IMT_DEBUG && mm < 0)
					debugwin--;
				ADJMODE(IMT_DEBUG)
				break;
			}
		iwcmode(m);
		return;
	}

	if (!strcmp("LOG", func)) {
		iw_logtofile(strtok(NULL, sep));
		return;
	}

	INVSYNTAX
}

/*
 * Put out something between Ctrl-A's.
 */
static void
ctcpcmd(i, pars)
	int i;
	char *pars;
{
	char *target, *cmd, *txt;
	static char sep[] = " \t";
	char t[MSGSZ];

	target = strtok(pars, sep);
	cmd = strtok(NULL, sep);
	txt = strtok(NULL, "");

	if (target == NULL || cmd == NULL) {
		INVSYNTAX
		return;
	}

	if (!strlen(cmd))
		return;

	if (!strcmp("PING", irc_strupr(cmd))) {
		struct timeval tv;

		gettimeofday(&tv, NULL);
		sprintf(t, "\1PING %ld\1", (long)tv.tv_sec);
	}
	else {
		if (txt != NULL)
			sprintf(t, "\1%s %s\1", cmd, txt);
		else
			sprintf(t, "\1%s\1", cmd);
	}
	privmsg(target, t, 1);
}

/*
 * Issue the ctcp ACTION command to the current channel.
 */
/*ARGSUSED*/
static void
mecmd(i, pars)
	int i;
	char *pars;
{
	struct channel *ch;
	char t[MSGSZ];
	struct iw_msg wm;

	if ((ch = cchan()) == NULL) {
		iw_printf(COLI_TEXT, "%sNo top channel\n", ppre);
		return;
	}
	sprintf(t, "\1ACTION %s\1", pars);
	privmsg(ch->ch_name, t, 1);

	sprintf(t, "* %s %s\n", nick, pars);
	wm.iwm_text = t;
	wm.iwm_type = IMT_CMSG | IMT_FMT;
	wm.iwm_chn = ch;
	wm.iwm_chint = COLI_ACTION;
	dispose_msg(&wm);
}

/*
 * Control tirc behaviour.
 * This function is global, since it is used by the options editor to
 * update the options.
 * Note: if 'i is -1, setcmd was not invoked through the command dispatch
 * table but explicitly (for example by the options editor).
 */
void
setcmd(i, pars)
	int i;
	char *pars;
{
	char *option, *args;
	static char sep[] = " \t", dummy[] = "",
		valfmt[] = "%sValue of %-9s is %s\n";
	int all = 0;

	if ((option = strtok(pars, sep)) == NULL) {
		all = 1;
		option = dummy;
	}
	args = strtok(NULL, sep);
	irc_strupr(option);

	if (all)
		iw_buf_printf(COLI_TEXT,
		  "%sOption variables settings (explanation see /help set):\n",
		  ppre);

	if (!strcmp("CTCP", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args))
				set_conf(CONF_CTCP, 1);
			else if (!strcmp("OFF", args))
				set_conf(CONF_CTCP, 0);
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "CTCP",
			(check_conf(CONF_CTCP)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("STAMP", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args))
				set_conf(CONF_STAMP, 1);
			else if (!strcmp("OFF", args))
				set_conf(CONF_STAMP, 0);
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "STAMP",
			(check_conf(CONF_STAMP)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("FLOODP", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args))
				set_conf(CONF_FLOODP, 1);
			else if (!strcmp("OFF", args))
				set_conf(CONF_FLOODP, 0);
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "FLOODP",
			(check_conf(CONF_FLOODP)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("FLSTRICT", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args))
				set_conf(CONF_FLSTRICT, 1);
			else if (!strcmp("OFF", args))
				set_conf(CONF_FLSTRICT, 0);
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "FLSTRICT",
			(check_conf(CONF_FLSTRICT)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("FLIGNORE", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args))
				set_conf(CONF_FLIGNORE, 1);
			else if (!strcmp("OFF", args))
				set_conf(CONF_FLIGNORE, 0);
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "FLIGNORE",
			(check_conf(CONF_FLIGNORE)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("KBIGNORE", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args))
				set_conf(CONF_KBIGNORE, 1);
			else if (!strcmp("OFF", args))
				set_conf(CONF_KBIGNORE, 0);
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "KBIGNORE",
			(check_conf(CONF_KBIGNORE)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("BEEP", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args))
				set_conf(CONF_BEEP, 1);
			else if (!strcmp("OFF", args))
				set_conf(CONF_BEEP, 0);
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "BEEP",
			(check_conf(CONF_BEEP)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("HIMSG", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args))
				set_conf(CONF_HIMSG, 1);
			else if (!strcmp("OFF", args))
				set_conf(CONF_HIMSG, 0);
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "HIMSG", 
			(check_conf(CONF_HIMSG)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("CLOCK", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args)) {
				set_conf(CONF_CLOCK, 1);
				clockpq = pq_add(disp_clock);
				iw_draw();
			}
			else if (!strcmp("OFF", args)) {
				set_conf(CONF_CLOCK, 0);
				pq_del(clockpq);
				iw_draw();
			}
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "CLOCK",
			(check_conf(CONF_CLOCK)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("BIFF", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args)) {
				set_conf(CONF_BIFF, 1);
				biffpq = pq_add(checkmail);
				iw_draw();
			}
			else if (!strcmp("OFF", args)) {
				set_conf(CONF_BIFF, 0);
				pq_del(biffpq);
				iw_draw();
			}
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "BIFF",
			(check_conf(CONF_BIFF)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("XTERMT", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args)) {
				char *termvar = getenv("TERM");

				if (termvar != NULL
				    && (!strncmp("xterm", termvar, 5)
				    || !strncmp("rxvt", termvar, 5)))
				    set_conf(CONF_XTERMT, 1);
				else
				    iw_printf(COLI_TEXT, "%sNot available with \
this terminal type\n", ppre);
			}
			else if (!strcmp("OFF", args))
				set_conf(CONF_XTERMT, 0);
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "XTERMT",
			(check_conf(CONF_XTERMT)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("PGUPDATE", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args)) {
				set_conf(CONF_PGUPDATE, 1);
				ipguppq = pq_add(ipg_updatechk);
			}
			else if (!strcmp("OFF", args)) {
				pq_del(ipguppq);
				set_conf(CONF_PGUPDATE, 0);
			}
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "PGUPDATE",
			(check_conf(CONF_PGUPDATE)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("OOD", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args))
				set_conf(CONF_OOD, 1);
			else if (!strcmp("OFF", args))
				set_conf(CONF_OOD, 0);
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "OOD",
			(check_conf(CONF_OOD)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("ATTRIBS", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args))
				set_conf(CONF_ATTRIBS, 1);
			else if (!strcmp("OFF", args))
				set_conf(CONF_ATTRIBS, 0);
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "ATTRIBS",
			(check_conf(CONF_ATTRIBS)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("COLOUR", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args))
				set_conf(CONF_COLOUR, 1);
			else if (!strcmp("OFF", args))
				set_conf(CONF_COLOUR, 0);
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "COLOUR",
			(check_conf(CONF_COLOUR)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("STIPPLE", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args))
				set_conf(CONF_STIPPLE, 1);
			else if (!strcmp("OFF", args))
				set_conf(CONF_STIPPLE, 0);
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "STIPPLE",
			(check_conf(CONF_STIPPLE)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("HIDEMCOL", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args))
				set_conf(CONF_HIDEMCOL, 1);
			else if (!strcmp("OFF", args))
				set_conf(CONF_HIDEMCOL, 0);
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "HIDEMCOL",
			(check_conf(CONF_HIDEMCOL)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("NCOLOUR", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args))
				set_conf(CONF_NCOLOUR, 1);
			else if (!strcmp("OFF", args))
				set_conf(CONF_NCOLOUR, 0);
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "NCOLOUR",
			(check_conf(CONF_NCOLOUR)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("NUMERICS", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args))
				set_conf(CONF_NUMERICS, 1);
			else if (!strcmp("OFF", args))
				set_conf(CONF_NUMERICS, 0);
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "NUMERICS",
			(check_conf(CONF_NUMERICS)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

	if (!strcmp("TELEVIDEO", option) || all) {
		if (args != NULL) {
			irc_strupr(args);
			if (!strcmp("ON", args)) {
				set_conf(CONF_TELEVIDEO, 1);
				tty_set_opt(tty_get_opt() | TTY_NOATTRIBS);
			}
			else if (!strcmp("OFF", args)) {
				set_conf(CONF_TELEVIDEO, 0);
				tty_set_opt(tty_get_opt() & ~TTY_NOATTRIBS);
			}
		}
		iw_buf_printf(COLI_TEXT, valfmt, ppre, "TELEVIDEO",
			(check_conf(CONF_TELEVIDEO)) ? "ON" : "OFF");
		if (!all) {
			setlog(0);
			iw_buf_flush();
			setlog(1);
			return;
		}
	}

#ifndef	SUPPRESS_ENDOF
	if (all)
		iw_buf_printf(COLI_TEXT, "%sEnd of /SET\n", ppre);
#endif
	setlog(0);
	iw_buf_flush();
	setlog(1);
}

/*
 * Ping the server.
 */
/*ARGSUSED*/
static void
pingcmd(i, pars)
	int i;
	char *pars;
{
	dprintf(sock, "PING :%s\r\n", nick);
}

/*
 * Send a raw line to the server.  Thus we can issue server commands 
 * that are not directly supported by tirc.
 */
/*ARGSUSED*/
static void
rawcmd(i, pars)
	int i;
	char *pars;
{
	dprintf(sock, "%s\r\n", pars);
}

/*
 * Linkup to the specified server. Quits an existing connection.
 */
static void
servercmd(i, pars)
	int i;
	char *pars;
{
	char *hostname, *prt, *pw;
	static char sep[] = " \t";
	extern int port;
	extern char *pass, *srvdn;
	extern int restart_irc;

	hostname = strtok(pars, sep);
	prt = strtok(NULL, sep);
	pw = strtok(NULL, sep);

	if (hostname == NULL) {
		INVSYNTAX
		return;
	}
	if (srvdn != NULL)
		free(srvdn);
	srvdn = chkmem(Strdup(hostname));

	if (prt != NULL)
		port = atoi(prt);
	if (pw != NULL) {
		if (pass != NULL)
			free(pass);
		pass = chkmem(Strdup(pw));
	}
	if (on_irc)
		dprintf(sock, "QUIT :Changing servers\r\n");
	restart_irc = 1;
}

/*
 * Show tirc info.
 */
/*ARGSUSED*/
static void
tirccmd(i, pars)
	int i;
	char *pars;
{
	extern char version[];

	setlog(0);
	iw_printf(COLI_TEXT, "%sversion %s for %s%s\n", ppre, version, osstring,
		CRSTRING);
	setlog(1);
}

/*
 * Set the topic on a channel.
 */
static void
topiccmd(i, pars)
	int i;
	char *pars;
{
	char *chan, *topic;
	static char sep[] = " \t";

	chan = strtok(pars, sep);
	GETCHAN(chan)
	topic = strtok(NULL, "");
	if (chan == NULL) {
		INVSYNTAX
		return;
	}
	if (topic == NULL)
		dprintf(sock, "TOPIC %s\r\n", chan);
	else
		dprintf(sock, "TOPIC %s :%s\r\n", chan, topic);
}

/*
 * Gain IRC operator privileges.
 * We need a nickname and a password. If password isn't specified, prompt
 * for it invisibly.
 */
/*ARGSUSED*/
static void
opercmd(i, pars)
	int i;
	char *pars;
{
	char *nickname, *pw;
	static char sep[] = " \t";

	nickname = strtok(pars, sep);
	pw = strtok(NULL, sep);
	if (nickname == NULL)
		nickname = nick;
	if (pw == NULL)
		if ((pw = askpass("Operator password:")) == NULL) {
			INVSYNTAX
			return;
		}
	dprintf(sock, "OPER %s %s\r\n", nickname, pw);
	memset(pw, 0, strlen(pw));
}

/*
 * Kill a lamer.
 */
/*ARGSUSED*/
static void
killcmd(i, pars)
	int i;
	char *pars;
{
	char *nickname, *reason;
	static char sep[] = " \t";

	nickname = strtok(pars, sep);
	reason = strtok(NULL, "");
	if (nick == NULL) {
		INVSYNTAX
		return;
	}
	if (reason == NULL || !strlen(reason)) {
		iw_printf(COLI_TEXT, "%sPlease provide a reason why you kill \
this user\n", ppre);
		return;
	}
	dprintf(sock, "KILL %s :%s\r\n", nickname, reason);
}

/*
 * Request time and date from server.
 */
/*ARGSUSED*/
static void
timecmd(i, pars)
	int i;
	char *pars;
{
	dprintf(sock, "TIME\r\n");
}

/*
 * Execute a shell command string in background.
 */
/*ARGSUSED*/
static void
systemcmd(i, pars)
	int i;
	char *pars;
{
	irc_system(pars);
}

/*
 * Check if the dumb command has parameters.  If not, ask if the
 * user really wants to execute the command w/o parameters (used for
 * /names, /list etc. where ircd could kill for flooding).
 */
static void
dumbpcmd(i, pars)
	int i;
	char *pars;
{
	int a;
	char t[80];

	if (!strlen(pars)) {
		sprintf(t, "No parameters given for /%s. Really send? ",
			ctbl[i].c_name);
		a = askyn(t);
		elrefr(1);
		if (a == NO)
			return;	
	}
	dumbcmd(i, pars);
}

/*
 * Spit out the channel list.
 */
/*ARGSUSED*/
static void
clistcmd(i, pars)
	int i;
	char *pars;
{
	extern struct channel *cha;
	struct channel *c;

	setlog(0);
	iw_printf(COLI_TEXT, "%sOn channels: ", ppre);
	if (cha == NULL) {
		iw_printf(COLI_TEXT, "none\n");
		setlog(1);
		return;
	}
	for (c = cha; c != NULL; c = c->ch_next)
		iw_printf(COLI_TEXT, "%s ", c->ch_name);
	iw_printf(COLI_TEXT, "\n");
	setlog(1);
}

/*
 * ignore does the following:
 * Add a source description to the ignore list,
 * remove a source description from the ignore list and
 * print the ignore list.
 */
static void
ignorecmd(i, pars)
	int i;
	char *pars;
{
	char *mode, *string;
	static char sep[] = " \t";

	mode = strtok(pars, sep);
	string = strtok(NULL, "");
	if (mode == NULL) {
		INVSYNTAX
		return;
	}
	irc_strupr(mode);

	setlog(0);

	if (!strcmp(mode, "ADD")) {
		/* Add a source description to the ignorance list. */
		if (string == NULL) {
			INVSYNTAX
			return;
		}
		add_ignore(string, 0);
	}
	else if (!strcmp(mode, "DEL")) {
		/*
		 * Remove a source from the ignorance list.  String is
		 * the numerical identifier.
		 */
		if (string == NULL) {
			INVSYNTAX
			return;
		}
		del_ignore(atoi(string));
	}
	else if (!strcmp(mode, "LIST"))
		list_ignore();
	else
		INVSYNTAX
	setlog(1);
	return;
}

/*
 * Write a WALLOPS message to all operators.
 */
static void
wallopscmd(i, pars)
	int i;
	char *pars;
{
	if (pars == NULL || !strlen(pars)) {
		INVSYNTAX
		return;
	}
	dprintf(sock, "WALLOPS :%s\r\n", pars);
	iw_printf(COLI_OWNMSG, "-> <WALLOPS> %s\n", pars);
}

static void
noticecmd(i, pars)
	int i;
	char *pars;
{
	static char *target, *txt;
	static char sep[] = " \t";

	target = strtok(pars, sep);
	txt = strtok(NULL, "");
	if (target == NULL || txt == NULL) {
		INVSYNTAX
		return;
	}
	if (!strlen(txt))
		return;
	notice(target, txt, 0);
}

/*
 * List current channel's user name cache.
 */
/*ARGSUSED*/
static void
cnamescmd(i, pars)
	int i;
	char *pars;
{
	iw_printcache();
}

/*
 * Give/take channel op privileges to/from multiple users with previous 
 * cache lookup.
 */
static void
opcmd(i, pars)
	int i;
	char *pars;
{
	struct channel *ch;
	struct cache_user *cu;
	char *n;
	char t[MSGSZ];
	int j, cnt = 0, op;

	if (pars == NULL || !strlen(pars)) {
		INVSYNTAX
		return;
	}
	if ((ch = iw_getchan()) == NULL) {
		iw_printf(COLI_TEXT, "%sNo top channel\n", ppre);
		return;
	}
	t[0] = 0;

	op = (!strcmp(ctbl[i].c_name, "DEOP")) ? 0 : 1;

	if ((n = strtok(pars, " \t,")) != NULL) {
		cu = getfromucache(n, ch, NULL, 0);
		if (op) {
			if (cu != NULL && !(cu->cu_mode & MD_O)) {
				strcat(t, n);
				cnt++;
			}
		}
		else
			if (cu != NULL && cu->cu_mode & MD_O) {
				strcat(t, n);
				cnt++;
			}
	}

	/* We parse up to 16 nicknames. */
	for (j = 1; j < 16; j++) {
		if ((n = strtok(NULL, " \t,")) != NULL) {
			cu = getfromucache(n, ch, NULL, 0);
			if (op) {
				if (cu == NULL || cu->cu_mode & MD_O)
					continue;
			}
			else
				if (cu == NULL || !(cu->cu_mode & MD_O))
					continue;
			strcat(t, " ");
			strcat(t, n);
			if (++cnt == 3) {
				dprintf(sock, "MODE %s %cooo %s\r\n", 
					ch->ch_name, (op) ? '+' : '-', t);
				cnt = 0;
				t[0] = 0;
			}
		}
	}
	/* Op the rest. */
	if (cnt > 0)
		dprintf(sock, "MODE %s %co%c%c %s\r\n", ch->ch_name,
			(op) ? '+' : '-',
			(cnt >= 2) ? 'o' : ' ',
			(cnt >= 3) ? 'o' : ' ',
			t);
}

/*
 * Remove multiple users from the current channel.  Checks nicks against
 * the user cache for not generating unrequired traffic.
 */
static void
qkcmd(i, pars)
	int i;
	char *pars;
{
	struct channel *ch;
	struct cache_user *cu;
	char *n;
	static char reason[] = "Out.";
	int j;

	if (pars == NULL || !strlen(pars)) {
		INVSYNTAX
		return;
	}
	if ((ch = iw_getchan()) == NULL) {
		iw_printf(COLI_TEXT, "%sNo top channel\n", ppre);
		return;
	}

	if ((n = strtok(pars, " \t")) != NULL) {
		cu = getfromucache(n, ch, NULL, 0);
		if (cu != NULL)
			dprintf(sock, "KICK %s %s :%s\r\n", ch->ch_name,
				n, reason);
	}

	for (j = 1; j < 16; j++) {
		if ((n = strtok(NULL, " \t")) == NULL)
			break;
		cu = getfromucache(n, ch, NULL, 0);
		if (cu != NULL)
			dprintf(sock, "KICK %s %s :%s\r\n", ch->ch_name,
				n, reason);
	}
}

/*
 * Kickban a luser off the current channel.
 */
static void
kbcmd(i, pars)
	int i;
	char *pars;
{
	struct channel *ch;
	char *b, *pure, *n, *c, *defc = "Begone.", *lart = "LARTed.";
	int needredo;
	char t[MSGSZ];

	if (pars == NULL || !strlen(pars) ||
	    (n = strtok(pars, " \t")) == NULL) {
		INVSYNTAX
		return;
	}
	if ((c = strtok(NULL, "")) == NULL)
		if (!strcmp(ctbl[i].c_name, "LART"))
			c = lart;
		else
			c = defc;

	if ((ch = iw_getchan()) == NULL) {
		iw_printf(COLI_TEXT, "%sNo top channel\n", ppre);
		return;
	}
	if (!irc_strcmp(n, nick)) {
		iw_printf(COLI_TEXT, "%sYou like self-bondage in open public, \
d00d?\n", ppre);
		return;
	}

	if ((b = buildban(pars, ch, &pure, &needredo)) == NULL) {
		if (needredo) {
			sprintf(t, "/%s %s %s", ctbl[i].c_name, pars, c);
			setredo(t, REDO_USERHOST);
		}
		return;
	}

	dprintf(sock, "MODE %s\r\n", b);
	dprintf(sock, "KICK %s %s :%s\r\n", ch->ch_name, n, c);
	if (check_conf(CONF_KBIGNORE))
		add_ignore(pure, 0);
}

/*
 * Invite someone to a channel we are on.
 */
static void
invitecmd(i, pars)
	int i;
	char *pars;
{
	char *nname, *chan;

	if ((nname = strtok(pars, " \t")) == NULL || (chan = strtok(NULL, ""))
		== NULL)
		return;
	GETCHAN(chan)
	dprintf(sock, "%s %s %s\r\n", ctbl[i].c_name, nname, chan);
}

/*
 * Get the channel name of the currently focussed channel, or NULL
 * if no top channel exists.
 */
static char *
defchanname()
{
	struct channel *ch = iw_getchan();
	static char *channame;

	if (channame != NULL)
		free(channame);

	if (ch == NULL || ch->ch_name == NULL)
		return NULL;
	else
		channame = chkmem(Strdup(ch->ch_name));

	return channame;
}

/*
 * Sends the specified signal to the running background process (/SYSTEM).
 */
/*ARGSUSED*/
static void
signalcmd(i, pars)
	int i;
	char *pars;
{
	irc_kill(atoi(pars));
}

/*
 * The famous Lame-O-Metre.
 */
static void
lamecmd(i, pars)
	int i;
	char *pars;
{
	struct iw_msg wm;
	unsigned p;
	struct channel *ch;
	char t[MSGSZ], t2[MSGSZ];
	int j;

	if (pars == NULL || !strlen(pars)) {
		INVSYNTAX
		return;
	}
	p = atoi(pars);
	if (p > 100)
		p = 100;
	if ((ch = cchan()) == NULL) {
		iw_printf(COLI_TEXT, "%sNo top channel\n", ppre);
		return;
	}

	for (j = 0; j < 20; j++)
		if (p/5 > (unsigned)j)
			t[j] = '*';
		else if (p/5 == (unsigned)j)
			t[j] = '>';
		else
			t[j] = '-';
	t[20] = 0;
	sprintf(t2, "Lame-O-Metre: [%s] %d%%", t, p);
	privmsg(ch->ch_name, t2, 1);

	sprintf(t, "%s> %s\n", ch->ch_name, t2);
	wm.iwm_text = t;
	wm.iwm_type = IMT_CMSG | IMT_FMT;
	wm.iwm_chn = ch;
	wm.iwm_chint = COLI_OWNMSG;
	dispose_msg(&wm);
}

/*
 * Create or disable a spam discard list.
 */
/*ARGSUSED*/
static void
spamcmd(i, pars)
	int i;
	char *pars;
{
	buildspam(pars);
	setlog(0);
	iw_printf(COLI_TEXT, "%sNew spam discard list defined\n", ppre);
	setlog(1);
}

/*
 * Send WHOIS request.
 */
/*ARGSUSED*/
static void
whoiscmd(i, pars)
	int i;
	char *pars;
{
	int a;
	char t[80];

	if (!strlen(pars) || !strcmp(pars, "*")) {
		sprintf(t, "No or invalid parameters given for /WHOIS. \
Really send? ");
		a = askyn(t);
		elrefr(1);
		if (a == NO)
			return;	
	}
	dprintf(sock, "WHOIS %s\r\n", pars);
}

/*
 * Send NAMES request.
 */
/*ARGSUSED*/
static void
namescmd(i, pars)
	int i;
	char *pars;
{
	char *first, *rest;
	static char dummy[] = "";
	int a;
	char t[80];

	if (!strlen(pars)) {
		sprintf(t, "No parameters given for /NAMES. Really send? ");
		a = askyn(t);
		elrefr(1);
		if (a == NO)
			return;	
	}

	if ((first = strtok(pars, " \t")) != NULL) {
		GETCHAN(first)
		rest = strtok(NULL, "");
	}
	else
		first = rest = dummy;

	dprintf(sock, "NAMES %s %s\r\n", first, (rest != NULL) ? rest : "");
}

/*
 * Manage DCC connections.
 */
static void
dcccmd(i, pars)
	int i;
	char *pars;
{
	char *mode, *string;
	static char sep[] = " \t";

	mode = strtok(pars, sep);
	string = strtok(NULL, "");
	if (mode == NULL) {
		INVSYNTAX
		return;
	}
	irc_strupr(mode);

	if (!strcmp(mode, "SEND")) {
		char *nickname, *path;

		/*
		 * Initiate a DCC SEND connection for the named file to the
		 * specified nickname.
		 */
		if (string == NULL) {
			INVSYNTAX
			return;
		}
		if (!on_irc) {
			iw_printf(COLI_TEXT, "%sNot connected to a server\n",
				ppre);
			return;
		}
		nickname = strtok(string, sep);
		path = strtok(NULL, sep);
		if (nickname != NULL && path != NULL)
			dcc_isend(nickname, path);
		else
			INVSYNTAX
		return;
	}
	else if (!strcmp(mode, "GET")) {
		char *arg;

		if (string == NULL) {
			INVSYNTAX
			return;
		}
		arg = strtok(string, sep);
		if (atoi(arg) == 0)
			dcc_iget(arg, 0, 0);
		else
			dcc_iget(NULL, atoi(arg), 0);
	}
	else if (!strcmp(mode, "RESUME")) {
		char *arg;

		if (string == NULL) {
			INVSYNTAX
			return;
		}
		arg = strtok(string, sep);
		if (atoi(arg) == 0)
			dcc_resume(arg, 0);
		else
			dcc_resume(NULL, atoi(arg));
	}
	else if (!strcmp(mode, "CHAT")) {
		char *arg;
		int id;

		if (string == NULL) {
			INVSYNTAX
			return;
		}
		if ((arg = strtok(string, sep)) == NULL) {
			INVSYNTAX
			return;
		}
		id = atoi(arg);

		if (id != 0 || dcc_ischat(arg) == 1)
			dcc_irchat(arg, id);
	}
	else if (!strcmp(mode, "CLOSE")) {
		char *arg;

		if (string == NULL) {
			INVSYNTAX
			return;
		}
		if ((arg = strtok(string, sep)) == NULL || !strlen(arg)) {
			INVSYNTAX
			return;
		}
		if (atoi(arg) == 0)
			dcc_close(arg, 0);
		else
			dcc_close(NULL, atoi(arg));
	}
	else if (!strcmp(mode, "LIST")) {
		dcc_print();
	}
	else if (!strcmp(mode, "RENAME")) {
		char *arg, *n;

		if (string == NULL) {
			INVSYNTAX
			return;
		}
		arg = strtok(string, sep);
		n = strtok(NULL, sep);
		if (arg == NULL || !strlen(arg) || n == NULL || !strlen(n)) {
			INVSYNTAX
			return;
		}
		dcc_rename(atoi(arg), n);
	}
	else {
		INVSYNTAX
		return;
	}
}

/*
 * Send a USERHOST query.
 */
/*ARGSUSED*/
static void
uhostcmd(i, pars)
	int i;
	char *pars;
{
	dprintf(sock, "USERHOST %s\r\n", pars);
}

struct ll_entry {
	char	*ll_text;
	LIST_ENTRY(ll_entry) ll_entries;
};

/*
 * Display last private messages and notices.
 */
/*ARGSUSED*/
static void
lastlogcmd(i, pars)
	int i;
	char *pars;
{
	int j = 0, k, l;
	char line[MSGSZ*2+1];
	LIST_HEAD(, ll_entry) llh;
	struct ll_entry *lle, *llf;
	struct iwstatus st;

	if (lastlog == NULL) {
		iw_printf(COLI_TEXT, "%sNo lastlog available\n", ppre);
		return;
	}
	fflush(lastlog);
	rewind(lastlog);

	if (pars != NULL)
		k = atoi(pars);
	if (pars == NULL || k == 0)
		k = 10000;
	iwc_status(&st);

	LIST_INIT(&llh);
	while (!feof(lastlog)) {
		if ((fgets(line, MSGSZ*2, lastlog)) == NULL)
			break;
		j++;
		lle = chkmem(malloc(sizeof (*lle)));
		lle->ll_text = chkmem(Strdup(line));
		LIST_INSERT_HEAD(&llh, lle, ll_entries);
	}
	fseek(lastlog, 0, SEEK_END);
	if (!j) {
		iw_printf(COLI_TEXT, "%sLastlog is empty\n", ppre);
		return;
	}
	setlog(0);

	for (j = 0, l = 0, lle = llh.lh_first; j < k && lle != NULL;
	    lle = lle->ll_entries.le_next, j++)
		if (lle->ll_text != NULL)
			iw_buf_printf(COLI_TEXT, "- %s", lle->ll_text);
	iw_buf_flush();
	setlog(1);
	elrefr(1);

	for (lle = llh.lh_first; lle != NULL; lle = llf) {
		llf = lle->ll_entries.le_next;
		free(lle->ll_text);
		free(lle);
	}
}

/*
 * Changes the user's mode.
 */
static void
umodecmd(i, pars)
	int i;
	char *pars;
{
	if (pars == NULL || !strlen(pars)) {
		INVSYNTAX
		return;
	}
	dprintf(sock, "MODE %s %s\r\n", nick, pars);
}

/*
 * Ignores the specified nickname effectively (forms an ignore mask
 * from the source address).
 */
static void
igncmd(i, pars)
	int i;
	char *pars;
{
	if (pars == NULL || !strlen(pars)) {
		INVSYNTAX
		return;
	}
	setlog(0);
	ignmask(pars);
	setlog(1);
}

/*
 * Remembers a command for redo (if source address has to be fetched
 * first etc.).  Returns 0 if ok and -1 on failure.
 */
int
setredo(c, t)
	char *c;
	enum redotype t;
{
	if (c == NULL || !strlen(c) || redoc != NULL)
		return -1;
	
	redoc = chkmem(Strdup(c));
	redot = t;
#ifdef	DEBUGV
	iw_printf(COLI_TEXT, "DEBUG: remembered for redo: %s\n", c);
#endif
	return 0;
}

/*
 * Redo the command.
 */
void
redo(t)
	enum redotype t;
{
	if (t == redot && redoc != NULL) {
#ifdef	DEBUGV
		iw_printf(COLI_TEXT, "DEBUG: executing redo: %s\n", redoc);
#endif
		cmdline(redoc);
		free(redoc);
		redoc = NULL;
	}
}

/*
 * Send a CTCP ACTION to a user (or channel) privately.
 */
static void
desccmd(i, pars)
	int i;
	char *pars;
{
	char *target, *txt;
	static char sep[] = " \t";
	char t[MSGSZ];

	target = strtok(pars, sep);
	txt = strtok(NULL, "");
	if (target == NULL || txt == NULL) {
		INVSYNTAX
		return;
	}

	if (strlen(txt) > MSGSZ-10)
		pars[MSGSZ-10] = 0;

	iw_printf(COLI_ACTION, "(-> %s) * %s %s\n", target, nick, txt);
	sprintf(t, "\1ACTION %s\1", txt);
	privmsg(target, t, 1);
}

/*
 * Display help on keybindings.
 */
/*ARGSUSED*/
static void
keyscmd(i, pars)
	int i;
	char *pars;
{
	helpcmd(0, "KEYS");
}

/*
 * Change nickname.
 */
/*ARGSUSED*/
static void
nickcmd(i, pars)
	int i;
	char *pars;
{
	if (!strlen(pars)) {
		INVSYNTAX
		return;
	}
	if (sock)
		dprintf(sock, "NICK %s\r\n", pars);
}

void
allow_empty(y)
	int y;
{
	allow_emptycmd = y;
}

/*
 * Add/delete a query-"channel" to the current window.
 */
/*ARGSUSED*/
static void
querycmd(i, pars)
	int i;
	char *pars;
{
	extern struct channel *cha;	/* XXX should use a function in */
					/* main.c to access this	*/
	struct channel *ch;
	char chan[NICKSZ+2], chn[NICKSZ+2], *c;
	char who[MSGSZ];
	static char sep[] = " \t";
	struct iw_msg wm;

	c = strtok(pars, sep);

	if (c == NULL) {
		/* Delete the top query. */

		if ((ch = iw_getchan()) == NULL) {
			iw_printf(COLI_TEXT, "%sNo query in this window\n",
				ppre);
			return;
		}
		if (! (ch->ch_modes & MD_QUERY)) {
			iw_printf(COLI_TEXT, "%sNo query on top of this window \
\n", ppre);
			return;
		}
		iw_delchan(ch);
		delchan(ch);
		set_prompt(NULL);
		iw_draw();
		elrefr(0);
		return;
	}

	chan[0] = '!';
	chan[1] = 0;
	strncpy(chan+1, c, NICKSZ);
	chan[NICKSZ+1] = 0;

	if ((ch = getchanbyname(chan)) != NULL) {
		/*
		 * We are already querying that user.  Move the channel to
		 * the top * of the current window.
		 */
		iw_delchan(ch);
		iw_addchan(ch);
		set_prompt(NULL);
		iw_printf(COLI_TEXT, "%sNow talking to user %s (query)\n",
			ppre, pars);
		iw_draw();
		elrefr(0);
		return;
	}
	else {
		/* Create new channel with query bit set. */

		ch = chkmem(calloc(1, sizeof (struct channel)));
		ch->ch_next = ch->ch_prev = ch->ch_wnext = ch->ch_wprev = NULL;
		ch->ch_logfile = NULL;
		ch->ch_logfname = NULL;
		LIST_INIT(&ch->ch_cachehd);
		ch->ch_modes |= MD_QUERY;
		strcpy(ch->ch_name, chan);
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
		set_prompt(NULL);

		sprintf(who, "%sTalking to %s (query)\n", ppre, ch->ch_name+1);
		wm.iwm_text = who;
		wm.iwm_type = IMT_CMSG | IMT_FMT;
		wm.iwm_chn = ch;
		wm.iwm_chint = COLI_TEXT;
		dispose_msg(&wm);
		iw_draw();
		elrefr(0);
	}
}

/*
 * Split a composed command line into a queue of seperate commands.
 * Does _not_ split if the line begins with a /msg, /m or /notice etc.
 */
static void
split_commands(l)
	char *l;
{
	struct command_entry *ce;
	char one[MSGSZ], *t;
	int quoted = 0;

	if (l != NULL && *l == cmdch)
		if (!irc_strncmp(l+1, "MSG ", 4) ||
		    !irc_strncmp(l+1, "M ", 2) ||
		    !irc_strncmp(l+1, "NOTICE ", 7) ||
		    !irc_strncmp(l+1, "ME ", 3) ||
		    !irc_strncmp(l+1, "DESC ", 5) ||
		    !irc_strncmp(l+1, "TOPIC ", 6)) {
			ce = chkmem(malloc(sizeof *ce));
			ce->c_string = chkmem(Strdup(l));
			TAILQ_INSERT_TAIL(&command_queue, ce, c_entries);
			return;
		}

	for (t = one; *l != 0; l++) {
		switch (*l) {
		case '"':
			quoted ^= 1;
			break;
		case ';':
			if (!quoted) {
				*t = 0;
				t = one;
				ce = chkmem(malloc(sizeof *ce));
				ce->c_string = chkmem(Strdup(one));
				TAILQ_INSERT_TAIL(&command_queue, ce,
						    c_entries);
				break;
			}
			/* else fallthrough */
		default:
			*t++ = *l;
			break;
		}
	}
	if (t > one) {
		*t = 0;
		ce = chkmem(malloc(sizeof *ce));
		ce->c_string = chkmem(Strdup(one));
		TAILQ_INSERT_TAIL(&command_queue, ce, c_entries);
	}
}

/*
 * Manage aliases.
 */
static void
aliascmd(i, pars)
	int i;
	char *pars;
{
	char *aname, *xpan;
	static char sep[] = " \t";
	struct alias_entry *ae;
	unsigned long h;

	if ((aname = strtok(pars, sep)) == NULL) {
		if (alias_list.lh_first == NULL) {
			iw_printf(COLI_TEXT, "%sNo aliases defined\n", ppre);
			return;
		}
		setlog(0);
		iw_printf(COLI_TEXT, "%sAlias list:\n", ppre);
	
		for (ae = alias_list.lh_first; ae != NULL;
		     ae = ae->a_entries.le_next)
			iw_printf(COLI_TEXT, "++ alias \"%s\" -> \"%s\"\n",
				ae->a_name, ae->a_expansion);
		setlog(1);
		return;
	}
	h = elf_hash(irc_strupr(aname));

	/* Delete alias if it exists */

	for (ae = alias_list.lh_first; ae != NULL; ae = ae->a_entries.le_next)
		if (ae->a_hash == h)
			break;
	if (ae != NULL) {
		LIST_REMOVE(ae, a_entries);
		if (ae->a_name)
			free(ae->a_name);
		if (ae->a_expansion)
			free(ae->a_expansion);
		free(ae);
	}

	if ((xpan = strtok(NULL, "\n")) != NULL) {
		/* Add an alias definition */

		if (h == ctbl[i].c_hash) {
			setlog(0);
			iw_printf(COLI_TEXT, "%sYou may not shadow ALIAS \
itself\n", ppre);
			setlog(1);
			return;
		}
		ae = chkmem(calloc(1, sizeof *ae));
		ae->a_hash = h;
		ae->a_name = chkmem(Strdup(aname));
		ae->a_expansion = chkmem(Strdup(xpan));
		LIST_INSERT_HEAD(&alias_list, ae, a_entries);
		setlog(0);
		iw_printf(COLI_TEXT, "%salias \"%s\" memorized\n", ppre, aname);
		setlog(1);
	}
}

/*
 * Expand an alias with variable substitution on l.
 * Returns the original line if not an alias or NULL if the
 * expansion yielded an error.
 */
static char *
expand_alias(l)
	char *l;
{
	char copy[MSGSZ];
	static char *full;
	char *aname, *x;
	struct alias_entry *ae;
	unsigned long h;
	char *args[100];	/* XXX 100 arguments should be enough */
	int num, which, size = 0, i;

	strncpy(copy, l, MSGSZ-1);
	copy[MSGSZ-1] = 0;

	if (full != NULL)
		free(full);
	full = chkmem(malloc(MSGSZ));
	full[0] = '\0';

	if ((aname = strtok(copy, " \t")) == NULL)
		return l;

	/* Get alias */
	h = elf_hash(irc_strupr(aname));

	for (ae = alias_list.lh_first; ae != NULL; ae = ae->a_entries.le_next) {
		if (ae->a_hash == h)
			break;
	}
	if (ae == NULL)
		return l;

	/* Get alias arguments */
	args[0] = l;

	for (num = 1; num < 100; num++) {
		if ((args[num] = strtok(NULL, " \t")) == NULL)
			break;
#ifdef	DEBUGV
		iw_printf(COLI_TEXT, "DEBUG: alias argument %d = %s\n", num,
			args[num]);
#endif
	}

	x = ae->a_expansion;

	/* Expand... */
	while (*x != 0) {
		if (*x == '$' && (isdigit(*(x+1)) || *(x+1) == '*')) {
			/* parse $* */
			if (*(x+1) == '*') {
				for (i = 1; i < num; i++) {
				    if (size+strlen(args[i])+1 > MSGSZ-1)
					goto exceed;
				    strcat(full, args[i]);
				    strcat(full, " ");
				    size += strlen(args[i])+1;
				}
				x += 2;
				continue;
			}
			/* parse $0, $n */
			which = atoi(++x);
			if (which >= num || which < 0) {
				iw_printf(COLI_TEXT, "%sIllegal argument \
referenced (\"$%d\") for alias \"%s\"\n", ppre, which, ae->a_name);
				return NULL;
			}
			if (size + strlen(args[which]) > MSGSZ-1) {
			exceed:
				iw_printf(COLI_TEXT, "%sAlias expansion \
exceeds maximum command line size for alias \"%s\"\n", ppre, ae->a_name);
				return NULL;
			}
			strcat(full, args[which]);
			size += strlen(args[which]);
			while (isdigit(*x))
				x++;
		}
		else if (size < MSGSZ-1) {
			full[size++] = *x++;
			full[size] = 0;
		}
		else
			goto exceed;
	}
#ifdef	DEBUGV
	iw_printf(COLI_TEXT, "DEBUG: expanded command line: \"%s\"\n", full);
#endif
	return full;
}

/*
 * Writes a privmsg to the current channel.
 */
/*ARGSUSED*/
static void
saycmd(i, pars)
	int i;
	char *pars;
{
	privmsg(NULL, pars, 0);
	elhome();
}

/*
 * Channel logfile functions.
 */
/*ARGSUSED*/
static void
logcmd(i, pars)
	int i;
	char *pars;
{
	char *chan, *filename;
	static char sep[] = " \t";

	chan = strtok(pars, sep);
	filename = strtok(NULL, "");

	if (chan == NULL || irc_strcmp(chan, "MSG"))
		logchannel(chan, filename);
	else
		logmessage(filename);
}

/*
 * Manage screen pages.
 */
static void
pagecmd(i, pars)
	int i;
	char *pars;
{
	char *func;
	static char sep[] = " \t";

	if ((func = strtok(pars, sep)) == NULL) {
		INVSYNTAX
		return;
	}

	if (isdigit(*func)) {
		if (ipg_switchto(atoi(func), 0))
			repinsel();
		return;
	}

	if (!irc_strcmp("NEW", func)) {
		ipg_create();
		return;
	}

	if (!irc_strcmp("DEL", func)) {
		ipg_delete();
		return;
	}

	INVSYNTAX
}

/*
 * Show, add or delete client op-on-demand lines.
 * /ood without parameters shows ood-lines, /ood add adds a line,
 * /ood del deletes one.
 */
static void
oodcmd(i, pars)
	int i;
	char *pars;
{
	char *func, *rest;
	static char sep[] = " \t";

	if ((func = strtok(pars, sep)) == NULL) {
		ood_display();
		return;
	}
	rest = strtok(NULL, sep);

	if (!irc_strcmp("ADD", func)) {
		if (rest != NULL) {
			if (ood_add(rest)) {
				setlog(0);
				iw_printf(COLI_TEXT, "%sNew OOD registered\n",
					ppre);
				setlog(1);
			}
			else {
				setlog(0);
				iw_printf(COLI_TEXT, "%sError in OOD syntax\n",
					ppre);
				setlog(1);
			}
			return;
		}
		else
			INVSYNTAX
	}

	if (!irc_strcmp("DEL", func)) {
		if (rest != NULL) {
			if (ood_del(atoi(rest))) {
			    setlog(0);
			    iw_printf(COLI_TEXT, "%sOOD-entry deleted\n", ppre);
			    setlog(1);
			}
			else {
			    setlog(0);
			    iw_printf(COLI_TEXT,
				"%sfailed to delete OOD-entry\n", ppre);
			    setlog(1);
			}
			return;
		}
		else
			INVSYNTAX
	}
}

/*
 * This one's for debugging only.  It simulates a server message.
 */
/*ARGSUSED*/
static void
parsecmd(i, pars)
	int i;
	char *pars;
{
#ifndef	DEBUGV
	setlog(0);
	iw_printf(COLI_TEXT, "%sThe PARSE command is only implemented in debug \
mode\n", ppre);
	setlog(1);
#else
	char line[MSGSZ];

	strncpy(line, pars, MSGSZ-3);
	line[MSGSZ-3] = '\0';
	strcat(line, "\r\n");
	add_to_ilbuf(line);
#endif	/* DEBUGV */
}

/*
 * Starts/stops the URL catcher.
 * /url start <filename> starts catching, /url end stops and closes file.
 */
/*ARGSUSED*/
static void
urlcmd(i, pars)
	int i;
	char *pars;
{
	char *func, *arg;
	static char sep[] = " \t";

	if ((func = strtok(pars, sep)) == NULL) {
		INVSYNTAX
		return;
	}
	irc_strupr(func);

	arg = strtok(NULL, "");

	if (!strcmp("START", func)) {
		urlstart(arg);
		return;
	}

	if (!strcmp("END", func)) {
		urlend();
		return;
	}
	if (!strcmp("FLUSH", func)) {
		urlflush();
		return;
	}
	INVSYNTAX
}

/*ARGSUSED*/
static void
uptimecmd(i, pars)
	int i;
	char *pars;
{
	extern time_t t_uptime, t_connecttime;
#ifdef	HAVE_GETRUSAGE
	struct rusage own, children;
#endif

	setlog(0);

	iw_printf(COLI_TEXT, "%sClient running since %s\n", ppre,
		  ctime(&t_uptime));

	if (t_connecttime > 0)
		iw_printf(COLI_TEXT, "%sConnected to server since %s\n", ppre,
			ctime(&t_connecttime));
	else
		iw_printf(COLI_TEXT, "%sClient is not connected to a \
server.\n", ppre);

#ifdef	HAVE_GETRUSAGE
	if (getrusage(RUSAGE_SELF, &own) < 0 ||
	    getrusage(RUSAGE_CHILDREN, &children) < 0)
		iw_printf(COLI_TEXT, "%sCannot obtain resource usage info: \
%s\n", Strerror(errno));
	else {
		int perc = own.ru_utime.tv_sec * 100 /
			(own.ru_stime.tv_sec > 0 ? own.ru_stime.tv_sec : 1);
		iw_printf(COLI_TEXT, "%stirc master: %d.%03du %d.%03ds \
%d:%d.%d %d.%d%% %d+%dio %dpf+%dw %dsm %dud %dus\n", ppre,
			/* user time */
			own.ru_utime.tv_sec, own.ru_utime.tv_usec / 10,
			/* system time */
			own.ru_stime.tv_sec, own.ru_stime.tv_usec / 10,
			/* total time */
			(own.ru_utime.tv_sec + own.ru_stime.tv_sec) / 3600,
			(own.ru_utime.tv_sec + own.ru_stime.tv_sec) / 60,
			(own.ru_utime.tv_sec + own.ru_stime.tv_sec) % 60,
			/* percentage user vs. system time */
			perc,
			perc / 10,
			/* input/output block operations */
			own.ru_inblock,
			own.ru_oublock,
			/* page faults and swaps */
			own.ru_majflt,
			own.ru_nswap,
			/* integral shared memory size */
			own.ru_ixrss,
			/* integral unshared data */
			own.ru_idrss,
			/* integral unshared stack */
			own.ru_isrss);

		perc = children.ru_utime.tv_sec * 100 /
			(children.ru_stime.tv_sec > 0 ?
			    children.ru_stime.tv_sec : 1);
		iw_printf(COLI_TEXT, "%schild procs: %d.%03du %d.%03ds \
%d:%d.%d %d.%d%% %d+%dio %dpf+%dw %dsm %dud %dus\n", ppre,
			/* user time */
			children.ru_utime.tv_sec, own.ru_utime.tv_usec / 10,
			/* system time */
			children.ru_stime.tv_sec, own.ru_stime.tv_usec / 10,
			/* total time */
			(children.ru_utime.tv_sec + own.ru_stime.tv_sec) / 3600,
			(children.ru_utime.tv_sec + own.ru_stime.tv_sec) / 60,
			(children.ru_utime.tv_sec + own.ru_stime.tv_sec) % 60,
			/* percentage user vs. system time */
			perc,
			perc / 10,
			/* input/output block operations */
			children.ru_inblock,
			children.ru_oublock,
			/* page faults and swaps */
			children.ru_majflt,
			children.ru_nswap,
			/* integral shared memory size */
			children.ru_ixrss,
			/* integral unshared data */
			children.ru_idrss,
			/* integral unshared stack */
			children.ru_isrss);
	}
#else	/* ! HAVE_GETRUSAGE */
	iw_printf(COLI_TEXT, "%sNo resource usage info available\n", ppre);
#endif
	setlog(1);
}

/*
 * Set colour of a colour type.
 */
/*ARGSUSED*/
static void
colourcmd(i, pars)
	int i;
	char *pars;
{
#ifdef	WITH_ANSI_COLOURS
	char *ctype, *fg, *bg;
	static char sep[] = " \t";
#endif

#ifndef	WITH_ANSI_COLOURS
	iw_printf(COLI_TEXT, "%sNo colour support compiled in\n", ppre);
#else
	ctype = strtok(pars, sep);
	fg = strtok(NULL, sep);
	bg = strtok(NULL, sep);

	if (ctype == NULL || fg == NULL || bg == NULL) {
		INVSYNTAX
		return;
	}

	colour_set(ctype, fg, bg);
#endif
}

/*
 * Parse a file with tirc commands, or ~/.tircrc if none specified.
 * XXX not yet implemented.
 */
/*ARGSUSED*/
static void
sourcecmd(i, pars)
	int i;
	char *pars;
{
	iw_printf(COLI_TEXT, "%sThe SOURCE command is not yet implemented\n",
		ppre);
}

/*
 * Set colourization for a certain nickname and those which look similar.
 */
/*ARGSUSED*/
static void
ncolcmd(i, pars)
	int i;
	char *pars;
{
#ifdef	WITH_ANSI_COLOURS
	char *func;
	static char sep[] = " \t";

	if ((func = strtok(pars, sep)) == NULL) {
		INVSYNTAX
		return;
	}
	irc_strupr(func);

	if (!strcmp("ADD", func)) {
		char *pat, *deg, *fgs, *bgs;
		int fgc, bgc;

		if ((pat = strtok(NULL, sep)) == NULL
		    || (deg = strtok(NULL, sep)) == NULL
		    || (fgs = strtok(NULL, sep)) == NULL
		    || (bgs = strtok(NULL, sep)) == NULL) {
			INVSYNTAX
			return;
		}
		if (colour_atoi(fgs, bgs, &fgc, &bgc) == -1) {
		    	INVSYNTAX
			return;
		}
		ncol_register(pat, atoi(deg), fgc, bgc);
		return;
	}

	if (!strcmp("DEL", func)) {
		char *which;

		if ((which = strtok(NULL, sep)) == NULL) {
			INVSYNTAX
			return;
		}
		ncol_remove(atoi(which));
		return;
	}

	if (!strcmp("LIST", func)) {
		ncol_print();
		return;
	}

#ifdef	DEBUGV
	if (!strcmp("MATCH", func)) {
		int fgc, bgc;
		char *n;

		if ((n = strtok(NULL, sep)) == NULL) {
			INVSYNTAX
			return;
		}
		if (!ncol_match(n, &fgc, &bgc)) {
			iw_printf(COLI_TEXT, "DEBUG: no matching pattern \
found for nick %s\n", n);
			return;
		}
		iw_printf(COLI_TEXT, "DEBUG: match found for %s, fgc=%d, bgc=\
%d\n", n, fgc, bgc);
		return;
	}
#endif	/* DEBUGV */
	INVSYNTAX
#else
	iw_printf(COLI_TEXT, "%sColour support is not configured\n", ppre);
#endif	/* ! WITH_ANSI_COLOURS */
}

/*
 * Toggle paste mode.
 */
void
pastemode()
{
	pasting ^= 1;

	setlog(0);
	iw_printf(COLI_TEXT, "%sPaste mode %s\n", ppre, pasting ? "on" : "off");
	setlog(1);
	set_prompt(NULL);
	elrefr(0);
}

