/*
 * capture_daheng.cpp
 *
 *  Created on: Nov 21, 2016
 *      Author: root
 */

#include "capture_daheng.h"

#include <QDebug>
#include <chrono>
#include <string>
#include <vector>

#define MUTEX_LOCK mutex.lock()
#define MUTEX_UNLOCK mutex.unlock()

#define ACQ_BUFFER_NUM 5               ///< Acquisition Buffer Qty.
#define ACQ_TRANSFER_SIZE (64 * 1024)  ///< Size of data transfer block
#define ACQ_TRANSFER_NUMBER_URB 64     ///< Qty. of data transfer block

int DahengInitManager::count = 0;

void GetErrorString(GX_STATUS emErrorStatus);

int CaptureDaheng::PixelFormatConvert() {
  VxInt32 emDXStatus = DX_OK;

  // Convert RAW8 or RAW16 image to RGB24 image
  switch (pFrameBuffer->nPixelFormat) {
    case GX_PIXEL_FORMAT_BAYER_GR8:
    case GX_PIXEL_FORMAT_BAYER_RG8:
    case GX_PIXEL_FORMAT_BAYER_GB8:
    case GX_PIXEL_FORMAT_BAYER_BG8: {
      // Convert to the RGB image
      emDXStatus =
          DxRaw8toRGB24((unsigned char *)pFrameBuffer->pImgBuf, g_pRGBImageBuf, pFrameBuffer->nWidth,
                        pFrameBuffer->nHeight, RAW2RGB_NEIGHBOUR, DX_PIXEL_COLOR_FILTER(g_i64ColorFilter), false);
      if (emDXStatus != DX_OK) {
        return -1;
      }
      break;
    }
    case GX_PIXEL_FORMAT_BAYER_GR10:
    case GX_PIXEL_FORMAT_BAYER_RG10:
    case GX_PIXEL_FORMAT_BAYER_GB10:
    case GX_PIXEL_FORMAT_BAYER_BG10:
    case GX_PIXEL_FORMAT_BAYER_GR12:
    case GX_PIXEL_FORMAT_BAYER_RG12:
    case GX_PIXEL_FORMAT_BAYER_GB12:
    case GX_PIXEL_FORMAT_BAYER_BG12: {
      // Convert to the Raw8 image
      emDXStatus = DxRaw16toRaw8((unsigned char *)pFrameBuffer->pImgBuf, g_pRaw8Image, pFrameBuffer->nWidth,
                                 pFrameBuffer->nHeight, DX_BIT_2_9);
      if (emDXStatus != DX_OK) {
        return -1;
      }
      // Convert to the RGB24 image
      emDXStatus = DxRaw8toRGB24(g_pRaw8Image, g_pRGBImageBuf, pFrameBuffer->nWidth, pFrameBuffer->nHeight,
                                 RAW2RGB_NEIGHBOUR, DX_PIXEL_COLOR_FILTER(g_i64ColorFilter), false);
      if (emDXStatus != DX_OK) {
        return -1;
      }
      break;
    }
    default: {
      return -1;
    }
  }
  return 0;
}

//-------------------------------------------------
/**
\brief Allocate the memory for pixel format transform
\return void
*/
//-------------------------------------------------
void CaptureDaheng::PreForAcquisition() {
  g_pRGBImageBuf = new unsigned char[g_nPayloadSize * 3];
  g_pRaw8Image = new unsigned char[g_nPayloadSize];

  return;
}

//-------------------------------------------------
/**
\brief Release the memory allocated
\return void
*/
//-------------------------------------------------
void CaptureDaheng::UnPreForAcquisition() {
  // Release resources
  if (g_pRaw8Image != NULL) {
    delete[] g_pRaw8Image;
    g_pRaw8Image = NULL;
  }
  if (g_pRGBImageBuf != NULL) {
    delete[] g_pRGBImageBuf;
    g_pRGBImageBuf = NULL;
  }

  return;
}

void DahengInitManager::register_capture() {
  if (count++ == 0) {
    GX_STATUS emStatus = GXInitLib();
    if (emStatus != GX_STATUS_SUCCESS) {
      GetErrorString(emStatus);
    }
  }
}

void DahengInitManager::unregister_capture() {
  if (--count == 0) {
    // Release libary
    GX_STATUS emStatus = GXCloseLib();
    if (emStatus != GX_STATUS_SUCCESS) {
      GetErrorString(emStatus);
    }
  }
}

