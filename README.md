fdk-aac-dabplus Package
=======================

This package contains several tools that use the standalone library
of the Fraunhofer FDK AAC code from Android, patched for
960-transform to do DAB+ broadcast encoding.

The main tool is the *dabplus-enc* encoder, which can encode from
a file (raw or wav) or from an ALSA source to a file or a pipe, and
to a ZeroMQ output compatible with ODR-DabMux.

The ALSA input supports experimental sound card clock drift compensation, that
can compensate for imprecise sound card clocks.

*dabplus-enc* includes support for DAB MOT Slideshow and DLS, written by
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

* boost-thread and boost-system
* ImageMagick magickwand (for MOT slideshow)
* The alsa libraries (libasound2)
* Download and install libfec from https://github.com/Opendigitalradio/ka9q-fec
* Download and install ZeroMQ from http://download.zeromq.org/zeromq-4.0.3.tar.gz

This package:

    git clone https://github.com/mpbraendli/fdk-aac-dabplus.git
    cd fdk-aac-dabplus
    ./bootstrap
    ./configure
    make
    sudo make install

* See the possible scenarios below on how to use the tools
* use mot-encoder to encode images into MOT Slideshow


How to use
==========

We assume that you have a ODR-DabMux configured for an ZeroMQ
input on port 9000.

    ALSASRC="default"
    DST="tcp://yourserver:9000"
    BITRATE=64

AAC encoder confiugration
-------------------------

By default, when not overridden by the --aaclc, --sbr or --ps options,
the encoder is configured according to bitrate and number of channels.

If only one channel is used, SBR (Spectral-Band Replication, also called
HE-AAC) is enabled up to 64kbps. AAC-LC is used for higher bitrates.

If two channels are used, PS (Parametric Stereo, also called HE-AAC v2)
is enabled up to 48kbps. Between 56kbps and 80kbps, SBR is enabled. 88kbps
and higher are using AAC-LC.

Scenario 1
----------

Live Stream from ALSA sound card at 32kHz, with ZMQ output for ODR-DabMux:

    dabplus-enc -d $ALSASRC -c 2 -r 32000 -b $BITRATE -o $DST -l

To enable sound card drift compensation, add the option **-D**:

    dabplus-enc -d $ALSASRC -c 2 -r 32000 -b $BITRATE -o $DST -D -l

You might see **U** and **O** appearing on the terminal. They correspond
to audio underruns and overruns that happen due to the different speeds at which
the audio is captured from the soundcard, and encoded into HE-AACv2.

High occurrence of these will lead to audible artifacts.

Scenario 2
----------

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


Scenario 3
----------
Live Stream encoding and preparing for DAB muxer, with ZMQ output, at 32kHz, using sox.
This illustrates the fifo input over standard input of *dabplus-enc*.


    sox -t alsa $ALSASRC -b 16 -t raw - rate 32k channels 2 | \
    dabplus-enc -r 32000 -l \
    -i - -b $BITRATE -f raw -a -o $DST -p 53

The -p 53 sets the padlen, compatible with the default mot-encoder setting. mot-encoder needs
to be given the same value for this option.


Scenario 4
----------
Live Stream encoding and preparing for DAB muxer, with FIFO to odr-dabmux, 48kHz, using
arecord.

    arecord -t raw -f S16_LE -c 2 -r 48000 -D plughw:CARD=Loopback,DEV=0,SUBDEV=0 | \
    dabplus-enc -l -a -b $BITRATE -f raw -c 2 -r 48000 -i /dev/stdin -o - | \
    mbuffer -q -m 10k -P 100 -s 360 > station1.fifo

Here we are using the ALSA plughw feature.


Scenario 5
----------
Live Stream resampling (to 32KHz) and encoding from FIFO and preparing for DAB muxer, with FIFO to odr-dabmux
using mplayer. If there are no data in FIFO, encoder generates silence.

    mplayer -quiet -loop 0 -af resample=32000:nowaveheader,format=s16le,channels=2 -ao pcm:file=/tmp/aac.fifo:fast <FILE/URL> &
    dabplus-enc -l -f raw --fifo-silence -i /tmp/aac.fifo -r 32000 -c 2 -b 72 -o /dev/stdout \
    mbuffer -q -m 10k -P 100 -s 1080 > station1.fifo

*Note*: Do not use /dev/stdout for pcm oputput in mplayer. Mplayer log messages on stdout.

Scenario 6
----------
Wave file encoding, for non-realtime processing

    dabplus-enc -a -b $BITRATE -i wave_file.wav -o station1.dabp


Usage of MOT Slideshow and DLS
==============================

MOT Slideshow is a new feature, which has been tested on several receivers and
using [XPADxpert](http://www.basicmaster.de/xpadxpert/), but is still a work
in progress.

*mot-encoder* reads images from
the specified folder, and generates the PAD data for the encoder. This is
communicated through a fifo to the encoder. It also reads DLS from a file, and
includes this information in the PAD.

*dabplus-enc-file-zmq* and *dabplus-enc-alsa-zmq* can insert the PAD data from
mot-encoder into the bitstream.
The mp2 encoder [toolame-dab](https://github.com/Opendigitalradio/toolame-dab)
can also read *mot-encoder* data.

This is an ongoing development. Make sure you use the same pad length option
for *mot-encoder* and the audio encoder. Only some pad lengths are supported,
please see *mot-encoder*'s help.

Known Limitations
-----------------

*mot-encoder* encodes slides in a 10 second interval, which is not linked
to the rate at which the encoder reads the PAD data.

Version 0.4.0 of the encoder changed the ZeroMQ framing. It will only work with
ODR-DabMux v0.7.0 and later.

