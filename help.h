/*
 * tirc -- client for the Internet Relay Chat
 *
 *	Copyright (c) 1996, 1997, 1998
 *		Matthias Buelow.  All rights reserved.
 *
 *	See the file ``COPYRIGHT'' for the usage and distribution
 *	license and warranty disclaimer.
 *
 * $Id: help.h,v 1.42 1998/02/24 18:30:16 mkb Exp $
 */

#ifndef TIRC_HELP_H
#define TIRC_HELP_H	1

char helphelp[] = 
"/HELP <command>\
\nDisplays a brief help text on the specified command explaining its\
\npurpose and syntax.  Help is available on the following commands:\n\
ADMIN  \
    ALIAS  \
    AWAY   \
    BYE    \
    CLEAR  \
    CLIST  \
\n\
CLOSE  \
    CMDCH  \
    CNAMES \
    COLOUR \
    CONNECT\
    CTCP   \
\n\
DATE   \
    DCC    \
    DEOP   \
    DESC   \
    DIE    \
    DLMOD  \
\n\
EXIT   \
    IGN    \
    IGNORE \
    INFO   \
    INVITE \
    ISON   \
\n\
JOIN   \
    KB     \
    KEYS   \
    KICK   \
    KILL   \
    LAME   \
\n\
LART   \
    LASTLOG\
    LEAVE  \
    LINKS  \
    LIST   \
    LOG    \
\n\
LUSERS \
    M      \
    MSG    \
    MODE   \
    MOTD   \
    N      \
\n\
NAMES  \
    NCOL   \
    NICK   \
    NOTICE \
    OOD    \
    OP     \
\n\
OPER   \
    PAGE   \
    PARSE  \
    PART   \
    PING   \
    QK     \
\n\
QUERY  \
    QUIT   \
    RAW    \
    REHASH \
    RESTART\
    SAY    \
\n\
SERVER \
    SERVLIST\
   SET    \
    SIGNAL \
    SIGNOFF\
    SOURCE \
\n\
SPAM   \
    SQUERY \
    SQUIT  \
    STATS  \
    SUMMON \
    SYSTEM \
\n\
TIME   \
    TIRC   \
    TRACE  \
    TOPIC  \
    UHOST  \
    UMODE  \
\n\
UPTIME \
    URL    \
    USERS  \
    VERSION\
    W      \
    WALLOPS\
\n\
WHO    \
    WHOIS  \
    WHOWAS \
    WIN    ";

char adminhelp[] =
"/ADMIN [<server>]\
\nDisplay the server's administrative contact.";

char awayhelp[] =
"/AWAY [<away message>]\
\nMarks you as being away.  The away message is added to the away\
\nnotification every user gets if messaging you or doing a whois.\
\nTimestamping of more or less interesting messages is activated\
\nand BEEP is enabled (see /SET).\
\nIf you omit the away message, the server marks you as no longer\
\nbeing away.";

char quithelp[] =
"/QUIT [<comment>]\
\nExit from IRC with the optional comment and terminate tirc.\
\n/QUIT, /EXIT, /SIGNOFF and /BYE are the same.  \
See also: /CLOSE.";

char clearhelp[] =
"/CLEAR\
\nClears the current window.  Redraw with ^L or ^R.";

char cmdchhelp[] =
"/CMDCH <character>\
\nChange your command prefix character (by default this is '/')";

char datehelp[] =
"/DATE [<server>]\
\nRequest date and time from the specified server or the server you\
\nare currently on if none specified.  /DATE and /TIME are \
identical.";

char ignorehelp[] =
"/IGNORE <function> {<spec>|<identifier>}\
\nPerform operation on the ignore-list.  This list contains source\
\ndescriptions of the form nick!user@some.domain from which tirc then\
\nwill ignore PRIVMSG, NOTICE and some other lifesigns that could annoy\
\nyou.  The wildcards * and ? are allowed, * means 0 or more characters\
\nmatch, ? means one character matches.\
\n\
\nFunction is one of the following:\
\nADD   Add a source description (spec) to the ignore list.\
\nDEL   Remove an entry from the ignore list.  You have to specify the\
\n      numerical index of the entry in the list (identifier).\
\nLIST  Print the ignore-list with the numerical index for each entry.\
\n\
\nCTCP requests and answers come in PRIVMSGs and NOTICEs and are also\
\ndiscarded from persons who are blacklisted.";

char infohelp[] =
"/INFO <server>\
\nCauses the specified server to display credits about IRC implementors\
\nand the server uptime.";

char invitehelp[] =
"/INVITE <nickname> <channel>\
\nInvites the specified user to the channel.  Useful if a channel is\
\ninvite-only (+i).  The user gets a notification about the invitation.";

char joinhelp[] = 
"/JOIN <channelname> [<key>]\
\nJoin the specified channel in the current window.\
\nIf the channel is +k, you have to specify the join key.\
\nIf you're already on the channel, you can use /JOIN to raise a \
channel\
\nto the top in the current window or to move it between windows.";

