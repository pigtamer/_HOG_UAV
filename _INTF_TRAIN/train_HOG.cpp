// train_HOG -d -dw=64 -dh=64 -pd=./pos -nd=./nega -tv=./Video_1.avi
#include "train_utils.hpp"
#include <iostream>
#include <time.h>

using namespace cv;
using namespace cv::ml;
using namespace std;

vector<float> HoGTrainer::get_svm_detector(const Ptr<SVM> &svm) {
  // get the support vectors
  Mat sv = svm->getSupportVectors();
  const int sv_total = sv.rows;
  // get the decision function
  Mat alpha, svidx;
  double rho = svm->getDecisionFunction(0, alpha, svidx);

  CV_Assert(alpha.total() == 1 && svidx.total() == 1 && sv_total == 1);
  CV_Assert((alpha.type() == CV_64F && alpha.at<double>(0) == 1.) ||
            (alpha.type() == CV_32F && alpha.at<float>(0) == 1.f));
  CV_Assert(sv.type() == CV_32F);

  vector<float> hog_detector(sv.cols + 1);
  memcpy(&hog_detector[0], sv.ptr(), sv.cols * sizeof(hog_detector[0]));
  hog_detector[sv.cols] = (float)-rho;
  return hog_detector;
}

/*
 * Convert training/testing set to be used by OpenCV Machine Learning
 * algorithms. TrainData is a matrix of size (#samples x max(#cols,#rows) per
 * samples), in 32FC1. Transposition of samples are made if needed.
 */
void HoGTrainer::convert_to_ml(const vector<Mat> &train_samples,
                               Mat &trainData) {
  //--Convert data
  const int rows = (int)train_samples.size();
  const int cols = (int)std::max(train_samples[0].cols, train_samples[0].rows);
  Mat tmp(1, cols, CV_32FC1); //< used for transposition if needed
  trainData = Mat(rows, cols, CV_32FC1);

  for (size_t i = 0; i < train_samples.size(); ++i) {
    CV_Assert(train_samples[i].cols == 1 || train_samples[i].rows == 1);

    if (train_samples[i].cols == 1) {
      transpose(train_samples[i], tmp);
      tmp.copyTo(trainData.row((int)i));
    } else if (train_samples[i].rows == 1) {
      train_samples[i].copyTo(trainData.row((int)i));
    }
  }
}

void HoGTrainer::load_images(const String &dirname, vector<Mat> &img_lst,
                             bool showImages = false) {
  vector<String> files;
  glob(dirname, files);

  for (size_t i = 0; i < files.size(); ++i) {
    Mat img = imread(files[i]); // load the image
    if (img.empty())            // invalid image, skip it.
    {
      cout << files[i] << " is invalid!" << endl;
      continue;
    }

    if (showImages) {
      imshow("image", img);
      waitKey(1);
    }
    img_lst.push_back(img);
  }
}

void HoGTrainer::sample_neg(const vector<Mat> &full_neg_lst,
                            vector<Mat> &neg_lst, const Size &size) {
  Rect box;
  box.width = size.width;
  box.height = size.height;

  const int size_x = box.width;
  const int size_y = box.height;

  srand((unsigned int)time(NULL));

  for (size_t i = 0; i < full_neg_lst.size(); i++)
    if (full_neg_lst[i].cols >= box.width &&
        full_neg_lst[i].rows >= box.height) {
      box.x = rand() % (full_neg_lst[i].cols - size_x);
      box.y = rand() % (full_neg_lst[i].rows - size_y);
      Mat roi = full_neg_lst[i](box);
      neg_lst.push_back(roi.clone());
    }
}

void HoGTrainer::computeHOGs(const Size wsize, const vector<Mat> &img_lst,
                             vector<Mat> &gradient_lst, bool use_flip) {
  HOGDescriptor hog;
  hog.winSize = wsize;
  Mat gray;
  vector<float> descriptors;

  for (size_t i = 0; i < img_lst.size(); i++) {
    if (img_lst[i].cols >= wsize.width && img_lst[i].rows >= wsize.height) {
      Rect r =
          Rect((img_lst[i].cols - wsize.width) / 2,
               (img_lst[i].rows - wsize.height) / 2, wsize.width, wsize.height);
      cvtColor(img_lst[i](r), gray, COLOR_BGR2GRAY);
      hog.compute(gray, descriptors, Size(8, 8), Size(0, 0));
      gradient_lst.push_back(Mat(descriptors).clone());
      if (use_flip) {
        flip(gray, gray, 1);
        hog.compute(gray, descriptors, Size(8, 8), Size(0, 0));
        gradient_lst.push_back(Mat(descriptors).clone());
      }
    }
  }
}

