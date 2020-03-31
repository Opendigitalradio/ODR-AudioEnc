#pragma once

#include <string>
#include <cmath>
#include <cstdint>
#include <cstddef>

#define NUMOF(l) (sizeof(l) / sizeof(*l))

#define linear_to_dB(x) (log10(x) * 20)

/*! Calculate the little string containing a bargraph
 * 'VU-meter' from the peak value measured
 */
const char* level(int channel, int peak);

size_t strlen_utf8(const char *s);

struct ICY_TEXT_t {
    std::string artist;
    std::string title;
    std::string now_playing;

    operator bool() const {
        return not (artist.empty() and title.empty() and now_playing.empty());
    }

    bool operator==(const ICY_TEXT_t& other) const {
        return
            artist == other.artist and
            title == other.title and
            now_playing == other.now_playing;
    }
    bool operator!=(const ICY_TEXT_t& other) const {
        return !(*this == other);
    }
    void useArtistTitle(const std::string& artist, const std::string& title) {
        this->artist = artist;
        this->title = title;
        now_playing = "";
    }
    void useNowPlaying(const std::string& now_playing) {
        artist = "";
        title = "";
        this->now_playing = now_playing;
    }
};

/*! Write the corresponding text to a file readable by ODR-PadEnc, with optional
 * DL+ information. The text is passed as a copy because we actually use the
 * m_nowplaying variable which is also accessed in another thread, so better
 * make a copy.
 *
 * \return false on failure
 */
bool write_icy_to_file(const ICY_TEXT_t text, const std::string& filename, bool dl_plus);