char parthelp[] = 
"/PART <channelname> [<comment>]\
\nLeave the specified channel with an optional comment (IRC 2.9 and\
\nhigher).  The channel is also removed from the window it is in and\
\nwindow modes are reset if it was the last channel in the window.\
\n/PART and /LEAVE are the same.";

char linkshelp[] =
"/LINKS [[<remote server>] <server mask>]\
\nThe server lists all servers it knows of matching server mask.\
\nIf server mask is omitted, all links are matched.";

char listhelp[] =
"/LIST [<channel>[,<channel>] [<server>]]\
\nList channels and their topics.  Unless you are on these channels,\
\nprivate channels are listed as Prv without topic and secret channels\
\nare not listed at all.";

char lusershelp[] =
"/LUSERS [<wildcard> [<server>]]\
\nCount users, servers and operators etc. on the server that match an.\
\noptional wildcard.";

char msghelp[] =
"/MSG <target> <text>\
\nSends a PRIVMSG to the specified channel or user.  /M is a shortcut\
 for\
\n(the already terse) /MSG.";

char noticehelp[] =
"/NOTICE <target> <text>\
\nSends a NOTICE to the specified channel or user.";

char modehelp[] =
"/MODE {<channel>|<nick>} {+|-}<modechars> [<parameters>]\
\nChanges your personal mode or the mode of a channel.\
\n`+' activates the particular mode, `-' removes it.\
\n\
\nChannel modes are the following:\
\n+m          Moderated.  Only chanops and users with voice (+v) can\
\n            send to the channel.\
\n+s          Secret.  The channel is not listed to users that are not\
\n            on the channel.\
\n+p          Private.  The channel is listed as Prv and without topic\
\n            for users that are not on the channel.  On some servers,\
\n            the channel is not shown on a /WHOIS at all.\
\n+l <number> Channel is limited to number users.\
\n+t          Topic-limited.  Only channel ops can change the topic.\
\n+k <key>    Key-locked. The specified key is required to join the\
\n            channel.\
\n+a          Anonymous.  Only works for local (&) channels.\
\n+o <nick>   Chanop.  Gives the specified nickname channel operator\
\n            privileges.\
\n+i          Invite-only.  Users who want to join the channel need an\
\n            invitation (see /INVITE).\
\n+v          Voice.  Allows users who are not chanops to speak on a\
\n            moderated channel.\
\n+n          Message-limited.  No message to the channel is allowed\
\n            from a user who is not on the channel.  Useful against\
\n            spamming.\
\n+b <mask>   Ban.  Set a ban mask to keep users out of the channel.\
\n            The ban mask is composed of nickname!user@host.domain\
\n            where user may be preceeded by the server with ~^-+\
\n            characters, indicating certain restrictions or ident trust\
\n            levels.  * and ? may be used as wildcards.  It is generally\
\n            not useful to ban on nicknames.  Redundant bans will not\
\n            be added by the server.\
\n+e <mask>   (IRC 2.10) ban exclusion, this specifies a user/host-mask\
\n            that will be excluded from a ban.\
\n+I <mask>   (IRC 2.10) invitation list, this specifies a user/host-mask\
\n            that will not be subject to +i.\
\nUser modes are the following:\
\n+o          IRC operator.  You cannot set this directly.  Use /OPER\
\n            for that purpose.  However, -o might work.\
\n+i          Invisible.  People run into you on the street.\
\n            This only affects visibility to users that are not on a\
\n            channel with you.  It doesn't make you non-existent for\
\n            those, you just don't show up on listings for them.\
\n            (useful against spam bots, also see /SPAM for that\
\n            purpose).\
\n+w          Receive wallops (messaging to all operators).  Note that\
\n            many servers have wallops disabled due to earlier abuse.\
\n+s          Receive server notices.\
\n+r          Restricted.  You cannot get channel op or change your\
\n            nickname.  This mode can only be added, not removed.\
\n            Servers often restrict users that lack reverse dns lookup\
\n            or those who come from somewhat untrusted or foreign\
\n            domains.\
\nSee also: /UMODE.";

char motdhelp[] =
"/MOTD {<server>|<nickname>}\
\nDisplays the server's message-of-the-day file.  If a nickname is\
\nspecified, this user's server is queried.";

char nameshelp[] =
"/NAMES [<channel>[,<channel>]]\
\nShow nicknames on channels that are visible to you.  If no channels\
\nare specified, all nicknames on all visible channels are shown.\
\n/N is a shortcut for /NAMES.";

char nickhelp[] =
"/NICK <nickname>\
\nChange your nickname to a new one.  Maximum size is 9 characters.\
\nAllowed characters are US-ASCII alpha-numerical characters and in\
\naddition {}[]|\\_^.  Nicknames are case-insensitive.";

char queryhelp[] =
"/QUERY [<nickname>]\
\nStarts a private conversation in the current window.  This works\
\nlike talking to a channel.  Using /QUERY without a nickname deletes\
\nthe query \"channel\".";

