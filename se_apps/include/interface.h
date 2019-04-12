/*

 Copyright (c) 2014 University of Edinburgh, Imperial College, University of Manchester.
 Developed in the PAMELA project, EPSRC Programme Grant EP/K008730/1

 This code is licensed under the MIT License.

 */

#ifndef INTERFACE_H_
#define INTERFACE_H_

#ifdef __APPLE__
    #include <mach/clock.h>
    #include <mach/mach.h>
#endif

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <str_utils.h>
#include <unistd.h>
#include <Eigen/Dense>
#include <thirdparty/cutil_math.h>

#include "sys/stat.h"

enum ReaderType {
  READER_RAW,
  READER_SCENE,
  READER_OPENNI,
  READER_REALSENSE
};





/**
 * A struct containing configuration options for a reader class instance.
 */
struct ReaderConfiguration {
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
  int fps;
  bool blocking_read;
  std::string data_path;
  std::string groundtruth_path;
  Eigen::Matrix4f transform;
};





class DepthReader {
  public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    virtual ~DepthReader() { }

    virtual bool readNextDepthFrame(float* depthMap) = 0;

    inline bool readNextDepthFrame(unsigned short int* UintdepthMap) {
      return readNextDepthFrame(NULL, UintdepthMap);
    }

    virtual bool readNextDepthFrame(uchar3*             raw_rgb,
                                    unsigned short int* depthMap) = 0;

    virtual bool readNextData(uchar3*          ,
                              uint16_t*        ,
                              Eigen::Matrix4f& ) {
      return false;
    };

    virtual Eigen::Vector4f getK() = 0;

    virtual uint2 getinputSize() = 0;

    virtual void restart()=0;

    bool isValid() {
      return cameraOpen;
    }

    int getFrameNumber() {
      return (_frame);
    }

    virtual ReaderType getType() = 0;

    void get_next_frame() {
      if (_fps == 0) {
        _frame++;
        return;
      }

#ifdef __APPLE__
      clock_serv_t cclock;
      mach_timespec_t clockData;
      host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &cclock);
      clock_get_time(cclock, &clockData);
      mach_port_deallocate(mach_task_self(), cclock);
#else
      struct timespec clockData;
      clock_gettime(CLOCK_MONOTONIC, &clockData);
#endif
      double current_frame = clockData.tv_sec
        + clockData.tv_nsec / 1000000000.0;
      static double first_frame = current_frame;

      int frame = std::ceil((current_frame - first_frame) * (double) _fps);
      _frame = frame;

      double tpf = 1.0 / _fps;
      double ttw = ((double) _frame * tpf - current_frame + first_frame);
      if (_blocking_read) {
        if (ttw > 0) {
          usleep(1000000.0 * ttw);
        }
      }
    }

    inline bool readNextPose(Eigen::Matrix4f& pose) {
      std::string line;
      while (true) {
        std::getline(_gt_file,line);
        // EOF reached
        if (!_gt_file.good()) {
          return false;
        }
        // Ignore comment lines
        if (line[0] == '#') {
          continue;
        }
        // Data line read, split on spaces
        const std::vector<std::string> line_data = splitString(line, ' ');
        const size_t num_cols = line_data.size();
        if (num_cols < 7) {
          std::cout << "Invalid ground truth file format."
            << "Expected line format: ... tx ty tz qx qy qz qw" << std::endl;
          return false;
        }
        // Read the last 7 columns
        Eigen::Vector3f tran (std::stof(line_data[num_cols-7]),
                              std::stof(line_data[num_cols-6]),
                              std::stof(line_data[num_cols-5]));
        Eigen::Quaternionf quat (std::stof(line_data[num_cols-1]),
                                 std::stof(line_data[num_cols-4]),
                                 std::stof(line_data[num_cols-3]),
                                 std::stof(line_data[num_cols-2]));
        pose = Eigen::Matrix4f::Identity();
        pose.block<3,3>(0,0) = quat.toRotationMatrix();
        pose.block<3,1>(0,3) = tran;
        _pose_num++;
        // Apply the transform to the pose
        pose = _transform * pose;
        return true;
      }
    }

    bool cameraActive;
    bool cameraOpen;
  protected:
    int _frame;
    size_t _pose_num;
    int _fps;
    bool _blocking_read;
    std::string _data_path;
    std::string _groundtruth_path;
    std::ifstream _gt_file;
    Eigen::Matrix4f _transform;
};