CaptureDaheng::CaptureDaheng(VarList *_settings, int default_camera_id, QObject *parent)
    : QObject(parent), CaptureInterface(_settings) {
  is_capturing = false;
  g_hDevice = nullptr;
  ignore_capture_failure = false;
  last_buf = nullptr;

  settings->addChild(vars = new VarList("Capture Settings"));
  settings->removeFlags(VARTYPE_FLAG_HIDE_CHILDREN);
  vars->removeFlags(VARTYPE_FLAG_HIDE_CHILDREN);

  vars->addChild(v_camera_id = new VarInt("Camera ID", default_camera_id, 0, 10));

  v_binning = new VarInt("Binning (downscaling)", 0, 0, 4);
  vars->addChild(v_binning);

  v_packet_size = new VarInt("Packet Size", 8192, 0, 9000);
  vars->addChild(v_packet_size);

  v_packet_delay = new VarInt("Packet Delay", 0, 0, 100000);
  vars->addChild(v_packet_delay);

  v_framerate = new VarDouble("Max Framerate", 100.0, 0.0, 100.0);
  vars->addChild(v_framerate);

  v_auto_balance = new VarBool("Auto Balance", false);
  vars->addChild(v_auto_balance);

  v_auto_balance_roi_width = new VarInt("Auto Balance ROI Width", 0, 0);
  vars->addChild(v_auto_balance_roi_width);

  v_auto_balance_roi_height = new VarInt("Auto Balance ROI Height", 0, 0);
  vars->addChild(v_auto_balance_roi_height);

  v_auto_balance_roi_offset_x = new VarInt("Auto Balance ROI Offset X", 0);
  vars->addChild(v_auto_balance_roi_offset_x);

  v_auto_balance_roi_offset_y = new VarInt("Auto Balance ROI Offset Y", 0);
  vars->addChild(v_auto_balance_roi_offset_y);

  v_man_balance_ratio_red = new VarInt("Balance Ratio Red", 64, 0, 255);
  vars->addChild(v_man_balance_ratio_red);

  v_man_balance_ratio_green = new VarInt("Balance Ratio Green", 64, 0, 255);
  vars->addChild(v_man_balance_ratio_green);

  v_man_balance_ratio_blue = new VarInt("Balance Ratio Blue", 64, 0, 255);
  vars->addChild(v_man_balance_ratio_blue);

  v_auto_gain = new VarBool("Auto Gain", false);
  vars->addChild(v_auto_gain);

  v_gain = new VarInt("Gain", 0, 0, 255);
  vars->addChild(v_gain);

  v_auto_black_level = new VarBool("Auto Black Level", false);
  vars->addChild(v_auto_black_level);

  v_black_level = new VarDouble("Black Level", 64, 0, 1000);
  vars->addChild(v_black_level);

  v_auto_exposure = new VarBool("Auto Exposure", false);
  vars->addChild(v_auto_exposure);

  v_manual_exposure = new VarDouble("Manual Exposure (μs)", 100, 0, 30000);
  vars->addChild(v_manual_exposure);

  current_id = 0;

  mvc_connect(settings);
  mvc_connect(vars);
}

CaptureDaheng::~CaptureDaheng() { vars->deleteAllChildren(); }

