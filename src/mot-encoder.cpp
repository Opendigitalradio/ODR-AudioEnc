/*
    Copyright (C) 2014 CSP Innovazione nelle ICT s.c.a r.l. (http://rd.csp.it/)

    Copyright (C) 2014, 2015 Matthias P. Braendli (http://opendigitalradio.org)

    Copyright (C) 2015 Stefan Pöschel (http://opendigitalradio.org)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    mot-encoder.c
          Generete PAD data for MOT Slideshow and DLS

    Authors:
         Sergio Sagliocco <sergio.sagliocco@csp.it>
         Matthias P. Braendli <matthias@mpb.li>
         Stefan Pöschel <odr@basicmaster.de>
*/

#include <cstdio>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <deque>
#include <list>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <getopt.h>
#include "config.h"
#include "charset.h"


#if HAVE_MAGICKWAND
#  include <wand/magick_wand.h>
#endif

#define DEBUG 0

#define SLEEPDELAY_DEFAULT 10 //seconds

extern "C" {
#include "lib_crc.h"
}

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define XSTR(x) #x
#define STR(x) XSTR(x)

#define MAXSEGLEN 8189 // Bytes (EN 301 234 v2.1.1, ch. 5.1.1)
#define MAXDLS 128 // chars
#define MAXSLIDESIZE 51200 // Bytes (TS 101 499 v3.1.1, ch. 9.1.2)

// Roll-over value for fidx
#define MAXSLIDEID 9999

// How many slides to keep in history
#define MAXHISTORYLEN 50

// Do not allow the image compressor to go below
// JPEG quality 40
#define MINQUALITY 40

// Charsets from TS 101 756
#define CHARSET_COMPLETE_EBU_LATIN 0 // Complete EBU Latin based repertoire
#define CHARSET_EBU_LATIN_CY_GR 1 // EBU Latin based common core, Cyrillic, Greek
#define CHARSET_EBU_LATIN_AR_HE_CY_GR 2 // EBU Latin based core, Arabic, Hebrew, Cyrillic and Greek
#define CHARSET_ISO_LATIN_ALPHABET_2 3 // ISO Latin Alphabet No 2
#define CHARSET_UCS2_BE 6 // ISO/IEC 10646 using UCS-2 transformation format, big endian byte order
#define CHARSET_UTF8 15 // ISO Latin Alphabet No 2


typedef std::vector<uint8_t> uint8_vector_t;
static int verbose = 0;


struct MSCDG {
    // MSC Data Group Header (extension field not supported)
    unsigned char extflag;      //  1 bit
    unsigned char crcflag;      //  1 bit
    unsigned char segflag;      //  1 bit
    unsigned char accflag;      //  1 bit
    unsigned char dgtype;       //  4 bits
    unsigned char cindex;       //  4 bits
    unsigned char rindex;       //  4 bits
    /// Session header - Segment field
    unsigned char last;         //  1 bit
    unsigned short int segnum;  // 16 bits
    // Session header - User access field
    unsigned char rfa;          //  3 bits
    unsigned char tidflag;      //  1 bit
    unsigned char lenid;        //  4 bits - Fixed to value 2 in this implemntation
    unsigned short int tid;     // 16 bits
    // MSC data group data field
    //  Mot Segmentation header
    unsigned char rcount;       //  3 bits
    unsigned short int seglen;  // 13 bits
    // Mot segment
    unsigned char* segdata;
    // MSC data group CRC
    unsigned short int crc;     // 16 bits
};

/* Between collection of slides and transmission, the slide data is saved
 * in this structure.
 */
struct slide_metadata_t {
    // complete path to slide
    std::string filepath;

    // index, values from 0 to MAXSLIDEID, rolls over
    int fidx;

    // This is used to define the order in which several discovered
    // slides are transmitted
    bool operator<(const slide_metadata_t& other) const {
        return this->fidx < other.fidx;
    }
};

/* A simple fingerprint for each slide transmitted.
 * Allows us to reuse the same fidx if the same slide
 * is transmitted more than once.
 */
struct fingerprint_t {
    // file name
    std::string s_name;
    // file size, in bytes
    off_t s_size;
    // time of last modification
    unsigned long s_mtime;

    // assigned fidx, -1 means invalid
    int fidx;

    // The comparison is not done on fidx, only
    // on the file-specific data
    bool operator==(const fingerprint_t& other) const {
        return (((s_name == other.s_name &&
                 s_size == other.s_size) &&
                s_mtime == other.s_mtime));
    }

    void disp(void) {
        printf("%s_%ld_%lu:%d\n", s_name.c_str(), s_size, s_mtime, fidx);
    }

    void load_from_file(const char* filepath)
    {
        struct stat file_attribue;
        const char * final_slash;

        stat(filepath, &file_attribue);
        final_slash = strrchr(filepath, '/');

        // load filename, size and mtime
        // Save only the basename of the filepath
        this->s_name.assign((final_slash == NULL) ? filepath : final_slash + 1);
        this->s_size = file_attribue.st_size;
        this->s_mtime = file_attribue.st_mtime;

        this->fidx = -1;
    }
};

class History {
    public:
        History(size_t hist_size) :
            m_hist_size(hist_size),
            m_last_given_fidx(0) {}
        void disp_database();
        // controller of id base on database
        int get_fidx(const char* filepath);

    private:
        std::deque<fingerprint_t> m_database;

        size_t m_hist_size;

        int m_last_given_fidx;

        // find the fingerprint fp in database.
        // returns the fidx when found,
        //    or   -1 if not found
        int find(const fingerprint_t& fp) const;

        // add a new fingerprint into database
        // returns its fidx
        void add(fingerprint_t& fp);
};


int encodeFile(int output_fd, std::string& fname, int fidx, bool raw_slides);

uint8_vector_t createMotHeader(
        size_t blobsize,
        int fidx,
        bool jfif_not_png);

void createMscDG(MSCDG* msc, unsigned short int dgtype, int *cindex, unsigned short int segnum,
        unsigned short int lastseg, unsigned short int tid, unsigned char* data,
        unsigned short int datalen);

struct DATA_GROUP;
DATA_GROUP* packMscDG(MSCDG* msc);

