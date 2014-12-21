default: cgiformhelper

PREFIX	=/usr/local
VERSION	:= $(shell ./getlocalversion .)

CFLAGS	= -Wall -g3 -O0

-include config.mk

CPPFLAGS+= -DVERSION=\"$(VERSION)\"

install: cgiformhelper
	install $< $(DESTDIR)$(PREFIX)/bin

clean:
	rm -f cgiformhelper

