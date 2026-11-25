// Peanuts Native for Linux: AMediaCodec / AMediaFormat over FFmpeg's software decoders, for the host's movie and video-codec paths that the Android
// build feeds to the phone's MediaCodec (dm/host/plat/movie_avc_android.c, dm/host/android/aac_android.c), so those files build unchanged.
//   "video/avc"       → FFmpeg h264, output NV12 (COLOR_FormatYUV420SemiPlanar, 21) with stride = width, slice height = height
//   "audio/mp4a-latm" → FFmpeg aac (raw frames, AudioSpecificConfig in csd-0), output PCM16 interleaved (ENCODING_PCM_16BIT, 2)
// Any other type answers NULL (hle_movie_av1.c then decodes AV1 with dav1d, as on the Mac). Decoding is synchronous: queueInputBuffer decodes, and the
// pictures or samples it gives wait in output slots until dequeueOutputBuffer hands them out. A format change is reported before the first output in the new format.
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------- AMediaFormat ----------
const char *AMEDIAFORMAT_KEY_MIME = "mime", *AMEDIAFORMAT_KEY_WIDTH = "width", *AMEDIAFORMAT_KEY_HEIGHT = "height", *AMEDIAFORMAT_KEY_COLOR_FORMAT = "color-format",
    *AMEDIAFORMAT_KEY_COLOR_RANGE = "color-range", *AMEDIAFORMAT_KEY_COLOR_STANDARD = "color-standard", *AMEDIAFORMAT_KEY_DISPLAY_CROP = "crop",
    *AMEDIAFORMAT_KEY_STRIDE = "stride", *AMEDIAFORMAT_KEY_SLICE_HEIGHT = "slice-height", *AMEDIAFORMAT_KEY_MAX_INPUT_SIZE = "max-input-size",
    *AMEDIAFORMAT_KEY_SAMPLE_RATE = "sample-rate", *AMEDIAFORMAT_KEY_CHANNEL_COUNT = "channel-count", *AMEDIAFORMAT_KEY_IS_ADTS = "is-adts",
    *AMEDIAFORMAT_KEY_PCM_ENCODING = "pcm-encoding", *AMEDIAFORMAT_KEY_FRAME_RATE = "frame-rate", *AMEDIAFORMAT_KEY_LOW_LATENCY = "low-latency",
    *AMEDIAFORMAT_KEY_OPERATING_RATE = "operating-rate", *AMEDIAFORMAT_KEY_PRIORITY = "priority";
