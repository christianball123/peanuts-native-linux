// Peanuts Native for Linux: just enough of JNI's types for the few Java bridge functions in the Peanuts sources to compile.
// There is no Java VM on Linux: those functions are never called (peanuts-run calls the C functions behind them directly).
#pragma once
#include <stdint.h>
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef uint8_t jboolean; typedef int8_t jbyte; typedef uint16_t jchar; typedef int16_t jshort; typedef int32_t jint; typedef int64_t jlong;
typedef float jfloat; typedef double jdouble; typedef jint jsize;
typedef void *jobject; typedef jobject jclass, jstring, jarray, jobjectArray, jbooleanArray, jbyteArray, jcharArray, jshortArray, jintArray,
    jlongArray, jfloatArray, jdoubleArray, jthrowable, jweak;
typedef struct _jfieldID *jfieldID; typedef struct _jmethodID *jmethodID;
#define JNI_FALSE 0
#define JNI_TRUE 1
#define JNI_OK 0
#define JNI_ERR (-1)
#define JNI_VERSION_1_6 0x00010006
#define JNIEXPORT __attribute__((visibility("default")))
#define JNICALL
struct JNINativeInterface {
    jsize (*GetArrayLength)(struct JNINativeInterface const **env, jarray a);
    void (*SetFloatArrayRegion)(struct JNINativeInterface const **env, jfloatArray a, jsize start, jsize len, const jfloat *buf);
    void (*GetFloatArrayRegion)(struct JNINativeInterface const **env, jfloatArray a, jsize start, jsize len, jfloat *buf);
    void (*SetIntArrayRegion)(struct JNINativeInterface const **env, jintArray a, jsize start, jsize len, const jint *buf);
    void (*GetIntArrayRegion)(struct JNINativeInterface const **env, jintArray a, jsize start, jsize len, jint *buf);
    void (*SetLongArrayRegion)(struct JNINativeInterface const **env, jlongArray a, jsize start, jsize len, const jlong *buf);
    void (*SetByteArrayRegion)(struct JNINativeInterface const **env, jbyteArray a, jsize start, jsize len, const jbyte *buf);
    void (*GetByteArrayRegion)(struct JNINativeInterface const **env, jbyteArray a, jsize start, jsize len, jbyte *buf);
    jfloatArray (*NewFloatArray)(struct JNINativeInterface const **env, jsize n);
    jbyteArray (*NewByteArray)(struct JNINativeInterface const **env, jsize n);
    jstring (*NewStringUTF)(struct JNINativeInterface const **env, const char *s);
    const char *(*GetStringUTFChars)(struct JNINativeInterface const **env, jstring s, jboolean *copy);
    void (*ReleaseStringUTFChars)(struct JNINativeInterface const **env, jstring s, const char *u);
    void *(*GetDirectBufferAddress)(struct JNINativeInterface const **env, jobject buf);
    jlong (*GetDirectBufferCapacity)(struct JNINativeInterface const **env, jobject buf);
};
typedef const struct JNINativeInterface *JNIEnv;
typedef struct JavaVM_ *JavaVM;
#ifdef __cplusplus
}
#endif
