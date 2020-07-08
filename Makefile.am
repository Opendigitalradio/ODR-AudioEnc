ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = subdir-objects

SUBDIRS = fdk-aac

if IS_GIT_REPO
GITVERSION_FLAGS = -DGITVERSION="\"`git describe --dirty`\""
else
GITVERSION_FLAGS =
endif

noinst_LIBRARIES = libtoolame-dab.a

libtoolame_dab_a_SOURCES = \
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
							libtoolame-dab/utils.c \
							libtoolame-dab/absthr.h \
							libtoolame-dab/ath.h \
							libtoolame-dab/availbits.h \
							libtoolame-dab/bitstream.h \
							libtoolame-dab/common.h \
							libtoolame-dab/crc.h \
							libtoolame-dab/critband.h \
							libtoolame-dab/encode.h \
							libtoolame-dab/encode_new.h \
							libtoolame-dab/encoder.h \
							libtoolame-dab/enwindow.h \
							libtoolame-dab/fft.h \
							libtoolame-dab/freqtable.h \
							libtoolame-dab/ieeefloat.h \
							libtoolame-dab/mem.h \
							libtoolame-dab/musicin.h \
							libtoolame-dab/options.h \
							libtoolame-dab/portableio.h \
							libtoolame-dab/psycho_0.h \
							libtoolame-dab/psycho_1.h \
							libtoolame-dab/psycho_1_priv.h \
							libtoolame-dab/psycho_2.h \
							libtoolame-dab/psycho_3.h \
							libtoolame-dab/psycho_3priv.h \
							libtoolame-dab/psycho_4.h \
							libtoolame-dab/psycho_n1.h \
							libtoolame-dab/subband.h \
							libtoolame-dab/tables.h \
							libtoolame-dab/toolame.h \
							libtoolame-dab/utils.h


libtoolame_dab_a_CFLAGS  = -fomit-frame-pointer -DNEWENCODE -std=c99

pkgconfigdir = $(libdir)/pkgconfig

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

FEC_SOURCES = contrib/fec/char.h \
			  contrib/fec/decode_rs_char.c contrib/fec/decode_rs.h \
			  contrib/fec/encode_rs_char.c contrib/fec/encode_rs.h \
			  contrib/fec/fec.h \
			  contrib/fec/init_rs_char.c contrib/fec/init_rs.h \
			  contrib/fec/rs-common.h


odr_audioenc_LDADD       = libtoolame-dab.a \
						   fdk-aac/libfdk-aac-dab.a \
						   -lzmq \
						   $(odr_audioenc_LDADD_JACK) \
						   $(odr_audioenc_LDADD_ALSA) \
						   $(LIBVLC_LIBS) $(GST_LIBS)
odr_audioenc_CXXFLAGS    = $(GST_CFLAGS) $(GITVERSION_FLAGS) \
						   -Ifdk-aac/libSYS/include/ \
						   -Ifdk-aac/libAACenc/include/ \
						   -Ifdk-aac/libAACdec/include/ \
						   -Wall -ggdb -O2 -Isrc -Icontrib

odr_audioenc_SOURCES     = src/odr-audioenc.cpp \
						   src/PadInterface.cpp \
						   src/PadInterface.h \
						   src/FileInput.cpp \
						   src/FileInput.h \
						   src/AlsaInput.cpp \
						   src/AlsaInput.h \
						   src/JackInput.cpp \
						   src/JackInput.h \
						   src/GSTInput.cpp \
						   src/GSTInput.h \
						   src/VLCInput.cpp \
						   src/VLCInput.h \
						   src/Outputs.cpp \
						   src/Outputs.h \
						   src/AACDecoder.cpp \
						   src/AACDecoder.h \
						   src/SampleQueue.h \
						   src/StatsPublish.cpp \
						   src/StatsPublish.h \
						   src/encryption.c \
						   src/encryption.h \
						   src/zmq.hpp \
						   src/utils.cpp \
						   src/utils.h \
						   src/wavfile.cpp \
						   src/common.h \
						   contrib/ClockTAI.cpp \
						   contrib/ClockTAI.h \
						   contrib/Globals.cpp \
						   contrib/RemoteControl.cpp \
						   contrib/RemoteControl.h \
						   contrib/Log.cpp \
						   contrib/Log.h \
						   contrib/Socket.cpp \
						   contrib/Socket.h \
						   contrib/crc.c \
						   contrib/crc.h \
						   contrib/ReedSolomon.cpp \
						   contrib/ReedSolomon.h \
						   contrib/ThreadsafeQueue.h \
						   contrib/edi/AFPacket.cpp \
						   contrib/edi/AFPacket.h \
						   contrib/edi/EDIConfig.h \
						   contrib/edi/Interleaver.cpp \
						   contrib/edi/Interleaver.h \
						   contrib/edi/PFT.cpp \
						   contrib/edi/PFT.h \
						   contrib/edi/TagItems.cpp \
						   contrib/edi/TagItems.h \
						   contrib/edi/TagPacket.cpp \
						   contrib/edi/TagPacket.h \
						   contrib/edi/Transport.cpp \
						   contrib/edi/Transport.h \
						   $(FEC_SOURCES)

bin_PROGRAMS =  odr-audioenc$(EXEEXT)

noinst_HEADERS = src/wavfile.h

EXTRA_DIST = $(top_srcdir)/bootstrap \
			 $(top_srcdir)/README.md \
			 $(top_srcdir)/TODO.md \
			 $(top_srcdir)/ChangeLog \
			 $(top_srcdir)/libtoolame-dab.sym \
			 $(top_srcdir)/Doxyfile \
			 $(top_srcdir)/contrib/fec/LICENSE \
			 $(top_srcdir)/contrib/fec/README.md

doc: export PROJECT_NUMBER:=$(shell git describe --dirty)

.PHONY: doc
doc:
	doxygen
