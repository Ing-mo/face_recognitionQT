QT       += core gui widgets

# 目标可执行文件名
TARGET = video_face_recognition_qt

# 使用 C++11 标准
CONFIG += c++11

# 定义源代码
# .cpp 文件只应该在 SOURCES 中出现
# .c 文件也应该在 SOURCES 中出现
SOURCES += \
    main.cpp \
    mainwindow.cpp \
    videoprocessor.cpp \
    video_manager.c \
    face_detector.cpp \
    face_recognizer.cpp

# 定义头文件
# .h 文件只应该在 HEADERS 中出现
HEADERS += \
    mainwindow.h \
    videoprocessor.h \
    video_manager.h \
    face_detector.h \
    face_recognizer.h

FORMS += \
    mainwindow.ui

# ======== 交叉编译和库配置 ==========
# 引用你 Makefile 中的路径
OPENCV_INSTALL_PATH = /home/book/opencv_for_imx6ull/install_opencv

# 手动指定头文件和库路径，这比 pkg-config 更可靠
INCLUDEPATH += $$OPENCV_INSTALL_PATH/include/opencv4
INCLUDEPATH += /home/book/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/arm-buildroot-linux-gnueabihf/sysroot/usr/include

# LIBS 配置是解决 "undefined reference" 错误的关键
# -L 指定库的搜索路径
# -l 指定要链接的具体库
LIBS += -L$$OPENCV_INSTALL_PATH/lib \
        -lopencv_core \
        -lopencv_imgproc \
        -lopencv_imgcodecs \
        -lopencv_dnn \
        -lopencv_objdetect \
        -lopencv_videoio \
        -lopencv_highgui \
        -lopencv_features2d \
        -lopencv_calib3d \ # <--- 添加缺失的模块
        -lopencv_video \    # <--- 添加 video 模块，用于 KalmanFilter
        -lopencv_flann      # <--- 添加 flann 模块，它是 features2d 和 calib3d 的依赖
# 添加其他依赖库
LIBS += -lpthread -ljpeg -ldl -lrt

# 针对交叉编译环境的配置 (可选，但在Qt Creator Kit中配置更好)
unix {
    # 如果你的交叉编译器不在系统 PATH 中，需要指定
    # QMAKE_CC = arm-buildroot-linux-gnueabihf-gcc
    # QMAKE_CXX = arm-buildroot-linux-gnueabihf-g++
    # QMAKE_LINK = arm-buildroot-linux-gnueabihf-g++
    # QMAKE_AR = arm-buildroot-linux-gnueabihf-ar cqs
    # QMAKE_STRIP = arm-buildroot-linux-gnueabihf-strip
}
