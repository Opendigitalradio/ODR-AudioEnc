/*
    Copyright (C) 2014 CSP Innovazione nelle ICT s.c.a r.l. (http://rd.csp.it/)

    Copyright (C) 2014 Matthias P. Braendli (http://opendigitalradio.org)

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
*/

#include <cstdio>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <vector>
#include <deque>
#include <list>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <wand/magick_wand.h>
#include <getopt.h>
#include "config.h"

#define DEBUG 0

#define SLEEPDELAY_DEFAULT 10 //seconds

extern "C" {
#include "lib_crc.h"
}

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define XSTR(x) #x
#define STR(x) XSTR(x)

#define MAXSEGLEN 8179 // Bytes
#define MAXDLS 128 // chars
#define MAXSLIDESIZE 50000 // Bytes

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
#define CHARSET_UTF8 15 // ISO Latin Alphabet No 2

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


/*
   typedef struct {
// MOT HEADER CUSTOMIZED FOR SLIDESHOW APP
unsigned int bodysize;      // 28 bits
unsigned short int headsize;    // 13 bits
unsigned char ctype;        //  6 bits
unsigned char sctype;       //  9 bits
unsigned char triggertime[5];   // 0x85 0x00 0x00 0x00 0x00 => NOW
unsigned char contname[14];     // 0xCC 0x0C 0x00 imgXXXX.jpg
} MOTSLIDEHDR;
*/
int encodeFile(int output_fd, std::string& fname, int fidx, int padlen, bool raw_slides);

void createMotHeader(
        size_t blobsize,
        int fidx,
        unsigned char* mothdr,
        int* mothdrlen,
        bool jfif_not_png);

void createMscDG(MSCDG* msc, unsigned short int dgtype, unsigned short int cindex,
        unsigned short int lastseg, unsigned short int tid, unsigned char* data,
        unsigned short int datalen);

void packMscDG(unsigned char* mscblob, MSCDG* msc, unsigned short int *bsize);
void writeMotPAD(int output_fd,
        unsigned char* mscdg,
        unsigned short int mscdgsize,
        unsigned short int padlen);

void create_dls_pads(const char* text, const int padlen, const uint8_t charset);
void writeDLS(int output_fd, const char* dls_file, int padlen, uint8_t charset);


int get_xpadlengthmask(int padlen);
size_t get_xpadlength(int mask);
#define SHORT_PAD 6
#define ALLOWED_PADLEN "6 (short X-PAD; only DLS), 23, 26, 34, 42, 58"


// DLS related
#define DLS_SEG_LEN_PREFIX       2
#define DLS_SEG_LEN_CHAR_MAX    16
#define DLS_SEG_LEN_CRC          2
#define DLS_SEG_LEN_MAX         (DLS_SEG_LEN_PREFIX + DLS_SEG_LEN_CHAR_MAX + DLS_SEG_LEN_CRC)

typedef std::vector<uint8_t> pad_t;
static std::deque<pad_t> dls_pads;
static bool dls_toggle = false;
static char dlstext_prev[MAXDLS + 1];


static int verbose = 0;

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
                    " -c, --charset=ID       Signal the character set encoding defined by ID\n"
                    "                          ID = 0: Complete EBU Latin based repertoire\n"
                    "                          ID = 1: Latin based common core, Cyrillic, Greek\n"
                    "                          ID = 2: EBU Latin based core, Arabic, Hebrew, Cyrillic and Greek\n"
                    "                          ID = 3: ISO Latin Alphabet No 2\n"
                    "                          ID = 15: ISO/IEC 10646 using UTF-8\n"
                    "                          Default: 0\n"
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
    int  charset = CHARSET_COMPLETE_EBU_LATIN;

    const char* dir = NULL;
    const char* output = "/tmp/pad.fifo";
    const char* dls_file = NULL;

    const struct option longopts[] = {
        {"charset",    required_argument,  0, 'c'},
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
        ch = getopt_long(argc, argv, "ehRc:d:o:p:s:t:v", longopts, &index);
        switch (ch) {
            case 'c':
                charset = atoi(optarg);
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

    if (get_xpadlengthmask(padlen) == -1) {
        fprintf(stderr, "mot-encoder Error: pad length %d invalid: Possible values: "
                ALLOWED_PADLEN "\n",
                padlen);
        return 2;
    }
    if (dir && padlen == SHORT_PAD) {
        fprintf(stderr, "mot-encoder Error: Slideshow can't be used together with short X-PAD!\n");
        return 1;
    }

    if (dir && dls_file) {
        fprintf(stderr, "mot-encoder encoding Slideshow from %s and DLS from %s to %s\n",
                dir, dls_file, output);
    }
    else if (dir) {
        fprintf(stderr, "mot-encoder encoding Slideshow from %s to %s. No DLS.\n",
                dir, output);
    }
    else if (dls_file) {
        fprintf(stderr, "mot-encoder encoding DLS from %s to %s. No Slideshow.\n",
                dls_file, output);
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

    int output_fd = open(output, O_WRONLY);
    if (output_fd == -1) {
        perror("mot-encoder Error: failed to open output");
        return 3;
    }

    MagickWandGenesis();

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
                        fprintf(stderr, "mot-encoder found slide %s, fidx %d\n", imagepath, md.fidx);
                    }
                }
            }

