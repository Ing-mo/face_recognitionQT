#ifndef FACE_DETECTOR_H
#define FACE_DETECTOR_H

// 为了在C++代码中包含也能正常工作
#ifdef __cplusplus
extern "C" {
#endif

// 定义一个简单的矩形结构体，避免引入OpenCV头文件
typedef struct {
    int x;
    int y;
    int width;
    int height;
} FaceRect;

/**
 * @brief 初始化人脸检测器
 * @param cascade_path LBP/Haar分类器XML文件的路径
 * @return 成功返回0, 失败返回-1
 */
int face_detector_init(const char *cascade_path);

/**
 * @brief 在JPEG图像数据中检测人脸
 * 
 * @param jpeg_buf 指向JPEG数据的指针
 * @param jpeg_size JPEG数据的大小
 * @param detected_faces 指向FaceRect数组的指针，函数会为其分配内存。调用者需要负责free()这个数组。
 * @param max_faces `detected_faces` 数组的最大容量。
 * @return 检测到的人脸数量，如果出错则为-1。
 */
int face_detector_detect(const unsigned char *jpeg_buf, unsigned long jpeg_size, FaceRect **detected_faces);


/**
 * @brief 清理人脸检测器使用的资源
 */
void face_detector_cleanup();

#ifdef __cplusplus
}
#endif

#endif // FACE_DETECTOR_H