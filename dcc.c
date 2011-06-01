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
 * tirc's DCC is implemented in a different way than ircII does, it
 * uses child processes to serve the DCC connections, which communicate
 * with the master tirc through IPC.  This is much cleaner and gives us
 * better performance, since we allow the kernel to schedule the network
 * i/o and the DCC stuff doesn't affect the user and server i/o in the
 * master process.
 */

#ifndef lint
static char rcsid[] = "$Id: dcc.c,v 1.43 1998/02/02 04:31:49 mkb Exp $";
#endif

#include "tirc.h"

#ifdef	HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef	HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef	HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef	HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#ifdef	HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef	HAVE_SYS_SIGNAL_H
#include <sys/signal.h>
#endif
#ifdef	HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef	HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef	HAVE_STRING_H
#include <string.h>
#elif defined(HAVE_MEMORY_H)
#include <memory.h>
#endif
#ifdef	HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef	HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef	HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef	HAVE_LIBGEN_H
#include <libgen.h>
#endif
#ifdef	HAVE_PWD_H
#include <pwd.h>
#endif
#ifdef	HAVE_CTYPE_H
#include <ctype.h>
#endif

#define	CHLD_WAIT4KILL	{ pause(); _exit(1); }
#define TIMESTAMP	(is_away || check_conf(CONF_STAMP)) ? timestamp() : ""

#include "compat.h"
#include "tty.h"
#include "colour.h"

/* Data shared between parent + child for communicating. */
struct dcc_shared {
	long	dccs_xmitted;	/* size of data transmitted so far */
	unsigned long dccs_address; /* peer IP address */
	/* used with dcc resume */
	long	dccs_fstart;	/* starting offset for transmission */
	int	dccs_port;	/* contains dcc_port */
	long	dccs_size;	/* contains dcc_size */
};

/* Administrative data about a direct client connection. */
struct dcc_entry {
	int	dcc_id;		/* identifier */
	enum { DCC_SEND = 1, DCC_RECV, DCC_SCHAT, DCC_RCHAT }
		dcc_type;	/* connection type */
	enum { DCC_PENDING, DCC_ACTIVE }
		dcc_status;	/* current connection status */
	time_t	dcc_start;	/* xfer starting time */
	char	dcc_nick[NICKSZ+1];	/* peer nickname on offer */
	unsigned long dcc_address;	/* peer IP address */
	int	dcc_port;	/* tcp port for the connection */
	char	*dcc_arg;	/* argument string (filename or description) */
	long	dcc_size;	/* file size for DCC FILE */
	FILE	*dcc_file;	/* file for DCC FILE */
	pid_t	dcc_pid;	/* pid of the servicing process */
	int	dcc_pctrl;	/* pipe for messages of the servicing process */
	int	dcc_pchat;	/* pipe for dcc chat traffic */
	sig_atomic_t dcc_dead;	/* 1 if the process has died but not yet */
				/* removed */
	time_t	dcc_stamp;	/* stamp of incoming request */
	struct dcc_shared *dcc_shared;
	LIST_ENTRY(dcc_entry) dcc_entries;
	TAILQ_HEAD(, dcc_chatline) dcc_chathead; /* Spooled outgoing chat */
};

/* Stores one line of DCC text. */
struct dcc_chatline {
	char	*chat_text;
	TAILQ_ENTRY(dcc_chatline) chat_entries;
};

static int	spipe __P((int []));
static void	dcc_child_signals __P((struct dcc_entry *));
static void	dcc_sendfile __P((struct dcc_entry *, int));
static void	dcc_getfile __P((struct dcc_entry *, int));
static void	dcc_dochat __P((struct dcc_entry *, int));
static void	dcc_age __P((void));
static int	dcc_shmprobe __P((void));

extern char	ppre[], **argv0;
extern int	errno;
extern int	is_away;
extern FILE	*lastlog;
extern char	lastmsg[MSGNAMEHIST][NICKSZ+2];
extern int	lastmsgp;
extern int	on_irc;

static int	last_id;
static LIST_HEAD(, dcc_entry) dcc_list;
static char	dcc_chat_send[] = "initiated";
static char	dcc_chat_got[] = "received request";
static char	dcc_chat_acc[] = "accepted";

/*
 * Initialize this layer.
 */
void
dcc_init()
{
	last_id = 0;
	LIST_INIT(&dcc_list);
	Atexit(dcc_killch);
}

/*
 * Initiate a DCC SEND connection to the given nick for path.
 */
void
dcc_isend(nick, path)
	char *nick, *path;
{
	struct stat sb;
	struct dcc_entry *de;
	int s, dcc_sock;
	struct sockaddr_in sa, local, remote;
	int cfd[2];
	int loc_addrlen, rem_addrlen;
	static char newname1[] = "tirc accepting DCC FILE",
		newname2[] = "tirc serving DCC SEND";

	if (nick == NULL || path == NULL || !strlen(nick) || !strlen(path))
		return;

	if ((path = exptilde(path)) == NULL)
		return;

	/* Check if the file is valid. */
	if (stat(path, &sb) < 0) {
		iw_printf(COLI_WARN, "%sCannot stat %s: %s\n", ppre, path,
			Strerror(errno));
		return;
	}
	if (! S_ISREG(sb.st_mode)) {
		iw_printf(COLI_WARN, "%sNot a regular file: %s\n", ppre, path);
		return;
	}
	if (strstr(path, "/etc/") != NULL ||
	    strstr(path, "/passwd") != NULL ||
	    strstr(path, ".rhosts") != NULL ||
	    strstr(path, "hosts.equiv") != NULL) {
		iw_printf(COLI_TEXT, "%sInsecure filename rejected\n", ppre);
		return;
	}

	/* Verify availability of shared memory resource. */
	if (dcc_shmprobe() == 0)
		return;

	/* Create a new dcc entry. */
	de = chkmem(calloc(1, sizeof *de));
	de->dcc_id = ++last_id;
	de->dcc_file = NULL;
	if (!last_id)
		last_id++;
	de->dcc_type = DCC_SEND;
	de->dcc_status = DCC_PENDING;
	de->dcc_start = 0;
	de->dcc_arg = chkmem(Strdup(path));
	strncpy(de->dcc_nick, nick, 9);
	de->dcc_nick[NICKSZ] = 0;
	de->dcc_shared = chkmem(Shcalloc(sizeof (struct dcc_shared)));
	LIST_INSERT_HEAD(&dcc_list, de, dcc_entries);

	/* Create control stream pipe. */
	if (spipe(cfd) < 0) {
		iw_printf(COLI_WARN, "%sCannot create stream pipe: %s\n", ppre,
			Strerror(errno));
		goto messed_up;
	}

	/* Create a new process to serve the DCC request. */
	if ((de->dcc_pid = fork()) < 0) {
		iw_printf(COLI_WARN, "%sCannot fork: %s\n", ppre,
			Strerror(errno));
		return;
	}
	if (de->dcc_pid > 0) {
		close(cfd[0]);	/* parent uses socket 1 */
		de->dcc_pctrl = cfd[1];
		if (dg_allocbuffer(de->dcc_pctrl) < 0) {
			iw_printf(COLI_WARN, "%sdg_allocbuffer() for dcc_pctrl \
failed\n", ppre);
			kill(de->dcc_pid, 15);
			LIST_REMOVE(de, dcc_entries);
			free(de->dcc_arg);
			Shfree(de->dcc_shared);
			free(de);
		}
		return;
	}

	/* Child process */

	dcc_child_signals(de);
	close(cfd[1]);
	de->dcc_pctrl = cfd[0];

	/* Open file. */
	if ((de->dcc_file = fopen(path, "r")) == NULL) {
		if (dprintf(de->dcc_pctrl, "%sCannot open %s: %s\n", ppre, path,
		    Strerror(errno)) < 0)
			_exit(1);
		goto messed_up;
	}
	fseek(de->dcc_file, 0, SEEK_END);
	de->dcc_shared->dccs_size = de->dcc_size = ftell(de->dcc_file);
	rewind(de->dcc_file);

	/* Create a socket for this connection. */
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		if (dprintf(de->dcc_pctrl, "E%ssocket() failed: %s\n", ppre,
			Strerror(errno)) < 0)
			_exit(1);
	messed_up:
		LIST_REMOVE(de, dcc_entries);
		free(de->dcc_arg);
		Shfree(de->dcc_shared);
		free(de);
		CHLD_WAIT4KILL 
	}
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = INADDR_ANY;
	sa.sin_port = 0;

	if (bind(s, (struct sockaddr *) &sa, sizeof sa) < 0) {
		if (dprintf(de->dcc_pctrl, "E%sbind() failed: %s\n", ppre,
		    Strerror(errno)) < 0)
			_exit(1);
		close(s);
		goto messed_up;
	}
	loc_addrlen = sizeof local;
	getsockname(s, (struct sockaddr *) &local, &loc_addrlen);
	de->dcc_shared->dccs_port = de->dcc_port = ntohs(local.sin_port);
	de->dcc_address = ntohl(get_irc_s_addr());
	listen(s, 1);

	/* Now let the other side know about our generous offer. */
	if (dprintf(de->dcc_pctrl, "I\1DCC SEND %s %lu %d %lu\1\n",
	    basename(path), de->dcc_address, de->dcc_port, de->dcc_size) < 0)
		_exit(1);
	if (dprintf(de->dcc_pctrl, "M%sSent DCC SEND request (%s %lu) to %s \
[%d]\n", ppre, path, de->dcc_size, nick, de->dcc_id) < 0)
		_exit(1);
	*argv0 = newname1;

	rem_addrlen = sizeof remote;
	getsockname(s, (struct sockaddr *) &remote, &rem_addrlen);

	if ((dcc_sock = accept(s, (struct sockaddr *) &remote, &rem_addrlen))
	    < 0) {
		if (dprintf(de->dcc_pctrl, "E%saccept() failed\n", ppre) < 0)
			_exit(1);
		CHLD_WAIT4KILL
	}
	/*
	 * We got a connection, close the accepting socket and use the
	 * new one.
	 */
	close(s);
	*argv0 = newname2;
	de->dcc_shared->dccs_xmitted = 0;
	de->dcc_shared->dccs_address = ntohl(remote.sin_addr.s_addr);

	if (dprintf(de->dcc_pctrl, "S%sDCC SEND connection %d with %s[@%s:%d] \
established (%s)\n", ppre, de->dcc_id, de->dcc_nick,
	    inet_ntoa(remote.sin_addr), de->dcc_port, de->dcc_arg) < 0)
		_exit(1);
	dcc_sendfile(de, dcc_sock);
	CHLD_WAIT4KILL
}

