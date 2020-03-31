#include <cmath>
#include <cstdint>
#include <cstddef>
#include <sstream>

#include "utils.h"
#include <unistd.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

/* Taken from sox */
const char* level(int channel, int peak)
{
    static char const * const text[][2] = {
        /* White: 2dB steps */
        {"", ""}, {"-", "-"}, {"=", "="}, {"-=", "=-"},
        {"==", "=="}, {"-==", "==-"}, {"===", "==="}, {"-===", "===-"},
        {"====", "===="}, {"-====", "====-"}, {"=====", "====="},
        {"-=====", "=====-"}, {"======", "======"},
        /* Red: 1dB steps */
        {"!=====", "=====!"},
    };
    int const red = 1, white = NUMOF(text) - red;

    double linear = ((double)peak) / INT16_MAX;

    int vu_dB = linear ? floor(2 * white + red + linear_to_dB(linear)) : 0;

    int index = vu_dB < 2 * white ?
        MAX(vu_dB / 2, 0) :
        MIN(vu_dB - white, red + white - 1);

    return text[index][channel];
}

size_t strlen_utf8(const char *s) {
    size_t result = 0;

    // ignore continuation bytes - only count single/leading bytes
    while (*s)
        if ((*s++ & 0xC0) != 0x80)
            result++;

    return result;
}

static const std::string ICY_TEXT_SEPARATOR = " - ";

bool write_icy_to_file(const ICY_TEXT_t text, const std::string& filename, bool dl_plus)
{
    FILE* fd = fopen(filename.c_str(), "wb");
    if (fd) {
        bool ret = true;
        bool artist_title_used = !text.artist.empty() and !text.title.empty();

        // if desired, prepend DL Plus information
        if (dl_plus) {
            std::stringstream ss;
            ss << "##### parameters { #####\n";
            ss << "DL_PLUS=1\n";

            // if non-empty text, add tag
            if (artist_title_used) {
                size_t artist_len = strlen_utf8(text.artist.c_str());
                size_t title_start = artist_len + strlen_utf8(ICY_TEXT_SEPARATOR.c_str());

                // ITEM.ARTIST
                ss << "DL_PLUS_TAG=4 0 " << (artist_len - 1) << "\n";   // -1 !

                // ITEM.TITLE
                ss << "DL_PLUS_TAG=1 " << title_start << " " << (strlen_utf8(text.title.c_str()) - 1) << "\n";   // -1 !
            } else if (!text.now_playing.empty()) {
                // PROGRAMME.NOW
                ss << "DL_PLUS_TAG=33 0 " << (strlen_utf8(text.now_playing.c_str()) - 1) << "\n";   // -1 !
            }

            ss << "##### parameters } #####\n";
            ret &= fputs(ss.str().c_str(), fd) >= 0;
        }

        if (artist_title_used) {
            ret &= fputs(text.artist.c_str(), fd) >= 0;
            ret &= fputs(ICY_TEXT_SEPARATOR.c_str(), fd) >= 0;
            ret &= fputs(text.title.c_str(), fd) >= 0;
        }
        else {
            ret &= fputs(text.now_playing.c_str(), fd) >= 0;
        }
        fclose(fd);

        return ret;
    }

    return false;
}
