/*-
 * tirc -- client for the Internet Relay Chat
 *
 *	Copyright (c) 1996, 1997, 1998
 *		Matthias Buelow.  All rights reserved.
 *
 *	See the file ``COPYRIGHT'' for the usage and distribution
 *	license and warranty disclaimer.
 *
 * This is derived from code contributed to TIRC by Richard Corke
 * (rjc@rasi.demon.co.uk).
 */

#ifndef lint
static char rcsid[] = "$Id: dl.c,v 1.12 1998/02/20 18:20:26 token Exp $";
#endif

#include "tirc.h"

#ifdef	WITH_DLMOD

#ifdef	HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef	HAVE_DLFCN_H
#include <dlfcn.h>
#endif
#ifdef	HAVE_CTYPE_H
#include <ctype.h>
#endif
#ifdef	HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef	HAVE_STRING_H
#include <string.h>
#endif

#include "compat.h"
#include "dl.h"
#include "colour.h"

#define INVSYNTAX	{ setlog(0); iw_printf(COLI_TEXT, "%sInvalid command \
syntax for /%s\n", ppre, ctbl[i].c_str); setlog(1); }

#define	NOTUNIQUE	{ setlog(0); iw_printf(COLI_TEXT, "%s\"%s\" Not a \
unique filename, use reference number instead\n", ppre, dme->filestr); }

#define	SETLOGRET	{ setlog(1); return; }

#define LOOPDLCMDTBL(var)	for(var = dlcmdtbl_head.tqh_first; var != NULL;\
var = var->dlcmdtbl_entries.tqe_next)

#define LOOPDLMODTBL(var)	for(var = dlmodtbl_head.tqh_first; var != NULL;\
var = var->dlmodtbl_entries.tqe_next)

struct cmdtbl {
	const char	*c_str;
	unsigned long	c_hash;
	void 		(*c_fun) __P((int, char *));
	unsigned	c_flags;
	const char	*c_help;
};
 
static TAILQ_HEAD(, dlmodtbl_entry) dlmodtbl_head;
static TAILQ_HEAD(, dlcmdtbl_entry) dlcmdtbl_head;

extern struct cmdtbl	ctbl[];
extern int		numcmd;
extern int		on_irc;
extern char		ppre[];

static struct dlmodtbl_entry	*dlgetmodbystr(char *);
static struct dlmodtbl_entry	*dlismodptr(VOIDPTR);
static int	dlisuniquefilestr(struct dlmodtbl_entry *);
static void	dlmodtbl_remove(struct dlmodtbl_entry *);
static void	dlcmdtbl_remove(struct dlcmdtbl_entry *);
static void	dlcmdtbl_removeall(VOIDPTR);

static int	needs_uscore;

/*
 * Initialize some TAILQ's.
 */
void
dlinit()
{
	TAILQ_INIT(&dlmodtbl_head);
	TAILQ_INIT(&dlcmdtbl_head);	
	needs_uscore = 0;
}

/*
 * User front end for the module functions.
 * Called from cmd.c
 */