/*
 * Create a full duplex stream pipe in either Berkeley or SVR4 manner.
 */
static int
spipe(fd)
	int fd[];
{
#ifdef	HAVE_SOCKETPAIR
	return (socketpair(AF_UNIX, SOCK_STREAM, 0, fd));
#elif defined(SVR4)
	return (pipe(fd));
#endif
}

static void
dcc_child_signals(de)
	struct dcc_entry *de;
{
	int sig;

	sig = our_signal(SIGHUP, SIG_DFL);
	sig += our_signal(SIGINT, SIG_DFL);
	sig += our_signal(SIGPIPE, SIG_DFL);
	sig += our_signal(SIGTERM, SIG_DFL);
	sig += our_signal(SIGTSTP, SIG_IGN);
	sig += our_signal(SIGCHLD, SIG_DFL);
	sig += our_signal(SIGALRM, SIG_IGN);
	sig += our_signal(SIGWINCH, SIG_IGN);

	if (sig) {
		if (dprintf(de->dcc_pctrl,
		    "E%sResetting signal handlers failed\n", ppre) < 0)
			_exit(1);
		CHLD_WAIT4KILL
	}
}

static void
dcc_sendfile(de, dcc_sock)
	struct dcc_entry *de;
	int dcc_sock;
{
	char *buffer;
	unsigned long bsz, rsz, sent = 0;
	time_t end;

	buffer = chkmem(malloc(DCCBUFSZ));
	de->dcc_start = time(NULL);

	fseek(de->dcc_file, de->dcc_shared->dccs_fstart, SEEK_SET);
	de->dcc_size -= de->dcc_shared->dccs_fstart;

	/*
	 * Files are transferred in packets, packet size is DCCBUFSZ.
	 * We transfer a packet, then wait for the peer to acknowledge the
	 * transfer by sending the size of so far received data as a 4 byte
	 * integer in network byte order.  Packets are not resend since tcp
	 * already does this for us in lower levels.
	 */
	while (de->dcc_size > 0) {
		bsz = min(de->dcc_size, DCCBUFSZ);
		fread(buffer, bsz, 1, de->dcc_file);
		if (send(dcc_sock, buffer, (size_t)bsz, 0) != (int)bsz) {
			if (dprintf(de->dcc_pctrl, "E%sError sending buffer \
to peer for dcc-id %d: %s\n", ppre, de->dcc_id, Strerror(errno)) < 0)
				_exit(1);
			CHLD_WAIT4KILL
		}
		sent += bsz;
		de->dcc_shared->dccs_xmitted = sent;
		de->dcc_size -= bsz;
		/*
		 * DCC is idiocy-stricken.
		 * We have to wait until the peer confirmed all transferred
		 * data so far, even if it confirms smaller hunks in between.
		 */
		do {
		    if (recv(dcc_sock, &rsz, 4, 0) < 4) {
			if (dprintf(de->dcc_pctrl, "E%sDCC connection [%d] to \
%s lost\n", ppre, de->dcc_id, de->dcc_nick) < 0)
				_exit(1);
			CHLD_WAIT4KILL
		    }
		    rsz = ntohl(rsz);
		    if (rsz > sent) {
		    	if (dprintf(de->dcc_pctrl, "E%sDCC connection [%d] to \
%s is broken, aborting\n", ppre, de->dcc_id,
			    de->dcc_nick) < 0)
				_exit(1);
			CHLD_WAIT4KILL
		    }
		} while (rsz != sent);
	}
	end = time(NULL);
	free(buffer);

	if (dprintf(de->dcc_pctrl, "F%sDCC SEND transfer %d (%s) with %s \
finished (avg throughput %lu bytes/sec)\n", ppre, de->dcc_id, de->dcc_arg,
	    de->dcc_nick, sent/(end-de->dcc_start > 0 ? end-de->dcc_start : 1))
	    < 0)
		_exit(1);
}

/*
 * Set the relevant bits for DCC pctrl and pchat pipe descriptors in the
 * descriptor set for select()ing.
 */
int
dcc_fd_set(rfds)
	fd_set *rfds;
{
	struct dcc_entry *de;
	int maxd = 0;

	for (de = dcc_list.lh_first; de != NULL && !de->dcc_dead;
	    de = de->dcc_entries.le_next) {
		if (de->dcc_pctrl)
			FD_SET(de->dcc_pctrl, rfds);
		if (de->dcc_pchat)
			FD_SET(de->dcc_pchat, rfds);
		if (de->dcc_pctrl > maxd)
			maxd = de->dcc_pctrl;
		if (de->dcc_pchat > maxd)
			maxd = de->dcc_pchat;
	}
	return maxd;
}

/*
 * Periodically control dcc connections (parent process).
 */
