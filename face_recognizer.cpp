#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <fstream>
#include <cmath>

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>

#include "face_recognizer.h"

// --- 全局和异步处理组件 ---
struct RecognitionTask {
    cv::Mat image;
    std::vector<FaceRect> faces;
};
using RecognitionResultVec = std::vector<RecognitionResult>;  

static std::queue<RecognitionTask> task_queue;  
static std::mutex task_queue_mutex;
static std::condition_variable task_queue_cv;    

static std::queue<RecognitionResultVec> result_queue;   
static std::mutex result_queue_mutex;                   
static std::condition_variable result_queue_cv;        

static cv::dnn::Net net;   
static std::vector<std::pair<std::string, std::vector<cv::Mat>>> face_database_clustered;
static std::thread worker_thread;           
static std::atomic<bool> exit_flag(false);   
static std::string g_database_path;          

const cv::Size INPUT_SIZE(112, 112);    
const float THRESHOLD = 0.363f;          
const int NUM_CLUSTERS = 3;              

// --- 内部辅助函数 ---
// 计算两个特征向量的余弦相似度
static double cosine_similarity(const cv::Mat& a, const cv::Mat& b) {
    cv::Mat a_norm, b_norm;
    cv::normalize(a, a_norm);
    cv::normalize(b, b_norm);
    return a_norm.dot(b_norm);
}

// 对人脸切片进行预处理，增强图像质量
static cv::Mat preprocess_face_chip(const cv::Mat& face_chip) {
    if (face_chip.empty()) {
        return face_chip;
    }
    cv::Mat processed_chip;
    //  Gamma 校正
    float gamma = 0.8;
    cv::Mat lut(1, 256, CV_8U);
    uchar* p = lut.ptr();
    for(int i = 0; i < 256; ++i) {
        p[i] = cv::saturate_cast<uchar>(pow(i / 255.0, gamma) * 255.0);
    }
    cv::LUT(face_chip, lut, processed_chip);
    // 转换为灰度图
    cv::Mat gray_chip;
    cv::cvtColor(processed_chip, gray_chip, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray_chip, gray_chip);                            
    cv::cvtColor(gray_chip, processed_chip, cv::COLOR_GRAY2BGR);       
    return processed_chip;
}

// 从一个Mat格式的人脸切片中提取128维特征向量
static int get_feature(const cv::Mat& face_chip, cv::Mat& feature) {
    cv::Mat processed_chip = preprocess_face_chip(face_chip);
    cv::Mat blob;    
    cv::dnn::blobFromImage(processed_chip, blob, 1.0/255.0, INPUT_SIZE, cv::Scalar(), true, false);

    net.setInput(blob);
    feature = net.forward();        
    cv::normalize(feature, feature, 1.0, 0.0, cv::NORM_L2);    
    return 0;
}

// 从一个图像文件路径中提取主导人脸的特征
static int get_feature_from_path(const char* image_path, cv::Mat& feature) {
    cv::Mat img = cv::imread(image_path);
    if (img.empty()) {
        fprintf(stderr, "Failed to read image %s\n", image_path);
        return -1;
    }

    std::vector<uchar> jpeg_buf;
    cv::imencode(".jpg", img, jpeg_buf);

    FaceRect* faces = nullptr;
    int num_faces = face_detector_detect(jpeg_buf.data(), jpeg_buf.size(), &faces);
    if (num_faces <= 0) {
        if (faces) free(faces);
        fprintf(stderr, "No faces found in %s\n", image_path);
        return -1;
    }
    // 找到面积最大的人脸作为目标
    FaceRect target_face = faces[0];
    int max_area = 0;
    for(int i = 0; i < num_faces; i++) {
        int area = faces[i].width * faces[i].height;
        if (area > max_area) { max_area = area; target_face = faces[i]; }
    }
    free(faces);
    cv::Rect roi(target_face.x, target_face.y, target_face.width, target_face.height);
    cv::Mat face_chip = img(roi);

    return get_feature(face_chip, feature);
}