void prepend_dls_dgs(const std::string& text, uint8_t charset);
void writeDLS(int output_fd, const std::string& dls_file, uint8_t charset, bool raw_dls, bool remove_dls);

// PAD related
#define CRC_LEN 2

struct DATA_GROUP {
    uint8_vector_t data;
    int apptype_start;
    int apptype_cont;
    size_t written;

    DATA_GROUP(size_t len, int apptype_start, int apptype_cont) {
        this->data.resize(len);
        this->apptype_start = apptype_start;
        this->apptype_cont = apptype_cont;
        written = 0;
    }

    void AppendCRC() {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < data.size(); i++)
            crc = update_crc_ccitt(crc, data[i]);
        crc = ~crc;
#if DEBUG
        fprintf(stderr, "crc=%04x ~crc=%04x\n", crc, ~crc);
#endif

        data.push_back((crc & 0xFF00) >> 8);
        data.push_back((crc & 0x00FF));
    }

    size_t Available() {
        return data.size() - written;
    }

    int Write(uint8_t *write_data, size_t len, int *cont_apptype) {
        size_t written_now = std::min(len, Available());

        // fill up remaining bytes with zero padding
        memcpy(write_data, &data[written], written_now);
        memset(write_data + written_now, 0x00, len - written_now);

        // set app type depending on progress
        int apptype = written > 0 ? apptype_cont : apptype_start;

        written += written_now;

        // prevent continuation of a different DG having the same type
        if (cont_apptype)
            *cont_apptype = Available() > 0 ? apptype_cont : -1;

        return apptype;
    }
};

#define SHORT_PAD 6         // F-PAD + 1x CI              + 1x  3 bytes data sub-field
#define VARSIZE_PAD_MIN 8   // F-PAD + 1x CI + end marker + 1x  4 bytes data sub-field
#define VARSIZE_PAD_MAX 196 // F-PAD + 4x CI              + 4x 48 bytes data sub-field
#define ALLOWED_PADLEN "6 (short X-PAD), 8 to 196 (variable size X-PAD)"



// MOT Slideshow related
static int cindex_header = 0;
static int cindex_body = 0;


class MOTHeader {
private:
    size_t header_size;
    uint8_vector_t data;

    void IncrementHeaderSize(size_t size);
    void AddParamHeader(int pli, int param_id) {data.push_back((pli << 6) | (param_id & 0x3F));}
public:
    MOTHeader(size_t body_size, int content_type, int content_subtype);

    void AddExtension(int param_id);
    void AddExtension8Bit(int param_id, uint8_t data_field);
    void AddExtension32Bit(int param_id, uint32_t data_field);
    void AddExtensionVarSize(int param_id, const uint8_t* data_field, size_t data_field_len);
    const uint8_vector_t GetData() {return data;}
};

MOTHeader::MOTHeader(size_t body_size, int content_type, int content_subtype)
: header_size(0), data(uint8_vector_t(7, 0x00)) {
    // init header core

    // body size
    data[0] = (body_size >> 20) & 0xFF;
    data[1] = (body_size >> 12) & 0xFF;
    data[2] = (body_size >>  4) & 0xFF;
    data[3] = (body_size <<  4) & 0xF0;

    // header size
    IncrementHeaderSize(data.size());

    // content type
    data[5] |= (content_type << 1) & 0x7E;

    // content subtype
    data[5] |= (content_subtype >> 8) & 0x01;
    data[6] |=  content_subtype       & 0xFF;
}

void MOTHeader::IncrementHeaderSize(size_t size) {
    header_size += size;

    data[3] &= 0xF0;
    data[3] |= (header_size >> 9) & 0x0F;

    data[4]  = (header_size >> 1) & 0xFF;

    data[5] &= 0x7F;
    data[5] |= (header_size << 7) & 0x80;
}

void MOTHeader::AddExtension(int param_id) {
    AddParamHeader(0b00, param_id);

    IncrementHeaderSize(1);
}

void MOTHeader::AddExtension8Bit(int param_id, uint8_t data_field) {
    AddParamHeader(0b01, param_id);
    data.push_back(data_field);

    IncrementHeaderSize(2);
}

void MOTHeader::AddExtension32Bit(int param_id, uint32_t data_field) {
    AddParamHeader(0b10, param_id);
    data.push_back((data_field >> 24) & 0xFF);
    data.push_back((data_field >> 16) & 0xFF);
    data.push_back((data_field >>  8) & 0xFF);
    data.push_back( data_field        & 0xFF);

    IncrementHeaderSize(5);
}

void MOTHeader::AddExtensionVarSize(int param_id, const uint8_t* data_field, size_t data_field_len) {
    AddParamHeader(0b11, param_id);

    // longer field lens use 15 instead of 7 bits
    bool ext = data_field_len > 127;
    if (ext) {
        data.push_back(0x80 | ((data_field_len >> 8) & 0x7F));
        data.push_back(data_field_len & 0xFF);
    } else {
        data.push_back(data_field_len & 0x7F);
    }

    for (size_t i = 0; i < data_field_len; i++)
        data.push_back(data_field[i]);

    IncrementHeaderSize(1 + (ext ? 2 : 1) + data_field_len);
}



// DLS related
#define FPAD_LEN 2
#define DLS_SEG_LEN_PREFIX       2
#define DLS_SEG_LEN_CHAR_MAX    16
#define DLS_CMD_REMOVE_LABEL    0x01

CharsetConverter charset_converter;

typedef uint8_vector_t pad_t;
static bool dls_toggle = false;
static std::string dlstext_prev = "";
static bool dlstext_prev_set = false;



class PADPacketizer {
private:
    static const size_t subfield_lens[];

    const size_t xpad_size_max;
    const bool short_xpad;
    const size_t max_cis;

    size_t xpad_size;
    uint8_t subfields[4*48];
    size_t subfields_size;

    // PAD w/  CI list
    int ci_type[4];
    size_t ci_len_index[4];
    size_t used_cis;

    // PAD w/o CI list
    int last_ci_type;
    size_t last_ci_size;

    size_t AddCINeededBytes();
    void AddCI(int apptype, int len_index);

    int OptimalSubFieldSizeIndex(size_t available_bytes);
    int WriteDGToSubField(DATA_GROUP* dg, size_t len);