void
ctldcc(rfds)
	fd_set *rfds;
{
	struct dcc_entry *de, *df;
	int r, cont;
	char line[BUFSZ];
	char t[MSGSZ];
	struct iw_msg wm;

	/*
	 * Delete dead connections.
	 */
	for (de = dcc_list.lh_first; de != NULL && de->dcc_dead;
	    de = de->dcc_entries.le_next) {
		if (de->dcc_pctrl)
			close(de->dcc_pctrl);
		if (de->dcc_pchat)
			close(de->dcc_pctrl);
		if (de->dcc_arg != NULL)
			free(de->dcc_arg);
		if (de->dcc_shared != NULL)
			Shfree(de->dcc_shared);
		LIST_REMOVE(de, dcc_entries);
		free(de);
	}

	for (de = dcc_list.lh_first; de != NULL && !de->dcc_dead; de = df) {
		r = 0;
		cont = 1;
		df = de->dcc_entries.le_next;

		/* 
		 * Receiving messages from control pipe: first character is
		 * control character, E is error message, close connection,
		 * M is status message, just display, F is transfer/connection
		 * finished, S indicates connection established, I sends a
		 * privmsg to the peer nickname.
		 */
		while (cont && de->dcc_pctrl &&
		    FD_ISSET(de->dcc_pctrl, rfds) &&
		    (r = dgets(de->dcc_pctrl, BUFSZ, line, 0)) > 0) {
			switch (*line) {
			case 'I':
				privmsg(de->dcc_nick, line+1, 1);
				break;
			case 'S':
				/* Connected, transfer started */
				de->dcc_status = DCC_ACTIVE;
				iw_printf(COLI_TEXT, "%s\n", line+1);
				elhome();
				break;
			case 'E':
				/* fallthrough */
			case 'F':
				/* Transfer finished */
				close(de->dcc_pctrl);
				if (de->dcc_pchat)
					close(de->dcc_pchat);
				LIST_REMOVE(de, dcc_entries);
				if (de->dcc_file) {
					fclose(de->dcc_file);
					de->dcc_file = NULL;
				}
				kill(de->dcc_pid, 15);
				free(de->dcc_arg);
				Shfree(de->dcc_shared);
				free(de);
				iw_printf(COLI_TEXT, "%s\n", line+1);
				elhome();
				cont = 0;
				break;
			case 'M':
				iw_printf(COLI_TEXT, "%s\n", line+1);
				elhome();
				break;
			default:
				iw_printf(COLI_WARN, "%sGarbage on DCC control \
pipe\n", ppre);
				elhome();
				break;
			}
		}
		if (r < 0) {
			iw_printf(COLI_WARN, "%sError on DCC control pipe \
controlling dcc-id %d: %s\n", ppre, de->dcc_id, Strerror(errno));
			close(de->dcc_pctrl);
			if (de->dcc_pchat)
				close(de->dcc_pchat);
			LIST_REMOVE(de, dcc_entries);
			kill(de->dcc_pid, 15);
			free(de->dcc_arg);
			Shfree(de->dcc_shared);
			free(de);
			iw_printf(COLI_TEXT, "%s\n", line+1);
			elhome();
			cont = 0;
		}

		/*
		 * Process waiting data on chat pipe.
		 */
		while (cont && de->dcc_pchat
		    && FD_ISSET(de->dcc_pchat, rfds)
		    && (r = dgets(de->dcc_pchat, BUFSZ, line, 0)) > 0) {
			char *u = strchr(line, '\r'), *v = strchr(line, '\n');
			struct channel *ch;
			char buf[11];

			if (u != NULL)
				*u = 0;
			if (v != NULL)
				*v = 0;

			/* Check if the user is being queried */
			buf[0] = '=';
			strncpy(buf+1, de->dcc_nick, NICKSZ);
			buf[NICKSZ+1] = 0;
			ch = getquerybyname(buf);

			if (check_conf(CONF_HIMSG) && ch == NULL)
				sprintf(t, "%s=%s%s%s= %s\n", TIMESTAMP,
					tc_bold, de->dcc_nick, tc_noattr,
					line);
			else
				sprintf(t, "%s=%s= %s\n", TIMESTAMP,
					de->dcc_nick, line);
			if (lastlog != NULL)
				fprintf(lastlog, "%s=%s= %s\n", timestamp(),
				    de->dcc_nick, line);

			wm.iwm_text = t;

			if (ch == NULL)
				wm.iwm_type = IMT_PMSG | IMT_FMT;
			else
				wm.iwm_type = IMT_CMSG | IMT_FMT;
			wm.iwm_chint = COLI_DCCMSG;
			wm.iwm_chn = ch;
			dispose_msg(&wm);

			sprintf(t, "=%s", de->dcc_nick);
			strncpy(lastmsg[lastmsgp], t, NICKSZ+1);
			lastmsg[lastmsgp][NICKSZ+1] = 0;
			lastmsgp = (lastmsgp + 1) % MSGNAMEHIST;

			if (ch == NULL)
				add_nickhist(buf);
		}
		if (cont && r < 0) {
			iw_printf(COLI_WARN, "%sGot error on DCC chat pipe \
communicating for dcc-id %d: %s\n", ppre, de->dcc_id, Strerror(errno));
			close(de->dcc_pctrl);
			if (de->dcc_pchat)
				close(de->dcc_pchat);
			LIST_REMOVE(de, dcc_entries);
			kill(de->dcc_pid, 15);
			free(de->dcc_arg);
			Shfree(de->dcc_shared);
			free(de);
			elhome();
			cont = 0;
		}
	}
}

/*
 * Print out pending and active connections.
 */
void
dcc_print()
{
	struct dcc_entry *de;
	struct sockaddr_in sa;

	setlog(0);

	if (dcc_list.lh_first == NULL)
		iw_printf(COLI_TEXT, "%sNo DCC connections\n", ppre);
	else {
		iw_printf(COLI_TEXT, "%sDirect client connections:\n", ppre);
		for (de = dcc_list.lh_first; de != NULL && !de->dcc_dead; 
		    de = de->dcc_entries.le_next) {
			sa.sin_addr.s_addr =
				htonl(de->dcc_shared->dccs_address);
			iw_printf(COLI_TEXT, "++ [%2d]  %-5s %-8s %-9s \
pid=%-5d peer=%s offs=%lu xmit=%lu (%s)\n", de->dcc_id,
				(de->dcc_type == DCC_SEND) ? "SEND" :
					(de->dcc_type == DCC_RECV) ? "GET" :
					"CHAT",
				(de->dcc_dead) ? "dead" :
				(de->dcc_status == DCC_ACTIVE) ? "active" :
				    "pending",
				de->dcc_nick, de->dcc_pid,
				inet_ntoa(sa.sin_addr),
				de->dcc_shared->dccs_fstart,
				de->dcc_shared->dccs_xmitted,
				(de->dcc_arg != NULL) ? de->dcc_arg : "");
		}
	}
	setlog(1);
}

/*
 * Close all connections and kill all DCC child processes.
 */
void
dcc_killch()
{
	struct dcc_entry *de, *df;

	for (de = dcc_list.lh_first; de != NULL && !de->dcc_dead; de = df) {
		if (de->dcc_pid)
			kill(de->dcc_pid, SIGTERM);
		if (de->dcc_pctrl)
			close(de->dcc_pctrl);
		if (de->dcc_pchat)
			close(de->dcc_pchat);
		if (de->dcc_file != NULL)
			fclose(de->dcc_file);
		if (de->dcc_arg != NULL)
			free(de->dcc_arg);
		if (de->dcc_shared != NULL)
			Shfree(de->dcc_shared);
		LIST_REMOVE(de, dcc_entries);
		df = de->dcc_entries.le_next;
		free(de);
	}
}

/*
 * Register an incoming DCC request.
 */
void
dcc_register(sm)
	struct servmsg *sm;
{
	struct dcc_entry *de;
	char nname[NICKSZ+1];
	char mline[MSGSZ], answ[MSGSZ];
	char *part[6], *t;
	struct in_addr ia;

	dcc_age();

	/* Verify availability of shared memory resource. */
	if (dcc_shmprobe() == 0) {
		iw_printf(COLI_WARN, "%sIncoming DCC request discarded due \
to lack of shared memory\n", ppre);
		return;
	}

	strncpy(mline, sm->sm_par[1], MSGSZ-1);
	from_nick(sm, nname);

	part[0] = strtok(mline, " \t");	/* "^ADCC"			*/
	part[1] = strtok(NULL, " \t");	/* "SEND" || "CHAT" || "RESUME"	*/
					/* || "ACCEPT"			*/
	part[2] = strtok(NULL, " \t");	/* filename || "chat"		*/
	part[3] = strtok(NULL, " \t");	/* address || port (resume)	*/
	part[4] = strtok(NULL, " \t");	/* port || filepos"^A" (resume)	*/
	part[5] = strtok(NULL, " \t");	/* size"^A" || NULL (resume)	*/

