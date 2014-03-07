fdk-aac-dabplus
===============

A standalone library of the Fraunhofer FDK AAC code from Android, patched for
960-transform. Used for DAB+ broadcast encoding.

Also includes a version with a ODR-DabMux compatible ZeroMQ output.

There is experimental support for DAB MOT Slideshow and DLS, written by
CSP http://rd.csp.it


Usage:

    aac-enc-dabplus [OPTION...]
    
    -b, --bitrate={ 8, 16, ..., 192 }    Output bitrate in kbps. Must be multiple of 8.
    -i, --input=FILENAME                 Input filename (default: stdin).
    -o, --output=FILENAME                Output filename (default: stdout).
    -a, --afterburner                    Turn on AAC encoder quality increaser.
    -f, --format={ wav, raw }            Set input file format (default: wav).
    -c, --channels={ 1, 2 }              Nb of input channels for raw input (default: 2).
    -r, --rate={ 32000, 48000 }          Sample rate for raw input (default: 48000).


The encoder with ZeroMQ output has the same options as above,
but takes a zmq destination url as output

See
    aac-enc-dabplus-zmq -h


How to build
=============

Requirements:
* boost-thread and boost-system
* ImageMagick magickwand (for MOT slideshow)

* Download code and unpack it.
* Download and install libfec from https://github.com/Opendigitalradio/ka9q-fec
* do "./configure", then "make" and "make install"
* use aac-enc-dabplus or aac-enc-dabplus-zmq to encode live stream or file.
* use mot-encoder to encode images into MOT Slideshow


How to use
==========

Scenario 1
----------

Live Stream encoding and preparing for DAB muxer, with ZMQ output, at 32kHz, using sox.

    ALSASRC="default"
    DST="tcp://yourserver:9000"
    BITRATE=64

    sox -t alsa $ALSASRC -b 16 -t raw - rate 32k channels 2 | \
    ../fdk-aac-dabplus/aac-enc-dabplus-zmq -r 32000 \
    -i /dev/stdin -b $BITRATE -f raw -a -o $DST -p 4

Scenario 2
----------
Live Stream encoding and preparing for DAB muxer, with FIFO to odr-dabmux, 48kHz, using
arecord.

    arecord -t raw -f S16_LE -c 2 -r 48000 -D plughw:CARD=Loopback,DEV=0,SUBDEV=0 | \
    aac-enc-dabplus -b 24 -f raw -c 2 -r 48000 -i /dev/stdin -o /dev/stdout 2>/dev/null | \
    mbuffer -q -m 10k -P 100 -s 360 > station1.fifo


Scenario 3
----------
Wave file encoding, for non-realtime processing

    aac-enc-dabplus -a -b 64 -i wave_file.wav -o station1.dabp

Usage of MOT Slideshow
======================

MOT Slideshow is an experimental feature. The mot-encoder reads images in the specified folder,
and generates the PAD data for the encoder. This is communicated through a unique fifo in /tmp,
therefore only one instance can run on a single machine.

Only aac-enc-dabplus-zmq inserts the PAD data from mot-encoder into the bitstream.

This is an ongoing development.

