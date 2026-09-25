#ifndef HITVID_MEDIA_BRIDGE_H
#define HITVID_MEDIA_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hv_decoder hv_decoder;
typedef struct hv_renderer hv_renderer;

typedef struct {
    const uint8_t *pixels;
    size_t size;
    int width;
    int height;
    int stride;
} hv_video_frame;

int hv_decoder_open(const char *path, int target_fps, int max_width, hv_decoder **out);
double hv_decoder_duration(const hv_decoder *decoder);
int hv_decoder_next(hv_decoder *decoder, hv_video_frame *out);
int hv_decoder_seek(hv_decoder *decoder, double seconds);
void hv_decoder_close(hv_decoder *decoder);
const char *hv_last_error(void);

int hv_renderer_open(int width, int height, const char *symbols, const char *colors,
                     const char *dither, hv_renderer **out);
int hv_renderer_render(hv_renderer *renderer, const uint8_t *pixels, int width,
                       int height, int stride, char **out, size_t *out_size);
void hv_renderer_free_output(char *output);
void hv_renderer_close(hv_renderer *renderer);

#ifdef __cplusplus
}
#endif

#endif
