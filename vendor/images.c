/* Builds the vendored image decoder (stb_image) in its own translation
 * unit, behind the small interface of src/decode.h. */
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_STDIO /* files are read by the caller */
#include "stb/stb_image.h"

#include "../src/decode.h"

unsigned char *kk_decode_image(const unsigned char *data, size_t len, int *w, int *h,
                               int *frames, int **delays)
{
    int comp;
    *frames = 1;
    *delays = NULL;
    if (len > 0x7fffffff)
        return NULL;
    if (len >= 6 && memcmp(data, "GIF", 3) == 0) {
        int z = 0;
        unsigned char *p =
            stbi_load_gif_from_memory(data, (int)len, delays, w, h, &z, &comp, 4);
        *frames = p ? z : 0;
        return p;
    }
    return stbi_load_from_memory(data, (int)len, w, h, &comp, 4);
}

void kk_decode_image_free(void *p)
{
    stbi_image_free(p);
}