    bool AppendDG(DATA_GROUP* dg);
    void AppendDGWithCI(DATA_GROUP* dg);
    void AppendDGWithoutCI(DATA_GROUP* dg);

    void ResetPAD();
    pad_t* FlushPAD();
public:
    std::deque<DATA_GROUP*> queue;

    PADPacketizer(size_t pad_size);
    ~PADPacketizer();

    pad_t* GetPAD();

    // will be removed, when pull (instead of push) approach is implemented!
    void WriteAllPADs(int output_fd);
};


const size_t PADPacketizer::subfield_lens[] = {4, 6, 8, 12, 16, 24, 32, 48};

PADPacketizer::PADPacketizer(size_t pad_size) :
    xpad_size_max(pad_size - FPAD_LEN),
    short_xpad(pad_size == SHORT_PAD),
    max_cis(short_xpad ? 1 : 4),
    last_ci_type(-1)
{
    ResetPAD();
}

PADPacketizer::~PADPacketizer() {
    while (!queue.empty()) {
        delete queue.front();
        queue.pop_front();
    }
}


pad_t* PADPacketizer::GetPAD() {
    bool pad_flushable = false;

    // process DG queue
    while (!pad_flushable && !queue.empty()) {
        DATA_GROUP* dg = queue.front();

        // repeatedly append DG
        while (!pad_flushable && dg->Available() > 0)
            pad_flushable = AppendDG(dg);

        if (dg->Available() == 0) {
            delete dg;
            queue.pop_front();
        }
    }

    // (possibly empty) PAD
    return FlushPAD();
}

void PADPacketizer::WriteAllPADs(int output_fd) {
    for (;;) {
        pad_t* pad = GetPAD();

        // if only F-PAD present, abort
        if (pad->back() == FPAD_LEN) {
            delete pad;
            break;
        }

        ssize_t dummy = write(output_fd, &(*pad)[0], pad->size());
        delete pad;
    }
}


size_t PADPacketizer::AddCINeededBytes() {
    // returns the amount of additional bytes needed for the next CI

    // special cases: end marker added/replaced
    if (!short_xpad && used_cis == 0)
        return 2;
    if (!short_xpad && used_cis == (max_cis - 1))
        return 0;
    return 1;
}

void PADPacketizer::AddCI(int apptype, int len_index) {
    ci_type[used_cis] = apptype;
    ci_len_index[used_cis] = len_index;

    xpad_size += AddCINeededBytes();
    used_cis++;
}


int PADPacketizer::OptimalSubFieldSizeIndex(size_t available_bytes) {
    /* Return the index of the optimal sub-field size by stepwise search (regards only Variable Size X-PAD):
     * - find the smallest sub-field able to hold (at least) all available bytes
     * - find the biggest regarding sub-field we have space for (which definitely exists - otherwise previously the PAD would have been flushed)
     * - if the wasted space is at least as big as the smallest possible sub-field, use a sub-field one size smaller
     */
    int len_index = 0;

    while ((len_index + 1) < 8 && subfield_lens[len_index] < available_bytes)
        len_index++;
    while ((len_index - 1) >= 0 && (subfield_lens[len_index] + AddCINeededBytes()) > (xpad_size_max - xpad_size))
        len_index--;
    if ((len_index - 1) >= 0 && ((int) subfield_lens[len_index] - (int) available_bytes) >= (int) subfield_lens[0])
        len_index--;

    return len_index;
}

int PADPacketizer::WriteDGToSubField(DATA_GROUP* dg, size_t len) {
    int apptype = dg->Write(&subfields[subfields_size], len, &last_ci_type);
    subfields_size += len;
    xpad_size += len;
    return apptype;
}


bool PADPacketizer::AppendDG(DATA_GROUP* dg) {
    /* use X-PAD w/o CIs instead of X-PAD w/ CIs, if we can save some bytes or at least do not waste additional bytes
     *
     * Omit CI list in case:
     * 1.   no pending data sub-fields
     * 2.   last CI type valid
     * 3.   last CI type matching current (continuity) CI type
     * 4a.  short X-PAD; OR
     * 4ba. size of the last X-PAD being at least as big as the available X-PAD payload in case all CIs are used AND
     * 4bb. the amount of available DG bytes being at least as big as the size of the last X-PAD in case all CIs are used
     */
    if (
            used_cis == 0 &&
            last_ci_type != -1 &&
            last_ci_type == dg->apptype_cont &&
            (short_xpad ||
                    (last_ci_size >= (xpad_size_max - max_cis) &&
                            dg->Available() >= (last_ci_size - max_cis)))
            ) {
        AppendDGWithoutCI(dg);
        return true;
    } else {
        AppendDGWithCI(dg);

        // if no further sub-fields could be added, PAD must be flushed
        if (used_cis == max_cis || subfield_lens[0] + AddCINeededBytes() > (xpad_size_max - xpad_size))
            return true;
    }
    return false;
}


void PADPacketizer::AppendDGWithCI(DATA_GROUP* dg) {
    int len_index = short_xpad ? 0 : OptimalSubFieldSizeIndex(dg->Available());
    size_t len_size = short_xpad ? 3 : subfield_lens[len_index];

    int apptype = WriteDGToSubField(dg, len_size);
    AddCI(apptype, len_index);

#if DEBUG
    fprintf(stderr, "PADPacketizer: added sub-field w/  CI - type: %2d, size: %2zu\n", apptype, len_size);
#endif
}

void PADPacketizer::AppendDGWithoutCI(DATA_GROUP* dg) {
#if DEBUG
    int old_last_ci_type = last_ci_type;
#endif

    WriteDGToSubField(dg, last_ci_size);

#if DEBUG
    fprintf(stderr, "PADPacketizer: added sub-field w/o CI - type: %2d, size: %2zu\n", old_last_ci_type, last_ci_size);
#endif
}

void PADPacketizer::ResetPAD() {
    xpad_size = 0;
    subfields_size = 0;
    used_cis = 0;
}