char serverhelp[] =
"/SERVER <hostname> [<port>] [<password>]\
\nLink up to the specified IRC server.  Quits an existing connection.\
\nhostname is the DNS name of the server, port is the TCP port on which\
\nthe server is listening and password is your I-line password.\
\nPort and password are optional if port is the same as with the current\
\nconnection and password is the same (or not required).";

char closehelp[] =
"/CLOSE [<comment>]\
\nClose the connection with the IRC server.  Unlike /QUIT. this does\
\nnot terminate tirc.";

char statshelp[] =
"/STATS [<query>] [<server>]\
\nAsk for server statistics.  Query can be one of the following:\
\nc    Show C/N lines (servers that may connect or that the server can\
\n     connect to).\
\nh    Show H/L lines (hubs and leaves).\
\nk    Show K lines (banned user/hostname combinations).\
\ni    Show I lines (hosts that are allowed to connect).\
\nl    Shows traffic and duration of server and client connections.\
\nm    Returns a list of commands supported by the server with usage\
\n     statistics.\
\no    Show O lines (hosts of which users can become operators).\
\ny    Show Y lines (Y lines define connection classes).\
\nu    Server uptime.";

char summonhelp[] =
"/SUMMON <user> [<server>]\
\nSummon a user on the server to IRC.  The user must actually be\
\nlogged in on the server machine and allow writing to his terminal\
\n(mesg y).  Most irc servers don't support this.";

char topichelp[] =
"/TOPIC <channel> [<newtopic>]\
\nQueries or sets the channel topic.  If the channel is +t (topic-\
\nlimited), only channel ops can change the topic.";

char usershelp[] =
"/USERS [<server>]\
\nReturn a list of users currently logged in on the server machine.\
\nMany servers have disabled this feature due to security reasons.";

char versionhelp[] =
"/VERSION [<server>]\
\nReturn the version of the ircd running as server.";

char whohelp[] = 
"/WHO <spec>\
\nList users that match the specification.  If spec is a channel, match\
\nall users on that channel.  If no specification is given, all users\
\non the network are shown.";

char whoishelp[] =
"/WHOIS [<server>] <nickmask>[,<nickmask>[,...]]\
\nQuery information about particular users.  If server is specified, the\
\nquery will be sent to that specific server.  This is useful to get\
\ninformation that only exists on the server the user is on (for example\
\nidle time).  /W is a shortcut for /WHOIS.";

char whowashelp[] =
"/WHOWAS <nickname> [<count> [<server>]]\
\nSpit out the server's nickname history entries on the given nickname.\
\nIf there is more than one entry, limit the replies to count.";

char winhelp[] =
"/WIN <function>\
\nManage tirc windows.  Function is one of the following:\
\nNEW   Create a new window.  CTRL+X-2 is a shortcut for this.\
\nDEL   Delete the current window.  CTRL+X-0 is a shortcut.\
\nRES {+|-|=}n\
\n      Resize the window by n lines. = makes all windows the same height.\
\nMODE {+|-}CSPD\
\n      Change window mode.  C is receive channel traffic, S is control\
\n      messages, errors etc., and P is receive PRIVMSGs and NOTICEs.\
\n      If you set D (debug dump), you can look at all the communication\
\n      with the server in raw format.\
\nSTATUS\
\n      Query the current condition of the window.\
\nLOG [<filename>]\
\n      Log the window to the specified file.  If a logfile is already\
\n      open, tirc will ask you if you want to close it.\
\nYou can move the focus between windows with CTRL+X-O or CTRL+W.";

char ctcphelp[] =
"/CTCP <target> <ctcpcommand>\
\nSends the ctcpcommand string as a PRIVMSG to the specified target\
\n(channel or user).  CTCP is the client-to-client-protocol.  tirc\
\nsupports a subset of what other clients have here.  The ctcpcommand\
\nstring usually is one of the following:\
\n\
\nVERSION    May cause the other client(s) to send some version string\
\n           as a CTCP notice.\
\nPING       The other client responds with a CTCP PING notice.  tirc\
\n           time stamps the ping to determine the round trip time\
\n           for the PING.\
\nACTION     See /ME for CTCP ACTION to the current channel and\
\n           /DESC for actions directly to a person or channel.\
\nCLIENTINFO Returns the known CTCP commands of the other side.\
\nOP  <channel> [<password>]\
\n           Request channel operator status from a TIRC client where\
\n           you are registered via the op-on-demand mechanism.\
\nAnd some other stuff, depending on the irc client the receiver is\
\nusing.  Ctcpcommand can be chosen freely.  The first word is converted\
\nto upper case and the entire thing is put inside ^As";

char mehelp[] =
"/ME <some text>\
\nSends a CTCP ACTION message to the current channel.  This is used to\
\nimplement MUD-like actions, i.e. a third-person description of what\
\nyou might be doing or similar.";