// --- 数据库持久化函数 (声明为 static) ---
static void load_database_clustered() {
    std::ifstream db_file(g_database_path, std::ios::binary);
    if (!db_file.is_open()) {
        printf("Clustered DB file '%s' not found. A new one will be created upon registration.\n", g_database_path.c_str());
        return;
    }

    face_database_clustered.clear();
    int name_len;
    while (db_file.read(reinterpret_cast<char*>(&name_len), sizeof(name_len))) {
        std::string name(name_len, '\0');
        db_file.read(&name[0], name_len);

        int num_features;
        db_file.read(reinterpret_cast<char*>(&num_features), sizeof(num_features));

        std::vector<cv::Mat> features;
        for (int i = 0; i < num_features; ++i) {
            cv::Mat feature(1, 128, CV_32F);
            db_file.read(reinterpret_cast<char*>(feature.ptr<float>(0)), 128 * sizeof(float));
            if (db_file.gcount() != 128 * sizeof(float)) {
                fprintf(stderr, "DB Error: Incomplete feature read for %s\n", name.c_str());
                face_database_clustered.clear();
                return;
            }
            features.push_back(feature.clone());
        }

        face_database_clustered.emplace_back(name, features);
    }
    db_file.close();
    printf("Loaded %zu clustered faces from DB '%s'.\n", face_database_clustered.size(), g_database_path.c_str());
}

static void save_database_clustered() {
    std::ofstream db_file(g_database_path, std::ios::binary | std::ios::trunc);
    if (!db_file.is_open()) {
        fprintf(stderr, "Error: Could not open DB file '%s' for writing.\n", g_database_path.c_str());
        return;
    }

    for (const auto& entry : face_database_clustered) {
        const std::string& name = entry.first;
        const auto& features = entry.second;

        int name_len = name.length();
        db_file.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
        db_file.write(name.c_str(), name_len);

        int num_features = features.size();
        db_file.write(reinterpret_cast<const char*>(&num_features), sizeof(num_features));

        for (const auto& feature : features) {
            db_file.write(reinterpret_cast<const char*>(feature.ptr<float>(0)), 128 * sizeof(float));
        }
    }
    db_file.close();
    printf("Saved %zu clustered faces to DB file.\n", face_database_clustered.size());
}

static bool is_name_registered(const std::string& name) {
    for (const auto& entry : face_database_clustered) {
        if (entry.first == name) return true;
    }
    return false;
}

// --- 消费者线程函数 ---
void recognition_worker_func() {
    while (!exit_flag) {
        RecognitionTask task;
        {
            std::unique_lock<std::mutex> lock(task_queue_mutex);

            task_queue_cv.wait(lock, []{ return !task_queue.empty() || exit_flag; });
            if (exit_flag) break;
            
            task = task_queue.front();
            task_queue.pop();
        }

        //  执行耗时的识别任务 
        RecognitionResultVec results;
        for (const auto& face_rect : task.faces) {
            cv::Rect roi(face_rect.x, face_rect.y, face_rect.width, face_rect.height);
            roi = roi & cv::Rect(0, 0, task.image.cols, task.image.rows); 
            if(roi.width <= 1 || roi.height <= 1) continue;

            cv::Mat face_chip = task.image(roi);
            cv::Mat feature;
            if (get_feature(face_chip, feature) != 0) continue; 
            
            // --- 与数据库中的所有模板进行比对 ---
            float best_score = 0.f;
            std::string best_name = "Unknown";

            for (const auto& db_entry : face_database_clustered) {       
                for (const auto& cluster_center : db_entry.second) {    
                    double score = cosine_similarity(feature, cluster_center);
                    if (score > best_score) {
                        best_score = score;
                        if (score > THRESHOLD) {
                            best_name = db_entry.first;
                        } else {
                            best_name = "Unknown";
                        }
                    }
                }
            }
            //  将识别结果打包 
            RecognitionResult res;
            res.rect = face_rect;
            strncpy(res.name, best_name.c_str(), sizeof(res.name) - 1);
            res.name[sizeof(res.name) - 1] = '\0';
            res.score = best_score;
            results.push_back(res);
        }
        {   
            std::lock_guard<std::mutex> lock(result_queue_mutex);
            result_queue.push(results);
        }
        result_queue_cv.notify_one();
    }
    printf("Recognition worker thread has exited.\n");
}