pad_t* PADPacketizer::FlushPAD() {
    pad_t* result = new pad_t(xpad_size_max + FPAD_LEN + 1);
    pad_t &pad = *result;

    size_t pad_offset = xpad_size_max;

    if (subfields_size > 0) {
        if (used_cis > 0) {
            // X-PAD: CIs
            for (int i = 0; i < used_cis; i++)
                pad[--pad_offset] = (short_xpad ? 0 : ci_len_index[i]) << 5 | ci_type[i];

            // X-PAD: end marker (if needed)
            if (used_cis < max_cis)
                pad[--pad_offset] = 0x00;
        }

        // X-PAD: sub-fields (reversed on-the-fly)
        for (size_t off = 0; off < subfields_size; off++)
            pad[--pad_offset] = subfields[off];
    } else {
        // no X-PAD
        last_ci_type = -1;
    }

    // zero padding
    memset(&pad[0], 0x00, pad_offset);

    // F-PAD
    pad[xpad_size_max + 0] = subfields_size > 0 ? (short_xpad ? 0x10 : 0x20) : 0x00;
    pad[xpad_size_max + 1] = subfields_size > 0 ? (used_cis > 0 ? 0x02 : 0x00) : 0x00;

    // used PAD len
    pad[xpad_size_max + FPAD_LEN] = xpad_size + FPAD_LEN;

    last_ci_size = xpad_size;
    ResetPAD();
    return result;
}


static PADPacketizer *pad_packetizer;







void usage(char* name)
{
    fprintf(stderr, "DAB MOT encoder %s for slideshow and DLS\n\n"
                    "By CSP Innovazione nelle ICT s.c.a r.l. (http://rd.csp.it/) and\n"
                    "Opendigitalradio.org\n\n"
                    "Reads image data from the specified directory, DLS text from a file,\n"
                    "and outputs PAD data to the given FIFO.\n"
                    "  http://opendigitalradio.org\n\n",
#if defined(GITVERSION)
                    GITVERSION
#else
                    PACKAGE_VERSION
#endif
                    );
    fprintf(stderr, "Usage: %s [OPTIONS...]\n", name);
    fprintf(stderr, " -d, --dir=DIRNAME      Directory to read images from.\n"
                    " -e, --erase            Erase slides from DIRNAME once they have\n"
                    "                          been encoded.\n"
                    " -s, --sleep=DELAY      Wait DELAY seconds between each slide\n"
                    "                          Default: " STR(SLEEPDELAY_DEFAULT) "\n"
                    " -o, --output=FILENAME  Fifo to write PAD data into.\n"
                    "                          Default: /tmp/pad.fifo\n"
                    " -t, --dls=FILENAME     Fifo or file to read DLS text from.\n"
                    " -p, --pad=LENGTH       Set the pad length.\n"
                    "                          Possible values: " ALLOWED_PADLEN "\n"
                    "                          Default: 58\n"
                    " -c, --charset=ID       ID of the character set encoding used for DLS text input.\n"
                    "                          ID =  0: Complete EBU Latin based repertoire\n"
                    "                          ID =  6: ISO/IEC 10646 using UCS-2 BE\n"
                    "                          ID = 15: ISO/IEC 10646 using UTF-8\n"
                    "                          Default: 15\n"
                    " -r, --remove-dls       Always insert a DLS Remove Label command when replacing a DLS text.\n"
                    " -C, --raw-dls          Do not convert DLS texts to Complete EBU Latin based repertoire\n"
                    "                          character set encoding.\n"
                    " -R, --raw-slides       Do not process slides. Integrity checks and resizing\n"
                    "                          slides is skipped. Use this if you know what you are doing !\n"
                    "                          It is useful only when -d is used\n"
                    " -v, --verbose          Print more information to the console\n"
           );
}

