#include "videoprocessor.h"
#include <QDebug>
#include <QThread>
#include <QDir>
#include <QDateTime>
#include <sys/ioctl.h>
#include <vector>
#include <opencv2/opencv.hpp>
#include <cstdio>

// 嵌入式优化性能参数
#define TRACKER_LIFESPAN 30
#define MAX_TRACKERS 5
#define RECOGNITION_INTERVAL 12
#define DETECTION_INTERVAL 3
#define IOU_MATCH_THRESHOLD 0.3f

// 新增的注册流程常量
const int REGISTRATION_PHOTO_COUNT = 10;
const int REGISTRATION_CAPTURE_INTERVAL_FRAMES = 10; // 每10帧（约1秒）捕获一次
const QString PHOTO_SAVE_PATH = "/root/photos/";
const QString REG_TEMP_PATH = "/root/reg_temp/";


VideoProcessor::VideoProcessor(QObject *parent) : QObject(parent)
{
    const char *cascade_file = "/root/lbpcascade_frontalface.xml";
    const char *onnx_model_file = "/root/models/mobilefacenet.onnx";
    const char *database_file = "/root/face_database.db";

    if (face_detector_init(cascade_file) != 0) {
        qCritical() << "错误: 人脸检测器初始化失败!";
        return;
    }
    qDebug() << "人脸检测器初始化成功。";

    if (face_recognizer_init(onnx_model_file, database_file) != 0) {
        face_detector_cleanup();
        qCritical() << "错误: 人脸识别器初始化失败!";
        return;
    }
    qDebug() << "人脸识别器初始化成功。";

    // 保留示例注册，以确保数据库中有数据可供识别
    const char* person1_image_paths[] = { "/root/face_database/yy/001.jpg", "/root/face_database/yy/002.jpg", "/root/face_database/yy/003.jpg", "/root/face_database/yy/004.jpg", "/root/face_database/yy/005.jpg", "/root/face_database/yy/006.jpg", "/root/face_database/yy/007.jpg", "/root/face_database/yy/008.jpg", "/root/face_database/yy/009.jpg", "/root/face_database/yy/010.jpg" };
    int num_images = sizeof(person1_image_paths) / sizeof(person1_image_paths[0]);
    if (num_images >= 3) {
        int count = face_recognizer_register_faces_from_paths(person1_image_paths, num_images, "yy");
        if(count > 0) qDebug() << "示例用户 'yy' 已注册。";
    }

    m_trackers.resize(MAX_TRACKERS);

    // 确保目录存在
    QDir().mkpath(PHOTO_SAVE_PATH);
    QDir().mkpath(REG_TEMP_PATH);
}

VideoProcessor::~VideoProcessor()
{
    if (m_cam) {
        video_capture_cleanup(m_cam);
    }
    face_recognizer_cleanup();
    face_detector_cleanup();
    qDebug() << "VideoProcessor cleaned up.";
}

