AC_INIT([Tanks Lobby Server], [1.0], [MiksKalnins@MaikuMori.com], [tanks-lobby-server])
AC_PREREQ([2.59])
AM_INIT_AUTOMAKE([1.10 no-define])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC
AC_PROG_CC_C_O
AC_PROG_CC_STDC

AC_CONFIG_FILES([Makefile])

AC_CHECK_LIB(event, event_base_new, [], [AC_MSG_ERROR([Library "libevent" wasn't found])])
AC_CHECK_LIB(iniparser, iniparser_load, [], [AC_MSG_ERROR([Library "iniparser" wasn't found])])


AC_MSG_CHECKING([whether to build with debug information])
AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug],
        [enable debug data generation (def=no)])],
    [debugit="$enableval"],
    [debugit=no])
AC_MSG_RESULT([$debugit])

if test x"$debugit" = x"yes"; then
    AC_DEFINE([DEBUG],[],[Debug Mode])
    AM_CXXFLAGS="$AM_CXXFLAGS -g -Wall -Werror -Wno-uninitialized -O0"
else
    AC_DEFINE([NDEBUG],[],[No-debug Mode])
    AM_CXXFLAGS="$AM_CXXFLAGS -O3"
fi

AC_SUBST([AM_CXXFLAGS])

AC_OUTPUT