	if (!strncmp(sm->sm_par[1], "\1DCC SEND", 9)) {
		if (part[0] == NULL || part[1] == NULL ||
		    part[2] == NULL || part[3] == NULL ||
		    part[4] == NULL || part[5] == NULL) {
			iw_printf(COLI_TEXT, "%sBroken DCC SEND request \
from %s\n", ppre, nname);
			return;
		}
		if ((t = strchr(part[5], '\1')) != NULL)
			*t = 0;

		de = chkmem(calloc(1, sizeof *de));
		de->dcc_file = NULL;
		de->dcc_id = ++last_id;
		if (!last_id)
			last_id++;
		de->dcc_type = DCC_RECV;
		de->dcc_status = DCC_PENDING;
		strcpy(de->dcc_nick, nname);
		de->dcc_address = strtoul(part[3], NULL, 10);
		de->dcc_port = atoi(part[4]);
		de->dcc_stamp = time(NULL);
		if ((de->dcc_size = atol(part[5])) == 0) {
			iw_printf(COLI_TEXT, "%sDCC SEND null file size \
from %s\n", ppre, nname);
			free(de);
			return;
		}
		de->dcc_arg = chkmem(Strdup(basename(part[2])));
		de->dcc_shared = chkmem(Shcalloc(sizeof (struct dcc_shared)));
		LIST_INSERT_HEAD(&dcc_list, de, dcc_entries);

		ia.s_addr = htonl(de->dcc_address);
		iw_printf(COLI_TEXT, "%sDCC file send request [%d] from \
%s[@%s:%d]: %s (%lu bytes)\n", ppre, de->dcc_id, nname, inet_ntoa(ia),
			de->dcc_port,
			de->dcc_arg,
			de->dcc_size);
	}

	else if (!strncmp(sm->sm_par[1], "\1DCC RESUME", 11)) {
		int p, o;

		if (part[0] == NULL || part[1] == NULL ||
		    part[2] == NULL || part[3] == NULL || part[4] == NULL) {
			iw_printf(COLI_TEXT, "%sBroken DCC RESUME request \
from %s\n", ppre, nname);
			return;
		}
		if ((t = strchr(part[4], '\1')) != NULL)
			*t = 0;
		p = atoi(part[3]);

		for (de = dcc_list.lh_first; de != NULL && !de->dcc_dead;
		    de = de->dcc_entries.le_next) {
			if (de->dcc_shared->dccs_port == p &&
			    de->dcc_type == DCC_SEND &&
			    de->dcc_status == DCC_PENDING &&
			    !irc_strcmp(de->dcc_nick, nname))
				break;
		}

		if (de == NULL) {
			iw_printf(COLI_TEXT, "%sInvalid DCC RESUME request \
from %s (no matching offer)\n", ppre, nname);
			return;
		}

		/*
		 * Bummer.  We already have the serving child forked.
		 * What shall we do?  Well, let's have a starting offset
		 * in the shared memory.
		 */
		if ((o = atoi(part[4])) < 0 || o > de->dcc_shared->dccs_size) {
			iw_printf(COLI_TEXT, "%sBroken DCC RESUME request \
from %s (invalid file offset)\n", ppre, nname);
			return;
		}

		de->dcc_shared->dccs_fstart = atoi(part[4]);

		iw_printf(COLI_TEXT, "%sGot DCC RESUME for dcc-id %d (%s), \
starting at offset %d, accepting\n", ppre, de->dcc_id, de->dcc_arg,
			de->dcc_shared->dccs_fstart);

		sprintf(answ, "\1DCC ACCEPT %s %s %s\1", part[2], part[3],
			part[4]);
		privmsg(nname, answ, 1);
	}

	else if (!strncmp(sm->sm_par[1], "\1DCC ACCEPT", 11)) {
		int p;

		if (part[0] == NULL || part[1] == NULL ||
		    part[2] == NULL || part[3] == NULL || part[4] == NULL) {
			iw_printf(COLI_TEXT, "%sBroken DCC ACCEPT request \
from %s\n", ppre, nname);
			return;
		}
		if ((t = strchr(part[4], '\1')) != NULL)
			*t = 0;
		p = atoi(part[3]);

		for (de = dcc_list.lh_first; de != NULL && !de->dcc_dead;
		    de = de->dcc_entries.le_next) {
			if (de->dcc_port == p && de->dcc_type == DCC_RECV &&
			    de->dcc_status == DCC_PENDING &&
			    !irc_strcmp(de->dcc_nick, nname))
				break;
		}

		if (de == NULL) {
			iw_printf(COLI_TEXT, "%sInvalid DCC ACCEPT request \
from %s (no matching offer)\n", ppre, nname);
			return;
		}

		iw_printf(COLI_TEXT, "%sGot DCC ACCEPT from %s for dcc-id %d, \
starting DCC GET for %s at position %d\n", ppre, nname, de->dcc_id,
			de->dcc_arg, de->dcc_shared->dccs_fstart);

		dcc_iget(NULL, de->dcc_id, 1);
	}

	else if (!strncmp(sm->sm_par[1], "\1DCC CHAT", 9)) {
		if (part[0] == NULL || part[1] == NULL ||
		    part[2] == NULL || part[3] == NULL ||
		    part[4] == NULL) {
			iw_printf(COLI_TEXT, "%sBroken DCC CHAT request from \
%s\n", ppre, nname);
			return;
		}

		de = chkmem(calloc(1, sizeof *de));
		de->dcc_file = NULL;
		de->dcc_id = ++last_id;
		if (!last_id)
			last_id++;
		de->dcc_type = DCC_RCHAT;
		de->dcc_status = DCC_PENDING;
		strcpy(de->dcc_nick, nname);
		de->dcc_address = strtoul(part[3], NULL, 10);
		de->dcc_port = atoi(part[4]);
		de->dcc_arg = chkmem(Strdup(dcc_chat_got));
		de->dcc_shared = chkmem(Shcalloc(sizeof (struct dcc_shared)));
		de->dcc_stamp = time(NULL);
		LIST_INSERT_HEAD(&dcc_list, de, dcc_entries);
		TAILQ_INIT(&de->dcc_chathead);

		ia.s_addr = htonl(de->dcc_address);
		iw_printf(COLI_TEXT, "%sDCC chat request [%d] from \
%s[@%s:%d]\n", ppre, de->dcc_id, nname, inet_ntoa(ia), de->dcc_port);
	}
}

/*
 * Close or reject a DCC connection.
 */
void
dcc_close(n, id)
	char *n;
	int id;
{
	struct dcc_entry *de;

	for (de = dcc_list.lh_first; de != NULL && !de->dcc_dead;
	    de = de->dcc_entries.le_next) {
		if (n != NULL) {
			if (!irc_strcmp(n, de->dcc_nick))
				break;
		}
		else
			if (id == de->dcc_id)
				break;
	}
	if (de == NULL) {
		iw_printf(COLI_TEXT, "%sNo such DCC connection registered\n",
			ppre);
		return;
	}
	if ((de->dcc_type == DCC_RECV || de->dcc_type == DCC_RCHAT)
	    && de->dcc_status == DCC_PENDING) {
		iw_printf(COLI_TEXT, "%sRejecting DCC request %d from %s\n",
			ppre, de->dcc_id, de->dcc_nick);
		if (on_irc)
			notice(de->dcc_nick, "Your DCC request has been \
rejected.", 1);
	}
	else
		iw_printf(COLI_TEXT, "%sClosing DCC connection %d with %s\n",
			ppre, de->dcc_id, de->dcc_nick);
	if (de->dcc_pctrl)
		close(de->dcc_pctrl);
	if (de->dcc_pchat)
		close(de->dcc_pctrl);
	if (de->dcc_arg != NULL)
		free(de->dcc_arg);
	if (de->dcc_shared != NULL)
		Shfree(de->dcc_shared);
	if (de->dcc_pid)
		kill(de->dcc_pid, SIGTERM);
	dg_freebuffer(de->dcc_pchat);
	LIST_REMOVE(de, dcc_entries);

	free(de);
}

/*
 * Rename a file in a dcc entry.
 */
void
dcc_rename(id, newname)
	int id;
	char *newname;
{
	struct dcc_entry *de;

	for (de = dcc_list.lh_first; de != NULL && !de->dcc_dead; 
	    de = de->dcc_entries.le_next)
		if (id == de->dcc_id)
			break;
	if (de == NULL) {
		iw_printf(COLI_TEXT, "%sNo such DCC connection registered\n",
			ppre);
		return;
	}
	if (de->dcc_type != DCC_RECV || de->dcc_status != DCC_PENDING) {
		iw_printf(COLI_TEXT, "%sCan only rename pending DCC GET\n",
			ppre);
		return;
	}
	if (*newname == '.' || strstr(newname, "/etc/") != NULL || 
	    strstr(newname, "/passwd") != NULL ||
	    strstr(newname, "hosts.equiv") != NULL) {
		iw_printf(COLI_TEXT, "%sInsecure target filename rejected: \
%s\n", ppre, newname);
		return;
	}
	if (de->dcc_arg)
		free(de->dcc_arg);
	if (de->dcc_shared)
		Shfree(de->dcc_shared);
	de->dcc_arg = chkmem(Strdup(newname));
	iw_printf(COLI_TEXT, "%sDCC [%d] target file renamed to %s\n", ppre,
		de->dcc_id,
		de->dcc_arg);
}