static const float SceneK[3][3] = { { 481.20, 0.00, 319.50 }, { 0.00, -480.00,
  239.50 }, { 0.00, 0.00, 1.00 } };

static const int _scenewidth = 640;
static const int _sceneheight = 480;
static const float _u0 = SceneK[0][2];
static const float _v0 = SceneK[1][2];
static const float _focal_x = SceneK[0][0];
static const float _focal_y = SceneK[1][1];

/**
 * Reader for ICL-NUIM datasets.
 */
class SceneDepthReader: public DepthReader {
  private:

    std::string _dir;
    uint2 _inSize;

  public:
    ~SceneDepthReader() { };

    /**
     * Constructor using the ::ReaderConfiguration struct.
     */
    SceneDepthReader(const ReaderConfiguration& config)
      : SceneDepthReader(config.data_path, config.fps, config.blocking_read){ }

    /**
     * Old style constructor. Does not support ground truth loading.
     *
     * @deprecated Might be removed in the future.
     */
    SceneDepthReader(std::string dir, int fps, bool blocking_read)
      : DepthReader(), _dir(dir), _inSize(make_uint2(640, 480)) {
        std::cerr << "No such directory " << dir << std::endl;
        struct stat st;
        lstat(dir.c_str(), &st);
        if (S_ISDIR(st.st_mode)) {
          cameraOpen = true;
          cameraActive = true;
          _frame = -1;
          _fps = fps;
          _blocking_read = blocking_read;
        } else {
          std::cerr << "No such directory " << dir << std::endl;
          cameraOpen = false;
          cameraActive = false;
        }
    };

    ReaderType getType() {
      return (READER_SCENE);
    }

    inline Eigen::Vector4f getK() {
      return Eigen::Vector4f(481.20, 480.00, 319.50, 239.50);
    }

    inline uint2 getinputSize() {
      return _inSize;
    }

    inline void restart() {
      _frame = 0;
    }

    inline bool readNextDepthFrame(uchar3*, unsigned short int* depthMap) {

      float* FloatdepthMap = (float*) malloc(
          _inSize.x * _inSize.y * sizeof(float));
      bool res = readNextDepthFrame(FloatdepthMap);

      for (unsigned int i = 0; i < _inSize.x * _inSize.y; i++) {
        depthMap[i] = FloatdepthMap[i] * 1000.0f;
      }
      free(FloatdepthMap);
      return res;

    }

    inline bool readNextDepthFrame(float* depthMap) {

      std::ostringstream filename;
      get_next_frame();
      filename << this->_dir << "/scene_00_" << std::setfill('0')
        << std::setw(4) << _frame << ".depth";

      std::ifstream source;
      source.open(filename.str().c_str(), std::ios_base::in);

      if (!source) {
        std::cerr << "Can't open Data from " << filename.str().c_str()
          << "!\n";
        return 0;
      }
      uint index = 0;
      while (source.good()) {
        float d;
        source >> d;
        if (_scenewidth * _sceneheight <= index)
          continue;
        depthMap[index] = d;
        index++;
      }

      for (int v = 0; v < _sceneheight; v++) {
        for (int u = 0; u < _scenewidth; u++) {
          float u_u0_by_fx = (u - _u0) / _focal_x;
          float v_v0_by_fy = (v - _v0) / _focal_y;

          depthMap[u + v * _scenewidth] = depthMap[u + v * _scenewidth]
            / std::sqrt(
                u_u0_by_fx * u_u0_by_fx
                + v_v0_by_fy * v_v0_by_fy + 1);

        }
      }
      return index > 0;
    }

};





