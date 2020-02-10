//
// Created by Devilsen on 2019-08-09.
//

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc/types_c.h>
#include "ImageScheduler.h"
#include "JNIUtils.h"

int DEFAULT_MIN_LIGHT = 70;
int SCAN_TYPE_GRAY = 0;
int SCAN_TYPE_THRESHOLD = 1;
int SCAN_TYPE_ZBAR = 2;
int SCAN_TYPE_ADAPTIVE = 3;
int SCAN_TYPE_CUSTOMIZE = 4;

ImageScheduler::ImageScheduler(JNIEnv *env, JavaCallHelper *javaCallHelper) {
    this->env = env;
    this->javaCallHelper = javaCallHelper;
    qrCodeRecognizer = new QRCodeRecognizer();
    stopProcessing.store(false);
    isProcessing.store(false);
}

ImageScheduler::~ImageScheduler() {
    DELETE(env);
    DELETE(javaCallHelper);
    DELETE(qrCodeRecognizer);
    frameQueue.clear();
    delete &isProcessing;
    delete &stopProcessing;
    delete &cameraLight;
    delete &prepareThread;
    scanIndex = 0;
}

void *prepareMethod(void *arg) {
    auto scheduler = static_cast<ImageScheduler *>(arg);
    scheduler->start();
    return nullptr;
}

void ImageScheduler::prepare() {
    pthread_create(&prepareThread, nullptr, prepareMethod, this);
}

void ImageScheduler::start() {
    stopProcessing.store(false);
    isProcessing.store(false);
    frameQueue.setWork(1);
    scanIndex = -1;

    while (true) {
        if (stopProcessing.load()) {
            break;
        }

        if (isProcessing.load()) {
            continue;
        }

        FrameData frameData;
        int ret = frameQueue.deQueue(frameData);
        if (ret) {
            isProcessing.store(true);
            preTreatMat(frameData);
            isProcessing.store(false);
        }
    }
}

void ImageScheduler::stop() {
    isProcessing.store(false);
    stopProcessing.store(true);
    frameQueue.setWork(0);
    frameQueue.clear();
    scanIndex = 0;
}

void
ImageScheduler::process(jbyte *bytes, int left, int top, int cropWidth, int cropHeight,
                        int rowWidth,
                        int rowHeight) {
    if (isProcessing.load()) {
        return;
    }

    FrameData frameData;
    frameData.left = left;
    frameData.top = top;
    frameData.cropWidth = cropWidth;
    if (top + cropHeight > rowHeight) {
        frameData.cropHeight = rowHeight - top;
    } else {
        frameData.cropHeight = cropHeight;
    }
    if (frameData.cropHeight < frameData.cropWidth) {
        frameData.cropWidth = frameData.cropHeight;
    }
    frameData.rowWidth = rowWidth;
    frameData.rowHeight = rowHeight;
    frameData.bytes = bytes;

    frameQueue.enQueue(frameData);
//    LOGE("frame data size : %d", frameQueue.size());
}

/**
 * 预处理二进制数据
 */
void ImageScheduler::preTreatMat(const FrameData &frameData) {
    try {
        scanIndex++;
        LOGE("start preTreatMat..., scanIndex = %d", scanIndex);

        Mat src(frameData.rowHeight + frameData.rowHeight / 2,
                frameData.rowWidth, CV_8UC1,
                frameData.bytes);

        Mat gray;
        cvtColor(src, gray, COLOR_YUV2GRAY_NV21);

        if (frameData.left != 0) {
            gray = gray(
                    Rect(frameData.left, frameData.top, frameData.cropWidth, frameData.cropHeight));
        }

        // 分析亮度，如果亮度过低，不进行处理
        analysisBrightness(gray);
        if (cameraLight < 30) {
            return;
        }

        decodeGrayPixels(gray);
    } catch (const std::exception &e) {
        LOGE("preTreatMat error...");
    }
}

/**
 * 1.1
 * 直接解析gray后的mat
 * 顺时针旋转90度图片，得到正常的图片（Android的后置摄像头获取的格式是横着的，需要旋转90度）
 * @param gray
 */