enum { FT_I32, FT_I64, FT_F, FT_S, FT_B, FT_R };
typedef struct { char *k; int t; int64_t i; float f; char *s; void *b; size_t bn; int32_t r[4]; } fentry;
struct AMediaFormat { fentry *e; int n, cap; char *str; };
AMediaFormat *AMediaFormat_new(void) { return calloc(1, sizeof(AMediaFormat)); }
media_status_t AMediaFormat_delete(AMediaFormat *f) {
    if (!f) return AMEDIA_OK;
    for (int i = 0; i < f->n; i++) { free(f->e[i].k); free(f->e[i].s); free(f->e[i].b); }
    free(f->e); free(f->str); free(f); return AMEDIA_OK;
}
static fentry *fget(const AMediaFormat *f, const char *k, int t) { for (int i = 0; f && i < f->n; i++) if (!strcmp(f->e[i].k, k) && (t < 0 || f->e[i].t == t)) return &f->e[i]; return NULL; }
static fentry *fput(AMediaFormat *f, const char *k, int t) {
    fentry *e = fget(f, k, -1);
    if (e) { free(e->s); free(e->b); e->s = NULL; e->b = NULL; }
    else {
        if (f->n == f->cap) { int c = f->cap ? f->cap * 2 : 16; fentry *n = realloc(f->e, (size_t)c * sizeof *n); if (!n) return NULL; f->e = n; f->cap = c; }
        e = &f->e[f->n++]; memset(e, 0, sizeof *e); e->k = strdup(k);
    }
    e->t = t; return e;
}
bool AMediaFormat_getInt32(AMediaFormat *f, const char *k, int32_t *o) { fentry *e = fget(f, k, FT_I32); if (e) *o = (int32_t)e->i; return e != NULL; }
bool AMediaFormat_getInt64(AMediaFormat *f, const char *k, int64_t *o) { fentry *e = fget(f, k, FT_I64); if (!e) e = fget(f, k, FT_I32); if (e) *o = e->i; return e != NULL; }
bool AMediaFormat_getFloat(AMediaFormat *f, const char *k, float *o) { fentry *e = fget(f, k, FT_F); if (e) *o = e->f; return e != NULL; }
bool AMediaFormat_getString(AMediaFormat *f, const char *k, const char **o) { fentry *e = fget(f, k, FT_S); if (e) *o = e->s; return e != NULL; }
bool AMediaFormat_getBuffer(AMediaFormat *f, const char *k, void **d, size_t *n) { fentry *e = fget(f, k, FT_B); if (e) { *d = e->b; *n = e->bn; } return e != NULL; }
bool AMediaFormat_getRect(AMediaFormat *f, const char *k, int32_t *l, int32_t *t, int32_t *r, int32_t *b) {
    fentry *e = fget(f, k, FT_R); if (e) { *l = e->r[0]; *t = e->r[1]; *r = e->r[2]; *b = e->r[3]; } return e != NULL;
}
void AMediaFormat_setInt32(AMediaFormat *f, const char *k, int32_t v) { fentry *e = fput(f, k, FT_I32); if (e) e->i = v; }
void AMediaFormat_setInt64(AMediaFormat *f, const char *k, int64_t v) { fentry *e = fput(f, k, FT_I64); if (e) e->i = v; }
void AMediaFormat_setFloat(AMediaFormat *f, const char *k, float v) { fentry *e = fput(f, k, FT_F); if (e) e->f = v; }
void AMediaFormat_setString(AMediaFormat *f, const char *k, const char *v) { fentry *e = fput(f, k, FT_S); if (e) e->s = strdup(v ? v : ""); }
void AMediaFormat_setBuffer(AMediaFormat *f, const char *k, const void *d, size_t n) {
    fentry *e = fput(f, k, FT_B); if (!e) return;
    e->b = malloc(n ? n : 1); if (e->b && n) memcpy(e->b, d, n); e->bn = e->b ? n : 0;
}
void AMediaFormat_setRect(AMediaFormat *f, const char *k, int32_t l, int32_t t, int32_t r, int32_t b) { fentry *e = fput(f, k, FT_R); if (e) { e->r[0] = l; e->r[1] = t; e->r[2] = r; e->r[3] = b; } }
const char *AMediaFormat_toString(AMediaFormat *f) {
    free(f->str); size_t cap = 64 + (size_t)f->n * 48, n = 0; f->str = malloc(cap); if (!f->str) return "";
    n += (size_t)snprintf(f->str, cap, "{");
    for (int i = 0; i < f->n && n < cap; i++) {
        const fentry *e = &f->e[i];
        if (e->t == FT_I32 || e->t == FT_I64) n += (size_t)snprintf(f->str + n, cap - n, "%s%s=%lld", i ? ", " : "", e->k, (long long)e->i);
        else if (e->t == FT_S) n += (size_t)snprintf(f->str + n, cap - n, "%s%s=%s", i ? ", " : "", e->k, e->s);
        else n += (size_t)snprintf(f->str + n, cap - n, "%s%s", i ? ", " : "", e->k);
    }
    if (n < cap) snprintf(f->str + n, cap - n, "}");
    return f->str;
}