/**
 * Reader for Slambench 1.0 datasets.
 */
class RawDepthReader: public DepthReader {
  private:
    FILE* _rawFilePtr;
    uint2 _inSize;

  public:
    /**
     * Constructor using the ::ReaderConfiguration struct.
     */
    RawDepthReader(const ReaderConfiguration& config)
      : DepthReader() {
        // Copy configuration
        _data_path = config.data_path;
        _groundtruth_path = config.groundtruth_path;
        _transform = config.transform;
        // Open ground truth association file if supplied
        if (_groundtruth_path != "") {
          _gt_file.open(_groundtruth_path.c_str());
          if(!_gt_file.is_open()) {
            std::cout << "Failed to open ground truth association file "
              << _groundtruth_path << std::endl;
            _rawFilePtr = NULL;
            return;
          }
          _pose_num = -1;
        }
        // Open raw file
        const std::string raw_filename = _data_path;
        _rawFilePtr = fopen(raw_filename.c_str(), "rb");
        const size_t res = fread(&(_inSize), sizeof(_inSize), 1, _rawFilePtr);
        cameraOpen = false;
        cameraActive = false;
        if (res != 1) {
          std::cerr << "Invalid Raw file " << raw_filename << std::endl;
          return;
        } else {
          cameraOpen = true;
          cameraActive = true;
          _frame = -1;
          _fps = config.fps;
          _blocking_read = config.blocking_read;
          fseeko(_rawFilePtr, 0, SEEK_SET);
        }
      };

    /**
     * Old style constructor. Does not support ground truth loading.
     *
     * @deprecated Might be removed in the future.
     */
    RawDepthReader(std::string filename, int fps, bool blocking_read) :
      DepthReader(), _rawFilePtr(fopen(filename.c_str(), "rb")) {

        size_t res = fread(&(_inSize), sizeof(_inSize), 1, _rawFilePtr);
        cameraOpen = false;
        cameraActive = false;
        if (res != 1) {
          std::cerr << "Invalid Raw file." << std::endl;

        } else {
          cameraOpen = true;
          cameraActive = true;
          _frame = -1;
          _fps = fps;
          _blocking_read = blocking_read;
          fseeko(_rawFilePtr, 0, SEEK_SET);
        }
      };

    /**
     * Get the type of the reader.
     *
     * \return ReaderType::READER_RAW
     */
    ReaderType getType() {
      return (READER_RAW);
    }

    inline bool readNextDepthFrame(uchar3*             raw_rgb,
                                   unsigned short int* depthMap) {

      int total = 0;
      int expected_size = 0;
      unsigned int newImageSize[2];

      get_next_frame();

#ifdef LIGHT_RAW // This LightRaw mode is used to get smaller raw files
      unsigned int size_of_frame = (sizeof(unsigned int) * 2
          + _inSize.x * _inSize.y * sizeof(unsigned short int) );
#else
      off_t size_of_frame = (sizeof(unsigned int) * 4
          + _inSize.x * _inSize.y * sizeof(unsigned short int)
          + _inSize.x * _inSize.y * sizeof(uchar3));
#endif

      fseeko(_rawFilePtr, size_of_frame * _frame, SEEK_SET);

      total += fread(&(newImageSize), sizeof(newImageSize), 1, _rawFilePtr);

      if (depthMap) {
        total += fread(depthMap, sizeof(unsigned short int),
            newImageSize[0] * newImageSize[1], _rawFilePtr);
        expected_size += 1 + newImageSize[0] * newImageSize[1];
      } else {
        total += newImageSize[0] * newImageSize[1];
        fseeko(_rawFilePtr,
            newImageSize[0] * newImageSize[1]
            * sizeof(unsigned short int), SEEK_CUR);
        expected_size += 1 + newImageSize[0] * newImageSize[1];
      }

#ifdef LIGHT_RAW // This LightRaw mode is used to get smaller raw files

      if (raw_rgb) {
        raw_rgb[0].x = 0;
      }

#else
      total += fread(&(newImageSize), sizeof(newImageSize), 1, _rawFilePtr);

      if (raw_rgb) {
        total += fread(raw_rgb, sizeof(uchar3),
            newImageSize[0] * newImageSize[1], _rawFilePtr);
        expected_size += 1 + newImageSize[0] * newImageSize[1];
      } else {
        total += newImageSize[0] * newImageSize[1];
        fseeko(_rawFilePtr, newImageSize[0] * newImageSize[1] * sizeof(uchar3),
            SEEK_CUR);
        expected_size += 1 + newImageSize[0] * newImageSize[1];
      }
#endif

      if (total != expected_size) {
        std::cout << "End of file" << (total == 0 ? "" : "(garbage found)")
          << "." << std::endl;
        return false;
      } else {
        return true;
      }
    }

