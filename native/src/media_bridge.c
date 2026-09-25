#include "media_bridge.h"

#include <chafa.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

static _Thread_local char hv_error[512];

static int hv_set_error(const char *message)
{
    snprintf(hv_error, sizeof(hv_error), "%s", message);
    return -1;
}

static int hv_set_av_error(const char *operation, int error)
{
    char detail[256];
    av_strerror(error, detail, sizeof(detail));
    snprintf(hv_error, sizeof(hv_error), "%s: %s", operation, detail);
    return error;
}

const char *hv_last_error(void)
{
    return hv_error;
}

struct hv_decoder {
    AVFormatContext *format;
    AVCodecContext *codec;
    AVPacket *packet;
    AVFrame *frame;
    struct SwsContext *sws;
    int stream_index;
    int max_width;
    int target_fps;
    double next_output_pts;
    int output_width;
    int output_height;
    uint8_t *rgb;
    int rgb_stride;
    int rgb_size;
    int flushed;
};

static int hv_decoder_prepare_scaler(hv_decoder *decoder, const AVFrame *frame)
{
    int width = frame->width;
    int height = frame->height;
    int output_width = width;
    int output_height = height;

    if (decoder->max_width > 0 && output_width > decoder->max_width) {
        output_width = decoder->max_width;
        output_height = (int)((double)height * output_width / width + 0.5);
    }
    if (output_height & 1) {
        output_height--;
    }
    if (output_height < 2) {
        output_height = 2;
    }

    if (decoder->sws && decoder->output_width == output_width &&
        decoder->output_height == output_height) {
        return 0;
    }

    sws_freeContext(decoder->sws);
    decoder->sws = sws_getContext(width, height, frame->format,
                                  output_width, output_height, AV_PIX_FMT_RGB24,
                                  SWS_BILINEAR, NULL, NULL, NULL);
    if (!decoder->sws) {
        return hv_set_error("could not create FFmpeg scaler");
    }

    av_freep(&decoder->rgb);
    decoder->rgb_size = av_image_get_buffer_size(AV_PIX_FMT_RGB24,
                                                  output_width, output_height, 1);
    if (decoder->rgb_size < 0) {
        return hv_set_av_error("could not calculate RGB frame size", decoder->rgb_size);
    }
    decoder->rgb = av_malloc((size_t)decoder->rgb_size);
    if (!decoder->rgb) {
        return hv_set_error("could not allocate RGB frame buffer");
    }
    decoder->rgb_stride = output_width * 3;
    decoder->output_width = output_width;
    decoder->output_height = output_height;
    return 0;
}

