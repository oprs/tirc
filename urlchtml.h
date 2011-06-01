/*
 * tirc -- client for the Internet Relay Chat
 *
 *	Copyright (c) 1996, 1997, 1998
 *		Matthias Buelow.  All rights reserved.
 *
 *	See the file ``COPYRIGHT'' for the usage and distribution
 *	license and warranty disclaimer.
 *
 * $Id: urlchtml.h,v 1.3 1998/01/20 01:16:22 token Exp $
 */

#ifndef TIRC_URLCHTML_H
#define TIRC_URLCHTML_H	1

static char templhead[] = "\
<!DOCTYPE html PUBLIC \"-//IETF//DTD HTML//EN\">\n\
<html>\n\
<title>TIRC Caught URLs</title>\n\
<body>\n\
<h3>TIRC Caught URLs</h3>\n\
<hr>\n\
<dl>\n\
";

static char templfoot[] = "\
<!--EOC-->\n\
</dl>\n\
<hr>\n\
</body>\n\
</html>\n\
";

#endif	/* !TIRC_URLCHTML_H */

