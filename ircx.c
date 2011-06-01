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
static char rcsid[] = "$Id: ircx.c,v 1.44 1998/02/16 05:29:48 mkb Exp $";
#endif

#include "tirc.h"

#ifdef	HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef	HAVE_STRING_H
#include <string.h>
#elif defined(HAVE_MEMORY_H)
#include <memory.h>
#endif
#ifdef	HAVE_REGEX_H
#include <regex.h>
#elif defined(HAVE_LIBGEN_H)
#include <libgen.h>
#endif
#ifdef	HAVE_CTYPE_H
#include <ctype.h>
#endif

#include "compat.h"
#include "tty.h"
#include "colour.h"

#define TIMESTAMP	(is_away || check_conf(CONF_STAMP)) ? timestamp() : ""

static struct cache_user *addnametocache __P((char *, struct channel *));
static void	delnamefromcache __P((char *, struct channel *));
static void	done_clogfn __P((char *));
static void	done_mlogfn __P((char *));
#ifdef	HAVE_REGCOMP
static int	matchre_ood_uhost __P((char *, regex_t *));
#elif defined(HAVE_REGCMP)
static int	matchre_ood_uhost __P((char *, char *));
#endif

struct i_entry {
	struct i_entry *i_next;
	char	*i_spec;
#ifdef	HAVE_REGCOMP
	regex_t	i_re;
#elif defined(HAVE_REGCMP)
	char	*i_re;
#endif
	int	i_expire;
};

struct ood_entry {
	char	*ood_uhost;
#ifdef	HAVE_REGCOMP
	regex_t	ood_uhostre;
#elif defined(HAVE_REGCMP)
	char	*ood_uhostre;
#endif
	char	*ood_pass;
	char	*ood_chan;
	LIST_ENTRY(ood_entry) ood_entries;
};

extern struct channel *cha;
extern char	ppre[], *our_address;
extern int	sock;
extern void	(*othercmd) __P((char *));
extern int	errno;
extern int	is_away;

struct i_entry	*i_first;

char		*spam[16];
char		*spamtext;
int		swords;
FILE		*lastlog;
FILE		*msglog;

static struct channel *saved;
static char	llogfn[NAME_MAX], pid[16];
static char	*msglogfn;

static struct channel *logch;	/* required for done_clogfn() */

static LIST_HEAD(, ood_entry) ood_list;	/* op on demand list */

/*
 * Add a source to the ignore-list.  Server messages that match against
 * an entry in the ignore list are discarded and not processed.
 * An entry looks is the full source specification (nick!login@some.domain)
 * with the wildcards * and ?.  The specification is translated into the
 * regexp equivalent and then compiled with regcomp().
 * If the minutes to expire `exp' parameter is 0, the ban does not expire.
 */
void
add_ignore(what, exp)
	char *what;
	int exp;
{
	int pos = 0;
	struct i_entry *ie, *ia;
	char *istring;

	irc_strupr(what);
	ie = chkmem(calloc(1, sizeof (*ie)));
	ie->i_next = NULL;
	ie->i_spec = chkmem(Strdup(what));
	ie->i_expire = exp;

	if ((istring = globtore(what)) == NULL)
		return;

#ifdef	DEBUGV
	iw_printf(COLI_TEXT, "DEBUG: add_ignore(): istring == \"%s\"\n",
		istring);
#endif

#ifdef	HAVE_REGCOMP
	if ((regcomp(&ie->i_re, istring, REG_EXTENDED | REG_NOSUB)) != 0) {
		iw_printf(COLI_TEXT, "%sAdding to ignore-list failed \
(regcomp() failed)\n", ppre);
		free(ie->i_spec);
		free(ie);
		return;
	}
#elif defined(HAVE_REGCMP)
	if ((ie->i_re = regcmp(istring, NULL)) == NULL) {
		iw_printf(COLI_TEXT, "%sAdding to ignore-list failed \
(regcmp() failed)\n", ppre);
		free(ie->i_spec);
		free(ie);
		return;
	}
#endif

	/* 
	 * The ignore list is a single-linked list.  Add the new item at
	 * the end.
	 */
	if (i_first == NULL)
		i_first = ie;
	else {
		for (ia = i_first; ia->i_next != NULL; ia = ia->i_next, pos++)
			;
		ia->i_next = ie;
		pos++;
	}

	iw_printf(COLI_TEXT, "%s%s added to ignore-list [%d] \
(expire: %d min)\n", ppre, ie->i_spec, pos, ie->i_expire);
	elhome();
}

/*
 * Remove the nth item from the ignore-list, where n == which.  
 */
void
del_ignore(which)
	int which;
{
	struct i_entry *ia, *ib;
	int i;
	
	if (i_first == NULL) {
		iw_printf(COLI_TEXT, "%sIgnore-list is empty\n", ppre);
		return;
	}
		
	if (!which) {
		ib = i_first;
		i_first = i_first->i_next;
	}
	else {
		for (i = 1, ia = i_first; i < which && ia != NULL; i++,
		    ia = ia->i_next)
			;
		if (i != which || ia->i_next == NULL) {
			iw_printf(COLI_TEXT, "%sNo such item on ignore-list: \
[%d]\n", ppre, which);
			return;
		}
		ib = ia->i_next;
		ia->i_next = ib->i_next;
	}

#ifdef	HAVE_REGFREE
	regfree(&ib->i_re);
#elif defined(HAVE_REGCMP)
	free(ib->i_re);
#endif
	iw_printf(COLI_TEXT, "%s%s deleted from ignore-list\n", ppre,
		ib->i_spec);
	elhome();
	free(ib->i_spec);
	free(ib);
}