bool CaptureDaheng::_buildCamera() {
  DahengInitManager::register_capture();
  current_id = v_camera_id->get();

  GX_STATUS emStatus = GXOpenDeviceByIndex(current_id, &g_hDevice);
  if (emStatus != GX_STATUS_SUCCESS) {
    GetErrorString(emStatus);
    return false;
  }

  emStatus = GXGetInt(g_hDevice, GX_INT_PAYLOAD_SIZE, &g_nPayloadSize);
  if (emStatus != GX_STATUS_SUCCESS) {
    GetErrorString(emStatus);
    return false;
  }

  // Set acquisition mode
  emStatus = GXSetEnum(g_hDevice, GX_ENUM_ACQUISITION_MODE, GX_ACQ_MODE_CONTINUOUS);
  if (emStatus != GX_STATUS_SUCCESS) {
    GetErrorString(emStatus);
    return false;
  }

  // Set trigger mode
  emStatus = GXSetEnum(g_hDevice, GX_ENUM_TRIGGER_MODE, GX_TRIGGER_MODE_OFF);
  if (emStatus != GX_STATUS_SUCCESS) {
    GetErrorString(emStatus);
    return false;
  }

  // Set buffer quantity of acquisition queue
  uint64_t nBufferNum = ACQ_BUFFER_NUM;
  emStatus = GXSetAcqusitionBufferNumber(g_hDevice, nBufferNum);
  if (emStatus != GX_STATUS_SUCCESS) {
    GetErrorString(emStatus);
    return false;
  }

  // Get color filter
  emStatus = GXGetEnum(g_hDevice, GX_ENUM_PIXEL_COLOR_FILTER, &g_i64ColorFilter);
  if (emStatus != GX_STATUS_SUCCESS) {
    GetErrorString(emStatus);
    return false;
  }

  // Allocate the memory for pixel format transform
  PreForAcquisition();

  // Device start acquisition
  emStatus = GXStreamOn(g_hDevice);
  if (emStatus != GX_STATUS_SUCCESS) {
    GetErrorString(emStatus);
    return false;
  }

  is_capturing = true;
  return true;
}

bool CaptureDaheng::startCapture() {
  MUTEX_LOCK;
  if (g_hDevice == nullptr) {
    if (!_buildCamera()) {
      printf("Failed to build Daheng camera\n");
      MUTEX_UNLOCK;
      return false;
    }
  }

  MUTEX_UNLOCK;
  return true;
}

bool CaptureDaheng::_stopCapture() {
  if (is_capturing) {
    is_capturing = false;
    return true;
  }
  return false;
}

bool CaptureDaheng::stopCapture() {
  MUTEX_LOCK;
  bool stopped;
  try {
    stopped = _stopCapture();
    if (stopped) {
      // Device stop acquisition
      GX_STATUS emStatus = GXStreamOff(g_hDevice);
      if (emStatus != GX_STATUS_SUCCESS) {
        GetErrorString(emStatus);
      }

      // Release the resources and stop acquisition thread
      UnPreForAcquisition();

      // Close device
      emStatus = GXCloseDevice(g_hDevice);
      if (emStatus != GX_STATUS_SUCCESS) {
        GetErrorString(emStatus);
      }

      delete g_hDevice;
      g_hDevice = nullptr;
      DahengInitManager::unregister_capture();
    }
  } catch (...) {
    MUTEX_UNLOCK;
    throw;
  }
  MUTEX_UNLOCK;
  return stopped;
}

void CaptureDaheng::releaseFrame() {
  MUTEX_LOCK;
  try {
    if (last_buf) {
      free(last_buf);
      last_buf = nullptr;
    }
  } catch (...) {
    MUTEX_UNLOCK;
    throw;
  }
  MUTEX_UNLOCK;
}

RawImage CaptureDaheng::getFrame() {
  MUTEX_LOCK;
  RawImage img;
  img.setWidth(0);
  img.setHeight(0);
  img.setColorFormat(COLOR_RGB8);

  GX_STATUS emStatus = GX_STATUS_SUCCESS;

  // Calls GXDQBuf to get a frame of image.
  emStatus = GXDQBuf(g_hDevice, &pFrameBuffer, 1000);
  if (emStatus == GX_STATUS_SUCCESS) {
    // Check if frame grab was succesful
    if (pFrameBuffer->nStatus == GX_FRAME_STATUS_SUCCESS) {
      // Convert to RAW8
      if (PixelFormatConvert() == 0) {
        // Copy image data to RawImage
        img.setWidth(pFrameBuffer->nWidth);
        img.setHeight(pFrameBuffer->nHeight);
        unsigned char *buf = (unsigned char *)malloc(g_nPayloadSize * 3);
        memcpy(buf, g_pRGBImageBuf, g_nPayloadSize * 3);
        img.setData(buf);
        last_buf = buf;

        // Set the timestamp
        timeval tv = {};
        gettimeofday(&tv, nullptr);
        double systemTime = (double)tv.tv_sec + (tv.tv_usec / 1000000.0);
        img.setTime(systemTime);

      } else {
        printf("Daheng color conversion failed\n");
      }
    } else {
      if (!pFrameBuffer->nStatus == GX_FRAME_STATUS_INCOMPLETE) {
        printf(
            "Daheng framegrab error: incomplete frame -- check your network connection. It's best to use wired "
            "ethernet.\n");
      } else {
        printf("Daheng framegrab failed with status %d\n", pFrameBuffer->nStatus);
      }
    }

    // Requeue the buffer
    emStatus = GXQBuf(g_hDevice, pFrameBuffer);
    if (emStatus != GX_STATUS_SUCCESS) {
      GetErrorString(emStatus);
    }
  } else {
    printf("Daheng framegrab failed with status %d\n", emStatus);
    GetErrorString(emStatus);
  }

  MUTEX_UNLOCK;
  return img;
}

