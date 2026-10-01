/* Builds the vendored audio decoders in one translation unit, behind the
 * small interface of src/decode.h. */
#include <stdlib.h>

#define DR_WAV_IMPLEMENTATION
#include "dr_libs/dr_wav.h"

#define DR_MP3_IMPLEMENTATION
#include "dr_libs/dr_mp3.h"

#define STB_VORBIS_NO_PUSHDATA_API
#include "stb/stb_vorbis.c"

#include "../src/decode.h"

int16_t *kk_decode_file(const char *path, kk_decode_format fmt, int *channels,
                        int *rate, uint64_t *frames)
{
    *channels = *rate = 0;
    *frames = 0;
    switch (fmt) {
    case KK_DECODE_WAV: {
        unsigned int ch, r;
        drwav_uint64 n;
        drwav_int16 *p = drwav_open_file_and_read_pcm_frames_s16(path, &ch, &r, &n, NULL);
        *channels = (int)ch;
        *rate = (int)r;
        *frames = p ? n : 0;
        return p;
    }
    case KK_DECODE_MP3: {
        drmp3_config cfg;
        drmp3_uint64 n;
        drmp3_int16 *p = drmp3_open_file_and_read_pcm_frames_s16(path, &cfg, &n, NULL);
        *channels = p ? (int)cfg.channels : 0;
        *rate = p ? (int)cfg.sampleRate : 0;
        *frames = p ? n : 0;
        return p;
    }
    case KK_DECODE_OGG: {
        short *out = NULL;
        int n = stb_vorbis_decode_filename(path, channels, rate, &out);
        if (n <= 0) {
            free(out);
            return NULL;
        }
        *frames = (uint64_t)n;
        return out;
    }
    }
    return NULL;
}

void kk_decode_free(int16_t *pcm, kk_decode_format fmt)
{
    if (fmt == KK_DECODE_WAV)
        drwav_free(pcm, NULL);
    else if (fmt == KK_DECODE_MP3)
        drmp3_free(pcm, NULL);
    else
        free(pcm);
}