    /**
     * Restart the reader from the beginning.
     */
    inline void restart() {
      _frame = -1;
      _pose_num = -1;
      rewind(_rawFilePtr);
      if (_gt_file.is_open())
        _gt_file.seekg(0, _gt_file.beg);
    }

    inline bool readNextDepthFrame(float* depthMap) {

      unsigned short int* UintdepthMap = (unsigned short int*) malloc(
          _inSize.x * _inSize.y * sizeof(unsigned short int));
      bool res = readNextDepthFrame(NULL, UintdepthMap);

      for (unsigned int i = 0; i < _inSize.x * _inSize.y; i++) {
        depthMap[i] = (float) UintdepthMap[i] / 1000.0f;
      }
      free(UintdepthMap);
      return res;
    }

    /**
     * Read the RGB, depth and ground truth pose data corresponding to the next
     * measurement.
     *
     * \param[out] rgb_image The RGB frame.
     * \param[out] depth_image The depth frame.
     * \param[out] pose The ground truth pose.
     * \return true on success, false on failure to read.
     */
    inline bool readNextData(uchar3*          rgb_image,
                             uint16_t*        depth_image,
                             Eigen::Matrix4f& pose) {
      bool res;
      // _pose_num is incremented inside readNextPose()
      res = readNextPose(pose);
      // _frame is incremented inside readNextDepthFrame()
      res = res && readNextDepthFrame(rgb_image, depth_image);
      return res;
    }

    /**
     * Returns the dimensions of the frames read.
     */
    inline uint2 getinputSize() {
      return _inSize;
    }

    /**
     * Returns a vector with the camera matrix parameters. The `x`, `y`, `z`
     * and `w` elements are the x-axis focal length, y-axis focal length,
     * x-axis optical center and y-axis optical center.
     */
    inline Eigen::Vector4f getK() {
      return Eigen::Vector4f(531.15, 531.15, 640 / 2, 480 / 2);
    }

};





#ifdef DO_OPENNI
#include <OpenNI.h>

class MyDepthFrameAllocator: public openni::VideoStream::FrameAllocator {
  private:
    uint16_t *depth_buffers_;

  public:
    MyDepthFrameAllocator(uint16_t *depth_buffers) :
      depth_buffers_(depth_buffers) {
      }
    void *allocateFrameBuffer(int ) {
      return depth_buffers_;
    }
    void freeFrameBuffer(void *) {
    }
};

class MyColorFrameAllocator: public openni::VideoStream::FrameAllocator {
  private:
    uchar3 *rgb_buffer_;

  public:
    MyColorFrameAllocator(uchar3 *rgb_buffer) {
      rgb_buffer_ = rgb_buffer;
    }
    void *allocateFrameBuffer(int ) {
      return rgb_buffer_;
    }
    void freeFrameBuffer(void *) {
    }
};

