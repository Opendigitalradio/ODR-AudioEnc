fdk-aac-dabplus
===============

A standalone library of the Fraunhofer FDK AAC code from Android.

This is 960-frames version of codec. Used for DAB+ boradcast encoding.


Usage:

aac-enc-dabplus [OPTION...]

    -b, --bitrate={ 8, 16, ..., 192 }    Output bitrate in kbps. Must be 8 multiple.
    -i, --input=FILENAME                 Input filename (default: stdin).
    -o, --output=FILENAME                Output filename (default: stdout).
    -a, --afterburner                    Turn on AAC encoder quality increaser.
    -f, --format={ wav, raw }            Set input file format (default: wav).
    -c, --channels={ 1, 2 }              Nb of input channels for raw input (default: 2).
    -r, --rate={ 32000, 48000 }          Sample rate for raw input (default: 48000).



How to build
===============

* Download code and unpack it.
* Download and install libfec from crc.ca website: http://mmbtools.crc.ca/content/view/39/65/ (follow instructions on that website).
* do "./configure --enable-example", then "make" and "make install"
* use aac-enc-dabplus to encode live stream or file.


How to use
===============

Scenario 1 (Live Stream enconding and preparing for DAB muxer):

    arecord -t raw -f S16_LE -c 2 -r 48000 -D plughw:CARD=Loopback,DEV=0,SUBDEV=0 | \
    aac-enc-dabplus -b 24 -f raw -c 2 -r 48000 -i /dev/stdin -o /dev/stdout 2>/dev/null | \
    mbuffer -q -m 10k -P 100 -s 360 > station1.fifo


Scenario 2 (Wave file enconding):

    aac-enc-dabplus -a -b 64 -i wave_file.wav -o station1.dabp

