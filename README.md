fdk-aac-dabplus
===============

A standalone library of the Fraunhofer FDK AAC code from Android.

This is 960-frames version of codec. Used for DAB+/DRM boradcast encoding.


Usage:

aac-enc-dabplus [OPTION...]

    -b, --bitrate={ 8, 16, ..., 192 }    Output bitrate in kbps. Must be 8 multiple.
    -i, --input=FILENAME                 Input filename (default: stdin).
    -o, --output=FILENAME                Output filename (default: stdout).
    -a, --afterburner                    Turn on AAC encoder quality increaser.
    -f, --format={ wav, raw }            Set input file format (default: wav).
    -c, --channels={ 1, 2 }              Nb of input channels for raw input (default: 2).
    -r, --rate={ 32000, 48000 }          Sample rate for raw input (default: 48000).
    -t, --adts                           Set ADTS output format (for debugging).
    -l, --lp                             Set frame size to 1024 instead of 960.
