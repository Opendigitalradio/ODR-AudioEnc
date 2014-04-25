/*
    Copyright (C) 2014 CSP Innovazione nelle ICT s.c.a r.l. (http://rd.csp.it/)

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

    dls.c
          Generete PAD data for DLS

    Authors:
         Matthias P. Braendli
            http://opendigitalradio.org

         Sergio Sagliocco <sergio.sagliocco@csp.it>
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>

#include "lib_crc.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define MAXSEGLEN 8179
#define MAXDLS 129

typedef unsigned char UCHAR;
typedef unsigned short int USHORT;

void create_dls_datagroup (char* text, int padlen, unsigned char*** p_dlsdg, int* p_numdg);
void writeDLS(int output_fd, const char* dls_file, int padlen);

int get_xpadlengthmask(int padlen);
#define ALLOWED_PADLEN "23, 26, 34, 42, 58"

void usage(char* name)
{
    fprintf(stderr, "DAB DLS encoder %s\n\n"
                    "By CSP Innovazione nelle ICT s.c.a r.l. (http://rd.csp.it/)\n"
                    "and Opendigitalradio.org\n\n"
                    "Reads DLS from /tmp/dls.file\n\n"
                    "WARNING: This program has memory leaks! Do not attempt\n"
                    "to leave it running for long periods of time!\n\n"
                    "  http://opendigitalradio.org\n\n",
#if defined(GITVERSION)
                    GITVERSION
#else
                    PACKAGE_VERSION
#endif
                    );
    fprintf(stderr, "Usage: %s [OPTIONS...]\n", name);
    fprintf(stderr,
                    " -o, --output=FILENAME  Fifo to write PAD data into.\n"
                    "                        Default: /tmp/pad.fifo\n"
                    " -t, --dls=FILENAME     Fifo or file to read DLS text from.\n"
                    "                        Default: /tmp/dls.txt\n"
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
    int len, ret;
    int padlen=58;

    char* output = "/tmp/pad.fifo";
    char* dls_file = "/tmp/dls.txt";

    const struct option longopts[] = {
        {"output",     required_argument,  0, 'o'},
        {"dls",        required_argument,  0, 't'},
        {"pad",        required_argument,  0, 'p'},
        {"help",       no_argument,        0, 'h'},
        {0,0,0,0},
    };

    if (argc < 2) {
        fprintf(stderr, "Error: too few arguments!\n");
        usage(argv[0]);
        return 2;
    }

    int ch=0;
    int index;
    while(ch != -1) {
        ch = getopt_long(argc, argv, "ho:t:p:", longopts, &index);
        switch (ch) {
            case 'o':
                output = optarg;
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
        fprintf(stderr, "Error: pad length %d invalid: Possible values: "
                    ALLOWED_PADLEN "\n",
                padlen);
        return 2;
    }

    int output_fd = open(output, O_WRONLY);
    if (output_fd == -1) {
        perror("Failed to open output");
        return 3;
    }

    writeDLS(output_fd, dls_file, padlen);

    return 0;
}


void writeDLS(int output_fd, const char* dls_file, int padlen) {
    char dlstext[MAXDLS];
    static char dlstextprev[MAXDLS];
    int dlslen;
    int i;
    static unsigned char** dlsdg;
    static int numdg = 0;

    static int dlsfd = 0;

    if (dlsfd!=0) {
        close(dlsfd);
    }

    dlsfd = open(dls_file, O_RDONLY);
    if (dlsfd == -1) {
        fprintf(stderr,"Error - Cannot open dls file\n");
        return;
    }

    dlslen = read(dlsfd, dlstext, MAXDLS);
    dlstext[dlslen] = 0x00;
    //if (strcmp(dlstext,dlstextprev)!=0) {
    create_dls_datagroup(dlstext, padlen, &dlsdg, &numdg);
    strcpy(dlstextprev, dlstext);
    //}
    for (i = 0; i < numdg; i++) {
        write(output_fd, dlsdg[i], padlen);
    }

}

void create_dls_datagroup (char* text, int padlen, UCHAR*** p_dlsdg, int* p_numdg) {

    UCHAR dlsseg[8][16];      // max 8 segments, each max 16 chars
    UCHAR** dlsdg;            // Array of datagroups composing dls text;


    int numseg;               // Number of DSL segments
    int lastseglen;           // Length of the last segment
    int numdg;                // Number of data group
    int xpadlengthmask;
    int i, j, k, z, idx_start_crc, idx_stop_crc;
    USHORT dlscrc;
    static UCHAR toggle = 0;

    if (toggle == 0)
        toggle = 1;
    else
        toggle = 0;

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

    *p_numdg = numdg;
    fprintf(stderr, "PAD Length: %d\n", padlen);
    fprintf(stderr, "DLS text: %s\n", text);
    fprintf(stderr, "Number of DLS segments: %d\n", numseg);
    fprintf(stderr, "Number of DLS data groups: %d\n", numdg);

    xpadlengthmask = get_xpadlengthmask(padlen);

    *p_dlsdg = (UCHAR**) malloc(numdg * sizeof(UCHAR*));
    dlsdg = *p_dlsdg;

    i = 0;
    for (z=0; z < numseg; z++) {
        char* curseg;
        int curseglen;
        UCHAR firstseg, lastseg;

        curseg = &text[z * 16];
        fprintf(stderr, "Segment number %d\n", z+1);

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
            dlsdg[i] = (UCHAR*) malloc(padlen * sizeof(UCHAR));

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
            dlsdg[i][padlen-5]=((toggle*8+firstseg*4+lastseg*2+0)<<4) | (curseglen-1);

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
            fprintf(stderr, "crc=%x ~crc=%x\n", ~dlscrc, dlscrc);

            dlsdg[i][padlen-7-curseglen] = (dlscrc & 0xFF00) >> 8;     // HI CRC
            dlsdg[i][padlen-7-curseglen-1] = dlscrc & 0x00FF;          // LO CRC

            // NULL PADDING
            for (j = padlen-7-curseglen-2; j >= 0; j--) {
                dlsdg[i][j]=0x00;
            }

            fprintf(stderr, "Data group: ");
            for (j = 0; j < padlen; j++)
                fprintf(stderr, "%x ", dlsdg[i][j]);
            fprintf(stderr, "\n");
            i++;

        }
        else {   // Segment is composed of 2 data groups

            // FIRST DG (NO CRC)
            dlscrc = 0xffff;

            dlsdg[i] = (UCHAR*) malloc(padlen * sizeof(UCHAR));

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
            dlsdg[i][padlen-5]=((toggle*8+firstseg*4+lastseg*2+0)<<4) | (curseglen-1);


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

            fprintf(stderr, "First Data group: ");
            for (j = 0; j < padlen; j++)
                fprintf(stderr, "%x ", dlsdg[i][j]);
            fprintf(stderr,"\n");

            // SECOND DG (NO CI, NO PREFIX)
            i++;

            dlsdg[i] = (UCHAR*) malloc(padlen*sizeof(UCHAR));

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
            for (j = 0; j < padlen; j++)
                fprintf(stderr, "%x ", dlsdg[i][j]);

            fprintf(stderr, "\n");
            fprintf(stderr, "**** crc=%x ~crc=%x\n", ~dlscrc, dlscrc);
            i++;
        }
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