/*
 * Get a file offered for DCC.
 */
void
dcc_iget(n, id, resuming)
	char *n;
	int id;
	int resuming;
{
	struct dcc_entry *de;
	int cfd[2];
	struct hostent he;
	struct protoent *pe;
	struct sockaddr_in sa;
	int s, r;
	static char newname[] = "tirc receiving DCC FILE";
	struct stat sbuf;
	unsigned long a;

	for (de = dcc_list.lh_first; de != NULL && !de->dcc_dead; 
	    de = de->dcc_entries.le_next) {
		if (de->dcc_type != DCC_RECV || de->dcc_status != DCC_PENDING)
			continue;
		if (n != NULL) {
			if (!irc_strcmp(n, de->dcc_nick))
				break;
		}
		else
			if (id == de->dcc_id)
				break;
	}
	if (de == NULL) {
		iw_printf(COLI_TEXT, "%sNo such DCC connection registered\n",
			ppre);
		return;
	}
	if (*de->dcc_arg == '.') {
		iw_printf(COLI_TEXT, "%sOffered file starts with a dot.  \
Rename target before getting it (DCC RENAME).\n");
		return;
	}

	r = stat(de->dcc_arg, &sbuf);

	if (!resuming && !r) {
		iw_printf(COLI_TEXT, "%sFile %s already exists.  Rename and \
try again.\n", ppre, de->dcc_arg);
		return;
	}
	if (r == -1 && errno != ENOENT) {
		iw_printf(COLI_WARN, "%sError while attempting to stat() %s: \
%s\n", ppre, de->dcc_arg, Strerror(errno));
		return;
	}

	/* Filename ok, open for writing */

	if (!resuming) {
	    if ((de->dcc_file = fopen(de->dcc_arg, "w")) == NULL) {
		    iw_printf(COLI_WARN, "%sCannot open %s for writing: %s\n",
			ppre, de->dcc_arg, Strerror(errno));
	    }
	}
	else {
	    if ((de->dcc_file = fopen(de->dcc_arg, "a")) == NULL) {
		    iw_printf(COLI_WARN, "%sCannot open %s for appending: %s\n",
				ppre, de->dcc_arg, Strerror(errno));
	    }
	}

	/* Create control stream pipe. */
	if (spipe(cfd) < 0) {
		iw_printf(COLI_WARN, "%sCannot create stream pipe: %s\n", ppre,
			Strerror(errno));
		fclose(de->dcc_file);
		LIST_REMOVE(de, dcc_entries);
		free(de->dcc_arg);
		Shfree(de->dcc_shared);
		free(de);
		return;
	}

	/* Create a new process to serve the DCC request. */
	if ((de->dcc_pid = fork()) < 0) {
		iw_printf(COLI_WARN, "%sCannot fork: %s\n", ppre,
			Strerror(errno));
		return;
	}
	if (de->dcc_pid > 0) {
		close(cfd[0]);	/* parent uses socket 1 */
		de->dcc_pctrl = cfd[1];
		if (dg_allocbuffer(de->dcc_pctrl) < 0) {
			iw_printf(COLI_WARN, "%sdg_allocbuffer() for dcc_pctrl \
failed\n", ppre);
			kill(de->dcc_pid, 15);
			fclose(de->dcc_file);
			de->dcc_file = NULL;
			free(de->dcc_arg);
			Shfree(de->dcc_shared);
			free(de);
			return;
		}
		fclose(de->dcc_file);	/* Parent doesn't need this. */
		de->dcc_file = NULL;
		return;
	}

	/* Child process */

	dcc_child_signals(de);
	close(cfd[1]);
	de->dcc_pctrl = cfd[0];
	
	a = htonl(de->dcc_address);
	he.h_name = (char *)&a;
	he.h_aliases = NULL;
	he.h_length = sizeof a;
	he.h_addr_list = (char **) &he.h_name;

	if ((pe = getprotobyname("tcp")) == NULL) {
	messed_up:
		if (dprintf(de->dcc_pctrl, "E%sDCC GET connection failed: %s\n",
		    ppre, Strerror(errno)) < 0)
			_exit(1);
		fclose(de->dcc_file);
		LIST_REMOVE(de, dcc_entries);
		free(de->dcc_arg);
		Shfree(de->dcc_shared);
		free(de);
		CHLD_WAIT4KILL
	}
	if ((s = socket(AF_INET, SOCK_STREAM, pe->p_proto)) == -1)
		goto messed_up;

	memset(&sa, 0, sizeof sa);
	sa.sin_port = htons(de->dcc_port);
	sa.sin_family = AF_INET;
	memmove(&sa.sin_addr, he.h_addr_list[0], (unsigned)he.h_length);

	if (connect(s, (struct sockaddr *) &sa, sizeof sa) < 0)
		goto messed_up;

	*argv0 = newname;
	dcc_getfile(de, s);

	CHLD_WAIT4KILL
}

static void
dcc_getfile(de, dcc_sock)
	struct dcc_entry *de;
	int dcc_sock;
{
	char *buffer;
	time_t end;
	long brd, received = 0, ack;
	struct sockaddr_in remote;
	int nlen = sizeof (struct sockaddr_in);

	if (getpeername(dcc_sock, (struct sockaddr *) &remote, &nlen) < 0) {
		if (dprintf(de->dcc_pctrl, "E%sError getting peername for \
dcc_id %d: %s\n", ppre, de->dcc_id, Strerror(errno)) < 0)
			_exit(1);
		CHLD_WAIT4KILL
	}

	de->dcc_shared->dccs_xmitted = 0;
	de->dcc_shared->dccs_address = ntohl(remote.sin_addr.s_addr);

	de->dcc_size -= de->dcc_shared->dccs_fstart;

	if (dprintf(de->dcc_pctrl, "S%sDCC GET connection %d with %s[@%s:%d] \
established (%s)\n", ppre, de->dcc_id, de->dcc_nick,
	    inet_ntoa(remote.sin_addr), de->dcc_port, de->dcc_arg) < 0)
		_exit(1);

	buffer = chkmem(malloc(DCCBUFSZ));
	de->dcc_start = time(NULL);

	while (received < de->dcc_size) {
		if ((brd = recv(dcc_sock, buffer, DCCBUFSZ, 0)) == -1) {
			if (dprintf(de->dcc_pctrl, "E%sError receiving from \
socket for dcc_id %d: %s\n", ppre, de->dcc_id, Strerror(errno)) < 0)
				_exit(1);
			CHLD_WAIT4KILL
		}
		received += brd;
		de->dcc_shared->dccs_xmitted = received;
		ack = htonl(received);
		if (send(dcc_sock, &ack, 4, 0) != 4) {
			if (dprintf(de->dcc_pctrl, "E%sError sending to socket \
for dcc-id %d: %s\n", ppre, de->dcc_id, Strerror(errno)) < 0)
				_exit(1);
			CHLD_WAIT4KILL
		}
		if (fwrite(buffer, (unsigned)brd, 1, de->dcc_file) != 1) {
			if (dprintf(de->dcc_pctrl, "E%sError writing to file \
%s for dcc-id %d: %s\n", ppre, de->dcc_arg, de->dcc_id, Strerror(errno)) < 0)
				_exit(1);
			CHLD_WAIT4KILL
		}
	}

	end = time(NULL);
	fclose(de->dcc_file);
	free(buffer);

	if (dprintf(de->dcc_pctrl, "F%sDCC GET transfer %d (%s) with %s \
finished, %lu bytes received (avg throughput %lu bytes/sec)\n", ppre,
	    de->dcc_id, de->dcc_arg, de->dcc_nick, received,
	    received/(end-de->dcc_start > 0 ? end-de->dcc_start : 1)) < 0)
		_exit(1);
}

