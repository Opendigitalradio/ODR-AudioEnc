#include "vlc_input.h"

#if defined(VLC_INPUT)
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

int check_vlc_uses_size_t();
struct vlc_buffer* vlc_buffer_new();
void vlc_buffer_free(struct vlc_buffer* node);

libvlc_instance_t     *m_vlc;
libvlc_media_player_t *m_mp;

unsigned int vlc_rate;
unsigned int vlc_channels;

struct vlc_buffer *head_buffer;

// now playing information can get written to
// a file. This writing happens in a separate thread
#define NOWPLAYING_LEN 512
char vlc_nowplaying[NOWPLAYING_LEN];
int vlc_nowplaying_running;
pthread_t vlc_nowplaying_thread;
const char* vlc_nowplaying_filename;

struct icywriter_task_data {
    char        text[NOWPLAYING_LEN];
    int         success;
    sem_t       sem;
};

struct icywriter_task_data icy_task_data;


pthread_mutex_t buffer_lock = PTHREAD_MUTEX_INITIALIZER;

struct vlc_buffer* vlc_buffer_new()
{
    struct vlc_buffer* node;
    node = malloc(sizeof(struct vlc_buffer));
    memset(node, 0, sizeof(struct vlc_buffer));
    return node;
}

void vlc_buffer_free(struct vlc_buffer* node)
{
    if (node->buf) {
        free(node->buf);
    }

    free(node);
}

size_t vlc_buffer_totalsize(struct vlc_buffer* node)
{
    size_t totalsize = 0;
    for (; node != NULL; node = node->next) {
        totalsize += node->size;
    }

    return totalsize;
}

// VLC Audio prerender callback, we must allocate a buffer here
void prepareRender_size_t(
        void* p_audio_data,
        uint8_t** pp_pcm_buffer,
        size_t size)
{
    *pp_pcm_buffer = malloc(size);
}

void prepareRender(
        void* p_audio_data,
        uint8_t** pp_pcm_buffer,
        unsigned int size)
{
    *pp_pcm_buffer = malloc(size);
}


// Audio postrender callback
void handleStream_size_t(
        void* p_audio_data,
        uint8_t* p_pcm_buffer,
        unsigned int channels,
        unsigned int rate,
        unsigned int nb_samples,
        unsigned int bits_per_sample,
        size_t size,
        int64_t pts)
{
    assert(channels == vlc_channels);
    assert(rate == vlc_rate);
    assert(bits_per_sample == 16);

    // 16 is a bit arbitrary, if it's too small we might enter
    // a deadlock if toolame asks for too much data
    const size_t max_length = 16 * size;

    for (;;) {
        pthread_mutex_lock(&buffer_lock);

        if (vlc_buffer_totalsize(head_buffer) < max_length) {
            struct vlc_buffer* newbuf = vlc_buffer_new();

            newbuf->buf = p_pcm_buffer;
            newbuf->size = size;

            // Append the new buffer to the end of the linked list
            struct vlc_buffer* tail = head_buffer;
            while (tail->next) {
                tail = tail->next;
            }
            tail->next = newbuf;

            pthread_mutex_unlock(&buffer_lock);
            return;
        }

        pthread_mutex_unlock(&buffer_lock);
        usleep(100);
    }
}

// convert from unsigned int size to size_t size
void handleStream(
        void* p_audio_data,
        uint8_t* p_pcm_buffer,
        unsigned int channels,
        unsigned int rate,
        unsigned int nb_samples,
        unsigned int bits_per_sample,
        unsigned int size,
        int64_t pts)
{
    handleStream_size_t(
        p_audio_data,
        p_pcm_buffer,
        channels,
        rate,
        nb_samples,
        bits_per_sample,
        size,
        pts);
}