/*
 * Expire ignores.  This should be called once per minute.
 */
void
exp_ignore()
{
	struct i_entry *ia, *ib;
	int i;

	for (ia = i_first, i = 0; ia != NULL; ia = ib, i++) {
		ib = ia->i_next;
		if (ia->i_expire && !--ia->i_expire)
			del_ignore(i--);
	}
}

/*
 * Print the ignore-list.  Each entry has a number assigned to it which
 * is used when you remove something from the list.
 */
void
list_ignore()
{
	struct i_entry *ia;
	int pos;

	if (i_first == NULL) {
		iw_printf(COLI_TEXT, "%sIgnore-list is empty\n", ppre);
	}
	else {
		iw_printf(COLI_TEXT, "%sIgnore-list:\n", ppre);
		for (ia = i_first, pos = 0; ia != NULL; ia = ia->i_next, pos++)
			iw_printf(COLI_TEXT, "++ [%2d]  %s  (exp: %d)\n", pos,
				ia->i_spec, ia->i_expire);
	}
}

/*
 * Check if the server message comes from a person we have on our
 * ignore-list.  Ignore-queries on ourselves are ignored. :*)
 * The match is made with the regexec() library call.
 */
int
check_ignore(sm)
	struct servmsg *sm;
{
	char string[MSGSZ+1];
	struct i_entry *ia;
	char *l;

	if (sm->sm_prefix == NULL)
		return 0;

	strcpy(string, sm->sm_prefix);
	if ((l = strchr(string, '!')) == NULL)
		return 0;
	if (our_address == NULL || !strcmp(l+1, our_address))
		return 0;
	irc_strupr(string);

	for (ia = i_first; ia != NULL; ia = ia->i_next)
#ifdef	HAVE_REGEXEC
		if (!regexec(&ia->i_re, string, 0, NULL, 0))
			return 1;
#elif defined(HAVE_REGEX)
		if (regex(ia->i_re, string, NULL) != NULL)
			return 1;
#endif
	return 0;
}

/*
 * Build internal channel user cache from the result of a NAMES command.
 */
void
cache_names(sm, ch)
	struct servmsg *sm;
	struct channel *ch;
{
	char p[MSGSZ];
	char *t;

	if (sm->sm_par[3] == NULL)	/* empty. */
		return;
	strcpy(p, sm->sm_par[3]);

	/* Add to list from the server string. */
	t = strtok(p, " \t");
	addnametocache(t, ch);

	while ((t = strtok(NULL, " \t")) != NULL)
		addnametocache(t, ch);
}

static struct cache_user *
addnametocache(t, ch)
	char *t;
	struct channel *ch;
{
	struct cache_user *cu;
	int mode = 0;
	char upnick[NICKSZ+1];

	if (t == NULL || ch == NULL)
		return NULL;
	
	switch(*t) {
	case '@':
		mode = MD_O;
		t++;
		break;
	case '+':
		mode = MD_V;
		t++;
		break;
	}

	if (getfromucache(t, ch, NULL, 0) != NULL)
		return NULL;

	cu = chkmem(calloc(sizeof (*cu), sizeof (char)));
	cu->cu_source = NULL;
	strncpy(cu->cu_nick, t, NICKSZ);
	cu->cu_nick[NICKSZ] = 0;
	strncpy(upnick, t, NICKSZ);
	upnick[NICKSZ] = 0;
	cu->cu_hash = elf_hash(irc_strupr(upnick));
	cu->cu_mode = mode;
	LIST_INSERT_HEAD(&ch->ch_cachehd, cu, cu_entries);

	return cu;
}

static void
delnamefromcache(t, ch)
	char *t;
	struct channel *ch;
{
	struct cache_user *cu, *cun;
	unsigned long h;
	char tt[NICKSZ+1];

	if (t == NULL || ch == NULL)
		return;
	strcpy(tt, t);
	h = elf_hash(irc_strupr(t));
	
	for (cu = ch->ch_cachehd.lh_first; cu != NULL; cu = cun) {
		cun = cu->cu_entries.le_next;

		if (cu->cu_hash == h) {
			LIST_REMOVE(cu, cu_entries);
			if (cu->cu_source != NULL)
				free(cu->cu_source);
			free(cu);
		}
	}
}

/*
 * List the names in the cache.
 */
void
listcucache(ch)
	struct channel *ch;
{
	struct cache_user *cu;
	char t[BUFSZ], t2[BUFSZ];
	int len = 0;

	t[0] = 0;

	for (cu = ch->ch_cachehd.lh_first; cu != NULL;
	    cu = cu->cu_entries.le_next) {
#ifdef	SPRINTF_RETURNS_CHARP
		sprintf(t2, "    0x%-8lx: %9s(%s%s), %s\n",
			cu->cu_hash,
			cu->cu_nick,
			(cu->cu_mode & MD_O) ? "+o" : "",
			(cu->cu_mode & MD_V) ? "+v" : "",
			(cu->cu_source != NULL) ? cu->cu_source : "");
		len += strlen(t2);
#else
		len += sprintf(t2, "    0x%-8lx: %9s(%s%s), %s\n",
			cu->cu_hash,
			cu->cu_nick,
			(cu->cu_mode & MD_O) ? "+o" : "",
			(cu->cu_mode & MD_V) ? "+v" : "",
			(cu->cu_source != NULL) ? cu->cu_source : "");
#endif
		/*
		 * Note: must be smaller than BUFSZ-(length of format string
		 * + length of ppre)].  -100 is an arbitrary sufficient value.
		 */
		if (len >= BUFSZ-100) {
			iw_printf(COLI_TEXT, "%sUser cache for %s:\n%s", ppre,
				ch->ch_name, t);
			len = 0;
			t[0] = 0;
		}
		strcat(t, t2);
	}
	iw_printf(COLI_TEXT, "%sUser cache for %s:\n%s", ppre, ch->ch_name, t);
}