char operhelp[] =
"/OPER [<nickname>] [<password>]\
\nGain IRC operator privileges.  You must have an O-line for your host\
\nand provide your operator password.\
\nIf you don't specify your password on the command line, tirc will\
\nprompt you without echo for the password.";

char sethelp[] =
"/SET <variable> <value>\
\nControls tirc behaviour by altering variables.  Boolean values are\
\nON and OFF.\
\n\
\nThe following variables are available:\
\nCTCP  {ON|OFF}    Enables/disables response to CTCP messages.\
\nSTAMP {ON|OFF}    Enables/disables timestamping for certain messages.\
\n                  If STAMP is off, timestamps will occur only if you\
\n                  are set away (/AWAY.)\
\nFLOODP {ON|OFF}   Turns flood protection on or off.  Relaxed flood\
\n                  protection is enabled by default.\
\nFLSTRICT {ON|OFF} Sets mode for flood protection to strict.  Login\
\n                  names are not looked at, only machine names.  Useful\
\n                  against clone-bots of which each one will use a\
\n                  different login name.\
\nFLIGNORE {ON|OFF} Specifies whether the offending source should be\
\n                  automatically added to the ignore-list when flooding\
\n                  has been detected.\
\nKBIGNORE {ON|OFF} If enabled, we automatically add the victim of a\
\n                  kickban to our ignore-list, we use the ban mask as\
\n                  ignore mask.\
\nBEEP {ON|OFF}     If on, users may beep you, that is ^G characters are\
\n                  not filtered out of PRIVMSGs.  This only applies to\
\n                  PRIVMSG and the beep is executed only once.  NOTICEs\
\n                  are cleaned of that nuisance.  /AWAY enables beep\
\n                  by default so you have to manually disable it if you\
\n                  don't want acoustic signals while you're away.\
\nCLOCK {ON|OFF}    Whether we display the current time in the status\
\n                  bar of the focussed window.\
\nHIMSG {ON|OFF}    If enabled, display the sender nickname of nonpublic\
\n                  PRIVMSG and NOTICE messages with bold attribute.\
\nBIFF {ON|OFF}     If BIFF is enabled, tirc checks your mailbox on a\
\n                  regular basis and notices you when new mail has\
\n                  arrived.\
\nXTERMT {ON|OFF}   If enabled, an xterm's title is changed to reflect\
\n                  the current server connection.\
\nPGUPDATE {ON|OFF} Show if something happened in hidden pages as a list\
\n                  in the status line.\
\nOOD {ON|OFF}      Enables or disables ChanOp-On-Demand processing.\
\nATTRIBS {ON|OFF}  When enabled, ircII inline terminal attributes\
\n                  (bold, underscore, inverse) are processed.\
\nCOLOUR {ON|OFF}   Toggles colourization of tty output with ANSI\
\n                  colour codes.\
\nSTIPPLE {ON|OFF}  If on, the status line is drawn with minus characters\
\n                  in addition to the reverse video attribute.  This is\
\n                  useful on terminals that cannot display reverse video\
\n                  properly.\
\nHIDEMCOL {ON|OFF} If enabled, mIRC-style colour attributes are filtered\
\n                  out completely in incoming messages.\
\nNCOLOUR {ON|OFF}  If on, do nickname colourization based on approximate\
\n                  string matching (see NCOL)\
\nNUMERICS {ON|OFF} If on, display a server message numeric instead of\
\n                  stars at the beginning of a line (if the server msg is\
\n                  tagged with a numerical command identifier).\
\nTELEVIDEO {ON|OFF} Disables all charcell attributes (reverse, dim, bold,\
\n                  standout, underscore) because my old tvi9050 can't do them\
\n                  properly.  For maximum usability on old ttys, enable\
\n                  CONF_STIPPLE and disable CONF_COLOURS.";

char pinghelp[] =
"/PING\
\nPing the server.  This is to allow the user to check if the server\
\nis responsive.";

char rawhelp[] = 
"/RAW <text>\
\nSend text unchanged to the server as a raw IRC command.";

char tirchelp[] =
"/TIRC\
\nDisplay tirc version info.";

char rehashhelp[] =
"/REHASH\
\n(Oper privileged) Causes ircd to re-read its configuration file.";

char diehelp[] =
"/DIE\
\n(Oper privileged) Terminate ircd.";

char kickhelp[] = 
"/KICK <channel> <user> [<comment>]\
\n(Channel Op) Kick the specified user out of the channel with an\
\noptional comment.  Also see /QK and /KB.";

char killhelp[] =
"/KILL <user> <comment>\
\n(Oper privileged) Remove a user from IRC. You must provide a reason\
\nfor that measure.";

char squithelp[] =
"/SQUIT <server>\
\n(Oper privileged) Unlinks the named server from the network.";

char restarthelp[] =
"/RESTART\
\n(Oper privileged) Restart ircd.";

char connecthelp[] =
"/CONNECT <serverX> [<port>] <serverY>\
\n(Oper privileged) Try to connect server X to Y on the optionally\
\nspecified port.";

