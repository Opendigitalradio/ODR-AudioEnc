FDK-AAC-DABplus Package
=======================

This package contains a DAB and DAB+ encoder that integrates into the
ODR-mmbTools.

The DAB encoder is based on toolame. The DAB+ encoder uses a modified library
of the Fraunhofer FDK AAC code from Android, patched for 960-transform to do
DAB+ broadcast encoding.

The main tool is the *dabplus-enc* encoder, which can read audio from
a file (raw or wav), from an ALSA source, from JACK or using libVLC,
and encode to a file, a pipe, or to a ZeroMQ output compatible with ODR-DabMux.

The libVLC input allows the encoder to use all inputs supported by VLC, and
therefore also webstreams and other network sources.

The ALSA and libVLC inputs support experimental sound card clock drift
compensation, that can compensate for imprecise sound card clocks.

The JACK input does not automatically connect to anything. The encoder runs
at the rate defined by the system clock, and therefore sound
card clock drift compensation is also used.

*dabplus-enc* includes support for DAB MOT Slideshow and DLS, contributed by
[CSP](http://rd.csp.it).

To encode DLS and Slideshow data, the *mot-encoder* tool reads images
from a folder and DLS text from a file, and generates the PAD data
for the encoder.

For detailed usage, see the usage screen of the different tools.

More information is available on the
[Opendigitalradio wiki](http://opendigitalradio.org)

How to build
=============

Requirements:

* A C++11 compiler
* ImageMagick magickwand (optional, for MOT slideshow)
* Download and install libfec from https://github.com/Opendigitalradio/ka9q-fec
* Install ZeroMQ 4.0.4 or more recent
  * If your distribution does not include it, take it from
    from http://download.zeromq.org/zeromq-4.0.4.tar.gz
* JACK audio connection kit (optional)
* The alsa libraries (libasound2, optional)
* libvlc and vlc for the plugins (optional)

This package:

    git clone https://github.com/Opendigitalradio/fdk-aac-dabplus.git
    cd fdk-aac-dabplus
    ./bootstrap
    ./configure
    make
    sudo make install

If you want to use ALSA, JACK and libVLC inputs, please use

    ./configure --enable-alsa --enable-jack --enable-vlc

* See the possible scenarios below on how to use the tools
* use *mot-encoder* to encode images into MOT Slideshow


How to use
==========

We assume that you have a ODR-DabMux configured for an ZeroMQ
input on port 9000.

    ALSASRC="default"
    DST="tcp://yourserver:9000"
    BITRATE=64

DAB+ AAC encoder configuration
------------------------------
By default, when not overridden by the --aaclc, --sbr or --ps options,
the encoder is configured according to bitrate and number of channels.

If only one channel is used, SBR (Spectral-Band Replication, also called
HE-AAC) is enabled up to 64kbps. AAC-LC is used for higher bitrates.

If two channels are used, PS (Parametric Stereo, also called HE-AAC v2)
is enabled up to 48kbps. Between 56kbps and 80kbps, SBR is enabled. 88kbps
and higher are using AAC-LC.

ZeroMQ output
-------------

The ZeroMQ output included in FDK-AAC-DABplus is able to connect to
one or several instances of ODR-DabMux. The -o option can be used
more than once to achieve this.

Scenario *wav file for offline processing*
------------------------------------------
Wave file encoding, for non-realtime processing

    dabplus-enc -b $BITRATE -i wave_file.wav -o station1.dabp

Scenario *ALSA*
---------------
Live Stream from ALSA sound card at 32kHz, with ZMQ output for ODR-DabMux:

    dabplus-enc -d $ALSASRC -c 2 -r 32000 -b $BITRATE -o $DST -l

To enable sound card drift compensation, add the option **-D**:

    dabplus-enc -d $ALSASRC -c 2 -r 32000 -b $BITRATE -o $DST -D -l

You might see **U** and **O** appearing on the terminal. They correspond
to audio underruns and overruns that happen due to the different speeds at which
the audio is captured from the soundcard, and encoded into HE-AACv2.

High occurrence of these will lead to audible artifacts.

Scenario *libVLC input for a webstream*
---------------------------------------
Read a webstream and send it to ODR-DabMux over ZMQ:

    dabplus-enc -v $URL -r 32000 -c 2 -o $DST -l -b $BITRATE

If you need to extract the ICY-Text information, e.g. for DLS, you can use the
**-w <filename>** option to write the ICY-Text into a file that can be read by
*mot-encoder*.

If the webstream bitrate is slightly wrong (bad clock at the source), you can
enable drift compensation with **-D**.

Scenario *JACK input*
---------------------
JACK input: Instead of -i (file input) or -d (ALSA input), use -j *name*, where *name* specifies the JACK
name for the encoder:

    dabplus-enc -j myenc -l -b $BITRATE -f raw -o $DST

The samplerate of the JACK server should be 32kHz or 48kHz.

Scenario *local file through snd-aloop*
---------------------------------------
Play some local audio source from a file, with ZMQ output for ODR-DabMux. The problem with
playing a file is that *dabplus-enc* cannot directly be used, because ODR-DabMux
does not back-pressure the encoder, which will therefore encode much faster than realtime.

While this issue is sorted out, the following trick is a very flexible solution: use the
alsa virtual loop soundcard *snd-aloop* in the following way:

    modprobe snd-aloop

This creates a new audio card (usually 'hw:1' but have a look at /proc/asound/card to be sure) that
can then be used for the alsa encoder.

    ./dabplus-enc -d hw:1 -c 2 -r 32000 -b 64 -o $DST -l

Then, you can use any media player that has an alsa output to play whatever source it supports:

    cd your/preferred/music
    mplayer -ao alsa:device=hw=1.1 -srate 32000 -shuffle *

Important: you must specify the correct sample rate on both "sides" of the virtual sound card.


Scenario *mplayer and fifo*
---------------------------
*Warning*: Connection through pipes to ODR-DabMux are deprecated in favour of ZeroMQ.

Live Stream resampling (to 32KHz) and encoding from FIFO and preparing for DAB muxer, with FIFO to odr-dabmux
using mplayer. If there are no data in FIFO, encoder generates silence.

    mplayer -quiet -loop 0 -af resample=32000:nowaveheader,format=s16le,channels=2 -ao pcm:file=/tmp/aac.fifo:fast <FILE/URL> &
    dabplus-enc -l -f raw --fifo-silence -i /tmp/aac.fifo -r 32000 -c 2 -b 72 -o /dev/stdout \
    mbuffer -q -m 10k -P 100 -s 1080 > station1.fifo

*Note*: Do not use /dev/stdout for pcm output in mplayer. Mplayer log messages on stdout.

Return values
-------------
dabplus-enc returns:

 * 0 if it encoded the whole input file
 * 1 if some options were not understood, or encoder initialisation failed
 * 2 if the silence timeout was reached
 * 3 if the AAC encoder failed
 * 4 it the ZeroMQ send failed
 * 5 if the input had a fault

Usage of MOT Slideshow and DLS
==============================

*mot-encoder* reads images from the specified folder, and generates the PAD
data for the encoder. This is communicated through a fifo to the encoder. It
also reads DLS from a file, and includes this information in the PAD.

If ImageMagick is available
---------------------------
It can read all file formats supported by ImageMagick, and by default resizes
them to 320x240 pixels, and compresses them as JPEG. If the input file is already
a JPEG file of the correct size, and smaller than 50kB, it is sent without further
compression. If the input file is a PNG that satisfies the same criteria, it is
transmitted as PNG without any recompression.

RAW Format
----------
If ImageMagick is not compiled in, or when enabled with the -R option, the images
are not modified, and are transmitted as-is. Use this if you can guarantee that
the generated files are smaller than 50kB and not larger than 320x240 pixels.

Supported Encoders
------------------
*dabplus-enc* can insert the PAD data from *mot-encoder* into the bitstream.
The mp2 encoder [Toolame-DAB](https://github.com/Opendigitalradio/toolame-dab)
can also read *mot-encoder* data.

This is an ongoing development. Make sure you use the same pad length option
for *mot-encoder* and the audio encoder. Only some pad lengths are supported,
please see *mot-encoder*'s help.

Character Sets
--------------
When *mot-encoder* is launched with the default character set options, it assumes
that the DLS text in the file is encoded in UTF-8, and will convert it according to
the DAB standard to the *Complete EBU Latin based repertoire* character set encoding.

If you set the character set encoding to any other setting (except
*Complete EBU Latin based repertoire* which needs no conversion),
*mot-encoder* will abort, as it does not support any other conversion than from
UTF-8 to *Complete EBU Latin based repertoire*.

You can however use the -C option to transmit the untouched DLS text. In this
case, it is your responsibility to ensure the encoding is valid.  For instance,
if your data is already encoded in *Complete EBU Latin based repertoire*, you
must specify both --charset=0 and --raw-dls.

Known Limitations
-----------------
The gain option for libVLC enables the VLC audio compressor with default
settings. This has more impact than just changing the volume of the audio.

*mot-encoder* encodes slides in a 10 second interval, which is not linked
to the rate at which the encoder reads the PAD data. It also doesn't prioritise
DLS transmission over Slides.

Some receivers did not decode audio anymore between v0.3.0 and v0.5.0, because of
a change implemented to get PAD to work. The change was subsequently reverted in
v0.5.1 because it was deemed essential that audio decoding works on all receivers.
v0.7.0 fixes most issues, and PAD now works much more reliably.

Version 0.4.0 of the encoder changed the ZeroMQ framing. It will only work with
ODR-DabMux v0.7.0 and later.

LICENCE
=======

It's complicated. The FDK-AAC-DABplus project contains

 - The Third-Party Modified Version of the Fraunhofer FDK AAC Codec Library for
   Android, which is under its own licence. See NOTICE. This is built into a
   shared library.
 - The code for dabplus-enc in src/ licensed under the Apache Licence v2.0. See
   http://www.apache.org/licenses/LICENSE-2.0
 - libtoolame-dab, derived from TooLAME, licensed under LGPL v2.1 or later. See
   libtoolame-dab/LGPL.txt. This is built into a shared library.

The dabplus-enc binary is linked agains the libtoolame-dab and fdk-aac-dabplus
shared libraries.

In addition to the audio encoder, there is also mot-encoder, containing code

 - in src/ that is GPL v3+ licensed
 - and a crc library with unclear licence situation in contrib/

Whether it is legal or not to distribute compiled binaries from these sources
is unclear to me. Please seek legal advice to answer this question.

