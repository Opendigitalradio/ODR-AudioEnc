fdk-aac-dabplus Package
=======================

This package contains several tools that use the standalone library
of the Fraunhofer FDK AAC code from Android, patched for
960-transform to do DAB+ broadcast encoding.

The first tool, *dabplus-enc-file* can encode from a file or pipe
source, and encode into a file or pipe. There is no PAD support.

The *dabplus-enc-file-zmq* can encode from a file or pipe source,
and encode to a ZeroMQ output compatible with ODR-DabMux.

The *dabplus-enc-alsa-zmq* can encode from an ALSA soundcard,
and encode to a ZeroMQ output compatible with ODR-DabMux. It supports
experimental sound card clock drift compensation, that can compensate
for imprecise sound card clocks.

*dabplus-enc-file-zmq* and *dabplus-enc-alsa-zmq* include experimental
support for DAB MOT Slideshow and DLS, written by [CSP](http://rd.csp.it).

To encode DLS and Slideshow data, the *mot-encoder* tool reads images
from a folder, and DLS text from a file, and generates the PAD data
for the encoder.

For detailed usage, see the usage screen of the different tools.

More information is available on the
[Opendigitalradio wiki](http://opendigitalradio.org)

How to build
=============

Requirements:

* boost-thread and boost-system
* ImageMagick magickwand (for MOT slideshow)
* The alsa libraries
* Download and install libfec from https://github.com/Opendigitalradio/ka9q-fec

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

We assume:

    ALSASRC="default"
    DST="tcp://yourserver:9000"
    BITRATE=64

Scenario 1
----------

Live Stream from ALSA sound card at 32kHz, with ZMQ output for ODR-DabMux:

    dabplus-enc-alsa-zmq -d $ALSASRC -c 2 -r 32000 -b $BITRATE -o $DST

To enable sound card drift compensation, add the option **-D**:

    dabplus-enc-alsa-zmq -d $ALSASRC -c 2 -r 32000 -b $BITRATE -o $DST -D

You might see **U** and **O<number>** appearing on the terminal. They correspond
to audio underruns and overruns that happen due to the different speeds at which
the audio is captured from the soundcard, and encoded into HE-AACv2.

High occurrence of these will lead to audible artifacts.


Scenario 2
----------
Live Stream encoding and preparing for DAB muxer, with ZMQ output, at 32kHz, using sox.
This illustrates the fifo input of *dabplus-enc-file-zmq*.


    sox -t alsa $ALSASRC -b 16 -t raw - rate 32k channels 2 | \
    dabplus-enc-file-zmq -r 32000 \
    -i /dev/stdin -b $BITRATE -f raw -a -o $DST -p 53

The -p 53 sets the padlen, compatible with the default mot-encoder setting. mot-encoder needs
to be given the same value for this option.


Scenario 3
----------
Live Stream encoding and preparing for DAB muxer, with FIFO to odr-dabmux, 48kHz, using
arecord.

    arecord -t raw -f S16_LE -c 2 -r 48000 -D plughw:CARD=Loopback,DEV=0,SUBDEV=0 | \
    dabplus-enc-file -a -b 24 -f raw -c 2 -r 48000 -i /dev/stdin -o /dev/stdout 2>/dev/null | \
    mbuffer -q -m 10k -P 100 -s 360 > station1.fifo

Here we are also using the ALSA plughw feature.

Scenario 4
----------
Wave file encoding, for non-realtime processing

    dabplus-enc-file -a -b 64 -i wave_file.wav -o station1.dabp


Usage of MOT Slideshow
======================

MOT Slideshow is an experimental feature. The *mot-encoder* reads images from
the specified folder, and generates the PAD data for the encoder. This is
communicated through a fifo to the encoder.

Only *dabplus-enc-file-zmq* and *dabplus-enc-alsa-zmq* insert the PAD data from
mot-encoder into the bitstream.

This is an ongoing development.