/*
 * Wait for child processes.
 */
void
dcc_wait_children()
{
	struct dcc_entry *de;
	int status;

	for (de = dcc_list.lh_first; de != NULL && !de->dcc_dead;
	    de = de->dcc_entries.le_next)
		if (de->dcc_pid)
			if (waitpid(de->dcc_pid, &status, WNOHANG) > 0)
				de->dcc_dead = 1;
}

/*
 * Initiate a DCC Chat offer.
 * Returns 1 if a rchat connection is pending, 0 otherwise.
 */
int
dcc_ischat(nick)
	char *nick;
{
	struct dcc_entry *de;
	int s, dcc_sock, e;
	struct sockaddr_in sa, local, remote;
	int cfd[2], chfd[2];
	int loc_addrlen, rem_addrlen;
	static char newname1[] = "tirc accepting DCC CHAT",
		newname2[] = "tirc serving DCC CHAT";

	if (nick == NULL || !strlen(nick))
		return 0;

	for (de = dcc_list.lh_first; de != NULL && !de->dcc_dead; 
	    de = de->dcc_entries.le_next)
		if (!strcmp(nick, de->dcc_nick))
			break;
	if (de != NULL)
		return 1;

	/* Verify availability of shared memory resource. */
	if (dcc_shmprobe() == 0)
		return 0;

	/* Create a new dcc entry. */
	de = chkmem(calloc(1, sizeof *de));
	de->dcc_file = NULL;
	de->dcc_id = ++last_id;
	if (!last_id)
		last_id++;
	de->dcc_type = DCC_SCHAT;
	de->dcc_status = DCC_PENDING;
	de->dcc_start = 0;
	de->dcc_arg = chkmem(Strdup(dcc_chat_send));
	strncpy(de->dcc_nick, nick, 9);
	de->dcc_nick[NICKSZ] = 0;
	de->dcc_shared = chkmem(Shcalloc(sizeof (struct dcc_shared)));
	LIST_INSERT_HEAD(&dcc_list, de, dcc_entries);
	TAILQ_INIT(&de->dcc_chathead);

	/* Create control and chat stream pipe. */
	if ((e = spipe(cfd)) < 0 || spipe(chfd) < 0) {
		iw_printf(COLI_WARN, "%sCannot create stream pipe: %s\n", ppre,
			Strerror(errno));
		if (e > 0)
			close(e);
		LIST_REMOVE(de, dcc_entries);
		free(de->dcc_arg);
		Shfree(de->dcc_shared);
		free(de);
		return 0;
	}

	/* Create a new process to serve the DCC request. */
	if ((de->dcc_pid = fork()) < 0) {
		iw_printf(COLI_WARN, "%sCannot fork: %s\n", ppre,
			Strerror(errno));
		return 0;
	}
	if (de->dcc_pid > 0) {
		close(cfd[0]);	/* parent uses socket 1 */
		close(chfd[0]);	/* same here (bidirectional pipe) */
		de->dcc_pctrl = cfd[1];
		de->dcc_pchat = chfd[1];
		if (dg_allocbuffer(de->dcc_pctrl) < 0 ||
		    dg_allocbuffer(de->dcc_pchat)) {
			iw_printf(COLI_WARN, "%sdg_allocbuffer() failed\n",
				ppre);
			kill(de->dcc_pid, 15);
			LIST_REMOVE(de, dcc_entries);
			free(de->dcc_arg);
			Shfree(de->dcc_shared);
			free(de);
		}
		return 0;
	}

	/* Child process */

	dcc_child_signals(de);
	close(cfd[1]);
	close(chfd[1]);
	de->dcc_pctrl = cfd[0];
	de->dcc_pchat = chfd[0];

	/* Create a socket for this connection. */
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		if (dprintf(de->dcc_pctrl, "E%ssocket() failed: %s\n", ppre,
		    Strerror(errno)) < 0)
			_exit(1);
	messed_up:
		LIST_REMOVE(de, dcc_entries);
		free(de->dcc_arg);
		Shfree(de->dcc_shared);
		free(de);
		CHLD_WAIT4KILL
	}

	if (dg_allocbuffer(de->dcc_pchat)) {
		if (dprintf(de->dcc_pctrl, "E%sdg_allocbuffer() failed\n",
		    ppre) < 0)
			_exit(1);
		goto messed_up;
	}

	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = INADDR_ANY;
	sa.sin_port = 0;

	if (bind(s, (struct sockaddr *) &sa, sizeof sa) < 0) {
		if (dprintf(de->dcc_pctrl, "E%sbind() failed: %s\n", ppre,
		    Strerror(errno)) < 0)
			_exit(1);
		close(s);
		goto messed_up;
	}
	loc_addrlen = sizeof local;
	getsockname(s, (struct sockaddr *) &local, &loc_addrlen);
	de->dcc_port = ntohs(local.sin_port);
	de->dcc_address = ntohl(get_irc_s_addr());
	listen(s, 1);

	/* Let the other side know about it. */
	if (dprintf(de->dcc_pctrl, "I\1DCC CHAT chat %lu %d\1\n",
	    de->dcc_address, de->dcc_port) < 0)
		_exit(1);
	if (dprintf(de->dcc_pctrl, "M%sSent DCC CHAT request to %s [%d]\n",
	    ppre, de->dcc_nick, de->dcc_id) < 0)
		_exit(1);
	*argv0 = newname1;

	rem_addrlen = sizeof remote;
	getsockname(s, (struct sockaddr *) &remote, &rem_addrlen);

	if ((dcc_sock = accept(s, (struct sockaddr *) &remote, &rem_addrlen))
	    < 0) {
		if (dprintf(de->dcc_pctrl, "E%saccept() failed\n", ppre) < 0)
			_exit(1);
		CHLD_WAIT4KILL
	}
	/*
	 * We got a connection, close the accepting socket and use the
	 * new one.
	 */
	close(s);
	if (dg_allocbuffer(dcc_sock) < 0) {
		if (dprintf(de->dcc_pctrl, "E%sdg_allocbuffer() failed\n",
		    ppre) < 0)
			_exit(1);
		CHLD_WAIT4KILL
	}

	*argv0 = newname2;
	de->dcc_shared->dccs_address = ntohl(remote.sin_addr.s_addr);
	if (dprintf(de->dcc_pctrl, "S%sDCC CHAT connection %d with %s[@%s:%d] \
established\n", ppre, de->dcc_id, de->dcc_nick, inet_ntoa(remote.sin_addr),
	    de->dcc_port) < 0)
		_exit(1);
	dcc_dochat(de, dcc_sock);
	CHLD_WAIT4KILL
	return 0;	/*NOTREACHED*/
}

