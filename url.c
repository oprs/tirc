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
static char rcsid[] = "$Id: url.c,v 1.14 1998/02/02 03:41:29 mkb Exp $";
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
#ifdef	HAVE_CTYPE_H
#include <ctype.h>
#endif

#include "compat.h"
#include "tty.h"
#include "urlchtml.h"
#include "colour.h"

extern char     ppre[];
extern void     (*othercmd) __P((char *));
extern int      errno;

static void	done_urlfn __P((char *));

static FILE	*urlcfile;	/* the file where caught URLs are stored. */
static char	*urlcfilenm;	/* filename */
static int	catchurls;	/* if 1, we're currently catching */
static int	flushing;	/* if 1, we're flushing */

/*
 * User wants to start catching URLs.  If no filename specified, get one
 * from the editor line.
 */
void
urlstart(filename)
	char *filename;
{
	static char nprompt[] = "Enter URL catchfile name: ";

	if (filename != NULL && strlen(filename))
		done_urlfn(filename);
	else {
		set_prompt(nprompt);
		linedone(0);
		othercmd = done_urlfn;
	}
}

/*
 * Process filename input.
 */
static void
done_urlfn(filename)
	char *filename;
{
	struct stat statbuf;
	int r;
	char ul[MAXURLLINESIZE+1];
	int have_eoc = 0, were_flushing;
	long oldpos;

	/* lower the life expectancy of this ugly side effect */
	were_flushing = flushing;
	flushing = 0;

	othercmd = NULL;
	set_prompt(NULL);

	if (filename == NULL || !strlen(filename)) {
		iw_printf(COLI_TEXT, "%sNo catchfile specified.\n", ppre);
		elrefr(1);
		return;
	}

	filename = exptilde(filename);

	/* If we are already catching, close this one. */
	urlend();

#ifdef	DEBUGV
	iw_printf(COLI_TEXT, "DEBUG: done_urlfn: filename=\"%s\"\n", filename);
#endif

	r = stat(filename, &statbuf);

	if (r < 0 && errno != ENOENT) {
		iw_printf(COLI_WARN, "%s%sstat() returned error: %s%s\n", TBOLD, ppre,
			Strerror(errno), TBOLD);
		elrefr(1);
		return;
	}

	if (r < 0 || were_flushing) {
		/*
		 * File does not exist or needs to be cleaned.  Open it for
		 * r/w and create a basic html layout.
		 */
		if ((urlcfile = fopen(filename, "w+")) == NULL) {
			iw_printf(COLI_WARN, "%sCan't open %s: %s\n", ppre,
				filename, Strerror(errno));
			elrefr(1);
			return;
		}
		fwrite(templhead, 1, strlen(templhead), urlcfile);
		fwrite(templfoot, 1, strlen(templfoot), urlcfile);
	}
	else {
		/* Check for regular file. */
		if (! S_ISREG(statbuf.st_mode)) {
			iw_printf(COLI_WARN, "%sNot a regular file: %s\n",
				ppre, filename);
			elrefr(1);
			return;
		}
		/* Ok, open. */
		if ((urlcfile = fopen(filename, "r+")) == NULL) {
			iw_printf(COLI_WARN, "%sCan't open %s: %s\n", ppre,
				filename, Strerror(errno));
			elrefr(1);
			return;
		}
	}

	/*
	 * Go through the file and find the <!--EOC--> mark which denotes
	 * the end of caught urls.  If it can't be found, issue an error
	 * and close the file.  Otherwise, position at this marker, for
	 * adding incoming urls.
	 */
	rewind(urlcfile);
	while (!feof(urlcfile)) {
		oldpos = ftell(urlcfile);
		if (fgets(ul, MAXURLLINESIZE, urlcfile) == NULL &&
		    !feof(urlcfile)) {
			iw_printf(COLI_WARN, "%sError reading URL catch \
file\n", ppre);
			fclose(urlcfile);
			elrefr(1);
			return;
		}
		if (strstr(ul, "<!--EOC-->") != NULL) {
			have_eoc = 1;
			break;
		}
	}

	if (!have_eoc) {
		iw_printf(COLI_WARN, "%sExisting URL catch file is \
corrupted\n", ppre);
		fclose(urlcfile);
		elrefr(1);
		return;
	}

	fseek(urlcfile, oldpos, SEEK_SET);
	catchurls = 1;
	urlcfilenm = chkmem(Strdup(filename));

	iw_printf(COLI_TEXT, "%sCatching URLs to %s\n", ppre, filename);

	if (!were_flushing)
		elrefr(1);
}

/*
 * User is finished with catching URLs to this file.
 */
void
urlend()
{
	if (catchurls) {
		catchurls = 0;
		fclose(urlcfile);
		free(urlcfilenm);
		iw_printf(COLI_TEXT, "%sURL catch file closed\n", ppre);
	}
}

/*
 * Check the line for URLs if catching is activated, and add them to the
 * catch file.
 */
void
urlcheck(line, prefix)
	char *line, *prefix;
{
	char *eou, *hlink, *urlstring;
	int l, found = 0;
	long addpos;

	if (!catchurls)
		return;

	eou = line;

	do {
		if ((urlstring = strstr(eou, "http://")) != NULL ||
		    (urlstring = strstr(eou, "ftp://")) != NULL ||
		    (urlstring = strstr(eou, "gopher://")) != NULL ||
		    (urlstring = strstr(eou, "news://")) != NULL ||
		    (urlstring = strstr(eou, "mailto:")) != NULL
		    || (urlstring = strstr(eou, "saft://")) != NULL) {
			for (eou = urlstring; *eou != '\0' && !isspace(*eou) &&
			    *eou != '>'; eou++)
				;
			l = eou - urlstring;

			hlink = chkmem(malloc((unsigned)l+1));
			memmove(hlink, urlstring, (unsigned)l);
			hlink[l] = '\0';

			fprintf(urlcfile, "<dt><a href=\"%s\">%s</a>\n",
				hlink, hlink);
			fprintf(urlcfile, "<dd>From: %s<br>``%s''\n", prefix,
				line);
			found++;
			free(hlink);
		}
	} while (urlstring != NULL);

	if (found > 0) {
		addpos = ftell(urlcfile);
		fwrite(templfoot, 1, strlen(templfoot), urlcfile);
		fflush(urlcfile);
		fseek(urlcfile, addpos, SEEK_SET);
	}
}

/*
 * Remove all URLs from the catch file.
 */
void
urlflush()
{
	if (catchurls) {
		flushing = 1;
		done_urlfn(urlcfilenm);
	}
	else
		iw_printf(COLI_TEXT, "%sI am not catching URLs at the \
moment\n", ppre);
}

