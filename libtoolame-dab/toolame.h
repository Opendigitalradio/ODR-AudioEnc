#ifndef __TOOLAME_H_
#define __TOOLAME_H_

/* All exported functions shown here return zero
 * on success */

/* Initialise toolame encoding library. */
int toolame_init(void);

int toolame_enable_downmix_stereo(void);
int toolame_enable_byteswap(void);

/* Set channel mode. Allowed values:
 * s, d, j , and m
 */
int toolame_set_channel_mode(const char mode);

/* Valid PSY models: 0 to 3 */
int toolame_set_psy_model(int new_model);

int toolame_set_bitrate(int brate);

/* Enable PAD insertion from the specified file with length */
int toolame_set_pad(int pad_len);

int toolame_encode_frame(
        short buffer[2][1152],
        unsigned char* xpad_data,
        unsigned char *output_buffer);

#endif // __TOOLAME_H_