void HoGTrainer::test_trained_detector(String obj_det_filename,
                                       String test_video_dir,
                                       String videofilename) {
  cout << "Testing trained detector..." << endl;
  HOGDescriptor hog;
  hog.load(obj_det_filename);

  vector<String> files;
  glob(test_video_dir, files);

  int delay = 0;
  VideoCapture cap;

  if (videofilename != "") {
    if (videofilename.size() == 1 && isdigit(videofilename[0]))
      cap.open(videofilename[0] - '0');
    else
      cap.open(videofilename);
  }

  obj_det_filename = "testing " + obj_det_filename;
  namedWindow(obj_det_filename, WINDOW_NORMAL);

  for (size_t i = 0;; i++) {
    Mat img;

    if (cap.isOpened()) {
      cap >> img;
      delay = 1;
    } else if (i < files.size()) {
      img = imread(files[i]);
    }

    if (img.empty()) {
      return;
    }

    vector<Rect> detections;
    vector<double> foundWeights;

    hog.detectMultiScale(img, detections, foundWeights);
    if (foundWeights.size() > 0){
        double MAX_WEIGHT = *max_element(std::begin(foundWeights), std::end(foundWeights));
        for (size_t j = 0; j < detections.size(); j++) {
            Scalar color = Scalar(0, foundWeights[j] * foundWeights[j] * 200, 0);
            if( foundWeights[j] == MAX_WEIGHT){
                if (foundWeights[j] > 0.1){
                    // cout << foundWeights[j] << endl;
                    double w = foundWeights[j];
                    putText(img, to_string(w), detections[j].tl(),  FONT_HERSHEY_PLAIN, 2, Scalar(255, 0, 0));
                    rectangle(img, detections[j], color, img.cols / 400 + 1);
                }else{
                    putText(img, "[!]", Point(0, img.cols / 4),  FONT_HERSHEY_DUPLEX, 5, Scalar(0, 0, 255));
                }
            }
        }
    }else{
        putText(img, "[!]", Point(0, img.cols / 4),  FONT_HERSHEY_DUPLEX, 5, Scalar(0, 0, 255));
    }
    imshow(obj_det_filename, img);

    if (waitKey(delay) == 27) {
        return;
    }

    }
}

