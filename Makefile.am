ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = subdir-objects

if IS_GIT_REPO
GITVERSION_FLAGS = -DGITVERSION="\"`git describe --dirty`\""
else
GITVERSION_FLAGS =
endif

libtoolame_dab_la_LINK = $(LINK) $(libtoolame_dab_la_LDFLAGS)
# Mention a dummy pure C file to trigger generation of the $(LINK) variable

toolame_dabincludedir = $(includedir)/libtoolame-dab
toolame_dabinclude_HEADERS = \
	$(top_srcdir)/libtoolame-dab/absthr.h \
	$(top_srcdir)/libtoolame-dab/ath.h \
	$(top_srcdir)/libtoolame-dab/availbits.h \
	$(top_srcdir)/libtoolame-dab/bitstream.h \
	$(top_srcdir)/libtoolame-dab/common.h \
	$(top_srcdir)/libtoolame-dab/crc.h \
	$(top_srcdir)/libtoolame-dab/critband.h \
	$(top_srcdir)/libtoolame-dab/encode.h \
	$(top_srcdir)/libtoolame-dab/encode_new.h \
	$(top_srcdir)/libtoolame-dab/encoder.h \
	$(top_srcdir)/libtoolame-dab/enwindow.h \
	$(top_srcdir)/libtoolame-dab/fft.h \
	$(top_srcdir)/libtoolame-dab/freqtable.h \
	$(top_srcdir)/libtoolame-dab/ieeefloat.h \
	$(top_srcdir)/libtoolame-dab/mem.h \
	$(top_srcdir)/libtoolame-dab/musicin.h \
	$(top_srcdir)/libtoolame-dab/options.h \
	$(top_srcdir)/libtoolame-dab/portableio.h \
	$(top_srcdir)/libtoolame-dab/psycho_0.h \
	$(top_srcdir)/libtoolame-dab/psycho_1.h \
	$(top_srcdir)/libtoolame-dab/psycho_1_priv.h \
	$(top_srcdir)/libtoolame-dab/psycho_2.h \
	$(top_srcdir)/libtoolame-dab/psycho_3.h \
	$(top_srcdir)/libtoolame-dab/psycho_3priv.h \
	$(top_srcdir)/libtoolame-dab/psycho_4.h \
	$(top_srcdir)/libtoolame-dab/psycho_n1.h \
	$(top_srcdir)/libtoolame-dab/subband.h \
	$(top_srcdir)/libtoolame-dab/tables.h \
	$(top_srcdir)/libtoolame-dab/toolame.h \
	$(top_srcdir)/libtoolame-dab/utils.h

pkgconfigdir = $(libdir)/pkgconfig

lib_LTLIBRARIES = libtoolame-dab.la

libtoolame_dab_la_CFLAGS  = -fomit-frame-pointer -DNEWENCODE -std=c99
libtoolame_dab_la_LDFLAGS = -version-info @LIBTOOLAME_DAB_VERSION@ \
							-no-undefined \
							-export-symbols $(top_srcdir)/libtoolame-dab.sym

if HAVE_JACK
odr_audioenc_LDADD_JACK  = -ljack
else
odr_audioenc_LDADD_JACK  =
endif

if HAVE_ALSA
odr_audioenc_LDADD_ALSA  = -lasound
else
odr_audioenc_LDADD_ALSA  =
endif

if HAVE_FFMPEG
odr_audioenc_LDADD_FFMPEG = -lavdevice -lavformat -lavfilter -lavcodec -lavutil
else
odr_audioenc_LDADD_FFMPEG =
endif

FEC_SOURCES = contrib/fec/char.h \
			  contrib/fec/decode_rs_char.c contrib/fec/decode_rs.h \
			  contrib/fec/encode_rs_char.c contrib/fec/encode_rs.h \
			  contrib/fec/fec.h \
			  contrib/fec/init_rs_char.c contrib/fec/init_rs.h \
			  contrib/fec/rs-common.h


odr_audioenc_LDFLAGS     = -no-install
odr_audioenc_LDADD       = libtoolame-dab.la \
						   -lzmq \
						   $(odr_audioenc_LDADD_JACK) \
						   $(odr_audioenc_LDADD_ALSA) \
						   $(odr_audioenc_LDADD_FFMPEG) \
						   $(LIBVLC_LIBS)
odr_audioenc_CPPFLAGS    = $(AM_CPPFLAGS) $(GITVERSION_FLAGS) -ggdb -O2
odr_audioenc_SOURCES     = src/odr-audioenc.cpp \
						   src/FileInput.cpp \
						   src/FileInput.h \
						   src/AlsaInput.cpp \
						   src/AlsaInput.h \
						   src/JackInput.cpp \
						   src/JackInput.h \
						   src/VLCInput.cpp \
						   src/VLCInput.h \
						   src/FFMPEGInput.cpp \
						   src/FFMPEGInput.h \
						   src/AACDecoder.cpp \
						   src/AACDecoder.h \
						   src/SampleQueue.h \
						   src/encryption.c \
						   src/encryption.h \
						   src/zmq.hpp \
						   src/utils.c \
						   src/utils.h \
						   src/wavfile.cpp \
						   src/common.h \
						   $(FEC_SOURCES)

bin_PROGRAMS =  odr-audioenc$(EXEEXT)

noinst_HEADERS = src/wavfile.h

libtoolame_dab_la_SOURCES = \
							libtoolame-dab/common.c \
							libtoolame-dab/encode.c \
							libtoolame-dab/ieeefloat.c \
							libtoolame-dab/toolame.c \
							libtoolame-dab/portableio.c \
							libtoolame-dab/psycho_n1.c \
							libtoolame-dab/psycho_0.c \
							libtoolame-dab/psycho_1.c \
							libtoolame-dab/psycho_2.c \
							libtoolame-dab/psycho_3.c \
							libtoolame-dab/psycho_4.c \
							libtoolame-dab/fft.c \
							libtoolame-dab/subband.c \
							libtoolame-dab/bitstream.c \
							libtoolame-dab/mem.c \
							libtoolame-dab/crc.c \
							libtoolame-dab/tables.c \
							libtoolame-dab/availbits.c \
							libtoolame-dab/ath.c \
							libtoolame-dab/encode_new.c \
							libtoolame-dab/utils.c


EXTRA_DIST = $(top_srcdir)/bootstrap \
			 $(top_srcdir)/README.md \
			 $(top_srcdir)/ChangeLog \
			 $(top_srcdir)/libtoolame-dab.sym \
			 $(top_srcdir)/Doxyfile \
			 $(top_srcdir)/contrib/fec/LICENSE \
			 $(top_srcdir)/contrib/fec/README.md

doc: export PROJECT_NUMBER:=$(shell git describe --dirty)

.PHONY: doc
doc:
	doxygen