void ImageScheduler::decodeGrayPixels(const Mat &gray) {
    LOGE("start GrayPixels...");

    Mat mat;
    rotate(gray, mat, ROTATE_90_CLOCKWISE);

//    string result = decodePixels(mat);
//
//    if (!result.empty()) {
//        javaCallHelper->onResult(result, SCAN_TYPE_GRAY);
//        LOGE("ZXing GrayPixels Success, scanIndex = %d", scanIndex);
//    }

    wordSpotter = (cv::text::OCRHolisticWordRecognizer::create("dictnet_vgg_deploy.prototxt", "dictnet_vgg.caffemodel", "dictnet_vgg_labels.txt"));
    dst1 = src.clone();
    for (size_t i = 0; i < textBoxes.size(); i++)
    {
        cv::Mat wordImg;
        cv::cvtColor(src(textBoxes[i]), wordImg, cv::COLOR_BGR2GRAY);
        std::string word;
        std::vector<float> confs;
        wordSpotter->run(wordImg, word, NULL, NULL, &confs);//检测
        cv::Rect currrentBox = textBoxes[i];
        cv::rectangle(dst1, currrentBox, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
        int baseLine = 0;
        cv::Size labelSize = cv::getTextSize(word, cv::FONT_HERSHEY_PLAIN, 1, 1, &baseLine);
        int yLeftBottom = currrentBox.y>labelSize.height? currrentBox.y: labelSize.height;
        cv::rectangle(dst1, cv::Point(currrentBox.x, yLeftBottom - labelSize.height),
                      cv::Point(currrentBox.x + labelSize.width, yLeftBottom + baseLine), cv::Scalar(255, 255, 255), cv::FILLED);
        cv::putText(dst1, word, cv::Point(currrentBox.x, yLeftBottom), cv::FONT_HERSHEY_PLAIN, 1, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
    }
    cv::imshow("Text recognition", dst1);
}

/**
 * 1.2
 * 如果gray化没有解析出来，尝试提升亮度，处理图片亮度过低时的情况
 * 并进行二值化处理，让二维码更清晰
 * 同时旋转180度
 * @param gray
 */
void ImageScheduler::decodeThresholdPixels(const Mat &gray) {
    LOGE("start ThresholdPixels...");

    Mat mat;
    rotate(gray, mat, ROTATE_180);

    // 提升亮度
    if (cameraLight < 80) {
        mat.convertTo(mat, -1, 1.0, 30);
    }

    threshold(mat, mat, 50, 255, CV_THRESH_OTSU);

//    string result = decodePixels(mat);
//    if (!result.empty()) {
//        LOGE("ZXing Threshold Success, scanIndex = %d", scanIndex);
//        javaCallHelper->onResult(result, SCAN_TYPE_THRESHOLD);
//    }
}

/**
 * 2.2 降低图片亮度，再次识别图像，处理亮度过高时的情况
 * 逆时针旋转90度，即旋转了270度
 * @param gray
 */
void ImageScheduler::decodeAdaptivePixels(const Mat &gray) {
    if (scanIndex % 3 != 0) {
        return;
    }
    LOGE("start AdaptivePixels...");

    Mat mat;
    rotate(gray, mat, ROTATE_90_COUNTERCLOCKWISE);

    // 降低图片亮度
    Mat lightMat;
    mat.convertTo(lightMat, -1, 1.0, -60);

    adaptiveThreshold(lightMat, lightMat, 255, ADAPTIVE_THRESH_MEAN_C,
                      THRESH_BINARY, 55, 3);

//    Result result = decodePixels(lightMat);
//    if (result.isValid()) {
//        LOGE("ZXing Adaptive Success, scanIndex = %d", scanIndex);
//        javaCallHelper->onResult(result, SCAN_TYPE_ADAPTIVE);
//    }
}

bool ImageScheduler::analysisBrightness(const Mat &gray) {
    LOGE("start analysisBrightness...");

    // 平均亮度
    Scalar scalar = mean(gray);
    cameraLight = scalar.val[0];
    LOGE("平均亮度 %lf", cameraLight);
    // 判断在时间范围 AMBIENT_BRIGHTNESS_WAIT_SCAN_TIME * lightSize 内是不是亮度过暗
    bool isDark = cameraLight < DEFAULT_MIN_LIGHT;
    javaCallHelper->onBrightness(isDark);

    return isDark;
}