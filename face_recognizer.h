#ifndef FACE_RECOGNIZER_H
#define FACE_RECOGNIZER_H

#include "face_detector.h" 

#ifdef __cplusplus
extern "C" {
#endif

// 用于保存单条识别结果的结构体
typedef struct {
    FaceRect rect;
    char name[64]; // 识别出的人名
    float score;   // 置信度分数
} RecognitionResult;

/**
 * @brief 初始化人脸识别引擎。
 * 加载ONNX模型，从文件加载人脸数据库，并启动后台识别工作线程。
 * @param model_path ONNX 模型的路径。
 * @param db_path 人脸数据库文件的路径。
 * @return 成功返回0，失败返回-1。
 */
int face_recognizer_init(const char *model_path, const char* db_path);

/**
 * @brief 从多个图像文件路径注册一张人脸，以提高鲁棒性。
 * @param image_paths 一个包含多个JPEG文件路径的字符串数组。
 * @param num_images 数组中的路径数量。
 * @param name 要与这些图像关联的名字。
 * @return 成功注册的图片数量，如果一张都未成功则返回0或-1。
 */
int face_recognizer_register_faces_from_paths(const char* const* image_paths, int num_images, const char* name);

/**
 * @brief 清理人脸识别器使用的所有资源。
 * 停止工作线程并释放内存。
 */
void face_recognizer_cleanup();

/**
 * @brief 异步提交一个识别任务。
 * 这个函数是非阻塞的，它会把任务放入一个队列中，由后台线程处理。
 * @param jpeg_buf 指向JPEG图像数据的指针。
 * @param jpeg_size JPEG数据的大小。
 * @param faces 在该图像中已检测到的人脸矩形数组。
 * @param num_faces 矩形数组中的人脸数量。
 * @return 成功将任务入队返回0，如果队列已满或出错则返回-1。
 */
int face_recognizer_submit_task(const unsigned char *jpeg_buf, unsigned long jpeg_size, const FaceRect *faces, int num_faces);

/**
 * @brief 尝试获取一批已完成的识别结果。
 * 这个函数是非阻塞的。
 * @param results 指向 RecognitionResult 数组的指针。如果成功获取，函数会为该数组分配内存。
 *                调用者在使用完毕后必须负责 free(*results)。
 * @return > 0: 成功获取到的结果数量。
 *         = 0: 当前没有可用的结果。
 *         < 0: 发生错误。
 */
int face_recognizer_get_results(RecognitionResult **results);

/**
 * @brief 清理人脸识别器使用的所有资源。
 * 停止工作线程并释放内存。
 */
void face_recognizer_cleanup();

/**
 * @brief 清空所有已注册的人脸数据。
 * 这会清空内存中的数据库，并用一个空数据库覆盖磁盘上的文件。
 * @return 成功返回0，失败返回-1。
 */
int face_recognizer_clear_database();

#ifdef __cplusplus
}
#endif

#endif // FACE_RECOGNIZER_H
