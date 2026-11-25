// Peanuts Native for Linux: small Bionic / NDK pieces the host uses — the log, system properties, ALooper, the (absent) phone sensors,
// and pthread_gettid_np for glibc releases older than 2.42.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <android/log.h>
#include <android/looper.h>
#include <android/sensor.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

// ---- Log ----
static const char *prio_name(int p) { return p >= ANDROID_LOG_ERROR ? "E" : p == ANDROID_LOG_WARN ? "W" : p == ANDROID_LOG_INFO ? "I" : "D"; }
int __android_log_write(int prio, const char *tag, const char *text) {
    if (prio < ANDROID_LOG_INFO && !getenv("PNL_LOG_DEBUG")) return 0;
    fprintf(stderr, "%s/%s: %s\n", prio_name(prio), tag ? tag : "", text ? text : ""); return 1;
}
int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap) {
    char b[4096]; vsnprintf(b, sizeof b, fmt, ap); return __android_log_write(prio, tag, b);
}
int __android_log_print(int prio, const char *tag, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = __android_log_vprint(prio, tag, fmt, ap); va_end(ap); return r; }
void __android_log_assert(const char *cond, const char *tag, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); char b[4096]; vsnprintf(b, sizeof b, fmt ? fmt : "", ap); va_end(ap);
    fprintf(stderr, "F/%s: assertion %s: %s\n", tag ? tag : "", cond ? cond : "", b); abort();
}

// ---- System properties: only the device's maker and model (the host's log and bug reports), from the device tree or DMI ----
static int read_first_line(const char *path, char *out, size_t n) {
    FILE *f = fopen(path, "re"); if (!f) return 0;
    size_t k = fread(out, 1, n - 1, f); fclose(f); out[k] = 0;
    out[strcspn(out, "\r\n")] = 0; for (size_t i = 0; out[i]; i++) if ((unsigned char)out[i] < 32) out[i] = 0;   // device-tree strings end in NUL
    return out[0] != 0;
}
int __system_property_get(const char *name, char *value) {
    char b[PROP_VALUE_MAX] = "";
    if (!strcmp(name, "ro.product.model")) {
        if (!read_first_line("/sys/firmware/devicetree/base/model", b, sizeof b)) read_first_line("/sys/class/dmi/id/product_name", b, sizeof b);
    } else if (!strcmp(name, "ro.product.manufacturer")) {
        read_first_line("/sys/class/dmi/id/sys_vendor", b, sizeof b);   // the device tree's model already names the maker ("AYN Thor")
    } else if (!strcmp(name, "ro.build.version.release")) snprintf(b, sizeof b, "Linux");
    snprintf(value, PROP_VALUE_MAX, "%s", b);
    return (int)strlen(value);
}

// ---- ALooper: one per thread, an eventfd to be woken by ----
struct ALooper { int efd; _Atomic int refs; };
static __thread ALooper *t_looper;
ALooper *ALooper_prepare(int opts) {
    (void)opts; if (t_looper) return t_looper;
    ALooper *l = calloc(1, sizeof *l); if (!l) return NULL;
    l->efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK); atomic_store(&l->refs, 1);
    return t_looper = l;
}
ALooper *ALooper_forThread(void) { return t_looper; }
int ALooper_pollOnce(int timeout_ms, int *out_fd, int *out_events, void **out_data) {
    (void)out_fd; (void)out_events; (void)out_data;
    ALooper *l = t_looper; if (!l || l->efd < 0) { if (timeout_ms > 0) usleep((useconds_t)timeout_ms * 1000); return ALOOPER_POLL_TIMEOUT; }
    struct pollfd p = {l->efd, POLLIN, 0};
    int r = poll(&p, 1, timeout_ms);
    if (r > 0) { uint64_t v; if (read(l->efd, &v, sizeof v)) {} return ALOOPER_POLL_WAKE; }
    return r == 0 ? ALOOPER_POLL_TIMEOUT : ALOOPER_POLL_ERROR;
}
void ALooper_wake(ALooper *l) { if (l && l->efd >= 0) { uint64_t one = 1; if (write(l->efd, &one, sizeof one)) {} } }
void ALooper_acquire(ALooper *l) { if (l) atomic_fetch_add(&l->refs, 1); }
void ALooper_release(ALooper *l) { if (l && atomic_fetch_sub(&l->refs, 1) == 1) { if (l->efd >= 0) close(l->efd); if (t_looper == l) t_looper = NULL; free(l); } }

