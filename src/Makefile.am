## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	@PACKAGE_CFLAGS@

bin_PROGRAMS = pnmixer

pnmixer_SOURCES = \
	support.c support.h \
	main.c main.h \
	hotkeys.c hotkeys.h \
	notify.c notify.h \
	alsa.c alsa.h \
	callbacks.c callbacks.h \
	prefs.c prefs.h \
	debug.h

pnmixer_LDADD = @PACKAGE_LIBS@ $(INTLLIBS)