// ---------- AMediaCodec ----------
#define NIN 4
#define NOUT 48
enum { K_VIDEO = 1, K_AUDIO = 2 };
typedef struct { int w, h, full, bt709, rate, ch; } ofmt;
typedef struct { int used; uint8_t *d; size_t n; int64_t pts; uint32_t flags; ofmt fmt; } oslot;
struct AMediaCodec {
    int kind; char name[32];
    AVCodecContext *ctx; AVPacket *pkt; AVFrame *frm;
    uint8_t *in[NIN]; size_t in_cap; int in_busy[NIN];
    oslot out[NOUT]; int head[NOUT], nq;     // nq indices into out[], in output order
    ofmt told; int told_valid;               // the format the caller was last told
    int rate, ch;                            // configured (audio)
    int red;
};

AMediaCodec *AMediaCodec_createDecoderByType(const char *mime) {
    int kind = !strcmp(mime, "video/avc") ? K_VIDEO : !strcmp(mime, "audio/mp4a-latm") ? K_AUDIO : 0;
    if (!kind) return NULL;
    const AVCodec *codec = avcodec_find_decoder(kind == K_VIDEO ? AV_CODEC_ID_H264 : AV_CODEC_ID_AAC);
    if (!codec) return NULL;
    AMediaCodec *c = calloc(1, sizeof *c); if (!c) return NULL;
    c->kind = kind; snprintf(c->name, sizeof c->name, "ffmpeg.%s", codec->name);
    c->ctx = avcodec_alloc_context3(codec); c->pkt = av_packet_alloc(); c->frm = av_frame_alloc();
    if (!c->ctx || !c->pkt || !c->frm) { AMediaCodec_delete(c); return NULL; }
    return c;
}
AMediaCodec *AMediaCodec_createCodecByName(const char *name) { (void)name; return NULL; }

static void out_clear(AMediaCodec *c) { for (int i = 0; i < NOUT; i++) { free(c->out[i].d); c->out[i].d = NULL; c->out[i].used = 0; } c->nq = 0; }
media_status_t AMediaCodec_delete(AMediaCodec *c) {
    if (!c) return AMEDIA_OK;
    out_clear(c);
    for (int i = 0; i < NIN; i++) free(c->in[i]);
    av_frame_free(&c->frm); av_packet_free(&c->pkt); avcodec_free_context(&c->ctx);
    free(c); return AMEDIA_OK;
}
media_status_t AMediaCodec_configure(AMediaCodec *c, const AMediaFormat *f_, ANativeWindow *surface, AMediaCrypto *crypto, uint32_t flags) {
    (void)surface; (void)crypto; if (flags & AMEDIACODEC_CONFIGURE_FLAG_ENCODE) return AMEDIA_ERROR_UNSUPPORTED;
    AMediaFormat *f = (AMediaFormat *)f_; int32_t v; void *b0 = NULL, *b1 = NULL; size_t n0 = 0, n1 = 0;
    AMediaFormat_getBuffer(f, "csd-0", &b0, &n0); AMediaFormat_getBuffer(f, "csd-1", &b1, &n1);
    size_t en = n0 + n1;
    if (en) {
        c->ctx->extradata = av_mallocz(en + AV_INPUT_BUFFER_PADDING_SIZE); if (!c->ctx->extradata) return AMEDIA_ERROR_UNKNOWN;
        if (n0) memcpy(c->ctx->extradata, b0, n0);
        if (n1) memcpy(c->ctx->extradata + n0, b1, n1);
        c->ctx->extradata_size = (int)en;
    }
    int32_t maxin = 1 << 20;
    if (c->kind == K_VIDEO) {
        if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, &v) && v > maxin) maxin = v;
        c->ctx->thread_count = 4; c->ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    } else {
        c->rate = AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, &v) ? v : 48000;
        c->ch = AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &v) ? v : 2;
        c->ctx->sample_rate = c->rate; av_channel_layout_default(&c->ctx->ch_layout, c->ch);
        maxin = 64 << 10;
    }
    c->in_cap = (size_t)maxin;
    for (int i = 0; i < NIN; i++) if (!(c->in[i] = malloc(c->in_cap + AV_INPUT_BUFFER_PADDING_SIZE))) return AMEDIA_ERROR_UNKNOWN;
    if (avcodec_open2(c->ctx, c->ctx->codec, NULL) < 0) return AMEDIA_ERROR_UNSUPPORTED;
    return AMEDIA_OK;
}
media_status_t AMediaCodec_start(AMediaCodec *c) { (void)c; return AMEDIA_OK; }
media_status_t AMediaCodec_stop(AMediaCodec *c) { if (c) { out_clear(c); memset(c->in_busy, 0, sizeof c->in_busy); } return AMEDIA_OK; }
media_status_t AMediaCodec_flush(AMediaCodec *c) {
    out_clear(c); memset(c->in_busy, 0, sizeof c->in_busy);
    avcodec_flush_buffers(c->ctx); return AMEDIA_OK;
}
ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec *c, int64_t timeout_us) {
    (void)timeout_us;
    if (c->nq >= NOUT - 8) return AMEDIACODEC_INFO_TRY_AGAIN_LATER;   // the caller must take outputs first, as with a full hardware decoder
    for (int i = 0; i < NIN; i++) if (!c->in_busy[i]) { c->in_busy[i] = 1; return i; }
    return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
}
uint8_t *AMediaCodec_getInputBuffer(AMediaCodec *c, size_t idx, size_t *out_size) {
    if (idx >= NIN) return NULL;
    if (out_size) *out_size = c->in_cap;
    return c->in[idx];
}

