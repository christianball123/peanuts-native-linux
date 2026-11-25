// Peanuts Native for Linux: AMediaCodec over FFmpeg's decoders (pnl_mediacodec.c): "video/avc" (H.264, NV12 out) and "audio/mp4a-latm" (AAC, PCM16 out).
// Any other type answers NULL, as a phone without that decoder does (AV1 movies then use dav1d, as on the Mac).
#pragma once
#include <media/NdkMediaFormat.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AMediaCodec AMediaCodec;
typedef struct ANativeWindow ANativeWindow;
typedef struct AMediaCrypto AMediaCrypto;
typedef struct AMediaCodecBufferInfo { int32_t offset, size; int64_t presentationTimeUs; uint32_t flags; } AMediaCodecBufferInfo;
enum { AMEDIACODEC_BUFFER_FLAG_KEY_FRAME = 1, AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG = 2, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM = 4, AMEDIACODEC_BUFFER_FLAG_PARTIAL_FRAME = 8 };
enum { AMEDIACODEC_CONFIGURE_FLAG_ENCODE = 1 };
enum { AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED = -3, AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED = -2, AMEDIACODEC_INFO_TRY_AGAIN_LATER = -1 };
AMediaCodec *AMediaCodec_createDecoderByType(const char *mime_type);
AMediaCodec *AMediaCodec_createCodecByName(const char *name);
media_status_t AMediaCodec_delete(AMediaCodec *c);
media_status_t AMediaCodec_configure(AMediaCodec *c, const AMediaFormat *format, ANativeWindow *surface, AMediaCrypto *crypto, uint32_t flags);
media_status_t AMediaCodec_start(AMediaCodec *c);
media_status_t AMediaCodec_stop(AMediaCodec *c);
media_status_t AMediaCodec_flush(AMediaCodec *c);
ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec *c, int64_t timeout_us);
uint8_t *AMediaCodec_getInputBuffer(AMediaCodec *c, size_t idx, size_t *out_size);
media_status_t AMediaCodec_queueInputBuffer(AMediaCodec *c, size_t idx, off_t offset, size_t size, uint64_t time, uint32_t flags);
ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec *c, AMediaCodecBufferInfo *info, int64_t timeout_us);
uint8_t *AMediaCodec_getOutputBuffer(AMediaCodec *c, size_t idx, size_t *out_size);
AMediaFormat *AMediaCodec_getOutputFormat(AMediaCodec *c);
AMediaFormat *AMediaCodec_getInputFormat(AMediaCodec *c);
media_status_t AMediaCodec_releaseOutputBuffer(AMediaCodec *c, size_t idx, bool render);
media_status_t AMediaCodec_setParameters(AMediaCodec *c, const AMediaFormat *params);
media_status_t AMediaCodec_getName(AMediaCodec *c, char **out_name);
void AMediaCodec_releaseName(AMediaCodec *c, char *name);
#ifdef __cplusplus
}
#endif