#define no_argument 0
#define required_argument 1
#define optional_argument 2
int main(int argc, char *argv[])
{
    int len, ret;
    struct dirent *pDirent;
    DIR *pDir = NULL;
    int  padlen = 58;
    bool erase_after_tx = false;
    int  sleepdelay = SLEEPDELAY_DEFAULT;
    bool raw_slides = false;
    int  charset = CHARSET_UTF8;
    bool raw_dls = false;
    bool remove_dls = false;

    const char* dir = NULL;
    const char* output = "/tmp/pad.fifo";
    std::string dls_file;

    const struct option longopts[] = {
        {"charset",    required_argument,  0, 'c'},
        {"raw-dls",    no_argument,        0, 'C'},
        {"remove-dls", no_argument,        0, 'r'},
        {"dir",        required_argument,  0, 'd'},
        {"erase",      no_argument,        0, 'e'},
        {"output",     required_argument,  0, 'o'},
        {"dls",        required_argument,  0, 't'},
        {"pad",        required_argument,  0, 'p'},
        {"sleep",      required_argument,  0, 's'},
        {"raw-slides", no_argument,        0, 'R'},
        {"help",       no_argument,        0, 'h'},
        {"verbose",    no_argument,        0, 'v'},
        {0,0,0,0},
    };

    int ch=0;
    int index;
    while(ch != -1) {
        ch = getopt_long(argc, argv, "eChRrc:d:o:p:s:t:v", longopts, &index);
        switch (ch) {
            case 'c':
                charset = atoi(optarg);
                break;
            case 'C':
                raw_dls = true;
                break;
            case 'r':
                remove_dls = true;
                break;
            case 'd':
                dir = optarg;
                break;
            case 'e':
                erase_after_tx = true;
                break;
            case 'o':
                output = optarg;
                break;
            case 's':
                sleepdelay = atoi(optarg);
                break;
            case 't':
                dls_file = optarg;
                break;
            case 'p':
                padlen = atoi(optarg);
                break;
            case 'R':
                raw_slides = true;
                break;
            case 'v':
                verbose++;
                break;
            case '?':
            case 'h':
                usage(argv[0]);
                return 0;
        }
    }

    if (padlen != SHORT_PAD && (padlen < VARSIZE_PAD_MIN || padlen > VARSIZE_PAD_MAX)) {
        fprintf(stderr, "mot-encoder Error: pad length %d invalid: Possible values: "
                ALLOWED_PADLEN "\n",
                padlen);
        return 2;
    }

    if (dir && not dls_file.empty()) {
        fprintf(stderr, "mot-encoder encoding Slideshow from '%s' and DLS from '%s' to '%s'\n",
                dir, dls_file.c_str(), output);
    }
    else if (dir) {
        fprintf(stderr, "mot-encoder encoding Slideshow from '%s' to '%s'. No DLS.\n",
                dir, output);
    }
    else if (not dls_file.empty()) {
        fprintf(stderr, "mot-encoder encoding DLS from '%s' to '%s'. No Slideshow.\n",
                dls_file.c_str(), output);
    }
    else {
        fprintf(stderr, "mot-encoder Error: No DLS nor slideshow to encode !\n");
        usage(argv[0]);
        return 1;
    }

    const char* user_charset;
    switch (charset) {
        case CHARSET_COMPLETE_EBU_LATIN:
            user_charset = "Complete EBU Latin";
            break;
        case CHARSET_EBU_LATIN_CY_GR:
            user_charset = "EBU Latin core, Cyrillic, Greek";
            break;
        case CHARSET_EBU_LATIN_AR_HE_CY_GR:
            user_charset = "EBU Latin core, Arabic, Hebrew, Cyrillic, Greek";
            break;
        case CHARSET_ISO_LATIN_ALPHABET_2:
            user_charset = "ISO Latin Alphabet 2";
            break;
        case CHARSET_UCS2_BE:
            user_charset = "UCS-2 BE";
            break;
        case CHARSET_UTF8:
            user_charset = "UTF-8";
            break;
        default:
            user_charset = "Invalid";
            charset = -1;
            break;
    }

    if (charset == -1) {
        fprintf(stderr, "mot-encoder Error: Invalid charset!\n");
        usage(argv[0]);
        return 1;
    }
    else {
        fprintf(stderr, "mot-encoder using charset %s (%d)\n",
               user_charset, charset);
    }

    if (not raw_dls) {
        switch (charset) {
        case CHARSET_COMPLETE_EBU_LATIN:
            // no conversion needed
            break;
        case CHARSET_UTF8:
            fprintf(stderr, "mot-encoder converting DLS texts to Complete EBU Latin\n");
            break;
        default:
            fprintf(stderr, "mot-encoder Error: DLS conversion to EBU is currently only supported for UTF-8 input!\n");
            return 1;
        }
    }

    int output_fd = open(output, O_WRONLY);
    if (output_fd == -1) {
        perror("mot-encoder Error: failed to open output");
        return 3;
    }

#if HAVE_MAGICKWAND
    MagickWandGenesis();
#endif

    pad_packetizer = new PADPacketizer(padlen);

    std::list<slide_metadata_t> slides_to_transmit;
    History slides_history(MAXHISTORYLEN);

    while(1) {
        if (dir) {
            pDir = opendir(dir);
            if (pDir == NULL) {
                fprintf(stderr, "mot-encoder Error: cannot open directory '%s'\n", dir);
                return 1;
            }

            // Add new slides to transmit to list
            while ((pDirent = readdir(pDir)) != NULL) {
                if (pDirent->d_name[0] != '.') {
                    char imagepath[256];
                    sprintf(imagepath, "%s/%s", dir, pDirent->d_name);

                    slide_metadata_t md;
                    md.filepath = imagepath;
                    md.fidx     = slides_history.get_fidx(imagepath);
                    slides_to_transmit.push_back(md);

                    if (verbose) {
                        fprintf(stderr, "mot-encoder found slide '%s', fidx %d\n", imagepath, md.fidx);
                    }
                }
            }

#if DEBUG
            slides_history.disp_database();
#endif

            // Sort the list in fidx order
            slides_to_transmit.sort();

            if (not dls_file.empty()) {
                // Maybe we have no slides, always update DLS
                writeDLS(output_fd, dls_file, charset, raw_dls, remove_dls);
                sleep(sleepdelay);
            }

            // Encode the slides
            std::list<slide_metadata_t>::iterator it;
            for (it = slides_to_transmit.begin();
                    it != slides_to_transmit.end();
                    ++it) {

                ret = encodeFile(output_fd, it->filepath, it->fidx, raw_slides);
                if (ret != 1) {
                    fprintf(stderr, "mot-encoder Error: cannot encode file '%s'\n", it->filepath.c_str());
                }

                if (erase_after_tx) {
                    if (unlink(it->filepath.c_str()) == -1) {
                        fprintf(stderr, "mot-encoder Error: erasing file '%s' failed: ", it->filepath.c_str());
                        perror("");
                    }
                }

                // Always retransmit DLS after each slide, we want it to be updated frequently
                if (not dls_file.empty()) {
                    writeDLS(output_fd, dls_file, charset, raw_dls, remove_dls);
                }

                sleep(sleepdelay);
            }

            if (slides_to_transmit.empty()) {
                sleep(sleepdelay);
            }

            slides_to_transmit.resize(0);
        }
        else if (not dls_file.empty()) { // only DLS
            // Always retransmit DLS, we want it to be updated frequently
            writeDLS(output_fd, dls_file, charset, raw_dls, remove_dls);

            sleep(sleepdelay);
        }

        if (pDir) {
            closedir(pDir);
        }
    }

    delete pad_packetizer;

    return 1;
}


DATA_GROUP* createDataGroupLengthIndicator(size_t len) {
    DATA_GROUP* dg = new DATA_GROUP(2, 1, 1);    // continuation never used (except for comparison at short X-PAD)
    uint8_vector_t &data = dg->data;

    // Data Group length
    data[0] = (len & 0x3F00) >> 8;
    data[1] = (len & 0x00FF);

    // CRC
    dg->AppendCRC();

    return dg;
}


void warnOnSmallerImage(size_t height, size_t width, std::string& fname) {
    if (height < 240 || width < 320)
        fprintf(stderr, "mot-encoder Warning: Image '%s' smaller than recommended size (%zu x %zu < 320 x 240 px)\n", fname.c_str(), width, height);
}


