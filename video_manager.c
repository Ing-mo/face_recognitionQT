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

    struct v4l2_capability cap;
    if (ioctl(dev->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        goto fail;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) || !(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "Device does not support required capabilities\n");
        goto fail;
    }

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

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4; // 请求4个缓冲区足够了
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(dev->fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        goto fail;
    }
    dev->buffer_count = req.count;
    dev->buffers = calloc(dev->buffer_count, sizeof(void *));
    dev->buffer_lengths = calloc(dev->buffer_count, sizeof(unsigned int));
    if (!dev->buffers || !dev->buffer_lengths) {
        perror("calloc for buffers");
        goto fail;
    }

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
    //printf("DEBUG: Waiting for frame (poll)...\n"); // <--- 新增
    //fflush(stdout); // <--- 新增，确保信息立刻打印出来
    struct pollfd fds[1];
    fds[0].fd = dev->fd;
    fds[0].events = POLLIN;

    int ret = poll(fds, 1, 5000); // 5秒超时
    if (ret <= 0) {
        perror("poll");
        if (ret == 0) {
            //printf("DEBUG: poll timed out after 5 seconds.\n"); // <--- 新增
        }
        return NULL;
    }

    //printf("DEBUG: poll returned, frame is ready. Dequeuing buffer...\n"); // <--- 新增
    //fflush(stdout);

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(dev->fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("VIDIOC_DQBUF");
        return NULL;
    }

    VideoFrame *frame = malloc(sizeof(VideoFrame));
    if (!frame) {
        perror("malloc VideoFrame");
        // 致命错误，无法恢复，但至少要把dequeued的buffer还回去
        ioctl(dev->fd, VIDIOC_QBUF, &buf);
        return NULL;
    }
    
    frame->start = dev->buffers[buf.index];
    frame->length = buf.bytesused;
    frame->index = buf.index;

    return frame;
}

int video_capture_release_frame(VideoCaptureDevice *dev, VideoFrame *frame) {
    if (!dev || !frame) return -1;

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = frame->index;

    free(frame); // 释放 VideoFrame 结构体本身

    if (ioctl(dev->fd, VIDIOC_QBUF, &buf) < 0) {
        perror("VIDIOC_QBUF (requeue)");
        return -1;
    }
    return 0;
}

void video_capture_cleanup(VideoCaptureDevice *dev) {
    if (!dev) return;

    if (dev->fd >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(dev->fd, VIDIOC_STREAMOFF, &type); // 忽略错误
    }

    if (dev->buffers) {
        for (int i = 0; i < dev->buffer_count; i++) {
            if (dev->buffers[i] && dev->buffers[i] != MAP_FAILED) {
                munmap(dev->buffers[i], dev->buffer_lengths[i]);
            }
        }
        free(dev->buffers);
    }
    
    if (dev->buffer_lengths) {
        free(dev->buffer_lengths);
    }

    if (dev->fd >= 0) {
        close(dev->fd);
    }

    free(dev);
    printf("Video capture cleaned up.\n");
}
