dnl                                               -*- Autoconf -*-
dnl Process this file with autoconf to produce a configure script.

AC_INIT([fdk-aac-dabplus], [0.1.0], [http://opendigitalradio.org/])
AC_CONFIG_AUX_DIR(.)
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([tar-ustar foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl Various options for configure
AC_ARG_ENABLE([example],
            [AS_HELP_STRING([--enable-example],
                [enable example encoding program (default is no)])],
            [example=$enableval], [example=no])

dnl Automake conditionals to set
AM_CONDITIONAL(EXAMPLE, test x$example = xyes)

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_LIBTOOL
LT_INIT

AC_CHECK_LIB([m], [sin])

# fdk-aac-dabplus-zmq needs ZeroMQ
AC_ARG_ENABLE([zmq],
        [AS_HELP_STRING([--enable-zmq], [Enable fdk-aac-dabplus-zmq with ZeroMQ output])]
		)

AS_IF(test "x$enable_zmq" = "xyes",
        AC_CHECK_LIB(zmq, zmq_init, , AC_MSG_ERROR(ZeroMQ libzmq is required)))

AM_CONDITIONAL(HAVE_ZEROMQ_TEST, test "x$enable_zmq" = "xyes")

dnl soname version to use
dnl goes by ‘current[:revision[:age]]’ with the soname ending up as
dnl current.age.revision
FDK_AAC_VERSION=0:4:0

AS_IF([test x$enable_shared = xyes], [LIBS_PRIVATE=$LIBS], [LIBS_PUBLIC=$LIBS])
AC_SUBST(FDK_AAC_VERSION)
AC_SUBST(LIBS_PUBLIC)
AC_SUBST(LIBS_PRIVATE)

AC_CONFIG_FILES([Makefile
                 fdk-aac.pc])
AC_OUTPUT
