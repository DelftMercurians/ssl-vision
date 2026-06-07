/*
 * capture_basler.cpp
 *
 *  Created on: Nov 21, 2016
 *      Author: root
 */

#include "capture_basler.h"

#include <string>
#include <vector>

#define MUTEX_LOCK mutex.lock()
#define MUTEX_UNLOCK mutex.unlock()

int BaslerInitManager::count = 0;

void BaslerInitManager::register_capture() {
  if (count++ == 0) {
    Pylon::PylonInitialize();
  }
}

void BaslerInitManager::unregister_capture() {
  if (--count == 0) {
    Pylon::PylonTerminate();
  }
}

CaptureBasler::CaptureBasler(VarList* _settings, int default_camera_id, QObject* parent)
    : QObject(parent), CaptureInterface(_settings) {
  is_capturing = false;
  camera = nullptr;
  ignore_capture_failure = false;
  converter.OutputPixelFormat = Pylon::PixelType_RGB8packed;
  // camera.PixelFormat.SetValue(Pylon::PixelFormat_YUV422Packed, true);
  last_buf = nullptr;

  settings->addChild(vars = new VarList("Capture Settings"));
  settings->removeFlags(VARTYPE_FLAG_HIDE_CHILDREN);
  vars->removeFlags(VARTYPE_FLAG_HIDE_CHILDREN);

  vars->addChild(v_camera_id = new VarInt("Camera ID", default_camera_id, 0, 3));

  v_framerate = new VarDouble("Max Framerate", 100.0, 0.0, 100.0);
  vars->addChild(v_framerate);

  v_auto_gain = new VarStringEnum("Auto Gain", "Off");
  v_auto_gain->addItem("Off");
  v_auto_gain->addItem("Once");
  v_auto_gain->addItem("Continuous");
  vars->addChild(v_auto_gain);

  v_gain = new VarInt("Gain", 2, 0, 48);
  vars->addChild(v_gain);

  v_auto_exposure = new VarStringEnum("Auto Exposure", "Off");
  v_auto_exposure->addItem("Off");
  v_auto_exposure->addItem("Once");
  v_auto_exposure->addItem("Continuous");
  vars->addChild(v_auto_exposure);

  v_manual_exposure = new VarDouble("Manual Exposure (μs)", 10000, 19, 30000);
  vars->addChild(v_manual_exposure);

  v_whitebalance_mode = new VarStringEnum("Auto Whitebalance", "Off");
  v_whitebalance_mode->addItem("Off");
  v_whitebalance_mode->addItem("Once");
  v_whitebalance_mode->addItem("Continuous");
  vars->addChild(v_whitebalance_mode);

  v_pixel_format = new VarStringEnum("Pixel Format", "BayerRG8");
  v_pixel_format->addItem("Mono8");
  v_pixel_format->addItem("Mono10");
  v_pixel_format->addItem("Mono10p");
  v_pixel_format->addItem("Mono12");
  v_pixel_format->addItem("Mono12p");
  v_pixel_format->addItem("RGB8");
  v_pixel_format->addItem("BGR8");
  v_pixel_format->addItem("YCbCr422_8");
  v_pixel_format->addItem("BayerRG8");
  v_pixel_format->addItem("BayerRG10");
  v_pixel_format->addItem("BayerRG10p");
  v_pixel_format->addItem("BayerRG12");
  v_pixel_format->addItem("BayerRG12p");
  vars->addChild(v_pixel_format);

  v_width = new VarInt("Width", 1920, 2, 1936);
  vars->addChild(v_width);
  v_height = new VarInt("Height", 1200, 2, 1216);
  vars->addChild(v_height);
  v_offset_x = new VarInt("Offset X", 0, 0, 1934);
  vars->addChild(v_offset_x);
  v_offset_y = new VarInt("Offset Y", 0, 0, 1214);
  vars->addChild(v_offset_y);

  current_id = 0;

  mvc_connect(settings);
  mvc_connect(vars);
}

CaptureBasler::~CaptureBasler() { vars->deleteAllChildren(); }

