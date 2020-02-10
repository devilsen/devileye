/*
* Copyright 2016 Nu-book Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#include "JNIUtils.h"

#include <android/bitmap.h>
#include <stdexcept>
#include <vector>
#include <opencv2/core/types.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>

namespace {

    struct AutoUnlockPixels {
        JNIEnv *m_env;
        jobject m_bitmap;

        AutoUnlockPixels(JNIEnv *env, jobject bitmap) : m_env(env), m_bitmap(bitmap) {}

        ~AutoUnlockPixels() {
            AndroidBitmap_unlockPixels(m_env, m_bitmap);
        }
    };

} // anonymous

void
BitmapToMat(JNIEnv *env, jobject bitmap, cv::Mat &mat) {
    AndroidBitmapInfo bmInfo;
    AndroidBitmap_getInfo(env, bitmap, &bmInfo);
    cv::Mat &dst = mat;

    void *pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) == ANDROID_BITMAP_RESUT_SUCCESS) {
        AutoUnlockPixels autounlock(env, bitmap);

        if (bmInfo.format == ANDROID_BITMAP_FORMAT_RGBA_8888) {
            LOGE("nBitmapToMat: RGB_8888 -> CV_8UC4");
            cv::Mat tmp(bmInfo.height, bmInfo.width, CV_8UC4, pixels);
            tmp.copyTo(dst);
        } else {
            // info.format == ANDROID_BITMAP_FORMAT_RGB_565
            LOGE("nBitmapToMat: RGB_565 -> CV_8UC4");
            cv::Mat tmp(bmInfo.height, bmInfo.width, CV_8UC4, pixels);
            tmp.copyTo(dst);
        }
    } else {
        throw std::runtime_error("Failed to read bitmap's data");
    }
}

/**
 * string转wstring
 */
std::string UnicodeToANSI(const std::wstring &wstr) {
    std::string ret;
    std::mbstate_t state = {};
    const wchar_t *src = wstr.data();
    size_t len = std::wcsrtombs(nullptr, &src, 0, &state);
    if (static_cast<size_t>(-1) != len) {
        std::unique_ptr<char[]> buff(new char[len + 1]);
        len = std::wcsrtombs(buff.get(), &src, len, &state);
        if (static_cast<size_t>(-1) != len) {
            ret.assign(buff.get(), len);
        }
    }
    return ret;
}

std::wstring ANSIToUnicode(const std::string &str) {
    std::wstring ret;
    std::mbstate_t state = {};
    const char *src = str.data();
    size_t len = std::mbsrtowcs(nullptr, &src, 0, &state);
    if (static_cast<size_t>(-1) != len) {
        std::unique_ptr<wchar_t[]> buff(new wchar_t[len + 1]);
        len = std::mbsrtowcs(buff.get(), &src, len, &state);
        if (static_cast<size_t>(-1) != len) {
            ret.assign(buff.get(), len);
        }
    }
    return ret;
}

void ThrowJavaException(JNIEnv *env, const char *message) {
    static jclass jcls = env->FindClass("java/lang/RuntimeException");
    env->ThrowNew(jcls, message);
}

static bool RequiresSurrogates(uint32_t ucs4) {
    return ucs4 >= 0x10000;
}

static uint16_t HighSurrogate(uint32_t ucs4) {
    return uint16_t((ucs4 >> 10) + 0xd7c0);
}

static uint16_t LowSurrogate(uint32_t ucs4) {
    return uint16_t(ucs4 % 0x400 + 0xdc00);
}

static void Utf32toUtf16(const uint32_t *utf32, size_t length, std::vector<uint16_t> &result) {
    result.clear();
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        uint32_t c = utf32[i];
        if (RequiresSurrogates(c)) {
            result.push_back(HighSurrogate(c));
            result.push_back(LowSurrogate(c));
        } else {
            result.push_back(c);
        }
    }
}

jstring ToJavaString(JNIEnv *env, const std::wstring &str) {
    if (sizeof(wchar_t) == 2) {
        return env->NewString((const jchar *) str.data(), str.size());
    } else {
        std::vector<uint16_t> buffer;
        Utf32toUtf16((const uint32_t *) str.data(), str.size(), buffer);
        return env->NewString((const jchar *) buffer.data(), buffer.size());
    }
}
