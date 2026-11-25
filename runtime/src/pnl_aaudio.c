// Peanuts Native for Linux: AAudio output streams over SDL3 (PipeWire, PulseAudio or ALSA underneath). The host's audio output (hle_audio.c) opens one stream
// per Switch output, 48 kHz PCM16, and fills it from AAudio's data callback; here that callback runs on SDL's audio thread whenever the device wants more.
// SDL converts the format and rate to the device's, so the 48 kHz stream the host asks for always opens.
#include <aaudio/AAudio.h>
#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct AAudioStreamBuilderStruct { int32_t rate, ch, fmt, perf, share, cap; AAudioStream_dataCallback data; AAudioStream_errorCallback err; void *data_user, *err_user; };
struct AAudioStreamStruct {
    SDL_AudioStream *as; int32_t rate, ch, fmt, frame_bytes, burst, cap, size, perf, share;
    _Atomic int32_t state; AAudioStream_dataCallback data; void *data_user; AAudioStream_errorCallback err; void *err_user;
    uint8_t *scratch; size_t scratch_cap; _Atomic int32_t xruns;
};

aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **b) {
    *b = calloc(1, sizeof **b); if (!*b) return AAUDIO_ERROR_NO_MEMORY;
    (*b)->fmt = AAUDIO_FORMAT_PCM_I16; (*b)->ch = 2; (*b)->perf = AAUDIO_PERFORMANCE_MODE_LOW_LATENCY; (*b)->share = AAUDIO_SHARING_MODE_SHARED;
    return AAUDIO_OK;
}
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder *b, aaudio_direction_t d) { (void)b; (void)d; }
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *b, aaudio_performance_mode_t m) { b->perf = m; }
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *b, aaudio_sharing_mode_t m) { (void)m; b->share = AAUDIO_SHARING_MODE_SHARED; }
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *b, aaudio_format_t f) { b->fmt = f; }
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *b, int32_t n) { b->ch = n; }
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder *b, int32_t rate) { b->rate = rate; }
void AAudioStreamBuilder_setUsage(AAudioStreamBuilder *b, aaudio_usage_t u) { (void)b; (void)u; }
void AAudioStreamBuilder_setContentType(AAudioStreamBuilder *b, aaudio_content_type_t t) { (void)b; (void)t; }
void AAudioStreamBuilder_setBufferCapacityInFrames(AAudioStreamBuilder *b, int32_t frames) { b->cap = frames; }
void AAudioStreamBuilder_setFramesPerDataCallback(AAudioStreamBuilder *b, int32_t frames) { (void)b; (void)frames; }
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *b, AAudioStream_dataCallback cb, void *user) { b->data = cb; b->data_user = user; }
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *b, AAudioStream_errorCallback cb, void *user) { b->err = cb; b->err_user = user; }
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *b) { free(b); return AAUDIO_OK; }

// SDL asks for `need` more bytes: whole frames from the host's callback, in bursts, while the stream runs; silence when it is stopped
static void SDLCALL feed(void *user, SDL_AudioStream *as, int need, int total) {
    (void)total; AAudioStream *s = user;
    if (need <= 0) return;
    int32_t frames = (need + s->frame_bytes - 1) / s->frame_bytes;
    size_t bytes = (size_t)frames * (size_t)s->frame_bytes;
    if (bytes > s->scratch_cap) { uint8_t *n = realloc(s->scratch, bytes); if (!n) return; s->scratch = n; s->scratch_cap = bytes; }
    if (atomic_load(&s->state) != AAUDIO_STREAM_STATE_STARTED || !s->data) { memset(s->scratch, 0, bytes); SDL_PutAudioStreamData(as, s->scratch, (int)bytes); return; }
    for (int32_t done = 0; done < frames;) {
        int32_t k = frames - done < s->burst ? frames - done : s->burst;
        uint8_t *dst = s->scratch + (size_t)done * (size_t)s->frame_bytes;
        if (s->data(s, s->data_user, dst, k) == AAUDIO_CALLBACK_RESULT_STOP) { memset(dst, 0, (size_t)(frames - done) * (size_t)s->frame_bytes); atomic_store(&s->state, AAUDIO_STREAM_STATE_STOPPED); break; }
        done += k;
    }
    SDL_PutAudioStreamData(as, s->scratch, (int)bytes);
}

aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *b, AAudioStream **out) {
    *out = NULL;
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) { fprintf(stderr, "[RED] audio: SDL audio does not start: %s\n", SDL_GetError()); return AAUDIO_ERROR_UNAVAILABLE; }
    if (b->ch < 1 || b->ch > 8) return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    AAudioStream *s = calloc(1, sizeof *s); if (!s) return AAUDIO_ERROR_NO_MEMORY;
    s->rate = b->rate > 0 ? b->rate : 48000; s->ch = b->ch; s->fmt = b->fmt == AAUDIO_FORMAT_PCM_FLOAT ? AAUDIO_FORMAT_PCM_FLOAT : AAUDIO_FORMAT_PCM_I16;
    s->frame_bytes = s->ch * (s->fmt == AAUDIO_FORMAT_PCM_FLOAT ? 4 : 2);
    s->burst = s->rate / 200 >= 64 ? 256 * s->rate / 48000 : 64;   // about 5 ms
    s->cap = b->cap > 0 ? b->cap : 8 * s->burst; s->size = 4 * s->burst; s->perf = b->perf; s->share = AAUDIO_SHARING_MODE_SHARED;
    s->data = b->data; s->data_user = b->data_user; s->err = b->err; s->err_user = b->err_user;
    atomic_store(&s->state, AAUDIO_STREAM_STATE_OPEN);
    SDL_AudioSpec spec = {s->fmt == AAUDIO_FORMAT_PCM_FLOAT ? SDL_AUDIO_F32 : SDL_AUDIO_S16, s->ch, s->rate};
    s->as = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, s);
    if (!s->as) { fprintf(stderr, "[RED] audio: no output stream (%s)\n", SDL_GetError()); free(s); return AAUDIO_ERROR_UNAVAILABLE; }
    *out = s; return AAUDIO_OK;
}
aaudio_result_t AAudioStream_close(AAudioStream *s) {
    if (!s) return AAUDIO_ERROR_NULL;
    atomic_store(&s->state, AAUDIO_STREAM_STATE_CLOSED);
    SDL_DestroyAudioStream(s->as);   // stops the device's callbacks before it returns
    free(s->scratch); free(s); return AAUDIO_OK;
}
aaudio_result_t AAudioStream_requestStart(AAudioStream *s) {
    atomic_store(&s->state, AAUDIO_STREAM_STATE_STARTED);
    return SDL_ResumeAudioStreamDevice(s->as) ? AAUDIO_OK : AAUDIO_ERROR_DISCONNECTED;
}
aaudio_result_t AAudioStream_requestPause(AAudioStream *s) { atomic_store(&s->state, AAUDIO_STREAM_STATE_PAUSED); SDL_PauseAudioStreamDevice(s->as); return AAUDIO_OK; }
aaudio_result_t AAudioStream_requestFlush(AAudioStream *s) { SDL_ClearAudioStream(s->as); atomic_store(&s->state, AAUDIO_STREAM_STATE_FLUSHED); return AAUDIO_OK; }
aaudio_result_t AAudioStream_requestStop(AAudioStream *s) {
    SDL_LockAudioStream(s->as); atomic_store(&s->state, AAUDIO_STREAM_STATE_STOPPED); SDL_UnlockAudioStream(s->as);   // no data callback runs after this returns
    SDL_PauseAudioStreamDevice(s->as); SDL_ClearAudioStream(s->as);
    return AAUDIO_OK;
}
aaudio_stream_state_t AAudioStream_getState(AAudioStream *s) { return s ? atomic_load(&s->state) : AAUDIO_STREAM_STATE_UNKNOWN; }
aaudio_result_t AAudioStream_waitForStateChange(AAudioStream *s, aaudio_stream_state_t in, aaudio_stream_state_t *next, int64_t timeout_ns) {
    // every transition here is immediate: a state that still equals `in` stays so until someone else changes it
    for (int64_t waited = 0;; waited += 1000000) {
        aaudio_stream_state_t st = atomic_load(&s->state);
        if (st != in || waited >= timeout_ns) { if (next) *next = st; return st != in ? AAUDIO_OK : AAUDIO_ERROR_TIMEOUT; }
        SDL_Delay(1);
    }
}
int32_t AAudioStream_getSampleRate(AAudioStream *s) { return s->rate; }
int32_t AAudioStream_getChannelCount(AAudioStream *s) { return s->ch; }
int32_t AAudioStream_getBufferCapacityInFrames(AAudioStream *s) { return s->cap; }
int32_t AAudioStream_getBufferSizeInFrames(AAudioStream *s) { return s->size; }
aaudio_result_t AAudioStream_setBufferSizeInFrames(AAudioStream *s, int32_t frames) { s->size = frames < s->burst ? s->burst : frames > s->cap ? s->cap : frames; return s->size; }
int32_t AAudioStream_getFramesPerBurst(AAudioStream *s) { return s->burst; }
aaudio_performance_mode_t AAudioStream_getPerformanceMode(AAudioStream *s) { return s->perf; }
aaudio_sharing_mode_t AAudioStream_getSharingMode(AAudioStream *s) { return s->share; }
int32_t AAudioStream_getDeviceId(AAudioStream *s) { (void)s; return 0; }
int32_t AAudioStream_getXRunCount(AAudioStream *s) { return atomic_load(&s->xruns); }
const char *AAudio_convertResultToText(aaudio_result_t r) {
    switch (r) {
    case AAUDIO_OK: return "AAUDIO_OK"; case AAUDIO_ERROR_DISCONNECTED: return "AAUDIO_ERROR_DISCONNECTED"; case AAUDIO_ERROR_ILLEGAL_ARGUMENT: return "AAUDIO_ERROR_ILLEGAL_ARGUMENT";
    case AAUDIO_ERROR_UNAVAILABLE: return "AAUDIO_ERROR_UNAVAILABLE (no audio output)"; case AAUDIO_ERROR_NO_MEMORY: return "AAUDIO_ERROR_NO_MEMORY";
    case AAUDIO_ERROR_TIMEOUT: return "AAUDIO_ERROR_TIMEOUT"; default: return "AAUDIO_ERROR";
    }
}
const char *AAudio_convertStreamStateToText(aaudio_stream_state_t s) {
    static const char *n[] = {"UNINITIALIZED", "UNKNOWN", "OPEN", "STARTING", "STARTED", "PAUSING", "PAUSED", "FLUSHING", "FLUSHED", "STOPPING", "STOPPED", "CLOSING", "CLOSED", "DISCONNECTED"};
    return s >= 0 && s < (int)(sizeof n / sizeof *n) ? n[s] : "?";
}