static void
dcc_dochat(de, s)
	struct dcc_entry *de;
	int s;
{
	fd_set rfds, wfds;
	int j;
	char *buffer;
	struct dcc_chatline *cl, *cm;

	buffer = chkmem(malloc(MSGSZ+1));

	for (;;) {
		if (de->dcc_chathead.tqh_first != NULL) {
		    FD_ZERO(&wfds);
		    FD_SET(s, &wfds);
		restartsc0:
		    if (select(s+1, NULL, &wfds, NULL, NULL) < 0) {
			if (errno == EINTR)
				goto restartsc0;
			if (dprintf(de->dcc_pctrl, "E%sdcc_dochat() [%d/%s]: \
select() failed, %s\n", ppre, de->dcc_id, de->dcc_nick, Strerror(errno)) < 0)
				_exit(1);
			CHLD_WAIT4KILL
		    }
		}
		else {
		    FD_ZERO(&rfds);
		    FD_SET(de->dcc_pchat, &rfds);
		    FD_SET(s, &rfds);
		restartsc1:
		    if (select(max(de->dcc_pchat, s)+1, &rfds, NULL, NULL,
		    	NULL) < 0) {
			if (errno == EINTR)
				goto restartsc1;
			if (dprintf(de->dcc_pctrl, "E%sdcc_dochat() [%d/%s]: \
select() failed, %s\n", ppre, de->dcc_id, de->dcc_nick, Strerror(errno)) < 0)
				_exit(1);
			CHLD_WAIT4KILL
		    }
		}

		/*
		 * Write to dcc socket.
		 * Messages are spooled so that the slave client doesn't
		 * block the master if writing to the socket would block.
		 */
		if (FD_ISSET(de->dcc_pchat, &rfds)) {
		    if ((j = dgets(de->dcc_pchat, MSGSZ, buffer, 0)) < 0) {
			if (dprintf(de->dcc_pctrl, "E%sdcc_dochat() [%d/%s] \
read from chat-pipe: %s\n", ppre, de->dcc_id, de->dcc_nick, Strerror(errno))
			< 0)
				_exit(1);
			CHLD_WAIT4KILL
		    }
		    if (j > 0) {
		    	cl = malloc(sizeof *cl);
			cl->chat_text = chkmem(Strdup(buffer));
			TAILQ_INSERT_TAIL(&de->dcc_chathead, cl, chat_entries);
		    }
		}
		if (FD_ISSET(s, &wfds))
		    for (cl = de->dcc_chathead.tqh_first; cl != NULL; cl = cm) {
		    	cm = cl->chat_entries.tqe_next;
			if (dprintf(s, "%s\n", cl->chat_text) < 0) {
			    if (dprintf(de->dcc_pctrl, "F%s%s closed DCC CHAT \
connection %d\n", ppre, de->dcc_nick, de->dcc_id) < 0)
				    _exit(1);
			    CHLD_WAIT4KILL
			}
			de->dcc_shared->dccs_xmitted += strlen(cl->chat_text)+1;
			free(cl->chat_text);
			TAILQ_REMOVE(&de->dcc_chathead, cl, chat_entries);
			free(cl);
		    }

		/* Read from dcc socket */

		if (FD_ISSET(s, &rfds)) {
		    if ((j = dgets(s, MSGSZ, buffer, 0)) < 0) {
			if (j == -2) {
			    if (dprintf(de->dcc_pctrl, "F%s%s closed DCC CHAT \
connection %d\n", ppre, de->dcc_nick, de->dcc_id) < 0)
				    _exit(1);
			}
			else
			    if (dprintf(de->dcc_pctrl, "E%sdcc_dochat() \
[%d/%s] read from DCC socket: %s\n", ppre, de->dcc_id, de->dcc_nick,
				Strerror(errno)) < 0)
				    _exit(1);
			CHLD_WAIT4KILL
		    }
		    if (j > 0) {
			de->dcc_shared->dccs_xmitted += j+1;
			if (write(de->dcc_pchat, buffer, (unsigned)j) < 0
			    || write(de->dcc_pchat, "\r\n", 2) < 0) {
			    if (dprintf(de->dcc_pctrl, "E%sdcc_dochat() \
[%d/%s] write to chat pipe: %s\n", ppre, de->dcc_id, de->dcc_nick,
				Strerror(errno)) < 0)
				    _exit(1);
			    CHLD_WAIT4KILL
			}
		    }
		    else {
		    	if (dprintf(de->dcc_pctrl, "F%s%s closed DCC CHAT \
connection %d\n", ppre, de->dcc_nick, de->dcc_id) < 0)
				_exit(1);
			CHLD_WAIT4KILL
		    }
		}
	}
}

/*
 * Initiate a connection to a client serving DCC CHAT.
 */
void
dcc_irchat(n, id)
	char *n;
	int id;
{
	struct dcc_entry *de;
	int cfd[2], chfd[2];
	struct hostent he;
	struct protoent *pe;
	struct sockaddr_in sa, remote;
	int nlen = sizeof (struct sockaddr_in);
	int s, e;
	static char newname[] = "tirc connected DCC CHAT";
	unsigned long a;

	for (de = dcc_list.lh_first; de != NULL && !de->dcc_dead; 
	    de = de->dcc_entries.le_next) {
		if (de->dcc_type != DCC_RCHAT || de->dcc_status != DCC_PENDING)
			continue;
		if (n != NULL) {
			if (!irc_strcmp(n, de->dcc_nick))
				break;
		}
		else
			if (id == de->dcc_id)
				break;
	}
	if (de == NULL) {
		iw_printf(COLI_TEXT, "%sNo such DCC connection registered\n",
			ppre);
		return;
	}

	/* Create control and chat stream pipe. */
	if ((e = spipe(cfd)) < 0 || spipe(chfd) < 0) {
		iw_printf(COLI_WARN, "%sCannot create stream pipe: %s\n", ppre,
			Strerror(errno));
		fclose(de->dcc_file);
		LIST_REMOVE(de, dcc_entries);
		free(de->dcc_arg);
		Shfree(de->dcc_shared);
		free(de);
		if (e > 0)
			close(e);
		return;
	}

	/* Create a new process to serve the DCC request. */
	if ((de->dcc_pid = fork()) < 0) {
		iw_printf(COLI_WARN, "%sCannot fork: %s\n", ppre,
			Strerror(errno));
		return;
	}
	if (de->dcc_pid > 0) {
		close(cfd[0]);	/* parent uses socket 1 */
		close(chfd[0]);
		de->dcc_pctrl = cfd[1];
		de->dcc_pchat = chfd[1];
		if (dg_allocbuffer(de->dcc_pctrl) < 0 ||
		    dg_allocbuffer(de->dcc_pchat) < 0) {
			iw_printf(COLI_WARN, "%sdg_allocbuffer() failed\n",
				ppre);
			kill(de->dcc_pid, 15);
			free(de->dcc_arg);
			Shfree(de->dcc_shared);
			free(de);
			return;
		}
		if (de->dcc_arg != NULL)
			free(de->dcc_arg);
		de->dcc_arg = chkmem(Strdup(dcc_chat_acc));
		return;
	}

	/* Child process */

	dcc_child_signals(de);
	close(cfd[1]);
	close(chfd[1]);
	de->dcc_pctrl = cfd[0];
	de->dcc_pchat = chfd[0];

	if (dg_allocbuffer(de->dcc_pchat) < 0) {
		if (dprintf(de->dcc_pctrl, "E%sdg_allocbuffer() failed\n",
		    ppre) < 0)
			_exit(1);
		LIST_REMOVE(de, dcc_entries);
		free(de->dcc_arg);
		Shfree(de->dcc_shared);
		free(de);
		CHLD_WAIT4KILL
	}
	
	a = htonl(de->dcc_address);
	he.h_name = (char *)&a;
	he.h_aliases = NULL;
	he.h_length = sizeof a;
	he.h_addr_list = (char **) &he.h_name;

	if ((pe = getprotobyname("tcp")) == NULL) {
	messed_up:
		if (dprintf(de->dcc_pctrl, "E%sDCC CHAT connection failed: \
%s\n", ppre, Strerror(errno)) < 0)
			_exit(1);
		LIST_REMOVE(de, dcc_entries);
		free(de->dcc_arg);
		Shfree(de->dcc_shared);
		free(de);
		CHLD_WAIT4KILL
	}
	if ((s = socket(AF_INET, SOCK_STREAM, pe->p_proto)) == -1)
		goto messed_up;

	memset(&sa, 0, sizeof sa);
	sa.sin_port = htons(de->dcc_port);
	sa.sin_family = AF_INET;
	memmove(&sa.sin_addr, he.h_addr_list[0], (unsigned)he.h_length);

	if (connect(s, (struct sockaddr *) &sa, sizeof sa) < 0)
		goto messed_up;

	if (getpeername(s, (struct sockaddr *) &remote, &nlen) < 0) {
		if (dprintf(de->dcc_pctrl, "E%sError getting peername for \
dcc_id %d: %s\n", ppre, de->dcc_id, Strerror(errno)) < 0)
			_exit(1);
		CHLD_WAIT4KILL
	}

	if (dg_allocbuffer(s) < 0) {
		if (dprintf(de->dcc_pctrl, "E%sdg_allocbuffer() failed\n",
		    ppre) < 0)
			_exit(1);
		LIST_REMOVE(de, dcc_entries);
		free(de->dcc_arg);
		Shfree(de->dcc_shared);
		free(de);
		CHLD_WAIT4KILL
	}

	de->dcc_shared->dccs_address = ntohl(remote.sin_addr.s_addr);
	if (dprintf(de->dcc_pctrl, "S%sDCC CHAT connection %d with %s[@%s:%d] \
established\n", ppre, de->dcc_id, de->dcc_nick,
	    inet_ntoa(remote.sin_addr), de->dcc_port) < 0)
		_exit(1);

	*argv0 = newname;
	dcc_dochat(de, s);

	CHLD_WAIT4KILL
}

