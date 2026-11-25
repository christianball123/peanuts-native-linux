// Peanuts Native for Linux: AMediaFormat, a small key/value store (pnl_mediacodec.c).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { AMEDIA_OK = 0, AMEDIA_ERROR_BASE = -10000, AMEDIA_ERROR_UNKNOWN = -10000, AMEDIA_ERROR_MALFORMED = -10001, AMEDIA_ERROR_UNSUPPORTED = -10002,
               AMEDIA_ERROR_INVALID_OBJECT = -10003, AMEDIA_ERROR_INVALID_PARAMETER = -10004, AMEDIA_ERROR_INVALID_OPERATION = -10005 } media_status_t;
typedef struct AMediaFormat AMediaFormat;
extern const char *AMEDIAFORMAT_KEY_MIME, *AMEDIAFORMAT_KEY_WIDTH, *AMEDIAFORMAT_KEY_HEIGHT, *AMEDIAFORMAT_KEY_COLOR_FORMAT, *AMEDIAFORMAT_KEY_COLOR_RANGE,
    *AMEDIAFORMAT_KEY_COLOR_STANDARD, *AMEDIAFORMAT_KEY_DISPLAY_CROP, *AMEDIAFORMAT_KEY_STRIDE, *AMEDIAFORMAT_KEY_SLICE_HEIGHT, *AMEDIAFORMAT_KEY_MAX_INPUT_SIZE,
    *AMEDIAFORMAT_KEY_SAMPLE_RATE, *AMEDIAFORMAT_KEY_CHANNEL_COUNT, *AMEDIAFORMAT_KEY_IS_ADTS, *AMEDIAFORMAT_KEY_PCM_ENCODING, *AMEDIAFORMAT_KEY_FRAME_RATE,
    *AMEDIAFORMAT_KEY_LOW_LATENCY, *AMEDIAFORMAT_KEY_OPERATING_RATE, *AMEDIAFORMAT_KEY_PRIORITY;
AMediaFormat *AMediaFormat_new(void);
media_status_t AMediaFormat_delete(AMediaFormat *f);
bool AMediaFormat_getInt32(AMediaFormat *f, const char *name, int32_t *out);
bool AMediaFormat_getInt64(AMediaFormat *f, const char *name, int64_t *out);
bool AMediaFormat_getFloat(AMediaFormat *f, const char *name, float *out);
bool AMediaFormat_getString(AMediaFormat *f, const char *name, const char **out);
bool AMediaFormat_getBuffer(AMediaFormat *f, const char *name, void **data, size_t *size);
bool AMediaFormat_getRect(AMediaFormat *f, const char *name, int32_t *left, int32_t *top, int32_t *right, int32_t *bottom);
void AMediaFormat_setInt32(AMediaFormat *f, const char *name, int32_t value);
void AMediaFormat_setInt64(AMediaFormat *f, const char *name, int64_t value);
void AMediaFormat_setFloat(AMediaFormat *f, const char *name, float value);
void AMediaFormat_setString(AMediaFormat *f, const char *name, const char *value);
void AMediaFormat_setBuffer(AMediaFormat *f, const char *name, const void *data, size_t size);
void AMediaFormat_setRect(AMediaFormat *f, const char *name, int32_t left, int32_t top, int32_t right, int32_t bottom);
const char *AMediaFormat_toString(AMediaFormat *f);
#ifdef __cplusplus
}
#endif