char systemhelp[] =
"/SYSTEM [-msg <target>] <commandstring>\
\ntirc calls $SHELL or /bin/sh and executes the specified command line \
\nin the background.  The output is then displayed in the current window.\
\nInteractive programs cannot be executed.  You can only run one process\
\nat a time from tirc.  The process receives SIGINT if you press your\
\ninterrupt key so you can kill it if it doesn't terminate automatically.\
\nOutput from the executed command is normally printed in the current\
\nwindow.  However, you can route the output as privmsgs to a user or\
\nchannel with the -msg attribution.\
\nYou're encouraged to use /SYSTEM and not to suspend tirc since\
\nsuspending inhibits server PING answering.  Also see /SIGNAL.";

char clisthelp[] =
"/CLIST\
\nList the channels you are on.  This lists the client's channel\
\nlist, not the server's. [Thus it can be used for debugging, too].";

char wallopshelp[] =
"/WALLOPS <text>\
\nSend a message to all operators currently online.  Many servers don't\
\nsupport WALLOPS anymore.";

char tracehelp[] =
"/TRACE [<server>]\
\nTraces the route through the IRC network to the specified server.\
\nIf server is omitted, it shows the connections of the current server.";

char cnameshelp[] =
"/CNAMES\
\nPrint out the contents of the current channel's user name cache.";

char ophelp[] =
"/OP <nickname>[ <nickname> ...]\
\n(Channel Op) Give <nickname> channel op privileges on the current\
\nchannel (top channel in current window).  This is a shortcut for the\
\nrespective MODE command.  /OP checks if the user already has chanop\
\nprivileges by querying the local channel user cache so no unrequired\
\ntraffic will be generated.  Multiple nicknames can be given to /OP,\
\nseperated by whitespace.";

char deophelp[] =
"/DEOP <nickname>[ <nickname> ...]\
\n(Channel Op) Take channel op privileges from <nickname> on the current\
\nchannel.  This is the opposite of /OP.";

char qkhelp[] =
"/QK <nickname>[ <nickname> ...]\
\n(Channel Op) Kick the specified user(s) off the current channel.\
\nThis is a shortcut for /KICK and lets you quickly remove multiple\
\npests from your channel.  See /KB for an effortless way to keep a\
\nuser out.";

char kbhelp[] =
"/KB <nickname> [<comment>]\
\n(Channel Op) Bans the specified luser and then kicks it off the\
\ncurrent channel.  See also /QK and /KICK.  /LART is an alias for\
\n/KB with the exception that the default kick comment is different.\
\nIf KBIGNORE is enabled, the victim is also ignored with the ban mask.";

char isonhelp[] =
"/ISON <nickname>[ <nickname> ...]\
\nThe server returns a list of those nicknames that are online and\
\nmatch any of the nicks specified.";

char signalhelp[] =
"/SIGNAL <signal-number>\
\nSends the specified signal (numerical) to the program spawned with\
\n/SYSTEM.  See kill(1) for the numerical values of some useful signals.";

char lamehelp[] =
"/LAME <percentage>\
\nPrints a small, 1-line graph of the current lameness level, as\
\nspecified, to the focussed window's top channel.  Use with caution.";

char spamhelp[] =
"/SPAM <blacklist>\
\nPrivate messages and notices containing any of the (whitespace\
\ndelimited) words in blacklist are silently discarded.  Using /SPAM\
\nwith no parameters disables this feature.";

char dcchelp[] =
"/DCC {SEND|GET|CHAT|CLOSE|LIST|RENAME|RESUME}\
\nDCC is the direct-client-connection protocol.  It allows transferring\
\nfiles or typed text directly between clients without going through\
\nthe IRC network.\
\nThe following modes are available:\
\n\
\nSEND <nickname> <pathname>\
\n      Initiate a DCC FILE connection to the specified nickname's\
\n      client for sending the file specified with pathname.\
\nGET {<nickname>|<dcc-id>}\
\n      Get the first offered file from nickname or the one specified\
\n      with dcc-id.\
\nCHAT <nickname>\
\n      Initiate a DCC CHAT connection to the specified nickname's\
\n      client for exchanging messages directly between the clients.\
\n      Pending CHAT requests are accepted with DCC CHAT, too.  You\
\n      can send messages through the DCC connection then by typing\
\n      /msg =nickname (the = symbolizes the pipe {socket} character\
\n      of the connection.\
\nCLOSE {<nickname>|<dcc-id>}\
\n      Closes the first DCC connection with nickname or closes the\
\n      one specified with dcc-id.  This also rejects DCC offers.\
\nLIST\
\n      List active or pending DCC connections.\
\nRENAME <dcc-id> <newname>\
\n      Rename the save-filename from the default one (same as offered)\
\n      to the specified one.  This has to be done if files starting\
\n      with a dot are offered.\
\nRESUME {<nickname>|<dcc-id>}\
\n      Resume an interrupted DCC file transfer.  To do this, the offering\
\n      party sends a normal DCC SEND request.  Instead of DCC GET, you\
\n      use RESUME.  TIRC looks at the file size and sends a DCC RESUME\
\n      request to the peer telling the offering client to start at a\
\n      certain offset instead of trying to transmit the entire file again.\
\n      Both sides need to support the DCC RESUME protocol, unfortunately\
\n      only a minority of IRC clients support this (ircII for example does\
\n      not).\
\nNote: Unaccepted DCC offers get expired after some time and will be deleted\
\non the next DCC offer.  The period is 3 minutes.  This helps a bit against\
\nDCC flood attacks by cleaning up the flood turds after a while.";