// --- C风格API实现 ---
// 所有对外接口都放在这个 extern "C" 块中
extern "C" {

int face_recognizer_init(const char *model_path, const char* db_path) {
    g_database_path = db_path;
    try {
        net = cv::dnn::readNet(model_path);
        if (net.empty()) return -1;
    } catch (const cv::Exception& e) {
        fprintf(stderr, "OpenCV Exception during model loading: %s\n", e.what());
        return -1;
    }

    load_database_clustered();

    exit_flag = false;
    worker_thread = std::thread(recognition_worker_func);
    printf("Face recognizer (Clustered Features) initialized.\n");
    return 0;
}

void face_recognizer_cleanup() {
    if (exit_flag) return;
    exit_flag = true;
    task_queue_cv.notify_all();
    result_queue_cv.notify_all();
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    face_database_clustered.clear();
    printf("Face recognizer cleaned up.\n");
}

int face_recognizer_register_face(const unsigned char *jpeg_buf, unsigned long jpeg_size, const char *name) {
    fprintf(stderr, "Warning: Single photo registration is disabled. Please use 'register_faces_from_paths'.\n");
    return -1;
}

int face_recognizer_register_faces_from_paths(const char* const* image_paths, int num_images, const char* name) {
    if (is_name_registered(name)) {
        printf("Name '%s' is already registered. Skipping registration.\n", name);
        return 0;
    }

    std::vector<cv::Mat> all_features;
    for (int i = 0; i < num_images; ++i) {
        cv::Mat feature;
        if (get_feature_from_path(image_paths[i], feature) == 0) {
            all_features.push_back(feature);
        } else {
            fprintf(stderr, "Warning: Could not get feature from %s\n", image_paths[i]);
        }
    }

    if (all_features.size() < NUM_CLUSTERS) {
        fprintf(stderr, "Error: Not enough valid photos (%zu) to create %d clusters for '%s'.\n", all_features.size(), NUM_CLUSTERS, name);
        return 0; 
    }

    cv::Mat features_matrix(all_features.size(), 128, CV_32F);
    for (size_t i = 0; i < all_features.size(); ++i) {
        all_features[i].copyTo(features_matrix.row(i));
    }

    cv::Mat labels, centers;
    cv::kmeans(features_matrix, NUM_CLUSTERS, labels,
               cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 10, 1.0),
               3, cv::KMEANS_PP_CENTERS, centers);

    std::vector<cv::Mat> cluster_features;
    for (int i = 0; i < centers.rows; ++i) {
        cv::Mat center_row = centers.row(i);
        cv::normalize(center_row, center_row);
        cluster_features.push_back(center_row.clone());
    }

    face_database_clustered.emplace_back(std::string(name), cluster_features);
    save_database_clustered();

    printf("Registered %d feature clusters for '%s'.\n", NUM_CLUSTERS, name);
    return all_features.size();
}
// 异步接口 - 任务生产者
int face_recognizer_submit_task(const unsigned char *jpeg_buf, unsigned long jpeg_size, const FaceRect *faces, int num_faces) {
    std::vector<FaceRect> faces_vec(faces, faces + num_faces);

    std::vector<unsigned char> jpeg_vec(jpeg_buf, jpeg_buf + jpeg_size);
    cv::Mat image = cv::imdecode(jpeg_vec, cv::IMREAD_COLOR);
    if (image.empty()) {
        fprintf(stderr, "Failed to decode JPEG in submit_task\n");
        return -1;
    }

    std::lock_guard<std::mutex> lock(task_queue_mutex);
    if (task_queue.size() > 2) {
        return -1;
    }
    task_queue.push({image.clone(), faces_vec});
    task_queue_cv.notify_one();
    return 0;
}
// 异步接口 - 结果消费者
int face_recognizer_get_results(RecognitionResult **out_results) {
    std::unique_lock<std::mutex> lock(result_queue_mutex, std::try_to_lock);
    if (lock.owns_lock() && !result_queue.empty()) {
        RecognitionResultVec results_vec = result_queue.front();
        result_queue.pop();
        lock.unlock();

        int num_results = results_vec.size();
        if (num_results > 0) {
            *out_results = (RecognitionResult*)malloc(num_results * sizeof(RecognitionResult));
            if (!*out_results) {
                perror("malloc for results failed");
                return -1;
            }
            memcpy(*out_results, results_vec.data(), num_results * sizeof(RecognitionResult));
        } else {
            *out_results = NULL;
        }
        return num_results;
    }

    *out_results = NULL;
    return 0;
}

int face_recognizer_clear_database() {
    face_database_clustered.clear();
    save_database_clustered(); 

    printf("Face database has been cleared.\n");
    return 0;
}

} // extern "C"
