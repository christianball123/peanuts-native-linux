// Peanuts Native for Linux: the phone-sensor API, without sensors (the handheld's gyro reaches the host through the controller instead:
// SDL's gamepad sensors → nxhost_input_motion). ASensorManager_getInstanceForPackage answers NULL, so the host's motion thread logs and stops.
#pragma once
#include <stdint.h>
#include <sys/types.h>
#include <android/looper.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { ASENSOR_TYPE_ACCELEROMETER = 1, ASENSOR_TYPE_MAGNETIC_FIELD = 2, ASENSOR_TYPE_GYROSCOPE = 4 };
typedef struct ASensorManager ASensorManager;
typedef struct ASensorEventQueue ASensorEventQueue;
typedef struct ASensor ASensor;
typedef const ASensor *ASensorRef;
typedef struct ASensorVector { union { float v[3]; struct { float x, y, z; }; struct { float azimuth, pitch, roll; }; }; int8_t status; uint8_t reserved[3]; } ASensorVector;
typedef struct ASensorEvent {
    int32_t version, sensor, type, reserved0; int64_t timestamp;
    union { float data[16]; ASensorVector vector; ASensorVector acceleration; ASensorVector magnetic; float temperature, distance, light, pressure; };
    uint32_t flags; int32_t reserved1[3];
} ASensorEvent;
typedef int (*ALooper_callbackFunc)(int fd, int events, void *data);
ASensorManager *ASensorManager_getInstanceForPackage(const char *package);
const ASensor *ASensorManager_getDefaultSensor(ASensorManager *m, int type);
ASensorEventQueue *ASensorManager_createEventQueue(ASensorManager *m, ALooper *looper, int ident, ALooper_callbackFunc cb, void *data);
int ASensorManager_destroyEventQueue(ASensorManager *m, ASensorEventQueue *q);
int ASensorEventQueue_registerSensor(ASensorEventQueue *q, const ASensor *s, int32_t period_us, int64_t latency_us);
int ASensorEventQueue_enableSensor(ASensorEventQueue *q, const ASensor *s);
int ASensorEventQueue_disableSensor(ASensorEventQueue *q, const ASensor *s);
int ASensorEventQueue_setEventRate(ASensorEventQueue *q, const ASensor *s, int32_t us);
ssize_t ASensorEventQueue_getEvents(ASensorEventQueue *q, ASensorEvent *events, size_t count);
#ifdef __cplusplus
}
#endif