void VideoProcessor::process()
{
    m_cam = video_capture_init("/dev/video1", 640, 480, V4L2_PIX_FMT_MJPEG);
    if (!m_cam) {
        emit statusMessage("摄像头初始化失败!");
        qCritical() << "错误: 无法打开摄像头。";
        return;
    }

    emit statusMessage("视频流已启动...");
    qDebug() << "摄像头已成功启动。";

    int frame_count = 0;

    while (!m_stopped) {
        VideoFrame *frame = video_capture_get_frame(m_cam);
        if (!frame) {
             qDebug() << "DEBUG: Loop" << frame_count << "- Failed to get frame, continuing.";
             continue;
        }

        // 保存当前帧的JPEG数据，供拍照和注册使用
        m_lastFrameJpeg = QByteArray((const char*)frame->start, frame->length);

        std::vector<FaceRect> detected_faces;
        // 定期进行人脸检测，无论是否在注册模式下都需要
        if (frame_count % DETECTION_INTERVAL == 0) {
            FaceRect *p = nullptr; int n = face_detector_detect((unsigned char*)frame->start, frame->length, &p);
            if (n > 0) { detected_faces.assign(p, p + n); } if(p) free(p);
        }

        // 检查是否处于注册模式
        if (m_registrationMode.load()) {
            handleRegistration(frame, detected_faces);
            video_capture_release_frame(m_cam, frame);
            frame_count++;
            QThread::msleep(50);
            continue; // 跳过正常的追踪和识别流程
        }

        // --- 正常的追踪和识别流程 ---

        // 1. 预测
        for (auto& tracker : m_trackers) { if (tracker.active) { cv::Mat p = tracker.kf.predict(); tracker.rect = {int(p.at<float>(0)-p.at<float>(2)/2), int(p.at<float>(1)-p.at<float>(3)/2), int(p.at<float>(2)), int(p.at<float>(3))}; } }

        // 2. 关联、更新与创建 (使用上面已检测到的人脸)
        if (!detected_faces.empty()) {
            std::vector<bool> used(detected_faces.size(), false);
            for (auto& t : m_trackers) {
                if (!t.active) continue;
                float best_iou = 0; int best_idx = -1;
                for (size_t i=0; i<detected_faces.size(); ++i) { if (used[i]) continue; float iou = calculate_iou(t.rect, detected_faces[i]); if (iou > best_iou) { best_iou = iou; best_idx = i; } }
                if (best_iou > IOU_MATCH_THRESHOLD) {
                    const auto& r = detected_faces[best_idx]; cv::Mat m = (cv::Mat_<float>(4,1) << r.x+r.width/2.0f, r.y+r.height/2.0f, r.width, r.height); t.kf.correct(m); t.lifespan = TRACKER_LIFESPAN; used[best_idx] = true;
                } else { t.lifespan--; }
            }
            for (size_t i=0; i<detected_faces.size(); ++i) { if (!used[i]) { for (auto& t : m_trackers) { if (!t.active) { initKalmanFilter(t.kf, detected_faces[i]); t.active=1; t.rect=detected_faces[i]; strncpy(t.name,"Tracking...",63); t.score=0; t.lifespan=TRACKER_LIFESPAN; t.id=m_nextTrackerId++; qDebug()<<"新追踪器 #"<<t.id; break; }}}}
        } else { for (auto& t : m_trackers) { if (t.active) t.lifespan--; } }

        // 3. 识别
        if (frame_count % RECOGNITION_INTERVAL == 0 && !detected_faces.empty()) { face_recognizer_submit_task((const unsigned char*)frame->start, frame->length, detected_faces.data(), detected_faces.size()); }

        // 4. 获取结果
        RecognitionResult *res=nullptr; int n_res=face_recognizer_get_results(&res);
        if (n_res > 0) {
            for (int i=0; i<n_res; ++i) {
                const auto& r = res[i]; float best_iou = 0; FaceTracker* best_t = nullptr;
                for (auto& t : m_trackers) { if (t.active) { float iou = calculate_iou(t.rect, r.rect); if (iou > best_iou) { best_iou = iou; best_t = &t; } } }
                if (best_iou > IOU_MATCH_THRESHOLD && strcmp(r.name,"Unknown") != 0) {
                    strncpy(best_t->name, r.name, 63);
                    best_t->score = r.score;
                    best_t->lifespan = TRACKER_LIFESPAN; // <-- [关键修复] 识别成功，重置追踪器生命！
                    qDebug()<<"识别成功: "<<r.name << "(Tracker #" << best_t->id << " refreshed)";
                }
            }
            free(res);
        }

        // 5. 准备UI数据
        QList<RecognitionResult> final_results; QString status="正在监控...";
        for (auto& t : m_trackers) {
            if (t.active && t.lifespan > 0) { RecognitionResult r; r.rect=t.rect; strncpy(r.name,t.name,63); r.score=t.score; final_results.append(r); if(strcmp(t.name,"Tracking...")!=0 && strcmp(t.name,"Unknown")!=0) status=QString("检测到: %1").arg(t.name); }
            else { if(t.active) { qDebug()<<"追踪器 #"<<t.id<<" 丢失"; } t.active = 0; }
        }

        // 6. 发射信号 (使用已缓存的JPEG数据)
        emit frameProcessed(m_lastFrameJpeg, final_results);
        emit statusMessage(status);

        video_capture_release_frame(m_cam, frame);
        frame_count++;
        QThread::msleep(100);
    }
    qDebug() << "视频处理循环已退出。";
}

void VideoProcessor::takePhoto()
{
    if (m_lastFrameJpeg.isEmpty()) {
        emit statusMessage("拍照失败: 无有效图像");
        return;
    }

    QString fileName = PHOTO_SAVE_PATH + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".jpg";
    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(m_lastFrameJpeg);
        file.close();
        emit statusMessage(QString("照片已保存: %1").arg(QDir(fileName).dirName()));
        qDebug() << "Photo saved to" << fileName;
    } else {
        emit statusMessage("拍照失败: 无法写入文件");
        qWarning() << "Failed to save photo to" << fileName;
    }
}

void VideoProcessor::startRegistration(const QString &name)
{
    if (m_registrationMode.load()) {
        emit statusMessage("错误: 正在进行另一个注册任务");
        return;
    }

    // 清理之前的临时文件
    QDir tempDir(REG_TEMP_PATH);
    tempDir.removeRecursively();
    tempDir.mkpath(".");

    m_registrationName = name;
    m_photosToTake = REGISTRATION_PHOTO_COUNT;
    m_takenPhotoPaths.clear();
    m_regCaptureInterval = 0;
    m_registrationMode = true; // 原子操作，启动注册模式

    emit statusMessage(QString("注册 '%1': 请正对摄像头 (0/%2)").arg(name).arg(m_photosToTake));
    qDebug() << "Starting registration for" << name;
}

