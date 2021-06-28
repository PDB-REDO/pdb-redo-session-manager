AC_PREREQ([2.69])

m4_define([prsmd_version_major],[1])
m4_define([prsmd_version_minor],[0])
m4_define([prsmd_version_micro],[1])
m4_define([prsmd_version_extra],[])
m4_define([prsmd_version],[prsmd_version_major().prsmd_version_minor().prsmd_version_micro()prsmd_version_extra])

AC_INIT([prsmd], [prsmd_version], [m.hekkelman@nki.nl])

dnl Switch to a C++ compiler, and check if it works.
AC_LANG(C++)
AX_CXX_COMPILE_STDCXX_17([noext])

AX_CHECK_COMPILE_FLAG([-fstandalone-debug],
	[
		CXXFLAGS="$CXXFLAGS -fstandalone-debug"
	] , , [-Werror])

AC_CONFIG_SRCDIR([src/prsmd.cpp])
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_MACRO_DIR([config/m4])
AC_CONFIG_HEADERS([src/prsm-config.hpp])

AC_PREFIX_DEFAULT(/usr/local)

dnl AC_DEFUN([read_test], [AC_LANG_SOURCE(
dnl	esyscmd(tools/m4esc.sh config-tests/$1))])

AC_PROG_INSTALL
PKG_PROG_PKG_CONFIG

AX_PTHREAD

AC_ARG_VAR([DEBUG], [Build a debug version of the application])

AX_MRC

if test "x$MRC" = "x"; then
	AC_MSG_ERROR([mrc not found, the application can not be built])
fi

AC_CHECK_PROG([HAVE_YARN], [yarn], [yes],
	[AC_MSG_ERROR([yarn was not found, it is required to package the javascripts.])])
AX_FILESYSTEM

AX_BOOST_BASE([1.65.1], [], [AC_MSG_ERROR([Could not find a recent version of boost])])
AX_BOOST_IOSTREAMS
AX_BOOST_PROGRAM_OPTIONS
AX_BOOST_DATE_TIME
AX_BOOST_REGEX

AX_IOSTREAMS_Z
AX_IOSTREAMS_BZ2

AX_LIBZEEP
AX_URIPARSER

AX_PKG_CHECK_MODULES([PQXX], [libpq libpqxx], [], [], [AC_MSG_ERROR([the required package libpqxx-dev is not installed])])

dnl Process Makefile.in to create Makefile
AC_OUTPUT([GNUmakefile])