bool CaptureBasler::_buildCamera() {
  BaslerInitManager::register_capture();
  Pylon::DeviceInfoList devices;
  int amt = Pylon::CTlFactory::GetInstance().EnumerateDevices(devices);
  current_id = v_camera_id->get();
  std::cout << "[BASLER] Selected camera ID: " << std::to_string(current_id) << std::endl;
  std::cout << "[BASLER] Number of available cameras: " << std::to_string(amt) << std::endl;
  if (amt > current_id) {
    Pylon::CDeviceInfo info = devices[current_id];
    std::cout << "[BASLER] Camera User Defined Name: " << info.GetUserDefinedName() << std::endl;

    camera = new Pylon::CBaslerUniversalInstantCamera(Pylon::CTlFactory::GetInstance().CreateDevice(info));
    std::cout << "[BASLER] Opening camera..." << std::endl;
    camera->Open();
    // camera->GammaSelector.SetValue(Pylon::GammaSelector_User); //Necessary for interface to work
    camera->AcquisitionFrameRateEnable.SetValue(true);  // Turn on capped framerates
    camera_frequency = camera->GevTimestampTickFrequency.GetValue();

    // let camera send timestamps and FrameCounts.
    if (GenApi::IsWritable(camera->ChunkModeActive)) {
      std::cout << "[BASLER] Setting chunk modes" << std::endl;
      camera->ChunkModeActive.SetValue(true);
      camera->ChunkSelector.SetValue(Basler_UniversalCameraParams::ChunkSelector_Timestamp);
      camera->ChunkEnable.SetValue(true);
      // camera->ChunkSelector.SetValue(Pylon::ChunkSelector_Framecounter);
      // camera->ChunkEnable.SetValue(true);
      // camera->GevTimestampControlReset.Execute(); //Reset the internal time stamp counter of the camera to 0
    } else {
      std::cout << "[BASLER] Failed, camera model does not support accurate timings!" << std::endl;
      return false;  // Camera does not support accurate timings
    }
    is_capturing = true;
    std::cout << "[BASLER] Done opening." << std::endl;
    return true;
  }
  std::cout << "[BASLER] Camera ID out of range" << std::endl;
  return false;
}

