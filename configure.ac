# Initialization
AC_PREREQ([2.69])
AC_INIT([obmc-console], 1.0, [https://github.com/openbmc/obmc-console/issues])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([subdir-objects -Wall -Werror foreign dist-xz])
AM_SILENT_RULES([yes])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Checks for libraries.

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
LT_INIT # Removes 'unrecognized options: --with-libtool-sysroot'

# Checks for gtest specifics.
AX_PTHREAD([GTEST_CPPFLAGS="-DGTEST_HAS_PTHREAD=1"],[GTEST_CPP_FLAGS="-DGTEST_HAS_PTHREAD=0"])
AC_SUBST(GTEST_CPPFLAGS)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