/*
 * Check a server message against the cache and add/remove or change the
 * nickname respectively.
 */
void
update_cucache(sm)
	struct servmsg *sm;
{
	struct channel *ch;
	struct cache_user *cu;
	char who[MSGSZ], t[MSGSZ], *s;
	int mode = 0;

	if (!strcmp(sm->sm_cmd, "JOIN")) {
		strncpy(t, sm->sm_par[0], MSGSZ-1);
		t[MSGSZ-1] = 0;
		/* Take care of ircd 2.9 implicit mode change. */
		if ((s = strchr(t, '')) != NULL) {
			*s++ = 0;
			if (*s == 'o') mode |= MD_O;
			if (*s++ == 'v') mode |= MD_V;
			if (*s == 'o') mode |= MD_O;
			if (*s == 'v') mode |= MD_V;
		}
		ch = getchanbyname(t);

		if (ch != NULL) {
			from_nick(sm, who);
			if ((cu = addnametocache(who, ch)) != NULL) {
				cu->cu_source = chkmem(Strdup(sm->sm_prefix));
				cu->cu_mode = mode;
			}
		}
		return;
	}

	if (!strcmp(sm->sm_cmd, "PART")) {
		ch = getchanbyname(sm->sm_par[0]);
		if (ch != NULL) {
			from_nick(sm, who);
			delnamefromcache(who, ch);
		}
		return;
	}

	if (!strcmp(sm->sm_cmd, "KICK")) {
		strcpy(who, sm->sm_par[1]);
		ch = getchanbyname(sm->sm_par[0]);
		strcpy(who, sm->sm_par[1]);
		if (ch != NULL)
			delnamefromcache(who, ch);
		return;
	}

	if (!strcmp(sm->sm_cmd, "QUIT")) {
		from_nick(sm, who);
		if (getfromucache(who, cha, &ch, 1) != NULL)
			do
				delnamefromcache(who, ch);		
			while (getfromucache(who, NULL, &ch, 1) != NULL);
		return;
	}

	if (!strcmp(sm->sm_cmd, "KILL")) {
		strcpy(who, sm->sm_par[0]);
		if (getfromucache(who, cha, &ch, 1) != NULL)
			do
				delnamefromcache(who, ch);		
			while (getfromucache(who, NULL, &ch, 1) != NULL);
		return;
	}

	if (!strcmp(sm->sm_cmd, "NICK")) {
		char tn[NICKSZ+1];

		from_nick(sm, who);
		if ((cu = getfromucache(who, cha, NULL, 1)) != NULL)
			do {
				char *x;

				strncpy(cu->cu_nick, sm->sm_par[0], NICKSZ);
				cu->cu_nick[NICKSZ] = 0;
				strcpy(tn, cu->cu_nick);
				cu->cu_hash = elf_hash(irc_strupr(tn));
				if (cu->cu_source != NULL) {
					free(cu->cu_source);
					cu->cu_source = NULL;
				}
				if ((x = strchr(sm->sm_prefix, '!')) != NULL) {
					strcpy(t, sm->sm_par[0]);
					strcat(t, x);
					cu->cu_source = chkmem(Strdup(t));
				}
			} while ((cu = getfromucache(who, NULL, NULL, 1))
				!= NULL);
		return;
	}
}

/*
 * Get the next matching cache_user entry or NULL if nothing more to 
 * be found.  If `found' is not NULL and the search was positive, a
 * pointer to the channel structure is stored in where `chan' points
 * to.  If first is NULL, the last found is being used for the search.
 * If `iterate' is 0, only the specified or saved channel is searched.
 */
struct cache_user *
getfromucache(n, first, chan, iterate)
	char *n;
	struct channel *first, **chan;
	int iterate;
{
	struct channel *c;
	struct cache_user *cu;
	unsigned long h;
	char t[NICKSZ+1];

	if (first == NULL)
		first = saved;
	strncpy(t, n, NICKSZ);
	t[NICKSZ] = 0;
	h = elf_hash(irc_strupr(t));

	for (c = first; c != NULL; c = c->ch_next) {
		for (cu = c->ch_cachehd.lh_first; cu != NULL;
		    cu = cu->cu_entries.le_next)
			if (cu->cu_hash == h) {
				if (chan != NULL)
					*chan = c;
				saved = c->ch_next;
				return cu;
			}
		if (!iterate)
			break;
	}
	return NULL;
}

/*
 * Change the mode (+o, +v) of the named cache_user entry in the specified
 * channel.
 */
void
changecacheumode(nick, ch, mode, what)
	char *nick;
	struct channel *ch;
	int mode, what;
{
	struct cache_user *cu;

	if ((cu = getfromucache(nick, ch, NULL, 0)) != NULL)
		if (what)
			cu->cu_mode |= mode;
		else
			cu->cu_mode &= ~mode;
}

/*
 * Add the given `source' description in the servmsg to all cached nicknames
 * that match `n'.
 */
void
addsrctocache(n, source)
	char *n, *source;
{
	struct cache_user *cu;

	if ((cu = getfromucache(n, cha, NULL, 1)) != NULL)
		do {
			if (cu->cu_source != NULL)
				break;
			cu->cu_source = chkmem(Strdup(source));
		} while ((cu = getfromucache(n, NULL, NULL, 1)) != NULL);
}