int vlc_in_prepare(
        unsigned verbosity,
        unsigned int rate,
        const char* uri,
        unsigned channels,
        const char* icy_write_file
        )
{
    fprintf(stderr, "Initialising VLC...\n");

    vlc_nowplaying_running = 0;
    vlc_nowplaying_filename = icy_write_file;

    long long int handleStream_address;
    long long int prepareRender_address;

    int vlc_version_check = check_vlc_uses_size_t();
    if (vlc_version_check == 0) {
        fprintf(stderr, "You are using VLC with unsigned int size callbacks\n");

        handleStream_address = (long long int)(intptr_t)(void*)&handleStream;
        prepareRender_address = (long long int)(intptr_t)(void*)&prepareRender;
    }
    else if (vlc_version_check == 1) {
        fprintf(stderr, "You are using VLC with size_t size callbacks\n");

        handleStream_address = (long long int)(intptr_t)(void*)&handleStream_size_t;
        prepareRender_address = (long long int)(intptr_t)(void*)&prepareRender_size_t;
    }
    else {
        fprintf(stderr, "Error detecting VLC version!\n");
        fprintf(stderr, "      you are using %s\n", libvlc_get_version());
        return -1;
    }

    vlc_rate = rate;
    vlc_channels = channels;

    // VLC options
    char smem_options[512];
    snprintf(smem_options, sizeof(smem_options),
            "#transcode{acodec=s16l,samplerate=%d}:"
            // We are using transcode because smem only support raw audio and
            // video formats
            "smem{"
                "audio-postrender-callback=%lld,"
                "audio-prerender-callback=%lld"
            "}",
            vlc_rate,
            handleStream_address,
            prepareRender_address);

    char verb_options[512];
    snprintf(verb_options, sizeof(verb_options),
            "--verbose=%d", verbosity);

    const char * const vlc_args[] = {
        verb_options,
        "--sout", smem_options // Stream to memory
    };

    // Launch VLC
    m_vlc = libvlc_new(sizeof(vlc_args) / sizeof(vlc_args[0]), vlc_args);

    // Load the media
    libvlc_media_t *m;
    m = libvlc_media_new_location(m_vlc, uri);
    m_mp = libvlc_media_player_new_from_media(m);
    libvlc_media_release(m);

    // Allocate the list
    head_buffer = vlc_buffer_new();

    // Start playing
    int ret = libvlc_media_player_play(m_mp);

    if (ret == 0) {
        libvlc_media_t *media = libvlc_media_player_get_media(m_mp);
        libvlc_state_t st;

        ret = -1;

        int timeout;
        for (timeout = 0; timeout < 100; timeout++) {
            st = libvlc_media_get_state(media);
            usleep(10*1000);
            if (st != libvlc_NothingSpecial) {
                ret = 0;
                break;
            }
        }
    }

    return ret;
}

ssize_t vlc_in_read(void *buf, size_t len)
{
    if (len == 0) {
        return 0;
    }

    assert(buf);

    size_t requested = len;
    for (;;) {
        pthread_mutex_lock(&buffer_lock);

        if (vlc_buffer_totalsize(head_buffer) >= len) {
            while (len >= head_buffer->size) {
                if (head_buffer->buf && head_buffer->size) {
                    // Get all the data from this list element
                    memcpy(buf, head_buffer->buf, head_buffer->size);

                    buf += head_buffer->size;
                    len -= head_buffer->size;
                }

                if (head_buffer->next) {
                    struct vlc_buffer *next_head = head_buffer->next;
                    vlc_buffer_free(head_buffer);
                    head_buffer = next_head;
                }
                else {
                    vlc_buffer_free(head_buffer);
                    head_buffer = vlc_buffer_new();
                    break;
                }
            }

            if (len > 0) {
                assert(len < head_buffer->size);
                assert(head_buffer->buf);

                memcpy(buf, head_buffer->buf, len);

                // split the current head into two parts
                size_t remaining = head_buffer->size - len;
                uint8_t *newbuf = malloc(remaining);

                memcpy(newbuf, head_buffer->buf + len, remaining);
                free(head_buffer->buf);
                head_buffer->buf = newbuf;
                head_buffer->size = remaining;
            }

            pthread_mutex_unlock(&buffer_lock);
            return requested;
        }

        pthread_mutex_unlock(&buffer_lock);
        usleep(100);

        libvlc_media_t *media = libvlc_media_player_get_media(m_mp);
        libvlc_state_t st = libvlc_media_get_state(media);
        if (!(st == libvlc_Opening   ||
              st == libvlc_Buffering ||
              st == libvlc_Playing) ) {
            return -1;
        }

        char* nowplaying_sz = libvlc_media_get_meta(media, libvlc_meta_NowPlaying);
        if (nowplaying_sz) {
            snprintf(vlc_nowplaying, NOWPLAYING_LEN, "%s", nowplaying_sz);
            free(nowplaying_sz);
        }
    }

    abort();
}

