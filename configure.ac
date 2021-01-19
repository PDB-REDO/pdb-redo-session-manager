AC_PREREQ([2.69])

AC_INIT([prsmd], 1.0, [maarten@hekkelman.com])

dnl Switch to a C++ compiler, and check if it works.
AC_LANG(C++)
AX_CXX_COMPILE_STDCXX_17([noext])

AC_CONFIG_SRCDIR([src/prsmd.cpp])
AC_CONFIG_HEADERS([src/prsm-config.hpp])
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_MACRO_DIR([config/m4])

AC_PREFIX_DEFAULT(/usr/local)

dnl AC_DEFUN([read_test], [AC_LANG_SOURCE(
dnl	esyscmd(tools/m4esc.sh config-tests/$1))])

AC_PROG_INSTALL

AC_ARG_VAR([DEBUG], [Build a debug version of the application])
AC_ARG_VAR([MRC], [Specify a location for the mrc executable])

dnl We really want to use mrc
if test "x$MRC" = "x"; then
	AC_PATH_PROG([MRC], [mrc])
fi

if test "x$MRC" = "x"; then
	AC_MSG_ERROR([mrc not found, the application can not be built])
fi

AC_CHECK_PROG([HAVE_YARN], [yarn], [yes],
	[AC_MSG_ERROR([yarn was not found, it is required to package the javascripts.])])

PKG_PROG_PKG_CONFIG
AX_PKG_CHECK_MODULES([PQXX], [libpq libpqxx], [], [], [AC_MSG_ERROR([the required package libpqxx-dev is not installed])])
AX_PKG_CHECK_MODULES([ZEEP], [libzeep], [], [], [AC_MSG_ERROR([the required package libzeep-dev is not installed])])

AX_PTHREAD

AC_CHECK_HEADER([filesystem], [], [AC_MSG_ERROR([The file <filesystem> is missing, perhaps you should install a more recent libstdc++ implementation.])])

dnl check if we need stdc++fs as library
AC_TRY_LINK(
	[#include <filesystem>],
	[(void)std::filesystem::current_path();],
	[],
	[
		LIBS="$LIBS -lstdc++fs"

		AC_TRY_LINK(
			[#include <filesystem>],
			[(void)std::filesystem::current_path();],
			[],
			[
				AC_MSG_ERROR([Could not link filesystem])
			]
		)
	]
)

AX_BOOST_BASE([1.65.1], [], [AC_MSG_ERROR([Could not find a recent version of boost])])
AX_BOOST_ASIO
AX_BOOST_PROGRAM_OPTIONS
AX_BOOST_DATE_TIME
AX_BOOST_IOSTREAMS

dnl Process Makefile.in to create Makefile
AC_OUTPUT([GNUmakefile])
