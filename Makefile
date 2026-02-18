#
# SPDX-FileCopyrightText: Copyright (C) 1987, 1988 Chuck Simmons
# SPDX-License-Identifier: GPL-2.0+
#
# See the file COPYING, distributed with empire, for restriction
# and warranty information.

PREFIX      ?= /usr/local
BINDIR      ?= $(PREFIX)/bin
DATADIR     ?= $(PREFIX)/share
MANDIR      ?= $(DATADIR)/man

VERSION=$(shell sed -n <NEWS '/^[0-9]/s/:.*//p' | head -1)

# Use -g to compile the program for debugging.
#DEBUG = -g -DDEBUG
DEBUG = -O2

# Use -p to profile the program.
#PROFILE = -p -DPROFILE
PROFILE =

LIBS = -lncurses

# You shouldn't have to modify anything below this line.

# There's a dynamic format in the object-display routines; suppress the warning
CFLAGS = $(DEBUG) $(PROFILE) -Wall -Wextra -Werror -Wno-format-security -Wno-unused-parameter

FILES = \
	attack.c \
	compmove.c \
	data.c \
	display.c \
	edit.c \
	empire.c \
	game.c \
	main.c \
	map.c \
	math.c \
	object.c \
	term.c \
	usermove.c \
	util.c

HEADERS = empire.h extern.h

OFILES = \
	attack.o \
	compmove.o \
	data.o \
	display.o \
	edit.o \
	empire.o \
	game.o \
	main.o \
	map.o \
	math.o \
	object.o \
	term.o \
	usermove.o \
	util.o

# Build

all: vms-empire

vms-empire: $(OFILES)
	$(CC) $(PROFILE) -o vms-empire $(OFILES) $(LIBS)

attack.o:: extern.h empire.h
compmove.o:: extern.h empire.h
data.o:: empire.h
display.o:: extern.h empire.h
edit.o:: extern.h empire.h
empire.o:: extern.h empire.h
game.o:: extern.h empire.h
main.o:: extern.h empire.h
map.o:: extern.h empire.h
math.o:: extern.h empire.h
object.o:: extern.h empire.h
term.o:: extern.h empire.h
usermove.o:: extern.h empire.h
util.o:: extern.h empire.h

empire.6: vms-empire.xml
	xmlto man vms-empire.xml

vms-empire.html: vms-empire.xml
	xmlto html-nochunks vms-empire.xml

clean:
	rm -f *.o TAGS vms-empire
	rm -f *.6 *.html

clobber: clean
	rm -f vms-empire vms-empire-*.tar*

# Validate

spellcheck:
	@spellcheck vms-empire.xml

# This should run clean
cppcheck:
	@cppcheck --quiet --inline-suppr --suppress=missingIncludeSystem --suppress=unusedFunction --template=gcc --enable=all --force *.[ch]

reflow:
	@clang-format --style="{IndentWidth: 8, UseTab: ForIndentation}" -i $$(find . -name "*.[ch]")

SOURCES = README.adoc HACKING NEWS control empire.6 vms-empire.xml COPYING Makefile BUGS AUTHORS $(FILES) $(HEADERS) vms-empire.png vms-empire.desktop

# Install/uninstall

install: empire.6 uninstall
	install -m 0755 -d $(DESTDIR)$(BINDIR)
	install -m 0755 -d $(DESTDIR)$(MANDIR)/man6
	install -m 0755 -d $(DESTDIR)/usr/share/applications/
	install -m 0755 -d $(DESTDIR)/usr/share/icons/hicolor/48x48/apps/
	install -m 0755 -d $(DESTDIR)/usr/share/appdata
	install -m 0755 vms-empire $(DESTDIR)$(BINDIR)/
	install -m 0644 empire.6 $(DESTDIR)$(MANDIR)/man6/vms-empire.6
	install -m 0644 vms-empire.desktop $(DESTDIR)/usr/share/applications/
	install -m 0644 vms-empire.png $(DESTDIR)/usr/share/icons/hicolor/48x48/apps/
	install -m 0644 vms-empire.xml $(DESTDIR)/usr/share/appdata/

uninstall:
	rm -f $(BINDIR)/vms-empire $(MANDIR)/man6/vms-empire.6
	rm -f /usr/share/applications/vms-empire.desktop
	rm -f /usr/share/icons/hicolor/48x48/apps/vms-empire.png
	rm -f /usr/share/appdata/vms-empire.xml

# Export

version:
	@echo $(VERSION)

vms-empire-$(VERSION).tar.gz: $(SOURCES)
	mkdir vms-empire-$(VERSION)
	cp -r $(SOURCES) vms-empire-$(VERSION)
	tar -czf vms-empire-$(VERSION).tar.gz vms-empire-$(VERSION)
	rm -fr vms-empire-$(VERSION)
	ls -l vms-empire-$(VERSION).tar.gz

dist: vms-empire-$(VERSION).tar.gz

release: vms-empire-$(VERSION).tar.gz vms-empire.html
	shipper version=$(VERSION) | sh -e -x

refresh: vms-empire.html
	shipper -N -w version=$(VERSION) | sh -e -x

# end