// ---- Phone sensors: none (the controller's own sensors come through SDL instead) ----
ASensorManager *ASensorManager_getInstanceForPackage(const char *package) { (void)package; return NULL; }
const ASensor *ASensorManager_getDefaultSensor(ASensorManager *m, int type) { (void)m; (void)type; return NULL; }
ASensorEventQueue *ASensorManager_createEventQueue(ASensorManager *m, ALooper *l, int ident, ALooper_callbackFunc cb, void *data) { (void)m; (void)l; (void)ident; (void)cb; (void)data; return NULL; }
int ASensorManager_destroyEventQueue(ASensorManager *m, ASensorEventQueue *q) { (void)m; (void)q; return 0; }
int ASensorEventQueue_registerSensor(ASensorEventQueue *q, const ASensor *s, int32_t p, int64_t l) { (void)q; (void)s; (void)p; (void)l; return -1; }
int ASensorEventQueue_enableSensor(ASensorEventQueue *q, const ASensor *s) { (void)q; (void)s; return -1; }
int ASensorEventQueue_disableSensor(ASensorEventQueue *q, const ASensor *s) { (void)q; (void)s; return 0; }
int ASensorEventQueue_setEventRate(ASensorEventQueue *q, const ASensor *s, int32_t us) { (void)q; (void)s; (void)us; return -1; }
ssize_t ASensorEventQueue_getEvents(ASensorEventQueue *q, ASensorEvent *e, size_t n) { (void)q; (void)e; (void)n; return 0; }

// ---- pthread_gettid_np: glibc has it from 2.42 (ArmadaOS / Fedora 44 ships 2.43); older systems get the tid from struct pthread, whose offset is found once ----
static _Atomic long g_tid_off = -1;
static _Atomic int g_probe_go;
static void *tid_probe(void *arg) { (void)arg; while (!atomic_load(&g_probe_go)) usleep(100); return NULL; }   // alive until its tid was read (glibc clears it at exit)
static long find_tid_offset(void) {
    pid_t me = gettid(); const unsigned char *p = (const unsigned char *)pthread_self();
    for (long off = 0; off < 1024; off += 4) { pid_t v; memcpy(&v, p + off, sizeof v); if (v == me) {
        // confirm on a second thread: its tid must sit at the same offset
        pthread_t t; atomic_store(&g_probe_go, 0); if (pthread_create(&t, NULL, tid_probe, NULL)) return off;
        pid_t tv; memcpy(&tv, (const unsigned char *)t + off, sizeof tv); atomic_store(&g_probe_go, 1); pthread_join(t, NULL);
        if (tv > 0 && tv != me) return off;
    } }
    return -2;
}
pid_t pthread_gettid_np(pthread_t t) {
    static pid_t (*real)(pthread_t); static _Atomic int looked;
    if (!atomic_load(&looked)) { real = (pid_t (*)(pthread_t))dlvsym(RTLD_NEXT, "pthread_gettid_np", "GLIBC_2.42"); atomic_store(&looked, 1); }
    if (real) return real(t);
    if (pthread_equal(t, pthread_self())) return gettid();
    long off = atomic_load(&g_tid_off);
    if (off == -1) { off = find_tid_offset(); atomic_store(&g_tid_off, off); }
    if (off < 0) return -1;
    pid_t v; memcpy(&v, (const unsigned char *)t + off, sizeof v); return v;
}
