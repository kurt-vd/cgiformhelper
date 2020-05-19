PROGS = cgiformhelper
PROGS += jsonexpand
PROGS += jsontofiles
PROGS += urlencode
default: $(PROGS)

PREFIX	=/usr/local
VERSION	:= $(shell ./getlocalversion .)

CFLAGS	= -Wall -g3 -O0

-include config.mk

CPPFLAGS+= -DVERSION=\"$(VERSION)\"

VPATH=jsmn
jsonexpand: CPPFLAGS+=-Ijsmn -DJSMN_PARENT_LINKS=1
jsonexpand: jsmn.o
jsontofiles: CPPFLAGS+=-Ijsmn -DJSMN_PARENT_LINKS=1
jsontofiles: jsmn.o

install: $(PROGS)
	install $^ $(DESTDIR)$(PREFIX)/bin

clean:
	rm -f $(PROGS)
	rm -f $(wildcard *.o)