/*
 * Clear user cache for the given channel.
 */
void
clrucache(ch)
	struct channel *ch;
{
	struct cache_user *cu, *cun;

	for (cu = ch->ch_cachehd.lh_first; cu != NULL; cu = cun) {
		cun = cu->cu_entries.le_next;
		LIST_REMOVE(cu, cu_entries);
		if (cu->cu_source != NULL)
			free(cu->cu_source);
		free(cu);
	}
	saved = NULL;
}

/*
 * Construct a (hopefully) well working ban from the source address of the
 * specified nickname for the given channel.  Returns NULL and prints
 * an error message if it couldn't be done (for example the source address
 * was not in the channel's user cache).  The starting address of the "real"
 * banmask is store in pure.  If the command has to be tried again because
 * the source address wasn't cached, needredo will be set to 1.
 */
char *
buildban(n, ch, pure, needredo)
	char *n;
	struct channel *ch;
	char **pure;
	int *needredo;
{
	static char ban[MSGSZ];
	char s[MSGSZ], *t, *u;
	struct cache_user *cu;
	int i;

	*needredo = 0;

	if ((cu = getfromucache(n, ch, NULL, 0)) == NULL)
		return NULL;
	if (cu->cu_source == NULL) {
		iw_printf(COLI_TEXT, "%sSource address for %s not cached, \
fetching...\n", ppre, n);
		dprintf(sock, "USERHOST %s\r\n", n);
		*needredo = 1;
		return NULL;
	}
	strcpy(s, cu->cu_source);
	if (strchr(s, '!') == NULL || strchr(s, '@') == NULL ||
	    strchr(s, '.') == NULL)
		return NULL;

	if (cu->cu_mode & MD_O)
		sprintf(ban, "%s -o %s +b ", ch->ch_name, n);
	else
		sprintf(ban, "%s +b ", ch->ch_name);
	/*
	 * Banning on nicks is stupid.  We take the loginname, substituting
	 * the first character with "*".  Then we go for the domain... We
	 * need to ban all machines on the class C network.  If we have a
	 * numerical address, we substitute the last chunk with "*".  If we
	 * have a DNS address, we substitute the first chunk (the machine
	 * name) with "*".
	 */
	*pure = strchr(ban, 0);
	strcat(ban, "*!");
	t = strchr(s, '!');
	u = strchr(s, '@');
	*u++ = 0;
	*++t = '*';
	strcat(ban, t);
	strcat(ban, "@");
	t = u;
	if (!(i = strlen(t)))
		return NULL;
	for (; i > 0; i--)
		if (isalpha(*t++))
			break;
	if (!i) {
		/* numerical IP */
		if ((t = strchr(u, '.')) == NULL ||
		    (t = strchr(t+1, '.')) == NULL ||
		    (t = strchr(t+1, '.')) == NULL)
			return NULL;	/* IP broken, not enough dots */
		*t = 0;
		strcat(ban, u);
		strcat(ban, ".*");
	}
	else {
		/*
		 * domain name
		 * If the domain name contains only one dot, we do not
		 * replace the subdomain-name with * since we would
		 * otherwise ban the entire toplevel-domain.
		 */
		if ((t = strchr(u, '.')) == NULL)
			return NULL;	/* Adress broken, not a single dot */
		if (strchr(t+1, '.') != NULL) {
			strcat(ban, "*");
			strcat(ban, t);
		}
		else {
			strcat(ban, "*");
			strcat(ban, u);
		}
	}
#ifdef	DEBUGV
	iw_printf(COLI_TEXT, "DEBUG: formed ban %s from source %s\n", ban,
		cu->cu_source);
#endif
	return ban;
}

/*
 * Builds an ignore mask the same way as the ban mask.
 * The problem is that we have to search all channel caches for the
 * specified nickname.  This function is used for the /ign shortcut.
 */
void
ignmask(n)
	char *n;
{
	struct channel *ch;
	char *pure, *bm = NULL;
	int needredo;
	char t[MSGSZ];

	for (ch = cha; ch != NULL && bm == NULL; ch = ch->ch_next)
		bm = buildban(n, ch, &pure, &needredo);

	if (bm != NULL && pure != NULL) {
#ifdef	DEBUGV
		iw_printf(COLI_TEXT, "DEBUG: got ignore mask: %s\n", pure);
#endif
		add_ignore(pure, 0);
	}
	else if (needredo) {
		sprintf(t, "/IGN %s", n);
		setredo(t, REDO_USERHOST);
	}
}

/*
 * Create a new spam-discard list.  We use a static array of pointers
 * that stores up to 16 words instead of a dynamic list or queue because
 * it's easy and fast.
 */
void
buildspam(s)
	char *s;
{
	int i;

	if (spamtext != NULL)
		free(spamtext);
	if (s == NULL || strlen(s) == 0)
		return;
	spamtext = chkmem(Strdup(s));
	spam[0] = strtok(spamtext, " \t");
	irc_strupr(spam[0]);
#ifdef	DEBUGV
	iw_printf(COLI_TEXT, "DEBUG: spam[0] = \"%s\"\n", spam[0]);
#endif
	for (i = 1; i < 16; i++) {
		if ((spam[i] = strtok(NULL, " \t")) == NULL)
			break;
		irc_strupr(spam[i]);
#ifdef	DEBUGV
		iw_printf(COLI_TEXT, "DEBUG: spam[%d] = \"%s\"\n", i, spam[i]);
#endif
	}
	swords = i;
}

