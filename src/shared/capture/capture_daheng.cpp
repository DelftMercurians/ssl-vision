/*
 * capture_daheng.cpp
 *
 *  Created on: Nov 21, 2016
 *      Author: root
 */

#include "capture_daheng.h"

#include <vector>
#include <string>

#define MUTEX_LOCK mutex.lock()
#define MUTEX_UNLOCK mutex.unlock()

int DahengInitManager::count = 0;

void GetErrorString(GX_STATUS emErrorStatus);

void DahengInitManager::register_capture() {
	if (count++ == 0) {
		GX_STATUS emStatus = GXInitLib(); 
    if(emStatus != GX_STATUS_SUCCESS)
    {
        GetErrorString(emStatus);
    }
	}
}

void DahengInitManager::unregister_capture() {
	if (--count == 0) {
		//Release libary
    GX_STATUS emStatus = GXCloseLib();
    if(emStatus != GX_STATUS_SUCCESS)
    {
        GetErrorString(emStatus);
    }
	}
}

CaptureDaheng::CaptureDaheng(VarList* _settings, int default_camera_id, QObject* parent) :
		QObject(parent), CaptureInterface(_settings) {
	is_capturing = false;
	g_hDevice = nullptr;
	ignore_capture_failure = false;
	// converter.OutputPixelFormat = Pylon::PixelType_RGB8packed;
	//camera.PixelFormat.SetValue(Daheng_GigECamera::PixelFormat_YUV422Packed, true);
	last_buf = nullptr;

	settings->addChild(vars = new VarList("Capture Settings"));
	settings->removeFlags(VARTYPE_FLAG_HIDE_CHILDREN);
	vars->removeFlags(VARTYPE_FLAG_HIDE_CHILDREN);
	v_color_mode = new VarStringEnum("color mode",
			Colors::colorFormatToString(COLOR_RGB8));
	v_color_mode->addItem(Colors::colorFormatToString(COLOR_YUV422_UYVY));
	v_color_mode->addItem(Colors::colorFormatToString(COLOR_RGB8));
	vars->addChild(v_color_mode);

	vars->addChild(v_camera_id = new VarInt("Camera ID", default_camera_id, 0, 3));

	v_framerate = new VarDouble("Max Framerate",100.0,0.0,100.0);
	vars->addChild(v_framerate);

	v_balance_ratio_red = new VarInt("Balance Ratio Red", 64, 0, 255);
	vars->addChild(v_balance_ratio_red);

	v_balance_ratio_green = new VarInt("Balance Ratio Green", 64, 0, 255);
	vars->addChild(v_balance_ratio_green);

	v_balance_ratio_blue = new VarInt("Balance Ratio Blue", 64, 0, 255);
	vars->addChild(v_balance_ratio_blue);

	v_auto_gain = new VarBool("auto gain", false);
	vars->addChild(v_auto_gain);

	v_gain = new VarInt("gain", 300, 0, 542);
	vars->addChild(v_gain);

	v_gamma_enable = new VarBool("enable gamma correction", true);
	vars->addChild(v_gamma_enable);

	v_gamma = new VarDouble("gamma", 0.5, 0, 1.0);
	vars->addChild(v_gamma);

	v_black_level = new VarDouble("black level", 64, 0, 1000);
	vars->addChild(v_black_level);

	v_auto_exposure = new VarBool("auto exposure", false);
	vars->addChild(v_auto_exposure);

	v_manual_exposure = new VarDouble("manual exposure (μs)", 10000, 1000,
			30000);
	vars->addChild(v_manual_exposure);

	current_id = 0;

	mvc_connect(settings);
	mvc_connect(vars);
}

CaptureDaheng::~CaptureDaheng() {
	vars->deleteAllChildren();
}

