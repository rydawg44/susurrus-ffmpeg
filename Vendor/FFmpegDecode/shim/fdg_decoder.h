// Susurrus's narrow C surface over FFmpeg. Nothing else from FFmpeg is exported,
// which keeps the LGPL boundary obvious and the dynamic framework small.
#ifndef FDG_DECODER_H
#define FDG_DECODER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fdg_decoder fdg_decoder;

/// Opens `path` and prepares a float, interleaved stream at the requested format.
/// Returns NULL and fills `error` when the file cannot be decoded.
fdg_decoder *fdg_open(const char *path, int sample_rate, int channels, char *error, size_t error_length);
void fdg_close(fdg_decoder *decoder);

double fdg_duration_seconds(const fdg_decoder *decoder);
int fdg_source_sample_rate(const fdg_decoder *decoder);
int fdg_source_channels(const fdg_decoder *decoder);

/// Fills `output` with up to `max_frames` interleaved frames.
/// Returns frames written, 0 at end of stream, or a negative value on error.
int fdg_read(fdg_decoder *decoder, float *output, int max_frames);

int fdg_tag_count(const fdg_decoder *decoder);
const char *fdg_tag_key(const fdg_decoder *decoder, int index);
const char *fdg_tag_value(const fdg_decoder *decoder, int index);

int fdg_chapter_count(const fdg_decoder *decoder);
double fdg_chapter_start(const fdg_decoder *decoder, int index);
double fdg_chapter_end(const fdg_decoder *decoder, int index);
const char *fdg_chapter_title(const fdg_decoder *decoder, int index);

/// Embedded cover bytes, owned by the decoder. NULL when absent.
const uint8_t *fdg_cover_bytes(const fdg_decoder *decoder, size_t *length);

#ifdef __cplusplus
}
#endif

#endif