string CaptureDaheng::getCaptureMethodName() const { return "Daheng"; }

bool CaptureDaheng::copyAndConvertFrame(const RawImage &src, RawImage &target) {
  MUTEX_LOCK;
  try {
    target.ensure_allocation(COLOR_RGB8, src.getWidth(), src.getHeight());
    target.setTime(src.getTime());
    target.setTimeCam(src.getTimeCam());
    memcpy(target.getData(), src.getData(), src.getNumBytes());
  } catch (...) {
    MUTEX_UNLOCK;
    throw;
  }
  MUTEX_UNLOCK;
  return true;
}

void CaptureDaheng::readAllParameterValues() {
  // TODO: read all parameter values
}

void CaptureDaheng::resetCamera(unsigned int new_id) {
  bool restart = is_capturing;
  if (restart) {
    stopCapture();
  }
  current_id = new_id;
  if (restart) {
    startCapture();
  }
}

void CaptureDaheng::writeParameterValues(VarList *varList) {
  if (varList != this->settings) {
    return;
  }
  MUTEX_LOCK;

  if (current_id != v_camera_id->get()) {
    MUTEX_UNLOCK;
    resetCamera(v_camera_id->get());  // locks itself
    MUTEX_LOCK;
  }

  if (g_hDevice != nullptr) {
    GX_STATUS status = GX_STATUS_SUCCESS;

    // Binning setting
    int binning = v_binning->get();
    if (binning <= 0) {
      // Disable binning (set averging to 1x1)
      status = GXSetEnum(g_hDevice, GX_ENUM_BINNING_HORIZONTAL_MODE, GX_BINNING_HORIZONTAL_MODE_AVERAGE);
      status = GXSetEnum(g_hDevice, GX_ENUM_BINNING_VERTICAL_MODE, GX_BINNING_HORIZONTAL_MODE_AVERAGE);
      status = GXSetInt(g_hDevice, GX_INT_BINNING_HORIZONTAL, 1);
      status = GXSetInt(g_hDevice, GX_INT_BINNING_VERTICAL, 1);
    } else {
      // Enable binning
      status = GXSetEnum(g_hDevice, GX_ENUM_BINNING_HORIZONTAL_MODE, GX_BINNING_HORIZONTAL_MODE_SUM);
      status = GXSetEnum(g_hDevice, GX_ENUM_BINNING_VERTICAL_MODE, GX_BINNING_HORIZONTAL_MODE_SUM);
      status = GXSetInt(g_hDevice, GX_INT_BINNING_HORIZONTAL, binning);
      status = GXSetInt(g_hDevice, GX_INT_BINNING_VERTICAL, binning);
    }
    if (status != GX_STATUS_SUCCESS) {
      GetErrorString(status);
    }

    // Packet size setting
    status = GXSetInt(g_hDevice, GX_INT_GEV_PACKETSIZE, v_packet_size->get());
    if (status != GX_STATUS_SUCCESS) {
      GetErrorString(status);
    }

    // Packet delay setting
    status = GXSetInt(g_hDevice, GX_INT_GEV_PACKETDELAY, v_packet_delay->get());
    if (status != GX_STATUS_SUCCESS) {
      GetErrorString(status);
    }

    // Frame rate setting
    //  1. Enable the frame rate adjustment mode.
    status = GXSetEnum(g_hDevice, GX_ENUM_ACQUISITION_FRAME_RATE_MODE, GX_ACQUISITION_FRAME_RATE_MODE_ON);
    if (status == GX_STATUS_SUCCESS) {
      //  2. Set the frame rate.
      status = GXSetFloat(g_hDevice, GX_FLOAT_ACQUISITION_FRAME_RATE, v_framerate->getDouble());
      if (status != GX_STATUS_SUCCESS) {
        GetErrorString(status);
      }
    } else {
      GetErrorString(status);
    }

    // Balance ratio setting
    if (v_auto_balance->getBool()) {
      status = GXSetInt(g_hDevice, GX_INT_AWBROI_WIDTH, v_auto_balance_roi_width->get());
      status = GXSetInt(g_hDevice, GX_INT_AWBROI_HEIGHT, v_auto_balance_roi_height->get());
      status = GXSetInt(g_hDevice, GX_INT_AWBROI_OFFSETX, v_auto_balance_roi_offset_x->get());
      status = GXSetInt(g_hDevice, GX_INT_AWBROI_OFFSETY, v_auto_balance_roi_offset_y->get());
      if (status == GX_STATUS_SUCCESS) {
        status = GXSetEnum(g_hDevice, GX_ENUM_AWB_LAMP_HOUSE, GX_AWB_LAMP_HOUSE_FLUORESCENCE);
        status = GXSetEnum(g_hDevice, GX_ENUM_BALANCE_WHITE_AUTO, GX_BALANCE_WHITE_AUTO_CONTINUOUS);
        if (status != GX_STATUS_SUCCESS) {
          GetErrorString(status);
        }
      } else {
        GetErrorString(status);
      }
    } else {
      // Get range of balance ratio
      GX_FLOAT_RANGE ratioRange;
      status = GXGetFloatRange(g_hDevice, GX_FLOAT_BALANCE_RATIO, &ratioRange);

      // Red
      status = GXSetEnum(g_hDevice, GX_ENUM_BALANCE_RATIO_SELECTOR, GX_BALANCE_RATIO_SELECTOR_RED);
      if (status == GX_STATUS_SUCCESS) {
        // Remap value from [0, 255] to [min, max]
        float mapped = ((float)v_man_balance_ratio_red->get() / 255.0f) * ratioRange.dMax + ratioRange.dMin;
        status = GXSetFloat(g_hDevice, GX_FLOAT_BALANCE_RATIO, mapped);
        if (status != GX_STATUS_SUCCESS) {
          GetErrorString(status);
        }
      } else {
        GetErrorString(status);
      }

      // Green
      status = GXSetEnum(g_hDevice, GX_ENUM_BALANCE_RATIO_SELECTOR, GX_BALANCE_RATIO_SELECTOR_GREEN);
      if (status == GX_STATUS_SUCCESS) {
        // Remap value from [0, 255] to [min, max]
        float mapped = ((float)v_man_balance_ratio_green->get() / 255.0f) * ratioRange.dMax + ratioRange.dMin;
        status = GXSetFloat(g_hDevice, GX_FLOAT_BALANCE_RATIO, mapped);
        if (status != GX_STATUS_SUCCESS) {
          GetErrorString(status);
        }
      } else {
        GetErrorString(status);
      }

      // Blue
      status = GXSetEnum(g_hDevice, GX_ENUM_BALANCE_RATIO_SELECTOR, GX_BALANCE_RATIO_SELECTOR_BLUE);
      if (status == GX_STATUS_SUCCESS) {
        // Remap value from [0, 255] to [min, max]
        float mapped = ((float)v_man_balance_ratio_blue->get() / 255.0f) * ratioRange.dMax + ratioRange.dMin;
        status = GXSetFloat(g_hDevice, GX_FLOAT_BALANCE_RATIO, mapped);
        if (status != GX_STATUS_SUCCESS) {
          GetErrorString(status);
        }
      } else {
        GetErrorString(status);
      }
    }

    // Gain setting
    if (v_auto_gain->getBool()) {
      status = GXSetEnum(g_hDevice, GX_ENUM_GAIN_AUTO, GX_GAIN_AUTO_CONTINUOUS);
      if (status != GX_STATUS_SUCCESS) {
        GetErrorString(status);
      }
    } else {
      status = GXSetEnum(g_hDevice, GX_ENUM_GAIN_AUTO, GX_GAIN_AUTO_OFF);
      status = GXSetEnum(g_hDevice, GX_ENUM_GAIN_SELECTOR, GX_GAIN_SELECTOR_ALL);
      if (status != GX_STATUS_SUCCESS) {
        GetErrorString(status);
      }

      GX_FLOAT_RANGE gainRange;
      status = GXGetFloatRange(g_hDevice, GX_FLOAT_GAIN, &gainRange);
      float mapped = ((float)v_gain->get() / 255.0f) * gainRange.dMax + gainRange.dMin;
      status = GXSetFloat(g_hDevice, GX_FLOAT_GAIN, mapped);
      if (status != GX_STATUS_SUCCESS) {
        GetErrorString(status);
      }
    }

    // Black level setting
    if (v_auto_black_level->getBool()) {
      status = GXSetEnum(g_hDevice, GX_ENUM_BLACKLEVEL_AUTO, GX_BLACKLEVEL_AUTO_CONTINUOUS);
      if (status != GX_STATUS_SUCCESS) {
        GetErrorString(status);
      }
    } else {
      status = GXSetEnum(g_hDevice, GX_ENUM_BLACKLEVEL_AUTO, GX_BLACKLEVEL_AUTO_OFF);
      status = GXSetEnum(g_hDevice, GX_ENUM_BLACKLEVEL_SELECTOR, GX_BLACKLEVEL_SELECTOR_ALL);
      if (status != GX_STATUS_SUCCESS) {
        GetErrorString(status);
      }

      GX_FLOAT_RANGE blackLevelRange;
      status = GXGetFloatRange(g_hDevice, GX_FLOAT_BLACKLEVEL, &blackLevelRange);
      float mapped = ((float)v_black_level->getDouble() / 1000.0f) * blackLevelRange.dMax + blackLevelRange.dMin;
      status = GXSetFloat(g_hDevice, GX_FLOAT_BLACKLEVEL, mapped);
      if (status != GX_STATUS_SUCCESS) {
        GetErrorString(status);
      }
    }

    // Exposure setting
    if (v_auto_exposure->getBool()) {
      status = GXSetEnum(g_hDevice, GX_ENUM_EXPOSURE_AUTO, GX_EXPOSURE_AUTO_CONTINUOUS);
      if (status != GX_STATUS_SUCCESS) {
        GetErrorString(status);
      }
    } else {
      status = GXSetEnum(g_hDevice, GX_ENUM_EXPOSURE_AUTO, GX_EXPOSURE_AUTO_OFF);
      GX_FLOAT_RANGE exposureRange;
      status = GXGetFloatRange(g_hDevice, GX_FLOAT_EXPOSURE_TIME, &exposureRange);
      float mapped = ((float)v_manual_exposure->getDouble() / 30000.0f) * exposureRange.dMax + exposureRange.dMin;
      status = GXSetFloat(g_hDevice, GX_FLOAT_EXPOSURE_TIME, mapped);
      if (status != GX_STATUS_SUCCESS) {
        GetErrorString(status);
      }
    }
  }

  MUTEX_UNLOCK;
}