class OpenNIDepthReader: public DepthReader {
  private:
    FILE* _pFile;
    uint2 _inSize;

    openni::Device device;
    openni::VideoStream depth;
    openni::VideoStream rgb;
    openni::VideoFrameRef depthFrame;
    openni::VideoFrameRef colorFrame;

    uint16_t * depthImage;
    uchar3 * rgbImage;

    openni::Status rc;

  public:
    ~OpenNIDepthReader() {
      if (device.isValid()) {
        //std::cout << "Stopping depth stream..." << std::endl;

        depth.stop();
        //std::cout << "Stopping rgb stream..." << std::endl;
        rgb.stop();
        //std::cout << "Destroying depth stream..." << std::endl;
        depth.destroy();
        // std::cout << "Destroying rgb stream..." << std::endl;
        rgb.destroy();
        //std::cout << "Closing device..." << std::endl;
        device.close();

        //std::cout << "Shutting down OpenNI..." << std::endl;
        openni::OpenNI::shutdown();

        //std::cout << "Done!" << std::endl;
        cameraOpen = false;
        cameraActive = false;
      }
    };

    ReaderType getType() {
      return(READER_OPENNI);
    }

    /**
     * Constructor using the ::ReaderConfiguration struct.
     */
    OpenNIDepthReader(const ReaderConfiguration& config)
      : OpenNIDepthReader(config.data_path, config.fps, config.blocking_read){ }

    /**
     * Old style constructor. Does not support ground truth loading.
     *
     * @deprecated Might be removed in the future.
     */
    OpenNIDepthReader(std::string filename, int fps, bool blocking_read)
      : DepthReader(), _pFile(fopen(filename.c_str(), "rb")) {

        cameraActive = false;
        cameraOpen = false;

        rc = openni::OpenNI::initialize();

        if (rc != openni::STATUS_OK) {
          return;
        }

        if (filename == "") {
          rc = device.open(openni::ANY_DEVICE);
        } else {
          rc = device.open(filename.c_str());
          //std::cerr<<"Opened an oni file\n";
        }

        if (rc != openni::STATUS_OK) {
          std::cout << "No kinect device found. " << cameraActive
              << " " << cameraOpen << "\n";
          return;
        }

        if (device.getSensorInfo(openni::SENSOR_DEPTH) != NULL) {
          rc = depth.create(device, openni::SENSOR_DEPTH);

          if (rc != openni::STATUS_OK) {
            std::cout << "Couldn't create depth stream\n"
                << openni::OpenNI::getExtendedError() << std::endl;
            return;
          } else {
            rc = depth.start();
            if (rc != openni::STATUS_OK) {
              printf("Couldn't start depth stream:\n%s\n",
                  openni::OpenNI::getExtendedError());
              depth.destroy();
              return;
            }
            depth.stop();

          }

          rc = rgb.create(device, openni::SENSOR_COLOR);

          if (rc != openni::STATUS_OK) {
            std::cout << "Couldn't create color stream\n"
                << openni::OpenNI::getExtendedError() << std::endl;
            return;
          }

        }

        device.getSensorInfo(openni::SENSOR_DEPTH)->getSupportedVideoModes();

        if (!device.isFile()) {
          openni::VideoMode newDepthMode;
          newDepthMode.setFps(fps == 0 ? 30 : fps);
          newDepthMode.setPixelFormat(openni::PIXEL_FORMAT_DEPTH_1_MM);
          newDepthMode.setResolution(640,480);

          rc = depth.setVideoMode(newDepthMode);
          if(rc != openni::STATUS_OK) {
            std::cout << "Could not set videomode" << std::endl;
            return;
          }

          openni::VideoMode newRGBMode;
          newRGBMode.setFps(fps == 0 ? 30 : fps);
          newRGBMode.setPixelFormat(openni::PIXEL_FORMAT_RGB888);
          newRGBMode.setResolution(640,480);

          rc = rgb.setVideoMode(newRGBMode);
          if (rc != openni::STATUS_OK) {
            std::cout << "Could not set videomode" << std::endl;

            return;
          }
        }

        openni::VideoMode depthMode = depth.getVideoMode();
        openni::VideoMode colorMode = rgb.getVideoMode();

        _inSize.x = depthMode.getResolutionX();
        _inSize.y = depthMode.getResolutionY();

        if ((colorMode.getResolutionX() != _inSize.x)
            || (colorMode.getResolutionY() != _inSize.y)) {
          std::cout << "Incorrect rgb resolution: "
              << colorMode.getResolutionX() << " "
              << colorMode.getResolutionY() << std::endl;
          return;
        }

        depthImage = (uint16_t*) malloc(_inSize.x * _inSize.y * sizeof(uint16_t));
        rgbImage = (uchar3*) malloc(_inSize.x * _inSize.y * sizeof(uchar3));

        rgb.setMirroringEnabled(false);
        depth.setMirroringEnabled(false);

        device.setImageRegistrationMode(openni::IMAGE_REGISTRATION_DEPTH_TO_COLOR);

        if (device.isFile()) {
          device.getPlaybackControl()->setRepeatEnabled(false);
          depthFrame.release();
          colorFrame.release();
          // Set the playback in a manual mode i.e. read a frame whenever the application requests it
          device.getPlaybackControl()->setSpeed(-1);
        }

        // The allocators must survive this initialization function.
        MyDepthFrameAllocator *depthAlloc = new MyDepthFrameAllocator(depthImage);
        MyColorFrameAllocator *colorAlloc = new MyColorFrameAllocator(rgbImage);

        // Use allocators to have OpenNI write directly into our buffers
        depth.setFrameBuffersAllocator(depthAlloc);
        depth.start();

        rgb.setFrameBuffersAllocator(colorAlloc);
        rgb.start();

        cameraOpen =true;
        cameraActive =true;
        _frame=-1;
        _fps = fps;
        _blocking_read = blocking_read;

      };