// Scales the image down if needed,
// so that it is 320x240 pixels.
// Automatically reduces the quality to make sure the
// blobsize is not too large.
//
// Returns: the blobsize
#if HAVE_MAGICKWAND
size_t resizeImage(MagickWand* m_wand, unsigned char** blob, std::string& fname)
{
    size_t blobsize;
    size_t height = MagickGetImageHeight(m_wand);
    size_t width  = MagickGetImageWidth(m_wand);

    PixelWand  *p_wand = NULL;

    while (height > 240 || width > 320) {
        if (height/240.0 > width/320.0) {
            width = width * 240.0 / height;
            height = 240;
        }
        else {
            height = height * 320.0 / width;
            width = 320;
        }
        MagickResizeImage(m_wand, width, height, LanczosFilter, 1);
    }

    height = MagickGetImageHeight(m_wand);
    width  = MagickGetImageWidth(m_wand);

    MagickSetImageFormat(m_wand, "jpg");

    int quality = 100;

    do {
        quality -= 5;

        MagickSetImageCompressionQuality(m_wand, quality);
        *blob = MagickGetImagesBlob(m_wand, &blobsize);
    } while (blobsize > MAXSLIDESIZE && quality > MINQUALITY);

    if (blobsize > MAXSLIDESIZE) {
        fprintf(stderr, "mot-encoder: Image Size too large after compression: %zu bytes\n",
                blobsize);

        return 0;
    }

    if (verbose) {
        fprintf(stderr, "mot-encoder resized image to %zu x %zu. Size after compression %zu bytes (q=%d)\n",
                width, height, blobsize, quality);
    }

    // warn if resized image smaller than default dimension
    warnOnSmallerImage(height, width, fname);

    return blobsize;
}
#endif

int encodeFile(int output_fd, std::string& fname, int fidx, bool raw_slides)
{
    int ret = 0;
    int fd=0, nseg, lastseglen, i, last, curseglen;
#if HAVE_MAGICKWAND
    MagickWand *m_wand = NULL;
    MagickBooleanType err;
#endif
    size_t blobsize, height, width;
    bool jpeg_progr;
    unsigned char *blob = NULL;
    unsigned char *curseg = NULL;
    MSCDG msc;
    DATA_GROUP* dgli;
    DATA_GROUP* mscdg;

    size_t orig_quality;
    char*  orig_format = NULL;
    /* We handle JPEG differently, because we want to avoid recompressing the
     * image if it is suitable as is
     */
    bool orig_is_jpeg = false;

    /* If the original is a PNG, we transmit it as is, if the resolution is correct
     * and the file is not too large. Otherwise it gets resized and sent as JPEG.
     */
    bool orig_is_png = false;

    /* By default, we do resize the image to 320x240, with a quality such that
     * the blobsize is at most MAXSLIDESIZE.
     *
     * For JPEG input files that are already at the right resolution and at the
     * right blobsize, we disable this to avoid quality loss due to recompression
     *
     * As device support of this feature is optional, we furthermore require JPEG input
     * files to not have progressive coding.
     */
    bool resize_required = true;

    bool jfif_not_png = true;

    if (!raw_slides) {
#if HAVE_MAGICKWAND

        m_wand = NewMagickWand();

        err = MagickReadImage(m_wand, fname.c_str());
        if (err == MagickFalse) {
            fprintf(stderr, "mot-encoder Error: Unable to load image '%s'\n",
                    fname.c_str());

            goto encodefile_out;
        }

        height       = MagickGetImageHeight(m_wand);
        width        = MagickGetImageWidth(m_wand);
        orig_format  = MagickGetImageFormat(m_wand);
        jpeg_progr   = MagickGetImageInterlaceScheme(m_wand) == JPEGInterlace;

        // By default assume that the image has full quality and can be reduced
        orig_quality = 100;

        // strip unneeded information (profiles, meta data)
        MagickStripImage(m_wand);

        if (orig_format) {
            if (strcmp(orig_format, "JPEG") == 0) {
                orig_quality = MagickGetImageCompressionQuality(m_wand);
                orig_is_jpeg = true;

                if (verbose) {
                    fprintf(stderr, "mot-encoder image: '%s' (id=%d)."
                            " Original size: %zu x %zu. (%s, q=%zu, progr=%s)\n",
                            fname.c_str(), fidx, width, height, orig_format, orig_quality, jpeg_progr ? "y" : "n");
                }
            }
            else if (strcmp(orig_format, "PNG") == 0) {
                orig_is_png = true;
                jfif_not_png = false;

                if (verbose) {
                    fprintf(stderr, "mot-encoder image: '%s' (id=%d)."
                            " Original size: %zu x %zu. (%s)\n",
                            fname.c_str(), fidx, width, height, orig_format);
                }
            }
            else if (verbose) {
                fprintf(stderr, "mot-encoder image: '%s' (id=%d)."
                        " Original size: %zu x %zu. (%s)\n",
                        fname.c_str(), fidx, width, height, orig_format);
            }

            free(orig_format);
        }
        else {
            fprintf(stderr, "mot-encoder Warning: Unable to detect image format of '%s'\n",
                    fname.c_str());

            fprintf(stderr, "mot-encoder image: '%s' (id=%d).  Original size: %zu x %zu.\n",
                    fname.c_str(), fidx, width, height);
        }

        if ((orig_is_jpeg || orig_is_png) && height <= 240 && width <= 320 && not jpeg_progr) {
            // Don't recompress the image and check if the blobsize is suitable
            blob = MagickGetImagesBlob(m_wand, &blobsize);

            if (blobsize <= MAXSLIDESIZE) {
                if (verbose) {
                    fprintf(stderr, "mot-encoder image: '%s' (id=%d).  No resize needed: %zu Bytes\n",
                            fname.c_str(), fidx, blobsize);
                }
                resize_required = false;
            }
        }

        if (resize_required) {
            blobsize = resizeImage(m_wand, &blob, fname);

            // resizeImage always creates a jpg output
            jfif_not_png = true;
        }
        else {
            // warn if unresized image smaller than default dimension
            warnOnSmallerImage(height, width, fname);
        }

#else
        fprintf(stderr, "mot-encoder has not been compiled with MagickWand, only RAW slides are supported!\n");
        ret = -1;
        goto encodefile_out;
#endif
    }
    else { // Use RAW data, it might not even be a jpg !
        // read file
        FILE* pFile = fopen(fname.c_str(), "rb");
        if (pFile == NULL) {
            fprintf(stderr, "mot-encoder Error: Unable to load file '%s'\n",
                    fname.c_str());
            goto encodefile_out;
        }

        // obtain file size:
        fseek(pFile, 0, SEEK_END);
        blobsize = ftell(pFile);
        rewind(pFile);

        if (blobsize > MAXSLIDESIZE) {
            fprintf(stderr, "mot-encoder Warning: blob in raw-slide '%s' too large\n",
                    fname.c_str());
        }

        // allocate memory to contain the whole file:
        blob = (unsigned char*)malloc(sizeof(char) * blobsize);
        if (blob == NULL) {
            fprintf(stderr, "mot-encoder Error: Memory allocation error \n");
            goto encodefile_out;
        }

        // copy the file into the buffer:
        size_t dummy = fread(blob, 1, blobsize, pFile);

        size_t last_dot = fname.rfind(".");

        // default:
        jfif_not_png = true; // This is how we did it in the past.
                             // It's wrong anyway, so we're at least compatible

        if (last_dot != std::string::npos) {
            std::string file_extension = fname.substr(last_dot, std::string::npos);

            std::transform(file_extension.begin(), file_extension.end(), file_extension.begin(), ::tolower);

            if (file_extension == ".png") {
                jfif_not_png = false;
            }
        }

        if (pFile != NULL) {
            fclose(pFile);
        }
    }

    if (blobsize) {
        nseg = blobsize / MAXSEGLEN;
        lastseglen = blobsize % MAXSEGLEN;
        if (lastseglen != 0) {
            nseg++;
        }

        uint8_vector_t mothdr = createMotHeader(blobsize, fidx, jfif_not_png);
        // Create the MSC Data Group C-Structure
        createMscDG(&msc, 3, &cindex_header, 0, 1, fidx, &mothdr[0], mothdr.size());
        // Generate the MSC DG frame (Figure 9 en 300 401)
        mscdg = packMscDG(&msc);
        dgli = createDataGroupLengthIndicator(mscdg->data.size());

        pad_packetizer->queue.push_back(dgli);
        pad_packetizer->queue.push_back(mscdg);

        for (i = 0; i < nseg; i++) {
            curseg = blob + i * MAXSEGLEN;
            if (i == nseg-1) {
                curseglen = lastseglen;
                last = 1;
            }
            else {
                curseglen = MAXSEGLEN;
                last = 0;
            }

            createMscDG(&msc, 4, &cindex_body, i, last, fidx, curseg, curseglen);
            mscdg = packMscDG(&msc);
            dgli = createDataGroupLengthIndicator(mscdg->data.size());

            pad_packetizer->queue.push_back(dgli);
            pad_packetizer->queue.push_back(mscdg);
        }

        pad_packetizer->WriteAllPADs(output_fd);

        ret = 1;
    }

encodefile_out:
#if HAVE_MAGICKWAND
    if (m_wand) {
        m_wand = DestroyMagickWand(m_wand);
    }
#endif

    if (blob) {
        free(blob);
    }
    return ret;
}