void CaptureDaheng::mvc_connect(VarList *group) {
  vector<VarType *> v = group->getChildren();
  for (auto &i : v) {
    connect(i, SIGNAL(wasEdited(VarType *)), group, SLOT(mvcEditCompleted()));
  }
  connect(group, SIGNAL(wasEdited(VarType *)), this, SLOT(changed(VarType *)));
}

void CaptureDaheng::changed(VarType *group) {
  if (group->getType() == VARTYPE_ID_LIST) {
    writeParameterValues(dynamic_cast<VarList *>(group));
  }
}

//----------------------------------------------------------------------------------
/**
\brief  Get description of input error code
\param  emErrorStatus  error code

\return void
*/
//----------------------------------------------------------------------------------
void GetErrorString(GX_STATUS emErrorStatus) {
  char *error_info = NULL;
  size_t size = 0;
  GX_STATUS emStatus = GX_STATUS_SUCCESS;

  // Get length of error description
  emStatus = GXGetLastError(&emErrorStatus, NULL, &size);
  if (emStatus != GX_STATUS_SUCCESS) {
    printf("<Error when calling GXGetLastError>\n");
    return;
  }

  // Alloc error resources
  error_info = new char[size];
  if (error_info == NULL) {
    printf("<Failed to allocate memory>\n");
    return;
  }

  // Get error description
  emStatus = GXGetLastError(&emErrorStatus, error_info, &size);
  if (emStatus != GX_STATUS_SUCCESS) {
    printf("<Error when calling GXGetLastError>\n");
  } else {
    printf("%s\n", error_info);
  }

  // Realease error resources
  if (error_info != NULL) {
    delete[] error_info;
    error_info = NULL;
  }
}