// This task is run in a separate thread
void* vlc_in_write_icy_task(void* arg)
{
    struct icywriter_task_data* data = arg;

    FILE* fd = fopen(vlc_nowplaying_filename, "wb");
    if (fd) {
        int ret = fputs(data->text, fd);
        fclose(fd);

        if (ret >= 0) {
            data->success = 1;
        }
    }
    else {
        data->success = 0;
    }

    sem_post(&data->sem);
    return NULL;
}

void vlc_in_write_icy(void)
{
    if (vlc_nowplaying_filename == NULL) {
        return;
    }
    else if (vlc_nowplaying_running == 0) {
        memcpy(icy_task_data.text, vlc_nowplaying, NOWPLAYING_LEN);
        icy_task_data.success = 0;

        int ret = sem_init(&icy_task_data.sem, 0, 0);
        if (ret == 0) {
            ret = pthread_create(&vlc_nowplaying_thread, NULL, vlc_in_write_icy_task, &icy_task_data);

            if (ret == 0) {
                vlc_nowplaying_running = 1;
            }
            else {
                fprintf(stderr, "ICY Text writer: thread start failed: %s\n", strerror(ret));
            }
        }
        else {
            fprintf(stderr, "ICY Text writer: semaphore init failed: %s\n", strerror(errno));
        }

    }
    else {
        int ret = sem_trywait(&icy_task_data.sem);
        if (ret == -1 && errno == EAGAIN) {
            return;
        }
        else if (ret == 0) {
            ret = pthread_join(vlc_nowplaying_thread, NULL);
            if (ret != 0) {
                fprintf(stderr, "ICY Text writer: pthread_join error: %s\n", strerror(ret));
            }

            vlc_nowplaying_running = 0;
        }
        else {
            fprintf(stderr, "ICY Text writer: semaphore trywait failed: %s\n", strerror(errno));
        }
    }
}


/* VLC up to version 2.1.0 used a different callback function signature.
 * VLC 2.2.0 uses size_t
 *
 * \return 1 if the callback with size_t size should be used.
 *         0 if the callback with unsigned int size should be used.
 *        -1 if there was an error.
 */
int check_vlc_uses_size_t()
{
    int retval = -1;

    char libvlc_version[256];
    strncpy(libvlc_version, libvlc_get_version(), 255);

    char *space_position = strstr(libvlc_version, " ");

    if (space_position) {
        *space_position = '\0';
    }

    char *saveptr;
    char *major_ver_sz = strtok_r(libvlc_version, ".", &saveptr);
    if (major_ver_sz) {
        int major_ver = atoi(major_ver_sz);

        char *minor_ver_sz = strtok_r(NULL, ".", &saveptr);
        if (minor_ver_sz) {
            int minor_ver = atoi(minor_ver_sz);

            retval = (major_ver >= 2 && minor_ver >= 2) ? 1 : 0;
        }
    }

    return retval;
}

#endif // defined(VLC_INPUT)