uint8_vector_t createMotHeader(size_t blobsize, int fidx, bool jfif_not_png)
{
    // prepare ContentName
    uint8_t cntemp[10];     // = 1 + 8 + 1 = charset + name + terminator
    cntemp[0] = 0x0 << 4;   // charset: 0 (Complete EBU Latin based) - doesn't really matter here
    snprintf((char*) (cntemp + 1), sizeof(cntemp) - 1, "%04d.%s", fidx, jfif_not_png ? "jpg" : "png");
    if (verbose)
        fprintf(stderr, "mot-encoder writing image as '%s'\n", cntemp + 1);

    // MOT header - content type: image, content subtype: JFIF / PNG
    MOTHeader header(blobsize, 0x02, jfif_not_png ? 0x001 : 0x003);

    // TriggerTime: NOW
    header.AddExtension32Bit(0x05, 0x00000000);

    // ContentName: XXXX.jpg / XXXX.png
    header.AddExtensionVarSize(0x0C, cntemp, sizeof(cntemp) - 1);   // omit terminator

    return header.GetData();
}


void createMscDG(MSCDG* msc, unsigned short int dgtype,
        int *cindex, unsigned short int segnum, unsigned short int lastseg,
        unsigned short int tid, unsigned char* data,
        unsigned short int datalen)
{
    msc->extflag = 0;
    msc->crcflag = 1;
    msc->segflag = 1;
    msc->accflag = 1;
    msc->dgtype = dgtype;
    msc->cindex = *cindex;
    msc->rindex = 0;
    msc->last = lastseg;
    msc->segnum = segnum;
    msc->rfa = 0;
    msc->tidflag = 1;
    msc->lenid = 2;
    msc->tid = tid;
    msc->segdata = data;
    msc->rcount = 0;
    msc->seglen = datalen;

    *cindex = (*cindex + 1) % 16;   // increment continuity index
}


DATA_GROUP* packMscDG(MSCDG* msc)
{
    DATA_GROUP* dg = new DATA_GROUP(9 + msc->seglen, 12, 13);
    uint8_vector_t &b = dg->data;

    // headers
    b[0] = (msc->extflag<<7) | (msc->crcflag<<6) | (msc->segflag<<5) |
           (msc->accflag<<4) | msc->dgtype;

    b[1] = (msc->cindex<<4) | msc->rindex;
    b[2] = (msc->last<<7) | ((msc->segnum & 0x7F00) >> 8);
    b[3] =  msc->segnum & 0x00FF;
    b[4] = 0;
    b[4] = (msc->rfa << 5) | (msc->tidflag << 4) | msc->lenid;
    b[5] = (msc->tid & 0xFF00) >> 8;
    b[6] =  msc->tid & 0x00FF;
    b[7] = (msc->rcount << 5) | ((msc->seglen & 0x1F00)>>8);
    b[8] =  msc->seglen & 0x00FF;

    // data field
    memcpy(&b[9], msc->segdata, msc->seglen);

    // CRC
    dg->AppendCRC();

    return dg;
}


DATA_GROUP* createDynamicLabelCommand(uint8_t command) {
    DATA_GROUP* dg = new DATA_GROUP(2, 2, 3);
    uint8_vector_t &seg_data = dg->data;

    // prefix: toggle? + first seg + last seg + command flag + command
    seg_data[0] =
            (dls_toggle ? (1 << 7) : 0) +
            (1 << 6) +
            (1 << 5) +
            (1 << 4) +
            command;

    // prefix: charset (though irrelevant here)
    seg_data[1] = CHARSET_COMPLETE_EBU_LATIN;

    // CRC
    dg->AppendCRC();

    return dg;
}