static oslot *slot_new(AMediaCodec *c) {
    if (c->nq >= NOUT) return NULL;
    for (int i = 0; i < NOUT; i++) if (!c->out[i].used) { c->out[i].used = 1; c->head[c->nq++] = i; return &c->out[i]; }
    return NULL;
}
static inline uint16_t px10(const uint8_t *row, int x) { uint16_t v; memcpy(&v, row + 2 * x, 2); return v; }
// One decoded picture as tight NV12 (even width and height)
static void keep_video(AMediaCodec *c, const AVFrame *fr) {
    int w = fr->width, h = fr->height, W = (w + 1) & ~1, H = (h + 1) & ~1;
    enum AVPixelFormat pf = (enum AVPixelFormat)fr->format;
    int hi = pf == AV_PIX_FMT_YUV420P10LE, ok = pf == AV_PIX_FMT_YUV420P || pf == AV_PIX_FMT_YUVJ420P || hi;
    if (!ok) { if (!c->red++) fprintf(stderr, "[RED] movie: H.264 pictures in %s: not converted, no pictures\n", av_get_pix_fmt_name(pf)); return; }
    oslot *s = slot_new(c); if (!s) { if (!c->red++) fprintf(stderr, "[RED] movie: H.264 output queue full: a picture is dropped\n"); return; }
    s->n = (size_t)W * H * 3 / 2; s->d = malloc(s->n); if (!s->d) { s->used = 0; c->nq--; return; }
    uint8_t *y = s->d, *uv = s->d + (size_t)W * H;
    for (int r = 0; r < H; r++) {
        int sr = r < h ? r : h - 1; const uint8_t *src = fr->data[0] + (size_t)sr * fr->linesize[0]; uint8_t *d = y + (size_t)r * W;
        if (hi) for (int x = 0; x < W; x++) d[x] = (uint8_t)(px10(src, x < w ? x : w - 1) >> 2);
        else { memcpy(d, src, (size_t)w); if (W > w) d[w] = d[w - 1]; }
    }
    int cw = (w + 1) / 2, ch = (h + 1) / 2;
    for (int r = 0; r < H / 2; r++) {
        int sr = r < ch ? r : ch - 1;
        const uint8_t *u = fr->data[1] + (size_t)sr * fr->linesize[1], *v = fr->data[2] + (size_t)sr * fr->linesize[2]; uint8_t *d = uv + (size_t)r * W;
        for (int x = 0; x < W / 2; x++) { int sx = x < cw ? x : cw - 1; d[2 * x] = hi ? (uint8_t)(px10(u, sx) >> 2) : u[sx]; d[2 * x + 1] = hi ? (uint8_t)(px10(v, sx) >> 2) : v[sx]; }
    }
    s->pts = fr->pts != AV_NOPTS_VALUE ? fr->pts : fr->best_effort_timestamp != AV_NOPTS_VALUE ? fr->best_effort_timestamp : 0; s->flags = 0;
    s->fmt = (ofmt){W, H, fr->color_range == AVCOL_RANGE_JPEG || pf == AV_PIX_FMT_YUVJ420P,
                    fr->colorspace == AVCOL_SPC_BT709 || (fr->colorspace == AVCOL_SPC_UNSPECIFIED && h >= 720), 0, 0};
}
static inline int16_t s16(float v) { v *= 32767.f; return (int16_t)(v > 32767.f ? 32767 : v < -32768.f ? -32768 : v); }
// One decoded AAC frame as interleaved PCM16
static void keep_audio(AMediaCodec *c, const AVFrame *fr) {
    int ch = fr->ch_layout.nb_channels, n = fr->nb_samples; if (ch < 1 || n < 1) return;
    oslot *s = slot_new(c); if (!s) { if (!c->red++) fprintf(stderr, "[RED] movie: AAC output queue full: samples are dropped\n"); return; }
    s->n = (size_t)n * ch * 2; s->d = malloc(s->n); if (!s->d) { s->used = 0; c->nq--; return; }
    int16_t *o = (int16_t *)s->d;
    switch (fr->format) {
    case AV_SAMPLE_FMT_FLTP: for (int i = 0; i < n; i++) for (int k = 0; k < ch; k++) o[i * ch + k] = s16(((const float *)fr->extended_data[k])[i]); break;
    case AV_SAMPLE_FMT_FLT: for (int i = 0; i < n * ch; i++) o[i] = s16(((const float *)fr->data[0])[i]); break;
    case AV_SAMPLE_FMT_S16P: for (int i = 0; i < n; i++) for (int k = 0; k < ch; k++) o[i * ch + k] = ((const int16_t *)fr->extended_data[k])[i]; break;
    case AV_SAMPLE_FMT_S16: memcpy(o, fr->data[0], s->n); break;
    default: memset(o, 0, s->n); if (!c->red++) fprintf(stderr, "[RED] movie: AAC samples in format %d: silence\n", fr->format);
    }
    s->pts = fr->pts != AV_NOPTS_VALUE ? fr->pts : 0; s->flags = 0;
    s->fmt = (ofmt){0, 0, 0, 0, fr->sample_rate > 0 ? fr->sample_rate : c->rate, ch};
}
static void receive_all(AMediaCodec *c) {
    for (;;) {
        int r = avcodec_receive_frame(c->ctx, c->frm);
        if (r < 0) return;   // EAGAIN (needs input), EOF, or an error
        if (c->kind == K_VIDEO) keep_video(c, c->frm); else keep_audio(c, c->frm);
        av_frame_unref(c->frm);
    }
}
media_status_t AMediaCodec_queueInputBuffer(AMediaCodec *c, size_t idx, off_t offset, size_t size, uint64_t time, uint32_t flags) {
    if (idx >= NIN || !c->in_busy[idx]) return AMEDIA_ERROR_INVALID_PARAMETER;
    c->in_busy[idx] = 0;
    if (size > 0 && (size_t)offset + size <= c->in_cap) {
        memset(c->in[idx] + offset + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        c->pkt->data = c->in[idx] + offset; c->pkt->size = (int)size; c->pkt->pts = c->pkt->dts = (int64_t)time;
        int r = avcodec_send_packet(c->ctx, c->pkt);
        if (r == AVERROR(EAGAIN)) { receive_all(c); r = avcodec_send_packet(c->ctx, c->pkt); }
        c->pkt->data = NULL; c->pkt->size = 0;
        if (r < 0 && r != AVERROR(EAGAIN) && !c->red++) fprintf(stderr, "[RED] movie: %s refused a packet (%d)\n", c->name, r);
        receive_all(c);
    }
    if (flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
        avcodec_send_packet(c->ctx, NULL); receive_all(c);
        avcodec_flush_buffers(c->ctx);   // ready for a new stream after the end
        oslot *s = slot_new(c);
        if (s) { s->d = NULL; s->n = 0; s->pts = 0; s->flags = AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM; s->fmt = c->told_valid ? c->told : (ofmt){0}; }
    }
    return AMEDIA_OK;
}
static int fmt_same(const ofmt *a, const ofmt *b) { return a->w == b->w && a->h == b->h && a->full == b->full && a->bt709 == b->bt709 && a->rate == b->rate && a->ch == b->ch; }
ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec *c, AMediaCodecBufferInfo *info, int64_t timeout_us) {
    (void)timeout_us;
    if (!c->nq) return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
    oslot *s = &c->out[c->head[0]];
    if (!(s->flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) && (!c->told_valid || !fmt_same(&c->told, &s->fmt))) { c->told = s->fmt; c->told_valid = 1; return AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED; }
    int idx = c->head[0]; memmove(c->head, c->head + 1, (size_t)(--c->nq) * sizeof *c->head);
    info->offset = 0; info->size = (int32_t)s->n; info->presentationTimeUs = s->pts; info->flags = s->flags;
    return idx;
}
uint8_t *AMediaCodec_getOutputBuffer(AMediaCodec *c, size_t idx, size_t *out_size) {
    if (idx >= NOUT || !c->out[idx].used) return NULL;
    if (out_size) *out_size = c->out[idx].n;
    return c->out[idx].d ? c->out[idx].d : (uint8_t *)c;   // an end-of-stream marker has no bytes: a valid pointer with size 0
}
media_status_t AMediaCodec_releaseOutputBuffer(AMediaCodec *c, size_t idx, bool render) {
    (void)render; if (idx >= NOUT) return AMEDIA_ERROR_INVALID_PARAMETER;
    free(c->out[idx].d); c->out[idx].d = NULL; c->out[idx].used = 0; return AMEDIA_OK;
}
AMediaFormat *AMediaCodec_getOutputFormat(AMediaCodec *c) {
    AMediaFormat *f = AMediaFormat_new(); if (!f) return NULL;
    const ofmt *o = &c->told;
    if (c->kind == K_VIDEO) {
        AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, "video/raw");
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_WIDTH, o->w); AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_HEIGHT, o->h);
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_STRIDE, o->w); AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_SLICE_HEIGHT, o->h);
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_COLOR_FORMAT, 21);   // COLOR_FormatYUV420SemiPlanar (NV12)
        AMediaFormat_setRect(f, AMEDIAFORMAT_KEY_DISPLAY_CROP, 0, 0, o->w - 1, o->h - 1);
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_COLOR_STANDARD, o->bt709 ? 1 : 2);   // COLOR_STANDARD_BT709 / BT601_PAL
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_COLOR_RANGE, o->full ? 1 : 2);        // COLOR_RANGE_FULL / LIMITED
    } else {
        AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, "audio/raw");
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, o->rate ? o->rate : c->rate);
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, o->ch ? o->ch : c->ch);
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_PCM_ENCODING, 2);   // ENCODING_PCM_16BIT
    }
    return f;
}
AMediaFormat *AMediaCodec_getInputFormat(AMediaCodec *c) { (void)c; return AMediaFormat_new(); }
media_status_t AMediaCodec_setParameters(AMediaCodec *c, const AMediaFormat *p) { (void)c; (void)p; return AMEDIA_OK; }
media_status_t AMediaCodec_getName(AMediaCodec *c, char **out) { *out = strdup(c->name); return *out ? AMEDIA_OK : AMEDIA_ERROR_UNKNOWN; }
void AMediaCodec_releaseName(AMediaCodec *c, char *name) { (void)c; free(name); }
