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
  PGX_FRAME_BUFFER pFrameBuffer = NULL;
  unsigned char* g_pRGBImageBuf = NULL;  ///< Memory for RAW8toRGB24
  unsigned char* g_pRaw8Image = NULL;    ///< Memory for RAW16toRAW8
  int64_t g_i64ColorFilter = GX_COLOR_FILTER_NONE;
  long nColorCorrectionParam = 0;
  unsigned char* pGammaLut = NULL;
  unsigned char* pContrastLut = NULL;

  unsigned int current_id;
  unsigned char* last_buf;

  VarList* vars;
  VarInt* v_camera_id;
  VarInt* v_binning;
  VarInt* v_packet_size;
  VarInt* v_packet_delay;
  VarDouble* v_framerate;
  VarBool* v_auto_balance;
  VarInt* v_auto_balance_roi_width;
  VarInt* v_auto_balance_roi_height;
  VarInt* v_auto_balance_roi_offset_x;
  VarInt* v_auto_balance_roi_offset_y;
  VarInt* v_man_balance_ratio_red;
  VarInt* v_man_balance_ratio_green;
  VarInt* v_man_balance_ratio_blue;
  VarBool* v_auto_gain;
  VarInt* v_gain;
  VarBool* v_auto_black_level;
  VarDouble* v_black_level;
  VarBool* v_auto_exposure;
  VarDouble* v_manual_exposure;

  void PreForAcquisition();
  void UnPreForAcquisition();
  int PixelFormatConvert(unsigned char* imgBuf, int32_t nWidth, int32_t nHeight);

  void resetCamera(unsigned int new_id);
  bool _stopCapture();
  bool _buildCamera();
  bool _setupImageImprovements();
  void _releaseLuts();

  // A slight blur helps to reduce noise and improve color recognition.
  static const double blur_sigma;
  void gaussianBlur(RawImage& img);
  void contrast(RawImage& img, double factor);
  void sharpen(RawImage& img);
};

#endif