bool CaptureDaheng::_buildCamera() {
	DahengInitManager::register_capture();
	current_id = v_camera_id->get();
	printf("Current camera id: %d\n", current_id);

  GX_STATUS emStatus = GXOpenDeviceByIndex(1, &g_hDevice);
  if(emStatus != GX_STATUS_SUCCESS)
  {
      GetErrorString(emStatus);
      return false;           
  }

  printf("Done!\n");
  is_capturing = true;
  return true;
	// if (amt > current_id) {
	// 	// Pylon::CDeviceInfo info = devices[current_id];

	// 	// camera = new Pylon::CDahengGigEInstantCamera(
	// 	// 		// Pylon::CTlFactory::GetInstance().CreateDevice(info));
  //   //     	printf("Opening camera %d...\n", current_id);
	// 	// camera->Open();
  //   //     	camera->GammaSelector.SetValue(Daheng_GigECamera::GammaSelector_User); //Necessary for interface to work
  //   //     	camera->AcquisitionFrameRateEnable.SetValue(true); //Turn on capped framerates
  //   //     	camera_frequency = camera->GevTimestampTickFrequency.GetValue();;

  //   //     	//let camera send timestamps and FrameCounts.
	// 	// if (GenApi::IsWritable(camera->ChunkModeActive)) {
	// 	// 	camera->ChunkModeActive.SetValue(true);
	// 	// 	camera->ChunkSelector.SetValue(Daheng_GigECamera::ChunkSelector_Timestamp);
	// 	// 	camera->ChunkEnable.SetValue(true);
	// 	// 	camera->ChunkSelector.SetValue(Daheng_GigECamera::ChunkSelector_Framecounter);
	// 	// 	camera->ChunkEnable.SetValue(true);
	// 	// 	camera->GevTimestampControlReset.Execute(); //Reset the internal time stamp counter of the camera to 0
	// 	} else {
	// 		std::cout << "Failed, camera model does not support accurate timings!" << std::endl;
	// 		return false; //Camera does not support accurate timings
	// 	}
	// }
}

bool CaptureDaheng::startCapture() {
	MUTEX_LOCK;
  if (g_hDevice == nullptr) {
    if (!_buildCamera()) {
        // Did not make a camera!
        MUTEX_UNLOCK;
        return false;
    }
  }

  //Set acquisition mode
  GX_STATUS emStatus = GXSetEnum(g_hDevice, GX_ENUM_ACQUISITION_MODE, GX_ACQ_MODE_CONTINUOUS);
  GX_VERIFY_EXIT(emStatus);

  //Set trigger mode
  emStatus = GXSetEnum(g_hDevice, GX_ENUM_TRIGGER_MODE, GX_TRIGGER_MODE_OFF);
  GX_VERIFY_EXIT(emStatus);

  //Set buffer quantity of acquisition queue
  uint64_t nBufferNum = ACQ_BUFFER_NUM;
  emStatus = GXSetAcqusitionBufferNumber(g_hDevice, nBufferNum);
  GX_VERIFY_EXIT(emStatus);

  bool bStreamTransferSize = false;
  emStatus = GXIsImplemented(g_hDevice, GX_DS_INT_STREAM_TRANSFER_SIZE, &bStreamTransferSize);
  GX_VERIFY_EXIT(emStatus);

  if(bStreamTransferSize)
  {
      //Set size of data transfer block
      emStatus = GXSetInt(g_hDevice, GX_DS_INT_STREAM_TRANSFER_SIZE, ACQ_TRANSFER_SIZE);
      GX_VERIFY_EXIT(emStatus);
  }

  bool bStreamTransferNumberUrb = false;
  emStatus = GXIsImplemented(g_hDevice, GX_DS_INT_STREAM_TRANSFER_NUMBER_URB, &bStreamTransferNumberUrb);
  GX_VERIFY_EXIT(emStatus);

  if(bStreamTransferNumberUrb)
  {
      //Set qty. of data transfer block
      emStatus = GXSetInt(g_hDevice, GX_DS_INT_STREAM_TRANSFER_NUMBER_URB, ACQ_TRANSFER_NUMBER_URB);
      GX_VERIFY_EXIT(emStatus);
  }

  //Set Balance White Mode : Continuous
  emStatus = GXSetEnum(g_hDevice, GX_ENUM_BALANCE_WHITE_AUTO, GX_BALANCE_WHITE_AUTO_ONCE);
  GX_VERIFY_EXIT(emStatus);

  //Allocate the memory for pixel format transform 
  PreForAcquisition();

  //Device start acquisition
  emStatus = GXStreamOn(g_hDevice);
  if(emStatus != GX_STATUS_SUCCESS)
  {
      //Release the memory allocated
      UnPreForAcquisition();
      GX_VERIFY_EXIT(emStatus);
  }

	MUTEX_UNLOCK;
	return true;
}

