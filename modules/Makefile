# Generated automatically from Makefile.in by configure.
#	$Id: Makefile.in,v 1.1 1998/02/24 00:32:14 token Exp $


DLCFLAGS=
DLLDFLAGS=

CC      =gcc
INCLUDES=-I../..
CPPFLAGS=$(INCLUDES)  -D_BSD_SOURCE -D_POSIX_C_SOURCE=2 -D__USE_BSD -D__USE_BSD_SIGNAL 
CPP     =gcc -E $(CPPFLAGS)
LINT    =
CXREF   =
OPTFLAG =-O2
CFLAGS  =$(OPTFLAG) $(INCLUDES)  -Wall 
LDFLAGS =  -export-dynamic -ldl  -ltermcap
LINTFLAGS=$(CPPFLAGS) 
CXFLAGS = $(CPPFLAGS)
prefix  =/usr
exec_prefix=${prefix}
bindir  =${exec_prefix}/bin
datadir =${prefix}/share
mandir  =/usr/share
INSTALL =/usr/bin/install -c
MKDEP   =./mkdep.sh
DINST   =$(INSTALL) -m644

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<

.SUFFIXES: .ln .c
.c.ln:
		$(LINT) $(LINTFLAGS) -c $<

all:	hello.so

distclean: clean

clean:
	cd hello; $(MAKE) clean

hello.so:
	cd hello; $(MAKE)

