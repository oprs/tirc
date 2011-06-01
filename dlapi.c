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
static char rcsid[] = "$Id: dlapi.c,v 1.2 1998/02/24 18:30:16 mkb Exp $";
#endif

#define	TIRC_DL_IMPLEMENTATION	1
#include "dlapi.h"

#ifdef	WITH_DLMOD

extern char	version[], ppre[], osstring[];

void
tirc_get_strings(sinfo)
	struct tirc_sstat_info *sinfo;
{
	sinfo->si_ppre = ppre;
	sinfo->si_version = version;
	sinfo->si_osstring = osstring;
}

extern int	on_irc;
extern char	*srvnm, *srvdn;
extern int	sock, port;
extern time_t	t_uptime, t_connecttime;

void
tirc_get_connection_status(cstat)
	struct tirc_cstat_info *cstat;
{
	cstat->cs_is_conn = on_irc;
	cstat->cs_srv_dnsn = srvdn;
	cstat->cs_srv_ircn = srvnm;
	cstat->cs_srv_inaddr = get_irc_s_addr();
	cstat->cs_srv_sock = sock;
	cstat->cs_srv_port = port;
	cstat->cs_uptime = t_uptime;
	cstat->cs_connecttime = t_connecttime;
}

extern char	nick[], *realname;
extern int	umd, is_away;

void
tirc_get_user_status(ustat)
	struct tirc_ustat_info *ustat;
{
	ustat->us_realname = realname;
	ustat->us_nick = nick;
	ustat->us_umodes = umd;
	ustat->us_away = is_away;
}

#endif	/* WITH_DLMOD */

