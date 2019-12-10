This TODO file lists ideas and features for future developments. They are
more or less ordered according to their benefit, but that is subjective
to some degree.

Unless written, no activity has been started on the topics.

Sample rate conversion
----------------------

It's impossible to encode from a JACK, ALSA or file source that does not carry
audio at the desired output sample rate.

Implementing libsamplerate or libsoxr integration would enable this.


Drift compenstation statistics
------------------------------

Insert drift compensation statistics into ZeroMQ metadata. This would maybe
need a new protocol version and adaptations in ODR-DabMux, but ideally should
be done in a backward-compatible way.

GStreamer input and AES67
-------------------------

AES67 support could be nice.

GST can apparently use PTP https://gstreamer.freedesktop.org/documentation/net/gstptpclock.html?gi-language=c

https://gstreamer.freedesktop.org/documentation/sdpelem/sdpdemux.html?gi-language=c

https://www.collabora.com/news-and-blog/blog/2017/04/25/receiving-an-aes67-stream-with-gstreamer/

https://archive.fosdem.org/2016/schedule/event/synchronised_gstreamer/attachments/slides/889/export/events/attachments/synchronised_gstreamer/slides/889/synchronised_multidevice_media_playback_with_GStreamer.pdf
