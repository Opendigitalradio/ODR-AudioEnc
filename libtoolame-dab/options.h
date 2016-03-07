#ifndef OPTIONS_H
#define OPTIONS_H

#define INPUT_SELECT_JACK 1
#define INPUT_SELECT_WAV 2
#define INPUT_SELECT_VLC 3

typedef struct
{
  int usepsy;			/* TRUE   by default, use the psy model */
  int usepadbit;		/* TRUE   by default, use a padding bit */
  int quickmode;		/* FALSE  calculate psy model for every frame */
  int quickcount;		/* 10     when quickmode = TRUE, calculate psymodel every 10th frame */
  int downmix;			/* FALSE  downmix from stereo to mono */
  int byteswap;			/* FALSE  swap the bytes */
  int dab;			/* FALSE  DAB extensions */
  int vbr;			/* FALSE  switch for VBR mode */
  float vbrlevel;			/* 0      level of VBR . higher is better */
  float athlevel;                 /* 0      extra argument to the ATH equation - 
				          used for VBR in LAME */
  int verbosity;                /* 2 by default. 0 is no output at all */
  int show_level; /* 1=show the sox-like audio level measurement */
}
options;

options glopts;
#endif

