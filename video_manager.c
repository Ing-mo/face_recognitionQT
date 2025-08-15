#include "video_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>

VideoCaptureDevice* video_capture_init(const char *device_path, int width, int height, unsigned int format) {
    //  分配并清零设备结构体内存
    VideoCaptureDevice *dev = calloc(1, sizeof(VideoCaptureDevice));
    if (!dev) {
        perror("calloc VideoCaptureDevice");
        return NULL;
    }

    dev->fd = open(device_path, O_RDWR);
    if (dev->fd < 0) {
        fprintf(stderr, "Can't open device %s\n", device_path);
        free(dev);
        return NULL;
    }

    // 查询设备能力集
    struct v4l2_capability cap;
    if (ioctl(dev->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        goto fail;
    }

    //支持视频捕获 (V4L2_CAP_VIDEO_CAPTURE) 和流式I/O (V4L2_CAP_STREAMING)
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) || !(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "Device does not support required capabilities\n");
        goto fail;
    }

    // 设置视频格式
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = format;   
    fmt.fmt.pix.field = V4L2_FIELD_ANY;  
    if (ioctl(dev->fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        goto fail;
    }

    // 申请DMA缓冲区（内存映射方式）
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4; 
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(dev->fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        goto fail;
    }
    dev->buffer_count = req.count;

    // 为缓冲区指针数组和长度数组分配内存
    dev->buffers = calloc(dev->buffer_count, sizeof(void *));
    dev->buffer_lengths = calloc(dev->buffer_count, sizeof(unsigned int));
    if (!dev->buffers || !dev->buffer_lengths) {
        perror("calloc for buffers");
        goto fail;
    }
    // 查询每个缓冲区的物理信息并进行内存映射
    for (int i = 0; i < dev->buffer_count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(dev->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            goto fail;
        }

        dev->buffer_lengths[i] = buf.length;
        dev->buffers[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, buf.m.offset);
        if (dev->buffers[i] == MAP_FAILED) {
            perror("mmap");
            goto fail;
        }
    }

    // 将所有缓冲区放入队列 (VIDIOC_QBUF)，准备接收数据
    for (int i = 0; i < dev->buffer_count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(dev->fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            goto fail;
        }
    }

    // 启动视频流
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(dev->fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        goto fail;
    }

    printf("Video capture initialized successfully.\n");
    return dev;

fail:
    video_capture_cleanup(dev);
    return NULL;
}

VideoFrame* video_capture_get_frame(VideoCaptureDevice *dev) {
    struct pollfd fds[1];
    fds[0].fd = dev->fd;
    fds[0].events = POLLIN;  

    int ret = poll(fds, 1, 5000);  
    if (ret <= 0) {
        perror("poll");
        return NULL;
    }
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(dev->fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("VIDIOC_DQBUF");
        return NULL;
    }
    // 创建一个 VideoFrame 结构体来包装返回的数据
    VideoFrame *frame = malloc(sizeof(VideoFrame));
    if (!frame) {
        perror("malloc VideoFrame");
        ioctl(dev->fd, VIDIOC_QBUF, &buf);
        return NULL;
    }

    // 填充 VideoFrame 结构体
    frame->start = dev->buffers[buf.index];  
    frame->length = buf.bytesused;           
    frame->index = buf.index;                
    return frame;
}

int video_capture_release_frame(VideoCaptureDevice *dev, VideoFrame *frame) {
    if (!dev || !frame) return -1;

    // 准备将缓冲区重新放回队列 (VIDIOC_QBUF)
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = frame->index;

    free(frame); 

    // 将缓冲区重新入队，以便驱动可以再次向其中填充数据
    if (ioctl(dev->fd, VIDIOC_QBUF, &buf) < 0) {
        perror("VIDIOC_QBUF (requeue)");
        return -1;
    }
    return 0;
}

void video_capture_cleanup(VideoCaptureDevice *dev) {
    if (!dev) return;
    
    // 停止视频流 (VIDIOC_STREAMOFF)
    if (dev->fd >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(dev->fd, VIDIOC_STREAMOFF, &type); 
    }
    // 解除内存映射 (munmap)
    if (dev->buffers) {
        for (int i = 0; i < dev->buffer_count; i++) {
            if (dev->buffers[i] && dev->buffers[i] != MAP_FAILED) {
                munmap(dev->buffers[i], dev->buffer_lengths[i]);
            }
        }
        free(dev->buffers);
    }

    // 释放缓冲区长度数组
    if (dev->buffer_lengths) {
        free(dev->buffer_lengths);
    }

    if (dev->fd >= 0) {
        close(dev->fd);
    }

    free(dev);
    printf("Video capture cleaned up.\n");
}
