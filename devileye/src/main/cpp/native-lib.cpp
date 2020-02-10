#include <jni.h>
#include <string>
#include "JNIUtils.h"
#include "QRCodeRecognizer.h"
#include "opencv2/opencv.hpp"
#include <vector>
#include <opencv2/imgproc/types_c.h>
#include "ImageScheduler.h"
#include "JavaCallHelper.h"
#include <sys/time.h>

JavaCallHelper *javaCallHelper;
JavaVM *javaVM = nullptr;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    javaVM = vm;

    if (vm->GetEnv((void **) &env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR; // JNI version not supported.
    }

    return JNI_VERSION_1_6;
}

extern "C"
JNIEXPORT jlong JNICALL
Java_me_devilsen_czxing_code_NativeSdk_createInstance(JNIEnv *env, jobject instance,
                                                      jintArray formats_) {
    try {
        if (javaCallHelper) {
            DELETE(javaCallHelper);
        }
        javaCallHelper = new JavaCallHelper(javaVM, env, instance);

        auto *imageScheduler = new ImageScheduler(env, javaCallHelper);
        return reinterpret_cast<jlong>(imageScheduler);
    } catch (const std::exception &e) {
        ThrowJavaException(env, e.what());
    } catch (...) {
        ThrowJavaException(env, "Unknown exception");
    }
    return 0;
}

extern "C"
JNIEXPORT void JNICALL
Java_me_devilsen_czxing_code_NativeSdk_destroyInstance(JNIEnv *env, jobject instance,
                                                       jlong objPtr) {

    try {
        auto imageScheduler = reinterpret_cast<ImageScheduler *>(objPtr);
        imageScheduler->stop();
        delete imageScheduler;
        DELETE(javaCallHelper);
    }
    catch (const std::exception &e) {
        ThrowJavaException(env, e.what());
    }
    catch (...) {
        ThrowJavaException(env, "Unknown exception");
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_me_devilsen_czxing_code_NativeSdk_prepareRead(JNIEnv *env, jobject thiz, jlong objPtr) {
    auto imageScheduler = reinterpret_cast<ImageScheduler *>(objPtr);
    imageScheduler->prepare();
}

extern "C"
JNIEXPORT void JNICALL
Java_me_devilsen_czxing_code_NativeSdk_stopRead(JNIEnv *env, jobject thiz, jlong objPtr) {
    auto imageScheduler = reinterpret_cast<ImageScheduler *>(objPtr);
    imageScheduler->stop();
}

extern "C"
JNIEXPORT jint JNICALL
Java_me_devilsen_czxing_code_NativeSdk_readBarcodeByte(JNIEnv *env, jobject instance, jlong objPtr,
                                                       jbyteArray bytes_, jint left, jint top,
                                                       jint cropWidth, jint cropHeight,
                                                       jint rowWidth, jint rowHeight) {
    jbyte *bytes = env->GetByteArrayElements(bytes_, NULL);

    auto imageScheduler = reinterpret_cast<ImageScheduler *>(objPtr);
    imageScheduler->process(bytes, left, top, cropWidth, cropHeight, rowWidth, rowHeight);

    env->ReleaseByteArrayElements(bytes_, bytes, 0);
    return -1;
}

extern "C"
JNIEXPORT jint JNICALL
Java_me_devilsen_czxing_code_NativeSdk_readBarcode(JNIEnv *env, jobject instance, jlong objPtr,
                                                   jobject bitmap, jint left, jint top, jint width,
                                                   jint height, jobjectArray result) {

    try {
        auto imageScheduler = reinterpret_cast<ImageScheduler *>(objPtr);
        auto readResult = imageScheduler->readBitmap(bitmap, left, top, width, height);
        if (readResult.isValid()) {
            env->SetObjectArrayElement(result, 0, ToJavaString(env, readResult.text()));
            if (!readResult.resultPoints().empty()) {
                env->SetObjectArrayElement(result, 1, ToJavaArray(env, readResult.resultPoints()));
            }
            return static_cast<int>(readResult.format());
        }
    } catch (const std::exception &e) {
        ThrowJavaException(env, e.what());
    } catch (...) {
        ThrowJavaException(env, "Unknown exception");
    }
    return -1;
}