#if DEBUG
            slides_history.disp_database();
#endif

            // Sort the list in fidx order
            slides_to_transmit.sort();

            if (dls_file) {
                // Maybe we have no slides, always update DLS
                writeDLS(output_fd, dls_file, padlen, charset);
                sleep(sleepdelay);
            }

            // Encode the slides
            std::list<slide_metadata_t>::iterator it;
            for (it = slides_to_transmit.begin();
                    it != slides_to_transmit.end();
                    ++it) {

                ret = encodeFile(output_fd, it->filepath, it->fidx, padlen, raw_slides);
                if (ret != 1) {
                    fprintf(stderr, "mot-encoder Error: cannot encode file %s\n", it->filepath.c_str());
                }

                if (erase_after_tx) {
                    if (unlink(it->filepath.c_str()) == -1) {
                        fprintf(stderr, "mot-encoder Error: erasing file %s failed: ", it->filepath.c_str());
                        perror("");
                    }
                }

                // Always retransmit DLS after each slide, we want it to be updated frequently
                if (dls_file) {
                    writeDLS(output_fd, dls_file, padlen, charset);
                }

                sleep(sleepdelay);
            }

            if (slides_to_transmit.empty()) {
                sleep(sleepdelay);
            }

            slides_to_transmit.resize(0);
        }
        else if (dls_file) { // only DLS
            // Always retransmit DLS, we want it to be updated frequently
            writeDLS(output_fd, dls_file, padlen, charset);

            sleep(sleepdelay);
        }

        if (pDir) {
            closedir(pDir);
        }
    }
    return 1;
}

// Resize the image or add a black border around it
// so that it is 320x240 pixels.
// Automatically reduce the quality to make sure the
// blobsize is not too large.
//
// Returns: the blobsize
size_t resizeImage(MagickWand* m_wand, unsigned char** blob)
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

    // Make sure smaller images are 320x240 pixels, and
    // add a black border
    p_wand = NewPixelWand();
    PixelSetColor(p_wand, "black");
    MagickBorderImage(m_wand, p_wand, (320-width)/2, (240-height)/2);
    DestroyPixelWand(p_wand);

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
    return blobsize;
}

