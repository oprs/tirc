/*-
 * tirc -- client for the Internet Relay Chat
 *
 *	Copyright (c) 1996, 1997, 1998
 *		Matthias Buelow.  All rights reserved.
 *
 *	See the file ``COPYRIGHT'' for the usage and distribution
 *	license and warranty disclaimer.
 *
 * $Id: dlapi.h,v 1.3 1998/02/24 18:30:16 mkb Exp $
 */

#ifndef TIRC_DLAPI_H
#define TIRC_DLAPI_H	1

#ifdef	TIRC_DL_IMPLEMENTATION
#include "tirc.h"
#include "compat.h"
#include "colour.h"
#else
#include <tirc.h>
#include <compat.h>
#include <colour.h>
#endif

struct tirc_sstat_info {
	char	*si_ppre;	/* string prepending most client messages */
	char	*si_version;	/* tirc version string */
	char	*si_osstring;	/* canonical host system name */
};

struct tirc_cstat_info {
	int	cs_is_conn;	/* 1 if connected to server, 0 if not */
	char	*cs_srv_dnsn;	/* server dns name */
	char	*cs_srv_ircn;	/* server irc network name */
	unsigned long cs_srv_inaddr;	/* server internet address */
	int	cs_srv_sock;	/* file descriptor of server conn. socket */
	int	cs_srv_port;	/* tcp port we are linked to */
	time_t	cs_uptime;	/* time of client startup */
	time_t	cs_connecttime;	/* time of last successful server connection */
};

struct tirc_ustat_info {
	char	*us_realname;	/* "real" name as registered with server */
	char	*us_nick;	/* currently used nickname */
	int	us_umodes;	/* user modes */
	int	us_away;	/* 1 if set away */
};

void	tirc_get_connection_status __P((struct tirc_cstat_info *));
void	tirc_get_user_status __P((struct tirc_ustat_info *));
void	tirc_get_strings __P((struct tirc_sstat_info *));

#endif	/* !TIRC_DLAPI_H */

