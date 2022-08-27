dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(cc-tool, 0.26+, george-u@yandex.com)
AM_INIT_AUTOMAKE([no-dependencies foreign subdir-objects])
AM_SILENT_RULES([yes])
AC_CONFIG_MACRO_DIR([m4])
AC_PROG_LIBTOOL
AC_PROG_CXX

BOOST_REQUIRE([1.74])
BOOST_PROGRAM_OPTIONS([mt])
BOOST_SYSTEM([mt])
BOOST_REGEX([mt])
BOOST_FILESYSTEM([mt])
BOOST_FOREACH
BOOST_SMART_PTR
BOOST_STRING_ALGO
BOOST_SIGNALS2
BOOST_CRC

PKG_CHECK_MODULES([LIBUSB], [libusb-1.0 >= 1.0.0])

AC_CONFIG_FILES(Makefile)
AC_OUTPUT

