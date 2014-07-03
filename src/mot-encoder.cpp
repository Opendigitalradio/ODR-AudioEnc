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

#define DEBUG 0

#define SLEEPDELAY_DEFAULT 10 //seconds

extern "C" {
#include "lib_crc.h"
}

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define XSTR(x) #x
#define STR(x) XSTR(x)

#define MAXSEGLEN 8179
#define MAXDLS 129

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

struct slide_metadata_t {
    // complete path to slide
    std::string filepath;

    // index, values from 0 to 9999, rolls over
    int fidx;

    // This is used to define the order in which several discovered
    // slides are transmitted
    bool operator<(const slide_metadata_t& other) const {
        return this->filepath < other.filepath;
    }
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
int encodeFile(int output_fd, std::string& fname, int fidx, int padlen);
void createMotHeader(size_t blobsize, int fidx, unsigned char* mothdr, int* mothdrlen);
void createMscDG(MSCDG* msc, unsigned short int dgtype, unsigned short int cindex,
        unsigned short int lastseg, unsigned short int tid, unsigned char* data,
        unsigned short int datalen);

void packMscDG(unsigned char* mscblob, MSCDG* msc, unsigned short int *bsize);
void writeMotPAD(int output_fd,
        unsigned char* mscdg,
        unsigned short int mscdgsize,
        unsigned short int padlen);

void create_dls_datagroup(char* text, int padlen);
void writeDLS(int output_fd, const char* dls_file, int padlen);

int get_xpadlengthmask(int padlen);
#define ALLOWED_PADLEN "23, 26, 34, 42, 58"

// The toggle flag for the DLS
static uint8_t dls_toggle = 0;

// The DLS data groups
std::deque<std::vector<uint8_t> > dlsdg;
static int dlsfd = 0;


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
                    "                        been encoded.\n"
                    " -s, --sleep=DELAY      Wait DELAY seconds between each slide\n"
                    "                        Default: " STR(SLEEPDELAY_DEFAULT) "\n"
                    " -o, --output=FILENAME  Fifo to write PAD data into.\n"
                    "                        Default: /tmp/pad.fifo\n"
                    " -t, --dls=FILENAME     Fifo or file to read DLS text from.\n"
                    " -p, --pad=LENGTH       Set the pad length.\n"
                    "                        Possible values: " ALLOWED_PADLEN "\n"
                    "                        Default: 58\n"
           );
}