    inline bool readNextDepthFrame(uchar3*             raw_rgb,
                                   unsigned short int* depthMap) {

      rc = depth.readFrame(&depthFrame);

      if (rc != openni::STATUS_OK) {
        std::cout << "Wait failed" << std::endl;
        exit(1);
      }

      rc = rgb.readFrame(&colorFrame);
      if (rc != openni::STATUS_OK) {
        std::cout << "Wait failed" << std::endl;
        exit(1);
      }

      if ((depthFrame.getVideoMode().getPixelFormat() != openni::PIXEL_FORMAT_DEPTH_1_MM)
          && (depthFrame.getVideoMode().getPixelFormat() != openni::PIXEL_FORMAT_DEPTH_100_UM)) {
        std::cout << "Unexpected frame format" << std::endl;
        exit(1);
      }

      if (colorFrame.getVideoMode().getPixelFormat() != openni::PIXEL_FORMAT_RGB888) {
        std::cout << "Unexpected frame format" << std::endl;
        exit(1);
      }

      if (raw_rgb) {
        memcpy(raw_rgb,rgbImage,_inSize.x * _inSize.y*sizeof(uchar3));
      }
      if (depthMap) {
        memcpy(depthMap,depthImage,_inSize.x * _inSize.y*sizeof(uint16_t));
      }

      get_next_frame ();

      return true;
    }

    inline void restart() {
      _frame=-1;
      //FIXME : how to rewind OpenNI ?
    }

    inline bool readNextDepthFrame(float* depthMap) {

      unsigned short int* UintdepthMap = (unsigned short int*) malloc(_inSize.x
          * _inSize.y * sizeof(unsigned short int));
      bool res = readNextDepthFrame(NULL,UintdepthMap);

      for (unsigned int i = 0; i < _inSize.x * _inSize.y; i++) {
        depthMap[i] = (float) UintdepthMap[i] / 1000.0f;
      }
      free(UintdepthMap);
      return res;
    }

