AUTOMAKE_OPTIONS = subdir-objects
ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

AM_CPPFLAGS = -DCONF_FILE='"$(sysconfdir)/tls.conf"'

bin_PROGRAMS = lobbyserv
lobbyserv_SOURCES = src/client.c src/main.c

confdir = $(sysconfdir)
conf_DATA = data/tls.conf

#TESTS = lobbyserv
EXTRA_DIST = data

dist_noinst_SCRIPTS = autogen.sh