void
dlmodcmd(i, pars)
	int	i;
	char	*pars;
{
	struct dlmodtbl_entry	*dme;
	char 	*modcmd, *a, *b;
	char	sep[] = " \t";

	setlog(0);

	if ((modcmd = strtok(pars, sep)) == NULL) {
		INVSYNTAX
		return;
	}
	a = strtok(NULL, "");
	irc_strupr(modcmd);

	if (!strcmp(modcmd, "CMDS")) {
		dlcmdlistcmd();
		SETLOGRET
	}
	if (!strcmp(modcmd, "INFO")) {
		if (!a)
			dlinfocmd(NULL);
		else
			for (b = strtok(a, sep); b != NULL;
					b = strtok(NULL, sep)) {
				dme = dlgetmodbystr(b);
				if (*b != '$' && dme != NULL &&
				    dlisuniquefilestr(dme)) {
					NOTUNIQUE
					continue;
				}
				if (dme == NULL) {
					iw_printf(COLI_TEXT,
				    "%s\"%s\" Module not loaded\n" ,ppre, b);
					continue;
				}
				dlinfocmd(dme);	
			}
		SETLOGRET
	}
	if (!strcmp(modcmd, "HELP")) {
		b = strtok(a, " \t");

		if (!a)
			dlhelpcmd(NULL);
		else
			dlhelpcmd(b);
		SETLOGRET
	}

	if (a == NULL) {
		INVSYNTAX
		return;
	}
	if (!strcmp(modcmd, "OPEN")) {
		for (b = strtok(a, sep); b != NULL; b = strtok(NULL, sep))
			dlopencmd(b);
		SETLOGRET
	}
	if (!strcmp(modcmd, "CDEL")) {
		for (b = strtok(a, sep); b != NULL; b = strtok(NULL, sep))
			dlcdelcmd(b);
		SETLOGRET
	}	
	if (!strcmp(modcmd, "CLOSE")) {
		for (b = strtok(a, sep); b != NULL; b = strtok(NULL, sep)) {
			dme = dlgetmodbystr(b);
			if (!isdigit(*b) && dme != NULL &&
			    dlisuniquefilestr(dme)) {
				NOTUNIQUE
				continue;
			}
			if (dme == NULL) {
				iw_printf(COLI_TEXT, 
				    "%s\"%s\" Module not loaded\n", ppre, b);
				continue;
			}
			dlclosecmd(dme);
		}
		SETLOGRET
	}

	if (!strcmp(modcmd, "CADD")) {
		char	*c, *d, *e;

		if ((b = strtok(a, sep)) == NULL ||
			(c = strtok(NULL, sep)) == NULL ||
			(d = strtok(NULL, sep)) == NULL) {
			INVSYNTAX
			return;
		}
		e = strtok(NULL, sep);

		if ((dme = dlgetmodbystr(d)) == NULL) {
			iw_printf(COLI_TEXT, "%s\"%s\" Invalid module\n",
				ppre, d);
			SETLOGRET
		}
		if (*d != '$' && dlisuniquefilestr(dme)) {
			NOTUNIQUE
			SETLOGRET
		}
		if (e != NULL)
			dlcaddcmd(b, c, dme, NULL, (unsigned) atoi(e));
		else
			dlcaddcmd(b, c, dme, NULL, 0);
		SETLOGRET
	}

	INVSYNTAX
	return;
}

/*
 * Attempts to open 'fstr' as a module, and adds a record for it.
 */
int
dlopencmd(fstr)
	char	*fstr;
{
	VOIDPTR			dlptr;
	const char		*(*dlinitfunc)(struct dlmodtbl_entry *);
	const char		*(*nous)(struct dlmodtbl_entry *);
	char			rpath[MAXPATHLEN], *rpathptr, *rp;
	struct dlmodtbl_entry	*dme, *tempdme;
	int	t;

	if ((strlen(fstr) > MAXPATHLEN - 1) ||
			(rpathptr = realpath(fstr, rpath)) == NULL) {
		iw_printf(COLI_TEXT, "%s\"%s\" Illegal path\n", ppre, fstr);
		return -1;
	}

	if ((dlptr = dlopen(fstr, 1)) == NULL) {
		iw_printf(COLI_TEXT, "%s\"%s\" dlopen failed: %s\n", ppre, fstr,
				dlerror());
		return -1;
	}

	if (dlismodptr(dlptr) != NULL) {
		iw_printf(COLI_TEXT, "%s\"%s\" already loaded\n", ppre, fstr);
		return -1;
	}

	rp = rpathptr + strlen(rpathptr) - 1;
	while(*rp != '/')
		rp--;

	dme = (struct dlmodtbl_entry *)
			chkmem(calloc(1, sizeof(struct dlmodtbl_entry)));
			
	dme->idstring = NULL;
	dme->dlptr = dlptr;
	dme->filestr = chkmem(Strdup(++rp));
	*rp = 0;
	dme->path = chkmem(Strdup(rpathptr));

	/*
	 * Makes sure that the entry for our module is inserted in the
	 * right place in the TAILQ
	 */
	for(tempdme = dlmodtbl_head.tqh_first, t = 0; tempdme != NULL;
			tempdme = tempdme->dlmodtbl_entries.tqe_next, t++)
		if (tempdme->refno != t) {
			dme->refno = t;
			TAILQ_INSERT_BEFORE(tempdme, dme, dlmodtbl_entries);
			break;
		}
	if (tempdme == NULL) {
		dme->refno = t;
		TAILQ_INSERT_TAIL(&dlmodtbl_head, dme, dlmodtbl_entries);
	}

	/*
	 * An _init function is probably called anyway, but we call our own,
	 * since we do not even know how the shared library format on this
	 * system initalizes shared objects etc.
	 * We also have to check whether the system needs object symbols to
	 * be specified with or without a leading underscore.
	 */
	iw_printf(COLI_TEXT, "%s\"%s\" Shared library sucessfully opened, \
attempting to get ID...\n", ppre, fstr);
	if ((nous = dlinitfunc = (const char * (*)(struct dlmodtbl_entry *)) 
		    dlsym(dme->dlptr, "modid")) == NULL
	    && (dlinitfunc = (const char * (*)(struct dlmodtbl_entry *))
		    dlsym(dme->dlptr, "_modid")) == NULL)
		iw_printf(COLI_WARN, "%sModule loaded, no modid() function \
found.\n", ppre);
	else {
		needs_uscore = (nous == NULL);
		dme->idstring = (*dlinitfunc)(dme);
		iw_printf(COLI_TEXT, "%sModule identifies as \"%s\"\n",
			ppre, (dme->idstring != NULL) ? dme->idstring
							: "no info available");
	}

	return dme->refno;
}