int hv_decoder_open(const char *path, int target_fps, int max_width, hv_decoder **out)
{
    if (!path || !out) {
        return hv_set_error("invalid decoder arguments");
    }
    *out = NULL;

    hv_decoder *decoder = av_mallocz(sizeof(*decoder));
    if (!decoder) {
        return hv_set_error("could not allocate decoder");
    }
    decoder->max_width = max_width;
    decoder->target_fps = target_fps;

    int ret = avformat_open_input(&decoder->format, path, NULL, NULL);
    if (ret < 0) {
        hv_set_av_error("could not open input", ret);
        hv_decoder_close(decoder);
        return ret;
    }
    ret = avformat_find_stream_info(decoder->format, NULL);
    if (ret < 0) {
        hv_set_av_error("could not read stream info", ret);
        hv_decoder_close(decoder);
        return ret;
    }

    const AVCodec *codec = NULL;
    ret = av_find_best_stream(decoder->format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (ret < 0 || !codec) {
        hv_set_av_error("could not find video stream", ret < 0 ? ret : AVERROR(EINVAL));
        hv_decoder_close(decoder);
        return ret < 0 ? ret : AVERROR(EINVAL);
    }
    decoder->stream_index = ret;
    decoder->codec = avcodec_alloc_context3(codec);
    if (!decoder->codec) {
        hv_set_error("could not allocate codec context");
        hv_decoder_close(decoder);
        return AVERROR(ENOMEM);
    }
    ret = avcodec_parameters_to_context(decoder->codec,
                                        decoder->format->streams[ret]->codecpar);
    if (ret < 0) {
        hv_set_av_error("could not copy codec parameters", ret);
        hv_decoder_close(decoder);
        return ret;
    }
    ret = avcodec_open2(decoder->codec, codec, NULL);
    if (ret < 0) {
        hv_set_av_error("could not open video codec", ret);
        hv_decoder_close(decoder);
        return ret;
    }
    decoder->packet = av_packet_alloc();
    decoder->frame = av_frame_alloc();
    if (!decoder->packet || !decoder->frame) {
        hv_set_error("could not allocate FFmpeg frame state");
        hv_decoder_close(decoder);
        return AVERROR(ENOMEM);
    }
    *out = decoder;
    return 0;
}

double hv_decoder_duration(const hv_decoder *decoder)
{
    if (!decoder || !decoder->format || decoder->format->duration == AV_NOPTS_VALUE) {
        return 0.0;
    }
    return (double)decoder->format->duration / AV_TIME_BASE;
}

int hv_decoder_next(hv_decoder *decoder, hv_video_frame *out)
{
    if (!decoder || !out) {
        return hv_set_error("invalid decoder frame arguments");
    }
    memset(out, 0, sizeof(*out));

    for (;;) {
        int ret = avcodec_receive_frame(decoder->codec, decoder->frame);
        if (ret == 0) {
            if (decoder->target_fps > 0 &&
                decoder->frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                AVRational time_base = decoder->format->streams[decoder->stream_index]->time_base;
                double pts = decoder->frame->best_effort_timestamp * av_q2d(time_base);
                if (isfinite(decoder->next_output_pts) && pts + 1e-9 < decoder->next_output_pts) {
                    av_frame_unref(decoder->frame);
                    continue;
                }
                decoder->next_output_pts = pts + 1.0 / decoder->target_fps;
            }
            ret = hv_decoder_prepare_scaler(decoder, decoder->frame);
            if (ret < 0) {
                return ret;
            }
            uint8_t *dst[] = {decoder->rgb, NULL, NULL, NULL};
            int dst_linesize[] = {decoder->rgb_stride, 0, 0, 0};
            sws_scale(decoder->sws,
                      (const uint8_t *const *)decoder->frame->data,
                      decoder->frame->linesize, 0, decoder->frame->height,
                      dst, dst_linesize);
            out->pixels = decoder->rgb;
            out->size = (size_t)decoder->rgb_size;
            out->width = decoder->output_width;
            out->height = decoder->output_height;
            out->stride = decoder->rgb_stride;
            return 1;
        }
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            return hv_set_av_error("could not decode video frame", ret);
        }
        if (ret == AVERROR_EOF) {
            return 0;
        }

        if (decoder->flushed) {
            return 0;
        }
        ret = av_read_frame(decoder->format, decoder->packet);
        if (ret < 0) {
            decoder->flushed = 1;
            ret = avcodec_send_packet(decoder->codec, NULL);
            if (ret < 0 && ret != AVERROR_EOF) {
                return hv_set_av_error("could not flush decoder", ret);
            }
            continue;
        }
        if (decoder->packet->stream_index == decoder->stream_index) {
            ret = avcodec_send_packet(decoder->codec, decoder->packet);
        }
        av_packet_unref(decoder->packet);
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            return hv_set_av_error("could not submit packet", ret);
        }
    }
}

int hv_decoder_seek(hv_decoder *decoder, double seconds)
{
    if (!decoder || !decoder->format) {
        return hv_set_error("invalid decoder seek arguments");
    }
    AVRational time_base = decoder->format->streams[decoder->stream_index]->time_base;
    int64_t timestamp = (int64_t)(seconds / av_q2d(time_base));
    int ret = avformat_seek_file(decoder->format, decoder->stream_index,
                                 INT64_MIN, timestamp, INT64_MAX,
                                 AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        return hv_set_av_error("could not seek video", ret);
    }
    avcodec_flush_buffers(decoder->codec);
    decoder->flushed = 0;
    decoder->next_output_pts = seconds;
    return 0;
}

