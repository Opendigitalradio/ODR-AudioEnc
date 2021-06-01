A patched version of fdk-aac with DAB+ support
==============================================

This is a modified version of fdk-aac that supports the AOTs
required for DAB+ encoding.

This library is a dependency for [ODR-AudioEnc](https://github.com/Opendigitalradio/ODR-AudioEnc)

See http://www.opendigitalradio.org for more

Branches in this repository
===========================

The dabplus branch points to a version that builds `libfdk-aac.so.1`.
The dabplus2 branch points to a version that builds `libfdk-aac.so.2`.

Installation
============

Make sure you have installed git, build-essential and automake, otherwise install them with `sudo apt-get install git build-essential automake`.

    $ git clone https://github.com/Opendigitalradio/fdk-aac.git
    $ cd fdk-aac/
    $ ./bootstrap
    $ ./configure
    $ make
    $ sudo make install