/*
 * Attempts to bind a module function to a user command.
 */
int
dlcaddcmd(cmdstr, function, mod, c_help, flags)
	char	*cmdstr, *function;
	struct dlmodtbl_entry	*mod;
	char	*c_help;
	unsigned flags;
{
	struct dlcmdtbl_entry	*dce, *tempdce;
	char			c_str[MAXDLCMDSZ + 1];
	unsigned		c_hash;
	void			(*c_fun)(int, char *);
	int			i = 0;
	char			*funcbindname;

	if (strlen(cmdstr) > MAXDLCMDSZ) {
		iw_printf(COLI_TEXT, "%s\"%s\" Command name too large\n",
			ppre, cmdstr);
		return -1;
	}

	while(*cmdstr)
		c_str[i++] = toupper(*cmdstr++);
	c_str[i] = 0;
	c_hash = elf_hash(c_str);

	/*
	 * Check if the command trying to be bound is already a fixed,
	 * or bound command
	 */
	for (i = 0; i < numcmd; i++)
		if (ctbl[i].c_hash == c_hash) {
			iw_printf(COLI_TEXT,
			    "%s\"%s\" command already registered\n",
			    ppre, c_str);
			return -1;
		}

	LOOPDLCMDTBL(tempdce)
		if (tempdce->c_hash == c_hash) {
			iw_printf(COLI_TEXT,
				"%s\"%s\" command already registered\n",
				ppre, c_str);
			return -1;
		}

	/*
	 * Check if the function exists, and if so, get the address.
	 * Puts an underscore at the beginning before attempting to
	 * bind if this is required by the system.
	 */	
	if (needs_uscore) {
		funcbindname = chkmem(malloc(strlen(function)+2));
		*funcbindname = '_';
		strcpy(funcbindname+1, function);
	}
	else
		funcbindname = function;
	if ((c_fun = (void (*)(int, char *)) 
			dlsym(mod->dlptr, funcbindname)) == NULL) {
		iw_printf(COLI_TEXT,
			"%s\"%s\" no such function in module\n", ppre, 
				function);
		return -1;
	}
	if (funcbindname != function)
		free(funcbindname);

	/* Create new record */
	dce = (struct dlcmdtbl_entry *)
			chkmem(calloc(1, sizeof(struct dlcmdtbl_entry)));
	dce->c_fun = c_fun;
	dce->c_str = chkmem(Strdup(c_str));
	dce->c_hash = c_hash;
	dce->c_flags = flags ? 1 : 0;
	dce->c_help = c_help;
	dce->c_func = chkmem(Strdup(function));
	dce->dme = mod;

	TAILQ_INSERT_TAIL(&dlcmdtbl_head, dce, dlcmdtbl_entries);

	iw_printf(COLI_TEXT,
		"%sCommand \"%s\", bound to function \"%s\", in module "
		"\"%s\", Connection required = \"%d\"\n", ppre, 
		c_str, function, dce->dme->filestr, dce->c_flags);
	return 0;
}