char uhosthelp[] =
"/UHOST <nickname>[ <nickname>]...\
\nUHOST sends a USERHOST query to the server.  The server responds\
\nwith the login+hostnames associated with the specified nicks.";

char lastloghelp[] =
"/LASTLOG [<num>]\
\nLASTLOG displays the last <num> received private messages and\
\nnotices with a time stamp.  If <num> is not specified, LASTLOG\
\ndefaults to all messages/notices that you received since you\
\nare on IRC.  The lastlog is stored temporarily in the file \
\nlastlog.<pid> in your temp-directory.  Pressing 'q' in LASTLOG's\
\nMORE-prompt aborts the listing of further lastlog messages.";

char umodehelp[] =
"/UMODE <modechars>\
\nChanges user mode.  Same as MODE <nickname> <modechars>.";

char ignhelp[] =
"/IGN <nickname>\
\nGenerates an ignore mask in the same way as done in auto-ignore\
\nand kickban and adds the mask to the ignore list.";

char deschelp[] =
"/DESC <target> <description>\
\nSends a directed (== not necessarily to the current channel)\
\nCTCP ACTION message.  Useful for private `/ME.'.";

char keyshelp[] =
"A short overview of keybindings used in tirc:\
\nCtrl+W     Switch focus to next tirc window region when in command\
\n           mode.  In input/overstrike mode it is the vi erase-word\
\n           backward character.\
\nCtrl+T     Like Ctrl+W but switches backwards.\
\nCtrl+F     Page down in the current window's backscroll buffer.\
\nor PageDn\
\nCtrl+B     Page up in the current window's backscroll buffer.\
\nor PageUp\
\nCtrl+G     Go to end of window's backscroll (the current line).\
\nCtrl+L     Redisplay screen from the backscroll buffer.  Also removes\
\nor Ctrl+R  unlogged text like help information.\
\nCtrl+Z     Suspend tirc.  (depends on your susp/dsusp character).\
\nor Ctrl+Y\
\nCtrl+C     Interrupt blocking system calls or asks if you want\
\n           to exit tirc.\
\nTab        If at the beginning of an empty line, writes a response\
\n           template for the last user you received/send a message\
\n           from/to.  Pressing Tab several times then iterates through\
\n           a small history.\
\nCtrl+V     (lnext character) Insert the next character literally.\
\n           It might be required that you press Ctrl+V twice.  Control\
\n           values are displayed by a '_' in the command line.\
\nThe editor line looks and feels mostly like the vi(1) text editor,\
\nplease refer to \"man vi\" for an explanation on basic editing with\
\nvi.  However, don't expect the tirc command line to be fully compatible\
\nwith vi.  Some things are missing or different.  A few emacs(1)-like\
\nbindings have been added for insert/overwrite mode.\
\nCtrl+E     Go after the end of line, also sets input mode.\
\nCtrl+A     Go to beginning of line, automatically sets input mode.\
\nCtrl+D     Delete next character.\
\nCtrl+K     Delete to end of line.\
\nCursor keys  Go left, right in line and up down in the command line\
\n           history, respectively.\
\nBackspace  Deletes char left to cursor if your terminal type is set\
\n           correctly.\
\nCtrl+U     (or whatever you have as kill character) kills the line\
\n           as expected.  You can undo a kill or modification by\
\n           typing u in command mode.\
\nCtrl+P     Toggles paste mode.  In paste mode, commands are not recognized\
\n           and all text you type is sent to the top channel/query in the\
\n           current window.\
\n/          In command mode, this lets you search forward in the\
\n           current window's backscroll buffer.  Extended regular\
\n           expressions are matched.\
\n?          Like / but backwards.  This is what you probably want to\
\n           use if you want to search for the latest occurance first.\
\nn          (command mode) Find the next match for a previously entered\
\n           regex search string.\
\nN          Like n but search in opposite direction.\
\n:          Insert the command character at the beginning of the line\
\n           and set insert mode.\
\no or O     In command mode: invoke the options editor dialog.\
\nCtrl+X     Perform some window or page operations:\
\n Ctrl+X-o\
\n or Ctrl+N Switch to next window (like Ctrl+W in command mode).\
\n Ctrl+X-O  Switch to previous window (like Ctrl+T in command mode).\
\n Ctrl+X-2  Create new window (like /WIN NEW).\
\n Ctrl+X-0  Delete current window (if possible, like /WIN DEL).\
\n Ctrl+X-n  Switch to next page.\
\n Ctrl+X-p  Switch to previous page.\
\n Ctrl+X-b-<0-9>\
\n           Switch to page number 0-9.\
\n Ctrl+X-x  Switch between last visited page and current one.\
\nMiscellaneous:\
\nYou can abort 'more' prompts by typing q.\
\nSome notes one the editor line:\
\nThe command line parser splits the line on `;' characters.  That is,\
\nyou can write several commands in one line seperated with semicolons.\
\nIf the text contains a semicolon which is not intended as a seperator,\
\nyou must put the text into quotes (\"\").  This is required for example\
\nfor alias definitions that expand into multiple commands, seperated by\
\nsemicolons.  Command names must follow a semicolon without any\
\npreceding whitespace.  Text following MSG, M, NOTICE, ME, DESC and\
\nTOPIC is _not_ splitted on `;' and such needn't be quoted, in addition,\
\nquotes are treated as ordinary commands.  Text following all other \
\ncommands _is_ splitted at the next semicolon and may require\
\nappropriate quoting.";

