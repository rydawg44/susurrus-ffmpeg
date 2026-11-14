#include "fdg_decoder.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *key;
    char *value;
} fdg_tag;

typedef struct {
    double start;
    double end;
    char *title;
} fdg_chapter;

struct fdg_decoder {
    AVFormatContext *format;
    AVCodecContext *codec;
    SwrContext *resampler;
    AVPacket *packet;
    AVFrame *frame;
    int stream_index;
    int output_rate;
    int output_channels;
    float *pending;
    int pending_capacity;
    int pending_frames;
    int pending_offset;
    int draining;
    int finished;
    fdg_tag *tags;
    int tag_count;
    fdg_chapter *chapters;
    int chapter_count;
    uint8_t *cover;
    size_t cover_length;
};

static void fdg_set_error(char *error, size_t length, const char *what, int code) {
    if (error == NULL || length == 0) { return; }
    char reason[128] = "unknown error";
    if (code < 0) { av_strerror(code, reason, sizeof(reason)); }
    snprintf(error, length, "%s: %s", what, reason);
}

static void fdg_collect_tags(fdg_decoder *d, const AVDictionary *dictionary) {
    const AVDictionaryEntry *entry = NULL;
    while ((entry = av_dict_iterate(dictionary, entry)) != NULL) {
        fdg_tag *grown = realloc(d->tags, sizeof(fdg_tag) * (size_t)(d->tag_count + 1));
        if (grown == NULL) { return; }
        d->tags = grown;
        d->tags[d->tag_count].key = strdup(entry->key);
        d->tags[d->tag_count].value = strdup(entry->value);
        d->tag_count += 1;
    }
}

static void fdg_collect_chapters(fdg_decoder *d) {
    d->chapter_count = (int)d->format->nb_chapters;
    if (d->chapter_count == 0) { return; }
    d->chapters = calloc((size_t)d->chapter_count, sizeof(fdg_chapter));
    if (d->chapters == NULL) { d->chapter_count = 0; return; }
    for (int i = 0; i < d->chapter_count; i++) {
        const AVChapter *source = d->format->chapters[i];
        d->chapters[i].start = (double)source->start * av_q2d(source->time_base);
        d->chapters[i].end = (double)source->end * av_q2d(source->time_base);
        const AVDictionaryEntry *title = av_dict_get(source->metadata, "title", NULL, 0);
        d->chapters[i].title = title != NULL ? strdup(title->value) : NULL;
    }
}

static void fdg_collect_cover(fdg_decoder *d) {
    for (unsigned i = 0; i < d->format->nb_streams; i++) {
        const AVStream *stream = d->format->streams[i];
        if ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0) { continue; }
        if (stream->attached_pic.size <= 0) { continue; }
        d->cover = malloc((size_t)stream->attached_pic.size);
        if (d->cover == NULL) { return; }
        memcpy(d->cover, stream->attached_pic.data, (size_t)stream->attached_pic.size);
        d->cover_length = (size_t)stream->attached_pic.size;
        return;
    }
}