#define no_argument 0
#define required_argument 1
#define optional_argument 2
int main(int argc, char *argv[])
{
    int len, fidx, ret;
    struct dirent *pDirent;
    DIR *pDir;
    char dlstext[MAXDLS];
    int  padlen = 58;
    bool erase_after_tx = false;
    int  sleepdelay = 10;

    const char* dir = NULL;
    const char* output = "/tmp/pad.fifo";
    const char* dls_file = NULL;

    const struct option longopts[] = {
        {"dir",        required_argument,  0, 'd'},
        {"erase",      no_argument,        0, 'e'},
        {"output",     required_argument,  0, 'o'},
        {"dls",        required_argument,  0, 't'},
        {"pad",        required_argument,  0, 'p'},
        {"sleep",      required_argument,  0, 's'},
        {"help",       no_argument,        0, 'h'},
        {0,0,0,0},
    };

    int ch=0;
    int index;
    while(ch != -1) {
        ch = getopt_long(argc, argv, "ehd:o:s:t:p:", longopts, &index);
        switch (ch) {
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

    int output_fd = open(output, O_WRONLY);
    if (output_fd == -1) {
        perror("mot-encoder failed to open output");
        return 3;
    }

    MagickWandGenesis();

    std::list<slide_metadata_t> slides_to_transmit;

    fidx = 0;
    while(1) {
        if (dir) {
            pDir = opendir(dir);
            if (pDir == NULL) {
                fprintf(stderr, "Cannot open directory '%s'\n", dir);
                return 1;
            }
            if (fidx == 9999) {
                fidx = 0;
            }

            // Add new slides to transmit to list
            while ((pDirent = readdir(pDir)) != NULL) {
                if (pDirent->d_name[0] != '.') {
                    char imagepath[256];
                    sprintf(imagepath, "%s/%s", dir, pDirent->d_name);

                    slide_metadata_t md;
                    md.filepath = imagepath;
                    md.fidx     = fidx;

                    slides_to_transmit.push_back(md);

                    fprintf(stderr, "Found slide %s\n", imagepath);

                    fidx++;
                }
            }

            // Sort the list in alphabetic order
            slides_to_transmit.sort();

            // Encode the slides
            std::list<slide_metadata_t>::iterator it;
            for (it = slides_to_transmit.begin();
                    it != slides_to_transmit.end();
                    ++it) {
                ret = encodeFile(output_fd, it->filepath, it->fidx, padlen);
                if (ret != 1) {
                    fprintf(stderr, "Error - Cannot encode file %s\n", it->filepath.c_str());
                }

                if (erase_after_tx) {
                    if (unlink(it->filepath.c_str()) == -1) {
                        fprintf(stderr, "Erasing file %s failed: ", it->filepath.c_str());
                        perror("");
                    }
                }

                sleep(sleepdelay);
            }

            slides_to_transmit.resize(0);
        }

        if (dls_file) {
            // Always retransmit DLS, we want it to be updated frequently
            writeDLS(output_fd, dls_file, padlen);
        }

        sleep(sleepdelay);

        closedir(pDir);
    }
    return 1;
}

int encodeFile(int output_fd, std::string& fname, int fidx, int padlen)
{
    int fd=0, ret, mothdrlen, nseg, lastseglen, i, last, curseglen;
    unsigned char mothdr[32];
    MagickWand *m_wand = NULL;
    PixelWand  *p_wand = NULL;
    size_t blobsize, height, width;
    unsigned char *blob = NULL, *curseg = NULL;
    MagickBooleanType err;
    MSCDG msc;
    unsigned char mscblob[8200];
    unsigned short int mscblobsize;
    //float aspectRatio;

    m_wand = NewMagickWand();
    p_wand = NewPixelWand();
    PixelSetColor(p_wand, "black");

    err = MagickReadImage(m_wand, fname.c_str());
    if (err == MagickFalse) {
        fprintf(stderr, "Error - Unable to load image %s\n", fname.c_str());
        ret = 0;
        goto RETURN;
    }

    height = MagickGetImageHeight(m_wand);
    width  = MagickGetImageWidth(m_wand);
    //aspectRatio = (width * 1.0)/height;

    fprintf(stderr, "Image: %s (id=%d). Original size: %zu x %zu. ",
            fname.c_str(), fidx, width, height);

    while (height > 240 || width > 320) {
        if (height/240.0 > width/320.0) {
            //width = height * aspectRatio;
            width = width * 240.0 / height;
            height = 240;
        }
        else {
            //height = width * (1.0/aspectRatio);
            height = height * 320.0 / width;
            width = 320;
        }
        MagickResizeImage(m_wand, width, height, LanczosFilter, 1);
    }

    height = MagickGetImageHeight(m_wand);
    width  = MagickGetImageWidth(m_wand);

    MagickBorderImage(m_wand, p_wand, (320-width)/2, (240-height)/2);

    MagickSetImageCompressionQuality(m_wand, 75);
    MagickSetImageFormat(m_wand, "jpg");
    blob = MagickGetImagesBlob(m_wand, &blobsize);
    fprintf(stderr, "Resized to %zu x %zu. Size after compression %zu bytes\n",
            width, height, blobsize);

    nseg = blobsize / MAXSEGLEN;
    lastseglen = blobsize % MAXSEGLEN;
    if (lastseglen != 0) {
        nseg++;
    }

    createMotHeader(blobsize, fidx, mothdr, &mothdrlen);
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

RETURN:
    if (m_wand) {
        m_wand = DestroyMagickWand(m_wand);
    }
    if (blob) {
        free(blob);
    }
    return ret;
}


void createMotHeader(size_t blobsize, int fidx, unsigned char* mothdr, int* mothdrlen)
{
    int ret;
    struct stat s;
    char MotHeaderCore[7] = {0x00,0x00,0x00,0x00,0x0D,0x04,0x01};
    char MotHeaderExt[19] = {0x85,0x00,0x00,0x00,0x00,0xcc,0x0c,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    char cntemp[12];
    int i;

    MotHeaderCore[0] = (blobsize<<4 & 0xFF000000) >> 24;
    MotHeaderCore[1] = (blobsize<<4 & 0x00FF0000) >> 16;
    MotHeaderCore[2] = (blobsize<<4 & 0x0000FF00) >> 8;
    MotHeaderCore[3] = (blobsize<<4 & 0x000000FF);

    sprintf(cntemp, "img%04d.jpg", fidx);
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


void writeDLS(int output_fd, const char* dls_file, int padlen)
{
    char dlstext[MAXDLS];
    int dlslen;
    int i;

    if (dlsfd != 0) {
        close(dlsfd);
    }

    dlsfd = open(dls_file, O_RDONLY);
    if (dlsfd == -1) {
        fprintf(stderr,"Error - Cannot open dls file\n");
        return;
    }

    dlslen = read(dlsfd, dlstext, MAXDLS);
    dlstext[dlslen] = 0x00;
    fprintf(stderr, "Writing DLS text \"%s\"\n", dlstext);

    create_dls_datagroup(dlstext, padlen);
    for (i = 0; i < dlsdg.size(); i++) {
        write(output_fd, &dlsdg[i].front(), dlsdg[i].size());
    }

}

void create_dls_datagroup(char* text, int padlen)
{
    int numdg = 0;            // Number of data groups
    int numseg;               // Number of DSL segments
    int lastseglen;           // Length of the last segment
    int xpadlengthmask;
    int i, j, k, z, idx_start_crc, idx_stop_crc;
    uint16_t dlscrc;

    if (dls_toggle == 0)
        dls_toggle = 1;
    else
        dls_toggle = 0;

    numseg = strlen(text) / 16;
    lastseglen = strlen(text) % 16;
    if (padlen-9 >= 16) {
        if (lastseglen > 0) {
            numseg++;   // The last incomplete segment
        }

        // The PAD can contain the full segmnet and overhead (9 bytes)
        numdg = numseg;

    }
    else {
        // Each 16 char segment span over 2 dg
        numdg = numseg * 2;

        if (lastseglen > 0) {
            numseg++;              // The last incomplete segment

            if (lastseglen <= padlen-9) {
                numdg += 1;
            }
            else {
                numdg += 2;
            }
        }
    }

#if DEBUG
    fprintf(stderr, "PAD Length: %d\n", padlen);
    fprintf(stderr, "DLS text: %s\n", text);
    fprintf(stderr, "Number of DLS segments: %d\n", numseg);
    fprintf(stderr, "Number of DLS data groups: %d\n", numdg);
#endif

    xpadlengthmask = get_xpadlengthmask(padlen);

    dlsdg.resize(0);
    dlsdg.resize(numdg);

    i = 0;
    for (z=0; z < numseg; z++) {
        char* curseg;
        int curseglen;
        uint8_t firstseg, lastseg;

        curseg = &text[z * 16];
#if DEBUG
        fprintf(stderr, "Segment number %d\n", z+1);
#endif

        if (z == 0) {             // First segment
            firstseg = 1;
        }
        else {
            firstseg = 0;
        }

        if (z == numseg-1) {      //Last segment
            if (lastseglen != 0) {
                curseglen = lastseglen;
            }
            else {
                curseglen = 16;
            }
            lastseg = 1;
        }
        else {
            curseglen = 16;
            lastseg = 0;
        }

        if (curseglen <= padlen-9) {  // Segment is composed of 1 data group
            dlsdg[i].resize(padlen);

            // FF-PAD Byte L (CI=1)
            dlsdg[i][padlen-1]=0x02;

            // FF-PAD Byte L-1 (Variable size X_PAD)
            dlsdg[i][padlen-2]=0x20;

            // CI => data length = 12 (011) - Application Type=2
            // (DLS - start of X-PAD data group)
            dlsdg[i][padlen-3]=(xpadlengthmask<<5) | 0x02;

            // End of CI list
            dlsdg[i][padlen-4]=0x00;

            // DLS Prefix (T=1,Only one segment,segment length-1)
            dlsdg[i][padlen-5]=((dls_toggle*8+firstseg*4+lastseg*2+0)<<4) | 
                (curseglen-1);

            if (firstseg==1) {
                // DLS Prefix (Charset standard)
                dlsdg[i][padlen-6]=0x00;
            }
            else {
                // DLS SegNum
                dlsdg[i][padlen-6]=z<<4;
            }

            // CRC start from prefix
            idx_start_crc = padlen-5;

            // DLS text
            for (j = 0; j < curseglen; j++) {
                dlsdg[i][padlen-7-j] = curseg[j];
            }

            idx_stop_crc = padlen - 7 - curseglen+1;

            dlscrc = 0xffff;
            for (j = idx_start_crc; j >= idx_stop_crc; j--) {
                dlscrc = update_crc_ccitt(dlscrc, dlsdg[i][j]);
            }

            dlscrc = ~dlscrc;
#if DEBUG
            fprintf(stderr, "crc=%x ~crc=%x\n", ~dlscrc, dlscrc);
#endif

            dlsdg[i][padlen-7-curseglen]   = (dlscrc & 0xFF00) >> 8;  // HI CRC
            dlsdg[i][padlen-7-curseglen-1] = dlscrc & 0x00FF;         // LO CRC

            // NULL PADDING
            for (j = padlen-7-curseglen-2; j >= 0; j--) {
                dlsdg[i][j]=0x00;
            }

#if DEBUG
            fprintf(stderr, "Data group: ");
            for (j = 0; j < padlen; j++)
                fprintf(stderr, "%x ", dlsdg[i][j]);
            fprintf(stderr, "\n");
#endif
            i++;

        }
        else {   // Segment is composed of 2 data groups

            // FIRST DG (NO CRC)
            dlscrc = 0xffff;

            dlsdg[i].resize(padlen);

            // FF-PAD Byte L (CI=1)
            dlsdg[i][padlen-1]=0x02;

            // FF-PAD Byte L-1 (Variable size X_PAD)
            dlsdg[i][padlen-2]=0x20;

            // CI => data length = 12 (011) - Application Type=2
            // (DLS - start of X-PAD data group)
            dlsdg[i][padlen-3]=(xpadlengthmask<<5) | 0x02;

            // End of CI list
            dlsdg[i][padlen-4]=0x00;

            // DLS Prefix (T=1,Only one segment,segment length-1)
            dlsdg[i][padlen-5]=((dls_toggle*8+firstseg*4+lastseg*2+0)<<4) |
                (curseglen-1);


            if (firstseg == 1) {
                // DLS Prefix (Charset standard)
                dlsdg[i][padlen-6] = 0x00;
            }
            else {
                // DLS SegNum
                dlsdg[i][padlen-6]=(i-1)<<4;
            }

            dlscrc = update_crc_ccitt(dlscrc, dlsdg[i][padlen-5]);
            dlscrc = update_crc_ccitt(dlscrc, dlsdg[i][padlen-6]);

            // DLS text
            for (j=0; j < MIN(curseglen, padlen-7); j++) {
                dlsdg[i][padlen-7-j] = curseg[j];
                dlscrc = update_crc_ccitt(dlscrc, dlsdg[i][padlen-7-j]);
            }
            k = j;

            // end of segment
            if (curseglen == padlen-8) {
                dlscrc = ~dlscrc;
                dlsdg[i][1] = (dlscrc & 0xFF00) >> 8;     // HI CRC
                fprintf(stderr, "crc=%x ~crc=%x\n", ~dlscrc, dlscrc);
            }
            else if (curseglen == padlen-7) {
                dlscrc = ~dlscrc;
                fprintf(stderr, "crc=%x ~crc=%x\n", ~dlscrc, dlscrc);
            }
            dlsdg[i][0]=0x00;

#if DEBUG
            fprintf(stderr, "First Data group: ");
            for (j = 0; j < padlen; j++) {
                fprintf(stderr, "%x ", dlsdg[i][j]);
            }
            fprintf(stderr,"\n");
#endif

            // SECOND DG (NO CI, NO PREFIX)
            i++;

            dlsdg[i].resize(padlen);

            // FF-PAD Byte L (CI=0)
            dlsdg[i][padlen-1] = 0x00;

            // FF-PAD Byte L-1 (Variable size X_PAD)
            dlsdg[i][padlen-2] = 0x20;

            if (curseglen == padlen-8) {
                dlsdg[i][padlen-3] = dlscrc & 0x00FF;          // LO CRC
            }
            else if (curseglen==padlen-7) {
                dlsdg[i][padlen-3] = (dlscrc & 0xFF00) >> 8;    // HI CRC
                dlsdg[i][padlen-4] =  dlscrc & 0x00FF;          // LO CRC
            }
            else {
                // DLS text
                for (j = 0; j < curseglen-k; j++) {
                    dlsdg[i][padlen-3-j] = curseg[k+j];
                    dlscrc = update_crc_ccitt(dlscrc, dlsdg[i][padlen-3-j]);
                }
                dlscrc = ~dlscrc;
                dlsdg[i][padlen-3-curseglen+k] =  (dlscrc & 0xFF00) >> 8;    // HI CRC
                dlsdg[i][padlen-3-curseglen+k-1] = dlscrc & 0x00FF;          // LO CRC
            }

            fprintf(stderr, "Second Data group: ");
            for (j = 0; j < padlen; j++) {
                fprintf(stderr, "%x ", dlsdg[i][j]);
            }

            fprintf(stderr, "\n");
            fprintf(stderr, "**** crc=%x ~crc=%x\n", ~dlscrc, dlscrc);
            i++;
        }
    }
}

void writeMotPAD(int output_fd,
        unsigned char* mscdg,
        unsigned short int mscdgsize,
        unsigned short int padlen)
{

    unsigned char pad[128];
    int xpadlengthmask, i, j,k, numseg, lastseglen;
    unsigned short int crc;

    xpadlengthmask = get_xpadlengthmask(padlen);

    // Write MSC Data Groups
    int curseglen, non_ci_seglen;
    for (i = 0; i < mscdgsize; i += curseglen) {
        uint8_t* curseg;
        uint8_t  firstseg;

        curseg = &mscdg[i];
        //fprintf(stderr,"Segment offset %d\n",i);

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

        write(output_fd, pad, padlen);
        //fprintf(stderr,"MSC Data Group - Segment %d: ",i);
        //for (j=0;j<padlen;j++) fprintf(stderr,"%02x ",pad[j]);
        //        fprintf(stderr,"\n");

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
    else
        xpadlengthmask = -1; // Error

    return xpadlengthmask;
}