    inline uint2 getinputSize() {
      return _inSize;
    }

    inline Eigen::Vector4f getK() {
      return Eigen::Vector4f(481.2, 480, 640/2, 480/2);
    }

};

#else
class OpenNIDepthReader: public DepthReader {
  public:
    /**
     * Constructor using the ::ReaderConfiguration struct.
     */
    OpenNIDepthReader(const ReaderConfiguration& config)
      : OpenNIDepthReader(config.data_path, config.fps, config.blocking_read){ }

    /**
     * Old style constructor. Does not support ground truth loading.
     *
     * @deprecated Might be removed in the future.
     */
    OpenNIDepthReader(std::string, int, bool) {
      std::cerr << "OpenNI Library Not found." << std::endl;
      cameraOpen = false;
      cameraActive = false;
    }

    bool readNextDepthFrame(float* ) { return false;
    }

    bool readNextDepthFrame(uchar3* , unsigned short int* ) {
      return false;
    }

    Eigen::Vector4f getK() {
      return Eigen::Vector4f::Constant(0.f);
    }

    uint2 getinputSize() {
      return make_uint2(0);
    }

    void restart() {
    }

    ReaderType getType() {
      return (READER_OPENNI);
    }
};
#endif /* DO_OPENNI*/





#ifdef USE_REALSENSE
#include <librealsense2/rs.hpp>

static const int _rs_depth_w = 640;
static const int _rs_depth_h = 480;
static const int _rs_rgb_w = 640;
static const int _rs_rgb_h = 480;

/**
 * Reader class for Intel RealSense RGBD cameras.
 */
class RealSenseDepthReader: public DepthReader {
  private:
    rs2::pipeline _pipeline;

    /**
     * The pipeline configuration.
     */
    rs2::config _config;

    /**
     * The pipeline profile.
     */
    rs2::pipeline_profile _profile;

    /**
     * Sets whether the depth frame is aligned to the RGB frame or vice versa.
     */
    rs2::align _align_to;

    /**
     * The current depth and RGB frames as returned by the pipeline.
     */
    rs2::frameset _frames;

    /**
     * The camera intrinsic parameters.
     */
    rs2_intrinsics _camera_instrinsics;

    /**
     * The parameters of the camera matrix.
     */
    Eigen::Vector4f _K;

    /**
     * Multiply depth frame values by this factor to convert them to meters. It
     * is usually 0.001.
     */
    float _depth_scale;

    /**
     * The dimensions of the RGB and depth frames.
     */
    uint2 _input_size;

  public:
    /**
     * Constructor using the ::ReaderConfiguration struct.
     */
    RealSenseDepthReader(const ReaderConfiguration& config)
      : _align_to(RS2_STREAM_COLOR) {
      std::cout << "Using RealSense2 reader\n";

      // Configure the pipeline.
      _config.enable_stream(RS2_STREAM_DEPTH, -1,
          _rs_depth_w, _rs_depth_h, RS2_FORMAT_Z16, config.fps);
      _config.enable_stream(RS2_STREAM_COLOR, -1,
          _rs_rgb_w, _rs_rgb_h, RS2_FORMAT_RGB8, config.fps);

      // Start the pipeline.
      _profile = _pipeline.start(_config);

      // Create a video_stream_profile to allow getting the camera intrinsics.
      const rs2::video_stream_profile vs_profile(
          _profile.get_stream(RS2_STREAM_COLOR));

      // Get the camera intrinsic parameters.
      _camera_instrinsics = vs_profile.get_intrinsics();
      _K = Eigen::Vector4f(_camera_instrinsics.fx, _camera_instrinsics.fy,
          _camera_instrinsics.ppx, _camera_instrinsics.ppy);
      _input_size = make_uint2(_camera_instrinsics.width,
          _camera_instrinsics.height);

      // Get the scale of the first depth sensor.
      const rs2::depth_sensor depth_device
          = _profile.get_device().first<rs2::depth_sensor>();
      _depth_scale = depth_device.get_depth_scale();

      cameraOpen = true;
      cameraActive = true;

      std::cout << "  width:       " << _camera_instrinsics.width << std::endl;
      std::cout << "  height:      " << _camera_instrinsics.height << std::endl;
      std::cout << "  fx:          " << _camera_instrinsics.fx << std::endl;
      std::cout << "  fy:          " << _camera_instrinsics.fy << std::endl;
      std::cout << "  cx:          " << _camera_instrinsics.ppx << std::endl;
      std::cout << "  cy:          " << _camera_instrinsics.ppy << std::endl;
      std::cout << "  depth scale: " << _depth_scale << " m\n";
    }