fdg_decoder *fdg_open(const char *path, int sample_rate, int channels, char *error, size_t error_length) {
    av_log_set_level(AV_LOG_QUIET);
    fdg_decoder *d = calloc(1, sizeof(fdg_decoder));
    if (d == NULL) { return NULL; }
    d->output_rate = sample_rate;
    d->output_channels = channels;

    int status = avformat_open_input(&d->format, path, NULL, NULL);
    if (status < 0) {
        fdg_set_error(error, error_length, "open", status);
        free(d);
        return NULL;
    }
    status = avformat_find_stream_info(d->format, NULL);
    if (status < 0) {
        fdg_set_error(error, error_length, "stream info", status);
        goto failed;
    }

    const AVCodec *codec = NULL;
    d->stream_index = av_find_best_stream(d->format, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (d->stream_index < 0 || codec == NULL) {
        fdg_set_error(error, error_length, "no decodable audio stream", d->stream_index);
        goto failed;
    }

    d->codec = avcodec_alloc_context3(codec);
    if (d->codec == NULL) { goto failed; }
    status = avcodec_parameters_to_context(d->codec, d->format->streams[d->stream_index]->codecpar);
    if (status >= 0) { status = avcodec_open2(d->codec, codec, NULL); }
    if (status < 0) {
        fdg_set_error(error, error_length, "codec", status);
        goto failed;
    }

    AVChannelLayout output_layout;
    av_channel_layout_default(&output_layout, channels);
    status = swr_alloc_set_opts2(&d->resampler,
                                 &output_layout, AV_SAMPLE_FMT_FLT, sample_rate,
                                 &d->codec->ch_layout, d->codec->sample_fmt, d->codec->sample_rate,
                                 0, NULL);
    av_channel_layout_uninit(&output_layout);
    if (status >= 0) { status = swr_init(d->resampler); }
    if (status < 0) {
        fdg_set_error(error, error_length, "resampler", status);
        goto failed;
    }

    d->packet = av_packet_alloc();
    d->frame = av_frame_alloc();
    if (d->packet == NULL || d->frame == NULL) { goto failed; }

    fdg_collect_tags(d, d->format->metadata);
    fdg_collect_tags(d, d->format->streams[d->stream_index]->metadata);
    fdg_collect_chapters(d);
    fdg_collect_cover(d);
    return d;

failed:
    fdg_close(d);
    return NULL;
}

void fdg_close(fdg_decoder *d) {
    if (d == NULL) { return; }
    swr_free(&d->resampler);
    av_frame_free(&d->frame);
    av_packet_free(&d->packet);
    avcodec_free_context(&d->codec);
    avformat_close_input(&d->format);
    for (int i = 0; i < d->tag_count; i++) {
        free(d->tags[i].key);
        free(d->tags[i].value);
    }
    free(d->tags);
    for (int i = 0; i < d->chapter_count; i++) { free(d->chapters[i].title); }
    free(d->chapters);
    free(d->cover);
    free(d->pending);
    free(d);
}

double fdg_duration_seconds(const fdg_decoder *d) {
    if (d->format->duration != AV_NOPTS_VALUE) { return (double)d->format->duration / AV_TIME_BASE; }
    const AVStream *stream = d->format->streams[d->stream_index];
    if (stream->duration != AV_NOPTS_VALUE) { return (double)stream->duration * av_q2d(stream->time_base); }
    return 0;
}

int fdg_source_sample_rate(const fdg_decoder *d) { return d->codec->sample_rate; }
int fdg_source_channels(const fdg_decoder *d) { return d->codec->ch_layout.nb_channels; }

// Converts one decoded frame into `pending`. Returns 1 on success.
static int fdg_convert_frame(fdg_decoder *d, const AVFrame *frame) {
    int capacity = swr_get_out_samples(d->resampler, frame != NULL ? frame->nb_samples : 0);
    if (capacity <= 0) { return 0; }
    if (capacity > d->pending_capacity) {
        float *grown = realloc(d->pending, sizeof(float) * (size_t)capacity * (size_t)d->output_channels);
        if (grown == NULL) { return -1; }
        d->pending = grown;
        d->pending_capacity = capacity;
    }
    uint8_t *output[1] = { (uint8_t *)d->pending };
    const uint8_t **input = frame != NULL ? (const uint8_t **)frame->extended_data : NULL;
    int converted = swr_convert(d->resampler, output, capacity, input, frame != NULL ? frame->nb_samples : 0);
    if (converted < 0) { return -1; }
    d->pending_frames = converted;
    d->pending_offset = 0;
    return converted > 0 ? 1 : 0;
}

// Pulls until `pending` holds audio. Returns 1 for audio, 0 for end of stream, <0 for failure.
static int fdg_fill_pending(fdg_decoder *d) {
    while (!d->finished) {
        int status = avcodec_receive_frame(d->codec, d->frame);
        if (status == 0) {
            int converted = fdg_convert_frame(d, d->frame);
            av_frame_unref(d->frame);
            if (converted != 0) { return converted; }
            continue;
        }
        if (status == AVERROR_EOF) {
            d->finished = 1;
            return fdg_convert_frame(d, NULL);
        }
        if (status != AVERROR(EAGAIN)) { return status; }
        if (d->draining) { continue; }

        status = av_read_frame(d->format, d->packet);
        if (status == AVERROR_EOF) {
            d->draining = 1;
            avcodec_send_packet(d->codec, NULL);
            continue;
        }
        if (status < 0) { return status; }
        if (d->packet->stream_index == d->stream_index) {
            // A corrupt packet should not end a thirty hour book, so it is skipped.
            (void)avcodec_send_packet(d->codec, d->packet);
        }
        av_packet_unref(d->packet);
    }
    return 0;
}

int fdg_read(fdg_decoder *d, float *output, int max_frames) {
    int written = 0;
    while (written < max_frames) {
        if (d->pending_offset >= d->pending_frames) {
            int status = fdg_fill_pending(d);
            if (status < 0) { return written > 0 ? written : status; }
            if (status == 0) { break; }
        }
        int available = d->pending_frames - d->pending_offset;
        int count = available < max_frames - written ? available : max_frames - written;
        memcpy(output + (size_t)written * (size_t)d->output_channels,
               d->pending + (size_t)d->pending_offset * (size_t)d->output_channels,
               sizeof(float) * (size_t)count * (size_t)d->output_channels);
        d->pending_offset += count;
        written += count;
    }
    return written;
}

int fdg_tag_count(const fdg_decoder *d) { return d->tag_count; }
const char *fdg_tag_key(const fdg_decoder *d, int i) { return d->tags[i].key; }
const char *fdg_tag_value(const fdg_decoder *d, int i) { return d->tags[i].value; }

int fdg_chapter_count(const fdg_decoder *d) { return d->chapter_count; }
double fdg_chapter_start(const fdg_decoder *d, int i) { return d->chapters[i].start; }
double fdg_chapter_end(const fdg_decoder *d, int i) { return d->chapters[i].end; }
const char *fdg_chapter_title(const fdg_decoder *d, int i) { return d->chapters[i].title; }

const uint8_t *fdg_cover_bytes(const fdg_decoder *d, size_t *length) {
    if (length != NULL) { *length = d->cover_length; }
    return d->cover;
}
