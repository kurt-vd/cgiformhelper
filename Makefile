PROGS = cgiformhelper
PROGS += urlencode
default: $(PROGS)

PREFIX	=/usr/local
VERSION	:= $(shell ./getlocalversion .)

CFLAGS	= -Wall -g3 -O0

-include config.mk

CPPFLAGS+= -DVERSION=\"$(VERSION)\"

install: $(PROGS)
	install $^ $(DESTDIR)$(PREFIX)/bin

clean:
	rm -f $(PROGS)

