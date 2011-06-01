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
 *
 * $Id: dl.h,v 1.4 1998/02/20 18:20:26 token Exp $
 */

#ifndef	TIRC_DL_H
#define	TIRC_DL_H	1

#define MAXDLCMDSZ	7

struct dlmodtbl_entry {
	char	*filestr;
	char	*path;
	int	refno;
	void	*dlptr;
	const char *idstring;
	TAILQ_ENTRY(dlmodtbl_entry) dlmodtbl_entries;
};

struct dlcmdtbl_entry {
	char		*c_str;
	unsigned long	c_hash;
	void 		(*c_fun) __P((int, char *));
	unsigned	c_flags;
	char		*c_help;
	char		*c_func;
	struct dlmodtbl_entry	*dme;
	TAILQ_ENTRY(dlcmdtbl_entry) dlcmdtbl_entries;
};

void	dlinit __P((void));
void	dlmodcmd __P((int, char *));
int	dlopencmd __P((char *));
int	dlcaddcmd __P((char *, char *, struct dlmodtbl_entry *, char *,
								unsigned));
void	dlinfocmd __P((struct dlmodtbl_entry *));
void	dlhelpcmd __P((char *));
void	dlcmdlistcmd __P((void));
int	dlcdelcmd __P((char *));
int	dlclosecmd __P((struct dlmodtbl_entry *));
int	dlfuncrun __P((char *, int, char *));

#endif		/* ! TIRC_DL_H */