int HoGTrainer::run(int detector_width, int detector_height, string pos_dir,
                    string neg_dir, string test_video_dir,
                    string obj_det_filename, string videofilename,
                    bool FLAG_ONLY_TEST, bool FLAG_VISUALIZE_TRAIN,
                    bool FLAG_FLIP_SAMPLES, bool FLAG_TRAIN_TWICE) {
  if (FLAG_ONLY_TEST) {
    test_trained_detector(obj_det_filename, test_video_dir, videofilename);
    exit(0);
  }

  if (pos_dir.empty() || neg_dir.empty()) {
    cout << "ILLEGAL DIR FOR SAMPLES." << endl;
    exit(1);
  }

  vector<Mat> pos_lst, full_neg_lst, neg_lst, gradient_lst;
  vector<int> labels;

  clog << "Positive images are being loaded...";
  load_images(pos_dir, pos_lst, FLAG_VISUALIZE_TRAIN);
  if (pos_lst.size() > 0) {
    clog << "...[done]" << endl;
  } else {
    clog << "no image in " << pos_dir << endl;
    return 1;
  }

  Size pos_image_size = pos_lst[0].size();

  if (detector_width && detector_height) {
    pos_image_size = Size(detector_width, detector_height);
  } else {
    for (size_t i = 0; i < pos_lst.size(); ++i) {
      if (pos_lst[i].size() != pos_image_size) {
        cout << "All positive images should be same size!" << endl;
        exit(1);
      }
    }
    pos_image_size = pos_image_size / 8 * 8;
  }

  clog << "Negative images are being loaded...";
  load_images(neg_dir, full_neg_lst, false);
  sample_neg(full_neg_lst, neg_lst, pos_image_size);
  clog << "...[done]" << endl;

  clog << "Histogram of Gradients are being calculated for positive images...";
  computeHOGs(pos_image_size, pos_lst, gradient_lst, FLAG_FLIP_SAMPLES);
  size_t positive_count = gradient_lst.size();
  labels.assign(positive_count, +1);
  clog << "...[done] ( positive count : " << positive_count << " )" << endl;

  clog << "Histogram of Gradients are being calculated for negative images...";
  computeHOGs(pos_image_size, neg_lst, gradient_lst, FLAG_FLIP_SAMPLES);
  size_t negative_count = gradient_lst.size() - positive_count;
  labels.insert(labels.end(), negative_count, -1);
  CV_Assert(positive_count < labels.size());
  clog << "...[done] ( negative count : " << negative_count << " )" << endl;

  Mat train_data;
  convert_to_ml(gradient_lst, train_data);

  clog << "Training SVM...";
  Ptr<SVM> svm = SVM::create();
  /* Default values to train SVM */
  svm->setCoef0(0.0);
  svm->setDegree(3);
  svm->setTermCriteria(
      TermCriteria(CV_TERMCRIT_ITER + CV_TERMCRIT_EPS, 1000, 1e-3));
  svm->setGamma(0);
  svm->setKernel(SVM::LINEAR);
  svm->setNu(0.5);
  svm->setP(0.1);             // for EPSILON_SVR, epsilon in loss function?
  svm->setC(0.01);            // From paper, soft classifier
  svm->setType(SVM::EPS_SVR); // C_SVC; // EPSILON_SVR; // may be also NU_SVR;
                              // // do regression task
  svm->train(train_data, ROW_SAMPLE, labels);
  clog << "...[done]" << endl;

  if (FLAG_TRAIN_TWICE) {
    clog << "Testing trained detector on negative images. This may take a few "
            "minutes...";
    HOGDescriptor my_hog;
    my_hog.winSize = pos_image_size;

    // Set the trained svm to my_hog
    my_hog.setSVMDetector(get_svm_detector(svm));

    vector<Rect> detections;
    vector<double> foundWeights;

    for (size_t i = 0; i < full_neg_lst.size(); i++) {
      if (full_neg_lst[i].cols >= pos_image_size.width &&
          full_neg_lst[i].rows >= pos_image_size.height)
        my_hog.detectMultiScale(full_neg_lst[i], detections, foundWeights);
      else
        detections.clear();

      for (size_t j = 0; j < detections.size(); j++) {
        Mat detection = full_neg_lst[i](detections[j]).clone();
        resize(detection, detection, pos_image_size, 0, 0, INTER_LINEAR_EXACT);
        neg_lst.push_back(detection);
      }

      if (FLAG_VISUALIZE_TRAIN) {
        for (size_t j = 0; j < detections.size(); j++) {
          rectangle(full_neg_lst[i], detections[j], Scalar(0, 255, 0), 2);
        }
        imshow("testing trained detector on negative images", full_neg_lst[i]);
        waitKey(5);
      }
    }
    clog << "...[done]" << endl;

    gradient_lst.clear();
    clog
        << "Histogram of Gradients are being calculated for positive images...";
    computeHOGs(pos_image_size, pos_lst, gradient_lst, FLAG_FLIP_SAMPLES);
    positive_count = gradient_lst.size();
    clog << "...[done] ( positive count : " << positive_count << " )" << endl;

    clog
        << "Histogram of Gradients are being calculated for negative images...";
    computeHOGs(pos_image_size, neg_lst, gradient_lst, FLAG_FLIP_SAMPLES);
    negative_count = gradient_lst.size() - positive_count;
    clog << "...[done] ( negative count : " << negative_count << " )" << endl;

    labels.clear();
    labels.assign(positive_count, +1);
    labels.insert(labels.end(), negative_count, -1);

    clog << "Training SVM again...";
    convert_to_ml(gradient_lst, train_data);
    svm->train(train_data, ROW_SAMPLE, labels);
    clog << "...[done]" << endl;
  }

  HOGDescriptor hog;
  hog.winSize = pos_image_size;
  hog.setSVMDetector(get_svm_detector(svm));
  hog.save(obj_det_filename);

  test_trained_detector(obj_det_filename, test_video_dir, videofilename);
  return 0;
}

// TEST THIS CLASS
int main(int argc, char** argv) {
  HoGTrainer testTrainer;
//   testTrainer.run(64, 64, "../pos", "../nega", "../", "detector.yml", "../Video_1.avi", true, true);
  testTrainer.run(64, 64, "../pos", "../nega", "../", "detector.yml",argv[1], argv[2], true);

}