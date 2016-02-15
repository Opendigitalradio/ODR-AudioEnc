tooLAME - an optimized mpeg 1/2 layer 2 audio encoder, in form
of a library to be used with the encoder for the ODR-mmbTools

Copyright (C) 2002, 2003 Michael Cheng [mikecheng at NOT planckenergy.com] remove the NOT
http://www.planckenergy.com/

Copyright (C) 2014, 2015, 2016 Matthias P. Braendli [matthias.braendli@mpb.li]
http://opendigitalradio.org/

All changes to the ISO source are licensed under the LGPL
(see LGPL.txt for details)

tooLAME is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

tooLAME is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with tooLAME; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


*********************
INTRODUCTION
*********************

tooLAME is an optimized Mpeg Audio 1/2 Layer 2 encoder.  It is based heavily on
    - the ISO dist10 code
    - improvement to algorithms as part of the LAME project (www.sulaco.org/mp3)
    - work by Mike Cheng and other contributors (see CONTRIBUTORS)

*********************
CONTRIBUTORS
*********************

Dist10 code writers
LAME specific contributions
    fht routines from Ron Mayer <mayer at acuson.com>
    fht tweaking by Mathew Hendry <math at vissci.com>
    window_subband & filter_subband from LAME circa v3.30
        (multiple LAME authors)
        (before Takehiro's window/filter/mdct combination)

Oliver Lietz <lietz at nanocosmos.de>
    Tables now included in the exe!  (yay! :)

Patrick de Smet <pds at telin.rug.ac.be>
    scale_factor calc speedup.
    subband_quantization speedup

Federico Grau <grauf at rfa.org>
Bill Eldridge <bill at hk.rfa.org>
    option for "no padding"

Nick Burch  <gagravarr at SoftHome.net>
    WAV file reading
    os/2 Makefile mods.

Phillipe Jouguet <philippe.jouguet at vdldiffusion.com>
    DAB extensions
    spelling, LSF using psyII, WAVE reading

Henrik Herranen - leopold at vlsi.fi
    (WAVE reading)

Andreas Neukoetter - anti at webhome.de
    (verbosity patch '-t' switch for transcode plugin)

Sami Sallinen - sami.sallinen at g-cluster.com
    (filter_subband loop unroll
     psycho_i fix for "% 1408" calcs)

Mike Cheng <mikecheng at NOT planckenergy.com> (remove the NOT)
    Most of the rest

Matthias P. Braendli <matthias@mpb.li>
    PAD insertion for DLS and slides
    Integration into FDK-AAC-DABplus

*********************
REFERENCE PAPERS
*********************
(Specifically LayerII Papers)

Kumar, M & Zubair, M., A high performance software implementation of mpeg audio 
encoder, 1996, ICASSP Conf Proceedings (I think)

Fischer, K.A., Calculation of the psychoacoustic simultaneous masked threshold 
based on MPEG/Audio Encoder Model One, ICSI Technical Report, 1997
ftp://ftp.icsi.berkeley.edu/pub/real/kyrill/PsychoMpegOne.tar.Z 

Hyen-O et al, New Implementation techniques of a real-time mpeg-2 audio encoding 
system. p2287, ICASSP 99.

Imai, T., et al, MPEG-1 Audio real-time encoding system, IEEE Trans on Consumer
Electronics, v44, n3 1998. p888

Teh, D., et al, Efficient bit allocation algorithm for ISO/MPEG audio encoder,
Electronics Letters, v34, n8, p721

Murphy, C & Anandakumar, K, Real-time MPEG-1 audio coding and decoding on a DSP
Chip, IEEE Trans on Consumer Electronics, v43, n1, 1997 p40

Hans, M & Bhaskaran, V., A compliant MPEG-1 layer II audio decoder with 16-B 
arithmetic operations, IEEE Signal Proc Letters v4 n5 1997 p121

[mikecheng at NOT planckenergy.com] remove the NOT
and
[matthias.braendli@mpb.li]