/*
 * Check case-insensitive for spam in incoming PRIVMSG and NOTICEs.
 * Returns 1 if a match was found and the message should be discarded,
 * 0 otherwise.
 */
int
checkspam(m)
	char *m;
{
	int i;
	char *t;

	if (m == NULL || !swords)
		return 0;
	t = chkmem(Strdup(m));
	irc_strupr(t);

	for (i = 0; i < swords; i++)
		if (strstr(t, spam[i]) != NULL) {
#ifdef	DEBUGV
			iw_printf(COLI_TEXT, "DEBUG: checkspam() match: %s\n",
				spam[i]);
#endif
			free(t);
			return 1;
		}
	free(t);
	return 0;
}

/*
 * Parse a userhost reply.  Display and update cache if user is on
 * any of our channels.
 */
void
parse_uhost(sm)
	struct servmsg *sm;
{
	char *n, *a;
	char src[MSGSZ];

	iw_printf(COLI_SERVMSG, "%sUser/Host: %s\n", ppre, sm->sm_par[1]);

	n = strtok(sm->sm_par[1], " ");

	while (n != NULL) {
		if ((a = strchr(n, '=')) == NULL)
			break;
		if (*(a-1) == '*')	/* operatorflag, not part of nick */
			*(a-1) = 0;
		*a++ = 0;		/* = delimiter */
		a++;			/* skip away-flag */

#ifdef	DEBUGV
		iw_printf(COLI_TEXT, "DEBUG: uhost user: nick=%s, addr=%s\n",
			n, a);
#endif
		/* Add src hostaddress to cache. */
		sprintf(src, "%s!%s", n, a);
		addsrctocache(n, src);

		/* Next user. */
		n = strtok(NULL, " ");
	}
}

/*
 * Open the lastlog file for writing.
 */
void
open_llog()
{
	int oum, r;
	struct stat sb;

	if (lastlog != NULL)	/* logfile already in use */
		return;
	strcpy(llogfn, LLOGNAME);
	sprintf(pid, "%d", getpid());
	strcat(llogfn, pid);

	if ((r = stat(llogfn, &sb)) < 0 && errno != ENOENT) {
		fprintf(stderr, "WARNING: cannot stat %s: %s, lastlog \
unavailable\n", llogfn, Strerror(errno));
		return;
	}
	else if (r == 0) {
		fprintf(stderr, "WARNING: whoa there: lastlog file %s exists, \
I won't use this one! Lastlog unavailable.\n", llogfn);
		return;
	}

	oum = umask(077);
	lastlog = fopen(llogfn, "w+");
	umask(oum);
	if (lastlog == NULL) {
		fprintf(stderr, "WARNING: failed to open/create %s, lastlog \
unavailable\n", llogfn);
		return;
	}
	if (unlink(llogfn) < 0) {
		fprintf(stderr, "WARNING: failed to unlink %s: %s, lastlog \
unavailable\n", llogfn, Strerror(errno));
		fclose(lastlog);
		lastlog = NULL;
	}
	fprintf(stderr, "Lastlog opened\n");
}

/*
 * Manage channel logging to file.
 */
void
logchannel(channel, filename)
	char *channel, *filename;
{
	int a;
	static char nprompt[] = "Enter channel logfile name: ";
	char t[512];
	struct channel *ch;
	int numlogs = 0;

	/* If called without parameters, display open logfiles */
	if (channel == NULL) {
		for (ch = cha; ch != NULL; ch = ch->ch_next)
			if (ch->ch_logfile != NULL && ch->ch_logfname != NULL) {
				numlogs++;
				iw_printf(COLI_TEXT, "++ Logging %s to %s\n",
					ch->ch_name, ch->ch_logfname);
			}
		if (numlogs == 0)
			iw_printf(COLI_TEXT, "%sNo channels are being logged\n",
				ppre);

		/* Does this belong here?  Well, who cares. */
		if (msglog != NULL && msglogfn != NULL)
			iw_printf(COLI_TEXT, "++ Messages are logged to %s\n",
				msglogfn);
		else
			iw_printf(COLI_TEXT, "%sMessages are not logged\n",
				ppre);
		return;
	}
		
	if ((ch = getchanbyname(channel)) == NULL) {
		iw_printf(COLI_TEXT, "%s%s: no such channel\n", ppre, channel);
		return;
	}

	logch = ch;	/* remember channel, for done_clogfn() */

	if (ch->ch_logfile != NULL) {
		sprintf(t, "Already logging to %s, close? ", ch->ch_logfname);
		a = askyn(t);
		elrefr(1);
		if (a == YES)
			ch_closelog(ch);
		else
			iw_printf(COLI_TEXT, "%sNothing happens\n", ppre);
	}
	else {
		if (filename != NULL && strlen(filename))
			done_clogfn(filename);
		else {
			set_prompt(nprompt);
			linedone(0);
			othercmd = done_clogfn;
		}
	}
}

/*
 * Close a channel's logfile.
 */
void
ch_closelog(ch)
	struct channel *ch;
{
	if (ch->ch_logfile == NULL)
		return;

	fprintf(ch->ch_logfile, "\n### Closing logfile (channel %s), %s\n",
		ch->ch_name, timestamp());
	fclose(ch->ch_logfile);
	ch->ch_logfile = NULL;

	if (ch->ch_logfname != NULL) {
		iw_printf(COLI_TEXT, "%sLogfile %s closed.\n", ppre,
			ch->ch_logfname);
		free(ch->ch_logfname);
		ch->ch_logfname = NULL;
	}
}

