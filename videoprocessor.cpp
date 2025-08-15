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
#define MAX_TRACKERS 3           
#define RECOGNITION_INTERVAL 15  
#define DETECTION_INTERVAL 5    
#define IOU_MATCH_THRESHOLD 0.3f 
#define FRAME_INTERVAL_MS 100    

// 注册流程常量
const int REGISTRATION_PHOTO_COUNT = 5;                
const int REGISTRATION_CAPTURE_INTERVAL_FRAMES = 10;   
const QString PHOTO_SAVE_PATH = "/root/photos/";
const QString REG_TEMP_PATH = "/root/reg_temp/";

//初始化底层C-API模块。传入模型和数据库文件的硬编码路径，并检查初始化是否成功
VideoProcessor::VideoProcessor(QObject *parent) : QObject(parent)
{
    const char *cascade_file    = "/root/lbpcascade_frontalface.xml"; 
    const char *onnx_model_file = "/root/models/mobilefacenet.onnx";  
    const char *database_file   = "/root/face_database.db";           

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

    // 演示注册
    const char* person1_image_paths[] = { "/root/face_database/yy/001.jpg", "/root/face_database/yy/002.jpg", "/root/face_database/yy/003.jpg", "/root/face_database/yy/004.jpg", "/root/face_database/yy/005.jpg", "/root/face_database/yy/006.jpg", "/root/face_database/yy/007.jpg", "/root/face_database/yy/008.jpg", "/root/face_database/yy/009.jpg", "/root/face_database/yy/010.jpg" };
    int num_images = sizeof(person1_image_paths) / sizeof(person1_image_paths[0]);
    if (num_images >= 3) {
        int count = face_recognizer_register_faces_from_paths(person1_image_paths, num_images, "yy");
        if(count > 0) qDebug() << "示例用户 'yy' 已注册。";
    }

    m_trackers.resize(MAX_TRACKERS);  
    QDir().mkpath(PHOTO_SAVE_PATH);   
    QDir().mkpath(REG_TEMP_PATH);      

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &VideoProcessor::processSingleFrame);
}

VideoProcessor::~VideoProcessor()
{
    // 析构时确保资源被释放
    if (m_cam) {
        video_capture_cleanup(m_cam); 
    }
    face_recognizer_cleanup();
    face_detector_cleanup();
    qDebug() << "VideoProcessor cleaned up.";
}

void VideoProcessor::startProcessing()
{
    if (m_timer->isActive()) {
        qWarning("Processing is already active.");
        return;
    }

    m_cam = video_capture_init("/dev/video1", 640, 480, V4L2_PIX_FMT_MJPEG);
    if (!m_cam) {
        emit statusMessage("摄像头初始化失败!");
        qCritical() << "错误: 无法打开摄像头。";
        return;
    }

    m_stopped = false;       
    m_frameCounter = 0;       
    emit statusMessage("视频流已启动...");
    qDebug() << "摄像头已成功启动，处理定时器开启。";

    m_timer->start(FRAME_INTERVAL_MS);
}

