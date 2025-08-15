#include "face_detector.h"
#include <opencv2/opencv.hpp>
#include <vector>

#include <cstdio>   
#include <cstdlib>  
#include <cstring>  
#include <cerrno>   

// 使用静态变量来保存分类器，避免每次都加载耗时的XML模型文件
static cv::CascadeClassifier face_cascade;

extern "C" {

int face_detector_init(const char *cascade_path) {
    if (!face_cascade.load(cascade_path)) {
        fprintf(stderr, "Error loading face cascade from %s\n", cascade_path);
        return -1;
    }
    printf("Face detector initialized with LBP cascade.\n");
    return 0;
}

int face_detector_detect(const unsigned char *jpeg_buf, unsigned long jpeg_size, FaceRect **detected_faces) {
    if (jpeg_buf == NULL || jpeg_size == 0) {
        return -1;
    }

    // 将JPEG内存缓冲区解码为OpenCV的Mat对象
    std::vector<unsigned char> jpeg_vec(jpeg_buf, jpeg_buf + jpeg_size);
    cv::Mat frame = cv::imdecode(jpeg_vec, cv::IMREAD_COLOR);
    if (frame.empty()) {
        fprintf(stderr, "Failed to decode JPEG image\n");
        return -1;
    }

    // 转换为灰度图以提高检测速度
    cv::Mat gray_frame;
    cv::cvtColor(frame, gray_frame, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray_frame, gray_frame); // 直方图均衡化

    // 检测人脸
    std::vector<cv::Rect> faces;  
    face_cascade.detectMultiScale(gray_frame, faces, 1.1, 5, 0, cv::Size(100,100));


    int num_faces = faces.size();
    // 如果检测到了人脸，为C接口的输出参数分配内存。
    if (num_faces > 0) {
        *detected_faces = (FaceRect *)malloc(num_faces * sizeof(FaceRect));
        if (*detected_faces == NULL) {
            perror("malloc for detected_faces");
            return -1;
        }
        // 遍历OpenCV的检测结果，并将其复制到我们自己的C风格的FaceRect结构体数组中。
        for (int i = 0; i < num_faces; i++) {
            (*detected_faces)[i].x = faces[i].x;
            (*detected_faces)[i].y = faces[i].y;
            (*detected_faces)[i].width = faces[i].width;
            (*detected_faces)[i].height = faces[i].height;
        }
    } else {
        *detected_faces = NULL;
    }

    return num_faces;
}

void face_detector_cleanup() {
    printf("Face detector cleaned up.\n");
}

} // extern "C"
