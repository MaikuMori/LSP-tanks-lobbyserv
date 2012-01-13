AUTOMAKE_OPTIONS = subdir-objects
ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

AM_CPPFLAGS = -DCONF_FILE='"$(sysconfdir)/lobbyserv.conf"'
AM_CPPFLAGS += -DRUNNING_DIR='"$(bindir)"'
AM_CPPFLAGS += -DLOCK_FILE='"$(sysconfdir)/lobbyserv.pid"'

bin_PROGRAMS = lobbyserv
bin_PROGRAMS += lobbytest
lobbyserv_SOURCES = src/client.c src/main.c src/lobby.h src/client.h
lobbytest_SOURCES = src/tests/test_client.c src/lobby.h

confdir = $(sysconfdir)
conf_DATA = data/lobbyserv.conf

#TESTS = lobbyserv
EXTRA_DIST = data

dist_noinst_SCRIPTS = autogen.sh
