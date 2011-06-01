# Generated automatically from Makefile.in by configure.
#	$Id: Makefile.in,v 1.1 1998/02/24 00:32:14 token Exp $


DLCFLAGS=
DLLDFLAGS=

CC      =cc
INCLUDES=-I../..
CPPFLAGS=$(INCLUDES)  
CPP     =cc -E $(CPPFLAGS)
LINT    =
CXREF   =
OPTFLAG =-O2
CFLAGS  =$(OPTFLAG) $(INCLUDES)  
LDFLAGS =    -ltermcap
LINTFLAGS=$(CPPFLAGS) -g -s
CXFLAGS =-xref all -html $(CPPFLAGS)
prefix  =/usr/local
exec_prefix=${prefix}
bindir  =${exec_prefix}/bin
datadir =${prefix}/share
mandir  =${prefix}/man
INSTALL =/usr/bin/install -c
MKDEP   =/usr/bin/mkdep
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