void writeDLS(int output_fd, const std::string& dls_file, uint8_t charset, bool raw_dls, bool remove_dls)
{
    std::ifstream dls_fstream(dls_file.c_str());
    if (!dls_fstream.is_open()) {
        std::cerr << "Could not open " << dls_file << std::endl;
        return;
    }

    std::vector<std::string> dls_lines;

    std::string line;
    // Read and convert lines one by one because the converter doesn't understand
    // line endings
    while (std::getline(dls_fstream, line)) {
        if (not line.empty()) {
            if (not raw_dls && charset == CHARSET_UTF8) {
                dls_lines.push_back(charset_converter.convert(line));
            }
            else {
                dls_lines.push_back(line);
            }
            // TODO handle the other charsets accordingly
        }
    }

    std::stringstream ss;
    for (size_t i = 0; i < dls_lines.size(); i++) {
        if (i != 0) {
            if (charset == CHARSET_UCS2_BE)
                ss << '\0' << '\n';
            else
                ss << '\n';
        }

        // UCS-2 BE: if from file the first byte of \0\n remains, remove it
        if (charset == CHARSET_UCS2_BE && dls_lines[i].size() % 2) {
            dls_lines[i].resize(dls_lines[i].size() - 1);
        }

        ss << dls_lines[i];
    }
    std::string dlstext = ss.str();
    if (dlstext.size() > MAXDLS)
        dlstext.resize(MAXDLS);

    if (not raw_dls)
        charset = CHARSET_COMPLETE_EBU_LATIN;


    // Toggle the toggle bit only on (first call or) new text
    bool dlstext_is_new = !dlstext_prev_set || (dlstext != dlstext_prev);
    if (verbose) {
        fprintf(stderr, "mot-encoder writing %s DLS text \"%s\"\n", dlstext_is_new ? "new" : "old", dlstext.c_str());
    }

    DATA_GROUP *remove_label_dg = NULL;
    if (dlstext_is_new) {
        if (remove_dls)
            remove_label_dg = createDynamicLabelCommand(DLS_CMD_REMOVE_LABEL);

        dls_toggle = !dls_toggle;   // indicate changed text

        dlstext_prev = dlstext;
        dlstext_prev_set = true;
    }

    prepend_dls_dgs(dlstext, charset);
    if (remove_label_dg)
        pad_packetizer->queue.push_front(remove_label_dg);
    pad_packetizer->WriteAllPADs(output_fd);
}


int dls_count(const std::string& text) {
    size_t text_len = text.size();
    return text_len / DLS_SEG_LEN_CHAR_MAX + (text_len % DLS_SEG_LEN_CHAR_MAX ? 1 : 0);
}


DATA_GROUP* dls_get(const std::string& text, uint8_t charset, unsigned int seg_index) {
    bool first_seg = seg_index == 0;
    bool last_seg  = seg_index == dls_count(text) - 1;

    int seg_text_offset = seg_index * DLS_SEG_LEN_CHAR_MAX;
    const char *seg_text_start = text.c_str() + seg_text_offset;
    size_t seg_text_len = MIN(text.size() - seg_text_offset, DLS_SEG_LEN_CHAR_MAX);

    DATA_GROUP* dg = new DATA_GROUP(DLS_SEG_LEN_PREFIX + seg_text_len, 2, 3);
    uint8_vector_t &seg_data = dg->data;

    // prefix: toggle? + first seg? + last seg? + (seg len - 1)
    seg_data[0] =
            (dls_toggle ? (1 << 7) : 0) +
            (first_seg  ? (1 << 6) : 0) +
            (last_seg   ? (1 << 5) : 0) +
            (seg_text_len - 1);

    // prefix: charset / seg index
    seg_data[1] = (first_seg ? charset : seg_index) << 4;

    // character field
    memcpy(&seg_data[DLS_SEG_LEN_PREFIX], seg_text_start, seg_text_len);

    // CRC
    dg->AppendCRC();

#if DEBUG
    fprintf(stderr, "DL segment:");
    for (int i = 0; i < seg_data.size(); i++)
        fprintf(stderr, " %02x", seg_data[i]);
    fprintf(stderr, "\n");
#endif
    return dg;
}


void prepend_dls_dgs(const std::string& text, uint8_t charset) {
    // process all DL segments
    int seg_count = dls_count(text);
    std::vector<DATA_GROUP*> segs;
    for (int seg_index = 0; seg_index < seg_count; seg_index++) {
#if DEBUG
        fprintf(stderr, "Segment number %d\n", seg_index + 1);
#endif
        segs.push_back(dls_get(text, charset, seg_index));
    }

    // prepend to packetizer
    pad_packetizer->queue.insert(pad_packetizer->queue.begin(), segs.begin(), segs.end());

#if DEBUG
    fprintf(stderr, "PAD length: %d\n", padlen);
    fprintf(stderr, "DLS text: %s\n", text.c_str());
    fprintf(stderr, "Number of DL segments: %d\n", seg_count);
#endif
}

int History::find(const fingerprint_t& fp) const
{
    size_t i;
    for (i = 0; i < m_database.size(); i++) {
        if (m_database[i] == fp) {
            // return the id of fingerprint found
            return m_database[i].fidx;
        }
    }

    // return -1 when the database doesn't contain this fingerprint
    return -1;
}

void History::add(fingerprint_t& fp)
{
    m_database.push_back(fp);

    if (m_database.size() > m_hist_size) {
        m_database.pop_front();
    }
}

void History::disp_database()
{
    size_t id;
    printf("HISTORY DATABASE:\n");
    if (m_database.empty()) {
        printf(" empty\n");
    }
    else {
        for (id = 0; id < m_database.size(); id++) {
            printf(" id %4zu: ", id);
            m_database[id].disp();
        }
    }
    printf("-----------------\n");
}

int History::get_fidx(const char* filepath)
{
    fingerprint_t fp;

    fp.load_from_file(filepath);

    int idx = find(fp);

    if (idx < 0) {
        idx = m_last_given_fidx++;
        fp.fidx = idx;

        if (m_last_given_fidx > MAXSLIDEID) {
            m_last_given_fidx = 0;
        }

        add(fp);
    }

    return idx;
}

