dnl                                               -*- Autoconf -*-
dnl Process this file with autoconf to produce a configure script.

AC_INIT([ODR-AudioEnc], [3.0.0], [http://opendigitalradio.org/])
AC_CONFIG_AUX_DIR(.)
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([tar-ustar foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_CONFIG_HEADER([config.h])

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_RANLIB
AM_PROG_AR

AX_CXX_COMPILE_STDCXX(11,noext,mandatory)

# std::thread requires pthread
AX_PTHREAD( [
            AC_DEFINE(HAVE_PTHREAD,1,[Define if you have POSIX threads libraries and header files.])
            CLIBS="$PTHREAD_LIBS $LIBS"
            CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
            LDFLAGS="$LDFLAGS $PTHREAD_CFLAGS"
            CC="$PTHREAD_CC"], [AC_MSG_ERROR([requires pthread])] )

AC_CHECK_LIB([m], [sin])

AX_CHECK_COMPILE_FLAG([-Wduplicated-cond], [CFLAGS="$CFLAGS -Wduplicated-cond"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG([-Wduplicated-branches], [CFLAGS="$CFLAGS -Wduplicated-branches"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG([-Wlogical-op], [CFLAGS="$CFLAGS -Wlogical-op"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG([-Wrestrict], [CFLAGS="$CFLAGS -Wrestrict"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG(["-Wformat=2"], [CFLAGS="$CFLAGS -Wformat=2"], [], ["-Werror"])

AC_LANG_PUSH([C++])
AX_CHECK_COMPILE_FLAG([-Wduplicated-cond], [CXXFLAGS="$CXXFLAGS -Wduplicated-cond"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG([-Wduplicated-branches], [CXXFLAGS="$CXXFLAGS -Wduplicated-branches"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG([-Wlogical-op], [CXXFLAGS="$CXXFLAGS -Wlogical-op"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG([-Wrestrict], [CXXFLAGS="$CXXFLAGS -Wrestrict"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG(["-Wformat=2"], [CXXFLAGS="$CXXFLAGS -Wformat=2"], [], ["-Werror"])

# Linux defines MSG_NOSIGNAL, some other systems have SO_NOSIGPIPE instead
AC_MSG_CHECKING(for MSG_NOSIGNAL)
AC_COMPILE_IFELSE([ AC_LANG_PROGRAM([[
                    #include <sys/socket.h>
                    int f = MSG_NOSIGNAL;
                    ]])],
                   [ AC_MSG_RESULT(yes)
                     AC_DEFINE(HAVE_MSG_NOSIGNAL, 1, [Define this symbol if you have MSG_NOSIGNAL]) ],
                   [ AC_MSG_RESULT(no) ])

AC_MSG_CHECKING(for SO_NOSIGPIPE)
AC_COMPILE_IFELSE([ AC_LANG_PROGRAM([[
                    #include <sys/socket.h>
                    int f = SO_NOSIGPIPE;
                    ]])],
                   [ AC_MSG_RESULT(yes)
                     AC_DEFINE(HAVE_SO_NOSIGPIPE, 1, [Define this symbol if you have SO_NOSIGPIPE]) ],
                   [ AC_MSG_RESULT(no) ])

AC_LANG_POP([C++])

# Check for options
AC_ARG_ENABLE([alsa],
        AS_HELP_STRING([--enable-alsa], [Enable ALSA input]))

AC_ARG_ENABLE([jack],
        AS_HELP_STRING([--enable-jack], [Enable JACK input]))

AC_ARG_ENABLE([vlc],
        AS_HELP_STRING([--enable-vlc], [Enable libvlc input]))

AC_ARG_ENABLE([gst],
        AS_HELP_STRING([--enable-gst], [Enable GStreamer input]))

GST_REQUIRED=1.0.0

AS_IF([test "x$enable_gst" = "xyes"],
    [PKG_CHECK_MODULES(GST, [
      gstreamer-1.0              >= $GST_REQUIRED
      gstreamer-base-1.0         >= $GST_REQUIRED
      gstreamer-app-1.0          >= $GST_REQUIRED
      gstreamer-controller-1.0   >= $GST_REQUIRED
      gstreamer-audio-1.0        >= $GST_REQUIRED
    ], [
      AC_SUBST(GST_CFLAGS)
      AC_SUBST(GST_LIBS)
    ], [
    AC_MSG_ERROR([
      Can't find the following GStreamer development packages:

        gstreamer-1.0              >= $GST_REQUIRED
        gstreamer-base-1.0         >= $GST_REQUIRED
        gstreamer-app-1.0          >= $GST_REQUIRED
        gstreamer-controller-1.0   >= $GST_REQUIRED
        gstreamer-audio-1.0        >= $GST_REQUIRED

      Please make sure you have the necessary GStreamer-1.0
      development headers installed.

      On debian/Ubuntu systems you will probably need to install the
      'libgstreamer1.0-dev' and 'libgstreamer-plugins-base1.0-dev' packages.

      On RPM-based systems you will probably need to install the
      'gstreamer-devel-1.0' package.
    ])
  ])
])

AS_IF([test "x$enable_gst" = "xyes"],
       AC_DEFINE(HAVE_GST, [1], [Define if GST input is enabled]))


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

AC_CHECK_LIB(zmq, zmq_init, , AC_MSG_ERROR(ZeroMQ libzmq is required))

AC_CHECK_LIB(curl, curl_easy_init)
have_curl=$ac_cv_lib_curl_curl_easy_init

AS_IF([test "x$have_curl" = "xyes"],
             [AC_DEFINE(HAVE_CURL, [1], [Define if cURL is available])])

AS_IF([test "x$have_curl" = "xno"],
             [AC_MSG_WARN([cURL not found, timestamps will not work])])

AM_EXTRA_RECURSIVE_TARGETS([fdk-aac])

AM_CONDITIONAL([IS_GIT_REPO], [test -d '.git'])

AM_CONDITIONAL([EXAMPLE], [false])

AC_CONFIG_FILES([Makefile fdk-aac/Makefile])
AC_OUTPUT

echo
echo "***********************************************"
echo
echo "Features enabled:"
enabled=""
disabled=""
for feature in jack vlc alsa gst
do
    eval var=\$enable_$feature
    AS_IF([test "x$var" = "xyes"],
          [enabled="$enabled $feature"],
          [disabled="$disabled $feature"])
done

echo "  Enabled: $enabled"
echo "  Disabled: $disabled"