static void
done_clogfn(filename)
	char *filename;
{
	struct stat statbuf;
	int r, a;

	othercmd = NULL;
	set_prompt(NULL);

	if (filename == NULL || !strlen(filename)) {
		iw_printf(COLI_TEXT, "%sNo logfile specified.\n", ppre);
		elrefr(1);
		return;
	}

	filename = exptilde(filename);
	r = stat(filename, &statbuf);

	if (r < 0 && errno != ENOENT) {
		iw_printf(COLI_WARN, "%s%sstat() returned error: %s%s\n",
			TBOLD, ppre, Strerror(errno), TBOLD);
		elrefr(1);
		return;
	}

	if (r < 0) {
		/* File does not exist, open it for writing. */
		if ((logch->ch_logfile = fopen(filename, "w")) == NULL) {
			iw_printf(COLI_WARN, "%sCan't open %s: %s\n", ppre,
				filename, Strerror(errno));
			elrefr(1);
			return;
		}
	}
	else {
		/* Check for regular file. */
		if (! S_ISREG(statbuf.st_mode)) {
			iw_printf(COLI_WARN, "%sNot a regular file: %s\n",
				ppre, filename);
			elrefr(1);
			return;
		}

		/*
		 * File does already exist.  Ask if we may append to it
		 * or if we should abort.
		 */
		a = askyn("File does already exist.  Append to it? ");
		elrefr(1);
		if (a == YES) {
			if ((logch->ch_logfile = fopen(filename, "a"))
			    == NULL) {
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

	logch->ch_logfname = chkmem(Strdup(filename));
	fprintf(logch->ch_logfile, "\n### Opening logfile (channel %s), %s\n",
		logch->ch_name, timestamp());
	iw_printf(COLI_TEXT, "%sNow logging channel %s to %s\n", ppre,
		logch->ch_name, filename);
	elrefr(1);
	logch = NULL;
}

/*
 * Try to complete `name' with the appropriate nickname from `ch''s
 * user cache.  Beeps if the name cannot be resolved and returns the
 * name up to the last ambiguous character.  The scope of the string
 * returned is valid until the next call.
 * Returns NULL on failure.
 */
char *
complete_from_ucache(ch, name, addcolon)
	struct channel *ch;
	char *name;
	int addcolon;
{
	static char t[NICKSZ+3];
	char lnodes[NICKSZ+1];
	struct cache_user *cu;
	int fill, namelen, i;
	int is_ambiguous = 0;

	if (name == NULL || (namelen = strlen(name)) == 0)
		return NULL;

	fill = 0;
	memset(lnodes, 0, NICKSZ+1);

	for (cu = ch->ch_cachehd.lh_first; cu != NULL;
	    cu = cu->cu_entries.le_next) {
		/* Verify with name */
		if (irc_strncmp(name, cu->cu_nick, (unsigned)namelen))
			continue;
		/*
		 * Skip equal characters until fill ptr, if reached, add
		 * new ones.
		 */
		for (i = 0; !irc_chrcmp(lnodes[i], cu->cu_nick[namelen+i]) &&
		    i < fill; i++)
			;
		if (i < fill) {
			/* Ambiguity detected, mark lnode */
			lnodes[i] = 0;
			is_ambiguous = 1;
		}
		else {
			/* No ambiguity, add the rest of the string */
			fill = strlen(cu->cu_nick);
			memmove(lnodes+i, cu->cu_nick+namelen+i,
				(unsigned)fill);
		}
	}

	if (is_ambiguous)
		dobell();
	if (fill != 0) {
		strcpy(t, name);
		strcat(t, lnodes);
		if (!is_ambiguous)
			/* unique nicks get space or colon+space appended */
			if (addcolon)
				strcat(t, ": ");
			else
				strcat(t, " ");
		return t;
	}
	return NULL;
}

/*
 * Return boolean true if character is allowed in nickname, false otherwise.
 */
int
isnickch(c)
	int c;
{
	switch (c) {
	case '{':
	case '}':
	case '[':
	case ']':
	case '|':
	case '\\':
	case '_':
	case '^':
	case '-':
	case '`':
		return 1;
	}
	return isalnum(c);
}

/*
 * Manage message logging to file.
 */
void
logmessage(filename)
	char *filename;
{
	int a;
	static char nprompt[] = "Enter message logfile name: ";
	char t[512];

	if (msglog != NULL) {
		sprintf(t, "Already logging messages to %s, close? ",
			msglogfn);
		a = askyn(t);
		elrefr(1);
		if (a == YES)
			closemlog();
		else
			iw_printf(COLI_TEXT, "%sNothing happens\n", ppre);
	}
	else {
		if (filename != NULL && strlen(filename))
			done_mlogfn(filename);
		else {
			set_prompt(nprompt);
			linedone(0);
			othercmd = done_mlogfn;
		}
	}
}

/*
 * Close message logfile.
 */
void
closemlog()
{
	if (msglog == NULL)
		return;

	fprintf(msglog, "\n### Closing messages logfile, %s\n", timestamp());
	fclose(msglog);
	msglog = NULL;

	if (msglogfn != NULL) {
		iw_printf(COLI_TEXT, "%sMessages logfile %s closed.\n", ppre,
			msglogfn);
		free(msglogfn);
		msglogfn = NULL;
	}
}

static void
done_mlogfn(filename)
	char *filename;
{
	struct stat statbuf;
	int r, a;

	othercmd = NULL;
	set_prompt(NULL);

	if (filename == NULL || !strlen(filename)) {
		iw_printf(COLI_TEXT, "%sNo logfile specified.\n", ppre);
		elrefr(1);
		return;
	}

	filename = exptilde(filename);
	r = stat(filename, &statbuf);

	if (r < 0 && errno != ENOENT) {
		iw_printf(COLI_WARN, "%s%sstat() returned error: %s%s\n",
			TBOLD, ppre, Strerror(errno), TBOLD);
		elrefr(1);
		return;
	}

	if (r < 0) {
		/* File does not exist, open it for writing. */
		if ((msglog = fopen(filename, "w")) == NULL) {
			iw_printf(COLI_WARN, "%sCan't open %s: %s\n", ppre,
				filename, Strerror(errno));
			elrefr(1);
			return;
		}
	}
	else {
		/* Check for regular file. */
		if (! S_ISREG(statbuf.st_mode)) {
			iw_printf(COLI_WARN, "%sNot a regular file: %s\n",
				ppre, filename);
			elrefr(1);
			return;
		}

		/*
		 * File does already exist.  Ask if we may append to it
		 * or if we should abort.
		 */
		a = askyn("File does already exist.  Append to it? ");
		elrefr(1);
		if (a == YES) {
			if ((msglog = fopen(filename, "a")) == NULL) {
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

	msglogfn = chkmem(Strdup(filename));
	fprintf(msglog, "\n### Opening messages logfile, %s\n", timestamp());
	iw_printf(COLI_TEXT, "%sNow logging messages to %s\n", ppre, filename);
	elrefr(1);
}

/*
 * Init OOD handling.
 */
void
ood_init()
{
	LIST_INIT(&ood_list);
}

/*
 * Clean the ood list.
 */
void
ood_clean()
{
	while (ood_del(0))
		;
}

/*
 * Add an entry to the ood list.  Format of a line is:
 *	u@h:[pass]:channel[,channel]*
 * Returns 1 if ok, 0 on error.  Destructive on parameter ``line''.
 */
int
ood_add(line)
	char *line;
{
	char *uhost, *pass, *chan;
	struct ood_entry *ood;
	char *uhre;

	pass = chan = NULL;
	if ((uhost = line) == NULL)
		return 0;
	if ((pass = strchr(line, ':')) != NULL) {
		*pass = '\0';
		pass++;
	}
	else
		return 0;
	if ((chan = strchr(pass, ':')) != NULL) {
		*chan = '\0';
		chan++;
	}
	else
		return 0;

	ood = chkmem(malloc(sizeof *ood));
	ood->ood_uhost = chkmem(Strdup(uhost));
	if ((uhre = globtore(ood->ood_uhost)) == NULL)
		return 1;
#ifdef	HAVE_REGCOMP
	if ((regcomp(&ood->ood_uhostre, uhre, REG_EXTENDED | REG_NOSUB)) != 0) {
		iw_printf(COLI_TEXT, "%sAdding to ood-list failed (regcomp() \
failed)\n", ppre);
		free(ood);
		return 1;
	}
#elif defined(HAVE_REGCMP)
	if ((ood->ood_uhostre = regcmp(uhre, NULL)) == NULL) {
		iw_printf(COLI_TEXT, "%sAdding to ood-list failed (regcmp() \
failed)\n", ppre);
		free(ood);
		return;
	}
#endif
	ood->ood_pass = chkmem(Strdup(pass));
	ood->ood_chan = chkmem(Strdup(irc_strupr(chan)));

#ifdef	DEBUGV
	iw_printf(COLI_TEXT, "DEBUG: ood_add(): uhost='%s', pass='%s', \
chan='%s'\n", ood->ood_uhost, ood->ood_pass, ood->ood_chan);
#endif

	LIST_INSERT_HEAD(&ood_list, ood, ood_entries);
	return 1;
}

/*
 * Delete an ood entry, given the index number.
 * Returns 1 if ok, 0 on error.
 */
int
ood_del(no)
	int no;
{
	struct ood_entry *ood, *ooe;
	int i;

	for (i = 0, ood = ood_list.lh_first; ood != NULL; ood = ooe, i++) {
		ooe = ood->ood_entries.le_next;

		if (i == no) {
			free(ood->ood_uhost);
			free(ood->ood_pass);
			free(ood->ood_chan);
			LIST_REMOVE(ood, ood_entries);
			free(ood);
			return 1;
		}
	}
	return 0;
}

/*
 * Check an ood request for validity...
 * Returns 1 if passed, 0 if not.
 */
int
ood_verify(uhost, pass, chan)
	char *uhost, *pass, *chan;
{
	struct ood_entry *ood;
	char *p;

	irc_strupr(chan);

	for (ood = ood_list.lh_first; ood != NULL;
	    ood = ood->ood_entries.le_next)
		if (matchre_ood_uhost(uhost, &ood->ood_uhostre) &&
		    !strcmp(pass, ood->ood_pass)) {
#ifdef	lint
			p = NULL;	/* make lint happy */
#endif
			if ((p = strstr(ood->ood_chan, chan)) == NULL)
				return 0;
			if (*(p + strlen(chan)) == ' ' ||
			    *(p + strlen(chan)) == ',' ||
			    *(p + strlen(chan)) == '\0')
				return 1;
		}
	return 0;
}

static int
matchre_ood_uhost(uhost, uhostre)
	char *uhost;
#ifdef	HAVE_REGCOMP
	regex_t	*uhostre;
#elif defined(HAVE_REGCMP)
	char	*uhostre;
#endif
{
#ifdef	HAVE_REGEXEC
		if (!regexec(uhostre, uhost, 0, NULL, 0))
			return 1;
#elif defined(HAVE_REGEX)
		if (regex(uhostre, uhost, NULL) != NULL)
			return 1;
#endif
	return 0;
}

/*
 * Print all ood lines.
 */
void
ood_display()
{
	struct ood_entry *ood;
	int i;

	setlog(0);

	if (ood_list.lh_first == NULL)
		iw_printf(COLI_TEXT, "%sNo OOD lines registered\n", ppre);
	else
		iw_printf(COLI_TEXT, "%sOOD lines:\n", ppre);
	
	for (i = 0, ood = ood_list.lh_first; ood != NULL;
	    ood = ood->ood_entries.le_next, i++)
		iw_printf(COLI_TEXT, "++ [%2d]  %s:%s:%s\n", i, ood->ood_uhost,
			*ood->ood_pass != '\0' ? "*" : "", ood->ood_chan);
	setlog(1);
}

/*
 * Process incoming ood request.
 */
void
ood_incoming(sm, nname)
	struct servmsg *sm;
	char *nname;
{
	char *uhost, *chan, *pass, *t, *u;
	char *nopass = "";
	struct channel *ch;
	struct cache_user *cu;

	if ((uhost = strchr(sm->sm_prefix, '!')) == NULL ||
	    (t = strchr(sm->sm_par[1], ' ')) == NULL) {
broken_ood:
		iw_printf(COLI_TEXT, "%sBroken OOD request from %s: %s %s\n",
			ppre, sm->sm_prefix, sm->sm_par[1], TIMESTAMP);
		notice(nname, "\1OP broken request\1", 1);
		return;
	}
	uhost++;
	if (*uhost == '~' || *uhost == '-' || *uhost == '=' || *uhost == '^')
		uhost++;

	t++;
	u = t;
	while (*u != '\0') {
		if (*u == '\1')
			*u = ' ';
		u++;
	}

	if ((chan = strtok(t, " ")) == NULL)
		goto broken_ood;

	if ((pass = strtok(NULL, " ")) == NULL)
		pass = nopass;

#ifdef	DEBUGV
	iw_printf(COLI_TEXT, "DEBUG: ood_incoming(): uhost='%s', pass='%s', \
chan='%s'\n", uhost, pass, chan);
#endif

	if (ood_verify(uhost, pass, chan) != 1) {
		iw_printf(COLI_TEXT, "%sOOD request from %s for channel %s: no \
authorization %s\n", ppre, sm->sm_prefix, chan, TIMESTAMP);
		notice(nname, "\1OP no authorization\1", 1);
		return;
	}
	else {
		if (!check_conf(CONF_OOD)) {
			iw_printf(COLI_TEXT, "%sGot OOD request from %s for \
channel %s: not served (OOD disabled)\n", ppre, sm->sm_prefix, chan);
			notice(nname, "\1OP OOD is disabled\1", 1);
			return;
		}

		if ((ch = getchanbyname(chan)) == NULL) {
			iw_printf(COLI_TEXT, "%sOOD request from %s: not on \
channel %s %s\n", ppre, sm->sm_prefix, chan, TIMESTAMP);
			notice(nname, "\1OP I'm not on channel\1", 1);
			return;
		}

		if ((cu = getfromucache(nname, ch, NULL, 0)) == NULL) {
			iw_printf(COLI_TEXT, "%sOOD request from %s: no \
nickname %s on channel %s %s\n", ppre, sm->sm_prefix, nname, chan, TIMESTAMP);
			notice(nname, "\1OP You're not on channel\1", 1);
			return;
		}

		if (cu->cu_mode & MD_O) {
			iw_printf(COLI_TEXT, "%sOOD request from %s: %s is \
already +o on channel %s %s\n", ppre , sm->sm_prefix, nname, chan, TIMESTAMP);
			notice(nname, "\1OP already +o\1", 1);
			return;
		}

		iw_printf(COLI_TEXT, "%sServing OOD request from %s for \
channel %s %s\n", ppre, sm->sm_prefix, chan, TIMESTAMP);
		dprintf(sock, "MODE %s +o %s\r\n", ch->ch_name, nname);
	}
}

/*
 * Converts a shell-globbing like pattern to a regular expression for
 * use with regcomp/regcmp.  Returns NULL on error.
 */
#define IESC(c)	{ repat[i++] = '\\'; repat[i++] = (c); break; }

char *
globtore(gpat)
	char *gpat;
{
	static char *repat;
	int i = 0, l;

	if (gpat == NULL || (l = strlen(gpat)) == 0) {
		iw_printf(COLI_TEXT, "%snull globbing\n", ppre);
		return NULL;
	}
	if (repat != NULL)
		free(repat);
	repat = chkmem(malloc((unsigned)l*2+1));

	repat[i++] = '^';

	while (*gpat) {
		switch (*gpat) {
		default:
			repat[i++] = *gpat;
			break;
		case '*':	
			repat[i++] = '.';
			repat[i++] = '*';
			break;
		case '?':
			repat[i++] = '.';
			break;
		case '.':	IESC('.');
		case '+':	IESC('+');
		case '|':	IESC('|');
		case '\\':	IESC('\\');
		case '&':	IESC('&');
		case '$':	IESC('$');
		case '^':	IESC('^');
		case '[':	IESC('[');
		case ']':	IESC(']');
		case '(':	IESC('(');
		case ')':	IESC(')');
		case '{':	IESC('{');
		case '}':	IESC('}');
		}
		gpat++;
	}
	repat[i++] = '$';
	repat[i] = 0;

	return repat;
}

