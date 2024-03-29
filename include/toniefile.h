
#include <stdint.h>

#include "fs_ext.h"

#define OPUS_FRAME_SIZE_MS OPUS_FRAMESIZE_60_MS
#define OPUS_SAMPLING_RATE 48000
#define OPUS_BIT_RATE 96000
#define OPUS_FRAME_SIZE 2880 /* samples: 60ms at 48kHz */
#define OPUS_CHANNELS 2
#define OPUS_PACKET_PAD 64
#define OPUS_PACKET_MINSIZE 64

#define TONIEFILE_FRAME_SIZE 4096
#define TONIEFILE_MAX_CHAPTERS 100
#define TONIEFILE_PAD_END 64

typedef struct toniefile_s toniefile_t;

typedef struct
{
    bool_t active;
    char *source;
    size_t skip_seconds;
    char *targetFile;
    OsTaskId taskId;
    error_t error;
    bool_t quit;

} ffmpeg_stream_ctx_t;

toniefile_t *toniefile_create(const char *fullPath, uint32_t audio_id);
error_t toniefile_close(toniefile_t *ctx);
error_t toniefile_encode(toniefile_t *ctx, int16_t *sample_buffer, size_t samples_available);
error_t toniefile_write_header(toniefile_t *ctx);
error_t toniefile_new_chapter(toniefile_t *ctx);

FILE *ffmpeg_decode_audio_start(const char *input_source);
FILE *ffmpeg_decode_audio_start_skip(const char *input_source, size_t skip_seconds);
error_t ffmpeg_decode_audio_end(FILE *ffmpeg_pipe, error_t error);
error_t ffmpeg_decode_audio(FILE *ffmpeg_pipe, int16_t *buffer, size_t size, size_t *bytes_read);
error_t ffmpeg_stream(char source[99][PATH_LEN], size_t source_len, const char *target_taf, size_t skip_seconds, bool_t *active);
error_t ffmpeg_convert(char source[99][PATH_LEN], size_t source_len, const char *target_taf, size_t skip_seconds);
void ffmpeg_stream_task(void *param);