void
dlinfocmd(mod)
	struct dlmodtbl_entry	*mod;
{
	struct dlmodtbl_entry	*dme;
	char	t[BUFSZ], t2[BUFSZ];
	int	len = 0;

	t[0] = 0;

	if (mod == NULL) {
		if (dlmodtbl_head.tqh_first == NULL) 
			sprintf(t, "\tNone\n");
		else
			LOOPDLMODTBL(dme) {
				len += sprintf(t2, "[%2d]  %s  \"%s\"\n",
					    dme->refno, dme->filestr,
					    (dme->idstring != NULL)
						? dme->idstring
						: "no info available");
				if (len >= BUFSZ) {
					iw_printf(COLI_TEXT,
						"%sCurrent modules loaded:\n%s",
						ppre, t);
					len = 0;
					t[0] = 0;
				}
				strcat(t, t2);
			}
		iw_printf(COLI_TEXT, "%sCurrent modules loaded:\n%s", ppre, t);
	} else {
		iw_printf(COLI_TEXT,
"%sModule information:\n\
Path:          %s\n\
Filename:      %s\n\
Ref No.:       %d\n\
Mod. Pointer:  0x%-8lx\n\
ID String:     %s\n", ppre, mod->path, mod->filestr, mod->refno,
			(long unsigned int) mod->dlptr,
			(mod->idstring != NULL) ? mod->idstring : "<none>");
	}

	return;
}

void
dlhelpcmd(topic)
	char	*topic;
{
	struct dlcmdtbl_entry	*dce;
	char	spaces[] = "       ", *s = (char *) &spaces;
	int	i = 0;

	if (dlcmdtbl_head.tqh_first == NULL) {
		iw_printf(COLI_TEXT, "%sNo help availiable\n", ppre);
		return;
	}
	if (topic == NULL) {
		iw_printf(COLI_TEXT,
			"%sHelp is availiable for the following:\n", ppre);

		LOOPDLCMDTBL(dce) {
			if (dce->c_help != NULL) {
				iw_printf(COLI_TEXT, 
					"    %s%s", dce->c_str, s +
							strlen(dce->c_str));
				i++;
			}
			if (i == 6) {
				i = 0;
				iw_printf(COLI_TEXT, "\n");
			}
		}
		if (i)
			iw_printf(COLI_TEXT, "\n");
		return;
	} else {
		LOOPDLCMDTBL(dce)
			if (!irc_strcmp(dce->c_str, topic))
				if (dce->c_help != NULL) {
					iw_printf(COLI_TEXT,
						"Usage: %s\n", dce->c_help);
					return;
				}
	}
	iw_printf(COLI_TEXT, "%sNo matching topic found\n", ppre);
	return;
}

void
dlcmdlistcmd()
{
	struct dlcmdtbl_entry	*dce;
	char	t[BUFSZ], t2[BUFSZ];
	int	len = 0;

	t[0] = 0;

	if (dlcmdtbl_head.tqh_first == NULL)
		sprintf(t, "\tNone\n");
	else
		LOOPDLCMDTBL(dce) {
			len += sprintf(t2, "Command: %s\t%sConnection req: %d"
				    "\nModule: %d - %s\t\tFunction: %s\n",
				    dce->c_str,
				    (strlen(dce->c_str) < 7) ? "\t\t" : "\t", 
				    (dce->c_flags & CF_SERV) ? 1 : 0,
				    dce->dme->refno, dce->dme->filestr,
				    dce->c_func);
			if (len >= BUFSZ - 1) {
				iw_printf(COLI_TEXT,
					"%sCurrent bound commands\n%s", ppre,
					t);
				len = 0;
				t[0] = 0;
			}
			strcat(t, t2);
		}

	iw_printf(COLI_TEXT, "%sCurrent bound commands:\n%s", ppre, t);

	return;
}