bool CaptureBasler::startCapture() {
  MUTEX_LOCK;
  try {
    if (camera == nullptr) {
      if (!_buildCamera()) {
        // Did not make a camera!
        std::cout << "[BASLER] Did not make a camera!" << std::endl;
        MUTEX_UNLOCK;
        return false;
      }
      std::cout << "[BASLER] Built camera" << std::endl;
    }

    // Set color mode
    const auto pixel_format = v_pixel_format->getString();
    if (pixel_format == "RGB8") {
      std::cout << "[BASLER] PixelFormat PixelFormat_RGB8" << std::endl;
      camera->PixelFormat.SetValue(Basler_UniversalCameraParams::PixelFormat_RGB8);
    } else if (pixel_format == "BGR8") {
      std::cout << "[BASLER] PixelFormat PixelFormat_BGR8" << std::endl;
      camera->PixelFormat.SetValue(Basler_UniversalCameraParams::PixelFormat_BGR8);
    } else if (pixel_format == "BayerRG8") {
      std::cout << "[BASLER] PixelFormat PixelFormat_BayerRG8" << std::endl;
      camera->PixelFormat.SetValue(Basler_UniversalCameraParams::PixelFormat_BayerRG8);
    } else if (pixel_format == "YCbCr422_8") {
      std::cout << "[BASLER] PixelFormat PixelFormat_YCbCr422_8" << std::endl;
      camera->PixelFormat.SetValue(Basler_UniversalCameraParams::PixelFormat_YCbCr422_8);
    } else if (pixel_format == "BayerRG10") {
      std::cout << "[BASLER] PixelFormat PixelFormat_BayerRG10" << std::endl;
      camera->PixelFormat.SetValue(Basler_UniversalCameraParams::PixelFormat_BayerRG10);
    } else if (pixel_format == "BayerRG10p") {
      std::cout << "[BASLER] PixelFormat PixelFormat_BayerRG10p" << std::endl;
      camera->PixelFormat.SetValue(Basler_UniversalCameraParams::PixelFormat_BayerRG10p);
    } else if (pixel_format == "BayerRG12") {
      std::cout << "[BASLER] PixelFormat PixelFormat_BayerRG12" << std::endl;
      camera->PixelFormat.SetValue(Basler_UniversalCameraParams::PixelFormat_BayerRG12);
    } else if (pixel_format == "BayerRG12p") {
      std::cout << "[BASLER] PixelFormat PixelFormat_BayerRG12p" << std::endl;
      camera->PixelFormat.SetValue(Basler_UniversalCameraParams::PixelFormat_BayerRG12p);
    } else if (pixel_format == "Mono8") {
      std::cout << "[BASLER] PixelFormat PixelFormat_Mono8" << std::endl;
      camera->PixelFormat.SetValue(Basler_UniversalCameraParams::PixelFormat_Mono8);
    } else if (pixel_format == "Mono10") {
      std::cout << "[BASLER] PixelFormat PixelFormat_Mono10" << std::endl;
      camera->PixelFormat.SetValue(Basler_UniversalCameraParams::PixelFormat_Mono10);
    } else if (pixel_format == "Mono10p") {
      std::cout << "[BASLER] PixelFormat PixelFormat_Mono10p" << std::endl;
      camera->PixelFormat.SetValue(Basler_UniversalCameraParams::PixelFormat_Mono10p);
    } else if (pixel_format == "Mono12") {
      std::cout << "[BASLER] PixelFormat PixelFormat_Mono12" << std::endl;
      camera->PixelFormat.SetValue(Basler_UniversalCameraParams::PixelFormat_Mono12);
    } else if (pixel_format == "Mono12p") {
      std::cout << "[BASLER] PixelFormat PixelFormat_Mono12p" << std::endl;
      camera->PixelFormat.SetValue(Basler_UniversalCameraParams::PixelFormat_Mono12p);
    }

    // Set framing
    std::cout << "[BASLER] Width " << std::to_string(v_width->getInt()) << std::endl;
    camera->Width.SetValue(v_width->getInt());

    std::cout << "[BASLER] Height " << std::to_string(v_height->getInt()) << std::endl;
    camera->Height.SetValue(v_height->getInt());

    std::cout << "[BASLER] OffsetX " << std::to_string(v_offset_x->getInt()) << std::endl;
    camera->OffsetX.SetValue(v_offset_x->getInt());

    std::cout << "[BASLER] OffsetY " << std::to_string(v_offset_y->getInt()) << std::endl;
    camera->OffsetY.SetValue(v_offset_y->getInt());

    camera->StartGrabbing(Pylon::GrabStrategy_LatestImageOnly);
    std::cout << "[BASLER] Started Grabbing" << std::endl;
  } catch (Pylon::GenericException& e) {
    std::cout << "[BASLER] Pylon exception: " << e.what() << std::endl;
    delete camera;
    camera = nullptr;
    current_id = -1;
    MUTEX_UNLOCK;
    return false;
  } catch (...) {
    std::cout << "[BASLER] Uncaught exception at line 148 " << std::endl;
    MUTEX_UNLOCK;
    throw;
  }
  MUTEX_UNLOCK;
  std::cout << "[BASLER] startCapture() Done" << std::endl;
  return true;
}

bool CaptureBasler::_stopCapture() {
  if (is_capturing) {
    camera->StopGrabbing();
    camera->Close();
    is_capturing = false;
    return true;
  }
  return false;
}

bool CaptureBasler::stopCapture() {
  MUTEX_LOCK;
  bool stopped;
  try {
    stopped = _stopCapture();
    if (stopped) {
      delete camera;
      camera = nullptr;
      BaslerInitManager::unregister_capture();
    }
  } catch (...) {
    MUTEX_UNLOCK;
    throw;
  }
  MUTEX_UNLOCK;
  return stopped;
}

