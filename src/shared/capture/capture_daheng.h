/*
 * capture_daheng.h
 *
 *  Created on: Nov 21, 2016
 *      Author: root
 */

#ifndef CAPTURE_DAHENG_H_
#define CAPTURE_DAHENG_H_

#include <sys/time.h>

#include <QMutex>

#include "DxImageProc.h"
#include "GxIAPI.h"
#include "TimeSync.h"
#include "VarTypes.h"
#include "captureinterface.h"

class DahengInitManager {
 public:
  static void register_capture();
  static void unregister_capture();
  // private:
  static int count;
};

class CaptureDaheng : public QObject, public CaptureInterface {
 public:
  Q_OBJECT

 public slots:
  void changed(VarType* group);

 private:
  QMutex mutex;

 public:
  CaptureDaheng(VarList* _settings = 0, int default_camera_id = 0, QObject* parent = 0);
  void mvc_connect(VarList* group);
  ~CaptureDaheng();

  bool startCapture();

  bool stopCapture();

  bool isCapturing() { return is_capturing; };

  RawImage getFrame();

  void releaseFrame();

  string getCaptureMethodName() const;

  bool copyAndConvertFrame(const RawImage& src, RawImage& target);

  void readAllParameterValues();

  void writeParameterValues(VarList* varList);

 private:
  bool is_capturing;
  TimeSync timeSync;
  bool ignore_capture_failure;

  GX_DEV_HANDLE g_hDevice;
  int64_t g_nPayloadSize = 0;
  unsigned char* g_pRGBImageBuf = NULL;  ///< Memory for RAW8toRGB24
  unsigned char* g_pRaw8Image = NULL;    ///< Memory for RAW16toRAW8

  unsigned int current_id;
  unsigned char* last_buf;

  // freq should always be 125 MHz for Daheng-ace-1300-75gc
  int camera_frequency = 125e6;
  VarList* vars;
  VarInt* v_camera_id;
  VarDouble* v_framerate;
  VarInt* v_balance_ratio_red;
  VarInt* v_balance_ratio_green;
  VarInt* v_balance_ratio_blue;
  VarBool* v_auto_gain;
  VarInt* v_gain;
  VarBool* v_gamma_enable;
  VarDouble* v_gamma;
  VarDouble* v_black_level;
  VarBool* v_auto_exposure;
  VarDouble* v_manual_exposure;
  VarStringEnum* v_color_mode;

  void PreForAcquisition();
  void UnPreForAcquisition();

  void resetCamera(unsigned int new_id);
  bool _stopCapture();
  bool _buildCamera();

  // A slight blur helps to reduce noise and improve color recognition.
  static const double blur_sigma;
  void gaussianBlur(RawImage& img);
  void contrast(RawImage& img, double factor);
  void sharpen(RawImage& img);
};

#endif
