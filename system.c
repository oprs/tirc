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
static char rcsid[] = "$Id: system.c,v 1.14 1998/02/02 03:41:29 mkb Exp $";
#endif

#include "tirc.h"

#ifdef	HAVE_STRING_H
#include <string.h>
#elif defined(HAVE_MEMORY_H)
#include <memory.h>
#endif
#ifdef	HAVE_SIGNAL_H
#include <signal.h>
#endif

#include "compat.h"
#include "tty.h"
#include "colour.h"

struct pmsg_entry {
	char	*pme_target;
	char	*pme_text;
	TAILQ_ENTRY(pmsg_entry) pme_entries;
};

extern int	errno;
extern char	ppre[];

int	syspipe;
int	systempid;
char	*s_target;
TAILQ_HEAD(, pmsg_entry) pmsg_list;

/*
 * Initialize this layer.
 */
void
init_system()
{
	syspipe = 0;
	TAILQ_INIT(&pmsg_list);
}

/*
 * Forks, calls a process in background and attaches its output to a pipe
 * which will be read in doirc() and copied into the current window.
 */
void
irc_system(cmd)
	char *cmd;
{
	int pfd[2];
	char *shell;
	static char sh[] = "/bin/sh";
	char *s_cmd;

	if (cmd == NULL || !strlen(cmd))
		return;
	if (syspipe > 0) {
		iw_printf(COLI_TEXT, "%sYou've already a process running, \
multiple processes are not supported\n", ppre);
		return;
	}

	/* Output is routed to irc? */
	if (!strncmp(cmd, "-msg ", 5)) {
		s_target = chkmem(Strdup(strtok(skipws(cmd+4), " \t")));
		s_cmd = strtok(NULL, "");
		if (s_target == NULL || s_cmd == NULL) {
			iw_printf(COLI_TEXT, "%sNot enough parameters\n", ppre);
			if (s_target != NULL)
				free(s_target);
			return;
		}
	}
	else {
		s_target = NULL;
		s_cmd = cmd;
	}

	if (pipe(pfd) < 0) {
		iw_printf(COLI_WARN, "%s%sCannot create pipe: %s%s\n", TBOLD,
ppre, Strerror(errno), TBOLD);
		free(s_target);
		return;
	}
	if (dg_allocbuffer(pfd[0]) < 0) {
		iw_printf(COLI_WARN, "%s%sdg_allocbuffer failed, closing \
pipe%s\n", TBOLD, ppre, TBOLD);
		close(pfd[0]);
		close(pfd[1]);
		free(s_target);
		return;
	}
	syspipe = pfd[0];

	if ((systempid = fork()) < 0) {
		iw_printf(COLI_WARN, "%s%sCannot fork(): %s%s\n", TBOLD, ppre,
			Strerror(errno), TBOLD);
		free(s_target);
		return;
	}
	else if (!systempid) {
		/* Executing child process. */
		close(pfd[0]);
		dup2(pfd[1], STDOUT_FILENO);
		dup2(pfd[1], STDERR_FILENO);
		close(pfd[1]);
		shell = getenv("SHELL");
		if (shell == NULL)
			shell = sh;
		execl(shell, shell, "-c", s_cmd, NULL); 
		fprintf(stderr, "Cannot execl() %s: %s\n", shell,
			Strerror(errno));
		_exit(-1);
	}

	/* Parent process. */
	close(pfd[1]);
	iw_printf(COLI_TEXT, "%sforked: %d\n", ppre, systempid);
}

/*
 * Send the specified signal to the spawned background process.
 */
void
irc_kill(sig)
	int sig;
{
	if (syspipe > 0) {
		if (kill(systempid, sig) < 0)
			iw_printf(COLI_WARN, "%sKilling process %d with \
signal %d failed: %s\n", ppre, systempid, sig, Strerror(errno));
	}
	else
		iw_printf(COLI_TEXT, "%sNo spawned process\n", ppre);
}

void
system_printline(line)
	char *line;
{
	struct iw_msg msg;

	if (s_target == NULL) {
		msg.iwm_text = line;
		msg.iwm_type = IMT_CTRL;
		msg.iwm_chn = NULL;
		msg.iwm_chint = COLI_TEXT;
		dispose_msg(&msg);
	}
	else {
		/* Store on outgoing privmsgs list */

		struct pmsg_entry *pme = chkmem(malloc(sizeof *pme));

		pme->pme_target = chkmem(Strdup(s_target));
		pme->pme_text = chkmem(Strdup(line));
		TAILQ_INSERT_TAIL(&pmsg_list, pme, pme_entries);
	}
}

/*
 * Return the status of the pmsg queue.
 */
int
system_out_waiting()
{
	return (pmsg_list.tqh_first != NULL);
}

/*
 * Send the next waiting privmsg from the queue if at least 1 second
 * passed since last call and add an additional delay every 10 lines.
 */
void
system_sendpmsg()
{
	struct pmsg_entry *pme = pmsg_list.tqh_first;
	time_t now = time(NULL);
	static time_t last;
	static int numl;

	if (pme != NULL && now > last) {
		privmsg(pme->pme_target, pme->pme_text, 0);
		free(pme->pme_text);
		free(pme->pme_target);
		TAILQ_REMOVE(&pmsg_list, pme, pme_entries);
		free(pme);
		last = now;
		if (++numl > 10) {
			numl = 0;
			last += 3;	/* 3 sec delay */
		}
	}
}

/*
 * Clear the queue.
 */
void
system_dequeue()
{
	struct pmsg_entry *pme, *pme_next;

	for (pme = pmsg_list.tqh_first; pme != NULL; pme = pme_next) {
		pme_next = pme->pme_entries.tqe_next;
		free(pme->pme_target);
		free(pme->pme_text);
		TAILQ_REMOVE(&pmsg_list, pme, pme_entries);
		free(pme);
	}
	TAILQ_INIT(&pmsg_list);
}