void CaptureBasler::releaseFrame() {
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

RawImage CaptureBasler::getFrame() {
  MUTEX_LOCK;
  RawImage img;
  img.setWidth(0);
  img.setHeight(0);
  img.setColorFormat(COLOR_RGB8);
  try {
    // Keep grabbing in case of partial grabs
    int fail_count = 0;
    while (fail_count < 10 && (!grab_result || !grab_result->GrabSucceeded())) {
      try {
        // std::cout << "[BASLER] retrieving frame" << std::endl;
        camera->RetrieveResult(1000, grab_result, Pylon::TimeoutHandling_ThrowException);
      } catch (Pylon::TimeoutException& e) {
        std::cerr << "[BASLER] Timeout expired in CaptureBasler::getFrame: " << e.what() << std::endl;
        MUTEX_UNLOCK;
        return img;
      }
      if (!grab_result) {
        fail_count++;
        continue;
      }
      if (!grab_result->GrabSucceeded()) {
        fail_count++;
        std::cerr << "[BASLER] Image grab failed in CaptureBasler::getFrame: " << grab_result->GetErrorDescription()
                  << std::endl;
      }
    }
    if (fail_count == 10) {
      std::cerr << "[BASLER] Maximum retry count for image grabbing (" << fail_count << ") exceeded in capture_basler"
                << std::endl;
      MUTEX_UNLOCK;
      return img;
    }

    Pylon::CPylonImage capture;

    // Convert to RGB8 format
    converter.Convert(capture, grab_result);

    img.setWidth(capture.GetWidth());
    img.setHeight(capture.GetHeight());
    unsigned char* buf = (unsigned char*)malloc(capture.GetImageSize());
    memcpy(buf, capture.GetBuffer(), capture.GetImageSize());
    img.setData(buf);
    last_buf = buf;

    if (grab_result->GetPayloadType() == Pylon::PayloadType_ChunkData &&
        GenApi::IsReadable(grab_result->ChunkTimestamp)) {
      double period = 1e9 / camera_frequency;
      uint64_t image_timestamp = period * grab_result->ChunkTimestamp.GetValue();
      timeSync.update(image_timestamp);
      double time = timeSync.sync(image_timestamp) / 1e9;
      img.setTime(time);
    } else {
      timeval tv = {};
      gettimeofday(&tv, nullptr);
      double systemTime = (double)tv.tv_sec + (tv.tv_usec / 1000000.0);
      img.setTime(systemTime);
    }

    // Original buffer is not needed anymore, it has been copied to img
    grab_result.Release();
  } catch (Pylon::GenericException& e) {
    std::cerr << "[BASLER] Exception while grabbing a frame: " << e.what() << std::endl;
    MUTEX_UNLOCK;
    throw;
  } catch (...) {
    // Make sure the mutex is unlocked before propagating
    std::cerr << "[BASLER] Uncaught exception!" << std::endl;
    MUTEX_UNLOCK;
    throw;
  }
  MUTEX_UNLOCK;
  return img;
}

string CaptureBasler::getCaptureMethodName() const { return "Basler"; }

bool CaptureBasler::copyAndConvertFrame(const RawImage& src, RawImage& target) {
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

void CaptureBasler::readAllParameterValues() {
  MUTEX_LOCK;
  try {
    if (!camera) return;
    bool was_open = camera->IsOpen();
    if (!was_open) {
      camera->Open();
    }

    // std::cout << "[BASLER] Read AcquisitionFrameRate" << std::endl;
    v_framerate->setDouble(camera->AcquisitionFrameRate.GetValue());

    const auto pixelformat = camera->PixelFormat.GetValue();
    switch (pixelformat) {
      case Basler_UniversalCameraParams::PixelFormat_Mono8:
        v_pixel_format->setString("Mono8");
        break;
      case Basler_UniversalCameraParams::PixelFormat_Mono10:
        v_pixel_format->setString("Mono10");
        break;
      case Basler_UniversalCameraParams::PixelFormat_Mono10p:
        v_pixel_format->setString("Mono10p");
        break;
      case Basler_UniversalCameraParams::PixelFormat_Mono12:
        v_pixel_format->setString("Mono12");
        break;
      case Basler_UniversalCameraParams::PixelFormat_Mono12p:
        v_pixel_format->setString("Mono12p");
        break;
      case Basler_UniversalCameraParams::PixelFormat_BayerRG8:
        v_pixel_format->setString("BayerRG8");
        break;
      case Basler_UniversalCameraParams::PixelFormat_BayerRG10:
        v_pixel_format->setString("BayerRG10");
        break;
      case Basler_UniversalCameraParams::PixelFormat_BayerRG10p:
        v_pixel_format->setString("BayerRG10p");
        break;
      case Basler_UniversalCameraParams::PixelFormat_BayerRG12:
        v_pixel_format->setString("BayerRG12");
        break;
      case Basler_UniversalCameraParams::PixelFormat_BayerRG12p:
        v_pixel_format->setString("BayerRG12p");
        break;
      case Basler_UniversalCameraParams::PixelFormat_RGB8:
        v_pixel_format->setString("RGB8");
        break;
      case Basler_UniversalCameraParams::PixelFormat_BGR8:
        v_pixel_format->setString("BGR8");
        break;
      case Basler_UniversalCameraParams::PixelFormat_YCbCr422_8:
        v_pixel_format->setString("YCbCr422_8");
        break;
      default:
        break;
    }

    // std::cout << "[BASLER] Read BalanceWhiteAuto" << std::endl;
    const auto whitebalanceauto = camera->BalanceWhiteAuto.GetValue();
    if (whitebalanceauto == Basler_UniversalCameraParams::BalanceWhiteAuto_Off) {
      // std::cout << "[BASLER] BalanceWhiteAuto BalanceWhiteAuto_Off" << std::endl;
      v_whitebalance_mode->selectIndex(0);
    } else if (whitebalanceauto == Basler_UniversalCameraParams::BalanceWhiteAuto_Once) {
      // std::cout << "[BASLER] BalanceWhiteAuto BalanceWhiteAuto_Once" << std::endl;
      v_whitebalance_mode->selectIndex(1);
    } else if (whitebalanceauto == Basler_UniversalCameraParams::BalanceWhiteAuto_Continuous) {
      // std::cout << "[BASLER] BalanceWhiteAuto BalanceWhiteAuto_Continuous" << std::endl;
      v_whitebalance_mode->selectIndex(2);
    }

    // std::cout << "[BASLER] Read GainAuto" << std::endl;
    const auto gainauto = camera->GainAuto.GetValue();
    if (gainauto == Basler_UniversalCameraParams::GainAuto_Off) {
      // std::cout << "[BASLER] GainAuto GainAuto_Off" << std::endl;
      v_auto_gain->selectIndex(0);
    } else if (gainauto == Basler_UniversalCameraParams::GainAuto_Once) {
      // std::cout << "[BASLER] GainAuto GainAuto_Once" << std::endl;
      v_auto_gain->selectIndex(1);
    } else if (gainauto == Basler_UniversalCameraParams::GainAuto_Continuous) {
      // std::cout << "[BASLER] GainAuto GainAuto_Continuous" << std::endl;
      v_auto_gain->selectIndex(2);
    }

    // std::cout << "[BASLER] Read Gain" << std::endl;
    v_gain->setDouble(camera->Gain.GetValue());

    v_width->setInt(camera->Width.GetValue());
    v_height->setInt(camera->Height.GetValue());
    v_offset_x->setInt(camera->OffsetX.GetValue());
    v_offset_y->setInt(camera->OffsetY.GetValue());

    // std::cout << "[BASLER] Read ExposureAuto" << std::endl;
    const auto exposureauto = camera->ExposureAuto.GetValue();
    if (exposureauto == Basler_UniversalCameraParams::ExposureAuto_Off) {
      // std::cout << "[BASLER] ExposureAuto ExposureAuto_Off" << std::endl;
      v_auto_exposure->selectIndex(0);
    } else if (exposureauto == Basler_UniversalCameraParams::ExposureAuto_Once) {
      // std::cout << "[BASLER] ExposureAuto ExposureAuto_Once" << std::endl;
      v_auto_exposure->selectIndex(1);
    } else if (exposureauto == Basler_UniversalCameraParams::ExposureAuto_Continuous) {
      // std::cout << "[BASLER] ExposureAuto ExposureAuto_Continuous" << std::endl;
      v_auto_exposure->selectIndex(2);
    }

    // std::cout << "[BASLER] Getting ExposureTime" << std::endl;
    v_manual_exposure->setDouble(camera->ExposureTime.GetValue());
    // std::cout << "[BASLER] Done getting" << std::endl;
  } catch (const GenICam_3_5_Basler_pylon_v1::AccessException& e) {
    std::cerr << "[BASLER] Access exception: " << e.what() << std::endl;
    MUTEX_UNLOCK;
    return;
  } catch (const Pylon::GenericException& e) {
    std::cerr << "[BASLER] Exception reading parameter values: " << e.what() << std::endl;
    MUTEX_UNLOCK;
    return;
  } catch (...) {
    MUTEX_UNLOCK;
    throw;
  }
  MUTEX_UNLOCK;
}

void CaptureBasler::resetCamera(unsigned int new_id) {
  bool restart = is_capturing;
  if (restart) {
    stopCapture();
  }
  current_id = new_id;
  if (restart) {
    startCapture();
  }
}

void CaptureBasler::writeParameterValues(VarList* varList) {
  if (varList != this->settings) {
    return;
  }
  MUTEX_LOCK;
  try {
    if (current_id != v_camera_id->get()) {
      MUTEX_UNLOCK;
      resetCamera(v_camera_id->get());  // locks itself
      MUTEX_LOCK;
    }

    if (camera != nullptr) {
      camera->Open();

      std::cout << "[BASLER] Set BalanceWhiteAuto" << std::endl;
      switch (v_whitebalance_mode->getIndex()) {
        case 0:
          camera->BalanceWhiteAuto.SetValue(Basler_UniversalCameraParams::BalanceWhiteAuto_Off);
          break;
        case 1:
          camera->BalanceWhiteAuto.SetValue(Basler_UniversalCameraParams::BalanceWhiteAuto_Once);
          break;
        case 2:
          camera->BalanceWhiteAuto.SetValue(Basler_UniversalCameraParams::BalanceWhiteAuto_Continuous);
          break;
      }

      std::cout << "[BASLER] AcquisitionFrameRate " << v_framerate->getDouble() << std::endl;
      camera->AcquisitionFrameRate.SetValue(v_framerate->getDouble());

      switch (v_auto_gain->getIndex()) {
        case 2:
          std::cout << "[BASLER] GainAuto GainAuto_Continuous" << std::endl;
          camera->GainAuto.SetValue(Basler_UniversalCameraParams::GainAuto_Continuous);
          break;
        case 1:
          std::cout << "[BASLER] GainAuto GainAuto_Once" << std::endl;
          camera->GainAuto.SetValue(Basler_UniversalCameraParams::GainAuto_Once);
          break;
        case 0:
          std::cout << "[BASLER] GainAuto GainAuto_Off" << std::endl;
          camera->GainAuto.SetValue(Basler_UniversalCameraParams::GainAuto_Off);
          camera->Gain.SetValue(v_gain->getInt());
          break;
      }

      switch (v_auto_exposure->getIndex()) {
        case 2:
          std::cout << "[BASLER] ExposureAuto ExposureAuto_Continuous" << std::endl;
          camera->ExposureAuto.SetValue(Basler_UniversalCameraParams::ExposureAuto_Continuous);
          break;
        case 1:
          std::cout << "[BASLER] ExposureAuto ExposureAuto_Once" << std::endl;
          camera->ExposureAuto.SetValue(Basler_UniversalCameraParams::ExposureAuto_Once);
          break;
        case 0:
          std::cout << "[BASLER] ExposureAuto ExposureAuto_Off" << std::endl;
          camera->ExposureAuto.SetValue(Basler_UniversalCameraParams::ExposureAuto_Off);
          std::cout << "[BASLER] ExposureTime = " << v_manual_exposure->getDouble() << std::endl;
          camera->ExposureTime.SetValue(v_manual_exposure->getDouble());
          std::cout << "[BASLER] ExposureTime set." << std::endl;
          break;
      }
    }
  } catch (const GenICam_3_5_Basler_pylon_v1::AccessException& e) {
    std::cerr << "[BASLER] Access exception: " << e.what() << std::endl;
    MUTEX_UNLOCK;
    return;
  } catch (const Pylon::GenericException& e) {
    MUTEX_UNLOCK;
    std::cerr << "[BASLER] Error writing parameter values: " << e.what() << std::endl;
    return;
  } catch (...) {
    MUTEX_UNLOCK;
    throw;
  }
  MUTEX_UNLOCK;
}

void CaptureBasler::mvc_connect(VarList* group) {
  vector<VarType*> v = group->getChildren();
  for (auto& i : v) {
    connect(i, SIGNAL(wasEdited(VarType*)), group, SLOT(mvcEditCompleted()));
  }
  connect(group, SIGNAL(wasEdited(VarType*)), this, SLOT(changed(VarType*)));
}

void CaptureBasler::changed(VarType* group) {
  if (group->getType() == VARTYPE_ID_LIST) {
    writeParameterValues(dynamic_cast<VarList*>(group));
  }
}