/*
 * Send a DCC CHAT message to the specified user.  The first character
 * of the target string is '=' which we must skip.
 * Returns 1 if sending the message has succeeded, 0 otherwise.
 */
int
dcc_chatmsg(target, txt)
	char *target;
	char *txt;
{
	struct dcc_entry *de;

	if (target == NULL || txt == NULL || *target == 0 || *txt == 0)
		return 0;

	for (de = dcc_list.lh_first; de != NULL && !de->dcc_dead; 
	    de = de->dcc_entries.le_next) {
		if ((de->dcc_type != DCC_SCHAT &&
		    de->dcc_type != DCC_RCHAT) ||
		    de->dcc_status != DCC_ACTIVE)
			continue;
		if (!irc_strcmp(target+1, de->dcc_nick))
			break;
	}
	if (de == NULL) {
		iw_printf(COLI_TEXT, "%sNo DCC connection with user %s\n", ppre,
			target+1);
		return 0;
	}

	if (dprintf(de->dcc_pchat, "%s\n", txt) <= 0) {
		iw_printf(COLI_WARN, "%sdcc_chatmsg() [%d/%s] cannot send to \
chat pipe: %s\n", ppre, de->dcc_id, de->dcc_nick, Strerror(errno));
		return 0;
	}
	return 1;
}

/*
 * Updates the list of DCC connections appropriately if peer's status
 * has changed.
 */
void
dcc_update(sm)
	struct servmsg *sm;
{
	struct dcc_entry *de, *df;
	char who[NICKSZ+1];
	int what;

	if (!strcmp(sm->sm_cmd, "QUIT")) {
		from_nick(sm, who);
		what = 1;
	}
	else if (!strcmp(sm->sm_cmd, "NICK")) {
		from_nick(sm, who);
		what = 2;
	}
	else if (!strcmp(sm->sm_cmd, "KILL")) {
		strncpy(who, sm->sm_par[0], NICKSZ);
		who[NICKSZ] = 0;
		what = 1;
	}
	else
		return;

	for (de = dcc_list.lh_first; de != NULL && !de->dcc_dead; de = df) {
		df = de->dcc_entries.le_next;

		if (!irc_strcmp(who, de->dcc_nick))
		    if (what == 1 && de->dcc_status == DCC_PENDING)
			dcc_close(NULL, de->dcc_id);
		    else if (what == 2) {
			strncpy(de->dcc_nick, sm->sm_par[0], NICKSZ);
			de->dcc_nick[NICKSZ] = 0;
		    }
	}
}

/*
 * Expand the first '~' character to absolute home directory, like csh.
 * Returns a pointer to the expanded path in a seperate buffer, or a
 * copy of the original path, if nothing is to be expanded.
 * Returns NULL on error (if expansion was possible but failed).
 *
 * If the tilde is preceeded by a space character, expansion is done.
 * If the tilde is followed by a '/', or stands alone, the user's home
 * directory is substituted.
 * Else, the characters up to the next tilde or space character are parsed as
 * a user name and that user's home directory is substituted.
 */
char *
exptilde(path)
	char *path;
{
	static char *newpath;
	char *t, *u, *lname, *hdpath;
	struct passwd *pwe;
	static char ef[] = "%sTilde expansion failed\n";

	if (newpath != NULL)
		free(newpath);
	newpath = NULL;

	if ((t = strchr(path, '~')) == NULL || (t != path
	    && !isspace(*(t-1)))) {
		newpath = chkmem(Strdup(path));
		return newpath;
	}
	if (*(t+1) == '/' || isspace(*(t+1)) || *(t+1) == '\0') {
		if ((lname = getenv("USER")) != NULL && strlen(lname) > 0
		    && (pwe = getpwnam(lname)) != NULL
		    && pwe->pw_dir != NULL && strlen(pwe->pw_dir) != 0)
			hdpath = pwe->pw_dir;
		else {
			if ((hdpath = getenv("HOME")) == NULL
			    || strlen(hdpath) == 0) {
				iw_printf(COLI_TEXT, ef, ppre);
				return NULL;
			}
		}
		u = t+1;
	}
	else {
		for (u = t; *u != '\0' && !isspace(*u) && *u != '/'; u++)
			;
		if (--u == t) {
			newpath = chkmem(Strdup(path));
			return newpath;
		}
		lname = chkmem(malloc((unsigned)(1+u-t)));
		memmove(lname, t+1, (unsigned)(u-t));
		lname[u-t] = '\0';
		if ((pwe = getpwnam(lname)) == NULL || pwe->pw_dir == NULL
		    || strlen(pwe->pw_dir) == 0) {
			iw_printf(COLI_TEXT, ef, ppre);
			free(lname);
			return NULL;
		}
		free(lname);
		u++;
		hdpath = pwe->pw_dir;
	}
	newpath = chkmem(malloc(strlen(path) + strlen(hdpath)));
	memmove(newpath, path, (unsigned)(t-path));
	newpath[t-path] = '\0';
	strcat(newpath, hdpath);
	strcat(newpath, u);
#ifdef	DEBUGV
	iw_printf(COLI_TEXT, "DEBUG: tilde-expansion: path=\"%s\", \
newpath=\"%s\"\n", path, newpath);
#endif
	return newpath;
}

/*
 * Send a DCC RESUME request to the offering party.
 */
void
dcc_resume(n, id)
	char *n;
	int id;
{
	struct dcc_entry *de;
	struct stat sbuf;
	int r;
	char answ[MSGSZ];

	for (de = dcc_list.lh_first; de != NULL && !de->dcc_dead; 
	    de = de->dcc_entries.le_next) {
		if (de->dcc_type != DCC_RECV || de->dcc_status != DCC_PENDING)
			continue;
		if (n != NULL) {
			if (!irc_strcmp(n, de->dcc_nick))
				break;
		}
		else
			if (id == de->dcc_id)
				break;
	}
	if (de == NULL) {
		iw_printf(COLI_TEXT, "%sNo such DCC connection registered\n",
			ppre);
		return;
	}

	r = stat(de->dcc_arg, &sbuf);

	if (r == -1 && errno != ENOENT) {
		iw_printf(COLI_WARN, "%sError while attempting to stat() %s: \
%s\n", ppre, de->dcc_arg, Strerror(errno));
		return;
	}
	if (r != 0) {
		iw_printf(COLI_TEXT, "%sFile does not exist -- use DCC GET to \
retrieve non-existent files instead of DCC RESUME\n", ppre);
		return;
	}

	de->dcc_shared->dccs_fstart = sbuf.st_size;

	sprintf(answ, "\1DCC RESUME %s %d %ld\1", de->dcc_arg, de->dcc_port,
		(long)sbuf.st_size);
	privmsg(de->dcc_nick, answ, 1);
}

/*
 * Look for DCC entries that need to get expired.
 */
static void
dcc_age()
{
	struct dcc_entry *de;
	time_t now = time(NULL);

	for (de = dcc_list.lh_first; de != NULL && !de->dcc_dead
	    && (de->dcc_type == DCC_RECV || de->dcc_type == DCC_RCHAT)
	    && de->dcc_status == DCC_PENDING; de = de->dcc_entries.le_next)
		if (now > de->dcc_stamp + DCCAGE) {
			de->dcc_dead = 1;
			iw_printf(COLI_TEXT, "%sPending incoming DCC \
connection %d (%s/%s) expired and deleted.\n", ppre, de->dcc_id,
			    (de->dcc_type == DCC_RECV) ? de->dcc_arg : "<chat>",
			    de->dcc_nick);
		}
}

/*
 * Probe if enough shared memory is left for a DCC operation.
 * Returns 1 if ok, 0 if not.
 */
static int
dcc_shmprobe()
{
	/*
	 * Typically one dcc_shared structure is instantiated in shared
	 * memory.
	 */
	VOIDPTR probe = Shmalloc(sizeof(struct dcc_shared));

	if (probe != NULL) {
		Shfree(probe);
		return 1;
	}
	iw_printf(COLI_WARN, "%sWARNING: dcc_shmprobe: out of shared memory, \
aborting operation\n", ppre);
	return 0;
}

