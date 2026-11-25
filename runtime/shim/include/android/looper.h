// Peanuts Native for Linux: a minimal ALooper (a thread-local wake pipe), enough for the host's sensor thread to sleep and be woken.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ALooper ALooper;
enum { ALOOPER_PREPARE_ALLOW_NON_CALLBACKS = 1 };
enum { ALOOPER_POLL_WAKE = -1, ALOOPER_POLL_CALLBACK = -2, ALOOPER_POLL_TIMEOUT = -3, ALOOPER_POLL_ERROR = -4 };
ALooper *ALooper_prepare(int opts);
ALooper *ALooper_forThread(void);
int ALooper_pollOnce(int timeout_ms, int *out_fd, int *out_events, void **out_data);
void ALooper_wake(ALooper *looper);
void ALooper_acquire(ALooper *looper);
void ALooper_release(ALooper *looper);
#ifdef __cplusplus
}
#endif
