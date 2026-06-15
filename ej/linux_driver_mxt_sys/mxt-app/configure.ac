#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([mxt-app], m4_esyscmd([tr -d '\n' < VERSION]),
        [nick.dyer@itdev.co.uk])
AM_INIT_AUTOMAKE([foreign subdir-objects -Wall])
AC_CONFIG_SRCDIR([src/libmaxtouch/libmaxtouch.c])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

AC_CANONICAL_HOST

AC_USE_SYSTEM_EXTENSIONS

# Detect Windows (MinGW) host for CLI build (USB-only)
build_windows=no
case $host_os in
  *mingw*|*msys*)
    build_windows=yes
    AC_MSG_NOTICE([Building for Windows (USB-only CLI)])
    ;;
esac
case $host in
  *mingw*)
    build_windows=yes
    AC_MSG_NOTICE([Building for Windows host $host (USB-only CLI)])
    ;;
esac
AM_CONDITIONAL([MXT_OS_WINDOWS], [test x$build_windows = xyes])

# Checks for programs.
AC_PROG_CC
m4_ifdef([AM_PROG_AR], [AM_PROG_AR]) dnl Workaround for earlier Automake
LT_INIT

# Check for usb support (required on Windows)
AC_CHECK_LIB([usb-1.0], [libusb_init], [libusb=true])
if test x$build_windows = xyes && test x$libusb != xtrue; then
  AC_MSG_ERROR([libusb-1.0 is required for Windows build. Install e.g. mingw-w64-x86_64-libusb])
fi
AM_CONDITIONAL([HAVE_LIBUSB], [test x$libusb = xtrue])

# Handle debug/release build
AC_ARG_ENABLE(debug,
AS_HELP_STRING([--enable-debug],
               [enable debugging, default: no]),
[case "${enableval}" in
             yes) debug=true ;;
             no)  debug=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
esac],
[debug=false])

AM_CONDITIONAL(DEBUG, test x"$debug" = x"true")

# Handle generation of man page
AC_ARG_ENABLE(man,
AS_HELP_STRING([--enable-man],
               [generate the man page, default: no]),
               enable_man=${enableval}, enable_man=no)
AM_CONDITIONAL(ENABLE_MAN, test x"$enable_man" = x"yes")

# Handle generation of odt page
AC_ARG_ENABLE(odt,
AS_HELP_STRING([--enable-odt],
               [generate the odt page, default: no]),
               enable_odt=${enableval}, enable_odt=no)
AM_CONDITIONAL(ENABLE_ODT, test x"$enable_odt" = x"yes")

# Checks for header files (skip Linux-only on Windows)
AC_HEADER_STDBOOL
AC_CHECK_HEADERS([stdint.h stdlib.h string.h limits.h malloc.h ctype.h])
if test x$build_windows != xyes; then
  AC_CHECK_HEADERS([arpa/inet.h fcntl.h netinet/in.h sys/ioctl.h sys/socket.h sys/statfs.h unistd.h netdb.h sys/time.h cmocka.h])
else
  AC_CHECK_HEADERS([unistd.h]) dnl MinGW provides a minimal unistd.h
fi

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T
AC_TYPE_SIZE_T

AC_PATH_PROG(CTAGS, ctags, /bin/true)

# Checks for library functions (skip socket/select on Windows)
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([strerror strncasecmp tolower setlocale])
if test x$build_windows != xyes; then
  AC_CHECK_FUNCS([bzero gettimeofday select socket asprintf])
else
  AC_CHECK_FUNCS([bzero])
  AC_DEFINE([_WIN32_WINNT], [0x0600], [Target Vista+ for MinGW])
fi

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
