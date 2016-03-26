dnl                                               -*- Autoconf -*-
dnl Process this file with autoconf to produce a configure script.

AC_INIT([fdk-aac-dabplus], [1.0.0], [http://opendigitalradio.org/])
AC_CONFIG_AUX_DIR(.)
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([tar-ustar foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_CONFIG_HEADER([config.h])

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_LIBTOOL
LT_INIT

AX_CXX_COMPILE_STDCXX_11(noext,mandatory)

# std::thread requires pthread
AX_PTHREAD( [
            AC_DEFINE(HAVE_PTHREAD,1,[Define if you have POSIX threads libraries and header files.])
            CLIBS="$PTHREAD_LIBS $LIBS"
            CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
            LDFLAGS="$LDFLAGS $PTHREAD_CFLAGS"
            CC="$PTHREAD_CC"], [AC_MSG_ERROR([requires pthread])] )

AC_CHECK_LIB([m], [sin])

AC_SEARCH_LIBS([init_rs_char], [fec], [], [
  AC_MSG_ERROR([unable to find libfec])
])

AC_CHECK_LIB([rt], [clock_gettime], [], [AC_MSG_ERROR([library rt is missing])])

# Check for options
AC_ARG_ENABLE([alsa],
        AS_HELP_STRING([--enable-alsa], [Enable ALSA input]))

AC_ARG_ENABLE([jack],
        AS_HELP_STRING([--enable-jack], [Enable JACK input]))

AC_ARG_ENABLE([vlc],
        AS_HELP_STRING([--enable-vlc], [Enable libvlc input]))


AS_IF([test "x$enable_alsa" = "xyes"],
      [AM_PATH_ALSA(1.0.25)])
AS_IF([test "x$enable_alsa" = "xyes"],
       AC_DEFINE(HAVE_ALSA, [1], [Define if ALSA input is enabled]))


AS_IF([test "x$enable_jack" = "xyes"],
       AC_CHECK_LIB(jack, jack_client_open, [],
                    [AC_MSG_ERROR([JACK is required])]))
AS_IF([test "x$enable_jack" = "xyes"],
       AC_DEFINE(HAVE_JACK, [1], [Define if JACK input is enabled]))


AS_IF([test "x$enable_vlc" = "xyes"], [
    PKG_CHECK_MODULES([LIBVLC], [libvlc])
    AC_SUBST([LIBVLC_CFLAGS])
    AC_SUBST([LIBVLC_LIBS]) ] )

AS_IF([test "x$enable_vlc" = "xyes"],
       AC_DEFINE(HAVE_VLC, [1], [Define if VLC input is enabled]))


# Link against jack and alsa conditionally
AM_CONDITIONAL([HAVE_JACK], [ test "x$enable_jack" = "xyes" ])
AM_CONDITIONAL([HAVE_ALSA], [ test "x$enable_alsa" = "xyes" ])

# fdk-aac-dabplus-zmq needs ZeroMQ
AC_CHECK_LIB(zmq, zmq_init, , AC_MSG_ERROR(ZeroMQ libzmq is required))

if pkg-config MagickWand; then
    MAGICKWAND_CFLAGS=`pkg-config MagickWand --cflags`
    MAGICKWAND_LDADD=`pkg-config MagickWand --libs`
    AC_SUBST(MAGICKWAND_CFLAGS)
    AC_SUBST(MAGICKWAND_LDADD)
else
    AC_MSG_WARN(MagickWand not found)
fi

AS_IF([ pkg-config MagickWand ],
       AC_DEFINE(HAVE_MAGICKWAND, [1], [Define if MagickWand is available]))


dnl soname version to use
dnl goes by ‘current[:revision[:age]]’ with the soname ending up as
dnl current.age.revision
FDK_AAC_VERSION=0:4:0
LIBTOOLAME_DAB_VERSION=0:1:0

AS_IF([test x$enable_shared = xyes], [LIBS_PRIVATE=$LIBS], [LIBS_PUBLIC=$LIBS])
AC_SUBST(FDK_AAC_VERSION)
AC_SUBST(LIBTOOLAME_DAB_VERSION)
AC_SUBST(LIBS_PUBLIC)
AC_SUBST(LIBS_PRIVATE)

AM_CONDITIONAL([IS_GIT_REPO], [test -d '.git'])

AC_CONFIG_FILES([Makefile
                 fdk-dabplus.pc])
AC_OUTPUT

echo
echo "***********************************************"
echo
echo "Features enabled:"
enabled=""
disabled=""
for feature in jack vlc alsa
do
    eval var=\$enable_$feature
    AS_IF([test "x$var" = "xyes"],
          [enabled="$enabled $feature"],
          [disabled="$disabled $feature"])
done

AS_IF([ pkg-config MagickWand ],
      [enabled="$enabled magickwand"],
      [disabled="$disabled magickwand"])

echo "  Enabled: $enabled"
echo "  Disabled: $disabled"