char aliashelp[] = 
"/ALIAS [<aliasname>] [<expansion>]\
\nCreate an alias named <aliasname> that expands into the <expansion>\
\nstring and is then executed by the command parser.  Several commands\
\ncan be specified, seperated by `;'.  When the alias is called,\
\nparameters to the aliasname can be used by the expansion string.\
\nThe parameters are stored in the following variables:\
\n$0     -  entire command line string (including the alias name)\
\n$1..$n -  whitespace seperated arguments where $1 is the first argument\
\n$*     -  entire argument string\
\nCalling /ALIAS without specifying an expansion string deletes the\
\nalias.  Calling /ALIAS without arguments lists the currently memorized\
\naliases.  Aliases are parsed only once, that means the alias mechanism\
\nis not recursive.  So if you create an alias called JOIN, for example,\
\na command /JOIN inside the alias refers to the original JOIN.\
\nPlease refer to /HELP KEYS on how to quote text that is intended to\
\nexpand into a composed command line (several commands in one line).";

char sayhelp[] =
"/SAY <text>\
\nWrite text as a privmsg to the current channel.  This is useful in\
\naliases when you want to write something to the channel.";

char loghelp[] = 
"/LOG {MSG|[<channel>]} [<logfile>]\
\nLogs traffic on the specified channel to the file logfile.\
\nMSG instead of a channel name logs privmsgs and notices to the logfile.\
\nIf logfile is omitted, an existing logfile is closed, or you are \
\nprompted for a filename.\
\nIf channel is not specified, all open logfiles are shown.";

char pagehelp[] = 
"/PAGE {<command>|<number>}\
\nManages tirc pages that contain window regions.  Command is one of\
\nthe following:\
\nNEW     Create a new page and switch to it.\
\nDEL     Deletes the current page if it doesn't contain any windows.\
\nSpecifying a number switches to the respective page.  A shortcut for\
\nthat is CTRL+X-b-<0-9>, where <0-9> specifies the page number, starting\
\nwith 0, up to the tenth page, number 9.  CTRL+X-n switches to the\
\nnext page, CTRL+X-p to the previous one.  CTRL+X-CTRL+X toggles between\
\nthe current and the last visited page.";

char oodhelp[] =
"/OOD [<command> {<id>|<oline>}]\
\nWithout arguments, shows all ChanOp-On-Demand lines.\
\nCommand can be either ADD or DEL, adding or removing an ood-line\
\nrespectively.  An ood-line has the following format:\
\n  user@host.domain:[password]:channel[,channel...]\
\nwhere filling in the password field is optional.  Wildcards (*, ?) are\
\nallowed, whitespace is _not_ permitted.\
\nChannel op is requested by sending a CTCP OP channelname [password]\
\nprivmsg to the client.  Channel op is granted if u@h matches with the\
\nmessage sender as resolved by the server, an optional password is\
\nverified and the requested channel exists in the ood-line's channel\
\nlist.  You naturally must have channel operator status on the channel\
\nyourself.\
\nTo add an entry, use /OOD ADD <line>.  To delete an entry, specify\
\n/OOD DEL and the id as shown on adding the line or with /OOD.\
\nUse this feature with caution.  See also: /CTCP and /SET.";

char urlhelp[] =
"/URL <command> [<filename>]\
\nManages the URL (Universal Resource Locator) catching mechanism.\
\nThis allows you to automatically snap URLs beginning with \"http://\",\
\n\"ftp://\", \"gopher://\", \"saft://\", \"mailto:\" and \"news:\".\
\nThe URLs are added to the catcher-file, as specified by filename\
\nwhen starting the catcher and are valid html so that you can point\
\nyour browser at it.  The URL entries are listed with the message line\
\nwhere they have been extracted from and who sent them.\
\nCommand is one of the following:\
\nSTART  Start catching URLs to filename.\
\nEND    End catching.  filename is closed.\
\nFLUSH  Remove all URLs from the current catch file.";