void hv_decoder_close(hv_decoder *decoder)
{
    if (!decoder) {
        return;
    }
    av_frame_free(&decoder->frame);
    av_packet_free(&decoder->packet);
    sws_freeContext(decoder->sws);
    av_freep(&decoder->rgb);
    avcodec_free_context(&decoder->codec);
    avformat_close_input(&decoder->format);
    av_free(decoder);
}

struct hv_renderer {
    ChafaCanvasConfig *config;
    ChafaCanvas *canvas;
    ChafaTermInfo *term_info;
};

static ChafaCanvasMode hv_color_mode(const char *colors)
{
    if (colors && strcmp(colors, "none") == 0) {
        return CHAFA_CANVAS_MODE_FGBG;
    }
    if (colors && strcmp(colors, "16") == 0) {
        return CHAFA_CANVAS_MODE_INDEXED_16;
    }
    if (colors && strcmp(colors, "256") == 0) {
        return CHAFA_CANVAS_MODE_INDEXED_256;
    }
    return CHAFA_CANVAS_MODE_TRUECOLOR;
}

static ChafaDitherMode hv_dither_mode(const char *dither)
{
    if (dither && strcmp(dither, "none") == 0) {
        return CHAFA_DITHER_MODE_NONE;
    }
    if (dither && strcmp(dither, "diffusion") == 0) {
        return CHAFA_DITHER_MODE_DIFFUSION;
    }
    return CHAFA_DITHER_MODE_ORDERED;
}

int hv_renderer_open(int width, int height, const char *symbols, const char *colors,
                     const char *dither, hv_renderer **out)
{
    if (!out || width <= 0 || height <= 0) {
        return hv_set_error("invalid renderer arguments");
    }
    *out = NULL;
    hv_renderer *renderer = g_new0(hv_renderer, 1);
    renderer->config = chafa_canvas_config_new();
    renderer->term_info = chafa_term_info_new();
    if (!renderer->config || !renderer->term_info) {
        hv_renderer_close(renderer);
        return hv_set_error("could not allocate Chafa renderer");
    }
    chafa_canvas_config_set_geometry(renderer->config, width, height);
    chafa_canvas_config_set_canvas_mode(renderer->config, hv_color_mode(colors));
    chafa_canvas_config_set_dither_mode(renderer->config, hv_dither_mode(dither));

    ChafaSymbolMap *map = chafa_symbol_map_new();
    if (!map) {
        hv_renderer_close(renderer);
        return hv_set_error("could not allocate Chafa symbol map");
    }
    if (symbols && !chafa_symbol_map_apply_selectors(map, symbols, NULL)) {
        chafa_symbol_map_unref(map);
        hv_renderer_close(renderer);
        return hv_set_error("invalid Chafa symbol selector");
    }
    chafa_canvas_config_set_symbol_map(renderer->config, map);
    chafa_symbol_map_unref(map);
    renderer->canvas = chafa_canvas_new(renderer->config);
    if (!renderer->canvas) {
        hv_renderer_close(renderer);
        return hv_set_error("could not allocate Chafa canvas");
    }
    *out = renderer;
    return 0;
}

int hv_renderer_render(hv_renderer *renderer, const uint8_t *pixels, int width,
                       int height, int stride, char **out, size_t *out_size)
{
    if (!renderer || !pixels || !out || !out_size) {
        return hv_set_error("invalid renderer frame arguments");
    }
    chafa_canvas_draw_all_pixels(renderer->canvas, CHAFA_PIXEL_RGB8,
                                 pixels, width, height, stride);
    GString *result = chafa_canvas_print(renderer->canvas, renderer->term_info);
    if (!result) {
        return hv_set_error("Chafa failed to render frame");
    }
    *out = g_string_free(result, FALSE);
    *out_size = strlen(*out);
    return 0;
}

void hv_renderer_free_output(char *output)
{
    g_free(output);
}

void hv_renderer_close(hv_renderer *renderer)
{
    if (!renderer) {
        return;
    }
    if (renderer->canvas) {
        chafa_canvas_unref(renderer->canvas);
    }
    if (renderer->term_info) {
        chafa_term_info_unref(renderer->term_info);
    }
    if (renderer->config) {
        chafa_canvas_config_unref(renderer->config);
    }
    g_free(renderer);
}
