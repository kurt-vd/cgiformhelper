
default: cgiformhelper

PREFIX	=/usr/local
VERSION	:= $(shell ./getlocalversion .)

CPPFLAGS= -DVERSION=\"$(VERSION)\"
CFLAGS	= -Wall -g3 -O0

install: cgiformhelper
	install $< $(DESTDIR)$(PREFIX)/bin