void VideoProcessor::processSingleFrame()
{
    if (m_stopped) {
        if (m_timer->isActive()) {
            m_timer->stop();
            qDebug() << "处理定时器已停止。";
        }
        return;
    }

    VideoFrame *frame = video_capture_get_frame(m_cam);
    if (!frame) {
         qDebug() << "DEBUG: Loop" << m_frameCounter << "- Failed to get frame, continuing.";
         return; 
    }

    m_lastFrameJpeg = QByteArray((const char*)frame->start, frame->length);

    std::vector<FaceRect> detected_faces;
    // 定期进行人脸检测
    if (m_frameCounter % DETECTION_INTERVAL == 0 || m_registrationMode.load()) {
        FaceRect *p = nullptr; int n = face_detector_detect((unsigned char*)frame->start, frame->length, &p);
        if (n > 0) { detected_faces.assign(p, p + n); }　// 从C数组高效构造std::vector
        if(p) free(p);
    }

    if (m_registrationMode.load()) {
        handleRegistration(frame, detected_faces);
    } else {
        // --- 正常的追踪和识别流程 ---
        for (auto& tracker : m_trackers) { 
            // 卡尔曼滤波预测目标位置
            if (tracker.active) { 
                cv::Mat p = tracker.kf.predict(); 
                tracker.rect = {int(p.at<float>(0)-p.at<float>(2)/2), int(p.at<float>(1)-p.at<float>(3)/2), int(p.at<float>(2)), int(p.at<float>(3))};
            } 
        }

        // IOU匹配算法（检测框与跟踪器关联）
        if (!detected_faces.empty()) {
            std::vector<bool> used(detected_faces.size(), false);
            // 优先匹配现有跟踪器
            for (auto& t : m_trackers) { 
                if (!t.active) continue; 
                float best_iou = 0; int best_idx = -1;
                for (size_t i=0; i<detected_faces.size(); ++i) { 
                    if (used[i]) continue;  
                    float iou = calculate_iou(t.rect, detected_faces[i]); 
                    if (iou > best_iou) { 
                        best_iou = iou;
                        best_idx = i; 
                    } 
                }
                if (best_iou > IOU_MATCH_THRESHOLD) { // 更新卡尔曼滤波器
                    const auto& r = detected_faces[best_idx]; 
                    cv::Mat m = (cv::Mat_<float>(4,1) << r.x+r.width/2.0f, r.y+r.height/2.0f, r.width, r.height); 
                    t.kf.correct(m); t.lifespan = TRACKER_LIFESPAN; 
                    used[best_idx] = true;
                } else { 
                    t.lifespan--; 
                }
            }

            // 新目标初始化
            for (size_t i=0; i<detected_faces.size(); ++i) {
                if (!used[i]) {
                    for (auto& t : m_trackers) { 
                        if (!t.active) { 
                            initKalmanFilter(t.kf, detected_faces[i]);
                            t.active=1; t.rect=detected_faces[i]; strncpy(t.name,"Tracking...",63); 
                            t.score=0; t.lifespan=TRACKER_LIFESPAN; t.id=m_nextTrackerId++; 
                            qDebug()<<"新追踪器 #"<<t.id; break; 
                        }
                    }
                }
            }
        } else { 
            for (auto& t : m_trackers) { if (t.active) t.lifespan--; } 
        }

        // 异步任务提交
        if (m_frameCounter % RECOGNITION_INTERVAL == 0 && !detected_faces.empty()) { 
            face_recognizer_submit_task((const unsigned char*)frame->start, frame->length, detected_faces.data(), detected_faces.size()); 
        }

        // 异步结果获取与整合
        RecognitionResult *res=nullptr; int n_res=face_recognizer_get_results(&res);
        if (n_res > 0) {
            for (int i=0; i<n_res; ++i) {
                const auto& r = res[i]; float best_iou = 0; FaceTracker* best_t = nullptr;
                for (auto& t : m_trackers) { 
                    if (t.active) { 
                        float iou = calculate_iou(t.rect, r.rect); 
                        if (iou > best_iou) { best_iou = iou; best_t = &t; } 
                    } 
                }
                if (best_t && best_iou > IOU_MATCH_THRESHOLD && strcmp(r.name,"Unknown") != 0) {
                    strncpy(best_t->name, r.name, 63);
                    best_t->score = r.score;
                    best_t->lifespan = TRACKER_LIFESPAN;
                    qDebug()<<"识别成功: "<<r.name << "(Tracker #" << best_t->id << " refreshed)";
                }
            }
            free(res);
        }

        //状态聚合与信号发射
        QList<RecognitionResult> final_results; QString status="正在监控...";
        for (auto& t : m_trackers) {
            if (t.active && t.lifespan > 0) { 
                RecognitionResult r; r.rect=t.rect; strncpy(r.name,t.name,63); r.score=t.score; 
                final_results.append(r); 
                if(strcmp(t.name,"Tracking...")!=0 && strcmp(t.name,"Unknown")!=0) 
                    status=QString("检测到: %1").arg(t.name); 
                }
            else { 
                if(t.active) { qDebug()<<"追踪器 #"<<t.id<<" 丢失"; } 
                t.active = 0; 
            }
        }

        emit frameProcessed(m_lastFrameJpeg, final_results);
        emit statusMessage(status);
    }
    // 资源释放
    video_capture_release_frame(m_cam, frame);
    m_frameCounter++;
}

