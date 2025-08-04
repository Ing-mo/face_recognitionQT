#ifndef VIDEO_CAPTURE_H
#define VIDEO_CAPTURE_H

#include <linux/videodev2.h>

// 用于保存视频设备状态的结构体
typedef struct {
    int fd;
    int buffer_count;
    void **buffers;         // 指向 mmap 映射的缓冲区指针数组
    unsigned int *buffer_lengths; // 每个缓冲区的长度，用于 munmap
} VideoCaptureDevice;

// 用于保存捕获到的单个视频帧信息的结构体
typedef struct {
    void *start;          // 帧数据的起始地址
    unsigned int length;  // 帧数据的实际长度
    unsigned int index;   // 帧在缓冲区队列中的索引
} VideoFrame;

/**
 * @brief 初始化视频捕获设备。
 *
 * 打开设备，设置格式，请求并映射缓冲区，然后启动视频流。
 * @param device_path 视频设备的路径 (例如 "/dev/video0")。
 * @param width 期望的捕获宽度。
 * @param height 期望的捕获高度。
 * @param format 期望的像素格式 (例如 V4L2_PIX_FMT_MJPEG)。
 * @return 成功则返回一个指向 VideoCaptureDevice 结构体的指针，失败返回 NULL。
 */
VideoCaptureDevice* video_capture_init(const char *device_path, int width, int height, unsigned int format);

/**
 * @brief 等待并获取一帧视频数据。
 *
 * @param dev 指向已初始化的 VideoCaptureDevice 结构体的指针。
 * @return 成功则返回一个指向 VideoFrame 结构体的指针，失败返回 NULL。
 *         注意: 返回的 VideoFrame 指针需要在使用后通过 video_capture_release_frame 释放。
 */
VideoFrame* video_capture_get_frame(VideoCaptureDevice *dev);

/**
 * @brief 将一帧视频缓冲区重新入队，并释放 VideoFrame 结构体。
 *
 * @param dev 指向已初始化的 VideoCaptureDevice 结构体的指针。
 * @param frame 指向由 video_capture_get_frame 获取的 VideoFrame 的指针。
 * @return 成功返回0，失败返回-1。
 */
int video_capture_release_frame(VideoCaptureDevice *dev, VideoFrame *frame);

/**
 * @brief 清理并关闭视频捕获设备。
 *
 * 停止视频流，解除缓冲区映射，关闭设备文件描述符，并释放所有相关内存。
 * @param dev 指向由 video_capture_init 创建的 VideoCaptureDevice 结构体的指针。
 */
void video_capture_cleanup(VideoCaptureDevice *dev);

#endif // VIDEO_CAPTURE_H