int encodeFile(int output_fd, std::string& fname, int fidx, int padlen, bool raw_slides)
{
    int ret = 0;
    int fd=0, mothdrlen, nseg, lastseglen, i, last, curseglen;
    unsigned char mothdr[32];
    MagickWand *m_wand = NULL;
    size_t blobsize, height, width;
    unsigned char *blob = NULL;
    unsigned char *curseg = NULL;
    MagickBooleanType err;
    MSCDG msc;
    unsigned char mscblob[8200];
    unsigned short int mscblobsize;

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
     */
    bool resize_required = true;

    bool jfif_not_png = true;

    if (!raw_slides) {

        m_wand = NewMagickWand();

        err = MagickReadImage(m_wand, fname.c_str());
        if (err == MagickFalse) {
            fprintf(stderr, "mot-encoder Error: Unable to load image %s\n",
                    fname.c_str());

            goto encodefile_out;
        }

        height       = MagickGetImageHeight(m_wand);
        width        = MagickGetImageWidth(m_wand);
        orig_format  = MagickGetImageFormat(m_wand);

        // By default assume that the image has full quality and can be reduced
        orig_quality = 100;

        if (orig_format) {
            if (strcmp(orig_format, "JPEG") == 0) {
                orig_quality = MagickGetImageCompressionQuality(m_wand);
                orig_is_jpeg = true;

                if (verbose) {
                    fprintf(stderr, "mot-encoder image: %s (id=%d)."
                            " Original size: %zu x %zu. (%s, q=%zu)\n",
                            fname.c_str(), fidx, width, height, orig_format, orig_quality);
                }
            }
            else if (strcmp(orig_format, "PNG") == 0) {
                orig_is_png = true;
                jfif_not_png = false;

                if (verbose) {
                    fprintf(stderr, "mot-encoder image: %s (id=%d)."
                            " Original size: %zu x %zu. (%s)\n",
                            fname.c_str(), fidx, width, height, orig_format);
                }
            }
            else if (verbose) {
                fprintf(stderr, "mot-encoder image: %s (id=%d)."
                        " Original size: %zu x %zu. (%s)\n",
                        fname.c_str(), fidx, width, height, orig_format);
            }

            free(orig_format);
        }
        else {
            fprintf(stderr, "mot-encoder Warning: Unable to detect image format %s\n",
                    fname.c_str());

            fprintf(stderr, "mot-encoder image: %s (id=%d).  Original size: %zu x %zu.\n",
                    fname.c_str(), fidx, width, height);
        }

        if ((orig_is_jpeg || orig_is_png) && height == 240 && width == 320) {
            // Don't recompress the image and check if the blobsize is suitable
            blob = MagickGetImagesBlob(m_wand, &blobsize);

            if (blobsize < MAXSLIDESIZE) {
                if (verbose) {
                    fprintf(stderr, "mot-encoder image: %s (id=%d).  No resize needed: %zu Bytes\n",
                            fname.c_str(), fidx, blobsize);
                }
                resize_required = false;
            }
        }

        if (resize_required) {
            blobsize = resizeImage(m_wand, &blob);

            // resizeImage always creates a jpg output
            jfif_not_png = true;
        }

    }
    else { // Use RAW data, it might not even be a jpg !
        // read file
        FILE* pFile = fopen(fname.c_str(), "rb");
        if (pFile == NULL) {
            fprintf(stderr, "mot-encoder Error: Unable to load file %s\n",
                    fname.c_str());
            goto encodefile_out;
        }

        // obtain file size:
        fseek(pFile, 0, SEEK_END);
        blobsize = ftell(pFile);
        rewind(pFile);

        if (blobsize > MAXSLIDESIZE) {
            fprintf(stderr, "mot-encoder Warning: blob in raw-slide %s too large\n",
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

        createMotHeader(blobsize, fidx, mothdr, &mothdrlen, jfif_not_png);
        // Create the MSC Data Group C-Structure
        createMscDG(&msc, 3, 0, 1, fidx, mothdr, mothdrlen);
        // Generate the MSC DG frame (Figure 9 en 300 401)
        packMscDG(mscblob, &msc, &mscblobsize);
        writeMotPAD(output_fd, mscblob, mscblobsize, padlen);

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

            createMscDG(&msc, 4, i, last, fidx, curseg, curseglen);
            packMscDG(mscblob, &msc, &mscblobsize);
            writeMotPAD(output_fd, mscblob, mscblobsize, padlen);
        }

        ret = 1;
    }

encodefile_out:
    if (m_wand) {
        m_wand = DestroyMagickWand(m_wand);
    }

    if (blob) {
        free(blob);
    }
    return ret;
}


void createMotHeader(size_t blobsize, int fidx, unsigned char* mothdr, int* mothdrlen, bool jfif_not_png)
{
    struct stat s;
    uint8_t MotHeaderCore[7] = {0x00,0x00,0x00,0x00,0x0D,0x04,0x00};
    uint8_t MotHeaderExt[19] = {0x85,0x00,0x00,0x00,0x00,0xcc,0x0c,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    char cntemp[12];
    int i;

    // Set correct content subtype
    //  ETSI TS 101 756 V1.2.1 Clause 6.1 Table 17
    if (jfif_not_png) {
        MotHeaderCore[6] = 0x01;
    }
    else {
        MotHeaderCore[6] = 0x03;
    }

    MotHeaderCore[0] = (blobsize<<4 & 0xFF000000) >> 24;
    MotHeaderCore[1] = (blobsize<<4 & 0x00FF0000) >> 16;
    MotHeaderCore[2] = (blobsize<<4 & 0x0000FF00) >> 8;
    MotHeaderCore[3] = (blobsize<<4 & 0x000000FF);

    sprintf(cntemp,
            "img%04d.%s",
            fidx,
            jfif_not_png ? "jpg" : "png" );

    for (i = 0; i < strlen(cntemp); i++) {
        MotHeaderExt[8+i] = cntemp[i];
    }
    *mothdrlen = 26;
    for (i = 0; i < 7; i++)
        mothdr[i] = MotHeaderCore[i];
    for (i = 0; i < 19; i++)
        mothdr[7+i] = MotHeaderExt[i];

    return;
}

void createMscDG(MSCDG* msc, unsigned short int dgtype,
        unsigned short int cindex, unsigned short int lastseg,
        unsigned short int tid, unsigned char* data,
        unsigned short int datalen)
{
    msc->extflag = 0;
    msc->crcflag = 1;
    msc->segflag = 1;
    msc->accflag = 1;
    msc->dgtype = dgtype;
    msc->cindex = cindex;
    msc->rindex = 0;
    msc->last = lastseg;
    msc->segnum = cindex;
    msc->rfa = 0;
    msc->tidflag = 1;
    msc->lenid = 2;
    msc->tid = tid;
    msc->segdata = data;
    msc->rcount = 0;
    msc->seglen = datalen;
}


void packMscDG(unsigned char* b, MSCDG* msc, unsigned short int* bsize)
{
    int i;
    unsigned short int crc=0xFFFF;

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

    for (i = 0; i<9; i++) {
        crc = update_crc_ccitt(crc, b[i]);
    }

    for(i = 0; i < msc->seglen; i++) {
        b[i+9] = (msc->segdata)[i];
        crc = update_crc_ccitt(crc, b[i+9]);
    }

    crc = ~crc;
    b[9+msc->seglen]   = (crc & 0xFF00) >> 8;     // HI CRC
    b[9+msc->seglen+1] =  crc & 0x00FF;          // LO CRC

    *bsize = 9 + msc->seglen + 1 + 1;

    //write(1,b,9+msc->seglen+1+1);
}


void writeDLS(int output_fd, const char* dls_file, int padlen, uint8_t charset)
{
    char dlstext[MAXDLS + 1];
    int dlslen;
    int i;
    bool dlstext_new;

    int dlsfd = open(dls_file, O_RDONLY);
    if (dlsfd == -1) {
        fprintf(stderr, "mot-encoder Error: Cannot open dls file\n");
        return;
    }

    dlslen = read(dlsfd, dlstext, MAXDLS);

    close(dlsfd);

    dlstext[dlslen] = 0x00;

    // Remove trailing line breaks from the file
    char* endp = dlstext + dlslen;
    while (   endp > dlstext &&
            (*endp == '\0' || *endp == '\n')) {
        if (*endp == '\n') {
            *endp = '\0';
        }
        endp--;
    }

    // (Re)Create data groups (and thereby toggle the toggle bit) only on (first call or) new text
    dlstext_new = dls_pads.empty() || strcmp(dlstext, dlstext_prev);
    if (verbose) {
        fprintf(stderr, "mot-encoder writing %s DLS text \"%s\"\n", dlstext_new ? "new" : "old", dlstext);
    }
    if (dlstext_new) {
        create_dls_pads(dlstext, padlen, charset);
        strcpy(dlstext_prev, dlstext);
    }

    for (i = 0; i < dls_pads.size(); i++) {
        size_t dummy = write(output_fd, &dls_pads[i].front(), dls_pads[i].size());
    }
}


int dls_count(const char* text) {
    size_t text_len = strlen(text);
    return text_len / DLS_SEG_LEN_CHAR_MAX + (text_len % DLS_SEG_LEN_CHAR_MAX ? 1 : 0);
}


size_t dls_get(const char* text, const uint8_t charset, const unsigned int seg_index, uint8_t *seg_data) {
    int seg_count = dls_count(text);
    if (seg_index >= seg_count)
        return 0;

    bool first_seg = seg_index == 0;
    bool last_seg  = seg_index == seg_count - 1;

    const char *seg_text_start = text + seg_index * DLS_SEG_LEN_CHAR_MAX;
    size_t seg_text_len = strnlen(seg_text_start, DLS_SEG_LEN_CHAR_MAX);
    size_t seg_len = DLS_SEG_LEN_PREFIX + seg_text_len + DLS_SEG_LEN_CRC;


    // prefix: toggle? + first seg? + last seg? + (seg len - 1)
    seg_data[0] =
            (dls_toggle ? (1 << 7) : 0) +
            (first_seg  ? (1 << 6) : 0) +
            (last_seg   ? (1 << 5) : 0) +
            (seg_text_len - 1);

    // prefix: charset / seg index
    seg_data[1] = (first_seg ? charset : seg_index) << 4;

    // character field
    memcpy(seg_data + DLS_SEG_LEN_PREFIX, seg_text_start, seg_text_len);

    // CRC
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < seg_len - DLS_SEG_LEN_CRC; i++)
        crc = update_crc_ccitt(crc, seg_data[i]);
    crc = ~crc;
#if DEBUG
    fprintf(stderr, "crc=%04x ~crc=%04x\n", ~crc, crc);
#endif
    seg_data[seg_len - 2] = (crc & 0xFF00) >> 8;    // HI CRC
    seg_data[seg_len - 1] =  crc & 0x00FF;          // LO CRC

#if DEBUG
    fprintf(stderr, "DL segment:");
    for (int i = 0; i < seg_len; i++)
        fprintf(stderr, " %02x", seg_data[i]);
    fprintf(stderr, "\n");
#endif
    return seg_len;
}


void create_dls_pads(const char* text, const int padlen, const uint8_t charset) {
    uint8_t seg_data[DLS_SEG_LEN_MAX];
    int xpadlengthmask = get_xpadlengthmask(padlen);

    dls_pads.clear();
    dls_toggle = !dls_toggle;   // indicate changed text

    // process all DL segments
    int seg_count = dls_count(text);
    for (int seg_index = 0; seg_index < seg_count; seg_index++) {
#if DEBUG
        fprintf(stderr, "Segment number %d\n", seg_index + 1);
#endif
        int seg_len = dls_get(text, charset, seg_index, seg_data);

        // distribute the segment over one or more PADs
        for (int seg_off = 0; seg_off < seg_len;) {
            dls_pads.push_back(pad_t(padlen));
            pad_t &pad = dls_pads.back();
            int pad_off = padlen - 1;

            bool var_size_pad = padlen != SHORT_PAD;
            bool ci_needed = seg_off == 0;  // CI needed only at first data group

            int dg_len = ci_needed ? get_xpadlength(xpadlengthmask) : padlen - 2;
            int dg_used = MIN(dg_len, seg_len - seg_off);


            // F-PAD Byte L   (CI if needed)
            pad[pad_off--] = ci_needed ? 0x02 : 0x00;

            // F-PAD Byte L-1 (variable size / short X-PAD)
            pad[pad_off--] = var_size_pad ? 0x20 : 0x10;

            // CI (app type 2 = DLS, start of X-PAD data group)
            if (ci_needed)
                pad[pad_off--] = ((var_size_pad ? xpadlengthmask : 0) << 5) | 0x02;

            // CI end marker
            if (ci_needed && var_size_pad)
                pad[pad_off--] = 0x00;

            // segment (part)
            for (int i = 0; i < dg_used; i++)
                pad[pad_off--] = seg_data[seg_off + i];

            // NULL PADDING
            std::fill_n(pad.begin(), pad_off + 1, 0x00);

#if DEBUG
            fprintf(stderr, "DLS data group:");
            for (int i = 0; i < padlen; i++)
                fprintf(stderr, " %02x", pad[i]);
            fprintf(stderr, "\n");
#endif
            seg_off += dg_used;
        }
    }
#if DEBUG
    fprintf(stderr, "PAD length: %d\n", padlen);
    fprintf(stderr, "DLS text: %s\n", text);
    fprintf(stderr, "Number of DL segments: %d\n", seg_count);
    fprintf(stderr, "Number of DLS data groups: %zu\n", dls_pads.size());
#endif
}

void writeMotPAD(int output_fd,
        unsigned char* mscdg,
        unsigned short int mscdgsize,
        unsigned short int padlen)
{

    unsigned char pad[128];
    int xpadlengthmask, i, j, k;
    unsigned short int crc;

    xpadlengthmask = get_xpadlengthmask(padlen);

    // Write MSC Data Groups
    int curseglen, non_ci_seglen;
    for (i = 0; i < mscdgsize; i += curseglen) {
        uint8_t* curseg;
        uint8_t  firstseg;

        curseg = &mscdg[i];
#if DEBUG
        fprintf(stderr,"Segment offset %d\n",i);
#endif

        if (i == 0) {             // First segment
            firstseg = 1;
            curseglen = padlen-10;

            // size of first X-PAD = MSC-DG + DGLI-DG + End of CI list + 2x CI = size of subsequent non-CI X-PADs
            non_ci_seglen = curseglen + 4 + 1 + 2;
        }
        else {
            firstseg = 0;
            curseglen = MIN(non_ci_seglen,mscdgsize-i);
        }

        if (firstseg == 1) {
            // FF-PAD Byte L (CI=1)
            pad[padlen-1] = 0x02;

            // FF-PAD Byte L-1 (Variable size X_PAD)
            pad[padlen-2] = 0x20;

            // Write Data Group Length Indicator
            crc = 0xffff;
            // CI for data group length indicator: data length=4, Application Type=1
            pad[padlen-3]=0x01;
            // CI for data group length indicator: Application Type=12 (Start of MOT)
            pad[padlen-4]=(xpadlengthmask<<5) | 12;
            // End of CI list
            pad[padlen-5]=0x00;
            // RFA+HI Data group length
            pad[padlen-6]=(mscdgsize & 0x3F00)>>8;
            pad[padlen-7]=(mscdgsize & 0x00FF);
            crc = update_crc_ccitt(crc, pad[padlen-6]);
            crc = update_crc_ccitt(crc, pad[padlen-7]);
            crc = ~crc;
            // HI CRC
            pad[padlen-8]=(crc & 0xFF00) >> 8;
            // LO CRC
            pad[padlen-9]=(crc & 0x00FF);
            k=10;
        }
        else {
            // FF-PAD Byte L (CI=0)
            pad[padlen-1] = 0x00;

            // FF-PAD Byte L-1 (Variable size X_PAD)
            pad[padlen-2] = 0x20;
            k=3;
        }

        for (j = 0; j < curseglen; j++) {
            pad[padlen-k-j] = curseg[j];
        }
        for (j = padlen-k-curseglen; j >= 0; j--) {
            pad[j] = 0x00;
        }

        size_t dummy = write(output_fd, pad, padlen);
#if DEBUG
        fprintf(stderr,"MSC Data Group - Segment %d: ",i);
        for (j=0;j<padlen;j++)
            fprintf(stderr,"%02x ",pad[j]);
        fprintf(stderr,"\n");
#endif
    }
}

int get_xpadlengthmask(int padlen)
{
    int xpadlengthmask;

    /* Don't forget to change ALLOWED_PADLEN
     * if you change this check
     */
    if (padlen == 23)
        xpadlengthmask = 3;
    else if (padlen == 26)
        xpadlengthmask = 4;
    else if (padlen == 34)
        xpadlengthmask = 5;
    else if (padlen == 42)
        xpadlengthmask = 6;
    else if (padlen == 58)
        xpadlengthmask = 7;
    else if (padlen == SHORT_PAD)
        xpadlengthmask = 8; // internal value; used later at get_xpadlength
    else
        xpadlengthmask = -1; // Error

    return xpadlengthmask;
}

size_t get_xpadlength(int mask) {
    size_t length[] = {4, 6, 8, 12, 16, 24, 32, 48, 3}; // last value used for short X-PAD
    return length[mask];
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