bool CaptureDaheng::_stopCapture() {
	if (is_capturing) {
    // TODO: stop capture
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
    PGX_FRAME_BUFFER pFrameBuffer = NULL;

    try {
        // Keep grabbing in case of partial grabs
        int fail_count = 0;
        while (fail_count < 10) {
            emStatus = GXDQBuf(g_hDevice, &pFrameBuffer, 1000);

            if(emStatus != GX_STATUS_SUCCESS) {
                if (emStatus == GX_STATUS_TIMEOUT) {
                    fail_count++;
                    continue;
                } else {
                    GetErrorString(emStatus);
                    break;
                }
            }

            if(pFrameBuffer->nStatus != GX_FRAME_STATUS_SUCCESS) {
                fail_count++;
                fprintf(stderr, "Image grab failed in CaptureDaheng::getFrame: %d\n", pFrameBuffer->nStatus);
                continue;
            }

            // If successful, break from the loop
            break;
        }

        if (fail_count == 10) {
            fprintf(stderr, "Maximum retry count for image grabbing (%d) exceeded in capture_daheng\n", fail_count);
            MUTEX_UNLOCK;
            return img;
        }

        // Process the image data as needed
        img.setWidth(pFrameBuffer->nWidth);
        img.setHeight(pFrameBuffer->nHeight);
        unsigned char* buf = (unsigned char*) malloc(pFrameBuffer->nImgSize);
        memcpy(buf, pFrameBuffer->pImgBuf, pFrameBuffer->nImgSize);
        img.setData(buf);

        // Set the timestamp
        timeval tv = {};
        gettimeofday(&tv, nullptr);
        double systemTime = (double) tv.tv_sec + (tv.tv_usec / 1000000.0);
        img.setTime(systemTime);

        // Requeue the buffer
        emStatus = GXQBuf(g_hDevice, pFrameBuffer);
        if(emStatus != GX_STATUS_SUCCESS) {
            GetErrorString(emStatus);
        }

    } catch (...) {
        // Handle other exceptions
        fprintf(stderr, "Uncaught exception in CaptureDaheng::getFrame\n");
        MUTEX_UNLOCK;
        throw;
    }

    MUTEX_UNLOCK;
    return img;
}


string CaptureDaheng::getCaptureMethodName() const {
	return "Daheng";
}

bool CaptureDaheng::copyAndConvertFrame(const RawImage & src,
		RawImage & target) {
	MUTEX_LOCK;
	try {
		target.ensure_allocation(COLOR_RGB8, src.getWidth(), src.getHeight());
		target.setTime(src.getTime());
		target.setTimeCam (src.getTimeCam());
		memcpy(target.getData(), src.getData(), src.getNumBytes());
	} catch (...) {
		MUTEX_UNLOCK;
		throw;
	}
	MUTEX_UNLOCK;
	return true;
}

void CaptureDaheng::readAllParameterValues() {
	// MUTEX_LOCK;
	// try {
	// 	if (!camera)
	// 		return;
	// 	bool was_open = camera->IsOpen();
	// 	if (!was_open) {
	// 		camera->Open();
	// 	}
	// 	v_framerate->setDouble(camera->AcquisitionFrameRateAbs.GetValue());
	// 	camera->BalanceRatioSelector.SetValue(
	// 			Daheng_GigECamera::BalanceRatioSelector_Red);
	// 	v_balance_ratio_red->setInt(camera->BalanceRatioRaw.GetValue());
	// 	camera->BalanceRatioSelector.SetValue(
	// 			Daheng_GigECamera::BalanceRatioSelector_Green);
	// 	v_balance_ratio_green->setInt(camera->BalanceRatioRaw.GetValue());
	// 	camera->BalanceRatioSelector.SetValue(
	// 			Daheng_GigECamera::BalanceRatioSelector_Blue);
	// 	v_balance_ratio_blue->setInt(camera->BalanceRatioRaw.GetValue());

	// 	v_auto_gain->setBool(camera->GainAuto.GetValue() == Daheng_GigECamera::GainAuto_Continuous);
	// 	v_gain->setDouble(camera->GainRaw.GetValue());
	// 	v_gamma_enable->setBool(camera->GammaEnable.GetValue());
	// 	v_gamma->setDouble(camera->Gamma.GetValue());

	// 	v_auto_exposure->setBool(camera->ExposureAuto.GetValue() == Daheng_GigECamera::ExposureAuto_Continuous);
	// 	v_manual_exposure->setDouble(camera->ExposureTimeAbs.GetValue());
	// // } catch (const Pylon::GenericException& e) {
	// 	fprintf(stderr, "Exception reading parameter values: %s\n", e.what());
	// 	MUTEX_UNLOCK;
	// 	return;
	// } catch (...) {
	// 	MUTEX_UNLOCK;
	// 	throw;
	// }
	// MUTEX_UNLOCK;
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

void CaptureDaheng::writeParameterValues(VarList* varList) {
	// if (varList != this->settings) {
	// 	return;
	// }
	// MUTEX_LOCK;
	// try {
	// 	if(current_id != v_camera_id->get()){
  //           MUTEX_UNLOCK;
  //           resetCamera(v_camera_id->get()); // locks itself
  //           MUTEX_LOCK;
	// 	}

  //       if (camera != nullptr) {
  //           camera->Open();

  //           camera->AcquisitionFrameRateAbs.SetValue(v_framerate->getDouble());

  //           camera->BalanceRatioSelector.SetValue(
  //                   Daheng_GigECamera::BalanceRatioSelector_Red);
  //           camera->BalanceRatioRaw.SetValue(v_balance_ratio_red->get());
  //           camera->BalanceRatioSelector.SetValue(
  //                   Daheng_GigECamera::BalanceRatioSelector_Green);
  //           camera->BalanceRatioRaw.SetValue(v_balance_ratio_green->get());
  //           camera->BalanceRatioSelector.SetValue(
  //                   Daheng_GigECamera::BalanceRatioSelector_Blue);
  //           camera->BalanceRatioRaw.SetValue(v_balance_ratio_blue->get());
  //           camera->BalanceWhiteAuto.SetValue(
  //                   Daheng_GigECamera::BalanceWhiteAuto_Off);

  //           if (v_auto_gain->getBool()) {
  //               camera->GainAuto.SetValue(Daheng_GigECamera::GainAuto_Continuous);
  //           } else {
  //               camera->GainAuto.SetValue(Daheng_GigECamera::GainAuto_Off);
  //               camera->GainRaw.SetValue(v_gain->getInt());
  //           }

  //           if (v_gamma_enable->getBool()) {
  //               camera->GammaEnable.SetValue(true);
  //               camera->Gamma.SetValue(v_gamma->getDouble());
  //           } else {
  //               camera->GammaEnable.SetValue(false);
  //           }

  //           if (v_auto_exposure->getBool()) {
  //               camera->ExposureAuto.SetValue(
  //                       Daheng_GigECamera::ExposureAuto_Continuous);
  //           } else {
  //               camera->ExposureAuto.SetValue(Daheng_GigECamera::ExposureAuto_Off);
  //               camera->ExposureTimeAbs.SetValue(v_manual_exposure->getDouble());
  //           }
  //       }
	// // } catch (const Pylon::GenericException& e) {
	// 	MUTEX_UNLOCK;
	// 	fprintf(stderr, "Error writing parameter values: %s\n", e.what());
	// 	throw;
	// } catch (...) {
	// 	MUTEX_UNLOCK;
	// 	throw;
	// }
	// MUTEX_UNLOCK;
}

void CaptureDaheng::mvc_connect(VarList * group) {
	vector<VarType *> v = group->getChildren();
	for (auto & i : v) {
	connect(i,SIGNAL(wasEdited(VarType *)),group,SLOT(mvcEditCompleted()));
}
connect(group,SIGNAL(wasEdited(VarType *)),this,SLOT(changed(VarType *)));
}

void CaptureDaheng::changed(VarType * group) {
if (group->getType() == VARTYPE_ID_LIST) {
writeParameterValues(dynamic_cast<VarList*>(group));
}
}

//----------------------------------------------------------------------------------
/**
\brief  Get description of input error code
\param  emErrorStatus  error code

\return void
*/
//----------------------------------------------------------------------------------
void GetErrorString(GX_STATUS emErrorStatus)
{
    char *error_info = NULL;
    size_t size = 0;
    GX_STATUS emStatus = GX_STATUS_SUCCESS;
    
    // Get length of error description
    emStatus = GXGetLastError(&emErrorStatus, NULL, &size);
    if(emStatus != GX_STATUS_SUCCESS)
    {
        printf("<Error when calling GXGetLastError>\n");
        return;
    }
    
    // Alloc error resources
    error_info = new char[size];
    if (error_info == NULL)
    {
        printf("<Failed to allocate memory>\n");
        return ;
    }
    
    // Get error description
    emStatus = GXGetLastError(&emErrorStatus, error_info, &size);
    if (emStatus != GX_STATUS_SUCCESS)
    {
        printf("<Error when calling GXGetLastError>\n");
    }
    else
    {
        printf("%s\n", error_info);
    }

    // Realease error resources
    if (error_info != NULL)
    {
        delete []error_info;
        error_info = NULL;
    }
}