void VideoProcessor::stop()
{
    m_stopped = true; 
    qDebug() << "Stop requested. Timer will halt on next cycle.";
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
    //  使用QDir来递归地删除并重建临时目录
    QDir tempDir(REG_TEMP_PATH);
    tempDir.removeRecursively();
    tempDir.mkpath(".");

    m_registrationName = name;
    m_photosToTake = REGISTRATION_PHOTO_COUNT;
    m_takenPhotoPaths.clear();
    m_regCaptureInterval = 0;
    m_registrationMode = true; 

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
    // 在屏幕上绘制一个提示框
    QList<RecognitionResult> ui_results;
    if(!detected_faces.empty()){
        RecognitionResult r;
        r.rect = detected_faces[0];                          
        snprintf(r.name, sizeof(r.name), "Positioning..."); 
        r.score = 0;
        ui_results.append(r);
    }
    emit frameProcessed(m_lastFrameJpeg, ui_results);

    m_regCaptureInterval++;
    if (detected_faces.size() == 1 && m_regCaptureInterval >= REGISTRATION_CAPTURE_INTERVAL_FRAMES) {
        m_regCaptureInterval = 0;
        int photo_num = m_takenPhotoPaths.size() + 1;
        QString filePath = REG_TEMP_PATH + QString("%1.jpg").arg(photo_num, 3, 10, QChar('0'));
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(m_lastFrameJpeg);
            file.close();
            m_takenPhotoPaths.append(filePath);
            qDebug() << "Registration photo taken:" << filePath;

            if (m_takenPhotoPaths.size() >= m_photosToTake) {
                emit statusMessage(QString("采集完毕，正在处理照片..."));
            } else {
                emit statusMessage(QString("第 %1/%2 张采集成功，请调整姿势...")
                                       .arg(photo_num)
                                       .arg(m_photosToTake));
                QThread::sleep(3);
            }
        }
        //照片采集完毕，开始真正的注册
        if (m_takenPhotoPaths.size() >= m_photosToTake) {
            std::vector<const char*> c_paths;
            std::vector<QByteArray> c_paths_storage;
            for(const QString& p : m_takenPhotoPaths) {
                c_paths_storage.push_back(p.toUtf8());
                c_paths.push_back(c_paths_storage.back().constData());
            }
            //调用注册API:
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
    QDir tempDir(REG_TEMP_PATH);
    tempDir.removeRecursively();
    m_registrationMode = false;
}

void VideoProcessor::setBrightness(int v) { 
    if(!m_cam) return; struct v4l2_control ctl; 
    ctl.id = V4L2_CID_BRIGHTNESS; 
    ctl.value=v; 
    if(ioctl(m_cam->fd, VIDIOC_S_CTRL, &ctl)<0) 
    qWarning("设置亮度失败"); 
}

//卡尔曼滤波器的核心初始化
void VideoProcessor::initKalmanFilter(cv::KalmanFilter& kf, const FaceRect& r) 
{ 
    kf.init(8,4,0,CV_32F);
    kf.transitionMatrix=(cv::Mat_<float>(8,8)<<1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1); 
    kf.measurementMatrix=cv::Mat::eye(4,8,CV_32F); 

    cv::setIdentity(kf.processNoiseCov,cv::Scalar::all(1e-2)); 
    cv::setIdentity(kf.measurementNoiseCov,cv::Scalar::all(1e-1)); 
    cv::setIdentity(kf.errorCovPost,cv::Scalar::all(1)); 
    
    //滤波器的初始状态
    kf.statePost.at<float>(0)=r.x+r.width/2.f; 
    kf.statePost.at<float>(1)=r.y+r.height/2.f; 
    kf.statePost.at<float>(2)=r.width; 
    kf.statePost.at<float>(3)=r.height; 
    kf.statePost.at<float>(4)=0; 
    kf.statePost.at<float>(5)=0; 
    kf.statePost.at<float>(6)=0; 
    kf.statePost.at<float>(7)=0; 
}
//计算交集的左上角和右下角坐标，从而得到交集面积
float VideoProcessor::calculate_iou(const FaceRect& r1, const FaceRect& r2) { 
    int x1=std::max(r1.x,r2.x), y1=std::max(r1.y,r2.y), x2=std::min(r1.x+r1.width, r2.x+r2.width), y2=std::min(r1.y+r1.height, r2.y+r2.height); 
    int w=std::max(0,x2-x1), h=std::max(0,y2-y1); int inter=w*h; 
    int unio=r1.width*r1.height+r2.width*r2.height-inter; 
    return unio>0? (float)inter/unio : 0.0f; 
}