    ~RealSenseDepthReader() {
      // TODO
    }



    bool readNextDepthFrame(float* depth) {
      // Block program until frames arrive.
      _frames = _pipeline.wait_for_frames();

      // Align frames
      _frames = _frames.apply_filter(_align_to);

      const rs2::depth_frame rs_depth = _frames.get_depth_frame();

      // Copy depth frame data and convert to meters.
      const uint16_t* raw_depth_data = (uint16_t*) rs_depth.get_data();
#pragma omp parallel for
      for (int p = 0; p < _rs_depth_w * _rs_depth_h; ++p) {
        depth[p] = _depth_scale * raw_depth_data[p];
      }

      _frame++;

      return true;
    }



    bool readNextDepthFrame(uchar3* rgb, unsigned short int* depth) {
      // Block program until frames arrive.
      _frames = _pipeline.wait_for_frames();

      // Align frames
      _frames = _frames.apply_filter(_align_to);

      const rs2::depth_frame rs_depth = _frames.get_depth_frame();
      const rs2::video_frame rs_rgb = _frames.get_color_frame();

      // Copy RGB frame data.
      if (rgb != nullptr) {
        memcpy(rgb, rs_rgb.get_data(),
            _rs_rgb_w * _rs_rgb_h * sizeof(uchar3));
      }

      // Copy depth frame data.
      if (_depth_scale == 0.001) {
        // The depth is already in millimeters. mm2metersKernel() will then
        // convert it to meters.
        memcpy(depth, rs_depth.get_data(),
            _rs_depth_w * _rs_depth_h * sizeof(uint16_t));
      } else {
        const uint16_t* raw_depth_data = (uint16_t*) rs_depth.get_data();
        // Convert the depth to millimeters.
#pragma omp parallel for
        for (int p = 0; p < _rs_depth_w * _rs_depth_h; ++p) {
          depth[p] = 1000.f * _depth_scale * raw_depth_data[p];
        }
      }

      _frame++;
      return true;
    }



    Eigen::Vector4f getK() {
      return _K;
    }



    uint2 getinputSize() {
      return _input_size;
    }



    void restart() {
      _frame = -1;
    }



    ReaderType getType() {
      return READER_REALSENSE;
    }
};
#else
/**
 * Dummy class for when the RealSense2 library is not available. It does
 * nothing.
 */
class RealSenseDepthReader: public DepthReader {
  public:
    /**
     * Constructor using the ::ReaderConfiguration struct.
     */
    RealSenseDepthReader(const ReaderConfiguration& config) {
      std::cerr << "RealSense2 library not found\n";
      cameraOpen = false;
      cameraActive = false;
    }

    bool readNextDepthFrame(float* ) {
      return false;
    }

    bool readNextDepthFrame(uchar3* , unsigned short int* ) {
      return false;
    }

    Eigen::Vector4f getK() {
      return Eigen::Vector4f::Constant(0.f);
    }

    uint2 getinputSize() {
      return make_uint2(0);
    }

    void restart() {
    }

    ReaderType getType() {
      return (READER_REALSENSE);
    }
};
#endif // USE_REALSENSE

#endif /* INTERFACE_H_ */