void VideoProcessor::clearDatabase()
{
    if (face_recognizer_clear_database() == 0) {
        emit statusMessage("数据库已清空");
        qDebug() << "Face database cleared successfully.";
    } else {
        emit statusMessage("错误: 清空数据库失败");
        qWarning() << "Failed to clear face database.";
    }
}

void VideoProcessor::handleRegistration(VideoFrame *frame, const std::vector<FaceRect> &detected_faces)
{
    // 在注册期间，也需要将帧画面发给UI，让用户看到自己
    QList<RecognitionResult> ui_results;
    if(!detected_faces.empty()){
        RecognitionResult r;
        r.rect = detected_faces[0]; // 只画第一个检测到的人脸框
        snprintf(r.name, sizeof(r.name), "Positioning...");
        r.score = 0;
        ui_results.append(r);
    }
    emit frameProcessed(m_lastFrameJpeg, ui_results);

    m_regCaptureInterval++;
    // 采集条件：检测到一张脸 且 达到了捕获间隔
    if (detected_faces.size() == 1 && m_regCaptureInterval >= REGISTRATION_CAPTURE_INTERVAL_FRAMES) {
        m_regCaptureInterval = 0; // 重置计数器

        int photo_num = m_takenPhotoPaths.size() + 1;
        QString filePath = REG_TEMP_PATH + QString("%1.jpg").arg(photo_num, 3, 10, QChar('0'));

        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(m_lastFrameJpeg);
            file.close();
            m_takenPhotoPaths.append(filePath);
            qDebug() << "Registration photo taken:" << filePath;
            emit statusMessage(QString("注册 '%1': 请保持姿势 (%2/%3)")
                                   .arg(m_registrationName)
                                   .arg(photo_num)
                                   .arg(m_photosToTake));
        }

        // 检查是否已采集足够照片
        if (m_takenPhotoPaths.size() >= m_photosToTake) {
            emit statusMessage(QString("正在处理 '%1' 的照片...").arg(m_registrationName));

            // 准备C-API需要的 const char* 数组
            std::vector<const char*> c_paths;
            std::vector<QByteArray> c_paths_storage; // 确保字符串在调用期间存活
            for(const QString& p : m_takenPhotoPaths) {
                c_paths_storage.push_back(p.toUtf8());
                c_paths.push_back(c_paths_storage.back().constData());
            }

            // 调用注册函数
            int registered_count = face_recognizer_register_faces_from_paths(
                c_paths.data(), c_paths.size(), m_registrationName.toUtf8().constData());

            if (registered_count > 0) {
                 qDebug() << "Successfully registered" << m_registrationName;
                 emit statusMessage(QString("'%1' 注册成功!").arg(m_registrationName));
                 cleanupRegistration(true);
            } else {
                 qWarning() << "Failed to register" << m_registrationName;
                 emit statusMessage(QString("'%1' 注册失败，请重试").arg(m_registrationName));
                 cleanupRegistration(false);
            }
        }
    }
}

void VideoProcessor::cleanupRegistration(bool success)
{
    // 清理临时文件
    QDir tempDir(REG_TEMP_PATH);
    tempDir.removeRecursively();

    m_registrationMode = false; // 退出注册模式
}


// --- 保持不变的辅助函数 ---
void VideoProcessor::stop() { m_stopped = true; }
void VideoProcessor::setBrightness(int v) { if(!m_cam) return; struct v4l2_control ctl; ctl.id = V4L2_CID_BRIGHTNESS; ctl.value=v; if(ioctl(m_cam->fd, VIDIOC_S_CTRL, &ctl)<0) qWarning("设置亮度失败"); }
void VideoProcessor::initKalmanFilter(cv::KalmanFilter& kf, const FaceRect& r) { kf.init(8,4,0,CV_32F); kf.transitionMatrix=(cv::Mat_<float>(8,8)<<1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1); kf.measurementMatrix=cv::Mat::eye(4,8,CV_32F); cv::setIdentity(kf.processNoiseCov,cv::Scalar::all(1e-2)); cv::setIdentity(kf.measurementNoiseCov,cv::Scalar::all(1e-1)); cv::setIdentity(kf.errorCovPost,cv::Scalar::all(1)); kf.statePost.at<float>(0)=r.x+r.width/2.f; kf.statePost.at<float>(1)=r.y+r.height/2.f; kf.statePost.at<float>(2)=r.width; kf.statePost.at<float>(3)=r.height; kf.statePost.at<float>(4)=0; kf.statePost.at<float>(5)=0; kf.statePost.at<float>(6)=0; kf.statePost.at<float>(7)=0; }
float VideoProcessor::calculate_iou(const FaceRect& r1, const FaceRect& r2) { int x1=std::max(r1.x,r2.x), y1=std::max(r1.y,r2.y), x2=std::min(r1.x+r1.width, r2.x+r2.width), y2=std::min(r1.y+r1.height, r2.y+r2.height); int w=std::max(0,x2-x1), h=std::max(0,y2-y1); int inter=w*h; int unio=r1.width*r1.height+r2.width*r2.height-inter; return unio>0? (float)inter/unio : 0.0f; }