char sourcehelp[] =
"/SOURCE [<filename>]\
\nCauses TIRC to re-read the default config file ($HOME/.tircrc) or the\
\nfile specified and interprets all TIRC commands and config variable\
\nassignments.  The SOURCE command is not yet implemented.";

char uptimehelp[] =
"/UPTIME\
\nDisplays how long the client has been running and prints resource usage\
\nstatistics for the master client and for child processes.  The\
\nfollowing fields are printed:\
\n    u  -  time spent in user mode\
\n    s  -  time spent in kernel mode\
\n    accumulated real time\
\n    percentage user/system time\
\n    io -  number of input and output block operations\
\n    pf -  number of page faults\
\n    w  -  number of swaps\
\n    sm -  integral shared memory size\
\n    ud -  integral unshared data\
\n    us -  integral unshared stack";

char colourhelp[] =
"/COLOUR <type> <fg> <bg>\
\nSets colour values for the specified colour type.  This will only be\
\neffective when the client is configured with --with-ansi-colours and\
\ncolours are active (see SET).\
\nColour type can be one of the following:\
\n    text        - normal text\
\n    foc_status  - focussed status line\
\n    nfoc_status - non-focussed status line(s)\
\n    prompt      - user prompt\
\n    servmsg     - server messages\
\n    privmsg     - private messages excluding channel privmsg\
\n    chanmsg     - channel privmsg\
\n    ownmsg      - messages we send\
\n    dccmsg      - DCC chat messages\
\n    warn        - warnings and errors\
\n    action      - CTCP ACTION text\
\n    boldnick    - highlighted nicknames\
\n    help        - help text\
\nColour names can be one of the following, please note that background\
\ncolours usually can't be bright:\
\n    black, red, green, yellow, blue, magenta, cyan, white\
\nHighlighted (not for background):\
\n    brightblack, brightred, brightgreen, brightyellow, brightblue,\
\n    brightmagenta, brightcyan, brightwhite";

char parsehelp[] =
"/PARSE <server-message-string>\
\nParses the argument string as if it were a message received from the\
\nserver.  This command is for debugging purposes and only available\
\nif TIRC has been configured and compiled with --enable-debug.";

char ncolhelp[] =
"/NCOL [<function>] [<nick>|<id>] [<degree>] [<fg-colour>] [<bg-colour>]\
\nSets the automatic colourization of nicknames, with k-approximate matching\
\n(given the approximation degree).\
\nFor example, a degree of 0 specifies that the nicknames must be equal,\
\nwhile at a degree of (k =) 3, up to 3 characters may be different or\
\nmissing from either the pattern nickname or the message nickname.\
\nThe following functions are provided by this command:\
\n    ADD   - Adds a colour binding.  Uses the nick, degree, fg-colour and\
\n            bg-colour parameters.  For a listing of valid colour strings,\
\n            see /HELP COLOUR.\
\n    DEL   - Removes a colour binding.  Uses the numerical id parameter.\
\n    LIST  - List all currently active bindings.  No parameters.";

char dlmodhelp[] =
"/DLMOD [<command> {<options>}]\
\nInterface to TIRC's dynamic module interface.\
\nTIRC uses the runtime link editor through the dlopen(3) interface on\
\nsystems that support this method for dynamically binding modules into TIRC\
\nwhich are compiled as shared libraries.\
\nCommand may be: OPEN CADD CDEL INFO CMDS HELP CLOSE\
\n    OPEN  - Attemps to dynamically link the files specified by <options>\
\n            which is a space separated list of filenames.\
\n    CADD  - Attemps to bind a function in a dynamically linked function to\
\n            a user command. Takes <options> in the format:\
\n                <command name> <function name> <module>\
\n    CDEL  - Attemps to unbind functions from their commands. The <options>\
\n            are a space separated list of command names.\
\n    INFO  - If no options are given, this will list all loaded modules.\
\n            If there are options in the form of a space separated list of\
\n            module identifiers, it will print more detailed information\
\n            about each of the listed modules.\
\n    CMDS  - Lists all dynamic module functions that are bound to a command.\
\n    HELP  - If there are no options given, this will list all dynamic\
\n            commands with help available. If a topic is given as an option\
\n            then it will print the help for that command.\
\n    CLOSE - Attemps to close the files specified by <options> which is a\
\n            space separated list of module id's.\
\nWhere a module id is needed, a module's filename may be passed,\
\nproviding it is a unique filename. It is recommended to use the modules\
\nunique reference number instead of the shared library filename.\
\nNote: this interface basically works but is still under development.";

char squeryhelp[] =
"/SQUERY <service> <text>\
\n(IRC 2.9) Query a service.  The first argument is the service name, the\
\nsecond the text sent to the service.  The text format is service dependent.\
\nSee also: /SERVLIST.";

char servlisthelp[] =
"SERVLIST\
\n(IRC 2.9) List available IRC services.  See also: /SQUERY.";

#endif	/* !TIRC_HELP_H */