int
dlcdelcmd(cmdstr)
	char	*cmdstr;
{
	struct dlcmdtbl_entry	*dce;
	unsigned		hash;
	char			c_str[MAXDLCMDSZ + 1];
	int			i = 0;

	if (strlen(cmdstr) > MAXDLCMDSZ) {
		iw_printf(COLI_TEXT, "%s\"%s\" Invalid command name\n",
			ppre, cmdstr);
		return -1;
	}

	while(*cmdstr)
		c_str[i++] = toupper(*cmdstr++);
	c_str[i] = 0;
	hash = elf_hash(c_str);

	LOOPDLCMDTBL(dce)
		if (dce->c_hash == hash) {
			dlcmdtbl_remove(dce);
			iw_printf(COLI_TEXT, "%s\"%s\" command deleted\n", ppre,
					dce->c_str);
			return 0;
		}

	iw_printf(COLI_TEXT,
		"%s\"%s\" command isn't bound to a dynamic function\n", ppre,
			cmdstr);	
	return -1;
}

/*
 * The function _fini will automatically be called by the dlclose functon
 */
int
dlclosecmd(mod)
	struct dlmodtbl_entry	*mod;
{
	if(dlclose(mod->dlptr)) {
		iw_printf(COLI_TEXT, "%s\"%s\" dlclose failed: %s\n",
			ppre, mod->filestr, dlerror());
		return -1;
	} else {
		iw_printf(COLI_TEXT,
			"%sModule \"%s\" unloaded, cleaning up.\n",
			ppre, mod->filestr);
		dlmodtbl_remove(mod);
		dlcmdtbl_removeall(mod->dlptr);
		return 0;
	}
	/*NOTREACHED*/
	return -1;
}

/*
 * Not nescessary to pass 'i' to the module function
 */
int
dlfuncrun(funcstr, i, pars)
	char	*funcstr;
	int	i;
	char	*pars;
{
	struct dlcmdtbl_entry	*dce;
	
	LOOPDLCMDTBL(dce)
		if (dce->c_hash == elf_hash(funcstr)) {
			if (dce->c_flags & CF_SERV && !on_irc) {
				iw_printf(COLI_TEXT,
					"%sNot connected to a server\n",
						ppre);
				return 0;
			} else
				dce->c_fun(i, pars);
			return 0;
		}

	return -1;
}

/*
 * Identifies a module by 'filestr' or allows reference by module number
 * in numerical form. If two records exist with the same filename
 * (but different paths), then the first of the records will be returned
 * only.
 */
static struct dlmodtbl_entry *
dlgetmodbystr(modstr)
	char	*modstr;
{
	struct dlmodtbl_entry	*dme;
	unsigned	modhash;
	char	*t;
	int	refno = 0;

	if (isdigit(*modstr)) {
		t = modstr;
		while (*t)
			if(!isdigit(*t++))
				return NULL;
		refno = atoi(modstr);
		LOOPDLMODTBL(dme)
			if (dme->refno == refno)
				return dme;
		return NULL;
	}

	modhash = elf_hash(modstr);

	LOOPDLMODTBL(dme)
		if (modhash == elf_hash(dme->filestr))
			return dme;

	return NULL;
}

static int
dlisuniquefilestr(dme)
	struct dlmodtbl_entry	*dme;
{
	struct dlmodtbl_entry	*tempdme;
	unsigned	hash = elf_hash(dme->filestr);
	
	LOOPDLMODTBL(tempdme) {
		if (dme == tempdme)
			continue;
		if (hash == elf_hash(tempdme->filestr))
			return 1;
	}
	return 0;
}

static struct dlmodtbl_entry *
dlismodptr(dlptr)
	VOIDPTR	dlptr;
{
	struct dlmodtbl_entry	*dme;

	LOOPDLMODTBL(dme)
		if (dlptr == dme->dlptr)
			return dme;

	return NULL;
}

static void
dlmodtbl_remove(dme)
	struct dlmodtbl_entry	*dme;
{
	TAILQ_REMOVE(&dlmodtbl_head, dme, dlmodtbl_entries);
	free(dme->filestr);
	free(dme);
}

static void
dlcmdtbl_remove(dce)
	struct dlcmdtbl_entry	*dce;
{
	TAILQ_REMOVE(&dlcmdtbl_head, dce, dlcmdtbl_entries);
	free(dce->c_str);
	free(dce->c_func);
	free(dce);
}

static void 
dlcmdtbl_removeall(dlptr)
	VOIDPTR	dlptr;
{
	struct dlcmdtbl_entry	*dce;

	LOOPDLCMDTBL(dce)
		if (dce->dme->dlptr == dlptr)
			dlcmdtbl_remove(dce);
}

#endif	/* WITH_DLMOD */

