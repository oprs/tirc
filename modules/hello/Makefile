#	$Id$

#CFLAGS	=$(INCLUDES) -c -fpic
#LDFLAGS	=-shared -call_shared -no_archive
#LDFLAGS	=-nostdlib -shared -Xlinker -Bshareable -Xlinker -Bdynamic

all:	hello.so

clean:
	rm -f hello.so *.o *core* so_locations

hello.so:	hello.o
	$(CC) $(LDFLAGS) -o hello.so hello.o

hello.o:	hello.c	../../dlapi.h
	$(CC) $(CFLAGS) -o hello.o hello.c

