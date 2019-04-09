ODR-AudioEnc Package
========================

This package contains a DAB and DAB+ encoder that integrates into the
ODR-mmbTools.

The DAB encoder is based on toolame. The DAB+ encoder uses a modified library
of the Fraunhofer FDK AAC code from Android, patched for 960-transform to do
DAB+ broadcast encoding. FDK-AAC has to be supplied separately, and is available
in the [repository](https://github.com/Opendigitalradio/fdk-aac.git).

The main tool is the *odr-audioenc* encoder, which can read audio from
a file (raw or wav), from an ALSA source, from JACK or using libVLC,
and encode to a file, a pipe, or to a ZeroMQ output compatible with ODR-DabMux.

The libVLC input allows the encoder to use all inputs supported by VLC, and
therefore also webstreams and other network sources.

The ALSA and libVLC inputs support sound card clock drift
compensation, that can compensate for imprecise sound card clocks.

The JACK input does not automatically connect to anything. The encoder runs
at the rate defined by the system clock, and therefore sound
card clock drift compensation is also used.

*odr-audioenc* can insert Programme-Associated Data, that can be generated with
ODR-PadEnc.

For detailed usage, see the usage screen of the tool with the *-h* option.

More information is available on the
[Opendigitalradio wiki](http://opendigitalradio.org)

How to build
=============

Requirements:

* A C++11 compiler
* [FDK-AAC](https://github.com/Opendigitalradio/fdk-aac.git) with the DAB+ patches
* ZeroMQ 4.0.4 or more recent
* JACK audio connection kit (optional)
* The alsa libraries (libasound2, optional)
* libvlc and vlc for the plugins (optional)

This package:

    ./bootstrap
    ./configure
    make
    sudo make install

If you want to use ALSA, JACK and libVLC inputs, please use

    ./configure --enable-alsa --enable-jack --enable-vlc

* See the possible scenarios below on how to use the tools


How to use
==========

We assume that you have a ODR-DabMux configured for an ZeroMQ
input on port 9000.

    ALSASRC="default"
    DST="tcp://yourserver:9000"
    BITRATE=64

General remarks
---------------

Avoid using sources that are already encoded with a low bitrate, because
encoder cascading will noticeably reduce audio quality. Best are sources
encoded with a lossless codec (FLAC). Otherwise, try to get MP3 at 320kbps, AAC
at 256kbps or higher bitrates.

Ideally use a source at the correct sampling rate (32kHz or 48kHz, according to
your encoder configuration). VLC can do resampling, but on some systems selects
the ugly resampler which creates artifacts. Try adding
`-L --audio-resampler=samplerate -L --src-converter-type=0`
to your command line, but enable verbose mode and read the VLC debug output to
check that it enables the libsamplerate resampler, and not the ugly resampler.

The codecs do not behave well when your source material has peaks that go close
to saturation, especially when you have to resample. When you see little
exclamation marks with the -l option, it's too loud! Reduce the gain at the
source, or use the gain option if that's not possible.


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

The ZeroMQ output included in ODR-AudioEnc is able to connect to
one or several instances of ODR-DabMux. The -o option can be used
more than once to achieve this.

Scenario *wav file for offline processing*
------------------------------------------
Wave file encoding, for non-realtime processing

    odr-audioenc -b $BITRATE -i wave_file.wav -o station1.dabp

Scenario *file that VLC supports*
---------------------------------
If you want to input a file through libvlc, you need to give an absolute path:

    odr-audioenc -b $BITRATE -v file:///home/odr/audio/source.mp3 -o station1.dabp

Scenario *ALSA*
---------------
Live Stream from ALSA sound card at 32kHz, with ZMQ output for ODR-DabMux:

    odr-audioenc -d $ALSASRC -c 2 -r 32000 -b $BITRATE -o $DST -l

To enable sound card drift compensation, add the option **-D**:

    odr-audioenc -d $ALSASRC -c 2 -r 32000 -b $BITRATE -o $DST -D -l

You might see **U** and **O** appearing on the terminal. They correspond
to audio underruns and overruns that happen due to the different speeds at which
the audio is captured from the soundcard, and encoded into HE-AACv2.

High occurrence of these will lead to audible artifacts.

Scenario *libVLC input for a webstream*
---------------------------------------
Read a webstream and send it to ODR-DabMux over ZMQ:

    odr-audioenc -v $URL -r 32000 -c 2 -o $DST -l -b $BITRATE

If you need to extract the ICY-Text information, e.g. for DLS, you can use the
**-w <filename>** option to write the ICY-Text into a file that can be read by
*ODR-PadEnc*.

If the webstream bitrate is slightly wrong (bad clock at the source), you can
enable drift compensation with **-D**.

Scenario *JACK input*
---------------------
JACK input: Instead of -i (file input) or -d (ALSA input), use -j *name*, where *name* specifies the JACK
name for the encoder:

    odr-audioenc -j myenc -l -b $BITRATE -o $DST

The JACK server must run at the samplerate of the encoder (32kHz or 48kHz). If that is not possible,
one workaround is to access JACK through VLC, which will resample accordingly:

    odr-audioenc -l -v jack://dab -b $BITRATE -o $DST

Scenario *LiveWire* or *AES67*
------------------------------

When audio data is available on the network as a multicast stream, it can be encoded using the following pipeline:

    rtpdump -F payload 239.192.1.1/5004 | \
    sox -t raw -e signed-integer -r 48000 -c 2 -b 24 -B /dev/stdin -t raw --no-dither -r 48000 -c 2 -b 16 -L /dev/stdout gain 4 | \
    odr-audioenc -f raw -b $BITRATE -i /dev/stdin -o $DST

It is also possible to use the libvlc input, where you need to create an SDP file with the following contents:

    v=0
    o=Node 1 1 IN IP4 172.16.235.155
    s=TestSine
    t=0 0
    a=type:multicast
    c=IN IP4 239.192.0.1
    m=audio 5004 RTP/AVP 97
    a=rtpmap:97 L24/48000/2

Replace the IP address in the `o=` field by the one corresponding to your
source node IP address, and the IP in `c=` by the multicast IP of your stream.
Then use this SDP file as input for the VLC input.


Scenario *local file through snd-aloop*
---------------------------------------
Play some local audio source from a file, with ZMQ output for ODR-DabMux. The problem with
playing a file is that *odr-audioenc* cannot directly be used, because ODR-DabMux
does not back-pressure the encoder, which will therefore encode much faster than realtime.

While this issue is sorted out, the following trick is a very flexible solution: use the
alsa virtual loop soundcard *snd-aloop* in the following way:

    modprobe snd-aloop

This creates a new audio card (usually 'hw:1' but have a look at /proc/asound/card to be sure) that
can then be used for the alsa encoder.

    ./odr-audioenc -d hw:1 -c 2 -r 32000 -b 64 -o $DST -l

Then, you can use any media player that has an alsa output to play whatever source it supports:

    cd your/preferred/music
    mplayer -ao alsa:device=hw=1.1 -srate 32000 -format=s16le -shuffle *

Important: you must specify the correct sample rate and sample format on both
"sides" of the virtual sound card.


Scenario *mplayer and fifo*
---------------------------
*Warning*: Connection through pipes to ODR-DabMux are deprecated in favour of ZeroMQ.

Live Stream resampling (to 32KHz) and encoding from FIFO and preparing for DAB muxer, with FIFO to odr-dabmux
using mplayer. If there are no data in FIFO, encoder generates silence.

    mplayer -quiet -loop 0 -af resample=32000:nowaveheader,format=s16le,channels=2 -ao pcm:file=/tmp/aac.fifo:fast <FILE/URL> &
    odr-audioenc -l -f raw --fifo-silence -i /tmp/aac.fifo -r 32000 -c 2 -b 72 -o /dev/stdout \
    mbuffer -q -m 10k -P 100 -s 1080 > station1.fifo

*Note*: Do not use /dev/stdout for pcm output in mplayer. Mplayer log messages on stdout.

Return values
-------------
odr-audioenc returns:

 * 0 if it encoded the whole input file
 * 1 if some options were not understood, or encoder initialisation failed
 * 2 if the silence timeout was reached
 * 3 if the AAC encoder failed
 * 4 it the ZeroMQ send failed
 * 5 if the input had a fault

The *-R* option to get ODR-AudioEnc to restart the input
automatically has been deprecated. As this feature does not guarantee that
the odr-audioenc process will never die, running it under a process supervisor
is recommended regardless of this feature being enabled or not. It will be removed
in a future version.


Known Limitations
-----------------
The gain option for libVLC enables the VLC audio compressor with default
settings. This has more impact than just changing the volume of the audio.

Some receivers did not decode audio anymore between v0.3.0 and v0.5.0, because of
a change implemented to get PAD to work. The change was subsequently reverted in
v0.5.1 because it was deemed essential that audio decoding works on all receivers.
v0.7.0 fixes most issues, and PAD now works much more reliably.

Version 0.4.0 of the encoder changed the ZeroMQ framing. It will only work with
ODR-DabMux v0.7.0 and later.

LICENCE
=======

The ODR-AudioEnc project contains

 - The code for odr-audioenc in src/ licensed under the Apache Licence v2.0. See
   http://www.apache.org/licenses/LICENSE-2.0
 - libtoolame-dab, derived from TooLAME, licensed under LGPL v2.1 or later. See
   libtoolame-dab/LGPL.txt. This is built into a shared library.

The odr-audioenc binary is linked against the libtoolame-dab and fdk-aac
shared libraries.

Whether it is legal or not to distribute compiled binaries from these sources
is unclear to me. Please seek legal advice to answer this question.

