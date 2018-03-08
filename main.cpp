/********************************************************************************
*
* V5 SYSTEMS
* __________________
*
*  [2013] - [2017] V5 Systems Incorporated
*  All Rights Reserved.
*
*  NOTICE:  This is free software.  Permission is granted to everyone to use,
*          copy or modify this software under the terms and conditions of
*                 GNU General Public License, v. 2
*
*
*
* GPL license.
*
* This program is free software; you can redistribute it and/or modify it under
* the terms of the GNU General Public License as published by the Free Software
* Foundation; either version 2 of the License, or (at your option) any later
* version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY
* WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
* PARTICULAR PURPOSE. See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with
* this program; if not, write to the Free Software Foundation, Inc., 59 Temple
* Place, Suite 330, Boston, MA 02111-1307 USA
**********************************************************************************/

#include <csignal>
#include <cstring>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cstdlib>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>
#include <algorithm>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <vector>
#include <stdint.h>
#include <openssl/rsa.h>

#include "wsdd.nsmap"

#include "include/soapDeviceBindingProxy.h"
#include "include/soapMediaBindingProxy.h"
#include "include/soapImagingBindingProxy.h"
#include "include/soapPTZBindingProxy.h"

#include "include/rapidjson/document.h"

#include "plugin/wsseapi.h"
#include "plugin/httpda.h"

#define ONVIF_WAIT_TIMEOUT 10
#define ONVIF_PROT_MARKER       0x5432fbca
#define BUFFER_LEN              65536
#define MAX_DATE                32
#define MAX_IDLE_TIME           10
#define LISTENIP                "0.0.0.0"
#define LISTENPORT              4461
#define CAMERAIP                "192.168.2.11"
#define ONVIFADMIN              "admin"
#define ONVIFPASS               "admin"

#define CMD_COMMAND             "command"
#define CMD_PARAMS              "parameters"
#define STATUS                  "status"

#define RETRIES                 5


using namespace std;
using namespace rapidjson;

struct tHeader {
  uint32_t marker;
  uint32_t mesID;
  uint32_t dataLen;
  unsigned char data;
};

typedef tHeader* pHeader;
const size_t sHeader = 12;

struct tClient {
  int srvSocket;
  int idleTime;
  int totalSize;
  int currentSize;
  unsigned char* data;
};

std::map<int, tClient*> clientConnections;
std::vector<std::string> videoSources;

char const* progName;
std::string onvifPass;
std::string onvifLogin;
int verbosity;
std::string listenIP;
int listenPort;
std::string cameraIP;

int maxIdleTime;

struct timeval startTime;

DeviceBindingProxy proxyDevice;
MediaBindingProxy proxyMedia;
ImagingBindingProxy proxyImaging;
PTZBindingProxy proxyPTZ;

struct soap * glSoap;

std::string cachedImagingOptionsResponse;
std::string cachedMoveOptionsResponse;
std::string cachedOSDOptionsResponse;
std::string cachedCapabilities;
std::string faultStr;

//Helper functions:
int onDataReceived(int fd, unsigned char* data, uint32_t dataLen);
void processReceivedData(int fd, std::string message, uint32_t messageID);
int sendData(int fd, unsigned char *message, int mSize);
void processRcvData(int fd);
std::string getTimeStamp();
std::string timeToString(time_t convTime);
void onTimer();
void checkTime();
tClient* findConnection(int fd);
int onConnectionClosed(int fd);
int deleteConnection(tClient* tmpClient);


//Onvif functions:
void printError(soap* _psoap);
bool sendGetWsdlUrl(DeviceBindingProxy* tProxyDevice, _tds__GetWsdlUrl * wsdURL,
                    _tds__GetWsdlUrlResponse * wsdURLResponse);
bool sendGetCapabilities(DeviceBindingProxy* tProxyDevice, _tds__GetCapabilities * getCap,
                         _tds__GetCapabilitiesResponse * getCapResponse, MediaBindingProxy * tProxyMedia,
                         ImagingBindingProxy* tProxyImaging, PTZBindingProxy* tProxyPTZ);
bool sendSystemReboot(DeviceBindingProxy* tProxyDevice, _tds__SystemReboot * sysReboot,
                      _tds__SystemRebootResponse * sysRebootResponse);
bool sendGetDeviceInformation(DeviceBindingProxy* tProxyDevice, _tds__GetDeviceInformation * GetDeviceInformation,
                              _tds__GetDeviceInformationResponse * GetDeviceInformationResponse);
bool sendGetSystemDateAndTime(DeviceBindingProxy* tProxyDevice, _tds__GetSystemDateAndTime * GetSystemDateAndTime,
                              _tds__GetSystemDateAndTimeResponse * GetSystemDateAndTimeResponse);
bool sendSetSystemDateAndTime(DeviceBindingProxy* tProxyDevice, _tds__SetSystemDateAndTime * SetSystemDateAndTime,
                              _tds__SetSystemDateAndTimeResponse * SetSystemDateAndTimeResponse);
bool sendGetVideoSources(MediaBindingProxy* tProxyMedia, _trt__GetVideoSources * imagingSettings,
                         _trt__GetVideoSourcesResponse * imagingSettingsResponse);
//bool sendGetProfiles(MediaBindingProxy* tProxyMedia, _trt__GetProfiles * getProfiles,
//                     _trt__GetProfilesResponse * getProfilesResponse, soap * tSoap);
bool sendGetProfiles(MediaBindingProxy* tProxyMedia, _trt__GetProfiles * getProfiles,
                     _trt__GetProfilesResponse * getProfilesResponse);
bool sendSetVideoEncoderConfiguration(MediaBindingProxy* tProxyMedia,
                     _trt__SetVideoEncoderConfiguration * SetVideoEncoderConfiguration,
                     _trt__SetVideoEncoderConfigurationResponse * SetVideoEncoderConfigurationResponse);
bool sendGetVideoEncoderConfiguration(MediaBindingProxy* tProxyMedia,
                     _trt__GetVideoEncoderConfiguration * GetVideoEncoderConfiguration,
                     _trt__GetVideoEncoderConfigurationResponse * GetVideoEncoderConfigurationResponse);

bool sendAddPTZConfiguration(MediaBindingProxy* tProxyMedia, _trt__AddPTZConfiguration * AddPTZConfiguration,
                             _trt__AddPTZConfigurationResponse * AddPTZConfigurationResponse);
bool sendGetStreamUri(MediaBindingProxy* tProxyMedia, _trt__GetStreamUri * streamUri,
                      _trt__GetStreamUriResponse * streamUriResponse);
bool sendGetImagingSettings(ImagingBindingProxy* tProxyImaging, _timg__GetImagingSettings * imagingSettings,
                            _timg__GetImagingSettingsResponse * imagingSettingsResponse);
bool sendSetImagingSettings(ImagingBindingProxy* tProxyImaging, _timg__SetImagingSettings * imagingSettings,
                            _timg__SetImagingSettingsResponse * imagingSettingsResponse);
bool sendGetOptions(ImagingBindingProxy* tProxyImaging, _timg__GetOptions * imagingSettings,
                    _timg__GetOptionsResponse * imagingSettingsResponse);
bool sendGetMoveOptions(ImagingBindingProxy* tProxyImaging, _timg__GetMoveOptions * imagingSettings,
                        _timg__GetMoveOptionsResponse * imagingSettingsResponse);
bool sendGetStatus(ImagingBindingProxy* tProxyImaging, _timg__GetStatus * imagingSettings,
                   _timg__GetStatusResponse * imagingSettingsResponse);
//OSD send functions:
bool sendGetOSDOptions(MediaBindingProxy* tProxyMedia, _trt__GetOSDOptions * imagingSettings,
                       _trt__GetOSDOptionsResponse * imagingSettingsResponse);
bool sendStop(ImagingBindingProxy* tProxyImaging, _timg__Stop * imagingSettings,
              _timg__StopResponse * imagingSettingsResponse);
bool sendMove(ImagingBindingProxy* tProxyImaging, _timg__Move * imagingSettings,
              _timg__MoveResponse * imagingSettingsResponse);
bool sendGetOSDs(MediaBindingProxy* tProxyMedia, _trt__GetOSDs * imagingSettings,
                 _trt__GetOSDsResponse * imagingSettingsResponse);
bool sendGetOSD(MediaBindingProxy* tProxyMedia, _trt__GetOSD * imagingSettings,
                _trt__GetOSDResponse * imagingSettingsResponse);
bool sendSetOSD(MediaBindingProxy* tProxyMedia, _trt__SetOSD * imagingSettings,
                _trt__SetOSDResponse * imagingSettingsResponse);
bool sendCreateOSD(MediaBindingProxy* tProxyMedia, _trt__CreateOSD * imagingSettings,
                   _trt__CreateOSDResponse * imagingSettingsResponse);
bool sendDeleteOSD(MediaBindingProxy* tProxyMedia, _trt__DeleteOSD * imagingSettings,
                   _trt__DeleteOSDResponse * imagingSettingsResponse);
//PTZ send functions:
bool sendAbsoluteMove(PTZBindingProxy* tProxyPTZ, _tptz__AbsoluteMove * imagingSettings,
                      _tptz__AbsoluteMoveResponse * imagingSettingsResponse);
bool sendContinuousMove(PTZBindingProxy* tProxyPTZ, _tptz__ContinuousMove * imagingSettings,
                        _tptz__ContinuousMoveResponse * imagingSettingsResponse);
bool sendRelativeMove(PTZBindingProxy* tProxyPTZ, _tptz__RelativeMove * imagingSettings,
                      _tptz__RelativeMoveResponse * imagingSettingsResponse);
bool sendGeoMove(PTZBindingProxy* tProxyPTZ, _tptz__GeoMove * imagingSettings,
                 _tptz__GeoMoveResponse * imagingSettingsResponse);
bool sendPTZStop(PTZBindingProxy* tProxyPTZ, _tptz__Stop * imagingSettings,
                 _tptz__StopResponse * imagingSettingsResponse);
bool sendSendAuxiliaryCommand(PTZBindingProxy* tProxyPTZ, _tptz__SendAuxiliaryCommand * imagingSettings,
                              _tptz__SendAuxiliaryCommandResponse * imagingSettingsResponse);
bool sendGetServiceCapabilities(PTZBindingProxy* tProxyPTZ, _tptz__GetServiceCapabilities * imagingSettings,
                                _tptz__GetServiceCapabilitiesResponse * imagingSettingsResponse);
bool sendPTZGetStatus(PTZBindingProxy* tProxyPTZ, _tptz__GetStatus * imagingSettings,
                      _tptz__GetStatusResponse * imagingSettingsResponse);
bool sendGetConfigurations(PTZBindingProxy* tProxyPTZ, _tptz__GetConfigurations * imagingSettings,
                           _tptz__GetConfigurationsResponse * imagingSettingsResponse);
bool sendGetConfiguration(PTZBindingProxy* tProxyPTZ, _tptz__GetConfiguration * imagingSettings,
                          _tptz__GetConfigurationResponse * imagingSettingsResponse);
bool sendGetNodes(PTZBindingProxy* tProxyPTZ, _tptz__GetNodes * imagingSettings,
                  _tptz__GetNodesResponse * imagingSettingsResponse);
bool sendGetNode(PTZBindingProxy* tProxyPTZ, _tptz__GetNode * imagingSettings,
                 _tptz__GetNodeResponse * imagingSettingsResponse);
bool sendGetConfigurationOptions(PTZBindingProxy* tProxyPTZ, _tptz__GetConfigurationOptions * imagingSettings,
                                 _tptz__GetConfigurationOptionsResponse * imagingSettingsResponse);
bool sendGetCompatibleConfigurations(PTZBindingProxy* tProxyPTZ, _tptz__GetCompatibleConfigurations * imagingSettings,
                                     _tptz__GetCompatibleConfigurationsResponse * imagingSettingsResponse);
bool sendGetPresets(PTZBindingProxy* tProxyPTZ, _tptz__GetPresets * imagingSettings,
                    _tptz__GetPresetsResponse * imagingSettingsResponse);
bool sendRemovePreset(PTZBindingProxy* tProxyPTZ, _tptz__RemovePreset * imagingSettings,
                      _tptz__RemovePresetResponse * imagingSettingsResponse);
bool sendGotoPreset(PTZBindingProxy* tProxyPTZ, _tptz__GotoPreset * imagingSettings,
                    _tptz__GotoPresetResponse * imagingSettingsResponse);
bool sendGotoHomePosition(PTZBindingProxy* tProxyPTZ, _tptz__GotoHomePosition * imagingSettings,
                          _tptz__GotoHomePositionResponse * imagingSettingsResponse);
bool sendSetHomePosition(PTZBindingProxy* tProxyPTZ, _tptz__SetHomePosition * imagingSettings,
                         _tptz__SetHomePositionResponse * imagingSettingsResponse);
bool sendCreatePresetTour(PTZBindingProxy* tProxyPTZ, _tptz__CreatePresetTour * imagingSettings,
                          _tptz__CreatePresetTourResponse * imagingSettingsResponse);
bool sendRemovePresetTour(PTZBindingProxy* tProxyPTZ, _tptz__RemovePresetTour * imagingSettings,
                          _tptz__RemovePresetTourResponse * imagingSettingsResponse);
bool sendSetPreset(PTZBindingProxy* tProxyPTZ, _tptz__SetPreset * imagingSettings,
                   _tptz__SetPresetResponse * imagingSettingsResponse);
bool sendOperatePresetTour(PTZBindingProxy* tProxyPTZ, _tptz__OperatePresetTour * imagingSettings,
                           _tptz__OperatePresetTourResponse * imagingSettingsResponse);
bool sendSetConfiguration(PTZBindingProxy* tProxyPTZ, _tptz__SetConfiguration * imagingSettings,
                          _tptz__SetConfigurationResponse * imagingSettingsResponse);
bool sendGetPresetTours(PTZBindingProxy* tProxyPTZ, _tptz__GetPresetTours * imagingSettings,
                        _tptz__GetPresetToursResponse * imagingSettingsResponse);
bool sendGetPresetTour(PTZBindingProxy* tProxyPTZ, _tptz__GetPresetTour * imagingSettings,
                       _tptz__GetPresetTourResponse * imagingSettingsResponse);
bool sendGetPresetTourOptions(PTZBindingProxy* tProxyPTZ, _tptz__GetPresetTourOptions * imagingSettings,
                              _tptz__GetPresetTourOptionsResponse * imagingSettingsResponse);
bool sendModifyPresetTour(PTZBindingProxy* tProxyPTZ, _tptz__ModifyPresetTour * imagingSettings,
                          _tptz__ModifyPresetTourResponse * imagingSettingsResponse);

//SetVideoEncoderConfiguration
//GetProfiles add VideoEncoderConfiguration

//Commands functions
void execGetImagingSettings(int fd, rapidjson::Document &d1, uint32_t messageID);
void execSetImagingSettings(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetOptions(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetMoveOptions(int fd, rapidjson::Document &d1, uint32_t messageID);
void execStop(int fd, rapidjson::Document &d1, uint32_t messageID);
void execMove(int fd, rapidjson::Document &d1, uint32_t messageID);
void execSystemReboot(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetOSDs(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetOSD(int fd, rapidjson::Document &d1, uint32_t messageID);
void execSetOSD(int fd, rapidjson::Document &d1, uint32_t messageID);
void execCreateOSD(int fd, rapidjson::Document &d1, uint32_t messageID);
void execDeleteOSD(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetOSDOptions(int fd, rapidjson::Document &d1, uint32_t messageID);

void execAbsoluteMove(int fd, rapidjson::Document &d1, uint32_t messageID);
void execContinuousMove(int fd, rapidjson::Document &d1, uint32_t messageID);
void execRelativeMove(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGeoMove(int fd, rapidjson::Document &d1, uint32_t messageID);
void execPTZStop(int fd, rapidjson::Document &d1, uint32_t messageID);
void execSendAuxiliaryCommand(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetServiceCapabilities(int fd, rapidjson::Document &d1, uint32_t messageID);
void execPTZGetStatus(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetConfigurations(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetConfiguration(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetNodes(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetNode(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetConfigurationOptions(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetCompatibleConfigurations(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetPresets(int fd, rapidjson::Document &d1, uint32_t messageID);
void execRemovePreset(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGotoPreset(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGotoHomePosition(int fd, rapidjson::Document &d1, uint32_t messageID);
void execSetHomePosition(int fd, rapidjson::Document &d1, uint32_t messageID);
void execCreatePresetTour(int fd, rapidjson::Document &d1, uint32_t messageID);
void execRemovePresetTour(int fd, rapidjson::Document &d1, uint32_t messageID);
void execSetPreset(int fd, rapidjson::Document &d1, uint32_t messageID);
void execOperatePresetTour(int fd, rapidjson::Document &d1, uint32_t messageID);
void execSetConfiguration(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetPresetTours(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetPresetTour(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetProfiles(int fd, rapidjson::Document &d1, uint32_t messageID);
void execSetVideoEncoderConfiguration(int fd, rapidjson::Document &d1, uint32_t messageID);
void execAddPTZConfiguration(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetDeviceInformation(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetSystemDateAndTime(int fd, rapidjson::Document &d1, uint32_t messageID);
void execSetSystemDateAndTime(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetPresetTourOptions(int fd, rapidjson::Document &d1, uint32_t messageID);
void execModifyPresetTour(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGetCapabilities(int fd, rapidjson::Document &d1, uint32_t messageID);
void execGenerateRecoverVideoConfig(int fd, rapidjson::Document &d1, uint32_t messageID);

std::string prepareOptionsResponse(_timg__GetOptionsResponse* optionsResponse);
std::string prepareMoveOptionsResponse(_timg__GetMoveOptionsResponse* moveOptionsResponse);
std::string prepareOSDOptionsResponse(_trt__GetOSDOptionsResponse* OSDOptionsResponse);

//###################### Implementation: ##################################################

void printError(soap* _psoap) {
  if(!faultStr.empty())faultStr+=", ";
  faultStr+="ERR: " + std::string(*soap_faultstring(_psoap));
  fprintf(stderr,"error:%d faultstring:%s faultcode:%s faultsubcode:%s faultdetail:%s\n",
          _psoap->error,	*soap_faultstring(_psoap), *soap_faultcode(_psoap),*soap_faultsubcode(_psoap),
          *soap_faultdetail(_psoap));
}

bool sendGetWsdlUrl(DeviceBindingProxy* tProxyDevice, _tds__GetWsdlUrl * wsdURL,
                    _tds__GetWsdlUrlResponse * wsdURLResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }
  int result = tProxyDevice->GetWsdlUrl(wsdURL, *wsdURLResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "WsdlUrl Found: %s \n", wsdURLResponse->WsdlUrl.c_str());
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetWsdlUrl return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyDevice->soap);
    tProxyDevice->soap->userid = onvifLogin.c_str();
    tProxyDevice->soap->passwd = onvifPass.c_str();
    return sendGetWsdlUrl(tProxyDevice, wsdURL, wsdURLResponse);
  }
}


bool sendSystemReboot(DeviceBindingProxy* tProxyDevice, _tds__SystemReboot * sysReboot,
                      _tds__SystemRebootResponse * sysRebootResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }
  int result = tProxyDevice->SystemReboot(sysReboot, *sysRebootResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "SystemReboot: OK\n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "SystemReboot return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyDevice->soap);
    tProxyDevice->soap->userid = onvifLogin.c_str();
    tProxyDevice->soap->passwd = onvifPass.c_str();
    return sendSystemReboot(tProxyDevice, sysReboot, sysRebootResponse);
  }
}


bool sendGetDeviceInformation(DeviceBindingProxy* tProxyDevice, _tds__GetDeviceInformation * GetDeviceInformation,
                              _tds__GetDeviceInformationResponse * GetDeviceInformationResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }
  int result = tProxyDevice->GetDeviceInformation(GetDeviceInformation, *GetDeviceInformationResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetDeviceInformation: OK\n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "GetDeviceInformation return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyDevice->soap);
    tProxyDevice->soap->userid = onvifLogin.c_str();
    tProxyDevice->soap->passwd = onvifPass.c_str();
    return sendGetDeviceInformation(tProxyDevice, GetDeviceInformation, GetDeviceInformationResponse);
  }
}

bool sendGetSystemDateAndTime(DeviceBindingProxy* tProxyDevice, _tds__GetSystemDateAndTime * GetSystemDateAndTime,
                              _tds__GetSystemDateAndTimeResponse * GetSystemDateAndTimeResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }
  int result = tProxyDevice->GetSystemDateAndTime(GetSystemDateAndTime, *GetSystemDateAndTimeResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetSystemDateAndTime: OK\n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "GetSystemDateAndTime return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyDevice->soap);
    tProxyDevice->soap->userid = onvifLogin.c_str();
    tProxyDevice->soap->passwd = onvifPass.c_str();
    return sendGetSystemDateAndTime(tProxyDevice, GetSystemDateAndTime, GetSystemDateAndTimeResponse);
  }
}


bool sendSetSystemDateAndTime(DeviceBindingProxy* tProxyDevice, _tds__SetSystemDateAndTime * SetSystemDateAndTime,
                              _tds__SetSystemDateAndTimeResponse * SetSystemDateAndTimeResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }
  int result = tProxyDevice->SetSystemDateAndTime(SetSystemDateAndTime, *SetSystemDateAndTimeResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "SetSystemDateAndTime: OK\n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "SetSystemDateAndTime return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyDevice->soap);
    tProxyDevice->soap->userid = onvifLogin.c_str();
    tProxyDevice->soap->passwd = onvifPass.c_str();
    return sendSetSystemDateAndTime(tProxyDevice, SetSystemDateAndTime, SetSystemDateAndTimeResponse);
  }
}





bool sendGetCapabilities(DeviceBindingProxy* tProxyDevice, _tds__GetCapabilities * getCap,
                         _tds__GetCapabilitiesResponse * getCapResponse, MediaBindingProxy * tProxyMedia,
                         ImagingBindingProxy* tProxyImaging, PTZBindingProxy* tProxyPTZ) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }
  int result = tProxyDevice->GetCapabilities(getCap, *getCapResponse);
  if (result == SOAP_OK) {
    if (getCapResponse->Capabilities->Media != NULL) {
      tProxyMedia->soap_endpoint = getCapResponse->Capabilities->Media->XAddr.c_str();
      if(verbosity>2)fprintf(stderr, "MediaUrl Found: %s\n",getCapResponse->Capabilities->Media->XAddr.c_str());
      cachedCapabilities="{\"status\":\"OK\", \"parameters\":{\"Capabilities\":[\"Media\"";
    } else {
      if(verbosity>1)std::cout <<  "sendGetCapabilities Media not found: "  << std::endl;
      tCount = 0;
      return false;
    }
    if (getCapResponse->Capabilities->Imaging != NULL) {
      tProxyImaging->soap_endpoint = getCapResponse->Capabilities->Imaging->XAddr.c_str();
      if(verbosity>2)fprintf(stderr, "ImagingUrl Found: %s\n",getCapResponse->Capabilities->Imaging->XAddr.c_str());
      cachedCapabilities+=", \"Imaging\"";
    } else {
      if(verbosity>1)std::cout <<  "sendGetCapabilities Imaging not found: "  << std::endl;
      tProxyImaging->soap_endpoint = "NOTAVAILABLE";
    }
    if (getCapResponse->Capabilities->PTZ != NULL) {
      tProxyPTZ->soap_endpoint = getCapResponse->Capabilities->PTZ->XAddr.c_str();
      if(verbosity>2)fprintf(stderr, "PTZUrl Found: %s\n",getCapResponse->Capabilities->PTZ->XAddr.c_str());
      cachedCapabilities+=", \"PTZ\"";
    } else {
      if(verbosity>1)std::cout <<  "sendGetCapabilities PTZ not found: "  << std::endl;
      tProxyPTZ->soap_endpoint = "NOTAVAILABLE";
    }
    if (getCapResponse->Capabilities->Media != NULL) cachedCapabilities+="]}}";
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetCapabilities return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyDevice->soap);
    tProxyDevice->soap->userid = onvifLogin.c_str();
    tProxyDevice->soap->passwd = onvifPass.c_str();
    return sendGetCapabilities(tProxyDevice, getCap, getCapResponse, tProxyMedia, tProxyImaging, tProxyPTZ);
  }
}

bool sendGetImagingSettings(ImagingBindingProxy* tProxyImaging, _timg__GetImagingSettings * imagingSettings,
                            _timg__GetImagingSettingsResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyImaging->GetImagingSettings(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "ImagingSettings Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetImagingSettings return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyImaging->soap);
    tProxyImaging->soap->userid = onvifLogin.c_str();
    tProxyImaging->soap->passwd = onvifPass.c_str();
    return sendGetImagingSettings(tProxyImaging, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGetVideoSources(MediaBindingProxy* tProxyMedia, _trt__GetVideoSources * imagingSettings,
                         _trt__GetVideoSourcesResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyMedia->GetVideoSources(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "VideoSources Found: \n");
    }
    for(unsigned i=0; i<imagingSettingsResponse->VideoSources.size(); i++) {
      std::string tmpInp=imagingSettingsResponse->VideoSources[i]->token;
      videoSources.push_back(tmpInp);
      if(verbosity>2) {
        fprintf(stderr, "VideoSource Token: %s\n", tmpInp.c_str());
      }
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetVideoSources return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyMedia->soap);
    tProxyMedia->soap->userid = onvifLogin.c_str();
    tProxyMedia->soap->passwd = onvifPass.c_str();
    return sendGetVideoSources(tProxyMedia, imagingSettings, imagingSettingsResponse);
  }
}

bool sendSetImagingSettings(ImagingBindingProxy* tProxyImaging, _timg__SetImagingSettings * imagingSettings,
                            _timg__SetImagingSettingsResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyImaging->SetImagingSettings(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "Send ImagingSettings OK: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendSetImagingSettings return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyImaging->soap);
    tProxyImaging->soap->userid = onvifLogin.c_str();
    tProxyImaging->soap->passwd = onvifPass.c_str();
    return sendSetImagingSettings(tProxyImaging, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGetOptions(ImagingBindingProxy* tProxyImaging, _timg__GetOptions * imagingSettings,
                    _timg__GetOptionsResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyImaging->GetOptions(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetOptions Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetOptions return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyImaging->soap);
    tProxyImaging->soap->userid = onvifLogin.c_str();
    tProxyImaging->soap->passwd = onvifPass.c_str();
    return sendGetOptions(tProxyImaging, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGetOSDs(MediaBindingProxy* tProxyMedia, _trt__GetOSDs * imagingSettings,
                 _trt__GetOSDsResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyMedia->GetOSDs(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetOSDs Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetOSDs return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyMedia->soap);
    tProxyMedia->soap->userid = onvifLogin.c_str();
    tProxyMedia->soap->passwd = onvifPass.c_str();
    return sendGetOSDs(tProxyMedia, imagingSettings, imagingSettingsResponse);
  }
}


bool sendGetOSD(MediaBindingProxy* tProxyMedia, _trt__GetOSD * imagingSettings,
                _trt__GetOSDResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyMedia->GetOSD(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetOSD Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetOSD return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyMedia->soap);
    tProxyMedia->soap->userid = onvifLogin.c_str();
    tProxyMedia->soap->passwd = onvifPass.c_str();
    return sendGetOSD(tProxyMedia, imagingSettings, imagingSettingsResponse);
  }
}

bool sendCreateOSD(MediaBindingProxy* tProxyMedia, _trt__CreateOSD * imagingSettings,
                   _trt__CreateOSDResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyMedia->CreateOSD(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "CreateOSD Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendCreateOSD return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyMedia->soap);
    tProxyMedia->soap->userid = onvifLogin.c_str();
    tProxyMedia->soap->passwd = onvifPass.c_str();
    return sendCreateOSD(tProxyMedia, imagingSettings, imagingSettingsResponse);
  }
}

bool sendDeleteOSD(MediaBindingProxy* tProxyMedia, _trt__DeleteOSD * imagingSettings,
                   _trt__DeleteOSDResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyMedia->DeleteOSD(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "DeleteOSD Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendDeleteOSD return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyMedia->soap);
    tProxyMedia->soap->userid = onvifLogin.c_str();
    tProxyMedia->soap->passwd = onvifPass.c_str();
    return sendDeleteOSD(tProxyMedia, imagingSettings, imagingSettingsResponse);
  }
}


bool sendSetOSD(MediaBindingProxy* tProxyMedia, _trt__SetOSD * imagingSettings,
                _trt__SetOSDResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyMedia->SetOSD(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "SetOSD Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendSetOSD return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyMedia->soap);
    tProxyMedia->soap->userid = onvifLogin.c_str();
    tProxyMedia->soap->passwd = onvifPass.c_str();
    return sendSetOSD(tProxyMedia, imagingSettings, imagingSettingsResponse);
  }
}



bool sendGetOSDOptions(MediaBindingProxy* tProxyMedia, _trt__GetOSDOptions * imagingSettings,
                       _trt__GetOSDOptionsResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyMedia->GetOSDOptions(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetOSDOptions Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetOSDOptions return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyMedia->soap);
    tProxyMedia->soap->userid = onvifLogin.c_str();
    tProxyMedia->soap->passwd = onvifPass.c_str();
    return sendGetOSDOptions(tProxyMedia, imagingSettings, imagingSettingsResponse);
  }
}


bool sendGetMoveOptions(ImagingBindingProxy* tProxyImaging, _timg__GetMoveOptions * imagingSettings,
                        _timg__GetMoveOptionsResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyImaging->GetMoveOptions(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetMoveOptions Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetMoveOptions return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyImaging->soap);
    tProxyImaging->soap->userid = onvifLogin.c_str();
    tProxyImaging->soap->passwd = onvifPass.c_str();
    return sendGetMoveOptions(tProxyImaging, imagingSettings, imagingSettingsResponse);
  }
}


bool sendGetStatus(ImagingBindingProxy* tProxyImaging, _timg__GetStatus * imagingSettings,
                   _timg__GetStatusResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyImaging->GetStatus(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetStatus Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetStatus return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyImaging->soap);
    tProxyImaging->soap->userid = onvifLogin.c_str();
    tProxyImaging->soap->passwd = onvifPass.c_str();
    return sendGetStatus(tProxyImaging, imagingSettings, imagingSettingsResponse);
  }
}


bool sendStop(ImagingBindingProxy* tProxyImaging, _timg__Stop * imagingSettings,
              _timg__StopResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyImaging->Stop(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "SendStop OK: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendStop return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyImaging->soap);
    tProxyImaging->soap->userid = onvifLogin.c_str();
    tProxyImaging->soap->passwd = onvifPass.c_str();
    return sendStop(tProxyImaging, imagingSettings, imagingSettingsResponse);
  }
}

bool sendMove(ImagingBindingProxy* tProxyImaging, _timg__Move * imagingSettings,
              _timg__MoveResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyImaging->Move(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "SendMove Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendMove return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyImaging->soap);
    tProxyImaging->soap->userid = onvifLogin.c_str();
    tProxyImaging->soap->passwd = onvifPass.c_str();
    return sendMove(tProxyImaging, imagingSettings, imagingSettingsResponse);
  }
}

bool sendAbsoluteMove(PTZBindingProxy* tProxyPTZ, _tptz__AbsoluteMove * imagingSettings,
                      _tptz__AbsoluteMoveResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->AbsoluteMove(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "AbsoluteMove Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendAbsoluteMove return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendAbsoluteMove(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendContinuousMove(PTZBindingProxy* tProxyPTZ, _tptz__ContinuousMove * imagingSettings,
                        _tptz__ContinuousMoveResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->ContinuousMove(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "ContinuousMove Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendContinuousMove return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendContinuousMove(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGetServiceCapabilities(PTZBindingProxy* tProxyPTZ, _tptz__GetServiceCapabilities * imagingSettings,
                                _tptz__GetServiceCapabilitiesResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->GetServiceCapabilities(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetServiceCapabilities Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetServiceCapabilities return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendGetServiceCapabilities(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGetConfigurations(PTZBindingProxy* tProxyPTZ, _tptz__GetConfigurations * imagingSettings,
                           _tptz__GetConfigurationsResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->GetConfigurations(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetConfigurations Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetConfigurations return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendGetConfigurations(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGetPresets(PTZBindingProxy* tProxyPTZ, _tptz__GetPresets * imagingSettings,
                    _tptz__GetPresetsResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->GetPresets(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetPresets Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetPresets return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendGetPresets(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendSetPreset(PTZBindingProxy* tProxyPTZ, _tptz__SetPreset * imagingSettings,
                   _tptz__SetPresetResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->SetPreset(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "SetPreset Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendSetPreset return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendSetPreset(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendRemovePreset(PTZBindingProxy* tProxyPTZ, _tptz__RemovePreset * imagingSettings,
                      _tptz__RemovePresetResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->RemovePreset(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "RemovePreset Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendRemovePreset return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendRemovePreset(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGotoPreset(PTZBindingProxy* tProxyPTZ, _tptz__GotoPreset * imagingSettings,
                    _tptz__GotoPresetResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->GotoPreset(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GotoPreset Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGotoPreset return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendGotoPreset(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendPTZGetStatus(PTZBindingProxy* tProxyPTZ, _tptz__GetStatus * imagingSettings,
                      _tptz__GetStatusResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->GetStatus(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "PTZGetStatus Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendPTZGetStatus return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendPTZGetStatus(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGetConfiguration(PTZBindingProxy* tProxyPTZ, _tptz__GetConfiguration * imagingSettings,
                          _tptz__GetConfigurationResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->GetConfiguration(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetConfiguration Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetConfiguration return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendGetConfiguration(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGetNodes(PTZBindingProxy* tProxyPTZ, _tptz__GetNodes * imagingSettings,
                  _tptz__GetNodesResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->GetNodes(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetNodes Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetNodes return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendGetNodes(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGetNode(PTZBindingProxy* tProxyPTZ, _tptz__GetNode * imagingSettings,
                 _tptz__GetNodeResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->GetNode(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetNode Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetNode return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendGetNode(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendSetConfiguration(PTZBindingProxy* tProxyPTZ, _tptz__SetConfiguration * imagingSettings,
                          _tptz__SetConfigurationResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->SetConfiguration(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "SetConfiguration Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendSetConfiguration return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendSetConfiguration(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGetConfigurationOptions(PTZBindingProxy* tProxyPTZ, _tptz__GetConfigurationOptions * imagingSettings,
                                 _tptz__GetConfigurationOptionsResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->GetConfigurationOptions(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetConfigurationOptions Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetConfigurationOptions return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendGetConfigurationOptions(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGotoHomePosition(PTZBindingProxy* tProxyPTZ, _tptz__GotoHomePosition * imagingSettings,
                          _tptz__GotoHomePositionResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->GotoHomePosition(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GotoHomePosition Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGotoHomePosition return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendGotoHomePosition(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendSetHomePosition(PTZBindingProxy* tProxyPTZ, _tptz__SetHomePosition * imagingSettings,
                         _tptz__SetHomePositionResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->SetHomePosition(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "SetHomePosition Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendSetHomePosition return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendSetHomePosition(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendRelativeMove(PTZBindingProxy* tProxyPTZ, _tptz__RelativeMove * imagingSettings,
                      _tptz__RelativeMoveResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->RelativeMove(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "RelativeMove Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendRelativeMove return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendRelativeMove(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendSendAuxiliaryCommand(PTZBindingProxy* tProxyPTZ, _tptz__SendAuxiliaryCommand * imagingSettings,
                              _tptz__SendAuxiliaryCommandResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->SendAuxiliaryCommand(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "SendAuxiliaryCommand Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendSendAuxiliaryCommand return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendSendAuxiliaryCommand(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGeoMove(PTZBindingProxy* tProxyPTZ, _tptz__GeoMove * imagingSettings,
                 _tptz__GeoMoveResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->GeoMove(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GeoMove Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGeoMove return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendGeoMove(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendPTZStop(PTZBindingProxy* tProxyPTZ, _tptz__Stop * imagingSettings,
                 _tptz__StopResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->Stop(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "PTZStop Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendPTZStop return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendPTZStop(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGetPresetTours(PTZBindingProxy* tProxyPTZ, _tptz__GetPresetTours * imagingSettings,
                        _tptz__GetPresetToursResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->GetPresetTours(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetPresetTours Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetPresetTours return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendGetPresetTours(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGetPresetTour(PTZBindingProxy* tProxyPTZ, _tptz__GetPresetTour * imagingSettings,
                       _tptz__GetPresetTourResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->GetPresetTour(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetPresetTour Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetPresetTour return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendGetPresetTour(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGetPresetTourOptions(PTZBindingProxy* tProxyPTZ, _tptz__GetPresetTourOptions * imagingSettings,
                              _tptz__GetPresetTourOptionsResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->GetPresetTourOptions(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetPresetTourOptions Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetPresetTourOptions return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendGetPresetTourOptions(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendCreatePresetTour(PTZBindingProxy* tProxyPTZ, _tptz__CreatePresetTour * imagingSettings,
                          _tptz__CreatePresetTourResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->CreatePresetTour(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "CreatePresetTour Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendCreatePresetTour return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendCreatePresetTour(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendModifyPresetTour(PTZBindingProxy* tProxyPTZ, _tptz__ModifyPresetTour * imagingSettings,
                          _tptz__ModifyPresetTourResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->ModifyPresetTour(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "ModifyPresetTour Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendModifyPresetTour return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendModifyPresetTour(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendOperatePresetTour(PTZBindingProxy* tProxyPTZ, _tptz__OperatePresetTour * imagingSettings,
                           _tptz__OperatePresetTourResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->OperatePresetTour(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "OperatePresetTour Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendOperatePresetTour return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendOperatePresetTour(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendRemovePresetTour(PTZBindingProxy* tProxyPTZ, _tptz__RemovePresetTour * imagingSettings,
                          _tptz__RemovePresetTourResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->RemovePresetTour(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "RemovePresetTour Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendRemovePresetTour return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendRemovePresetTour(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendGetCompatibleConfigurations(PTZBindingProxy* tProxyPTZ, _tptz__GetCompatibleConfigurations * imagingSettings,
                                     _tptz__GetCompatibleConfigurationsResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyPTZ->GetCompatibleConfigurations(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "GetCompatibleConfigurations Found: \n");
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetCompatibleConfigurations return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyPTZ->soap);
    tProxyPTZ->soap->userid = onvifLogin.c_str();
    tProxyPTZ->soap->passwd = onvifPass.c_str();
    return sendGetCompatibleConfigurations(tProxyPTZ, imagingSettings, imagingSettingsResponse);
  }
}

bool sendAddPTZConfiguration(MediaBindingProxy* tProxyMedia, _trt__AddPTZConfiguration * AddPTZConfiguration,
                             _trt__AddPTZConfigurationResponse * AddPTZConfigurationResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyMedia->AddPTZConfiguration(AddPTZConfiguration, *AddPTZConfigurationResponse);
  if (result == SOAP_OK) {
    if(verbosity>2)fprintf(stderr, "AddPTZConfiguration Found:\n");

    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendAddPTZConfiguration return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyMedia->soap);
    tProxyMedia->soap->userid = onvifLogin.c_str();
    tProxyMedia->soap->passwd = onvifPass.c_str();
    return sendAddPTZConfiguration(tProxyMedia, AddPTZConfiguration, AddPTZConfigurationResponse);
  }
}

bool sendGetProfiles(MediaBindingProxy* tProxyMedia, _trt__GetProfiles * getProfiles,
                     _trt__GetProfilesResponse * getProfilesResponse/*, soap * tSoap*/) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyMedia->GetProfiles(getProfiles, *getProfilesResponse);
  if (result == SOAP_OK) {
    /*
        _trt__GetStreamUri *tmpGetStreamUri = soap_new__trt__GetStreamUri(tSoap, -1);
        tmpGetStreamUri->StreamSetup = soap_new_tt__StreamSetup(tSoap, -1);
        tmpGetStreamUri->StreamSetup->Stream = tt__StreamType__RTP_Unicast;
        tmpGetStreamUri->StreamSetup->Transport = soap_new_tt__Transport(tSoap, -1);
        tmpGetStreamUri->StreamSetup->Transport->Protocol = tt__TransportProtocol__RTSP;

        _trt__GetStreamUriResponse *tmpGetStreamUriResponse = soap_new__trt__GetStreamUriResponse(tSoap, -1);
    */
    if(verbosity>2)fprintf(stderr, "MediaProfilesFound:\n");
    /*
        for (int i = 0; i < getProfilesResponse->Profiles.size(); i++) {
          if(verbosity>2)fprintf(stderr, "\t%d Name:%s\n\t\tToken:%s\n", i, getProfilesResponse->Profiles[i]->Name.c_str(),
                                   getProfilesResponse->Profiles[i]->token.c_str());
          tmpGetStreamUri->ProfileToken = getProfilesResponse->Profiles[i]->token;
          if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(tProxyMedia->soap, NULL, onvifLogin.c_str(),
                                                            onvifPass.c_str())) {
            tCount = 0;
            return false;
          }
          if (false == sendGetStreamUri(tProxyMedia, tmpGetStreamUri, tmpGetStreamUriResponse)) {
            continue;
          }
          //profNames.push_back(getProfilesResponse->Profiles[i]->Name);
          //profTokens.push_back(getProfilesResponse->Profiles[i]->token);
        }
    */

    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetProfiles return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyMedia->soap);
    tProxyMedia->soap->userid = onvifLogin.c_str();
    tProxyMedia->soap->passwd = onvifPass.c_str();
    return sendGetProfiles(tProxyMedia, getProfiles, getProfilesResponse/*, tSoap*/);
  }
}

bool sendSetVideoEncoderConfiguration(MediaBindingProxy* tProxyMedia, _trt__SetVideoEncoderConfiguration *
        SetVideoEncoderConfiguration, _trt__SetVideoEncoderConfigurationResponse * SetVideoEncoderConfigurationResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }
  int result = tProxyMedia->SetVideoEncoderConfiguration(SetVideoEncoderConfiguration,
                                                          *SetVideoEncoderConfigurationResponse);
  if (result == SOAP_OK) {
    if(verbosity>2)fprintf(stderr, "SetVideoEncoderConfigurationFound:\n");
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendSetVideoEncoderConfiguration return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyMedia->soap);
    tProxyMedia->soap->userid = onvifLogin.c_str();
    tProxyMedia->soap->passwd = onvifPass.c_str();
    return sendSetVideoEncoderConfiguration(tProxyMedia, SetVideoEncoderConfiguration,
                                             SetVideoEncoderConfigurationResponse);
  }
}

bool sendGetVideoEncoderConfiguration(MediaBindingProxy* tProxyMedia, _trt__GetVideoEncoderConfiguration *
        GetVideoEncoderConfiguration, _trt__GetVideoEncoderConfigurationResponse * GetVideoEncoderConfigurationResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }
  int result = tProxyMedia->GetVideoEncoderConfiguration(GetVideoEncoderConfiguration,
                                                          *GetVideoEncoderConfigurationResponse);
  if (result == SOAP_OK) {
    if(verbosity>2)fprintf(stderr, "GetVideoEncoderConfigurationFound:\n");
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetVideoEncoderConfiguration return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyMedia->soap);
    tProxyMedia->soap->userid = onvifLogin.c_str();
    tProxyMedia->soap->passwd = onvifPass.c_str();
    return sendGetVideoEncoderConfiguration(tProxyMedia, GetVideoEncoderConfiguration,
                                             GetVideoEncoderConfigurationResponse);
  }
}


bool sendGetStreamUri(MediaBindingProxy* tProxyMedia, _trt__GetStreamUri * streamUri,
                      _trt__GetStreamUriResponse * streamUriResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > RETRIES) {
    tCount = 0;
    return false;
  }

  int result = tProxyMedia->GetStreamUri(streamUri, *streamUriResponse);
  if (result == SOAP_OK) {
    if(verbosity>2)fprintf(stderr, "\t\tRTSP:%s\n\n", streamUriResponse->MediaUri->Uri.c_str());
    //profURIs.push_back(streamUriResponse->MediaUri->Uri);
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetStreamUri return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyMedia->soap);
    tProxyMedia->soap->userid = onvifLogin.c_str();
    tProxyMedia->soap->passwd = onvifPass.c_str();
    return sendGetStreamUri(tProxyMedia, streamUri, streamUriResponse);
  }
}

/////////////////////////////////Exec calls:

void execSystemReboot(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  _tds__SystemReboot * tmpSystemReboot;
  _tds__SystemRebootResponse * tmpSystemRebootResponse;

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyDevice.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyDevice.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyDevice.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyDevice.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyDevice.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;


  tmpSystemReboot = soap_new__tds__SystemReboot(glSoap, -1);
  tmpSystemRebootResponse = soap_new__tds__SystemRebootResponse(glSoap, -1);

  faultStr="";
  if(false == sendSystemReboot(&proxyDevice, tmpSystemReboot, tmpSystemRebootResponse)) {
    if(verbosity>2)std::cout <<  "sendSystemReboot failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendSystemReboot failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

cleanSendResponse:
  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}



void execGetImagingSettings(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  _timg__GetImagingSettings * GetImagingSettings;
  _timg__GetImagingSettingsResponse * GetImagingSettingsResponse;

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyImaging.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyImaging.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyImaging.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyImaging.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyImaging.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;


  GetImagingSettings = soap_new__timg__GetImagingSettings(glSoap, -1);
  GetImagingSettingsResponse = soap_new__timg__GetImagingSettingsResponse(glSoap, -1);
  GetImagingSettings->VideoSourceToken=videoSources[0];

  faultStr="";
  if(false == sendGetImagingSettings(&proxyImaging, GetImagingSettings, GetImagingSettingsResponse)) {
    if(verbosity>2)std::cout <<  "sendGetImagingSettings failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetImagingSettings failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }
//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{";

  if(GetImagingSettingsResponse->ImagingSettings->BacklightCompensation !=NULL) {
    outStr+="\"BacklightCompensation\":{";
    if(GetImagingSettingsResponse->ImagingSettings->BacklightCompensation->Mode == tt__BacklightCompensationMode__OFF) {
      outStr+="\"Mode\":\"OFF\", ";
    } else {
      outStr+="\"Mode\":\"ON\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->BacklightCompensation->Level!=NULL) {
      outStr+="\"Level\":\""+
              std::to_string(*GetImagingSettingsResponse->ImagingSettings->BacklightCompensation->Level)+"\"";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(GetImagingSettingsResponse->ImagingSettings->Brightness !=NULL) {
    outStr+="\"Brightness\":\""+std::to_string(*GetImagingSettingsResponse->ImagingSettings->Brightness)+"\", ";
  }
  if(GetImagingSettingsResponse->ImagingSettings->ColorSaturation !=NULL) {
    outStr+="\"ColorSaturation\":\""
            +std::to_string(*GetImagingSettingsResponse->ImagingSettings->ColorSaturation) +"\", ";
  }
  if(GetImagingSettingsResponse->ImagingSettings->Contrast !=NULL) {
    outStr+="\"Contrast\":\""+std::to_string(*GetImagingSettingsResponse->ImagingSettings->Contrast)+"\", ";
  }
  if(GetImagingSettingsResponse->ImagingSettings->Sharpness !=NULL) {
    outStr+="\"Sharpness\":\""+std::to_string(*GetImagingSettingsResponse->ImagingSettings->Sharpness)+"\", ";
  }
  if(GetImagingSettingsResponse->ImagingSettings->Exposure !=NULL) {
    outStr+="\"Exposure\":{";
    if(GetImagingSettingsResponse->ImagingSettings->Exposure->Mode == tt__ExposureMode__AUTO) {
      outStr+="\"Mode\":\"AUTO\", ";
    } else {
      outStr+="\"Mode\":\"MANUAL\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Exposure->Priority != NULL) {
      if(*GetImagingSettingsResponse->ImagingSettings->Exposure->Priority == tt__ExposurePriority__LowNoise) {
        outStr+="\"Mode\":\"LowNoise\", ";
      } else {
        outStr+="\"Mode\":\"FrameRate\", ";
      }
    }
    if(GetImagingSettingsResponse->ImagingSettings->Exposure->ExposureTime!=NULL) {
      outStr+="\"ExposureTime\":\""+
              std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->ExposureTime)+"\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Exposure->Gain!=NULL) {
      outStr+="\"Gain\":\""+
              std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->Gain)+"\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Exposure->Iris!=NULL) {
      outStr+="\"Iris\":\""+
              std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->Iris)+"\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Exposure->MaxExposureTime!=NULL) {
      outStr+="\"MaxExposureTime\":\""+
              std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->MaxExposureTime)+"\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Exposure->MaxGain!=NULL) {
      outStr+="\"MaxGain\":\""+
              std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->MaxGain)+"\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Exposure->MaxIris!=NULL) {
      outStr+="\"MaxIris\":\""+
              std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->MaxIris)+"\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Exposure->MinExposureTime!=NULL) {
      outStr+="\"MinExposureTime\":\""+
              std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->MinExposureTime)+"\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Exposure->MinGain!=NULL) {
      outStr+="\"MinGain\":\""+
              std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->MinGain)+"\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Exposure->MinIris!=NULL) {
      outStr+="\"MinIris\":\""+
              std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->MinIris)+"\"";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(GetImagingSettingsResponse->ImagingSettings->Focus !=NULL) {
    outStr+="\"Focus\":{";
    if(GetImagingSettingsResponse->ImagingSettings->Focus->AutoFocusMode == tt__AutoFocusMode__AUTO) {
      outStr+="\"AutoFocusMode\":\"AUTO\", ";
    } else {
      outStr+="\"AutoFocusMode\":\"MANUAL\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Focus->DefaultSpeed!=NULL) {
      outStr+="\"DefaultSpeed\":\""+
              std::to_string(*GetImagingSettingsResponse->ImagingSettings->Focus->DefaultSpeed)+"\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Focus->FarLimit!=NULL) {
      outStr+="\"FarLimit\":\""+
              std::to_string(*GetImagingSettingsResponse->ImagingSettings->Focus->FarLimit)+"\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Focus->NearLimit!=NULL) {
      outStr+="\"NearLimit\":\""+
              std::to_string(*GetImagingSettingsResponse->ImagingSettings->Focus->NearLimit)+"\"";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(GetImagingSettingsResponse->ImagingSettings->WideDynamicRange !=NULL) {
    outStr+="\"WideDynamicRange\":{";
    if(GetImagingSettingsResponse->ImagingSettings->WideDynamicRange->Mode == tt__WideDynamicMode__OFF) {
      outStr+="\"Mode\":\"OFF\", ";
    } else {
      outStr+="\"Mode\":\"ON\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->WideDynamicRange->Level!=NULL) {
      outStr+="\"Level\":\""+
              std::to_string(*GetImagingSettingsResponse->ImagingSettings->WideDynamicRange->Level)+"\"";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(GetImagingSettingsResponse->ImagingSettings->IrCutFilter !=NULL) {
    outStr+="\"IrCutFilter\":";
    if(*GetImagingSettingsResponse->ImagingSettings->IrCutFilter == tt__IrCutFilterMode__OFF) {
      outStr+="\"OFF\", ";
    } else if(*GetImagingSettingsResponse->ImagingSettings->IrCutFilter == tt__IrCutFilterMode__ON) {
      outStr+="\"ON\", ";
    } else {
      outStr+="\"AUTO\", ";
    }
  }
  if(GetImagingSettingsResponse->ImagingSettings->WhiteBalance !=NULL) {
    outStr+="\"WhiteBalance\":{";
    if(GetImagingSettingsResponse->ImagingSettings->WhiteBalance->Mode == tt__WhiteBalanceMode__AUTO) {
      outStr+="\"Mode\":\"AUTO\", ";
    } else {
      outStr+="\"Mode\":\"MANUAL\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->WhiteBalance->CbGain!=NULL) {
      outStr+="\"CbGain\":\""+
              std::to_string(*GetImagingSettingsResponse->ImagingSettings->WhiteBalance->CbGain)+"\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->WhiteBalance->CrGain!=NULL) {
      outStr+="\"CrGain\":\""+
              std::to_string(*GetImagingSettingsResponse->ImagingSettings->WhiteBalance->CrGain)+"\"";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
  outStr+="}}";


//End process response
cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}


void execGetStatus(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  _timg__GetStatus * GetStatus;
  _timg__GetStatusResponse * GetStatusResponse;

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyImaging.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyImaging.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyImaging.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyImaging.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyImaging.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;


  GetStatus = soap_new__timg__GetStatus(glSoap, -1);
  GetStatusResponse = soap_new__timg__GetStatusResponse(glSoap, -1);
  GetStatus->VideoSourceToken=videoSources[0];

  faultStr="";
  if(false == sendGetStatus(&proxyImaging, GetStatus, GetStatusResponse)) {
    if(verbosity>2)std::cout <<  "sendGetStatus failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetStatus failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }
//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{";

  if(GetStatusResponse->Status!=NULL) {
    outStr+="\"Status\":{";
    if(GetStatusResponse->Status->FocusStatus20 !=NULL) {
      outStr+="\"FocusStatus20\":{";
      if(GetStatusResponse->Status->FocusStatus20->Error !=NULL) {
        outStr+="\"Error\":\""+ (*GetStatusResponse->Status->FocusStatus20->Error)+"\", ";
      }
      outStr+="\"MoveStatus\":\"";
      if(GetStatusResponse->Status->FocusStatus20->MoveStatus==tt__MoveStatus__IDLE)
        outStr+="IDLE\", ";
      else if(GetStatusResponse->Status->FocusStatus20->MoveStatus==tt__MoveStatus__MOVING)
        outStr+="MOVING\", ";
      else if(GetStatusResponse->Status->FocusStatus20->MoveStatus==tt__MoveStatus__UNKNOWN)
        outStr+="UNKNOWN\", ";
      else outStr+="UNKNOWN\", ";
      outStr+="\"Position\":\""+
              std::to_string(GetStatusResponse->Status->FocusStatus20->Position)+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}";
  }
  if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
  outStr+="}}";


//End process response
cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execSetImagingSettings(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;
  _timg__GetImagingSettings * GetImagingSettings;
  _timg__GetImagingSettingsResponse * GetImagingSettingsResponse;
  _timg__SetImagingSettings * SetImagingSettings;
  _timg__SetImagingSettingsResponse * SetImagingSettingsResponse;

  if (d1.HasMember(CMD_PARAMS)) {
    //tmpVar=std::string(d1[CMD_PARAMS].GetString());
  } else {
    std::cout << "Failed to process request" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyImaging.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyImaging.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyImaging.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyImaging.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyImaging.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;


  GetImagingSettings = soap_new__timg__GetImagingSettings(glSoap, -1);
  GetImagingSettingsResponse = soap_new__timg__GetImagingSettingsResponse(glSoap, -1);
  GetImagingSettings->VideoSourceToken=videoSources[0];

  faultStr="";
  if(false == sendGetImagingSettings(&proxyImaging, GetImagingSettings, GetImagingSettingsResponse)) {
    if(verbosity>2)std::cout <<  "sendGetImagingSettings failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetImagingSettings failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

  //SetImagingSettings = soap_new__timg__SetImagingSettings(glSoap, -1);
  SetImagingSettings = soap_new__timg__SetImagingSettings(glSoap);
  SetImagingSettingsResponse = soap_new__timg__SetImagingSettingsResponse(glSoap, -1);
  SetImagingSettings->ImagingSettings=GetImagingSettingsResponse->ImagingSettings;
  SetImagingSettings->VideoSourceToken=videoSources[0];

//Process response

  if((SetImagingSettings->ImagingSettings->BacklightCompensation !=NULL) &&
      (d1[CMD_PARAMS].HasMember("BacklightCompensation"))) {
    tmpVar=std::string(d1[CMD_PARAMS]["BacklightCompensation"]["Mode"].GetString());
    if(tmpVar=="OFF") {
      SetImagingSettings->ImagingSettings->BacklightCompensation->Mode = tt__BacklightCompensationMode__OFF;
    } else {
      SetImagingSettings->ImagingSettings->BacklightCompensation->Mode = tt__BacklightCompensationMode__ON;
    }
    if((SetImagingSettings->ImagingSettings->BacklightCompensation->Level!=NULL) &&
        (d1[CMD_PARAMS]["BacklightCompensation"].HasMember("Level"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["BacklightCompensation"]["Level"].GetString());
      (*SetImagingSettings->ImagingSettings->BacklightCompensation->Level)=std::stof(tmpVar);
    }
  }
  if((SetImagingSettings->ImagingSettings->Brightness !=NULL) &&
      (d1[CMD_PARAMS].HasMember("Brightness"))) {
    tmpVar=std::string(d1[CMD_PARAMS]["Brightness"].GetString());
    (*SetImagingSettings->ImagingSettings->Brightness)=std::stof(tmpVar);
  }
  if((SetImagingSettings->ImagingSettings->ColorSaturation !=NULL) &&
      (d1[CMD_PARAMS].HasMember("ColorSaturation"))) {
    tmpVar=std::string(d1[CMD_PARAMS]["ColorSaturation"].GetString());
    (*SetImagingSettings->ImagingSettings->ColorSaturation)=std::stof(tmpVar);
  }
  if((SetImagingSettings->ImagingSettings->Contrast !=NULL) &&
      (d1[CMD_PARAMS].HasMember("Contrast"))) {
    tmpVar=std::string(d1[CMD_PARAMS]["Contrast"].GetString());
    (*SetImagingSettings->ImagingSettings->Contrast)=std::stof(tmpVar);
  }
  if((SetImagingSettings->ImagingSettings->Sharpness !=NULL) &&
      (d1[CMD_PARAMS].HasMember("Sharpness"))) {
    tmpVar=std::string(d1[CMD_PARAMS]["Sharpness"].GetString());
    (*SetImagingSettings->ImagingSettings->Sharpness)=std::stof(tmpVar);
  }
  if((SetImagingSettings->ImagingSettings->Exposure !=NULL) &&
      (d1[CMD_PARAMS].HasMember("Exposure"))) {
    tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["Mode"].GetString());
    if(tmpVar=="AUTO") {
      SetImagingSettings->ImagingSettings->Exposure->Mode = tt__ExposureMode__AUTO;
    } else {
      SetImagingSettings->ImagingSettings->Exposure->Mode = tt__ExposureMode__MANUAL;
    }
    if((SetImagingSettings->ImagingSettings->Exposure->Priority != NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("Priority"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["Priority"].GetString());
      if(tmpVar=="LowNoise") {
        (*SetImagingSettings->ImagingSettings->Exposure->Priority)=tt__ExposurePriority__LowNoise;
      } else {
        (*SetImagingSettings->ImagingSettings->Exposure->Priority)=tt__ExposurePriority__FrameRate;
      }
    }
    if((SetImagingSettings->ImagingSettings->Exposure->ExposureTime!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("ExposureTime"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["ExposureTime"].GetString());
      (*SetImagingSettings->ImagingSettings->Exposure->ExposureTime)=std::stof(tmpVar);
    }
    if((SetImagingSettings->ImagingSettings->Exposure->Gain!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("Gain"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["Gain"].GetString());
      (*SetImagingSettings->ImagingSettings->Exposure->Gain)=std::stof(tmpVar);
    }
    if((SetImagingSettings->ImagingSettings->Exposure->Iris!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("Iris"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["Iris"].GetString());
      (*SetImagingSettings->ImagingSettings->Exposure->Iris)=std::stof(tmpVar);
    }
    if((SetImagingSettings->ImagingSettings->Exposure->MaxExposureTime!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("MaxExposureTime"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["MaxExposureTime"].GetString());
      (*SetImagingSettings->ImagingSettings->Exposure->MaxExposureTime)=std::stof(tmpVar);
    }
    if((SetImagingSettings->ImagingSettings->Exposure->MaxGain!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("MaxGain"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["MaxGain"].GetString());
      (*SetImagingSettings->ImagingSettings->Exposure->MaxGain)=std::stof(tmpVar);
    }
    if((SetImagingSettings->ImagingSettings->Exposure->MaxIris!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("MaxIris"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["MaxIris"].GetString());
      (*SetImagingSettings->ImagingSettings->Exposure->MaxIris)=std::stof(tmpVar);
    }
    if((SetImagingSettings->ImagingSettings->Exposure->MinExposureTime!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("MinExposureTime"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["MinExposureTime"].GetString());
      (*SetImagingSettings->ImagingSettings->Exposure->MinExposureTime)=std::stof(tmpVar);
    }
    if((SetImagingSettings->ImagingSettings->Exposure->MinGain!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("MinGain"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["MinGain"].GetString());
      (*SetImagingSettings->ImagingSettings->Exposure->MinGain)=std::stof(tmpVar);
    }
    if((SetImagingSettings->ImagingSettings->Exposure->MinIris!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("MinIris"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["MinIris"].GetString());
      (*SetImagingSettings->ImagingSettings->Exposure->MinIris)=std::stof(tmpVar);
    }
  }
  if((SetImagingSettings->ImagingSettings->Focus !=NULL) &&
      (d1[CMD_PARAMS].HasMember("Focus"))) {
    tmpVar=std::string(d1[CMD_PARAMS]["Focus"]["AutoFocusMode"].GetString());
    if(tmpVar=="AUTO") {
      SetImagingSettings->ImagingSettings->Focus->AutoFocusMode = tt__AutoFocusMode__AUTO;
    } else {
      SetImagingSettings->ImagingSettings->Focus->AutoFocusMode = tt__AutoFocusMode__MANUAL;
    }
    if((SetImagingSettings->ImagingSettings->Focus->DefaultSpeed!=NULL) &&
        (d1[CMD_PARAMS]["Focus"].HasMember("DefaultSpeed"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["Focus"]["DefaultSpeed"].GetString());
      (*SetImagingSettings->ImagingSettings->Focus->DefaultSpeed)=std::stof(tmpVar);
    }
    if((SetImagingSettings->ImagingSettings->Focus->FarLimit!=NULL) &&
        (d1[CMD_PARAMS]["Focus"].HasMember("FarLimit"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["Focus"]["FarLimit"].GetString());
      (*SetImagingSettings->ImagingSettings->Focus->FarLimit)=std::stof(tmpVar);
    }
    if((SetImagingSettings->ImagingSettings->Focus->NearLimit!=NULL) &&
        (d1[CMD_PARAMS]["Focus"].HasMember("NearLimit"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["Focus"]["NearLimit"].GetString());
      (*SetImagingSettings->ImagingSettings->Focus->NearLimit)=std::stof(tmpVar);
    }
  }
  if((SetImagingSettings->ImagingSettings->WideDynamicRange !=NULL) &&
      (d1[CMD_PARAMS].HasMember("WideDynamicRange"))) {
    tmpVar=std::string(d1[CMD_PARAMS]["WideDynamicRange"]["Mode"].GetString());
    if(tmpVar=="OFF") {
      SetImagingSettings->ImagingSettings->WideDynamicRange->Mode = tt__WideDynamicMode__OFF;
    } else {
      SetImagingSettings->ImagingSettings->WideDynamicRange->Mode = tt__WideDynamicMode__ON;
    }

    if((SetImagingSettings->ImagingSettings->WideDynamicRange->Level!=NULL) &&
        (d1[CMD_PARAMS]["WideDynamicRange"].HasMember("Level"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["WideDynamicRange"]["Level"].GetString());
      (*SetImagingSettings->ImagingSettings->WideDynamicRange->Level)=std::stof(tmpVar);
    }
  }
  if((SetImagingSettings->ImagingSettings->IrCutFilter !=NULL) &&
      (d1[CMD_PARAMS].HasMember("IrCutFilter"))) {
    tmpVar=std::string(d1[CMD_PARAMS]["IrCutFilter"].GetString());
    if(tmpVar=="OFF") {
      (*SetImagingSettings->ImagingSettings->IrCutFilter) = tt__IrCutFilterMode__OFF;
    } else if(tmpVar=="ON") {
      (*SetImagingSettings->ImagingSettings->IrCutFilter) = tt__IrCutFilterMode__ON;
    } else {
      (*SetImagingSettings->ImagingSettings->IrCutFilter) = tt__IrCutFilterMode__AUTO;
    }
  }
  if((SetImagingSettings->ImagingSettings->WhiteBalance !=NULL) &&
      (d1[CMD_PARAMS].HasMember("WhiteBalance"))) {
    tmpVar=std::string(d1[CMD_PARAMS]["WhiteBalance"]["Mode"].GetString());
    if(tmpVar=="AUTO") {
      SetImagingSettings->ImagingSettings->WhiteBalance->Mode = tt__WhiteBalanceMode__AUTO;
    } else {
      SetImagingSettings->ImagingSettings->WhiteBalance->Mode = tt__WhiteBalanceMode__MANUAL;
    }
    if((SetImagingSettings->ImagingSettings->WhiteBalance->CbGain!=NULL) &&
        (d1[CMD_PARAMS]["WhiteBalance"].HasMember("CbGain"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["WhiteBalance"]["CbGain"].GetString());
      (*SetImagingSettings->ImagingSettings->WhiteBalance->CbGain)=std::stof(tmpVar);
    }
    if((SetImagingSettings->ImagingSettings->WhiteBalance->CrGain!=NULL) &&
        (d1[CMD_PARAMS]["WhiteBalance"].HasMember("CrGain"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["WhiteBalance"]["CrGain"].GetString());
      (*SetImagingSettings->ImagingSettings->WhiteBalance->CrGain)=std::stof(tmpVar);
    }
  }

//End process response

  faultStr="";
  if(false == sendSetImagingSettings(&proxyImaging, SetImagingSettings, SetImagingSettingsResponse)) {
    if(verbosity>2)std::cout <<  "sendSetImagingSettings failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendSetImagingSettings failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);

}

void execGetOSDs(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  /*
  bool sendGetOSDs(MediaBindingProxy* tProxyMedia, _trt__GetOSDs * imagingSettings,
                  _trt__GetOSDsResponse * imagingSettingsResponse);
  */
  _trt__GetOSDs * GetOSDs;
  _trt__GetOSDsResponse * GetOSDsResponse;

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyMedia.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyMedia.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyMedia.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;


  GetOSDs = soap_new__trt__GetOSDs(glSoap, -1);
  GetOSDsResponse = soap_new__trt__GetOSDsResponse(glSoap, -1);

  faultStr="";
  if(false == sendGetOSDs(&proxyMedia, GetOSDs, GetOSDsResponse)) {
    if(verbosity>2)std::cout <<  "sendGetOSDs failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetOSDs failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }
//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{";
  if(verbosity>3) std::cout << "Number of OSDs received is " << GetOSDsResponse->OSDs.size() << std::endl;
  if(GetOSDsResponse->OSDs.size()>0) {
    outStr+="\"OSDs\":[";
    for (unsigned i=0; i<GetOSDsResponse->OSDs.size(); i++) {
      if(i>0)outStr+=", ";
      outStr+="{";
      tt__OSDConfiguration* tmpOSD=GetOSDsResponse->OSDs[i];
      if(tmpOSD->Position!=NULL) {
        outStr+="\"Position\":{";
        if(tmpOSD->Position->Pos !=NULL) {
          outStr+="\"Pos\":{";
          outStr+="\"x\":\""+std::to_string(*tmpOSD->Position->Pos->x)+"\", ";
          outStr+="\"y\":\""+std::to_string(*tmpOSD->Position->Pos->y)+"\"";
          outStr+="}, ";
        }
        outStr+="\"Type\":\""+tmpOSD->Position->Type+"\"";
        if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
        outStr+="}, ";
      }
      if(tmpOSD->TextString!=NULL) {
        outStr+="\"TextString\":{";
        if(tmpOSD->TextString->DateFormat!=NULL) {
          outStr+="\"DateFormat\":\""+(*tmpOSD->TextString->DateFormat)+"\", ";
        }
        if(tmpOSD->TextString->PlainText!=NULL) {
          outStr+="\"PlainText\":\""+(*tmpOSD->TextString->PlainText)+"\", ";
        }
        if(tmpOSD->TextString->TimeFormat!=NULL) {
          outStr+="\"TimeFormat\":\""+(*tmpOSD->TextString->TimeFormat)+"\", ";
        }
        if(tmpOSD->TextString->FontSize!=NULL) {
          outStr+="\"FontSize\":\""+std::to_string(*tmpOSD->TextString->FontSize)+"\", ";
        }
        if(tmpOSD->TextString->BackgroundColor!=NULL) {
          outStr+="\"BackgroundColor\":{";
          if(tmpOSD->TextString->BackgroundColor->Color!=NULL) {
            if(tmpOSD->TextString->BackgroundColor->Color->Colorspace!=NULL) {
              outStr+="\"Colorspace\":\""+(*tmpOSD->TextString->BackgroundColor->Color->Colorspace)+"\", ";
            }
            outStr+="\"X\":\""+std::to_string(tmpOSD->TextString->BackgroundColor->Color->X)+"\", ";
            outStr+="\"Y\":\""+std::to_string(tmpOSD->TextString->BackgroundColor->Color->Y)+"\", ";
            outStr+="\"Z\":\""+std::to_string(tmpOSD->TextString->BackgroundColor->Color->Z)+"\"";
          }
          if(tmpOSD->TextString->BackgroundColor->Transparent!=NULL) {
            outStr+="\"Transparent\":\""+std::to_string(*tmpOSD->TextString->BackgroundColor->Transparent)+"\"";
          }
          if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
          outStr+="}, ";
        }
        if(tmpOSD->TextString->FontColor!=NULL) {
          outStr+="\"FontColor\":{";
          if(tmpOSD->TextString->FontColor->Color!=NULL) {
            if(tmpOSD->TextString->FontColor->Color->Colorspace!=NULL) {
              outStr+="\"Colorspace\":\""+(*tmpOSD->TextString->FontColor->Color->Colorspace)+"\", ";
            }
            outStr+="\"X\":\""+std::to_string(tmpOSD->TextString->FontColor->Color->X)+"\", ";
            outStr+="\"Y\":\""+std::to_string(tmpOSD->TextString->FontColor->Color->Y)+"\", ";
            outStr+="\"Z\":\""+std::to_string(tmpOSD->TextString->FontColor->Color->Z)+"\"";
          }
          if(tmpOSD->TextString->FontColor->Transparent!=NULL) {
            outStr+="\"Transparent\":\""+std::to_string(*tmpOSD->TextString->FontColor->Transparent)+"\"";
          }
          if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
          outStr+="}, ";
        }
        if(tmpOSD->TextString->IsPersistentText!=NULL) {
          outStr+="\"IsPersistentText\":\"";
          if(*tmpOSD->TextString->IsPersistentText)outStr+="true";
          else outStr+="false";
          outStr+="\", ";
        }
        outStr+="\"Type\":\""+tmpOSD->TextString->Type+"\"";
        if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
        outStr+="}, ";
      }
      if(tmpOSD->VideoSourceConfigurationToken!=NULL) {
        outStr+="\"VideoSourceConfigurationToken\":\""+tmpOSD->VideoSourceConfigurationToken->__item+"\", ";
      }
      outStr+="\"Type\":\"";
      switch(tmpOSD->Type) {
      case tt__OSDType__Text:
        outStr+="Text\", ";
        break;
      case tt__OSDType__Image:
        outStr+="Image\", ";
        break;
      case tt__OSDType__Extended:
        outStr+="Extended\", ";
        break;
      }
      outStr+="\"token\":\""+tmpOSD->token+"\"";
      outStr+="}";
    }
    outStr+="]";
  }
  outStr+="}}";



//End process response
cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGetOSD(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string OSDToken="";
  /*
  bool sendGetOSD(MediaBindingProxy* tProxyMedia, _trt__GetOSD * imagingSettings,
                  _trt__GetOSDResponse * imagingSettingsResponse);
  */
  _trt__GetOSD * GetOSD;
  _trt__GetOSDResponse * GetOSDResponse;

  if (d1.HasMember(CMD_PARAMS)) {
    if(d1[CMD_PARAMS].HasMember("OSDToken"))
      OSDToken=std::string(d1[CMD_PARAMS]["OSDToken"].GetString());
    else {
      std::cout << "Failed to process request, No OSDToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No OSDToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyMedia.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyMedia.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyMedia.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;


  GetOSD = soap_new__trt__GetOSD(glSoap, -1);
  GetOSDResponse = soap_new__trt__GetOSDResponse(glSoap, -1);
  GetOSD->OSDToken=OSDToken;

  faultStr="";
  if(false == sendGetOSD(&proxyMedia, GetOSD, GetOSDResponse)) {
    if(verbosity>2)std::cout <<  "sendGetOSD failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetOSD failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }
//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{";
  if(GetOSDResponse->OSD!=NULL) {
    outStr+="\"OSD\":{";
    tt__OSDConfiguration* tmpOSD=GetOSDResponse->OSD;
    if(tmpOSD->Position!=NULL) {
      outStr+="\"Position\":{";
      if(tmpOSD->Position->Pos !=NULL) {
        outStr+="\"Pos\":{";
        outStr+="\"x\":\""+std::to_string(*tmpOSD->Position->Pos->x)+"\", ";
        outStr+="\"y\":\""+std::to_string(*tmpOSD->Position->Pos->y)+"\"";
        outStr+="}, ";
      }
      outStr+="\"Type\":\""+tmpOSD->Position->Type+"\"";
      if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
      outStr+="}, ";
    }
    if(tmpOSD->TextString!=NULL) {
      outStr+="\"TextString\":{";
      if(tmpOSD->TextString->DateFormat!=NULL) {
        outStr+="\"DateFormat\":\""+(*tmpOSD->TextString->DateFormat)+"\", ";
      }
      if(tmpOSD->TextString->PlainText!=NULL) {
        outStr+="\"PlainText\":\""+(*tmpOSD->TextString->PlainText)+"\", ";
      }
      if(tmpOSD->TextString->TimeFormat!=NULL) {
        outStr+="\"TimeFormat\":\""+(*tmpOSD->TextString->TimeFormat)+"\", ";
      }
      if(tmpOSD->TextString->FontSize!=NULL) {
        outStr+="\"FontSize\":\""+std::to_string(*tmpOSD->TextString->FontSize)+"\", ";
      }
      if(tmpOSD->TextString->BackgroundColor!=NULL) {
        outStr+="\"BackgroundColor\":{";
        if(tmpOSD->TextString->BackgroundColor->Color!=NULL) {
          if(tmpOSD->TextString->BackgroundColor->Color->Colorspace!=NULL) {
            outStr+="\"Colorspace\":\""+(*tmpOSD->TextString->BackgroundColor->Color->Colorspace)+"\", ";
          }
          outStr+="\"X\":\""+std::to_string(tmpOSD->TextString->BackgroundColor->Color->X)+"\", ";
          outStr+="\"Y\":\""+std::to_string(tmpOSD->TextString->BackgroundColor->Color->Y)+"\", ";
          outStr+="\"Z\":\""+std::to_string(tmpOSD->TextString->BackgroundColor->Color->Z)+"\"";
        }
        if(tmpOSD->TextString->BackgroundColor->Transparent!=NULL) {
          outStr+="\"Transparent\":\""+std::to_string(*tmpOSD->TextString->BackgroundColor->Transparent)+"\"";
        }
        if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
        outStr+="}, ";
      }
      if(tmpOSD->TextString->FontColor!=NULL) {
        outStr+="\"FontColor\":{";
        if(tmpOSD->TextString->FontColor->Color!=NULL) {
          if(tmpOSD->TextString->FontColor->Color->Colorspace!=NULL) {
            outStr+="\"Colorspace\":\""+(*tmpOSD->TextString->FontColor->Color->Colorspace)+"\", ";
          }
          outStr+="\"X\":\""+std::to_string(tmpOSD->TextString->FontColor->Color->X)+"\", ";
          outStr+="\"Y\":\""+std::to_string(tmpOSD->TextString->FontColor->Color->Y)+"\", ";
          outStr+="\"Z\":\""+std::to_string(tmpOSD->TextString->FontColor->Color->Z)+"\"";
        }
        if(tmpOSD->TextString->FontColor->Transparent!=NULL) {
          outStr+="\"Transparent\":\""+std::to_string(*tmpOSD->TextString->FontColor->Transparent)+"\"";
        }
        if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
        outStr+="}, ";
      }
      if(tmpOSD->TextString->IsPersistentText!=NULL) {
        outStr+="\"IsPersistentText\":\"";
        if(*tmpOSD->TextString->IsPersistentText)outStr+="true";
        else outStr+="false";
        outStr+="\", ";
      }
      outStr+="\"Type\":\""+tmpOSD->TextString->Type+"\"";
      if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
      outStr+="}, ";
    }
    if(tmpOSD->VideoSourceConfigurationToken!=NULL) {
      outStr+="\"VideoSourceConfigurationToken\":\""+tmpOSD->VideoSourceConfigurationToken->__item+"\", ";
    }
    outStr+="\"Type\":\"";
    switch(tmpOSD->Type) {
    case tt__OSDType__Text:
      outStr+="Text\", ";
      break;
    case tt__OSDType__Image:
      outStr+="Image\", ";
      break;
    case tt__OSDType__Extended:
      outStr+="Extended\", ";
      break;
    }
    outStr+="\"token\":\""+tmpOSD->token+"\"";
    outStr+="}";
  }
  outStr+="}}";



//End process response
cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execSetOSD(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string OSDToken="";
  std::string tmpVar;

  _trt__GetOSD * GetOSD;
  _trt__GetOSDResponse * GetOSDResponse;
  _trt__SetOSD * SetOSD;
  _trt__SetOSDResponse * SetOSDResponse;

  if (d1.HasMember(CMD_PARAMS)) {
    if(d1[CMD_PARAMS].HasMember("OSDToken"))
      OSDToken=std::string(d1[CMD_PARAMS]["OSDToken"].GetString());
    else {
      std::cout << "Failed to process request, No OSDToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No OSDToken found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS].HasMember("OSD")) {
      std::cout << "Failed to process request, No OSD found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No OSD found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyMedia.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyMedia.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyMedia.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;


  GetOSD = soap_new__trt__GetOSD(glSoap, -1);
  GetOSDResponse = soap_new__trt__GetOSDResponse(glSoap, -1);
  GetOSD->OSDToken=OSDToken;

  faultStr="";
  if(false == sendGetOSD(&proxyMedia, GetOSD, GetOSDResponse)) {
    if(verbosity>2)std::cout <<  "sendGetOSD failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetOSD failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

  /*
  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyMedia.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto cleanSendResponse;
  }
  */

  //SetOSD = soap_new__trt__SetOSD(glSoap, -1);
  SetOSD = soap_new__trt__SetOSD(glSoap);
  SetOSDResponse = soap_new__trt__SetOSDResponse(glSoap, -1);
  //if(SetOSD->OSD!=NULL)SetOSD->OSD->soap_del();
  //SetOSD->OSD=GetOSDResponse->OSD->soap_dup(glSoap);
  SetOSD->OSD=GetOSDResponse->OSD;

//Prepare request

  if(SetOSD->OSD!=NULL) {
    tt__OSDConfiguration* tmpOSD=SetOSD->OSD;
    if((tmpOSD->Position!=NULL) && (d1[CMD_PARAMS]["OSD"].HasMember("Position"))) {
      if((tmpOSD->Position->Pos!=NULL) && (d1[CMD_PARAMS]["OSD"]["Position"].HasMember("Pos"))) {
        tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["Position"]["Pos"]["x"].GetString());
        (*tmpOSD->Position->Pos->x)=std::stof(tmpVar);
        tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["Position"]["Pos"]["y"].GetString());
        (*tmpOSD->Position->Pos->y)=std::stof(tmpVar);
      }
      if(d1[CMD_PARAMS]["OSD"]["Position"].HasMember("Type")) {
        tmpOSD->Position->Type=std::string(d1[CMD_PARAMS]["OSD"]["Position"]["Type"].GetString());
      }
    }
    if((tmpOSD->TextString!=NULL) && (d1[CMD_PARAMS]["OSD"].HasMember("TextString"))) {
      if((tmpOSD->TextString->DateFormat!=NULL) && (d1[CMD_PARAMS]["OSD"]["TextString"].HasMember("DateFormat"))) {
        (*tmpOSD->TextString->DateFormat)=
          std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["DateFormat"].GetString());
      }
      if((tmpOSD->TextString->PlainText!=NULL) && (d1[CMD_PARAMS]["OSD"]["TextString"].HasMember("PlainText"))) {
        (*tmpOSD->TextString->PlainText)=
          std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["PlainText"].GetString());
      }
      if((tmpOSD->TextString->TimeFormat!=NULL) && (d1[CMD_PARAMS]["OSD"]["TextString"].HasMember("TimeFormat"))) {
        (*tmpOSD->TextString->TimeFormat)=
          std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["TimeFormat"].GetString());
      }
      if((tmpOSD->TextString->FontSize!=NULL) && (d1[CMD_PARAMS]["OSD"]["TextString"].HasMember("FontSize"))) {
        tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["FontSize"].GetString());
        (*tmpOSD->TextString->FontSize)=std::stoi(tmpVar);
      }
      if((tmpOSD->TextString->IsPersistentText!=NULL) &&
          (d1[CMD_PARAMS]["OSD"]["TextString"].HasMember("IsPersistentText"))) {
        tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["IsPersistentText"].GetString());
        if(tmpVar=="true")(*tmpOSD->TextString->IsPersistentText)=true;
        else (*tmpOSD->TextString->IsPersistentText)=false;
      }
      if(d1[CMD_PARAMS]["OSD"]["TextString"].HasMember("Type")) {
        tmpOSD->TextString->Type=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["Type"].GetString());
      }
      if((tmpOSD->TextString->BackgroundColor!=NULL) &&
          (d1[CMD_PARAMS]["OSD"]["TextString"].HasMember("BackgroundColor"))) {
        if((tmpOSD->TextString->BackgroundColor->Color!=NULL) &&
            (d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"].HasMember("Color"))) {
          if((tmpOSD->TextString->BackgroundColor->Color->Colorspace!=NULL) &&
              (d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Color"].HasMember("Colorspace"))) {
            (*tmpOSD->TextString->BackgroundColor->Color->Colorspace)=
              std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Color"]["Colorspace"].GetString());
          }
          if((d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Color"].HasMember("X"))) {
            tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Color"]["X"].GetString());
            tmpOSD->TextString->BackgroundColor->Color->X=std::stof(tmpVar);
          }
          if((d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Color"].HasMember("Y"))) {
            tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Color"]["Y"].GetString());
            tmpOSD->TextString->BackgroundColor->Color->Y=std::stof(tmpVar);
          }
          if((d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Color"].HasMember("Z"))) {
            tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Color"]["Z"].GetString());
            tmpOSD->TextString->BackgroundColor->Color->Z=std::stof(tmpVar);
          }
        }
        if((tmpOSD->TextString->BackgroundColor->Transparent!=NULL) &&
            (d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"].HasMember("Transparent"))) {
          tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Transparent"].GetString());
          (*tmpOSD->TextString->BackgroundColor->Transparent)=std::stoi(tmpVar);
        }
      }
      if((tmpOSD->TextString->FontColor!=NULL) &&
          (d1[CMD_PARAMS]["OSD"]["TextString"].HasMember("FontColor"))) {
        if((tmpOSD->TextString->FontColor->Color!=NULL) &&
            (d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"].HasMember("Color"))) {
          if((tmpOSD->TextString->FontColor->Color->Colorspace!=NULL) &&
              (d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Color"].HasMember("Colorspace"))) {
            (*tmpOSD->TextString->FontColor->Color->Colorspace)=
              std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Color"]["Colorspace"].GetString());
          }
          if((d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Color"].HasMember("X"))) {
            tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Color"]["X"].GetString());
            tmpOSD->TextString->FontColor->Color->X=std::stof(tmpVar);
          }
          if((d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Color"].HasMember("Y"))) {
            tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Color"]["Y"].GetString());
            tmpOSD->TextString->FontColor->Color->Y=std::stof(tmpVar);
          }
          if((d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Color"].HasMember("Z"))) {
            tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Color"]["Z"].GetString());
            tmpOSD->TextString->FontColor->Color->Z=std::stof(tmpVar);
          }
        }
        if((tmpOSD->TextString->FontColor->Transparent!=NULL) &&
            (d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"].HasMember("Transparent"))) {
          tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Transparent"].GetString());
          (*tmpOSD->TextString->FontColor->Transparent)=std::stoi(tmpVar);
        }
      }
    }
    if((tmpOSD->VideoSourceConfigurationToken!=NULL) &&
        (d1[CMD_PARAMS]["OSD"].HasMember("VideoSourceConfigurationToken"))) {
      (tmpOSD->VideoSourceConfigurationToken->__item)=
        std::string(d1[CMD_PARAMS]["OSD"]["VideoSourceConfigurationToken"].GetString());
    }
    if(d1[CMD_PARAMS]["OSD"].HasMember("Type")) {
      tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["Type"].GetString());
      if(tmpVar=="Text")tmpOSD->Type=tt__OSDType__Text;
      if(tmpVar=="Image")tmpOSD->Type=tt__OSDType__Image;
      if(tmpVar=="Extended")tmpOSD->Type=tt__OSDType__Extended;
    }
  } else {
    std::cout << "Failed to get valid OSD" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to get valid OSD\"}";
    goto cleanSendResponse;
  }

//End prepare request

  faultStr="";
  if(false == sendSetOSD(&proxyMedia, SetOSD, SetOSDResponse)) {
    if(verbosity>2)std::cout <<  "sendSetOSD failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendSetOSD failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }


cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execCreateOSD(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;
  float x,y;
  std::string dateFormat, timeFormat, plainText, bColorspace, fColorspace;
  int fontSize, bTransparent, fTransparent;
  bool isPersistentText;

  _trt__CreateOSD * CreateOSD;
  _trt__CreateOSDResponse * CreateOSDResponse;
  tt__OSDConfiguration* tmpOSD;

  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("OSD")) {
      std::cout << "Failed to process request, No OSD found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No OSD found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS]["OSD"].HasMember("VideoSourceConfigurationToken")) {
      std::cout << "Failed to process request, No OSD:VideoSourceConfigurationToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No OSD:VideoSourceConfigurationToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyMedia.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyMedia.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyMedia.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;



  CreateOSD = soap_new__trt__CreateOSD(glSoap, -1);
  CreateOSDResponse = soap_new__trt__CreateOSDResponse(glSoap, -1);
  CreateOSD->OSD=soap_new_tt__OSDConfiguration(glSoap, -1);

//Prepare request
  tmpOSD=CreateOSD->OSD;
  if(d1[CMD_PARAMS]["OSD"].HasMember("Position")) {
    tmpOSD->Position=soap_new_tt__OSDPosConfiguration(glSoap, -1);
    if(d1[CMD_PARAMS]["OSD"]["Position"].HasMember("Pos")) {
      //tmpOSD->Position->Pos=soap_new_set_tt__Vector();
      //tmpOSD->Position->Pos=soap_new_tt__Vector(glSoap, -1);
      tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["Position"]["Pos"]["x"].GetString());
      x=std::stof(tmpVar);
      //(*tmpOSD->Position->Pos->x)=std::stof(tmpVar);
      tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["Position"]["Pos"]["y"].GetString());
      y=std::stof(tmpVar);
      //(*tmpOSD->Position->Pos->y)=std::stof(tmpVar);
      tmpOSD->Position->Pos=soap_new_set_tt__Vector(glSoap,&x,&y);
    }
    if(d1[CMD_PARAMS]["OSD"]["Position"].HasMember("Type")) {
      tmpOSD->Position->Type=std::string(d1[CMD_PARAMS]["OSD"]["Position"]["Type"].GetString());
    }
  }
  if(d1[CMD_PARAMS]["OSD"].HasMember("TextString")) {
    tmpOSD->TextString=soap_new_tt__OSDTextConfiguration(glSoap,-1);
    if(d1[CMD_PARAMS]["OSD"]["TextString"].HasMember("DateFormat")) {
      dateFormat=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["DateFormat"].GetString());
      tmpOSD->TextString->DateFormat=&dateFormat;
    }
    if(d1[CMD_PARAMS]["OSD"]["TextString"].HasMember("PlainText")) {
      plainText=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["PlainText"].GetString());
      tmpOSD->TextString->PlainText=&plainText;
    }
    if(d1[CMD_PARAMS]["OSD"]["TextString"].HasMember("TimeFormat")) {
      timeFormat=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["TimeFormat"].GetString());
      tmpOSD->TextString->TimeFormat=&timeFormat;
    }
    if(d1[CMD_PARAMS]["OSD"]["TextString"].HasMember("FontSize")) {
      tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["FontSize"].GetString());
      fontSize=std::stoi(tmpVar);
      tmpOSD->TextString->FontSize=&fontSize;
    }
    if(d1[CMD_PARAMS]["OSD"]["TextString"].HasMember("IsPersistentText")) {
      tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["IsPersistentText"].GetString());
      if(tmpVar=="true")isPersistentText=true;
      else isPersistentText=false;
      tmpOSD->TextString->IsPersistentText=&isPersistentText;
    }
    if(d1[CMD_PARAMS]["OSD"]["TextString"].HasMember("Type")) {
      tmpOSD->TextString->Type=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["Type"].GetString());
    }
    if(d1[CMD_PARAMS]["OSD"]["TextString"].HasMember("BackgroundColor")) {
      tmpOSD->TextString->BackgroundColor=soap_new_tt__OSDColor(glSoap,-1);
      if(d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"].HasMember("Color")) {
        tmpOSD->TextString->BackgroundColor->Color=soap_new_tt__Color(glSoap,-1);
        if(d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Color"].HasMember("Colorspace")) {
          bColorspace=
            std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Color"]["Colorspace"].GetString());
          tmpOSD->TextString->BackgroundColor->Color->Colorspace=&bColorspace;
        }
        if((d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Color"].HasMember("X"))) {
          tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Color"]["X"].GetString());
          tmpOSD->TextString->BackgroundColor->Color->X=std::stof(tmpVar);
        }
        if((d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Color"].HasMember("Y"))) {
          tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Color"]["Y"].GetString());
          tmpOSD->TextString->BackgroundColor->Color->Y=std::stof(tmpVar);
        }
        if((d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Color"].HasMember("Z"))) {
          tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Color"]["Z"].GetString());
          tmpOSD->TextString->BackgroundColor->Color->Z=std::stof(tmpVar);
        }
      }
      if(d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"].HasMember("Transparent")) {
        tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["BackgroundColor"]["Transparent"].GetString());
        bTransparent=std::stoi(tmpVar);
        tmpOSD->TextString->BackgroundColor->Transparent=&bTransparent;
      }
    }

    if(d1[CMD_PARAMS]["OSD"]["TextString"].HasMember("FontColor")) {
      tmpOSD->TextString->FontColor=soap_new_tt__OSDColor(glSoap,-1);
      if(d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"].HasMember("Color")) {
        tmpOSD->TextString->FontColor->Color=soap_new_tt__Color(glSoap,-1);
        if(d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Color"].HasMember("Colorspace")) {
          fColorspace=
            std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Color"]["Colorspace"].GetString());
          tmpOSD->TextString->FontColor->Color->Colorspace=&bColorspace;
        }
        if((d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Color"].HasMember("X"))) {
          tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Color"]["X"].GetString());
          tmpOSD->TextString->FontColor->Color->X=std::stof(tmpVar);
        }
        if((d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Color"].HasMember("Y"))) {
          tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Color"]["Y"].GetString());
          tmpOSD->TextString->FontColor->Color->Y=std::stof(tmpVar);
        }
        if((d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Color"].HasMember("Z"))) {
          tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Color"]["Z"].GetString());
          tmpOSD->TextString->FontColor->Color->Z=std::stof(tmpVar);
        }
      }
      if(d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"].HasMember("Transparent")) {
        tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["TextString"]["FontColor"]["Transparent"].GetString());
        fTransparent=std::stoi(tmpVar);
        tmpOSD->TextString->FontColor->Transparent=&bTransparent;
      }
    }
  }
  tmpOSD->VideoSourceConfigurationToken=soap_new_tt__OSDReference(glSoap, -1);
  if(d1[CMD_PARAMS]["OSD"].HasMember("VideoSourceConfigurationToken")) {
    tmpOSD->VideoSourceConfigurationToken->__item=
      std::string(d1[CMD_PARAMS]["OSD"]["VideoSourceConfigurationToken"].GetString());
  }
  if(d1[CMD_PARAMS]["OSD"].HasMember("Type")) {
    tmpVar=std::string(d1[CMD_PARAMS]["OSD"]["Type"].GetString());
    if(tmpVar=="Text")tmpOSD->Type=tt__OSDType__Text;
    if(tmpVar=="Image")tmpOSD->Type=tt__OSDType__Image;
    if(tmpVar=="Extended")tmpOSD->Type=tt__OSDType__Extended;
  }
  if(d1[CMD_PARAMS]["OSD"].HasMember("token")) {
    tmpOSD->token=
      std::string(d1[CMD_PARAMS]["OSD"]["token"].GetString());
  }

//End prepare request

  faultStr="";
  if(false == sendCreateOSD(&proxyMedia, CreateOSD, CreateOSDResponse)) {
    if(verbosity>2)std::cout <<  "sendCreateOSD failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendCreateOSD failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }


cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execDeleteOSD(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string OSDToken="";
  /*
  bool sendGetOSD(MediaBindingProxy* tProxyMedia, _trt__GetOSD * imagingSettings,
                  _trt__GetOSDResponse * imagingSettingsResponse);
  */
  _trt__DeleteOSD * DeleteOSD;
  _trt__DeleteOSDResponse * DeleteOSDResponse;

  if (d1.HasMember(CMD_PARAMS)) {
    if(d1[CMD_PARAMS].HasMember("OSDToken"))
      OSDToken=std::string(d1[CMD_PARAMS]["OSDToken"].GetString());
    else {
      std::cout << "Failed to process request, No OSDToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No OSDToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyMedia.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyMedia.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyMedia.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;


  DeleteOSD = soap_new__trt__DeleteOSD(glSoap, -1);
  DeleteOSDResponse = soap_new__trt__DeleteOSDResponse(glSoap, -1);
  DeleteOSD->OSDToken=OSDToken;

  faultStr="";
  if(false == sendDeleteOSD(&proxyMedia, DeleteOSD, DeleteOSDResponse)) {
    if(verbosity>2)std::cout <<  "sendDeleteOSD failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendDeleteOSD failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }
//Process response
  outStr="{\"status\":\"OK\"}";

//End process response
cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}


void execGetOSDOptions(int fd, rapidjson::Document &d1, uint32_t messageID) {
  unsigned char data[cachedOSDOptionsResponse.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=cachedOSDOptionsResponse.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)cachedOSDOptionsResponse.c_str(),cachedOSDOptionsResponse.length());
  sendData(fd, data, cachedOSDOptionsResponse.length()+sHeader);
}

void execGetCapabilities(int fd, rapidjson::Document &d1, uint32_t messageID) {
  unsigned char data[cachedCapabilities.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=cachedCapabilities.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)cachedCapabilities.c_str(),cachedCapabilities.length());
  sendData(fd, data, cachedCapabilities.length()+sHeader);
}

void execGetOptions(int fd, rapidjson::Document &d1, uint32_t messageID) {
  unsigned char data[cachedImagingOptionsResponse.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=cachedImagingOptionsResponse.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)cachedImagingOptionsResponse.c_str(),cachedImagingOptionsResponse.length());
  sendData(fd, data, cachedImagingOptionsResponse.length()+sHeader);
}

void execGetMoveOptions(int fd, rapidjson::Document &d1, uint32_t messageID) {
  unsigned char data[cachedMoveOptionsResponse.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=cachedMoveOptionsResponse.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)cachedMoveOptionsResponse.c_str(),cachedMoveOptionsResponse.length());
  sendData(fd, data, cachedMoveOptionsResponse.length()+sHeader);
}

void execStop(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  _timg__Stop * GetStop;
  _timg__StopResponse * GetStopResponse;

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyImaging.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyImaging.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyImaging.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyImaging.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyImaging.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;


  GetStop = soap_new__timg__Stop(glSoap, -1);
  GetStopResponse = soap_new__timg__StopResponse(glSoap, -1);
  GetStop->VideoSourceToken=videoSources[0];

  faultStr="";
  if(false == sendStop(&proxyImaging, GetStop, GetStopResponse)) {
    if(verbosity>2)std::cout <<  "sendStop failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendStop failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);

}

void execAbsoluteMove(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;

  _tptz__AbsoluteMove * AbsoluteMove;
  _tptz__AbsoluteMoveResponse * AbsoluteMoveResponse;

  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("Position")) {
      std::cout << "Failed to process request, No Position found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No Position found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  AbsoluteMove = soap_new__tptz__AbsoluteMove(glSoap, -1);
  AbsoluteMoveResponse = soap_new__tptz__AbsoluteMoveResponse(glSoap, -1);
  AbsoluteMove->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());
  AbsoluteMove->Position=soap_new_tt__PTZVector(glSoap, -1);

//Prepare request
  if(d1[CMD_PARAMS]["Position"].HasMember("Zoom")) {
    AbsoluteMove->Position->Zoom=soap_new_tt__Vector1D(glSoap, -1);
    if(d1[CMD_PARAMS]["Position"]["Zoom"].HasMember("x")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Position"]["Zoom"]["x"].GetString());
      AbsoluteMove->Position->Zoom->x=std::stof(tmpVar);
    }
  }
  if(d1[CMD_PARAMS]["Position"].HasMember("PanTilt")) {
    AbsoluteMove->Position->PanTilt=soap_new_tt__Vector2D(glSoap, -1);
    if(d1[CMD_PARAMS]["Position"]["PanTilt"].HasMember("x")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Position"]["PanTilt"]["x"].GetString());
      AbsoluteMove->Position->PanTilt->x=std::stof(tmpVar);
    }
    if(d1[CMD_PARAMS]["Position"]["PanTilt"].HasMember("y")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Position"]["PanTilt"]["y"].GetString());
      AbsoluteMove->Position->PanTilt->y=std::stof(tmpVar);
    }
  }
  if(d1[CMD_PARAMS].HasMember("Speed")) {
    AbsoluteMove->Speed=soap_new_tt__PTZSpeed(glSoap, -1);
    if(d1[CMD_PARAMS]["Speed"].HasMember("Zoom")) {
      AbsoluteMove->Speed->Zoom=soap_new_tt__Vector1D(glSoap, -1);
      if(d1[CMD_PARAMS]["Speed"]["Zoom"].HasMember("x")) {
        tmpVar=std::string(d1[CMD_PARAMS]["Speed"]["Zoom"]["x"].GetString());
        AbsoluteMove->Speed->Zoom->x=std::stof(tmpVar);
      }
    }
    if(d1[CMD_PARAMS]["Speed"].HasMember("PanTilt")) {
      AbsoluteMove->Speed->PanTilt=soap_new_tt__Vector2D(glSoap, -1);
      if(d1[CMD_PARAMS]["Speed"]["PanTilt"].HasMember("x")) {
        tmpVar=std::string(d1[CMD_PARAMS]["Speed"]["PanTilt"]["x"].GetString());
        AbsoluteMove->Speed->PanTilt->x=std::stof(tmpVar);
      }
      if(d1[CMD_PARAMS]["Speed"]["PanTilt"].HasMember("y")) {
        tmpVar=std::string(d1[CMD_PARAMS]["Speed"]["PanTilt"]["y"].GetString());
        AbsoluteMove->Speed->PanTilt->y=std::stof(tmpVar);
      }
    }
  }

//End Prepare request

  faultStr="";
  if(false == sendAbsoluteMove(&proxyPTZ, AbsoluteMove, AbsoluteMoveResponse)) {
    if(verbosity>2)std::cout <<  "sendAbsoluteMove failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendAbsoluteMove failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);

}

void execContinuousMove(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;
  int64_t tmpTimeout;

  _tptz__ContinuousMove * ContinuousMove;
  _tptz__ContinuousMoveResponse * ContinuousMoveResponse;

  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("Velocity")) {
      std::cout << "Failed to process request, No Velocity found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No Velocity found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  ContinuousMove = soap_new__tptz__ContinuousMove(glSoap, -1);
  ContinuousMoveResponse = soap_new__tptz__ContinuousMoveResponse(glSoap, -1);
  ContinuousMove->Velocity=soap_new_tt__PTZSpeed(glSoap, -1);

//Prepare request
  ContinuousMove->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());
  if(d1[CMD_PARAMS]["Velocity"].HasMember("Zoom")) {
    ContinuousMove->Velocity->Zoom=soap_new_tt__Vector1D(glSoap, -1);
    if(d1[CMD_PARAMS]["Velocity"]["Zoom"].HasMember("x")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Velocity"]["Zoom"]["x"].GetString());
      ContinuousMove->Velocity->Zoom->x=std::stof(tmpVar);
    }
  }
  if(d1[CMD_PARAMS]["Velocity"].HasMember("PanTilt")) {
    ContinuousMove->Velocity->PanTilt=soap_new_tt__Vector2D(glSoap, -1);
    if(d1[CMD_PARAMS]["Velocity"]["PanTilt"].HasMember("x")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Velocity"]["PanTilt"]["x"].GetString());
      ContinuousMove->Velocity->PanTilt->x=std::stof(tmpVar);
    }
    if(d1[CMD_PARAMS]["Velocity"]["PanTilt"].HasMember("y")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Velocity"]["PanTilt"]["y"].GetString());
      ContinuousMove->Velocity->PanTilt->y=std::stof(tmpVar);
    }
  }
  if(d1[CMD_PARAMS].HasMember("Timeout")) {
    tmpVar=std::string(d1[CMD_PARAMS]["Timeout"].GetString());
    tmpTimeout=std::stoll(tmpVar);
    ContinuousMove->Timeout=&(tmpTimeout);
  }

//End Prepare request

  faultStr="";
  if(false == sendContinuousMove(&proxyPTZ, ContinuousMove, ContinuousMoveResponse)) {
    if(verbosity>2)std::cout <<  "sendContinuousMove failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendContinuousMove failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);

}

void execRelativeMove(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;

  _tptz__RelativeMove * RelativeMove;
  _tptz__RelativeMoveResponse * RelativeMoveResponse;

  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("Translation")) {
      std::cout << "Failed to process request, No Translation found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No Translation found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  RelativeMove = soap_new__tptz__RelativeMove(glSoap, -1);
  RelativeMoveResponse = soap_new__tptz__RelativeMoveResponse(glSoap, -1);
  RelativeMove->Translation=soap_new_tt__PTZVector(glSoap, -1);

//Prepare request
  RelativeMove->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());
  if(d1[CMD_PARAMS]["Translation"].HasMember("Zoom")) {
    RelativeMove->Translation->Zoom=soap_new_tt__Vector1D(glSoap, -1);
    if(d1[CMD_PARAMS]["Translation"]["Zoom"].HasMember("x")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Translation"]["Zoom"]["x"].GetString());
      RelativeMove->Translation->Zoom->x=std::stof(tmpVar);
    }
  }
  if(d1[CMD_PARAMS]["Translation"].HasMember("PanTilt")) {
    RelativeMove->Translation->PanTilt=soap_new_tt__Vector2D(glSoap, -1);
    if(d1[CMD_PARAMS]["Translation"]["PanTilt"].HasMember("x")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Translation"]["PanTilt"]["x"].GetString());
      RelativeMove->Translation->PanTilt->x=std::stof(tmpVar);
    }
    if(d1[CMD_PARAMS]["Translation"]["PanTilt"].HasMember("y")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Translation"]["PanTilt"]["y"].GetString());
      RelativeMove->Translation->PanTilt->y=std::stof(tmpVar);
    }
  }
  if(d1[CMD_PARAMS].HasMember("Speed")) {
    RelativeMove->Speed=soap_new_tt__PTZSpeed(glSoap, -1);
    if(d1[CMD_PARAMS]["Speed"].HasMember("Zoom")) {
      RelativeMove->Speed->Zoom=soap_new_tt__Vector1D(glSoap, -1);
      if(d1[CMD_PARAMS]["Speed"]["Zoom"].HasMember("x")) {
        tmpVar=std::string(d1[CMD_PARAMS]["Speed"]["Zoom"]["x"].GetString());
        RelativeMove->Speed->Zoom->x=std::stof(tmpVar);
      }
    }
    if(d1[CMD_PARAMS]["Speed"].HasMember("PanTilt")) {
      RelativeMove->Speed->PanTilt=soap_new_tt__Vector2D(glSoap, -1);
      if(d1[CMD_PARAMS]["Speed"]["PanTilt"].HasMember("x")) {
        tmpVar=std::string(d1[CMD_PARAMS]["Speed"]["PanTilt"]["x"].GetString());
        RelativeMove->Speed->PanTilt->x=std::stof(tmpVar);
      }
      if(d1[CMD_PARAMS]["Speed"]["PanTilt"].HasMember("y")) {
        tmpVar=std::string(d1[CMD_PARAMS]["Speed"]["PanTilt"]["y"].GetString());
        RelativeMove->Speed->PanTilt->y=std::stof(tmpVar);
      }
    }
  }

//End Prepare request

  faultStr="";
  if(false == sendRelativeMove(&proxyPTZ, RelativeMove, RelativeMoveResponse)) {
    if(verbosity>2)std::cout <<  "sendRelativeMove failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendRelativeMove failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGeoMove(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;
  double lon, lat;
  float elevation, AreaHeight, AreaWidth;

  _tptz__GeoMove * GeoMove;
  _tptz__GeoMoveResponse * GeoMoveResponse;

  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("Target")) {
      std::cout << "Failed to process request, No Target found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No Target found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GeoMove = soap_new__tptz__GeoMove(glSoap, -1);
  GeoMoveResponse = soap_new__tptz__GeoMoveResponse(glSoap, -1);
  GeoMove->Target=soap_new_tt__GeoLocation(glSoap, -1);

//Prepare request
  GeoMove->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());
  if(d1[CMD_PARAMS]["Target"].HasMember("lat")) {
    tmpVar=std::string(d1[CMD_PARAMS]["Target"]["lat"].GetString());
    lat=std::stod(tmpVar);
    GeoMove->Target->lat=&lat;
  }
  if(d1[CMD_PARAMS]["Target"].HasMember("lon")) {
    tmpVar=std::string(d1[CMD_PARAMS]["Target"]["lon"].GetString());
    lon=std::stod(tmpVar);
    GeoMove->Target->lon=&lon;
  }
  if(d1[CMD_PARAMS]["Target"].HasMember("elevation")) {
    tmpVar=std::string(d1[CMD_PARAMS]["Target"]["elevation"].GetString());
    elevation=std::stof(tmpVar);
    GeoMove->Target->elevation=&elevation;
  }

  if(d1[CMD_PARAMS].HasMember("Speed")) {
    GeoMove->Speed=soap_new_tt__PTZSpeed(glSoap, -1);
    if(d1[CMD_PARAMS]["Speed"].HasMember("Zoom")) {
      GeoMove->Speed->Zoom=soap_new_tt__Vector1D(glSoap, -1);
      if(d1[CMD_PARAMS]["Speed"]["Zoom"].HasMember("x")) {
        tmpVar=std::string(d1[CMD_PARAMS]["Speed"]["Zoom"]["x"].GetString());
        GeoMove->Speed->Zoom->x=std::stof(tmpVar);
      }
    }
    if(d1[CMD_PARAMS]["Speed"].HasMember("PanTilt")) {
      GeoMove->Speed->PanTilt=soap_new_tt__Vector2D(glSoap, -1);
      if(d1[CMD_PARAMS]["Speed"]["PanTilt"].HasMember("x")) {
        tmpVar=std::string(d1[CMD_PARAMS]["Speed"]["PanTilt"]["x"].GetString());
        GeoMove->Speed->PanTilt->x=std::stof(tmpVar);
      }
      if(d1[CMD_PARAMS]["Speed"]["PanTilt"].HasMember("y")) {
        tmpVar=std::string(d1[CMD_PARAMS]["Speed"]["PanTilt"]["y"].GetString());
        GeoMove->Speed->PanTilt->y=std::stof(tmpVar);
      }
    }
  }

  if(d1[CMD_PARAMS].HasMember("AreaHeight")) {
    tmpVar=std::string(d1[CMD_PARAMS]["AreaHeight"].GetString());
    AreaHeight=std::stof(tmpVar);
    GeoMove->AreaHeight=&AreaHeight;
  }

  if(d1[CMD_PARAMS].HasMember("AreaWidth")) {
    tmpVar=std::string(d1[CMD_PARAMS]["AreaWidth"].GetString());
    AreaWidth=std::stof(tmpVar);
    GeoMove->AreaWidth=&AreaWidth;
  }


//End Prepare request

  faultStr="";
  if(false == sendGeoMove(&proxyPTZ, GeoMove, GeoMoveResponse)) {
    if(verbosity>2)std::cout <<  "sendGeoMove failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGeoMove failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execPTZStop(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;
  bool PanTilt, Zoom;
  _tptz__Stop * Stop;
  _tptz__StopResponse * StopResponse;

  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  Stop = soap_new__tptz__Stop(glSoap, -1);
  StopResponse = soap_new__tptz__StopResponse(glSoap, -1);

//Prepare request
  Stop->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());

  if(d1[CMD_PARAMS].HasMember("PanTilt")) {
    tmpVar=std::string(d1[CMD_PARAMS]["PanTilt"].GetString());
    if(tmpVar=="true") PanTilt=true;
    else PanTilt=false;
    Stop->PanTilt=&PanTilt;
  }

  if(d1[CMD_PARAMS].HasMember("Zoom")) {
    tmpVar=std::string(d1[CMD_PARAMS]["Zoom"].GetString());
    if(tmpVar=="true") Zoom=true;
    else Zoom=false;
    Stop->Zoom=&Zoom;
  }


//End Prepare request

  faultStr="";
  if(false == sendPTZStop(&proxyPTZ, Stop, StopResponse)) {
    if(verbosity>2)std::cout <<  "sendPTZStop failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendPTZStop failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execSendAuxiliaryCommand(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\", \"parameters\":{\"AuxiliaryResponse \":\"";
  std::string tmpVar;

  _tptz__SendAuxiliaryCommand * SendAuxiliaryCommand;
  _tptz__SendAuxiliaryCommandResponse * SendAuxiliaryCommandResponse;

  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("AuxiliaryData")) {
      std::cout << "Failed to process request, No AuxiliaryData found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No AuxiliaryData found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  SendAuxiliaryCommand = soap_new__tptz__SendAuxiliaryCommand(glSoap, -1);
  SendAuxiliaryCommandResponse = soap_new__tptz__SendAuxiliaryCommandResponse(glSoap, -1);


//Prepare request
  SendAuxiliaryCommand->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());

  SendAuxiliaryCommand->AuxiliaryData=std::string(d1[CMD_PARAMS]["AuxiliaryData"].GetString());


//End Prepare request

  faultStr="";
  if(false == sendSendAuxiliaryCommand(&proxyPTZ, SendAuxiliaryCommand, SendAuxiliaryCommandResponse)) {
    if(verbosity>2)std::cout <<  "sendSendAuxiliaryCommand failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendSendAuxiliaryCommand failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

  outStr+=SendAuxiliaryCommandResponse->AuxiliaryResponse+"\"}}";

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGetServiceCapabilities(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;

  _tptz__GetServiceCapabilities * GetServiceCapabilities;
  _tptz__GetServiceCapabilitiesResponse * GetServiceCapabilitiesResponse;


  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetServiceCapabilities = soap_new__tptz__GetServiceCapabilities(glSoap, -1);
  GetServiceCapabilitiesResponse = soap_new__tptz__GetServiceCapabilitiesResponse(glSoap, -1);

//Prepare request

//End Prepare request

  faultStr="";
  if(false == sendGetServiceCapabilities(&proxyPTZ, GetServiceCapabilities, GetServiceCapabilitiesResponse)) {
    if(verbosity>2)std::cout <<  "sendGetServiceCapabilities failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetServiceCapabilities failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{\"Capabilities\":{";
  if(GetServiceCapabilitiesResponse->Capabilities->EFlip!=NULL) {
    if(*(GetServiceCapabilitiesResponse->Capabilities->EFlip)) outStr+="\"EFlip\":\"true\", ";
    else outStr+="\"EFlip\":\"false\", ";
  }
  if(GetServiceCapabilitiesResponse->Capabilities->GetCompatibleConfigurations!=NULL) {
    if(*(GetServiceCapabilitiesResponse->Capabilities->GetCompatibleConfigurations))
      outStr+="\"GetCompatibleConfigurations\":\"true\", ";
    else outStr+="\"GetCompatibleConfigurations\":\"false\", ";
  }
  if(GetServiceCapabilitiesResponse->Capabilities->MoveStatus!=NULL) {
    if(*(GetServiceCapabilitiesResponse->Capabilities->MoveStatus)) outStr+="\"MoveStatus\":\"true\", ";
    else outStr+="\"MoveStatus\":\"false\", ";
  }
  if(GetServiceCapabilitiesResponse->Capabilities->Reverse!=NULL) {
    if(*(GetServiceCapabilitiesResponse->Capabilities->Reverse)) outStr+="\"Reverse\":\"true\", ";
    else outStr+="\"Reverse\":\"false\", ";
  }
  if(GetServiceCapabilitiesResponse->Capabilities->StatusPosition!=NULL) {
    if(*(GetServiceCapabilitiesResponse->Capabilities->StatusPosition)) outStr+="\"StatusPosition\":\"true\", ";
    else outStr+="\"StatusPosition\":\"false\", ";
  }
  if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
  outStr+="}}}";
//End Process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execPTZGetStatus(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;

  _tptz__GetStatus * PTZGetStatus;
  _tptz__GetStatusResponse * PTZGetStatusResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  PTZGetStatus = soap_new__tptz__GetStatus(glSoap, -1);
  PTZGetStatusResponse = soap_new__tptz__GetStatusResponse(glSoap, -1);

//Prepare request
  PTZGetStatus->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());

//End Prepare request

  faultStr="";
  if(false == sendPTZGetStatus(&proxyPTZ, PTZGetStatus, PTZGetStatusResponse)) {
    if(verbosity>2)std::cout <<  "sendPTZGetStatus failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendPTZGetStatus failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{\"PTZStatus\":{";
  if(PTZGetStatusResponse->PTZStatus->Position!=NULL) {
    outStr+="\"Position\":{";
    if(PTZGetStatusResponse->PTZStatus->Position->PanTilt!=NULL) {
      outStr+="\"PanTilt\":{";
      outStr+="\"x\":\""+std::to_string(PTZGetStatusResponse->PTZStatus->Position->PanTilt->x)+"\", ";
      outStr+="\"y\":\""+std::to_string(PTZGetStatusResponse->PTZStatus->Position->PanTilt->y)+"\"";
      outStr+="}, ";
    }
    if(PTZGetStatusResponse->PTZStatus->Position->Zoom!=NULL) {
      outStr+="\"Zoom\":{";
      outStr+="\"x\":\""+std::to_string(PTZGetStatusResponse->PTZStatus->Position->Zoom->x)+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(PTZGetStatusResponse->PTZStatus->MoveStatus!=NULL) {
    outStr+="\"MoveStatus\":{";
    if(PTZGetStatusResponse->PTZStatus->MoveStatus->PanTilt!=NULL) {
      outStr+="\"PanTilt\":\"";
      switch(*(PTZGetStatusResponse->PTZStatus->MoveStatus->PanTilt)) {
      case tt__MoveStatus__IDLE:
        outStr+="IDLE";
        break;
      case tt__MoveStatus__MOVING:
        outStr+="MOVING";
        break;
      default:
        outStr+="UNKNOWN";
        break;
      }
      outStr+="\", ";
    }
    if(PTZGetStatusResponse->PTZStatus->MoveStatus->Zoom!=NULL) {
      outStr+="\"Zoom\":\"";
      switch(*(PTZGetStatusResponse->PTZStatus->MoveStatus->Zoom)) {
      case tt__MoveStatus__IDLE:
        outStr+="IDLE";
        break;
      case tt__MoveStatus__MOVING:
        outStr+="MOVING";
        break;
      default:
        outStr+="UNKNOWN";
        break;
      }
      outStr+="\"";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(PTZGetStatusResponse->PTZStatus->Error!=NULL) {
    outStr+="\"Error\":\""+(*(PTZGetStatusResponse->PTZStatus->Error))+"\", ";
  }
  outStr+="\"UtcTime\":\""+timeToString(PTZGetStatusResponse->PTZStatus->UtcTime)+"\"";
  if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
  outStr+="}}}";
//End Process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGetConfigurations(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;

  _tptz__GetConfigurations * GetConfigurations;
  _tptz__GetConfigurationsResponse * GetConfigurationsResponse;

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetConfigurations = soap_new__tptz__GetConfigurations(glSoap, -1);
  GetConfigurationsResponse = soap_new__tptz__GetConfigurationsResponse(glSoap, -1);
//Prepare request

//End Prepare request

  faultStr="";
  if(false == sendGetConfigurations(&proxyPTZ, GetConfigurations, GetConfigurationsResponse)) {
    if(verbosity>2)std::cout <<  "sendGetConfigurations failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetConfigurations failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{";
  if(verbosity>3) std::cout << "Number of PTZConfiguration received is " << GetConfigurationsResponse->PTZConfiguration.size() << std::endl;
  if(GetConfigurationsResponse->PTZConfiguration.size()>0) {
    outStr+="\"PTZConfiguration\":[";
    for (unsigned i=0; i<GetConfigurationsResponse->PTZConfiguration.size(); i++) {
      if(i>0)outStr+=", ";
      outStr+="{";
      tt__PTZConfiguration* tmpConfig=GetConfigurationsResponse->PTZConfiguration[i];
      if(tmpConfig->DefaultPTZSpeed!=NULL) {
        outStr+="\"DefaultPTZSpeed\":{";
        if(tmpConfig->DefaultPTZSpeed->PanTilt!=NULL) {
          outStr+="\"PanTilt\":{";
          outStr+="\"x\":\""+std::to_string(tmpConfig->DefaultPTZSpeed->PanTilt->x)+"\", ";
          outStr+="\"y\":\""+std::to_string(tmpConfig->DefaultPTZSpeed->PanTilt->y)+"\"";
          outStr+="}, ";
        }
        if(tmpConfig->DefaultPTZSpeed->Zoom!=NULL)
          outStr+="\"Zoom\":{\"x\":\""+std::to_string(tmpConfig->DefaultPTZSpeed->Zoom->x)+"\"}";
        if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
        outStr+="}, ";
      }
      if(tmpConfig->PanTiltLimits!=NULL) {
        outStr+="\"PanTiltLimits\":{";
        if(tmpConfig->PanTiltLimits->Range!=NULL) {
          outStr+="\"Range\":{";
          if(tmpConfig->PanTiltLimits->Range->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpConfig->PanTiltLimits->Range->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpConfig->PanTiltLimits->Range->XRange->Max)+"\"";
            outStr+="}, ";
          }
          if(tmpConfig->PanTiltLimits->Range->YRange!=NULL) {
            outStr+="\"YRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpConfig->PanTiltLimits->Range->YRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpConfig->PanTiltLimits->Range->YRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpConfig->PanTiltLimits->Range->URI +"\"";
          outStr+="}";
        }
        outStr+="}, ";
      }
      if(tmpConfig->ZoomLimits!=NULL) {
        outStr+="\"ZoomLimits\":{";
        if(tmpConfig->ZoomLimits->Range!=NULL) {
          outStr+="\"Range\":{";
          if(tmpConfig->ZoomLimits->Range->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpConfig->ZoomLimits->Range->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpConfig->ZoomLimits->Range->XRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpConfig->ZoomLimits->Range->URI +"\"";
          outStr+="}";
        }
        outStr+="}, ";
      }
//////////////////////////////////////////////////////////////////////
      if(tmpConfig->DefaultAbsolutePantTiltPositionSpace!=NULL)
        outStr+="\"DefaultAbsolutePantTiltPositionSpace\":\""+
                (*(tmpConfig->DefaultAbsolutePantTiltPositionSpace))+"\", ";
      if(tmpConfig->DefaultAbsoluteZoomPositionSpace!=NULL)
        outStr+="\"DefaultAbsoluteZoomPositionSpace\":\""+
                (*(tmpConfig->DefaultAbsoluteZoomPositionSpace))+"\", ";
      if(tmpConfig->DefaultContinuousPanTiltVelocitySpace!=NULL)
        outStr+="\"DefaultContinuousPanTiltVelocitySpace\":\""+
                (*(tmpConfig->DefaultContinuousPanTiltVelocitySpace))+"\", ";
      if(tmpConfig->DefaultContinuousZoomVelocitySpace!=NULL)
        outStr+="\"DefaultContinuousZoomVelocitySpace\":\""+
                (*(tmpConfig->DefaultContinuousZoomVelocitySpace))+"\", ";
      if(tmpConfig->DefaultRelativePanTiltTranslationSpace!=NULL)
        outStr+="\"DefaultRelativePanTiltTranslationSpace\":\""+
                (*(tmpConfig->DefaultRelativePanTiltTranslationSpace))+"\", ";
      if(tmpConfig->DefaultRelativeZoomTranslationSpace!=NULL)
        outStr+="\"DefaultRelativeZoomTranslationSpace\":\""+
                (*(tmpConfig->DefaultRelativeZoomTranslationSpace))+"\", ";
      if(tmpConfig->DefaultPTZTimeout!=NULL)
        outStr+="\"DefaultPTZTimeout\":\""+std::to_string(*(tmpConfig->DefaultPTZTimeout))+"\", ";
      if(tmpConfig->PresetTourRamp!=NULL)
        outStr+="\"PresetTourRamp\":\""+std::to_string(*(tmpConfig->PresetTourRamp))+"\", ";
      if(tmpConfig->PresetRamp!=NULL)
        outStr+="\"PresetRamp\":\""+std::to_string(*(tmpConfig->PresetRamp))+"\", ";
      if(tmpConfig->MoveRamp!=NULL)
        outStr+="\"MoveRamp\":\""+std::to_string(*(tmpConfig->MoveRamp))+"\", ";
      outStr+="\"UseCount\":\""+std::to_string(tmpConfig->UseCount)+"\", ";
      outStr+="\"Name\":\""+tmpConfig->Name+"\", ";
      outStr+="\"NodeToken\":\""+tmpConfig->NodeToken+"\", ";
      outStr+="\"token\":\""+tmpConfig->token+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="]";
  }
  outStr+="}}";
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGetConfiguration(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;

  _tptz__GetConfiguration * GetConfiguration;
  _tptz__GetConfigurationResponse * GetConfigurationResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("PTZConfigurationToken")) {
      std::cout << "Failed to process request, No PTZConfigurationToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No PTZConfigurationToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetConfiguration = soap_new__tptz__GetConfiguration(glSoap, -1);
  GetConfigurationResponse = soap_new__tptz__GetConfigurationResponse(glSoap, -1);
//Prepare request

  GetConfiguration->PTZConfigurationToken=std::string(d1[CMD_PARAMS]["PTZConfigurationToken"].GetString());

//End Prepare request

  faultStr="";
  if(false == sendGetConfiguration(&proxyPTZ, GetConfiguration, GetConfigurationResponse)) {
    if(verbosity>2)std::cout <<  "sendGetConfiguration failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetConfiguration failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{\"PTZConfiguration\":{";
  if(GetConfigurationResponse->PTZConfiguration!=NULL) {
    tt__PTZConfiguration* tmpConfig=GetConfigurationResponse->PTZConfiguration;
    if(tmpConfig->DefaultPTZSpeed!=NULL) {
      outStr+="\"DefaultPTZSpeed\":{";
      if(tmpConfig->DefaultPTZSpeed->PanTilt!=NULL) {
        outStr+="\"PanTilt\":{";
        outStr+="\"x\":\""+std::to_string(tmpConfig->DefaultPTZSpeed->PanTilt->x)+"\", ";
        outStr+="\"y\":\""+std::to_string(tmpConfig->DefaultPTZSpeed->PanTilt->y)+"\"";
        outStr+="}, ";
      }
      if(tmpConfig->DefaultPTZSpeed->Zoom!=NULL)
        outStr+="\"Zoom\":{\"x\":\""+std::to_string(tmpConfig->DefaultPTZSpeed->Zoom->x)+"\"}";
      if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
      outStr+="}, ";
    }
    if(tmpConfig->PanTiltLimits!=NULL) {
      outStr+="\"PanTiltLimits\":{";
      if(tmpConfig->PanTiltLimits->Range!=NULL) {
        outStr+="\"Range\":{";
        if(tmpConfig->PanTiltLimits->Range->XRange!=NULL) {
          outStr+="\"XRange\":{";
          outStr+="\"Min\":\""+std::to_string(tmpConfig->PanTiltLimits->Range->XRange->Min)+"\", ";
          outStr+="\"Max\":\""+std::to_string(tmpConfig->PanTiltLimits->Range->XRange->Max)+"\"";
          outStr+="}, ";
        }
        if(tmpConfig->PanTiltLimits->Range->YRange!=NULL) {
          outStr+="\"YRange\":{";
          outStr+="\"Min\":\""+std::to_string(tmpConfig->PanTiltLimits->Range->YRange->Min)+"\", ";
          outStr+="\"Max\":\""+std::to_string(tmpConfig->PanTiltLimits->Range->YRange->Max)+"\"";
          outStr+="}, ";
        }
        outStr+="\"URI\":\"" + tmpConfig->PanTiltLimits->Range->URI +"\"";
        outStr+="}";
      }
      outStr+="}, ";
    }
    if(tmpConfig->ZoomLimits!=NULL) {
      outStr+="\"ZoomLimits\":{";
      if(tmpConfig->ZoomLimits->Range!=NULL) {
        outStr+="\"Range\":{";
        if(tmpConfig->ZoomLimits->Range->XRange!=NULL) {
          outStr+="\"XRange\":{";
          outStr+="\"Min\":\""+std::to_string(tmpConfig->ZoomLimits->Range->XRange->Min)+"\", ";
          outStr+="\"Max\":\""+std::to_string(tmpConfig->ZoomLimits->Range->XRange->Max)+"\"";
          outStr+="}, ";
        }
        outStr+="\"URI\":\"" + tmpConfig->ZoomLimits->Range->URI +"\"";
        outStr+="}";
      }
      outStr+="}, ";
    }
//////////////////////////////////////////////////////////////////////
    if(tmpConfig->DefaultAbsolutePantTiltPositionSpace!=NULL)
      outStr+="\"DefaultAbsolutePantTiltPositionSpace\":\""+
              (*(tmpConfig->DefaultAbsolutePantTiltPositionSpace))+"\", ";
    if(tmpConfig->DefaultAbsoluteZoomPositionSpace!=NULL)
      outStr+="\"DefaultAbsoluteZoomPositionSpace\":\""+
              (*(tmpConfig->DefaultAbsoluteZoomPositionSpace))+"\", ";
    if(tmpConfig->DefaultContinuousPanTiltVelocitySpace!=NULL)
      outStr+="\"DefaultContinuousPanTiltVelocitySpace\":\""+
              (*(tmpConfig->DefaultContinuousPanTiltVelocitySpace))+"\", ";
    if(tmpConfig->DefaultContinuousZoomVelocitySpace!=NULL)
      outStr+="\"DefaultContinuousZoomVelocitySpace\":\""+
              (*(tmpConfig->DefaultContinuousZoomVelocitySpace))+"\", ";
    if(tmpConfig->DefaultRelativePanTiltTranslationSpace!=NULL)
      outStr+="\"DefaultRelativePanTiltTranslationSpace\":\""+
              (*(tmpConfig->DefaultRelativePanTiltTranslationSpace))+"\", ";
    if(tmpConfig->DefaultRelativeZoomTranslationSpace!=NULL)
      outStr+="\"DefaultRelativeZoomTranslationSpace\":\""+
              (*(tmpConfig->DefaultRelativeZoomTranslationSpace))+"\", ";
    if(tmpConfig->DefaultPTZTimeout!=NULL)
      outStr+="\"DefaultPTZTimeout\":\""+std::to_string(*(tmpConfig->DefaultPTZTimeout))+"\", ";
    if(tmpConfig->PresetTourRamp!=NULL)
      outStr+="\"PresetTourRamp\":\""+std::to_string(*(tmpConfig->PresetTourRamp))+"\", ";
    if(tmpConfig->PresetRamp!=NULL)
      outStr+="\"PresetRamp\":\""+std::to_string(*(tmpConfig->PresetRamp))+"\", ";
    if(tmpConfig->MoveRamp!=NULL)
      outStr+="\"MoveRamp\":\""+std::to_string(*(tmpConfig->MoveRamp))+"\", ";
    outStr+="\"UseCount\":\""+std::to_string(tmpConfig->UseCount)+"\", ";
    outStr+="\"Name\":\""+tmpConfig->Name+"\", ";
    outStr+="\"NodeToken\":\""+tmpConfig->NodeToken+"\", ";
    outStr+="\"token\":\""+tmpConfig->token+"\"";
  }
  outStr+="}}}";
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGetNodes(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;

  _tptz__GetNodes * GetNodes;
  _tptz__GetNodesResponse * GetNodesResponse;

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetNodes = soap_new__tptz__GetNodes(glSoap, -1);
  GetNodesResponse = soap_new__tptz__GetNodesResponse(glSoap, -1);
//Prepare request

//End Prepare request

  faultStr="";
  if(false == sendGetNodes(&proxyPTZ, GetNodes, GetNodesResponse)) {
    if(verbosity>2)std::cout <<  "sendGetNodes failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetNodes failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{";
  if(verbosity>3) std::cout << "Number of PTZNode received is " << GetNodesResponse->PTZNode.size() << std::endl;
  if(GetNodesResponse->PTZNode.size()>0) {
    outStr+="\"PTZNode\":[";
    for (unsigned i=0; i<GetNodesResponse->PTZNode.size(); i++) {
      if(i>0)outStr+=", ";
      outStr+="{";
      tt__PTZNode* tmpConfig=GetNodesResponse->PTZNode[i];
      if(tmpConfig->SupportedPTZSpaces!=NULL) {
        outStr+="\"SupportedPTZSpaces\":{";
//Add SupportedPTZSpaces:
        if(tmpConfig->SupportedPTZSpaces->AbsolutePanTiltPositionSpace.size()>0) {
          outStr+="\"AbsolutePanTiltPositionSpace\":[";
          for (unsigned k=0; k<tmpConfig->SupportedPTZSpaces->AbsolutePanTiltPositionSpace.size(); k++) {
            if(k!=0) outStr+=", ";
            tt__Space2DDescription* tmpDes=tmpConfig->SupportedPTZSpaces->AbsolutePanTiltPositionSpace[k];
            outStr+="{";
            if(tmpDes->XRange!=NULL) {
              outStr+="\"XRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
              outStr+="}, ";
            }
            if(tmpDes->YRange!=NULL) {
              outStr+="\"YRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpDes->YRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpDes->YRange->Max)+"\"";
              outStr+="}, ";
            }
            outStr+="\"URI\":\"" + tmpDes->URI +"\"";
            outStr+="}";
          }
          outStr+="], ";
        }
        if(tmpConfig->SupportedPTZSpaces->RelativePanTiltTranslationSpace.size()>0) {
          outStr+="\"RelativePanTiltTranslationSpace\":[";
          for (unsigned k=0; k<tmpConfig->SupportedPTZSpaces->RelativePanTiltTranslationSpace.size(); k++) {
            if(k!=0) outStr+=", ";
            tt__Space2DDescription* tmpDes=tmpConfig->SupportedPTZSpaces->RelativePanTiltTranslationSpace[k];
            outStr+="{";
            if(tmpDes->XRange!=NULL) {
              outStr+="\"XRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
              outStr+="}, ";
            }
            if(tmpDes->YRange!=NULL) {
              outStr+="\"YRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpDes->YRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpDes->YRange->Max)+"\"";
              outStr+="}, ";
            }
            outStr+="\"URI\":\"" + tmpDes->URI +"\"";
            outStr+="}";
          }
          outStr+="], ";
        }
        if(tmpConfig->SupportedPTZSpaces->ContinuousPanTiltVelocitySpace.size()>0) {
          outStr+="\"ContinuousPanTiltVelocitySpace\":[";
          for (unsigned k=0; k<tmpConfig->SupportedPTZSpaces->ContinuousPanTiltVelocitySpace.size(); k++) {
            if(k!=0) outStr+=", ";
            tt__Space2DDescription* tmpDes=tmpConfig->SupportedPTZSpaces->ContinuousPanTiltVelocitySpace[k];
            outStr+="{";
            if(tmpDes->XRange!=NULL) {
              outStr+="\"XRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
              outStr+="}, ";
            }
            if(tmpDes->YRange!=NULL) {
              outStr+="\"YRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpDes->YRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpDes->YRange->Max)+"\"";
              outStr+="}, ";
            }
            outStr+="\"URI\":\"" + tmpDes->URI +"\"";
            outStr+="}";
          }
          outStr+="], ";
        }
        if(tmpConfig->SupportedPTZSpaces->AbsoluteZoomPositionSpace.size()>0) {
          outStr+="\"AbsoluteZoomPositionSpace\":[";
          for (unsigned k=0; k<tmpConfig->SupportedPTZSpaces->AbsoluteZoomPositionSpace.size(); k++) {
            if(k!=0) outStr+=", ";
            tt__Space1DDescription* tmpDes=tmpConfig->SupportedPTZSpaces->AbsoluteZoomPositionSpace[k];
            outStr+="{";
            if(tmpDes->XRange!=NULL) {
              outStr+="\"XRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
              outStr+="}, ";
            }
            outStr+="\"URI\":\"" + tmpDes->URI +"\"";
            outStr+="}";
          }
          outStr+="], ";
        }
        if(tmpConfig->SupportedPTZSpaces->RelativeZoomTranslationSpace.size()>0) {
          outStr+="\"RelativeZoomTranslationSpace\":[";
          for (unsigned k=0; k<tmpConfig->SupportedPTZSpaces->RelativeZoomTranslationSpace.size(); k++) {
            if(k!=0) outStr+=", ";
            tt__Space1DDescription* tmpDes=tmpConfig->SupportedPTZSpaces->RelativeZoomTranslationSpace[k];
            outStr+="{";
            if(tmpDes->XRange!=NULL) {
              outStr+="\"XRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
              outStr+="}, ";
            }
            outStr+="\"URI\":\"" + tmpDes->URI +"\"";
            outStr+="}";
          }
          outStr+="], ";
        }
        if(tmpConfig->SupportedPTZSpaces->ContinuousZoomVelocitySpace.size()>0) {
          outStr+="\"ContinuousZoomVelocitySpace\":[";
          for (unsigned k=0; k<tmpConfig->SupportedPTZSpaces->ContinuousZoomVelocitySpace.size(); k++) {
            if(k!=0) outStr+=", ";
            tt__Space1DDescription* tmpDes=tmpConfig->SupportedPTZSpaces->ContinuousZoomVelocitySpace[k];
            outStr+="{";
            if(tmpDes->XRange!=NULL) {
              outStr+="\"XRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
              outStr+="}, ";
            }
            outStr+="\"URI\":\"" + tmpDes->URI +"\"";
            outStr+="}";
          }
          outStr+="], ";
        }
        if(tmpConfig->SupportedPTZSpaces->PanTiltSpeedSpace.size()>0) {
          outStr+="\"PanTiltSpeedSpace\":[";
          for (unsigned k=0; k<tmpConfig->SupportedPTZSpaces->PanTiltSpeedSpace.size(); k++) {
            if(k!=0) outStr+=", ";
            tt__Space1DDescription* tmpDes=tmpConfig->SupportedPTZSpaces->PanTiltSpeedSpace[k];
            outStr+="{";
            if(tmpDes->XRange!=NULL) {
              outStr+="\"XRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
              outStr+="}, ";
            }
            outStr+="\"URI\":\"" + tmpDes->URI +"\"";
            outStr+="}";
          }
          outStr+="], ";
        }
        if(tmpConfig->SupportedPTZSpaces->ZoomSpeedSpace.size()>0) {
          outStr+="\"ZoomSpeedSpace\":[";
          for (unsigned k=0; k<tmpConfig->SupportedPTZSpaces->ZoomSpeedSpace.size(); k++) {
            if(k!=0) outStr+=", ";
            tt__Space1DDescription* tmpDes=tmpConfig->SupportedPTZSpaces->ZoomSpeedSpace[k];
            outStr+="{";
            if(tmpDes->XRange!=NULL) {
              outStr+="\"XRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
              outStr+="}, ";
            }
            outStr+="\"URI\":\"" + tmpDes->URI +"\"";
            outStr+="}";
          }
          outStr+="], ";
        }
//End Add SupportedPTZSpaces:
        if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
        outStr+="}, ";
      }
      if(tmpConfig->AuxiliaryCommands.size()>0) {
        outStr+="\"AuxiliaryCommands\":[";
        for (unsigned j=0; j<tmpConfig->AuxiliaryCommands.size(); j++) {
          if(j!=0) outStr+=", ";
          outStr+="\""+tmpConfig->AuxiliaryCommands[j]+"\"";
        }
        outStr+="], ";
      }
      if(tmpConfig->HomeSupported) outStr+="\"HomeSupported\":\"true\", ";
      else outStr+="\"HomeSupported\":\"false\", ";
      if(tmpConfig->GeoMove!=NULL) {
        if(*(tmpConfig->GeoMove)) outStr+="\"GeoMove\":\"true\", ";
        else outStr+="\"GeoMove\":\"false\", ";
      }
      if(tmpConfig->FixedHomePosition!=NULL) {
        if(*(tmpConfig->FixedHomePosition)) outStr+="\"FixedHomePosition\":\"true\", ";
        else outStr+="\"FixedHomePosition\":\"false\", ";
      }
      outStr+="\"MaximumNumberOfPresets\":\""+std::to_string(tmpConfig->MaximumNumberOfPresets)+"\", ";
      if(tmpConfig->Name!=NULL)
        outStr+="\"Name\":\""+(*(tmpConfig->Name))+"\", ";
      outStr+="\"token\":\""+tmpConfig->token+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="]";
  }
  outStr+="}}";
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGetNode(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;

  _tptz__GetNode * GetNode;
  _tptz__GetNodeResponse * GetNodeResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("NodeToken")) {
      std::cout << "Failed to process request, No NodeToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No NodeToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetNode = soap_new__tptz__GetNode(glSoap, -1);
  GetNodeResponse = soap_new__tptz__GetNodeResponse(glSoap, -1);
//Prepare request
  GetNode->NodeToken=std::string(d1[CMD_PARAMS]["NodeToken"].GetString());
//End Prepare request

  faultStr="";
  if(false == sendGetNode(&proxyPTZ, GetNode, GetNodeResponse)) {
    if(verbosity>2)std::cout <<  "sendGetNode failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetNode failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{";
  outStr+="\"PTZNode\":{";
  if(GetNodeResponse->PTZNode!=NULL) {
    tt__PTZNode* tmpConfig=GetNodeResponse->PTZNode;
    if(tmpConfig->SupportedPTZSpaces!=NULL) {
      outStr+="\"SupportedPTZSpaces\":{";
//Add SupportedPTZSpaces:
      if(tmpConfig->SupportedPTZSpaces->AbsolutePanTiltPositionSpace.size()>0) {
        outStr+="\"AbsolutePanTiltPositionSpace\":[";
        for (unsigned k=0; k<tmpConfig->SupportedPTZSpaces->AbsolutePanTiltPositionSpace.size(); k++) {
          if(k!=0) outStr+=", ";
          tt__Space2DDescription* tmpDes=tmpConfig->SupportedPTZSpaces->AbsolutePanTiltPositionSpace[k];
          outStr+="{";
          if(tmpDes->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
            outStr+="}, ";
          }
          if(tmpDes->YRange!=NULL) {
            outStr+="\"YRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->YRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->YRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpDes->URI +"\"";
          outStr+="}";
        }
        outStr+="], ";
      }
      if(tmpConfig->SupportedPTZSpaces->RelativePanTiltTranslationSpace.size()>0) {
        outStr+="\"RelativePanTiltTranslationSpace\":[";
        for (unsigned k=0; k<tmpConfig->SupportedPTZSpaces->RelativePanTiltTranslationSpace.size(); k++) {
          if(k!=0) outStr+=", ";
          tt__Space2DDescription* tmpDes=tmpConfig->SupportedPTZSpaces->RelativePanTiltTranslationSpace[k];
          outStr+="{";
          if(tmpDes->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
            outStr+="}, ";
          }
          if(tmpDes->YRange!=NULL) {
            outStr+="\"YRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->YRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->YRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpDes->URI +"\"";
          outStr+="}";
        }
        outStr+="], ";
      }
      if(tmpConfig->SupportedPTZSpaces->ContinuousPanTiltVelocitySpace.size()>0) {
        outStr+="\"ContinuousPanTiltVelocitySpace\":[";
        for (unsigned k=0; k<tmpConfig->SupportedPTZSpaces->ContinuousPanTiltVelocitySpace.size(); k++) {
          if(k!=0) outStr+=", ";
          tt__Space2DDescription* tmpDes=tmpConfig->SupportedPTZSpaces->ContinuousPanTiltVelocitySpace[k];
          outStr+="{";
          if(tmpDes->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
            outStr+="}, ";
          }
          if(tmpDes->YRange!=NULL) {
            outStr+="\"YRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->YRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->YRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpDes->URI +"\"";
          outStr+="}";
        }
        outStr+="], ";
      }
      if(tmpConfig->SupportedPTZSpaces->AbsoluteZoomPositionSpace.size()>0) {
        outStr+="\"AbsoluteZoomPositionSpace\":[";
        for (unsigned k=0; k<tmpConfig->SupportedPTZSpaces->AbsoluteZoomPositionSpace.size(); k++) {
          if(k!=0) outStr+=", ";
          tt__Space1DDescription* tmpDes=tmpConfig->SupportedPTZSpaces->AbsoluteZoomPositionSpace[k];
          outStr+="{";
          if(tmpDes->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpDes->URI +"\"";
          outStr+="}";
        }
        outStr+="], ";
      }
      if(tmpConfig->SupportedPTZSpaces->RelativeZoomTranslationSpace.size()>0) {
        outStr+="\"RelativeZoomTranslationSpace\":[";
        for (unsigned k=0; k<tmpConfig->SupportedPTZSpaces->RelativeZoomTranslationSpace.size(); k++) {
          if(k!=0) outStr+=", ";
          tt__Space1DDescription* tmpDes=tmpConfig->SupportedPTZSpaces->RelativeZoomTranslationSpace[k];
          outStr+="{";
          if(tmpDes->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpDes->URI +"\"";
          outStr+="}";
        }
        outStr+="], ";
      }
      if(tmpConfig->SupportedPTZSpaces->ContinuousZoomVelocitySpace.size()>0) {
        outStr+="\"ContinuousZoomVelocitySpace\":[";
        for (unsigned k=0; k<tmpConfig->SupportedPTZSpaces->ContinuousZoomVelocitySpace.size(); k++) {
          if(k!=0) outStr+=", ";
          tt__Space1DDescription* tmpDes=tmpConfig->SupportedPTZSpaces->ContinuousZoomVelocitySpace[k];
          outStr+="{";
          if(tmpDes->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpDes->URI +"\"";
          outStr+="}";
        }
        outStr+="], ";
      }
      if(tmpConfig->SupportedPTZSpaces->PanTiltSpeedSpace.size()>0) {
        outStr+="\"PanTiltSpeedSpace\":[";
        for (unsigned k=0; k<tmpConfig->SupportedPTZSpaces->PanTiltSpeedSpace.size(); k++) {
          if(k!=0) outStr+=", ";
          tt__Space1DDescription* tmpDes=tmpConfig->SupportedPTZSpaces->PanTiltSpeedSpace[k];
          outStr+="{";
          if(tmpDes->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpDes->URI +"\"";
          outStr+="}";
        }
        outStr+="], ";
      }
      if(tmpConfig->SupportedPTZSpaces->ZoomSpeedSpace.size()>0) {
        outStr+="\"ZoomSpeedSpace\":[";
        for (unsigned k=0; k<tmpConfig->SupportedPTZSpaces->ZoomSpeedSpace.size(); k++) {
          if(k!=0) outStr+=", ";
          tt__Space1DDescription* tmpDes=tmpConfig->SupportedPTZSpaces->ZoomSpeedSpace[k];
          outStr+="{";
          if(tmpDes->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpDes->URI +"\"";
          outStr+="}";
        }
        outStr+="], ";
      }
//End Add SupportedPTZSpaces:
      if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
      outStr+="}, ";
    }
    if(tmpConfig->AuxiliaryCommands.size()>0) {
      outStr+="\"AuxiliaryCommands\":[";
      for (unsigned j=0; j<tmpConfig->AuxiliaryCommands.size(); j++) {
        if(j!=0) outStr+=", ";
        outStr+="\""+tmpConfig->AuxiliaryCommands[j]+"\"";
      }
      outStr+="], ";
    }
    if(tmpConfig->HomeSupported) outStr+="\"HomeSupported\":\"true\", ";
    else outStr+="\"HomeSupported\":\"false\", ";
    if(tmpConfig->GeoMove!=NULL) {
      if(*(tmpConfig->GeoMove)) outStr+="\"GeoMove\":\"true\", ";
      else outStr+="\"GeoMove\":\"false\", ";
    }
    if(tmpConfig->FixedHomePosition!=NULL) {
      if(*(tmpConfig->FixedHomePosition)) outStr+="\"FixedHomePosition\":\"true\", ";
      else outStr+="\"FixedHomePosition\":\"false\", ";
    }
    outStr+="\"MaximumNumberOfPresets\":\""+std::to_string(tmpConfig->MaximumNumberOfPresets)+"\", ";
    if(tmpConfig->Name!=NULL)
      outStr+="\"Name\":\""+(*(tmpConfig->Name))+"\", ";
    outStr+="\"token\":\""+tmpConfig->token+"\"";
  }
  outStr+="}}}";
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGetConfigurationOptions(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;

  _tptz__GetConfigurationOptions * GetConfigurationOptions;
  _tptz__GetConfigurationOptionsResponse * GetConfigurationOptionsResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("ConfigurationToken")) {
      std::cout << "Failed to process request, No ConfigurationToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ConfigurationToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetConfigurationOptions = soap_new__tptz__GetConfigurationOptions(glSoap, -1);
  GetConfigurationOptionsResponse = soap_new__tptz__GetConfigurationOptionsResponse(glSoap, -1);
//Prepare request
  GetConfigurationOptions->ConfigurationToken=std::string(d1[CMD_PARAMS]["ConfigurationToken"].GetString());
//End Prepare request

  faultStr="";
  if(false == sendGetConfigurationOptions(&proxyPTZ, GetConfigurationOptions, GetConfigurationOptionsResponse)) {
    if(verbosity>2)std::cout <<  "sendGetConfigurationOptions failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetConfigurationOptions failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{";
  outStr+="\"PTZConfigurationOptions\":{";
  if(GetConfigurationOptionsResponse->PTZConfigurationOptions!=NULL) {
    tt__PTZConfigurationOptions* tmpConfig=GetConfigurationOptionsResponse->PTZConfigurationOptions;
    if(tmpConfig->Spaces!=NULL) {
      outStr+="\"Spaces\":{";
//Add Spaces:
      if(tmpConfig->Spaces->AbsolutePanTiltPositionSpace.size()>0) {
        outStr+="\"AbsolutePanTiltPositionSpace\":[";
        for (unsigned k=0; k<tmpConfig->Spaces->AbsolutePanTiltPositionSpace.size(); k++) {
          if(k!=0) outStr+=", ";
          tt__Space2DDescription* tmpDes=tmpConfig->Spaces->AbsolutePanTiltPositionSpace[k];
          outStr+="{";
          if(tmpDes->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
            outStr+="}, ";
          }
          if(tmpDes->YRange!=NULL) {
            outStr+="\"YRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->YRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->YRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpDes->URI +"\"";
          outStr+="}";
        }
        outStr+="], ";
      }
      if(tmpConfig->Spaces->RelativePanTiltTranslationSpace.size()>0) {
        outStr+="\"RelativePanTiltTranslationSpace\":[";
        for (unsigned k=0; k<tmpConfig->Spaces->RelativePanTiltTranslationSpace.size(); k++) {
          if(k!=0) outStr+=", ";
          tt__Space2DDescription* tmpDes=tmpConfig->Spaces->RelativePanTiltTranslationSpace[k];
          outStr+="{";
          if(tmpDes->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
            outStr+="}, ";
          }
          if(tmpDes->YRange!=NULL) {
            outStr+="\"YRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->YRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->YRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpDes->URI +"\"";
          outStr+="}";
        }
        outStr+="], ";
      }
      if(tmpConfig->Spaces->ContinuousPanTiltVelocitySpace.size()>0) {
        outStr+="\"ContinuousPanTiltVelocitySpace\":[";
        for (unsigned k=0; k<tmpConfig->Spaces->ContinuousPanTiltVelocitySpace.size(); k++) {
          if(k!=0) outStr+=", ";
          tt__Space2DDescription* tmpDes=tmpConfig->Spaces->ContinuousPanTiltVelocitySpace[k];
          outStr+="{";
          if(tmpDes->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
            outStr+="}, ";
          }
          if(tmpDes->YRange!=NULL) {
            outStr+="\"YRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->YRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->YRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpDes->URI +"\"";
          outStr+="}";
        }
        outStr+="], ";
      }
      if(tmpConfig->Spaces->AbsoluteZoomPositionSpace.size()>0) {
        outStr+="\"AbsoluteZoomPositionSpace\":[";
        for (unsigned k=0; k<tmpConfig->Spaces->AbsoluteZoomPositionSpace.size(); k++) {
          if(k!=0) outStr+=", ";
          tt__Space1DDescription* tmpDes=tmpConfig->Spaces->AbsoluteZoomPositionSpace[k];
          outStr+="{";
          if(tmpDes->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpDes->URI +"\"";
          outStr+="}";
        }
        outStr+="], ";
      }
      if(tmpConfig->Spaces->RelativeZoomTranslationSpace.size()>0) {
        outStr+="\"RelativeZoomTranslationSpace\":[";
        for (unsigned k=0; k<tmpConfig->Spaces->RelativeZoomTranslationSpace.size(); k++) {
          if(k!=0) outStr+=", ";
          tt__Space1DDescription* tmpDes=tmpConfig->Spaces->RelativeZoomTranslationSpace[k];
          outStr+="{";
          if(tmpDes->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpDes->URI +"\"";
          outStr+="}";
        }
        outStr+="], ";
      }
      if(tmpConfig->Spaces->ContinuousZoomVelocitySpace.size()>0) {
        outStr+="\"ContinuousZoomVelocitySpace\":[";
        for (unsigned k=0; k<tmpConfig->Spaces->ContinuousZoomVelocitySpace.size(); k++) {
          if(k!=0) outStr+=", ";
          tt__Space1DDescription* tmpDes=tmpConfig->Spaces->ContinuousZoomVelocitySpace[k];
          outStr+="{";
          if(tmpDes->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpDes->URI +"\"";
          outStr+="}";
        }
        outStr+="], ";
      }
      if(tmpConfig->Spaces->PanTiltSpeedSpace.size()>0) {
        outStr+="\"PanTiltSpeedSpace\":[";
        for (unsigned k=0; k<tmpConfig->Spaces->PanTiltSpeedSpace.size(); k++) {
          if(k!=0) outStr+=", ";
          tt__Space1DDescription* tmpDes=tmpConfig->Spaces->PanTiltSpeedSpace[k];
          outStr+="{";
          if(tmpDes->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpDes->URI +"\"";
          outStr+="}";
        }
        outStr+="], ";
      }
      if(tmpConfig->Spaces->ZoomSpeedSpace.size()>0) {
        outStr+="\"ZoomSpeedSpace\":[";
        for (unsigned k=0; k<tmpConfig->Spaces->ZoomSpeedSpace.size(); k++) {
          if(k!=0) outStr+=", ";
          tt__Space1DDescription* tmpDes=tmpConfig->Spaces->ZoomSpeedSpace[k];
          outStr+="{";
          if(tmpDes->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpDes->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpDes->XRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpDes->URI +"\"";
          outStr+="}";
        }
        outStr+="], ";
      }
//End Add Spaces:
      if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
      outStr+="}, ";
    }
    if(tmpConfig->PTZRamps!=NULL)
      outStr+="\"PTZRamps\":\""+(*(tmpConfig->PTZRamps))+"\", ";
    if(tmpConfig->PTZTimeout!=NULL) {
      outStr+="\"PTZTimeout\":{";
      outStr+="\"Max\":\""+std::to_string(tmpConfig->PTZTimeout->Max)+"\", ";
      outStr+="\"Min\":\""+std::to_string(tmpConfig->PTZTimeout->Min)+"\", ";
      outStr+="}, ";
    }
  }
  outStr+="}}}";
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGetCompatibleConfigurations(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;

  _tptz__GetCompatibleConfigurations * GetCompatibleConfigurations;
  _tptz__GetCompatibleConfigurationsResponse * GetCompatibleConfigurationsResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetCompatibleConfigurations = soap_new__tptz__GetCompatibleConfigurations(glSoap, -1);
  GetCompatibleConfigurationsResponse = soap_new__tptz__GetCompatibleConfigurationsResponse(glSoap, -1);
//Prepare request
  GetCompatibleConfigurations->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());
//End Prepare request

  faultStr="";
  if(false == sendGetCompatibleConfigurations(&proxyPTZ, GetCompatibleConfigurations,
      GetCompatibleConfigurationsResponse)) {
    if(verbosity>2)std::cout <<  "sendGetCompatibleConfigurations failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetCompatibleConfigurations failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{";
  if(verbosity>3) std::cout << "Number of PTZConfiguration received is "
                              << GetCompatibleConfigurationsResponse->PTZConfiguration.size() << std::endl;
  if(GetCompatibleConfigurationsResponse->PTZConfiguration.size()>0) {
    outStr+="\"PTZConfiguration\":[";
    for (unsigned i=0; i<GetCompatibleConfigurationsResponse->PTZConfiguration.size(); i++) {
      if(i>0)outStr+=", ";
      outStr+="{";
      tt__PTZConfiguration* tmpConfig=GetCompatibleConfigurationsResponse->PTZConfiguration[i];
      if(tmpConfig->DefaultPTZSpeed!=NULL) {
        outStr+="\"DefaultPTZSpeed\":{";
        if(tmpConfig->DefaultPTZSpeed->PanTilt!=NULL) {
          outStr+="\"PanTilt\":{";
          outStr+="\"x\":\""+std::to_string(tmpConfig->DefaultPTZSpeed->PanTilt->x)+"\", ";
          outStr+="\"y\":\""+std::to_string(tmpConfig->DefaultPTZSpeed->PanTilt->y)+"\"";
          outStr+="}, ";
        }
        if(tmpConfig->DefaultPTZSpeed->Zoom!=NULL)
          outStr+="\"Zoom\":{\"x\":\""+std::to_string(tmpConfig->DefaultPTZSpeed->Zoom->x)+"\"}";
        if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
        outStr+="}, ";
      }
      if(tmpConfig->PanTiltLimits!=NULL) {
        outStr+="\"PanTiltLimits\":{";
        if(tmpConfig->PanTiltLimits->Range!=NULL) {
          outStr+="\"Range\":{";
          if(tmpConfig->PanTiltLimits->Range->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpConfig->PanTiltLimits->Range->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpConfig->PanTiltLimits->Range->XRange->Max)+"\"";
            outStr+="}, ";
          }
          if(tmpConfig->PanTiltLimits->Range->YRange!=NULL) {
            outStr+="\"YRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpConfig->PanTiltLimits->Range->YRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpConfig->PanTiltLimits->Range->YRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpConfig->PanTiltLimits->Range->URI +"\"";
          outStr+="}";
        }
        outStr+="}, ";
      }
      if(tmpConfig->ZoomLimits!=NULL) {
        outStr+="\"ZoomLimits\":{";
        if(tmpConfig->ZoomLimits->Range!=NULL) {
          outStr+="\"Range\":{";
          if(tmpConfig->ZoomLimits->Range->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+std::to_string(tmpConfig->ZoomLimits->Range->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+std::to_string(tmpConfig->ZoomLimits->Range->XRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpConfig->ZoomLimits->Range->URI +"\"";
          outStr+="}";
        }
        outStr+="}, ";
      }
//////////////////////////////////////////////////////////////////////
      if(tmpConfig->DefaultAbsolutePantTiltPositionSpace!=NULL)
        outStr+="\"DefaultAbsolutePantTiltPositionSpace\":\""+
                (*(tmpConfig->DefaultAbsolutePantTiltPositionSpace))+"\", ";
      if(tmpConfig->DefaultAbsoluteZoomPositionSpace!=NULL)
        outStr+="\"DefaultAbsoluteZoomPositionSpace\":\""+
                (*(tmpConfig->DefaultAbsoluteZoomPositionSpace))+"\", ";
      if(tmpConfig->DefaultContinuousPanTiltVelocitySpace!=NULL)
        outStr+="\"DefaultContinuousPanTiltVelocitySpace\":\""+
                (*(tmpConfig->DefaultContinuousPanTiltVelocitySpace))+"\", ";
      if(tmpConfig->DefaultContinuousZoomVelocitySpace!=NULL)
        outStr+="\"DefaultContinuousZoomVelocitySpace\":\""+
                (*(tmpConfig->DefaultContinuousZoomVelocitySpace))+"\", ";
      if(tmpConfig->DefaultRelativePanTiltTranslationSpace!=NULL)
        outStr+="\"DefaultRelativePanTiltTranslationSpace\":\""+
                (*(tmpConfig->DefaultRelativePanTiltTranslationSpace))+"\", ";
      if(tmpConfig->DefaultRelativeZoomTranslationSpace!=NULL)
        outStr+="\"DefaultRelativeZoomTranslationSpace\":\""+
                (*(tmpConfig->DefaultRelativeZoomTranslationSpace))+"\", ";
      if(tmpConfig->DefaultPTZTimeout!=NULL)
        outStr+="\"DefaultPTZTimeout\":\""+std::to_string(*(tmpConfig->DefaultPTZTimeout))+"\", ";
      if(tmpConfig->PresetTourRamp!=NULL)
        outStr+="\"PresetTourRamp\":\""+std::to_string(*(tmpConfig->PresetTourRamp))+"\", ";
      if(tmpConfig->PresetRamp!=NULL)
        outStr+="\"PresetRamp\":\""+std::to_string(*(tmpConfig->PresetRamp))+"\", ";
      if(tmpConfig->MoveRamp!=NULL)
        outStr+="\"MoveRamp\":\""+std::to_string(*(tmpConfig->MoveRamp))+"\", ";
      outStr+="\"UseCount\":\""+std::to_string(tmpConfig->UseCount)+"\", ";
      outStr+="\"Name\":\""+tmpConfig->Name+"\", ";
      outStr+="\"NodeToken\":\""+tmpConfig->NodeToken+"\", ";
      outStr+="\"token\":\""+tmpConfig->token+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="]";
  }
  outStr+="}}";
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGetPresets(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;

  _tptz__GetPresets * GetPresets;
  _tptz__GetPresetsResponse * GetPresetsResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetPresets = soap_new__tptz__GetPresets(glSoap, -1);
  GetPresetsResponse = soap_new__tptz__GetPresetsResponse(glSoap, -1);
//Prepare request
  GetPresets->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());
//End Prepare request

  faultStr="";
  if(false == sendGetPresets(&proxyPTZ, GetPresets,
                             GetPresetsResponse)) {
    if(verbosity>2)std::cout <<  "sendGetPresets failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetPresets failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{";
  if(verbosity>3) std::cout << "Number of Preset received is "
                              << GetPresetsResponse->Preset.size() << std::endl;
  if(GetPresetsResponse->Preset.size()>0) {
    outStr+="\"Preset\":[";
    for (unsigned i=0; i<GetPresetsResponse->Preset.size(); i++) {
      if(i>0)outStr+=", ";
      outStr+="{";
      tt__PTZPreset* tmpConfig=GetPresetsResponse->Preset[i];
      if(tmpConfig->PTZPosition!=NULL) {
        outStr+="\"PTZPosition\":{";
        if(tmpConfig->PTZPosition->PanTilt!=NULL) {
          outStr+="\"PanTilt\":{";
          outStr+="\"x\":\""+std::to_string(tmpConfig->PTZPosition->PanTilt->x)+"\", ";
          outStr+="\"y\":\""+std::to_string(tmpConfig->PTZPosition->PanTilt->y)+"\"";
          outStr+="}, ";
        }
        if(tmpConfig->PTZPosition->Zoom!=NULL) {
          outStr+="\"Zoom\":{";
          outStr+="\"x\":\""+std::to_string(tmpConfig->PTZPosition->Zoom->x)+"\"";
          outStr+="}";
        }
        if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
        outStr+="}, ";
      }
      if(tmpConfig->Name!=NULL)
        outStr+="\"Name\":\""+(*(tmpConfig->Name))+"\", ";
      if(tmpConfig->token!=NULL)
        outStr+="\"token\":\""+(*(tmpConfig->token))+"\"";
      if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="]";
  }
  outStr+="}}";
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execRemovePreset(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;

  _tptz__RemovePreset * RemovePreset;
  _tptz__RemovePresetResponse * RemovePresetResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("PresetToken")) {
      std::cout << "Failed to process request, No PresetToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No PresetToken found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  RemovePreset = soap_new__tptz__RemovePreset(glSoap, -1);
  RemovePresetResponse = soap_new__tptz__RemovePresetResponse(glSoap, -1);
//Prepare request
  RemovePreset->PresetToken=std::string(d1[CMD_PARAMS]["PresetToken"].GetString());
  RemovePreset->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());
//End Prepare request

  faultStr="";
  if(false == sendRemovePreset(&proxyPTZ, RemovePreset,
                               RemovePresetResponse)) {
    if(verbosity>2)std::cout <<  "sendRemovePreset failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendRemovePreset failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGotoPreset(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;

  _tptz__GotoPreset * GotoPreset;
  _tptz__GotoPresetResponse * GotoPresetResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("PresetToken")) {
      std::cout << "Failed to process request, No PresetToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No PresetToken found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GotoPreset = soap_new__tptz__GotoPreset(glSoap, -1);
  GotoPresetResponse = soap_new__tptz__GotoPresetResponse(glSoap, -1);

//Prepare request
  GotoPreset->PresetToken=std::string(d1[CMD_PARAMS]["PresetToken"].GetString());
  GotoPreset->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());

  if(d1[CMD_PARAMS].HasMember("Speed")) {
    GotoPreset->Speed=soap_new_tt__PTZSpeed(glSoap, -1);
    if(d1[CMD_PARAMS]["Speed"].HasMember("Zoom")) {
      GotoPreset->Speed->Zoom=soap_new_tt__Vector1D(glSoap, -1);
      if(d1[CMD_PARAMS]["Speed"]["Zoom"].HasMember("x")) {
        tmpVar=std::string(d1[CMD_PARAMS]["Speed"]["Zoom"]["x"].GetString());
        GotoPreset->Speed->Zoom->x=std::stof(tmpVar);
      }
    }
    if(d1[CMD_PARAMS]["Speed"].HasMember("PanTilt")) {
      GotoPreset->Speed->PanTilt=soap_new_tt__Vector2D(glSoap, -1);
      if(d1[CMD_PARAMS]["Speed"]["PanTilt"].HasMember("x")) {
        tmpVar=std::string(d1[CMD_PARAMS]["Speed"]["PanTilt"]["x"].GetString());
        GotoPreset->Speed->PanTilt->x=std::stof(tmpVar);
      }
      if(d1[CMD_PARAMS]["Speed"]["PanTilt"].HasMember("y")) {
        tmpVar=std::string(d1[CMD_PARAMS]["Speed"]["PanTilt"]["y"].GetString());
        GotoPreset->Speed->PanTilt->y=std::stof(tmpVar);
      }
    }
  }
//End Prepare request

  faultStr="";
  if(false == sendGotoPreset(&proxyPTZ, GotoPreset,
                             GotoPresetResponse)) {
    if(verbosity>2)std::cout <<  "sendGotoPreset failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGotoPreset failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGotoHomePosition(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;

  _tptz__GotoHomePosition * GotoHomePosition;
  _tptz__GotoHomePositionResponse * GotoHomePositionResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GotoHomePosition = soap_new__tptz__GotoHomePosition(glSoap, -1);
  GotoHomePositionResponse = soap_new__tptz__GotoHomePositionResponse(glSoap, -1);

//Prepare request
  GotoHomePosition->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());

  if(d1[CMD_PARAMS].HasMember("Speed")) {
    GotoHomePosition->Speed=soap_new_tt__PTZSpeed(glSoap, -1);
    if(d1[CMD_PARAMS]["Speed"].HasMember("Zoom")) {
      GotoHomePosition->Speed->Zoom=soap_new_tt__Vector1D(glSoap, -1);
      if(d1[CMD_PARAMS]["Speed"]["Zoom"].HasMember("x")) {
        tmpVar=std::string(d1[CMD_PARAMS]["Speed"]["Zoom"]["x"].GetString());
        GotoHomePosition->Speed->Zoom->x=std::stof(tmpVar);
      }
    }
    if(d1[CMD_PARAMS]["Speed"].HasMember("PanTilt")) {
      GotoHomePosition->Speed->PanTilt=soap_new_tt__Vector2D(glSoap, -1);
      if(d1[CMD_PARAMS]["Speed"]["PanTilt"].HasMember("x")) {
        tmpVar=std::string(d1[CMD_PARAMS]["Speed"]["PanTilt"]["x"].GetString());
        GotoHomePosition->Speed->PanTilt->x=std::stof(tmpVar);
      }
      if(d1[CMD_PARAMS]["Speed"]["PanTilt"].HasMember("y")) {
        tmpVar=std::string(d1[CMD_PARAMS]["Speed"]["PanTilt"]["y"].GetString());
        GotoHomePosition->Speed->PanTilt->y=std::stof(tmpVar);
      }
    }
  }
//End Prepare request

  faultStr="";
  if(false == sendGotoHomePosition(&proxyPTZ, GotoHomePosition,
                                   GotoHomePositionResponse)) {
    if(verbosity>2)std::cout <<  "sendGotoHomePosition failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGotoHomePosition failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execSetHomePosition(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;

  _tptz__SetHomePosition * SetHomePosition;
  _tptz__SetHomePositionResponse * SetHomePositionResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  SetHomePosition = soap_new__tptz__SetHomePosition(glSoap, -1);
  SetHomePositionResponse = soap_new__tptz__SetHomePositionResponse(glSoap, -1);

//Prepare request
  SetHomePosition->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());

//End Prepare request

  faultStr="";
  if(false == sendSetHomePosition(&proxyPTZ, SetHomePosition,
                                  SetHomePositionResponse)) {
    if(verbosity>2)std::cout <<  "sendSetHomePosition failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendSetHomePosition failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execRemovePresetTour(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;

  _tptz__RemovePresetTour * RemovePresetTour;
  _tptz__RemovePresetTourResponse * RemovePresetTourResponse;

  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("PresetTourToken")) {
      std::cout << "Failed to process request, No PresetTourToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No PresetTourToken found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  RemovePresetTour = soap_new__tptz__RemovePresetTour(glSoap, -1);
  RemovePresetTourResponse = soap_new__tptz__RemovePresetTourResponse(glSoap, -1);

//Prepare request
  RemovePresetTour->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());
  RemovePresetTour->PresetTourToken=std::string(d1[CMD_PARAMS]["PresetTourToken"].GetString());

//End Prepare request

  faultStr="";
  if(false == sendRemovePresetTour(&proxyPTZ, RemovePresetTour,
                                   RemovePresetTourResponse)) {
    if(verbosity>2)std::cout <<  "sendRemovePresetTour failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendRemovePresetTour failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execCreatePresetTour(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;

  _tptz__CreatePresetTour * CreatePresetTour;
  _tptz__CreatePresetTourResponse * CreatePresetTourResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  CreatePresetTour = soap_new__tptz__CreatePresetTour(glSoap, -1);
  CreatePresetTourResponse = soap_new__tptz__CreatePresetTourResponse(glSoap, -1);

//Prepare request
  CreatePresetTour->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());

//End Prepare request

  faultStr="";
  if(false == sendCreatePresetTour(&proxyPTZ, CreatePresetTour,
                                   CreatePresetTourResponse)) {
    if(verbosity>2)std::cout <<  "sendCreatePresetTour failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendCreatePresetTour failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{\"PresetTourToken\":\"";
  outStr+=CreatePresetTourResponse->PresetTourToken+"\"}}";
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execSetPreset(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;
  std::string PresetName,PresetToken;

  _tptz__SetPreset * SetPreset;
  _tptz__SetPresetResponse * SetPresetResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  SetPreset = soap_new__tptz__SetPreset(glSoap, -1);
  SetPresetResponse = soap_new__tptz__SetPresetResponse(glSoap, -1);

//Prepare request
  SetPreset->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());
  if(d1[CMD_PARAMS].HasMember("PresetName")) {
    PresetName=std::string(d1[CMD_PARAMS]["PresetName"].GetString());
    SetPreset->PresetName=&PresetName;
  }
  if(d1[CMD_PARAMS].HasMember("PresetToken")) {
    PresetToken=std::string(d1[CMD_PARAMS]["PresetToken"].GetString());
    SetPreset->PresetToken=&PresetToken;
  }
//End Prepare request

  faultStr="";
  if(false == sendSetPreset(&proxyPTZ, SetPreset,
                            SetPresetResponse)) {
    if(verbosity>2)std::cout <<  "sendSetPreset failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendSetPreset failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{\"PresetToken\":\"";
  outStr+=SetPresetResponse->PresetToken+"\"}}";
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execOperatePresetTour(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;

  _tptz__OperatePresetTour * OperatePresetTour;
  _tptz__OperatePresetTourResponse * OperatePresetTourResponse;

  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("PresetTourToken")) {
      std::cout << "Failed to process request, No PresetTourToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No PresetTourToken found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS].HasMember("Operation")) {
      std::cout << "Failed to process request, No Operation found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No Operation found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  OperatePresetTour = soap_new__tptz__OperatePresetTour(glSoap, -1);
  OperatePresetTourResponse = soap_new__tptz__OperatePresetTourResponse(glSoap, -1);

//Prepare request
  OperatePresetTour->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());
  OperatePresetTour->PresetTourToken=std::string(d1[CMD_PARAMS]["PresetTourToken"].GetString());
  tmpVar=std::string(d1[CMD_PARAMS]["Operation"].GetString());
  if(tmpVar=="Start") OperatePresetTour->Operation=tt__PTZPresetTourOperation__Start;
  else if(tmpVar=="Stop") OperatePresetTour->Operation=tt__PTZPresetTourOperation__Stop;
  else if(tmpVar=="Pause") OperatePresetTour->Operation=tt__PTZPresetTourOperation__Pause;
  else if(tmpVar=="Extended") OperatePresetTour->Operation=tt__PTZPresetTourOperation__Extended;
  else {
    std::cout << "Failed to process request, Operation has wrong value" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Operation has wrong value\"}";
    goto sendResponse;
  }
//End Prepare request

  faultStr="";
  if(false == sendOperatePresetTour(&proxyPTZ, OperatePresetTour,
                                    OperatePresetTourResponse)) {
    if(verbosity>2)std::cout <<  "sendOperatePresetTour failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendOperatePresetTour failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execSetConfiguration(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;

  std::string DefaultAbsolutePantTiltPositionSpace;
  std::string DefaultAbsoluteZoomPositionSpace;
  std::string DefaultContinuousPanTiltVelocitySpace;
  std::string DefaultContinuousZoomVelocitySpace;
  std::string DefaultRelativePanTiltTranslationSpace;
  std::string DefaultRelativeZoomTranslationSpace;

  int64_t DefaultPTZTimeout;
  int MoveRamp, PresetRamp, PresetTourRamp;

  _tptz__SetConfiguration * SetConfiguration;
  _tptz__SetConfigurationResponse * SetConfigurationResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("PTZConfiguration")) {
      std::cout << "Failed to process request, No PTZConfiguration found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No PTZConfiguration found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS]["PTZConfiguration"].HasMember("NodeToken")) {
      std::cout << "Failed to process request, No PTZConfiguration:NodeToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No PTZConfiguration:NodeToken found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS]["PTZConfiguration"].HasMember("token")) {
      std::cout << "Failed to process request, No PTZConfiguration:token found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No PTZConfiguration:token found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS]["PTZConfiguration"].HasMember("Name")) {
      std::cout << "Failed to process request, No PTZConfiguration:Name found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No PTZConfiguration:Name found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  SetConfiguration = soap_new__tptz__SetConfiguration(glSoap, -1);
  SetConfigurationResponse = soap_new__tptz__SetConfigurationResponse(glSoap, -1);
  SetConfiguration->PTZConfiguration=soap_new_tt__PTZConfiguration(glSoap, -1);

//Prepare request
  SetConfiguration->PTZConfiguration->token=std::string(d1[CMD_PARAMS]["PTZConfiguration"]["token"].GetString());
  SetConfiguration->PTZConfiguration->NodeToken=std::string(d1[CMD_PARAMS]["PTZConfiguration"]["NodeToken"].GetString());
  SetConfiguration->PTZConfiguration->Name=std::string(d1[CMD_PARAMS]["PTZConfiguration"]["Name"].GetString());

  if(d1[CMD_PARAMS].HasMember("ForcePersistence")) {
    tmpVar=std::string(d1[CMD_PARAMS]["ForcePersistence"].GetString());
    if(tmpVar=="true") SetConfiguration->ForcePersistence=true;
    else SetConfiguration->ForcePersistence=false;
  } else SetConfiguration->ForcePersistence=false;

  if(d1[CMD_PARAMS]["PTZConfiguration"].HasMember("DefaultPTZSpeed")) {
    SetConfiguration->PTZConfiguration->DefaultPTZSpeed=soap_new_tt__PTZSpeed(glSoap, -1);
    if(d1[CMD_PARAMS]["PTZConfiguration"]["DefaultPTZSpeed"].HasMember("Zoom")) {
      SetConfiguration->PTZConfiguration->DefaultPTZSpeed->Zoom=soap_new_tt__Vector1D(glSoap, -1);
      if(d1[CMD_PARAMS]["PTZConfiguration"]["DefaultPTZSpeed"]["Zoom"].HasMember("x")) {
        tmpVar=std::string(d1[CMD_PARAMS]["PTZConfiguration"]["DefaultPTZSpeed"]["Zoom"]["x"].GetString());
        SetConfiguration->PTZConfiguration->DefaultPTZSpeed->Zoom->x=std::stof(tmpVar);
      }
    }
    if(d1[CMD_PARAMS]["PTZConfiguration"]["DefaultPTZSpeed"].HasMember("PanTilt")) {
      SetConfiguration->PTZConfiguration->DefaultPTZSpeed->PanTilt=soap_new_tt__Vector2D(glSoap, -1);
      if(d1[CMD_PARAMS]["PTZConfiguration"]["DefaultPTZSpeed"]["PanTilt"].HasMember("x")) {
        tmpVar=std::string(d1[CMD_PARAMS]["PTZConfiguration"]["DefaultPTZSpeed"]["PanTilt"]["x"].GetString());
        SetConfiguration->PTZConfiguration->DefaultPTZSpeed->PanTilt->x=std::stof(tmpVar);
      }
      if(d1[CMD_PARAMS]["PTZConfiguration"]["DefaultPTZSpeed"]["PanTilt"].HasMember("y")) {
        tmpVar=std::string(d1[CMD_PARAMS]["PTZConfiguration"]["DefaultPTZSpeed"]["PanTilt"]["y"].GetString());
        SetConfiguration->PTZConfiguration->DefaultPTZSpeed->PanTilt->y=std::stof(tmpVar);
      }
    }
  }

  if(d1[CMD_PARAMS]["PTZConfiguration"].HasMember("DefaultPTZTimeout")) {
    tmpVar=std::string(d1[CMD_PARAMS]["PTZConfiguration"]["DefaultPTZTimeout"].GetString());
    DefaultPTZTimeout=std::stoll(tmpVar);
    SetConfiguration->PTZConfiguration->DefaultPTZTimeout=&(DefaultPTZTimeout);
  }

  if(d1[CMD_PARAMS]["PTZConfiguration"].HasMember("PresetTourRamp")) {
    tmpVar=std::string(d1[CMD_PARAMS]["PTZConfiguration"]["PresetTourRamp"].GetString());
    PresetTourRamp=std::stoi(tmpVar);
    SetConfiguration->PTZConfiguration->PresetTourRamp=&(PresetTourRamp);
  }
  if(d1[CMD_PARAMS]["PTZConfiguration"].HasMember("PresetRamp")) {
    tmpVar=std::string(d1[CMD_PARAMS]["PTZConfiguration"]["PresetRamp"].GetString());
    PresetRamp=std::stoi(tmpVar);
    SetConfiguration->PTZConfiguration->PresetRamp=&(PresetRamp);
  }
  if(d1[CMD_PARAMS]["PTZConfiguration"].HasMember("MoveRamp")) {
    tmpVar=std::string(d1[CMD_PARAMS]["PTZConfiguration"]["MoveRamp"].GetString());
    MoveRamp=std::stoi(tmpVar);
    SetConfiguration->PTZConfiguration->MoveRamp=&(MoveRamp);
  }

  if(d1[CMD_PARAMS]["PTZConfiguration"].HasMember("DefaultAbsoluteZoomPositionSpace")) {
    DefaultAbsoluteZoomPositionSpace=
      std::string(d1[CMD_PARAMS]["PTZConfiguration"]["DefaultAbsoluteZoomPositionSpace"].GetString());
    SetConfiguration->PTZConfiguration->DefaultAbsoluteZoomPositionSpace=&(DefaultAbsoluteZoomPositionSpace);
  }
  if(d1[CMD_PARAMS]["PTZConfiguration"].HasMember("DefaultContinuousPanTiltVelocitySpace")) {
    DefaultContinuousPanTiltVelocitySpace=
      std::string(d1[CMD_PARAMS]["PTZConfiguration"]["DefaultContinuousPanTiltVelocitySpace"].GetString());
    SetConfiguration->PTZConfiguration->DefaultContinuousPanTiltVelocitySpace=&(DefaultContinuousPanTiltVelocitySpace);
  }
  if(d1[CMD_PARAMS]["PTZConfiguration"].HasMember("DefaultContinuousZoomVelocitySpace")) {
    DefaultContinuousZoomVelocitySpace=
      std::string(d1[CMD_PARAMS]["PTZConfiguration"]["DefaultContinuousZoomVelocitySpace"].GetString());
    SetConfiguration->PTZConfiguration->DefaultContinuousZoomVelocitySpace=&(DefaultContinuousZoomVelocitySpace);
  }
  if(d1[CMD_PARAMS]["PTZConfiguration"].HasMember("DefaultRelativePanTiltTranslationSpace")) {
    DefaultRelativePanTiltTranslationSpace=
      std::string(d1[CMD_PARAMS]["PTZConfiguration"]["DefaultRelativePanTiltTranslationSpace"].GetString());
    SetConfiguration->PTZConfiguration->DefaultRelativePanTiltTranslationSpace=&(DefaultRelativePanTiltTranslationSpace);
  }
  if(d1[CMD_PARAMS]["PTZConfiguration"].HasMember("DefaultRelativeZoomTranslationSpace")) {
    DefaultRelativeZoomTranslationSpace=
      std::string(d1[CMD_PARAMS]["PTZConfiguration"]["DefaultRelativeZoomTranslationSpace"].GetString());
    SetConfiguration->PTZConfiguration->DefaultRelativeZoomTranslationSpace=&(DefaultRelativeZoomTranslationSpace);
  }
  if(d1[CMD_PARAMS]["PTZConfiguration"].HasMember("DefaultAbsolutePantTiltPositionSpace")) {
    DefaultAbsolutePantTiltPositionSpace=
      std::string(d1[CMD_PARAMS]["PTZConfiguration"]["DefaultAbsolutePantTiltPositionSpace"].GetString());
    SetConfiguration->PTZConfiguration->DefaultAbsolutePantTiltPositionSpace=&(DefaultAbsolutePantTiltPositionSpace);
  }
  if(d1[CMD_PARAMS]["PTZConfiguration"].HasMember("ZoomLimits")) {
    SetConfiguration->PTZConfiguration->ZoomLimits=soap_new_tt__ZoomLimits(glSoap, -1);
    if(d1[CMD_PARAMS]["PTZConfiguration"]["ZoomLimits"].HasMember("Range")) {
      SetConfiguration->PTZConfiguration->ZoomLimits->Range=soap_new_tt__Space1DDescription(glSoap, -1);
      if(d1[CMD_PARAMS]["PTZConfiguration"]["ZoomLimits"]["Range"].HasMember("XRange")) {
        SetConfiguration->PTZConfiguration->ZoomLimits->Range->XRange=soap_new_tt__FloatRange(glSoap, -1);
        if(d1[CMD_PARAMS]["PTZConfiguration"]["ZoomLimits"]["Range"]["XRange"].HasMember("Max")) {
          tmpVar=std::string(d1[CMD_PARAMS]["PTZConfiguration"]["ZoomLimits"]["Range"]["XRange"]["Max"].GetString());
          SetConfiguration->PTZConfiguration->ZoomLimits->Range->XRange->Max=std::stof(tmpVar);
        }
        if(d1[CMD_PARAMS]["PTZConfiguration"]["ZoomLimits"]["Range"]["XRange"].HasMember("Min")) {
          tmpVar=std::string(d1[CMD_PARAMS]["PTZConfiguration"]["ZoomLimits"]["Range"]["XRange"]["Min"].GetString());
          SetConfiguration->PTZConfiguration->ZoomLimits->Range->XRange->Min=std::stof(tmpVar);
        }
      }
      if(d1[CMD_PARAMS]["PTZConfiguration"]["ZoomLimits"]["Range"].HasMember("URI")) {
        SetConfiguration->PTZConfiguration->ZoomLimits->Range->URI=
          std::string(d1[CMD_PARAMS]["PTZConfiguration"]["ZoomLimits"]["Range"]["URI"].GetString());
      }
    }
  }
  if(d1[CMD_PARAMS]["PTZConfiguration"].HasMember("PanTiltLimits")) {
    SetConfiguration->PTZConfiguration->PanTiltLimits=soap_new_tt__PanTiltLimits(glSoap, -1);
    if(d1[CMD_PARAMS]["PTZConfiguration"]["PanTiltLimits"].HasMember("Range")) {
      SetConfiguration->PTZConfiguration->PanTiltLimits->Range=soap_new_tt__Space2DDescription(glSoap, -1);
      if(d1[CMD_PARAMS]["PTZConfiguration"]["PanTiltLimits"]["Range"].HasMember("XRange")) {
        SetConfiguration->PTZConfiguration->PanTiltLimits->Range->XRange=soap_new_tt__FloatRange(glSoap, -1);
        if(d1[CMD_PARAMS]["PTZConfiguration"]["PanTiltLimits"]["Range"]["XRange"].HasMember("Max")) {
          tmpVar=std::string(d1[CMD_PARAMS]["PTZConfiguration"]["PanTiltLimits"]["Range"]["XRange"]["Max"].GetString());
          SetConfiguration->PTZConfiguration->PanTiltLimits->Range->XRange->Max=std::stof(tmpVar);
        }
        if(d1[CMD_PARAMS]["PTZConfiguration"]["PanTiltLimits"]["Range"]["XRange"].HasMember("Min")) {
          tmpVar=std::string(d1[CMD_PARAMS]["PTZConfiguration"]["PanTiltLimits"]["Range"]["XRange"]["Min"].GetString());
          SetConfiguration->PTZConfiguration->PanTiltLimits->Range->XRange->Min=std::stof(tmpVar);
        }
      }
      if(d1[CMD_PARAMS]["PTZConfiguration"]["PanTiltLimits"]["Range"].HasMember("YRange")) {
        SetConfiguration->PTZConfiguration->PanTiltLimits->Range->YRange=soap_new_tt__FloatRange(glSoap, -1);
        if(d1[CMD_PARAMS]["PTZConfiguration"]["PanTiltLimits"]["Range"]["YRange"].HasMember("Max")) {
          tmpVar=std::string(d1[CMD_PARAMS]["PTZConfiguration"]["PanTiltLimits"]["Range"]["YRange"]["Max"].GetString());
          SetConfiguration->PTZConfiguration->PanTiltLimits->Range->YRange->Max=std::stof(tmpVar);
        }
        if(d1[CMD_PARAMS]["PTZConfiguration"]["PanTiltLimits"]["Range"]["YRange"].HasMember("Min")) {
          tmpVar=std::string(d1[CMD_PARAMS]["PTZConfiguration"]["PanTiltLimits"]["Range"]["YRange"]["Min"].GetString());
          SetConfiguration->PTZConfiguration->PanTiltLimits->Range->YRange->Min=std::stof(tmpVar);
        }
      }
      if(d1[CMD_PARAMS]["PTZConfiguration"]["PanTiltLimits"]["Range"].HasMember("URI")) {
        SetConfiguration->PTZConfiguration->PanTiltLimits->Range->URI=
          std::string(d1[CMD_PARAMS]["PTZConfiguration"]["PanTiltLimits"]["Range"]["URI"].GetString());
      }
    }
  }

//End Prepare request

  faultStr="";
  if(false == sendSetConfiguration(&proxyPTZ, SetConfiguration, SetConfigurationResponse)) {
    if(verbosity>2)std::cout <<  "sendSetConfiguration failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendSetConfiguration failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGetPresetTours(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;

  _tptz__GetPresetTours * GetPresetTours;
  _tptz__GetPresetToursResponse * GetPresetToursResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetPresetTours = soap_new__tptz__GetPresetTours(glSoap, -1);
  GetPresetToursResponse = soap_new__tptz__GetPresetToursResponse(glSoap, -1);
//Prepare request
  GetPresetTours->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());
//End Prepare request

  faultStr="";
  if(false == sendGetPresetTours(&proxyPTZ, GetPresetTours, GetPresetToursResponse)) {
    if(verbosity>2)std::cout <<  "sendGetPresetTours failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetPresetTours failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{";
  if(verbosity>3) std::cout << "Number of PresetTour received is "
                              << GetPresetToursResponse->PresetTour.size() << std::endl;
  if(GetPresetToursResponse->PresetTour.size()>0) {
    outStr+="\"PresetTour\":[";
    for (unsigned i=0; i<GetPresetToursResponse->PresetTour.size(); i++) {
      if(i>0)outStr+=", ";
      outStr+="{";
      tt__PresetTour* tmpConfig=GetPresetToursResponse->PresetTour[i];
      if(tmpConfig->Status!=NULL) {
        outStr+="\"Status\":{";
        if(tmpConfig->Status->CurrentTourSpot!=NULL) {
          outStr+="\"CurrentTourSpot\":{";
          if(tmpConfig->Status->CurrentTourSpot->PresetDetail!=NULL) {
            outStr+="\"PresetDetail\":{";
            if(tmpConfig->Status->CurrentTourSpot->PresetDetail->__union_PTZPresetTourPresetDetail
                ==SOAP_UNION__tt__union_PTZPresetTourPresetDetail_PresetToken) {
              outStr+="\"PresetToken\":\""+
                      (*(tmpConfig->Status->CurrentTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.PresetToken))
                      +"\", ";
            } else if(tmpConfig->Status->CurrentTourSpot->PresetDetail->__union_PTZPresetTourPresetDetail
                      ==SOAP_UNION__tt__union_PTZPresetTourPresetDetail_Home) {
              if(tmpConfig->Status->CurrentTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.Home)
                outStr+="\"Home\":\"true\", ";
              else outStr+="\"Home\":\"false\", ";
            } else if(tmpConfig->Status->CurrentTourSpot->PresetDetail->__union_PTZPresetTourPresetDetail
                      ==SOAP_UNION__tt__union_PTZPresetTourPresetDetail_PTZPosition) {
              if(tmpConfig->Status->CurrentTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.PTZPosition!=NULL) {
                tt__PTZVector* tmpPosition=
                  tmpConfig->Status->CurrentTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.PTZPosition;
                outStr+="\"PTZPosition\":{";
                if(tmpPosition->PanTilt!=NULL) {
                  outStr+="\"PanTilt\":{";
                  outStr+="\"x\":\""+std::to_string(tmpPosition->PanTilt->x)+"\", ";
                  outStr+="\"y\":\""+std::to_string(tmpPosition->PanTilt->y)+"\"";
                  outStr+="}, ";
                }
                if(tmpPosition->Zoom!=NULL)
                  outStr+="\"Zoom\":{\"x\":\""+std::to_string(tmpPosition->Zoom->x)+"\"}";
                if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
                outStr+="}";
              }
            }
            outStr+="}, ";
          }
          if(tmpConfig->Status->CurrentTourSpot->Speed!=NULL) {
            outStr+="\"Speed\":{";
            if(tmpConfig->Status->CurrentTourSpot->Speed->PanTilt!=NULL) {
              outStr+="\"PanTilt\":{";
              outStr+="\"x\":\""+std::to_string(tmpConfig->Status->CurrentTourSpot->Speed->PanTilt->x)+"\", ";
              outStr+="\"y\":\""+std::to_string(tmpConfig->Status->CurrentTourSpot->Speed->PanTilt->y)+"\"";
              outStr+="}, ";
            }
            if(tmpConfig->Status->CurrentTourSpot->Speed->Zoom!=NULL)
              outStr+="\"Zoom\":{\"x\":\""+std::to_string(tmpConfig->Status->CurrentTourSpot->Speed->Zoom->x)+"\"}";
            if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
            outStr+="}, ";
          }
          if(tmpConfig->Status->CurrentTourSpot->StayTime!=NULL) {
            outStr+="\"StayTime\":\""+std::to_string(*(tmpConfig->Status->CurrentTourSpot->StayTime))+"\"";
          }
          if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
          outStr+="}, ";
        }
        if(tmpConfig->Status->State==tt__PTZPresetTourState__Idle)
          outStr+="\"State\:\"Idle\"";
        if(tmpConfig->Status->State==tt__PTZPresetTourState__Touring)
          outStr+="\"State\:\"Touring\"";
        if(tmpConfig->Status->State==tt__PTZPresetTourState__Paused)
          outStr+="\"State\:\"Paused\"";
        if(tmpConfig->Status->State==tt__PTZPresetTourState__Extended)
          outStr+="\"State\:\"Extended\"";
        if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
        outStr+="}, ";
      }
      if(tmpConfig->StartingCondition!=NULL) {
        outStr+="\"StartingCondition\":{";
        if(tmpConfig->StartingCondition->RandomPresetOrder!=NULL) {
          if(*tmpConfig->StartingCondition->RandomPresetOrder)
            outStr+="\"RandomPresetOrder\":\"true\", ";
          else outStr+="\"RandomPresetOrder\":\"false\", ";
        }
        if(tmpConfig->StartingCondition->RecurringDuration!=NULL) {
          outStr+="\"RecurringDuration\":\""+std::to_string(*(tmpConfig->StartingCondition->RecurringDuration))+"\", ";
        }
        if(tmpConfig->StartingCondition->RecurringTime!=NULL) {
          outStr+="\"RecurringTime\":\""+std::to_string(*(tmpConfig->StartingCondition->RecurringTime))+"\", ";
        }
        if(tmpConfig->StartingCondition->Direction!=NULL) {
          if((*tmpConfig->StartingCondition->Direction)==tt__PTZPresetTourDirection__Forward)
            outStr+="\"Direction\":\"Forward\"";
          else if((*tmpConfig->StartingCondition->Direction)==tt__PTZPresetTourDirection__Backward)
            outStr+="\"Direction\":\"Backward\"";
          else if((*tmpConfig->StartingCondition->Direction)==tt__PTZPresetTourDirection__Extended)
            outStr+="\"Direction\":\"Extended\"";
        }
        if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
        outStr+="}, ";
      }
      if(tmpConfig->TourSpot.size()>0) {
        outStr+="\"TourSpot\":[";
        for (unsigned j=0; j<tmpConfig->TourSpot.size(); j++) {
          if(j>0)outStr+=", ";
          outStr+="{";
          tt__PTZPresetTourSpot* tmpSpot=tmpConfig->TourSpot[j];
          if(tmpSpot->PresetDetail!=NULL) {
            outStr+="\"PresetDetail\":{";
            if(tmpSpot->PresetDetail->__union_PTZPresetTourPresetDetail
                ==SOAP_UNION__tt__union_PTZPresetTourPresetDetail_PresetToken) {
              outStr+="\"PresetToken\":\""+
                      (*(tmpSpot->PresetDetail->union_PTZPresetTourPresetDetail.PresetToken))
                      +"\", ";
            } else if(tmpSpot->PresetDetail->__union_PTZPresetTourPresetDetail
                      ==SOAP_UNION__tt__union_PTZPresetTourPresetDetail_Home) {
              if(tmpSpot->PresetDetail->union_PTZPresetTourPresetDetail.Home)
                outStr+="\"Home\":\"true\", ";
              else outStr+="\"Home\":\"false\", ";
            } else if(tmpSpot->PresetDetail->__union_PTZPresetTourPresetDetail
                      ==SOAP_UNION__tt__union_PTZPresetTourPresetDetail_PTZPosition) {
              if(tmpSpot->PresetDetail->union_PTZPresetTourPresetDetail.PTZPosition!=NULL) {
                tt__PTZVector* tmpPosition=
                  tmpSpot->PresetDetail->union_PTZPresetTourPresetDetail.PTZPosition;
                outStr+="\"PTZPosition\":{";
                if(tmpPosition->PanTilt!=NULL) {
                  outStr+="\"PanTilt\":{";
                  outStr+="\"x\":\""+std::to_string(tmpPosition->PanTilt->x)+"\", ";
                  outStr+="\"y\":\""+std::to_string(tmpPosition->PanTilt->y)+"\"";
                  outStr+="}, ";
                }
                if(tmpPosition->Zoom!=NULL)
                  outStr+="\"Zoom\":{\"x\":\""+std::to_string(tmpPosition->Zoom->x)+"\"}";
                if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
                outStr+="}";
              }
            }
            outStr+="}, ";
          }
          if(tmpSpot->Speed!=NULL) {
            outStr+="\"Speed\":{";
            if(tmpSpot->Speed->PanTilt!=NULL) {
              outStr+="\"PanTilt\":{";
              outStr+="\"x\":\""+std::to_string(tmpSpot->Speed->PanTilt->x)+"\", ";
              outStr+="\"y\":\""+std::to_string(tmpSpot->Speed->PanTilt->y)+"\"";
              outStr+="}, ";
            }
            if(tmpSpot->Speed->Zoom!=NULL)
              outStr+="\"Zoom\":{\"x\":\""+std::to_string(tmpSpot->Speed->Zoom->x)+"\"}";
            if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
            outStr+="}, ";
          }
          if(tmpSpot->StayTime!=NULL) {
            outStr+="\"StayTime\":\""+std::to_string(*(tmpSpot->StayTime))+"\"";
          }
          if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
          outStr+="}";
        }
        outStr+="], ";
      }
      if(tmpConfig->AutoStart==true)
        outStr+="\"AutoStart\":\"true\", ";
      else  outStr+="\"AutoStart\":\"false\", ";
      if(tmpConfig->Name!=NULL)
        outStr+="\"Name\":\""+(*(tmpConfig->Name))+"\", ";
      if(tmpConfig->token!=NULL)
        outStr+="\"token\":\""+(*(tmpConfig->token))+"\"";
      if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="]";
  }
  outStr+="}}";
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGetPresetTour(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;

  _tptz__GetPresetTour * GetPresetTour;
  _tptz__GetPresetTourResponse * GetPresetTourResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("PresetTourToken")) {
      std::cout << "Failed to process request, No PresetTourToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No PresetTourToken found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetPresetTour = soap_new__tptz__GetPresetTour(glSoap, -1);
  GetPresetTourResponse = soap_new__tptz__GetPresetTourResponse(glSoap, -1);
//Prepare request
  GetPresetTour->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());
  GetPresetTour->PresetTourToken=std::string(d1[CMD_PARAMS]["PresetTourToken"].GetString());
//End Prepare request

  faultStr="";
  if(false == sendGetPresetTour(&proxyPTZ, GetPresetTour, GetPresetTourResponse)) {
    if(verbosity>2)std::cout <<  "sendGetPresetTour failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetPresetTour failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{";
  if(GetPresetTourResponse->PresetTour!=NULL) {
    outStr+="\"PresetTour\":{";
    tt__PresetTour* tmpConfig=GetPresetTourResponse->PresetTour;
    if(tmpConfig->Status!=NULL) {
      outStr+="\"Status\":{";
      if(tmpConfig->Status->CurrentTourSpot!=NULL) {
        outStr+="\"CurrentTourSpot\":{";
        if(tmpConfig->Status->CurrentTourSpot->PresetDetail!=NULL) {
          outStr+="\"PresetDetail\":{";
          if(tmpConfig->Status->CurrentTourSpot->PresetDetail->__union_PTZPresetTourPresetDetail
              ==SOAP_UNION__tt__union_PTZPresetTourPresetDetail_PresetToken) {
            outStr+="\"PresetToken\":\""+
                    (*(tmpConfig->Status->CurrentTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.PresetToken))
                    +"\", ";
          } else if(tmpConfig->Status->CurrentTourSpot->PresetDetail->__union_PTZPresetTourPresetDetail
                    ==SOAP_UNION__tt__union_PTZPresetTourPresetDetail_Home) {
            if(tmpConfig->Status->CurrentTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.Home)
              outStr+="\"Home\":\"true\", ";
            else outStr+="\"Home\":\"false\", ";
          } else if(tmpConfig->Status->CurrentTourSpot->PresetDetail->__union_PTZPresetTourPresetDetail
                    ==SOAP_UNION__tt__union_PTZPresetTourPresetDetail_PTZPosition) {
            if(tmpConfig->Status->CurrentTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.PTZPosition!=NULL) {
              tt__PTZVector* tmpPosition=
                tmpConfig->Status->CurrentTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.PTZPosition;
              outStr+="\"PTZPosition\":{";
              if(tmpPosition->PanTilt!=NULL) {
                outStr+="\"PanTilt\":{";
                outStr+="\"x\":\""+std::to_string(tmpPosition->PanTilt->x)+"\", ";
                outStr+="\"y\":\""+std::to_string(tmpPosition->PanTilt->y)+"\"";
                outStr+="}, ";
              }
              if(tmpPosition->Zoom!=NULL)
                outStr+="\"Zoom\":{\"x\":\""+std::to_string(tmpPosition->Zoom->x)+"\"}";
              if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
              outStr+="}";
            }
          }
          outStr+="}, ";
        }
        if(tmpConfig->Status->CurrentTourSpot->Speed!=NULL) {
          outStr+="\"Speed\":{";
          if(tmpConfig->Status->CurrentTourSpot->Speed->PanTilt!=NULL) {
            outStr+="\"PanTilt\":{";
            outStr+="\"x\":\""+std::to_string(tmpConfig->Status->CurrentTourSpot->Speed->PanTilt->x)+"\", ";
            outStr+="\"y\":\""+std::to_string(tmpConfig->Status->CurrentTourSpot->Speed->PanTilt->y)+"\"";
            outStr+="}, ";
          }
          if(tmpConfig->Status->CurrentTourSpot->Speed->Zoom!=NULL)
            outStr+="\"Zoom\":{\"x\":\""+std::to_string(tmpConfig->Status->CurrentTourSpot->Speed->Zoom->x)+"\"}";
          if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
          outStr+="}, ";
        }
        if(tmpConfig->Status->CurrentTourSpot->StayTime!=NULL) {
          outStr+="\"StayTime\":\""+std::to_string(*(tmpConfig->Status->CurrentTourSpot->StayTime))+"\"";
        }
        if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
        outStr+="}, ";
      }
      if(tmpConfig->Status->State==tt__PTZPresetTourState__Idle)
        outStr+="\"State\:\"Idle\"";
      if(tmpConfig->Status->State==tt__PTZPresetTourState__Touring)
        outStr+="\"State\:\"Touring\"";
      if(tmpConfig->Status->State==tt__PTZPresetTourState__Paused)
        outStr+="\"State\:\"Paused\"";
      if(tmpConfig->Status->State==tt__PTZPresetTourState__Extended)
        outStr+="\"State\:\"Extended\"";
      if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
      outStr+="}, ";
    }
    if(tmpConfig->StartingCondition!=NULL) {
      outStr+="\"StartingCondition\":{";
      if(tmpConfig->StartingCondition->RandomPresetOrder!=NULL) {
        if(*tmpConfig->StartingCondition->RandomPresetOrder)
          outStr+="\"RandomPresetOrder\":\"true\", ";
        else outStr+="\"RandomPresetOrder\":\"false\", ";
      }
      if(tmpConfig->StartingCondition->RecurringDuration!=NULL) {
        outStr+="\"RecurringDuration\":\""+std::to_string(*(tmpConfig->StartingCondition->RecurringDuration))+"\", ";
      }
      if(tmpConfig->StartingCondition->RecurringTime!=NULL) {
        outStr+="\"RecurringTime\":\""+std::to_string(*(tmpConfig->StartingCondition->RecurringTime))+"\", ";
      }
      if(tmpConfig->StartingCondition->Direction!=NULL) {
        if((*tmpConfig->StartingCondition->Direction)==tt__PTZPresetTourDirection__Forward)
          outStr+="\"Direction\":\"Forward\"";
        else if((*tmpConfig->StartingCondition->Direction)==tt__PTZPresetTourDirection__Backward)
          outStr+="\"Direction\":\"Backward\"";
        else if((*tmpConfig->StartingCondition->Direction)==tt__PTZPresetTourDirection__Extended)
          outStr+="\"Direction\":\"Extended\"";
      }
      if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
      outStr+="}, ";
    }
    if(tmpConfig->TourSpot.size()>0) {
      outStr+="\"TourSpot\":[";
      for (unsigned j=0; j<tmpConfig->TourSpot.size(); j++) {
        if(j>0)outStr+=", ";
        outStr+="{";
        tt__PTZPresetTourSpot* tmpSpot=tmpConfig->TourSpot[j];
        if(tmpSpot->PresetDetail!=NULL) {
          outStr+="\"PresetDetail\":{";
          if(tmpSpot->PresetDetail->__union_PTZPresetTourPresetDetail
              ==SOAP_UNION__tt__union_PTZPresetTourPresetDetail_PresetToken) {
            outStr+="\"PresetToken\":\""+
                    (*(tmpSpot->PresetDetail->union_PTZPresetTourPresetDetail.PresetToken))
                    +"\", ";
          } else if(tmpSpot->PresetDetail->__union_PTZPresetTourPresetDetail
                    ==SOAP_UNION__tt__union_PTZPresetTourPresetDetail_Home) {
            if(tmpSpot->PresetDetail->union_PTZPresetTourPresetDetail.Home)
              outStr+="\"Home\":\"true\", ";
            else outStr+="\"Home\":\"false\", ";
          } else if(tmpSpot->PresetDetail->__union_PTZPresetTourPresetDetail
                    ==SOAP_UNION__tt__union_PTZPresetTourPresetDetail_PTZPosition) {
            if(tmpSpot->PresetDetail->union_PTZPresetTourPresetDetail.PTZPosition!=NULL) {
              tt__PTZVector* tmpPosition=
                tmpSpot->PresetDetail->union_PTZPresetTourPresetDetail.PTZPosition;
              outStr+="\"PTZPosition\":{";
              if(tmpPosition->PanTilt!=NULL) {
                outStr+="\"PanTilt\":{";
                outStr+="\"x\":\""+std::to_string(tmpPosition->PanTilt->x)+"\", ";
                outStr+="\"y\":\""+std::to_string(tmpPosition->PanTilt->y)+"\"";
                outStr+="}, ";
              }
              if(tmpPosition->Zoom!=NULL)
                outStr+="\"Zoom\":{\"x\":\""+std::to_string(tmpPosition->Zoom->x)+"\"}";
              if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
              outStr+="}";
            }
          }
          outStr+="}, ";
        }
        if(tmpSpot->Speed!=NULL) {
          outStr+="\"Speed\":{";
          if(tmpSpot->Speed->PanTilt!=NULL) {
            outStr+="\"PanTilt\":{";
            outStr+="\"x\":\""+std::to_string(tmpSpot->Speed->PanTilt->x)+"\", ";
            outStr+="\"y\":\""+std::to_string(tmpSpot->Speed->PanTilt->y)+"\"";
            outStr+="}, ";
          }
          if(tmpSpot->Speed->Zoom!=NULL)
            outStr+="\"Zoom\":{\"x\":\""+std::to_string(tmpSpot->Speed->Zoom->x)+"\"}";
          if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
          outStr+="}, ";
        }
        if(tmpSpot->StayTime!=NULL) {
          outStr+="\"StayTime\":\""+std::to_string(*(tmpSpot->StayTime))+"\"";
        }
        if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
        outStr+="}";
      }
      outStr+="], ";
    }
    if(tmpConfig->AutoStart==true)
      outStr+="\"AutoStart\":\"true\", ";
    else  outStr+="\"AutoStart\":\"false\", ";
    if(tmpConfig->Name!=NULL)
      outStr+="\"Name\":\""+(*(tmpConfig->Name))+"\", ";
    if(tmpConfig->token!=NULL)
      outStr+="\"token\":\""+(*(tmpConfig->token))+"\"";
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}";
  }
  outStr+="}}";
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGetProfiles(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;

  _trt__GetProfiles * GetProfiles;
  _trt__GetProfilesResponse * GetProfilesResponse;


  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyMedia.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyMedia.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyMedia.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetProfiles = soap_new__trt__GetProfiles(glSoap, -1);
  GetProfilesResponse = soap_new__trt__GetProfilesResponse(glSoap, -1);
//Prepare request
//End Prepare request

  faultStr="";
  if(false == sendGetProfiles(&proxyMedia, GetProfiles, GetProfilesResponse)) {
    if(verbosity>2)std::cout <<  "sendGetProfiles failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetProfiles failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{";
  if(verbosity>3) std::cout << "Number of Profiles received is "
                              << GetProfilesResponse->Profiles.size() << std::endl;
  if(GetProfilesResponse->Profiles.size()>0) {
    outStr+="\"Profiles\":[";
    for (unsigned i=0; i<GetProfilesResponse->Profiles.size(); i++) {
      if(i>0)outStr+=", ";
      outStr+="{";
      tt__Profile* tmpConfig=GetProfilesResponse->Profiles[i];
      if(tmpConfig->PTZConfiguration!=NULL) {
        tt__PTZConfiguration* tmpPTZConfig=tmpConfig->PTZConfiguration;
        outStr+="\"PTZConfiguration\":{";
        if(tmpPTZConfig->DefaultPTZSpeed!=NULL) {
          outStr+="\"DefaultPTZSpeed\":{";
          if(tmpPTZConfig->DefaultPTZSpeed->PanTilt!=NULL) {
            outStr+="\"PanTilt\":{";
            outStr+="\"x\":\""+std::to_string(tmpPTZConfig->DefaultPTZSpeed->PanTilt->x)+"\", ";
            outStr+="\"y\":\""+std::to_string(tmpPTZConfig->DefaultPTZSpeed->PanTilt->y)+"\"";
            outStr+="}, ";
          }
          if(tmpPTZConfig->DefaultPTZSpeed->Zoom!=NULL)
            outStr+="\"Zoom\":{\"x\":\""+std::to_string(tmpPTZConfig->DefaultPTZSpeed->Zoom->x)+"\"}";
          if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
          outStr+="}, ";
        }
        if(tmpPTZConfig->PanTiltLimits!=NULL) {
          outStr+="\"PanTiltLimits\":{";
          if(tmpPTZConfig->PanTiltLimits->Range!=NULL) {
            outStr+="\"Range\":{";
            if(tmpPTZConfig->PanTiltLimits->Range->XRange!=NULL) {
              outStr+="\"XRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpPTZConfig->PanTiltLimits->Range->XRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpPTZConfig->PanTiltLimits->Range->XRange->Max)+"\"";
              outStr+="}, ";
            }
            if(tmpPTZConfig->PanTiltLimits->Range->YRange!=NULL) {
              outStr+="\"YRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpPTZConfig->PanTiltLimits->Range->YRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpPTZConfig->PanTiltLimits->Range->YRange->Max)+"\"";
              outStr+="}, ";
            }
            outStr+="\"URI\":\"" + tmpPTZConfig->PanTiltLimits->Range->URI +"\"";
            outStr+="}";
          }
          outStr+="}, ";
        }
        if(tmpPTZConfig->ZoomLimits!=NULL) {
          outStr+="\"ZoomLimits\":{";
          if(tmpPTZConfig->ZoomLimits->Range!=NULL) {
            outStr+="\"Range\":{";
            if(tmpPTZConfig->ZoomLimits->Range->XRange!=NULL) {
              outStr+="\"XRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpPTZConfig->ZoomLimits->Range->XRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpPTZConfig->ZoomLimits->Range->XRange->Max)+"\"";
              outStr+="}, ";
            }
            outStr+="\"URI\":\"" + tmpPTZConfig->ZoomLimits->Range->URI +"\"";
            outStr+="}";
          }
          outStr+="}, ";
        }
        if(tmpPTZConfig->DefaultAbsolutePantTiltPositionSpace!=NULL)
          outStr+="\"DefaultAbsolutePantTiltPositionSpace\":\""+
                  (*(tmpPTZConfig->DefaultAbsolutePantTiltPositionSpace))+"\", ";
        if(tmpPTZConfig->DefaultAbsoluteZoomPositionSpace!=NULL)
          outStr+="\"DefaultAbsoluteZoomPositionSpace\":\""+
                  (*(tmpPTZConfig->DefaultAbsoluteZoomPositionSpace))+"\", ";
        if(tmpPTZConfig->DefaultContinuousPanTiltVelocitySpace!=NULL)
          outStr+="\"DefaultContinuousPanTiltVelocitySpace\":\""+
                  (*(tmpPTZConfig->DefaultContinuousPanTiltVelocitySpace))+"\", ";
        if(tmpPTZConfig->DefaultContinuousZoomVelocitySpace!=NULL)
          outStr+="\"DefaultContinuousZoomVelocitySpace\":\""+
                  (*(tmpPTZConfig->DefaultContinuousZoomVelocitySpace))+"\", ";
        if(tmpPTZConfig->DefaultRelativePanTiltTranslationSpace!=NULL)
          outStr+="\"DefaultRelativePanTiltTranslationSpace\":\""+
                  (*(tmpPTZConfig->DefaultRelativePanTiltTranslationSpace))+"\", ";
        if(tmpPTZConfig->DefaultRelativeZoomTranslationSpace!=NULL)
          outStr+="\"DefaultRelativeZoomTranslationSpace\":\""+
                  (*(tmpPTZConfig->DefaultRelativeZoomTranslationSpace))+"\", ";
        if(tmpPTZConfig->DefaultPTZTimeout!=NULL)
          outStr+="\"DefaultPTZTimeout\":\""+std::to_string(*(tmpPTZConfig->DefaultPTZTimeout))+"\", ";
        if(tmpPTZConfig->PresetTourRamp!=NULL)
          outStr+="\"PresetTourRamp\":\""+std::to_string(*(tmpPTZConfig->PresetTourRamp))+"\", ";
        if(tmpPTZConfig->PresetRamp!=NULL)
          outStr+="\"PresetRamp\":\""+std::to_string(*(tmpPTZConfig->PresetRamp))+"\", ";
        if(tmpPTZConfig->MoveRamp!=NULL)
          outStr+="\"MoveRamp\":\""+std::to_string(*(tmpPTZConfig->MoveRamp))+"\", ";
        outStr+="\"UseCount\":\""+std::to_string(tmpPTZConfig->UseCount)+"\", ";
        outStr+="\"Name\":\""+tmpPTZConfig->Name+"\", ";
        outStr+="\"NodeToken\":\""+tmpPTZConfig->NodeToken+"\", ";
        outStr+="\"token\":\""+tmpPTZConfig->token+"\"";
        outStr+="}, ";
      }
      if(tmpConfig->VideoEncoderConfiguration!=NULL) {
        outStr+="\"VideoEncoderConfiguration\":{";
        if(tmpConfig->VideoEncoderConfiguration->H264!=NULL) {
          outStr+="\"H264\":{";
          if(tmpConfig->VideoEncoderConfiguration->H264->H264Profile==tt__H264Profile__Baseline)
            outStr+="\"H264Profile\":\"Baseline\", ";
          else if(tmpConfig->VideoEncoderConfiguration->H264->H264Profile==tt__H264Profile__Main)
            outStr+="\"H264Profile\":\"Main\", ";
          else if(tmpConfig->VideoEncoderConfiguration->H264->H264Profile==tt__H264Profile__Extended)
            outStr+="\"H264Profile\":\"Extended\", ";
          else if(tmpConfig->VideoEncoderConfiguration->H264->H264Profile==tt__H264Profile__High)
            outStr+="\"H264Profile\":\"High\", ";
          outStr+="\"GovLength\":\""+std::to_string(tmpConfig->VideoEncoderConfiguration->H264->GovLength)+"\"";
          outStr+="}, ";
        }
        if(tmpConfig->VideoEncoderConfiguration->RateControl!=NULL) {
          outStr+="\"RateControl\":{";
          outStr+="\"BitrateLimit\":\""+
                std::to_string(tmpConfig->VideoEncoderConfiguration->RateControl->BitrateLimit)+"\", ";
          outStr+="\"EncodingInterval\":\""+
                std::to_string(tmpConfig->VideoEncoderConfiguration->RateControl->EncodingInterval)+"\", ";
          outStr+="\"FrameRateLimit\":\""+
                std::to_string(tmpConfig->VideoEncoderConfiguration->RateControl->FrameRateLimit)+"\"";
          outStr+="}, ";
        }
        if(tmpConfig->VideoEncoderConfiguration->Resolution!=NULL) {
          outStr+="\"Resolution\":{";
          outStr+="\"Height\":\""+std::to_string(tmpConfig->VideoEncoderConfiguration->Resolution->Height)+"\", ";
          outStr+="\"Width\":\""+std::to_string(tmpConfig->VideoEncoderConfiguration->Resolution->Width)+"\"";
          outStr+="}, ";
        }
        if(tmpConfig->VideoEncoderConfiguration->MPEG4!=NULL) {
          outStr+="\"MPEG4\":{";
          if(tmpConfig->VideoEncoderConfiguration->MPEG4->Mpeg4Profile==tt__Mpeg4Profile__SP)
            outStr+="\"Mpeg4Profile\":\"SP\", ";
          else if(tmpConfig->VideoEncoderConfiguration->MPEG4->Mpeg4Profile==tt__Mpeg4Profile__ASP)
            outStr+="\"Mpeg4Profile\":\"ASP\", ";
          outStr+="\"GovLength\":\""+std::to_string(tmpConfig->VideoEncoderConfiguration->MPEG4->GovLength)+"\"";
          outStr+="}, ";
        }
        if(tmpConfig->VideoEncoderConfiguration->Encoding==tt__VideoEncoding__H264)
          outStr+="\"Encoding\":\"H264\", ";
        else if(tmpConfig->VideoEncoderConfiguration->Encoding==tt__VideoEncoding__MPEG4)
          outStr+="\"Encoding\":\"MPEG4\", ";
        else if(tmpConfig->VideoEncoderConfiguration->Encoding==tt__VideoEncoding__JPEG)
          outStr+="\"Encoding\":\"JPEG\", ";
        outStr+="\"UseCount\":\""+std::to_string(tmpConfig->VideoEncoderConfiguration->UseCount)+"\", ";
        outStr+="\"SessionTimeout\":\""+std::to_string(tmpConfig->VideoEncoderConfiguration->SessionTimeout)+"\", ";
        outStr+="\"Quality\":\""+std::to_string(tmpConfig->VideoEncoderConfiguration->Quality)+"\", ";
        outStr+="\"Name\":\""+tmpConfig->VideoEncoderConfiguration->Name+"\", ";
        outStr+="\"token\":\""+tmpConfig->VideoEncoderConfiguration->token+"\"";
        outStr+="}, ";
      }
      if(tmpConfig->VideoSourceConfiguration!=NULL){
        outStr+="\"VideoSourceConfiguration\":{";
        outStr+="\"token\":\""+tmpConfig->VideoSourceConfiguration->token+"\"";
        outStr+="}, ";
      }
      outStr+="\"Name\":\""+tmpConfig->Name+"\", ";
      outStr+="\"token\":\""+tmpConfig->token+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="]";
  }
  outStr+="}}";
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execSetVideoEncoderConfiguration(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;
  std::string encToken="";

  _trt__SetVideoEncoderConfiguration * SetVideoEncoderConfiguration;
  _trt__SetVideoEncoderConfigurationResponse * SetVideoEncoderConfigurationResponse;

  _trt__GetVideoEncoderConfiguration * GetVideoEncoderConfiguration;
  _trt__GetVideoEncoderConfigurationResponse * GetVideoEncoderConfigurationResponse;

  tt__VideoEncoderConfiguration* tmpConfig;

  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("Configuration")) {
      std::cout << "Failed to process request, No Configuration found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No Configuration found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS]["Configuration"].HasMember("token")) {
      std::cout << "Failed to process request, No Configuration:token found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No Configuration:token found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyMedia.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyMedia.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyMedia.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  encToken=std::string(d1[CMD_PARAMS]["Configuration"]["token"].GetString());

  GetVideoEncoderConfiguration = soap_new__trt__GetVideoEncoderConfiguration(glSoap, -1);
  GetVideoEncoderConfigurationResponse = soap_new__trt__GetVideoEncoderConfigurationResponse(glSoap, -1);
  GetVideoEncoderConfiguration->ConfigurationToken=encToken;

  faultStr="";
  if(false == sendGetVideoEncoderConfiguration(&proxyMedia, GetVideoEncoderConfiguration,
                                                GetVideoEncoderConfigurationResponse)) {
    if(verbosity>2)std::cout <<  "sendGetVideoEncoderConfiguration failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetVideoEncoderConfiguration failed all attempts\", \"message\":\""
      +faultStr+"\"}";
    goto cleanSendResponse;
  }


  SetVideoEncoderConfiguration = soap_new__trt__SetVideoEncoderConfiguration(glSoap, -1);
  SetVideoEncoderConfigurationResponse = soap_new__trt__SetVideoEncoderConfigurationResponse(glSoap, -1);


  SetVideoEncoderConfiguration->Configuration=GetVideoEncoderConfigurationResponse->Configuration;

//Prepare request
  tmpConfig=SetVideoEncoderConfiguration->Configuration;
  if(d1[CMD_PARAMS]["Configuration"].HasMember("Encoding")) {
    tmpVar=std::string(d1[CMD_PARAMS]["Configuration"]["Encoding"].GetString());
    if(tmpVar=="H264") tmpConfig->Encoding=tt__VideoEncoding__H264;
    else if(tmpVar=="MPEG4") tmpConfig->Encoding=tt__VideoEncoding__MPEG4;
    else if(tmpVar=="JPEG") tmpConfig->Encoding=tt__VideoEncoding__JPEG;
  }
  if(d1[CMD_PARAMS]["Configuration"].HasMember("UseCount")) {
    tmpVar=std::string(d1[CMD_PARAMS]["Configuration"]["UseCount"].GetString());
    tmpConfig->UseCount=std::stoi(tmpVar);
  }
  if(d1[CMD_PARAMS]["Configuration"].HasMember("SessionTimeout")) {
    tmpVar=std::string(d1[CMD_PARAMS]["Configuration"]["SessionTimeout"].GetString());
    tmpConfig->SessionTimeout=std::stoll(tmpVar);
  }
  if(d1[CMD_PARAMS]["Configuration"].HasMember("Quality")) {
    tmpVar=std::string(d1[CMD_PARAMS]["Configuration"]["Quality"].GetString());
    tmpConfig->Quality=std::stof(tmpVar);
  }
  if(d1[CMD_PARAMS]["Configuration"].HasMember("Name")) {
    tmpConfig->Name=std::string(d1[CMD_PARAMS]["Configuration"]["Name"].GetString());
  }
  if(d1[CMD_PARAMS]["Configuration"].HasMember("Resolution")) {
    if(d1[CMD_PARAMS]["Configuration"]["Resolution"].HasMember("Height")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Configuration"]["Resolution"]["Height"].GetString());
      tmpConfig->Resolution->Height=std::stoi(tmpVar);
    }
    if(d1[CMD_PARAMS]["Configuration"]["Resolution"].HasMember("Width")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Configuration"]["Resolution"]["Width"].GetString());
      tmpConfig->Resolution->Width=std::stoi(tmpVar);
    }
  }
  if(d1[CMD_PARAMS]["Configuration"].HasMember("RateControl")) {
    if(tmpConfig->RateControl==NULL) tmpConfig->RateControl=soap_new_tt__VideoRateControl(glSoap,-1);
    if(d1[CMD_PARAMS]["Configuration"]["RateControl"].HasMember("BitrateLimit")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Configuration"]["RateControl"]["BitrateLimit"].GetString());
      tmpConfig->RateControl->BitrateLimit=std::stoi(tmpVar);
    }
    if(d1[CMD_PARAMS]["Configuration"]["RateControl"].HasMember("EncodingInterval")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Configuration"]["RateControl"]["EncodingInterval"].GetString());
      tmpConfig->RateControl->EncodingInterval=std::stoi(tmpVar);
    }
    if(d1[CMD_PARAMS]["Configuration"]["RateControl"].HasMember("FrameRateLimit")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Configuration"]["RateControl"]["FrameRateLimit"].GetString());
      tmpConfig->RateControl->FrameRateLimit=std::stoi(tmpVar);
    }
  }
  if(d1[CMD_PARAMS]["Configuration"].HasMember("H264")) {
    if(tmpConfig->H264==NULL) tmpConfig->H264=soap_new_tt__H264Configuration(glSoap,-1);
    if(d1[CMD_PARAMS]["Configuration"]["H264"].HasMember("H264Profile")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Configuration"]["H264"]["H264Profile"].GetString());
      if(tmpVar=="Baseline") tmpConfig->H264->H264Profile=tt__H264Profile__Baseline;
      else if(tmpVar=="Main") tmpConfig->H264->H264Profile=tt__H264Profile__Main;
      else if(tmpVar=="Extended") tmpConfig->H264->H264Profile=tt__H264Profile__Extended;
      else if(tmpVar=="High") tmpConfig->H264->H264Profile=tt__H264Profile__High;
    }
    if(d1[CMD_PARAMS]["Configuration"]["H264"].HasMember("GovLength")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Configuration"]["H264"]["GovLength"].GetString());
      tmpConfig->H264->GovLength=std::stoi(tmpVar);
    }
  }
  if(d1[CMD_PARAMS]["Configuration"].HasMember("MPEG4")) {
    if(tmpConfig->MPEG4==NULL) tmpConfig->MPEG4=soap_new_tt__Mpeg4Configuration(glSoap,-1);
    if(d1[CMD_PARAMS]["Configuration"]["MPEG4"].HasMember("Mpeg4Profile")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Configuration"]["MPEG4"]["Mpeg4Profile"].GetString());
      if(tmpVar=="SP") tmpConfig->MPEG4->Mpeg4Profile=tt__Mpeg4Profile__SP;
      else if(tmpVar=="ASP") tmpConfig->MPEG4->Mpeg4Profile=tt__Mpeg4Profile__ASP;
    }
    if(d1[CMD_PARAMS]["Configuration"]["MPEG4"].HasMember("GovLength")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Configuration"]["MPEG4"]["GovLength"].GetString());
      tmpConfig->MPEG4->GovLength=std::stoi(tmpVar);
    }
  }
  if(d1[CMD_PARAMS].HasMember("ForcePersistence")) {
    tmpVar=std::string(d1[CMD_PARAMS]["ForcePersistence"].GetString());
    if(tmpVar=="true") SetVideoEncoderConfiguration->ForcePersistence=true;
    else SetVideoEncoderConfiguration->ForcePersistence=false;
  }
  else SetVideoEncoderConfiguration->ForcePersistence=false;
//End prepare request

  faultStr="";
  if(false == sendSetVideoEncoderConfiguration(&proxyMedia, SetVideoEncoderConfiguration,
                                                SetVideoEncoderConfigurationResponse)) {
    if(verbosity>2)std::cout <<  "sendSetVideoEncoderConfiguration failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendSetVideoEncoderConfiguration failed all attempts\", \"message\":\""
      +faultStr+"\"}";
    goto cleanSendResponse;
  }


cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGetDeviceInformation(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;

  _tds__GetDeviceInformation * GetDeviceInformation;
  _tds__GetDeviceInformationResponse * GetDeviceInformationResponse;


  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyDevice.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyDevice.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyDevice.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyDevice.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyDevice.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetDeviceInformation = soap_new__tds__GetDeviceInformation(glSoap, -1);
  GetDeviceInformationResponse = soap_new__tds__GetDeviceInformationResponse(glSoap, -1);
//Prepare request
//End Prepare request

  faultStr="";
  if(false == sendGetDeviceInformation(&proxyDevice, GetDeviceInformation, GetDeviceInformationResponse)) {
    if(verbosity>2)std::cout <<  "sendGetDeviceInformation failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetDeviceInformation failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{";
  outStr+="\"FirmwareVersion\":\""+GetDeviceInformationResponse->FirmwareVersion+"\", ";
  outStr+="\"HardwareId\":\""+GetDeviceInformationResponse->HardwareId+"\", ";
  outStr+="\"Manufacturer\":\""+GetDeviceInformationResponse->Manufacturer+"\", ";
  outStr+="\"Model\":\""+GetDeviceInformationResponse->Model+"\", ";
  outStr+="\"SerialNumber\":\""+GetDeviceInformationResponse->SerialNumber+"\"";
  outStr+="}}";
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGetSystemDateAndTime(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;

  _tds__GetSystemDateAndTime * GetSystemDateAndTime;
  _tds__GetSystemDateAndTimeResponse * GetSystemDateAndTimeResponse;


  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyDevice.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyDevice.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyDevice.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyDevice.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyDevice.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetSystemDateAndTime = soap_new__tds__GetSystemDateAndTime(glSoap, -1);
  GetSystemDateAndTimeResponse = soap_new__tds__GetSystemDateAndTimeResponse(glSoap, -1);
//Prepare request
//End Prepare request

  faultStr="";
  if(false == sendGetSystemDateAndTime(&proxyDevice, GetSystemDateAndTime, GetSystemDateAndTimeResponse)) {
    if(verbosity>2)std::cout <<  "sendGetSystemDateAndTime failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetSystemDateAndTime failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{\"SystemDateAndTime\":{";
  if(GetSystemDateAndTimeResponse->SystemDateAndTime->TimeZone!=NULL){
    outStr+="\"TimeZone\":\""+GetSystemDateAndTimeResponse->SystemDateAndTime->TimeZone->TZ+"\", ";
  }
  if(GetSystemDateAndTimeResponse->SystemDateAndTime->UTCDateTime!=NULL){
    outStr+="\"UTCDateTime\":{";
    outStr+="\"Date\":{";
    outStr+="\"Year\":\""+
        std::to_string(GetSystemDateAndTimeResponse->SystemDateAndTime->UTCDateTime->Date->Year)+"\", ";
    outStr+="\"Month\":\""+
        std::to_string(GetSystemDateAndTimeResponse->SystemDateAndTime->UTCDateTime->Date->Month)+"\", ";
    outStr+="\"Day\":\""+
        std::to_string(GetSystemDateAndTimeResponse->SystemDateAndTime->UTCDateTime->Date->Day)+"\"";
    outStr+="}, ";
    outStr+="\"Time\":{";
    outStr+="\"Hour\":\""+
        std::to_string(GetSystemDateAndTimeResponse->SystemDateAndTime->UTCDateTime->Time->Hour)+"\", ";
    outStr+="\"Minute\":\""+
        std::to_string(GetSystemDateAndTimeResponse->SystemDateAndTime->UTCDateTime->Time->Minute)+"\", ";
    outStr+="\"Second\":\""+
        std::to_string(GetSystemDateAndTimeResponse->SystemDateAndTime->UTCDateTime->Time->Second)+"\"";
    outStr+="}}, ";
  }
  if(GetSystemDateAndTimeResponse->SystemDateAndTime->LocalDateTime!=NULL){
    outStr+="\"LocalDateTime\":{";
    outStr+="\"Date\":{";
    outStr+="\"Year\":\""+
        std::to_string(GetSystemDateAndTimeResponse->SystemDateAndTime->LocalDateTime->Date->Year)+"\", ";
    outStr+="\"Month\":\""+
        std::to_string(GetSystemDateAndTimeResponse->SystemDateAndTime->LocalDateTime->Date->Month)+"\", ";
    outStr+="\"Day\":\""+
        std::to_string(GetSystemDateAndTimeResponse->SystemDateAndTime->LocalDateTime->Date->Day)+"\"";
    outStr+="}, ";
    outStr+="\"Time\":{";
    outStr+="\"Hour\":\""+
        std::to_string(GetSystemDateAndTimeResponse->SystemDateAndTime->LocalDateTime->Time->Hour)+"\", ";
    outStr+="\"Minute\":\""+
        std::to_string(GetSystemDateAndTimeResponse->SystemDateAndTime->LocalDateTime->Time->Minute)+"\", ";
    outStr+="\"Second\":\""+
        std::to_string(GetSystemDateAndTimeResponse->SystemDateAndTime->LocalDateTime->Time->Second)+"\"";
    outStr+="}}, ";
  }
  if(GetSystemDateAndTimeResponse->SystemDateAndTime->DaylightSavings)
    outStr+="\"DaylightSavings\":\"true\", ";
  else outStr+="\"DaylightSavings\":\"false\", ";

  if(GetSystemDateAndTimeResponse->SystemDateAndTime->DateTimeType==tt__SetDateTimeType__Manual)
    outStr+="\"DateTimeType\":\"Manual\", ";
  else outStr+="\"DateTimeType\":\"NTP\"";
  outStr+="}}}";
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execSetSystemDateAndTime(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;

  _tds__SetSystemDateAndTime * SetSystemDateAndTime;
  _tds__SetSystemDateAndTimeResponse * SetSystemDateAndTimeResponse;

  _tds__GetSystemDateAndTime * GetSystemDateAndTime;
  _tds__GetSystemDateAndTimeResponse * GetSystemDateAndTimeResponse;


  if (d1.HasMember(CMD_PARAMS)) {
  }
  else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyDevice.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyDevice.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyDevice.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyDevice.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyDevice.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetSystemDateAndTime = soap_new__tds__GetSystemDateAndTime(glSoap, -1);
  GetSystemDateAndTimeResponse = soap_new__tds__GetSystemDateAndTimeResponse(glSoap, -1);

  faultStr="";
  if(false == sendGetSystemDateAndTime(&proxyDevice, GetSystemDateAndTime,
                                                GetSystemDateAndTimeResponse)) {
    if(verbosity>2)std::cout <<  "sendGetSystemDateAndTime failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetSystemDateAndTime failed all attempts\", \"message\":\""
      +faultStr+"\"}";
    goto cleanSendResponse;
  }


  SetSystemDateAndTime = soap_new__tds__SetSystemDateAndTime(glSoap, -1);
  SetSystemDateAndTimeResponse = soap_new__tds__SetSystemDateAndTimeResponse(glSoap, -1);


  SetSystemDateAndTime->DaylightSavings=GetSystemDateAndTimeResponse->SystemDateAndTime->DaylightSavings;
  SetSystemDateAndTime->DateTimeType=GetSystemDateAndTimeResponse->SystemDateAndTime->DateTimeType;
  SetSystemDateAndTime->TimeZone=GetSystemDateAndTimeResponse->SystemDateAndTime->TimeZone;
  SetSystemDateAndTime->UTCDateTime=GetSystemDateAndTimeResponse->SystemDateAndTime->UTCDateTime;

//Prepare request
  if(d1[CMD_PARAMS].HasMember("DateTimeType")) {
    tmpVar=std::string(d1[CMD_PARAMS]["DateTimeType"].GetString());
    if(tmpVar=="Manual") SetSystemDateAndTime->DateTimeType=tt__SetDateTimeType__Manual;
    else SetSystemDateAndTime->DateTimeType=tt__SetDateTimeType__NTP;
  }
  if(d1[CMD_PARAMS].HasMember("DaylightSavings")) {
    tmpVar=std::string(d1[CMD_PARAMS]["DaylightSavings"].GetString());
    if(tmpVar=="true") SetSystemDateAndTime->DaylightSavings=true;
    else SetSystemDateAndTime->DaylightSavings=false;
  }
  if(d1[CMD_PARAMS].HasMember("TimeZone")) {
    tmpVar=std::string(d1[CMD_PARAMS]["TimeZone"].GetString());
    if(SetSystemDateAndTime->TimeZone==NULL) SetSystemDateAndTime->TimeZone=soap_new_tt__TimeZone(glSoap,-1);
    SetSystemDateAndTime->TimeZone->TZ=tmpVar;
  }
  if(d1[CMD_PARAMS].HasMember("UTCDateTime")) {
    if(SetSystemDateAndTime->UTCDateTime==NULL) SetSystemDateAndTime->UTCDateTime=soap_new_tt__DateTime(glSoap,-1);
    if(SetSystemDateAndTime->UTCDateTime->Date==NULL) SetSystemDateAndTime->UTCDateTime->Date=soap_new_tt__Date(glSoap,-1);
    if(SetSystemDateAndTime->UTCDateTime->Time==NULL) SetSystemDateAndTime->UTCDateTime->Time=soap_new_tt__Time(glSoap,-1);
    if(d1[CMD_PARAMS]["UTCDateTime"].HasMember("Date")) {
      if(d1[CMD_PARAMS]["UTCDateTime"]["Date"].HasMember("Year")) {
        tmpVar=std::string(d1[CMD_PARAMS]["UTCDateTime"]["Date"]["Year"].GetString());
        SetSystemDateAndTime->UTCDateTime->Date->Year=std::stoi(tmpVar);
      }
      else{
        std::cout << "Failed to process request, No UTCDateTime:Date:Year found" << std::endl;
        outStr="{\"status\":\"ERROR\", \"reason\":\"No UTCDateTime:Date:Year found\"}";
        goto sendResponse;
      }
      if(d1[CMD_PARAMS]["UTCDateTime"]["Date"].HasMember("Month")) {
        tmpVar=std::string(d1[CMD_PARAMS]["UTCDateTime"]["Date"]["Month"].GetString());
        SetSystemDateAndTime->UTCDateTime->Date->Month=std::stoi(tmpVar);
      }
      else{
        std::cout << "Failed to process request, No UTCDateTime:Date:Month found" << std::endl;
        outStr="{\"status\":\"ERROR\", \"reason\":\"No UTCDateTime:Date:Month found\"}";
        goto sendResponse;
      }
      if(d1[CMD_PARAMS]["UTCDateTime"]["Date"].HasMember("Day")) {
        tmpVar=std::string(d1[CMD_PARAMS]["UTCDateTime"]["Date"]["Day"].GetString());
        SetSystemDateAndTime->UTCDateTime->Date->Day=std::stoi(tmpVar);
      }
      else{
        std::cout << "Failed to process request, No UTCDateTime:Date:Day found" << std::endl;
        outStr="{\"status\":\"ERROR\", \"reason\":\"No UTCDateTime:Date:Day found\"}";
        goto sendResponse;
      }
    }
    else{
      std::cout << "Failed to process request, No UTCDateTime:Date found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No UTCDateTime:Date found\"}";
      goto sendResponse;
    }
    if(d1[CMD_PARAMS]["UTCDateTime"].HasMember("Time")) {
      if(d1[CMD_PARAMS]["UTCDateTime"]["Time"].HasMember("Hour")) {
        tmpVar=std::string(d1[CMD_PARAMS]["UTCDateTime"]["Time"]["Hour"].GetString());
        SetSystemDateAndTime->UTCDateTime->Time->Hour=std::stoi(tmpVar);
      }
      else{
        std::cout << "Failed to process request, No UTCDateTime:Time:Hour found" << std::endl;
        outStr="{\"status\":\"ERROR\", \"reason\":\"No UTCDateTime:Time:Hour found\"}";
        goto sendResponse;
      }
      if(d1[CMD_PARAMS]["UTCDateTime"]["Time"].HasMember("Minute")) {
        tmpVar=std::string(d1[CMD_PARAMS]["UTCDateTime"]["Time"]["Minute"].GetString());
        SetSystemDateAndTime->UTCDateTime->Time->Minute=std::stoi(tmpVar);
      }
      else{
        std::cout << "Failed to process request, No UTCDateTime:Time:Minute found" << std::endl;
        outStr="{\"status\":\"ERROR\", \"reason\":\"No UTCDateTime:Time:Minute found\"}";
        goto sendResponse;
      }
      if(d1[CMD_PARAMS]["UTCDateTime"]["Time"].HasMember("Second")) {
        tmpVar=std::string(d1[CMD_PARAMS]["UTCDateTime"]["Time"]["Second"].GetString());
        SetSystemDateAndTime->UTCDateTime->Time->Second=std::stoi(tmpVar);
      }
      else{
        std::cout << "Failed to process request, No UTCDateTime:Time:Second found" << std::endl;
        outStr="{\"status\":\"ERROR\", \"reason\":\"No UTCDateTime:Time:Second found\"}";
        goto sendResponse;
      }
    }
    else{
      std::cout << "Failed to process request, No UTCDateTime:Time found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No UTCDateTime:Time found\"}";
      goto sendResponse;
    }
  }

//End prepare request

  faultStr="";
  if(false == sendSetSystemDateAndTime(&proxyDevice, SetSystemDateAndTime,
                                                SetSystemDateAndTimeResponse)) {
    if(verbosity>2)std::cout <<  "sendSetSystemDateAndTime failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendSetSystemDateAndTime failed all attempts\", \"message\":\""
      +faultStr+"\"}";
    goto cleanSendResponse;
  }


cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execAddPTZConfiguration(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;

  _trt__AddPTZConfiguration * AddPTZConfiguration;
  _trt__AddPTZConfigurationResponse * AddPTZConfigurationResponse;

  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("ConfigurationToken")) {
      std::cout << "Failed to process request, No ConfigurationToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ConfigurationToken found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyMedia.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyMedia.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyMedia.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  AddPTZConfiguration = soap_new__trt__AddPTZConfiguration(glSoap, -1);
  AddPTZConfigurationResponse = soap_new__trt__AddPTZConfigurationResponse(glSoap, -1);
//Prepare request
  AddPTZConfiguration->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());
  AddPTZConfiguration->ConfigurationToken=std::string(d1[CMD_PARAMS]["ConfigurationToken"].GetString());
//End Prepare request

  faultStr="";
  if(false == sendAddPTZConfiguration(&proxyMedia, AddPTZConfiguration, AddPTZConfigurationResponse)) {
    if(verbosity>2)std::cout <<  "sendAddPTZConfiguration failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendAddPTZConfiguration failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execGetPresetTourOptions(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar, PresetTourToken;

  _tptz__GetPresetTourOptions * GetPresetTourOptions;
  _tptz__GetPresetTourOptionsResponse * GetPresetTourOptionsResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetPresetTourOptions = soap_new__tptz__GetPresetTourOptions(glSoap, -1);
  GetPresetTourOptionsResponse = soap_new__tptz__GetPresetTourOptionsResponse(glSoap, -1);
//Prepare request
  GetPresetTourOptions->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());
  if(d1[CMD_PARAMS].HasMember("PresetTourToken")) {
    PresetTourToken=std::string(d1[CMD_PARAMS]["PresetTourToken"].GetString());
    GetPresetTourOptions->PresetTourToken=&PresetTourToken;
  }
//End Prepare request

  faultStr="";
  if(false == sendGetPresetTourOptions(&proxyPTZ, GetPresetTourOptions, GetPresetTourOptionsResponse)) {
    if(verbosity>2)std::cout <<  "sendGetPresetTourOptions failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetPresetTourOptions failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{";
  if(GetPresetTourOptionsResponse->Options!=NULL) {
    outStr+="\"Options\":{";
    tt__PTZPresetTourOptions* tmpConfig=GetPresetTourOptionsResponse->Options;
    if(tmpConfig->StartingCondition!=NULL) {
      outStr+="\"StartingCondition\":{";
      if(tmpConfig->StartingCondition->RecurringDuration!=NULL) {
        outStr+="\"RecurringDuration\":{";
        outStr+="\"Max\":\""+std::to_string(tmpConfig->StartingCondition->RecurringDuration->Max)+"\", ";
        outStr+="\"Min\":\""+std::to_string(tmpConfig->StartingCondition->RecurringDuration->Min)+"\"";
        outStr+="}, ";
      }
      if(tmpConfig->StartingCondition->RecurringTime!=NULL) {
        outStr+="\"RecurringTime\":{";
        outStr+="\"Max\":\""+std::to_string(tmpConfig->StartingCondition->RecurringTime->Max)+"\", ";
        outStr+="\"Min\":\""+std::to_string(tmpConfig->StartingCondition->RecurringTime->Min)+"\"";
        outStr+="}, ";
      }
      if(tmpConfig->StartingCondition->Direction.size()>0) {
        outStr+="\"Direction\":[";
        for (unsigned j=0; j<tmpConfig->StartingCondition->Direction.size(); j++) {
          if(j>0)outStr+=", ";
          if(tmpConfig->StartingCondition->Direction[j]==tt__PTZPresetTourDirection__Forward)
            outStr+="\"Forward\"";
          else if(tmpConfig->StartingCondition->Direction[j]==tt__PTZPresetTourDirection__Backward)
            outStr+="\"Backward\"";
          else if(tmpConfig->StartingCondition->Direction[j]==tt__PTZPresetTourDirection__Extended)
            outStr+="\"Extended\"";
        }
        outStr+="]";
      }
      if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
      outStr+="}, ";
    }
    if(tmpConfig->TourSpot!=NULL) {
      outStr+="\"TourSpot\":{";
      if(tmpConfig->TourSpot->StayTime!=NULL) {
        outStr+="\"StayTime\":{";
        outStr+="\"Max\":\""+std::to_string(tmpConfig->TourSpot->StayTime->Max)+"\", ";
        outStr+="\"Min\":\""+std::to_string(tmpConfig->TourSpot->StayTime->Min)+"\"";
        outStr+="}, ";
      }
      if(tmpConfig->TourSpot->PresetDetail!=NULL) {
        outStr+="\"PresetDetail\":{";
        if(tmpConfig->TourSpot->PresetDetail->Home!=NULL) {
          if(*(tmpConfig->TourSpot->PresetDetail->Home))
            outStr+="\"Home\":\"true\", ";
          else outStr+="\"Home\":\"false\", ";
        }
        if(tmpConfig->TourSpot->PresetDetail->PanTiltPositionSpace!=NULL) {
          outStr+="\"PanTiltPositionSpace\":{";
          if(tmpConfig->TourSpot->PresetDetail->PanTiltPositionSpace->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+
                    std::to_string(tmpConfig->TourSpot->PresetDetail->PanTiltPositionSpace->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+
                    std::to_string(tmpConfig->TourSpot->PresetDetail->PanTiltPositionSpace->XRange->Max)+"\"";
            outStr+="}, ";
          }
          if(tmpConfig->TourSpot->PresetDetail->PanTiltPositionSpace->YRange!=NULL) {
            outStr+="\"YRange\":{";
            outStr+="\"Min\":\""+
                    std::to_string(tmpConfig->TourSpot->PresetDetail->PanTiltPositionSpace->YRange->Min)+"\", ";
            outStr+="\"Max\":\""+
                    std::to_string(tmpConfig->TourSpot->PresetDetail->PanTiltPositionSpace->YRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpConfig->TourSpot->PresetDetail->PanTiltPositionSpace->URI +"\"";
          outStr+="}, ";
        }
        if(tmpConfig->TourSpot->PresetDetail->ZoomPositionSpace!=NULL) {
          outStr+="\"ZoomPositionSpace\":{";
          if(tmpConfig->TourSpot->PresetDetail->ZoomPositionSpace->XRange!=NULL) {
            outStr+="\"XRange\":{";
            outStr+="\"Min\":\""+
                    std::to_string(tmpConfig->TourSpot->PresetDetail->ZoomPositionSpace->XRange->Min)+"\", ";
            outStr+="\"Max\":\""+
                    std::to_string(tmpConfig->TourSpot->PresetDetail->ZoomPositionSpace->XRange->Max)+"\"";
            outStr+="}, ";
          }
          outStr+="\"URI\":\"" + tmpConfig->TourSpot->PresetDetail->ZoomPositionSpace->URI +"\"";
          outStr+="}, ";
        }
        if(tmpConfig->TourSpot->PresetDetail->PresetToken.size()>0) {
          outStr+="\"PresetToken\":[";
          for (unsigned j=0; j<tmpConfig->TourSpot->PresetDetail->PresetToken.size(); j++) {
            if(j>0)outStr+=", ";
            outStr+="\""+tmpConfig->TourSpot->PresetDetail->PresetToken[j]+"\"";
          }
          outStr+="]";
        }
        if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
        outStr+="}";
      }
      if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
      outStr+="}, ";
    }
    if(tmpConfig->AutoStart==true)
      outStr+="\"AutoStart\":\"true\"";
    else  outStr+="\"AutoStart\":\"false\"";
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}";
  }
  outStr+="}}";
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execModifyPresetTour(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar, token, tName;

  int64_t RecurringDuration;
  int RecurringTime;
  bool RandomPresetOrder;
  enum tt__PTZPresetTourDirection     Direction;

  _tptz__ModifyPresetTour * ModifyPresetTour;
  _tptz__ModifyPresetTourResponse * ModifyPresetTourResponse;


  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("PresetTour")) {
      std::cout << "Failed to process request, No PresetTour found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No PresetTour found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS].HasMember("ProfileToken")) {
      std::cout << "Failed to process request, No ProfileToken found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No ProfileToken found\"}";
      goto sendResponse;
    }
    if(!d1[CMD_PARAMS]["PresetTour"].HasMember("token")) {
      std::cout << "Failed to process request, No PresetTour:token found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No PresetTour:token found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyPTZ.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyPTZ.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyPTZ.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyPTZ.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  ModifyPresetTour = soap_new__tptz__ModifyPresetTour(glSoap, -1);
  ModifyPresetTourResponse = soap_new__tptz__ModifyPresetTourResponse(glSoap, -1);
  ModifyPresetTour->PresetTour=soap_new_tt__PresetTour(glSoap, -1);

//Prepare request
  token=std::string(d1[CMD_PARAMS]["PresetTour"]["token"].GetString());
  ModifyPresetTour->PresetTour->token=&token;
  ModifyPresetTour->ProfileToken=std::string(d1[CMD_PARAMS]["ProfileToken"].GetString());
  if(d1[CMD_PARAMS]["PresetTour"].HasMember("Name")) {
    tName=std::string(d1[CMD_PARAMS]["PresetTour"]["Name"].GetString());
    ModifyPresetTour->PresetTour->Name=&tName;
  }
  if(d1[CMD_PARAMS]["PresetTour"].HasMember("AutoStart")) {
    tmpVar=std::string(d1[CMD_PARAMS]["PresetTour"]["AutoStart"].GetString());
    if(tmpVar=="true") ModifyPresetTour->PresetTour->AutoStart=true;
    else ModifyPresetTour->PresetTour->AutoStart=false;
  }

  if(d1[CMD_PARAMS]["PresetTour"].HasMember("StartingCondition")) {
    ModifyPresetTour->PresetTour->StartingCondition=soap_new_tt__PTZPresetTourStartingCondition(glSoap, -1);
    if(d1[CMD_PARAMS]["PresetTour"]["StartingCondition"].HasMember("RecurringDuration")) {
      tmpVar=std::string(d1[CMD_PARAMS]["PresetTour"]["StartingCondition"]["RecurringDuration"].GetString());
      RecurringDuration=std::stoll(tmpVar);
      ModifyPresetTour->PresetTour->StartingCondition->RecurringDuration=&RecurringDuration;
    }
    if(d1[CMD_PARAMS]["PresetTour"]["StartingCondition"].HasMember("RecurringTime")) {
      tmpVar=std::string(d1[CMD_PARAMS]["PresetTour"]["StartingCondition"]["RecurringTime"].GetString());
      RecurringTime=std::stoi(tmpVar);
      ModifyPresetTour->PresetTour->StartingCondition->RecurringTime=&RecurringTime;
    }
    if(d1[CMD_PARAMS]["PresetTour"]["StartingCondition"].HasMember("RandomPresetOrder")) {
      tmpVar=std::string(d1[CMD_PARAMS]["PresetTour"]["StartingCondition"]["RandomPresetOrder"].GetString());
      if(tmpVar=="true") RandomPresetOrder=true;
      else RandomPresetOrder=false;
      ModifyPresetTour->PresetTour->StartingCondition->RandomPresetOrder=&RandomPresetOrder;
    }
    if(d1[CMD_PARAMS]["PresetTour"]["StartingCondition"].HasMember("Direction")) {
      tmpVar=std::string(d1[CMD_PARAMS]["PresetTour"]["StartingCondition"]["Direction"].GetString());
      if(tmpVar=="Forward") Direction=tt__PTZPresetTourDirection__Forward;
      else if(tmpVar=="Backward") Direction=tt__PTZPresetTourDirection__Backward;
      else if(tmpVar=="Extended") Direction=tt__PTZPresetTourDirection__Extended;
      ModifyPresetTour->PresetTour->StartingCondition->Direction=&Direction;
    }
  }
  if((d1[CMD_PARAMS]["PresetTour"].HasMember("TourSpot")) && (d1[CMD_PARAMS]["PresetTour"]["TourSpot"].IsArray())) {
    for (Value::ConstValueIterator itr = d1[CMD_PARAMS]["PresetTour"]["TourSpot"].Begin();
         itr != d1[CMD_PARAMS]["PresetTour"]["TourSpot"].End(); ++itr) {
      tt__PTZPresetTourSpot* tmpTourSpot=soap_new_tt__PTZPresetTourSpot(glSoap, -1);
      if((*itr).HasMember("StayTime")) {
        tmpTourSpot->StayTime=new int64_t;
        tmpVar=std::string((*itr)["StayTime"].GetString());
        *(tmpTourSpot->StayTime)=std::stoll(tmpVar);
      }
      if((*itr).HasMember("Speed")) {
        tmpTourSpot->Speed=soap_new_tt__PTZSpeed(glSoap, -1);
        if((*itr)["Speed"].HasMember("Zoom")) {
          tmpTourSpot->Speed->Zoom=soap_new_tt__Vector1D(glSoap, -1);
          if((*itr)["Speed"]["Zoom"].HasMember("x")) {
            tmpVar=std::string((*itr)["Speed"]["Zoom"]["x"].GetString());
            tmpTourSpot->Speed->Zoom->x=std::stof(tmpVar);
          }
        }
        if((*itr)["Speed"].HasMember("PanTilt")) {
          tmpTourSpot->Speed->PanTilt=soap_new_tt__Vector2D(glSoap, -1);
          if((*itr)["Speed"]["PanTilt"].HasMember("x")) {
            tmpVar=std::string((*itr)["Speed"]["PanTilt"]["x"].GetString());
            tmpTourSpot->Speed->PanTilt->x=std::stof(tmpVar);
          }
          if((*itr)["Speed"]["PanTilt"].HasMember("y")) {
            tmpVar=std::string((*itr)["Speed"]["PanTilt"]["y"].GetString());
            tmpTourSpot->Speed->PanTilt->y=std::stof(tmpVar);
          }
        }
      }
      if((*itr).HasMember("PresetDetail")) {
        tmpTourSpot->PresetDetail=soap_new_tt__PTZPresetTourPresetDetail(glSoap, -1);
        if((*itr)["PresetDetail"].HasMember("Home")) {
          tmpTourSpot->PresetDetail->__union_PTZPresetTourPresetDetail=
            SOAP_UNION__tt__union_PTZPresetTourPresetDetail_Home;
          tmpVar=std::string((*itr)["PresetDetail"]["Home"].GetString());
          if(tmpVar=="true") tmpTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.Home=true;
          else tmpTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.Home=false;
        } else if((*itr)["PresetDetail"].HasMember("PresetToken")) {
          tmpTourSpot->PresetDetail->__union_PTZPresetTourPresetDetail=
            SOAP_UNION__tt__union_PTZPresetTourPresetDetail_PresetToken;
          tmpVar=std::string((*itr)["PresetDetail"]["PresetToken"].GetString());
          tmpTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.PresetToken = new std::string();
          *(tmpTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.PresetToken)=tmpVar;
        } else if((*itr)["PresetDetail"].HasMember("PTZPosition")) {
          tmpTourSpot->PresetDetail->__union_PTZPresetTourPresetDetail=
            SOAP_UNION__tt__union_PTZPresetTourPresetDetail_PTZPosition;
          tmpTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.PTZPosition =
            soap_new_tt__PTZVector(glSoap, -1);
          if((*itr)["PresetDetail"]["PTZPosition"].HasMember("Zoom")) {
            tmpTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.PTZPosition->Zoom=
              soap_new_tt__Vector1D(glSoap, -1);
            if((*itr)["PresetDetail"]["PTZPosition"]["Zoom"].HasMember("x")) {
              tmpVar=std::string((*itr)["PresetDetail"]["PTZPosition"]["Zoom"]["x"].GetString());
              tmpTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.PTZPosition->Zoom->x=std::stof(tmpVar);
            }
          }
          if((*itr)["PresetDetail"]["PTZPosition"].HasMember("PanTilt")) {
            tmpTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.PTZPosition->PanTilt=
              soap_new_tt__Vector2D(glSoap, -1);
            if((*itr)["PresetDetail"]["PTZPosition"]["PanTilt"].HasMember("x")) {
              tmpVar=std::string((*itr)["PresetDetail"]["PTZPosition"]["PanTilt"]["x"].GetString());
              tmpTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.PTZPosition->PanTilt->x=std::stof(tmpVar);
            }
            if((*itr)["PresetDetail"]["PTZPosition"]["PanTilt"].HasMember("y")) {
              tmpVar=std::string((*itr)["PresetDetail"]["PTZPosition"]["PanTilt"]["y"].GetString());
              tmpTourSpot->PresetDetail->union_PTZPresetTourPresetDetail.PTZPosition->PanTilt->y=std::stof(tmpVar);
            }
          }
        }
      }
      ModifyPresetTour->PresetTour->TourSpot.push_back(tmpTourSpot);
    }
  }



//End Prepare request

  faultStr="";
  if(false == sendModifyPresetTour(&proxyPTZ, ModifyPresetTour, ModifyPresetTourResponse)) {
    if(verbosity>2)std::cout <<  "sendModifyPresetTour failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendModifyPresetTour failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

//Process response
//End process response

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

void execMove(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr="{\"status\":\"OK\"}";
  std::string tmpVar;
  float aspeed, rspeed, cspeed;

  _timg__Move * GetMove;
  _timg__MoveResponse * GetMoveResponse;

  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("Focus")) {
      std::cout << "Failed to process request, No Focus found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No Focus found\"}";
      goto sendResponse;
    }
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyImaging.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyImaging.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyImaging.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyImaging.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyImaging.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetMove = soap_new__timg__Move(glSoap, -1);
  GetMoveResponse = soap_new__timg__MoveResponse(glSoap, -1);
  GetMove->VideoSourceToken=videoSources[0];
  GetMove->Focus=soap_new_tt__FocusMove(glSoap, -1);

//Prepare request
  if(d1[CMD_PARAMS]["Focus"].HasMember("Absolute")) {
    GetMove->Focus->Absolute=soap_new_tt__AbsoluteFocus(glSoap, -1);
    if(d1[CMD_PARAMS]["Focus"]["Absolute"].HasMember("Speed")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Focus"]["Absolute"]["Speed"].GetString());
      aspeed=std::stof(tmpVar);
      GetMove->Focus->Absolute->Speed=&aspeed;
    }
    if(d1[CMD_PARAMS]["Focus"]["Absolute"].HasMember("Position")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Focus"]["Absolute"]["Position"].GetString());
      GetMove->Focus->Absolute->Position=std::stof(tmpVar);
    }
  }
  if(d1[CMD_PARAMS]["Focus"].HasMember("Relative")) {
    GetMove->Focus->Relative=soap_new_tt__RelativeFocus(glSoap, -1);
    if(d1[CMD_PARAMS]["Focus"]["Relative"].HasMember("Speed")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Focus"]["Relative"]["Speed"].GetString());
      rspeed=std::stof(tmpVar);
      GetMove->Focus->Relative->Speed=&rspeed;
    }
    if(d1[CMD_PARAMS]["Focus"]["Relative"].HasMember("Distance")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Focus"]["Relative"]["Distance"].GetString());
      GetMove->Focus->Relative->Distance=std::stof(tmpVar);
    }
  }
  if(d1[CMD_PARAMS]["Focus"].HasMember("Continuous")) {
    GetMove->Focus->Continuous=soap_new_tt__ContinuousFocus(glSoap, -1);
    if(d1[CMD_PARAMS]["Focus"]["Continuous"].HasMember("Speed")) {
      tmpVar=std::string(d1[CMD_PARAMS]["Focus"]["Continuous"]["Speed"].GetString());
      GetMove->Focus->Continuous->Speed=std::stof(tmpVar);
    }
  }



//End Prepare request

  faultStr="";
  if(false == sendMove(&proxyImaging, GetMove, GetMoveResponse)) {
    if(verbosity>2)std::cout <<  "sendMove failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendMove failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);

}

void execGenerateRecoverVideoConfig(int fd, rapidjson::Document &d1, uint32_t messageID) {
  std::string outStr;
  std::string tmpVar;
  std::string onvifcliPath;
  std::string destinationScriptPath;
  ofstream recoverScript;

  _trt__GetProfiles * GetProfiles;
  _trt__GetProfilesResponse * GetProfilesResponse;

  if (d1.HasMember(CMD_PARAMS)) {
    if(!d1[CMD_PARAMS].HasMember("onvifcliPath")) {
      std::cout << "Failed to process request, No onvifcliPath found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No onvifcliPath found\"}";
      goto sendResponse;
    }
    else onvifcliPath=std::string(d1[CMD_PARAMS]["onvifcliPath"].GetString());
    if(!d1[CMD_PARAMS].HasMember("destinationScriptPath")) {
      std::cout << "Failed to process request, No destinationScriptPath found" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"No destinationScriptPath found\"}";
      goto sendResponse;
    }
    else destinationScriptPath=std::string(d1[CMD_PARAMS]["destinationScriptPath"].GetString());
  } else {
    std::cout << "Failed to process request, No parameters found" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"No parameters found\"}";
    goto sendResponse;
  }


  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyMedia.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to assign user:password\"}";
    goto sendResponse;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyMedia.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"Failed to set a timestamp\"}";
    goto sendResponse;
  }

  proxyMedia.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  GetProfiles = soap_new__trt__GetProfiles(glSoap, -1);
  GetProfilesResponse = soap_new__trt__GetProfilesResponse(glSoap, -1);
//Prepare request
//End Prepare request

  faultStr="";
  if(false == sendGetProfiles(&proxyMedia, GetProfiles, GetProfilesResponse)) {
    if(verbosity>2)std::cout <<  "sendGetProfiles failed all attempts" << std::endl;
    outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetProfiles failed all attempts\", \"message\":\""+faultStr+"\"}";
    goto cleanSendResponse;
  }

  recoverScript.open (destinationScriptPath.c_str());
//Process response
  outStr="{\"status\":\"OK\", \"parameters\":{";
  if(verbosity>3) std::cout << "Number of Profiles received is "
                              << GetProfilesResponse->Profiles.size() << std::endl;
  if(GetProfilesResponse->Profiles.size()>0) {
    outStr+="\"Profiles\":[";
    for (unsigned i=0; i<GetProfilesResponse->Profiles.size(); i++) {
      if(i>0)outStr+=", ";
      outStr+="{";
      tt__Profile* tmpConfig=GetProfilesResponse->Profiles[i];
      if(tmpConfig->PTZConfiguration!=NULL) {
        tt__PTZConfiguration* tmpPTZConfig=tmpConfig->PTZConfiguration;
        outStr+="\"PTZConfiguration\":{";
        if(tmpPTZConfig->DefaultPTZSpeed!=NULL) {
          outStr+="\"DefaultPTZSpeed\":{";
          if(tmpPTZConfig->DefaultPTZSpeed->PanTilt!=NULL) {
            outStr+="\"PanTilt\":{";
            outStr+="\"x\":\""+std::to_string(tmpPTZConfig->DefaultPTZSpeed->PanTilt->x)+"\", ";
            outStr+="\"y\":\""+std::to_string(tmpPTZConfig->DefaultPTZSpeed->PanTilt->y)+"\"";
            outStr+="}, ";
          }
          if(tmpPTZConfig->DefaultPTZSpeed->Zoom!=NULL)
            outStr+="\"Zoom\":{\"x\":\""+std::to_string(tmpPTZConfig->DefaultPTZSpeed->Zoom->x)+"\"}";
          if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
          outStr+="}, ";
        }
        if(tmpPTZConfig->PanTiltLimits!=NULL) {
          outStr+="\"PanTiltLimits\":{";
          if(tmpPTZConfig->PanTiltLimits->Range!=NULL) {
            outStr+="\"Range\":{";
            if(tmpPTZConfig->PanTiltLimits->Range->XRange!=NULL) {
              outStr+="\"XRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpPTZConfig->PanTiltLimits->Range->XRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpPTZConfig->PanTiltLimits->Range->XRange->Max)+"\"";
              outStr+="}, ";
            }
            if(tmpPTZConfig->PanTiltLimits->Range->YRange!=NULL) {
              outStr+="\"YRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpPTZConfig->PanTiltLimits->Range->YRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpPTZConfig->PanTiltLimits->Range->YRange->Max)+"\"";
              outStr+="}, ";
            }
            outStr+="\"URI\":\"" + tmpPTZConfig->PanTiltLimits->Range->URI +"\"";
            outStr+="}";
          }
          outStr+="}, ";
        }
        if(tmpPTZConfig->ZoomLimits!=NULL) {
          outStr+="\"ZoomLimits\":{";
          if(tmpPTZConfig->ZoomLimits->Range!=NULL) {
            outStr+="\"Range\":{";
            if(tmpPTZConfig->ZoomLimits->Range->XRange!=NULL) {
              outStr+="\"XRange\":{";
              outStr+="\"Min\":\""+std::to_string(tmpPTZConfig->ZoomLimits->Range->XRange->Min)+"\", ";
              outStr+="\"Max\":\""+std::to_string(tmpPTZConfig->ZoomLimits->Range->XRange->Max)+"\"";
              outStr+="}, ";
            }
            outStr+="\"URI\":\"" + tmpPTZConfig->ZoomLimits->Range->URI +"\"";
            outStr+="}";
          }
          outStr+="}, ";
        }
        if(tmpPTZConfig->DefaultAbsolutePantTiltPositionSpace!=NULL)
          outStr+="\"DefaultAbsolutePantTiltPositionSpace\":\""+
                  (*(tmpPTZConfig->DefaultAbsolutePantTiltPositionSpace))+"\", ";
        if(tmpPTZConfig->DefaultAbsoluteZoomPositionSpace!=NULL)
          outStr+="\"DefaultAbsoluteZoomPositionSpace\":\""+
                  (*(tmpPTZConfig->DefaultAbsoluteZoomPositionSpace))+"\", ";
        if(tmpPTZConfig->DefaultContinuousPanTiltVelocitySpace!=NULL)
          outStr+="\"DefaultContinuousPanTiltVelocitySpace\":\""+
                  (*(tmpPTZConfig->DefaultContinuousPanTiltVelocitySpace))+"\", ";
        if(tmpPTZConfig->DefaultContinuousZoomVelocitySpace!=NULL)
          outStr+="\"DefaultContinuousZoomVelocitySpace\":\""+
                  (*(tmpPTZConfig->DefaultContinuousZoomVelocitySpace))+"\", ";
        if(tmpPTZConfig->DefaultRelativePanTiltTranslationSpace!=NULL)
          outStr+="\"DefaultRelativePanTiltTranslationSpace\":\""+
                  (*(tmpPTZConfig->DefaultRelativePanTiltTranslationSpace))+"\", ";
        if(tmpPTZConfig->DefaultRelativeZoomTranslationSpace!=NULL)
          outStr+="\"DefaultRelativeZoomTranslationSpace\":\""+
                  (*(tmpPTZConfig->DefaultRelativeZoomTranslationSpace))+"\", ";
        if(tmpPTZConfig->DefaultPTZTimeout!=NULL)
          outStr+="\"DefaultPTZTimeout\":\""+std::to_string(*(tmpPTZConfig->DefaultPTZTimeout))+"\", ";
        if(tmpPTZConfig->PresetTourRamp!=NULL)
          outStr+="\"PresetTourRamp\":\""+std::to_string(*(tmpPTZConfig->PresetTourRamp))+"\", ";
        if(tmpPTZConfig->PresetRamp!=NULL)
          outStr+="\"PresetRamp\":\""+std::to_string(*(tmpPTZConfig->PresetRamp))+"\", ";
        if(tmpPTZConfig->MoveRamp!=NULL)
          outStr+="\"MoveRamp\":\""+std::to_string(*(tmpPTZConfig->MoveRamp))+"\", ";
        outStr+="\"UseCount\":\""+std::to_string(tmpPTZConfig->UseCount)+"\", ";
        outStr+="\"Name\":\""+tmpPTZConfig->Name+"\", ";
        outStr+="\"NodeToken\":\""+tmpPTZConfig->NodeToken+"\", ";
        outStr+="\"token\":\""+tmpPTZConfig->token+"\"";
        outStr+="}, ";
      }
      if(tmpConfig->VideoEncoderConfiguration!=NULL) {
        recoverScript << "\n" << onvifcliPath << " " << listenIP << " " << listenPort << " \'";
        recoverScript << "{\"command\":\"SetVideoEncoderConfiguration\", \"parameters\":{\"ForcePersistence\":\"true\", ";
        recoverScript << "\"Configuration\":{";
        outStr+="\"VideoEncoderConfiguration\":{";
        if(tmpConfig->VideoEncoderConfiguration->H264!=NULL) {
          outStr+="\"H264\":{";
          recoverScript << "\"H264\":{";
          if(tmpConfig->VideoEncoderConfiguration->H264->H264Profile==tt__H264Profile__Baseline){
            outStr+="\"H264Profile\":\"Baseline\", ";
            recoverScript << "\"H264Profile\":\"Baseline\", ";
          }

          else if(tmpConfig->VideoEncoderConfiguration->H264->H264Profile==tt__H264Profile__Main){
            outStr+="\"H264Profile\":\"Main\", ";
            recoverScript << "\"H264Profile\":\"Main\", ";
          }

          else if(tmpConfig->VideoEncoderConfiguration->H264->H264Profile==tt__H264Profile__Extended){
            outStr+="\"H264Profile\":\"Extended\", ";
            recoverScript << "\"H264Profile\":\"Extended\", ";
          }

          else if(tmpConfig->VideoEncoderConfiguration->H264->H264Profile==tt__H264Profile__High){
            outStr+="\"H264Profile\":\"High\", ";
            recoverScript << "\"H264Profile\":\"High\", ";
          }

          outStr+="\"GovLength\":\""+std::to_string(tmpConfig->VideoEncoderConfiguration->H264->GovLength)+"\"";
          outStr+="}, ";
          recoverScript << "\"GovLength\":\""<<std::to_string(tmpConfig->VideoEncoderConfiguration->H264->GovLength);
          recoverScript << "\"}, ";
        }
        if(tmpConfig->VideoEncoderConfiguration->RateControl!=NULL) {
          outStr+="\"RateControl\":{";
          recoverScript << "\"RateControl\":{";
          outStr+="\"BitrateLimit\":\""+
                std::to_string(tmpConfig->VideoEncoderConfiguration->RateControl->BitrateLimit)+"\", ";
          recoverScript << "\"BitrateLimit\":\"";
          recoverScript << std::to_string(tmpConfig->VideoEncoderConfiguration->RateControl->BitrateLimit);
          recoverScript << "\", ";
          outStr+="\"EncodingInterval\":\""+
                std::to_string(tmpConfig->VideoEncoderConfiguration->RateControl->EncodingInterval)+"\", ";
          recoverScript << "\"EncodingInterval\":\"";
          recoverScript << std::to_string(tmpConfig->VideoEncoderConfiguration->RateControl->EncodingInterval);
          recoverScript << "\", ";
          outStr+="\"FrameRateLimit\":\""+
                std::to_string(tmpConfig->VideoEncoderConfiguration->RateControl->FrameRateLimit)+"\"";
          outStr+="}, ";
          recoverScript << "\"FrameRateLimit\":\"";
          recoverScript << std::to_string(tmpConfig->VideoEncoderConfiguration->RateControl->FrameRateLimit);
          recoverScript << "\"}, ";
        }
        if(tmpConfig->VideoEncoderConfiguration->Resolution!=NULL) {
          outStr+="\"Resolution\":{";
          recoverScript << "\"Resolution\":{";
          outStr+="\"Height\":\""+std::to_string(tmpConfig->VideoEncoderConfiguration->Resolution->Height)+"\", ";
          recoverScript << "\"Height\":\"";
          recoverScript << std::to_string(tmpConfig->VideoEncoderConfiguration->Resolution->Height);
          recoverScript << "\", ";
          outStr+="\"Width\":\""+std::to_string(tmpConfig->VideoEncoderConfiguration->Resolution->Width)+"\"";
          recoverScript << "\"Width\":\"";
          recoverScript << std::to_string(tmpConfig->VideoEncoderConfiguration->Resolution->Width);
          recoverScript << "\"}, ";
          outStr+="}, ";
        }
        if(tmpConfig->VideoEncoderConfiguration->MPEG4!=NULL) {
          outStr+="\"MPEG4\":{";
          recoverScript << "\"MPEG4\":{";
          if(tmpConfig->VideoEncoderConfiguration->MPEG4->Mpeg4Profile==tt__Mpeg4Profile__SP){
            outStr+="\"Mpeg4Profile\":\"SP\", ";
            recoverScript << "\"Mpeg4Profile\":\"SP\", ";
          }
          else if(tmpConfig->VideoEncoderConfiguration->MPEG4->Mpeg4Profile==tt__Mpeg4Profile__ASP){
            outStr+="\"Mpeg4Profile\":\"ASP\", ";
            recoverScript << "\"Mpeg4Profile\":\"ASP\", ";
          }
          outStr+="\"GovLength\":\""+std::to_string(tmpConfig->VideoEncoderConfiguration->MPEG4->GovLength)+"\"";
          recoverScript << "\"GovLength\":\"";
          recoverScript << std::to_string(tmpConfig->VideoEncoderConfiguration->MPEG4->GovLength);
          recoverScript << "\"}, ";
          outStr+="}, ";
        }
        if(tmpConfig->VideoEncoderConfiguration->Encoding==tt__VideoEncoding__H264){
          outStr+="\"Encoding\":\"H264\", ";
          recoverScript << "\"Encoding\":\"H264\", ";
        }
        else if(tmpConfig->VideoEncoderConfiguration->Encoding==tt__VideoEncoding__MPEG4){
          outStr+="\"Encoding\":\"MPEG4\", ";
          recoverScript << "\"Encoding\":\"MPEG4\", ";
        }
        else if(tmpConfig->VideoEncoderConfiguration->Encoding==tt__VideoEncoding__JPEG){
          outStr+="\"Encoding\":\"JPEG\", ";
          recoverScript << "\"Encoding\":\"JPEG\", ";
        }
        outStr+="\"UseCount\":\""+std::to_string(tmpConfig->VideoEncoderConfiguration->UseCount)+"\", ";
        recoverScript << "\"UseCount\":\"";
        recoverScript << std::to_string(tmpConfig->VideoEncoderConfiguration->UseCount);
        recoverScript << "\", ";
        outStr+="\"SessionTimeout\":\""+std::to_string(tmpConfig->VideoEncoderConfiguration->SessionTimeout)+"\", ";
        recoverScript << "\"SessionTimeout\":\"";
        recoverScript << std::to_string(tmpConfig->VideoEncoderConfiguration->SessionTimeout);
        recoverScript << "\", ";
        outStr+="\"Quality\":\""+std::to_string(tmpConfig->VideoEncoderConfiguration->Quality)+"\", ";
        recoverScript << "\"Quality\":\"";
        recoverScript << std::to_string(tmpConfig->VideoEncoderConfiguration->Quality);
        recoverScript << "\", ";
        outStr+="\"Name\":\""+tmpConfig->VideoEncoderConfiguration->Name+"\", ";
        recoverScript << "\"Name\":\"";
        recoverScript << tmpConfig->VideoEncoderConfiguration->Name;
        recoverScript << "\", ";
        outStr+="\"token\":\""+tmpConfig->VideoEncoderConfiguration->token+"\"";
        recoverScript << "\"token\":\"";
        recoverScript << tmpConfig->VideoEncoderConfiguration->token;
        recoverScript << "\"}}}\'\n";
        outStr+="}, ";
      }
      if(tmpConfig->VideoSourceConfiguration!=NULL){
        outStr+="\"VideoSourceConfiguration\":{";
        outStr+="\"token\":\""+tmpConfig->VideoSourceConfiguration->token+"\"";
        outStr+="}, ";
      }
      outStr+="\"Name\":\""+tmpConfig->Name+"\", ";
      outStr+="\"token\":\""+tmpConfig->token+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="]";
  }
  outStr+="}}";
//End process response
  recoverScript.close();

cleanSendResponse:

  soap_destroy(glSoap);
  soap_end(glSoap);

sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);
}

////////////////////////////Prepare calls:

std::string prepareOptionsResponse(_timg__GetOptionsResponse* optionsResponse) {
  std::string outStr="{\"status\":\"OK\", \"parameters\":{";
  if(optionsResponse->ImagingOptions->BacklightCompensation!=NULL) {
    outStr+="\"BacklightCompensation\":{";
    if(optionsResponse->ImagingOptions->BacklightCompensation->Mode.size()>0) {
      outStr+="\"Mode\":[\"ON\", \"OFF\"], ";
    }
    if(optionsResponse->ImagingOptions->BacklightCompensation->Level!=NULL) {
      outStr+="\"Level\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->BacklightCompensation->Level->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->BacklightCompensation->Level->Max)+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(optionsResponse->ImagingOptions->Brightness !=NULL) {
    outStr+="\"Brightness\":{";
    outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Brightness->Min)+"\", ";
    outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Brightness->Max)+"\"";
    outStr+="}, ";
  }
  if(optionsResponse->ImagingOptions->ColorSaturation !=NULL) {
    outStr+="\"ColorSaturation\":{";
    outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->ColorSaturation->Min)+"\", ";
    outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->ColorSaturation->Max)+"\"";
    outStr+="}, ";
  }
  if(optionsResponse->ImagingOptions->Contrast !=NULL) {
    outStr+="\"ColorSaturation\":{";
    outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Contrast->Min)+"\", ";
    outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Contrast->Max)+"\"";
    outStr+="}, ";
  }
  if(optionsResponse->ImagingOptions->Exposure !=NULL) {
    outStr+="\"Exposure\":{";
    if(optionsResponse->ImagingOptions->Exposure->Mode.size()>0) {
      outStr+="\"Mode\":[\"AUTO\", \"MANUAL\"], ";
    }
    if(optionsResponse->ImagingOptions->Exposure->Priority.size()>0) {
      outStr+="\"Mode\":[\"LowNoise\", \"FrameRate\"], ";
    }
    if(optionsResponse->ImagingOptions->Exposure->ExposureTime !=NULL) {
      outStr+="\"ExposureTime\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->ExposureTime->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->ExposureTime->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->Exposure->Gain !=NULL) {
      outStr+="\"Gain\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->Gain->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->Gain->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->Exposure->Iris !=NULL) {
      outStr+="\"Iris\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->Iris->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->Iris->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->Exposure->MaxExposureTime !=NULL) {
      outStr+="\"MaxExposureTime\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MaxExposureTime->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MaxExposureTime->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->Exposure->MaxGain !=NULL) {
      outStr+="\"MaxGain\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MaxGain->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MaxGain->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->Exposure->MaxIris !=NULL) {
      outStr+="\"MaxIris\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MaxIris->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MaxIris->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->Exposure->MinExposureTime !=NULL) {
      outStr+="\"MinExposureTime\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MinExposureTime->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MinExposureTime->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->Exposure->MinGain !=NULL) {
      outStr+="\"MinGain\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MinGain->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MinGain->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->Exposure->MinIris !=NULL) {
      outStr+="\"MinIris\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MinIris->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MinIris->Max)+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(optionsResponse->ImagingOptions->Sharpness !=NULL) {
    outStr+="\"Sharpness\":{";
    outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Sharpness->Min)+"\", ";
    outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Sharpness->Max)+"\"";
    outStr+="}, ";
  }
  if(optionsResponse->ImagingOptions->IrCutFilterModes.size() > 0) {
    outStr+="\"IrCutFilterModes\":[\"ON\", \"OFF\", \"AUTO\"], ";
  }
  if(optionsResponse->ImagingOptions->Focus != NULL) {
    outStr+="\"Focus\":{";
    if(optionsResponse->ImagingOptions->Focus->AutoFocusModes.size()>0) {
      outStr+="\"AutoFocusModes\":[\"AUTO\", \"MANUAL\"], ";
    }
    if(optionsResponse->ImagingOptions->Focus->DefaultSpeed !=NULL) {
      outStr+="\"DefaultSpeed\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Focus->DefaultSpeed->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Focus->DefaultSpeed->Max)+"\"";
      outStr+="}";
    }
    if(optionsResponse->ImagingOptions->Focus->FarLimit !=NULL) {
      outStr+="\"FarLimit\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Focus->FarLimit->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Focus->FarLimit->Max)+"\"";
      outStr+="}";
    }
    if(optionsResponse->ImagingOptions->Focus->NearLimit !=NULL) {
      outStr+="\"NearLimit\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Focus->NearLimit->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Focus->NearLimit->Max)+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(optionsResponse->ImagingOptions->WhiteBalance) {
    outStr+="\"WhiteBalance\":{";
    if(optionsResponse->ImagingOptions->WhiteBalance->Mode.size()>0) {
      outStr+="\"Mode\":[\"AUTO\", \"MANUAL\"], ";
    }
    if(optionsResponse->ImagingOptions->WhiteBalance->YbGain !=NULL) {
      outStr+="\"YbGain\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->WhiteBalance->YbGain->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->WhiteBalance->YbGain->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->WhiteBalance->YrGain !=NULL) {
      outStr+="\"YrGain\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->WhiteBalance->YrGain->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->WhiteBalance->YrGain->Max)+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(optionsResponse->ImagingOptions->WideDynamicRange) {
    outStr+="\"WideDynamicRange\":{";
    if(optionsResponse->ImagingOptions->WideDynamicRange->Mode.size()>0) {
      outStr+="\"Mode\":[\"ON\", \"OFF\"], ";
    }
    if(optionsResponse->ImagingOptions->WideDynamicRange->Level!=NULL) {
      outStr+="\"Level\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->WideDynamicRange->Level->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->WideDynamicRange->Level->Max)+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }

  if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
  outStr+="}}";
  return outStr;
}

std::string prepareOSDOptionsResponse(_trt__GetOSDOptionsResponse* OSDOptionsResponse) {
  std::string outStr="{\"status\":\"OK\", \"parameters\":{";
  // continue here
  if(OSDOptionsResponse->OSDOptions->TextOption!=NULL) {
    outStr+="\"TextOption\":{";
    if(OSDOptionsResponse->OSDOptions->TextOption->BackgroundColor!=NULL) {
      outStr+="\"BackgroundColor\":{";
      if(OSDOptionsResponse->OSDOptions->TextOption->BackgroundColor->Color!=NULL) {
        outStr+="\"Color\":{";

        if ((OSDOptionsResponse->OSDOptions->TextOption->BackgroundColor->Color->__union_ColorOptions ==
             SOAP_UNION__tt__union_ColorOptions_ColorList) &&
            (OSDOptionsResponse->OSDOptions->TextOption->BackgroundColor->Color->union_ColorOptions.ColorList!=NULL)) {
          outStr+="\"ColorList\":[";
          for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->TextOption->
              BackgroundColor->Color->union_ColorOptions.ColorList->size(); i++) {
            if(i!=0)outStr+=", ";
            tt__Color* tmpColor=(*OSDOptionsResponse->OSDOptions->TextOption->
                                 BackgroundColor->Color->union_ColorOptions.ColorList)[i];
            outStr+="{\"X\":\"" + std::to_string(tmpColor->X)+"\"";
            outStr+=", \"Y\":\"" + std::to_string(tmpColor->Y)+"\"";
            outStr+=", \"Z\":\"" + std::to_string(tmpColor->Z)+"\"";
            if(tmpColor->Colorspace!=NULL)outStr+=", \"Colorspace\":\"" + (*tmpColor->Colorspace) +"\"";
            outStr+="}";
          }
          outStr+="], ";
        }
        if ((OSDOptionsResponse->OSDOptions->TextOption->BackgroundColor->Color->__union_ColorOptions ==
             SOAP_UNION__tt__union_ColorOptions_ColorspaceRange) && (OSDOptionsResponse->OSDOptions->TextOption->
                 BackgroundColor->Color->union_ColorOptions.ColorspaceRange !=NULL)) {
          outStr+="\"ColorspaceRange\":[";
          for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->TextOption->
              BackgroundColor->Color->union_ColorOptions.ColorspaceRange->size(); i++) {
            if(i!=0)outStr+=", ";
            tt__ColorspaceRange* tmpColor=(*OSDOptionsResponse->OSDOptions->TextOption->
                                           BackgroundColor->Color->union_ColorOptions.ColorspaceRange)[i];
            outStr+="{\"X\":{\"Max\":\"" + std::to_string(tmpColor->X->Max)+"\", \"Min\":\"" +
                    std::to_string(tmpColor->X->Min)+"\"}";
            outStr+=", \"Y\":{\"Max\":\"" + std::to_string(tmpColor->Y->Max)+"\", \"Min\":\"" +
                    std::to_string(tmpColor->Y->Min)+"\"}";
            outStr+=", \"Z\":{\"Max\":\"" + std::to_string(tmpColor->Z->Max)+"\", \"Min\":\"" +
                    std::to_string(tmpColor->Z->Min)+"\"}";
            outStr+=", \"Colorspace\":\"" + (tmpColor->Colorspace) +"\"";
            outStr+="}";
          }
          outStr+="], ";
        }
        if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
        outStr+="}, ";
      }
      if(OSDOptionsResponse->OSDOptions->TextOption->BackgroundColor->Transparent!=NULL) {
        outStr+="\"Transparent\":{";
        outStr+="\"Min\":\""+
                std::to_string(OSDOptionsResponse->OSDOptions->TextOption->BackgroundColor->Transparent->Min)+"\", ";
        outStr+="\"Max\":\""+
                std::to_string(OSDOptionsResponse->OSDOptions->TextOption->BackgroundColor->Transparent->Max)+"\"";
        outStr+="}";
      }
      outStr+="}, ";
    }
    if(OSDOptionsResponse->OSDOptions->TextOption->DateFormat.size()>0) {
      outStr+="\"DateFormat\":[";
      for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->TextOption->DateFormat.size(); i++) {
        if(i!=0)outStr+=", ";
        outStr+="\""+OSDOptionsResponse->OSDOptions->TextOption->DateFormat[i]+"\"";
      }
      outStr+="], ";
    }
    if(OSDOptionsResponse->OSDOptions->TextOption->FontColor!=NULL) {
      outStr+="\"FontColor\":{";
      if(OSDOptionsResponse->OSDOptions->TextOption->FontColor->Color!=NULL) {
        outStr+="\"Color\":{";
        if ((OSDOptionsResponse->OSDOptions->TextOption->FontColor->Color->__union_ColorOptions ==
             SOAP_UNION__tt__union_ColorOptions_ColorList) &&
            (OSDOptionsResponse->OSDOptions->TextOption->FontColor->Color->union_ColorOptions.ColorList !=NULL)) {
          outStr+="\"ColorList\":[";
          for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->TextOption->
              FontColor->Color->union_ColorOptions.ColorList->size(); i++) {
            if(i!=0)outStr+=", ";
            tt__Color* tmpColor=(*OSDOptionsResponse->OSDOptions->TextOption->
                                 FontColor->Color->union_ColorOptions.ColorList)[i];
            outStr+="{\"X\":\"" + std::to_string(tmpColor->X)+"\"";
            outStr+=", \"Y\":\"" + std::to_string(tmpColor->Y)+"\"";
            outStr+=", \"Z\":\"" + std::to_string(tmpColor->Z)+"\"";
            if(tmpColor->Colorspace!=NULL)outStr+=", \"Colorspace\":\"" + (*tmpColor->Colorspace) +"\"";
            outStr+="}";
          }
          outStr+="], ";
        }
        if ((OSDOptionsResponse->OSDOptions->TextOption->FontColor->Color->__union_ColorOptions ==
             SOAP_UNION__tt__union_ColorOptions_ColorspaceRange) && (OSDOptionsResponse->OSDOptions->TextOption->
                 FontColor->Color->union_ColorOptions.ColorspaceRange !=NULL)) {
          outStr+="\"ColorspaceRange\":[";
          for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->TextOption->
              FontColor->Color->union_ColorOptions.ColorspaceRange->size(); i++) {
            if(i!=0)outStr+=", ";
            tt__ColorspaceRange* tmpColor=(*OSDOptionsResponse->OSDOptions->TextOption->
                                           FontColor->Color->union_ColorOptions.ColorspaceRange)[i];
            outStr+="{\"X\":{\"Max\":\"" + std::to_string(tmpColor->X->Max)+"\", \"Min\":\"" +
                    std::to_string(tmpColor->X->Min)+"\"}";
            outStr+=", \"Y\":{\"Max\":\"" + std::to_string(tmpColor->Y->Max)+"\", \"Min\":\"" +
                    std::to_string(tmpColor->Y->Min)+"\"}";
            outStr+=", \"Z\":{\"Max\":\"" + std::to_string(tmpColor->Z->Max)+"\", \"Min\":\"" +
                    std::to_string(tmpColor->Z->Min)+"\"}";
            outStr+=", \"Colorspace\":\"" + (tmpColor->Colorspace) +"\"";
            outStr+="}";
          }
          outStr+="], ";
        }
        if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
        outStr+="}, ";
      }
      if(OSDOptionsResponse->OSDOptions->TextOption->FontColor->Transparent!=NULL) {
        outStr+="\"Transparent\":{";
        outStr+="\"Min\":\""+
                std::to_string(OSDOptionsResponse->OSDOptions->TextOption->FontColor->Transparent->Min)+"\", ";
        outStr+="\"Max\":\""+
                std::to_string(OSDOptionsResponse->OSDOptions->TextOption->FontColor->Transparent->Max)+"\"";
        outStr+="}";
      }
      outStr+="}, ";
    }

    if(OSDOptionsResponse->OSDOptions->TextOption->FontSizeRange!=NULL) {
      outStr+="\"FontSizeRange\":{";
      outStr+="\"Min\":\""+
              std::to_string(OSDOptionsResponse->OSDOptions->TextOption->FontSizeRange->Min)+"\", ";
      outStr+="\"Max\":\""+
              std::to_string(OSDOptionsResponse->OSDOptions->TextOption->FontSizeRange->Max)+"\"";
      outStr+="}, ";
    }
    if(OSDOptionsResponse->OSDOptions->TextOption->TimeFormat.size()>0) {
      outStr+="\"TimeFormat\":[";
      for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->TextOption->TimeFormat.size(); i++) {
        if(i!=0)outStr+=", ";
        outStr+="\""+OSDOptionsResponse->OSDOptions->TextOption->TimeFormat[i]+"\"";
      }
      outStr+="], ";
    }
    if(OSDOptionsResponse->OSDOptions->TextOption->Type.size()>0) {
      outStr+="\"Type\":[";
      for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->TextOption->Type.size(); i++) {
        if(i!=0)outStr+=", ";
        outStr+="\""+OSDOptionsResponse->OSDOptions->TextOption->Type[i]+"\"";
      }
      outStr+="]";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(OSDOptionsResponse->OSDOptions->PositionOption.size()>0) {
    outStr+="\"PositionOption\":[";
    for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->PositionOption.size(); i++) {
      if(i!=0)outStr+=", ";
      outStr+="\""+OSDOptionsResponse->OSDOptions->PositionOption[i]+"\"";
    }
    outStr+="], ";
  }
  if(OSDOptionsResponse->OSDOptions->Type.size()>0) {
    outStr+="\"Type\":[";
    for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->Type.size(); i++) {
      if(i!=0)outStr+=", ";
      switch(OSDOptionsResponse->OSDOptions->Type[i]) {
      case tt__OSDType__Text:
        outStr+="\"Text\"";
        break;
      case tt__OSDType__Image:
        outStr+="\"Image\"";
        break;
      case tt__OSDType__Extended:
        outStr+="\"Extended\"";
        break;
      }
    }
    outStr+="], ";
  }
  if(OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs!=NULL) {
    outStr+="\"MaximumNumberOfOSDs\":{";
    if(OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->Date !=NULL) {
      outStr+="\"Date\":\""+ std::to_string(*OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->Date)+"\", ";
    }
    if(OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->DateAndTime !=NULL) {
      outStr+="\"DateAndTime\":\""+
              std::to_string(*OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->DateAndTime)+"\", ";
    }
    if(OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->Image !=NULL) {
      outStr+="\"Image\":\""+
              std::to_string(*OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->Image)+"\", ";
    }
    if(OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->PlainText !=NULL) {
      outStr+="\"PlainText\":\""+
              std::to_string(*OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->PlainText)+"\", ";
    }
    if(OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->Time !=NULL) {
      outStr+="\"Time\":\""+ std::to_string(*OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->Time)+"\", ";
    }
    outStr+="\"Total\":\""+ std::to_string(OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->Total)+"\"";
    outStr+="}, ";
  }
  if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
  outStr+="}}";
  return outStr;
}


std::string prepareMoveOptionsResponse(_timg__GetMoveOptionsResponse* moveOptionsResponse) {
  std::string outStr="{\"status\":\"OK\", \"parameters\":{";
  if(moveOptionsResponse->MoveOptions->Absolute!=NULL) {
    outStr+="\"Absolute\":{";
    if(moveOptionsResponse->MoveOptions->Absolute->Position !=NULL) {
      outStr+="\"Position\":{";
      outStr+="\"Min\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Absolute->Position->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Absolute->Position->Max)+"\"";
      outStr+="}, ";
    }
    if(moveOptionsResponse->MoveOptions->Absolute->Speed!=NULL) {
      outStr+="\"Speed\":{";
      outStr+="\"Min\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Absolute->Speed->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Absolute->Speed->Max)+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(moveOptionsResponse->MoveOptions->Relative!=NULL) {
    outStr+="\"Relative\":{";
    if(moveOptionsResponse->MoveOptions->Relative->Distance !=NULL) {
      outStr+="\"Distance\":{";
      outStr+="\"Min\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Relative->Distance->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Relative->Distance->Max)+"\"";
      outStr+="}, ";
    }
    if(moveOptionsResponse->MoveOptions->Relative->Speed!=NULL) {
      outStr+="\"Speed\":{";
      outStr+="\"Min\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Relative->Speed->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Relative->Speed->Max)+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(moveOptionsResponse->MoveOptions->Continuous!=NULL) {
    outStr+="\"Continuous\":{";
    if(moveOptionsResponse->MoveOptions->Continuous->Speed!=NULL) {
      outStr+="\"Speed\":{";
      outStr+="\"Min\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Continuous->Speed->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Continuous->Speed->Max)+"\"";
      outStr+="}";
    }
    outStr+="}";
  }
  if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
  outStr+="}}";
  return outStr;
}

///////////////////////Server functionality:

void usage() {
  std::cout << "Usage: " << progName << "\n"
            << "\t-l login \n"
            << "\t-i ip or host \n"
            << "\t-v output verbosity \n"
            << "\t-L listen ip \n"
            << "\t-P listen port \n"
            << "\t-m max idle time in connection (seconds) use 0 to disable\n"
            << "\t-p password \n";
  exit(1);
}

std::string timeToString(time_t convTime) {
  char the_date[MAX_DATE];
  the_date[0] = '\0';
  strftime(the_date, MAX_DATE, "%d%b%y-%X", gmtime(&convTime));
  return std::string(the_date);
}

std::string getTimeStamp() {
  time_t now;
  char the_date[MAX_DATE];
  the_date[0] = '\0';
  now = time(NULL);
  if (now != -1) {
    strftime(the_date, MAX_DATE, "%d%b%y-%X", gmtime(&now));
  }
  return std::string(the_date);
}

void onTimer() {
  if(maxIdleTime) {
    for(std::map<int, tClient*>::iterator it = clientConnections.begin(); it != clientConnections.end();) {
      if(it->second == NULL) clientConnections.erase(it++);
      else {
        it->second->idleTime++;
        if(it->second->idleTime > maxIdleTime) {
          close(it->second->srvSocket);
          if(verbosity>1)std::cout << "Connection " << it->second->srvSocket << " is timed out" << std::endl;
          if(it->second->data) delete[] it->second->data;
          delete it->second;
          it->second = NULL;
        }
        ++it;
      }
    }
  }
}

void checkTime() {
  struct timeval current, timeout;
  int rc = gettimeofday(&current, 0 /* timezone unused */);
  if(rc != 0) {
    perror("ERROR on setting timer");
  }
  timeout.tv_sec = startTime.tv_sec - current.tv_sec;
  timeout.tv_usec = startTime.tv_usec - current.tv_usec;
  while(timeout.tv_usec < 0) {
    timeout.tv_usec += 1000000;
    timeout.tv_sec -= 1;
  }
  if(timeout.tv_sec < 0) {
    startTime.tv_sec+=1;
    onTimer();
  }
}


int setupListenSocket() {
  struct sockaddr_in serv_addr;
  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr(listenIP.c_str());
  serv_addr.sin_port = htons(listenPort);

  int sockfd = socket(AF_INET , SOCK_STREAM , 0);
  if (sockfd  < 0) {
    perror("ERROR opening socket");
    return -5;
  }
  int reuseaddr=1;
  if (setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&reuseaddr,sizeof(reuseaddr))<0) {
    perror("ERROR setsockopt");
    return -6;
  }

  if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))<0) {
    perror("ERROR on binding");
    return -7;
  }
  //fcntl(sockfd, F_SETFL, O_NONBLOCK);

  if (listen(sockfd, 5) < 0) {
    perror("ERROR on listen");
    return -8;
  }
  return sockfd;
}

void setupClientSocket(int sockfd) {
  tClient* tmpClient = new tClient();
  tmpClient->srvSocket=sockfd;
  tmpClient->data=NULL;
  clientConnections[sockfd]=tmpClient;
  if(verbosity>0)fprintf(stderr,"%s setupClientSocket: new client socket %d is connected \n",
                           getTimeStamp().c_str(), sockfd);
}


void processReceivedData(int fd, std::string message, uint32_t messageID) {
  rapidjson::Document d1;
  std::string command="";
  std::string outStr="{\"status\":\"ERROR\", \"reason\":\"Unknown or unsupported command\"}";

  if (!d1.Parse(message.c_str()).HasParseError()) {
    if (d1.HasMember(CMD_COMMAND)) {
      command=std::string(d1[CMD_COMMAND].GetString());
    } else {
      fprintf(stderr,"%s processReceivedData: Command not found\n", getTimeStamp().c_str());
      outStr="{\"status\":\"ERROR\", \"reason\":\"Command not found\"}";
      goto sendResponse;
    }
  } else {
    fprintf(stderr,"%s processReceivedData: Request parsing error\n", getTimeStamp().c_str());
    outStr="{\"status\":\"ERROR\", \"reason\":\"Request parsing error\"}";
    goto sendResponse;
  }
  if (verbosity>3) fprintf(stderr,"%s processReceivedData: : Executing command: %s\n",
                             getTimeStamp().c_str(), message.c_str());

  if((command=="GetImagingSettings") && (proxyImaging.soap_endpoint != "NOTAVAILABLE"))
    return execGetImagingSettings(fd, d1, messageID);
  if((command=="SetImagingSettings") && (proxyImaging.soap_endpoint != "NOTAVAILABLE"))
    return execSetImagingSettings(fd, d1, messageID);
  if((command=="GetOptions")  && (proxyImaging.soap_endpoint != "NOTAVAILABLE"))
    return execGetOptions(fd, d1, messageID);
  if((command=="GetMoveOptions") && (proxyImaging.soap_endpoint != "NOTAVAILABLE"))
    return execGetMoveOptions(fd, d1, messageID);
  if((command=="Stop")  && (proxyImaging.soap_endpoint != "NOTAVAILABLE"))
    return execStop(fd, d1, messageID);
  if((command=="Move")  && (proxyImaging.soap_endpoint != "NOTAVAILABLE"))
    return execMove(fd, d1, messageID);
  if((command=="GetStatus")  && (proxyImaging.soap_endpoint != "NOTAVAILABLE"))
    return execGetStatus(fd, d1, messageID);
  if(command=="SystemReboot") return execSystemReboot(fd, d1, messageID);
  if(command=="GetSystemDateAndTime") return execGetSystemDateAndTime(fd, d1, messageID);
  if(command=="SetSystemDateAndTime") return execSetSystemDateAndTime(fd, d1, messageID);
  if(command=="GetDeviceInformation") return execGetDeviceInformation(fd, d1, messageID);
  if(command=="GetOSDs") return execGetOSDs(fd, d1, messageID);
  if(command=="GetOSD") return execGetOSD(fd, d1, messageID);
  if(command=="CreateOSD") return execCreateOSD(fd, d1, messageID);
  if(command=="DeleteOSD") return execDeleteOSD(fd, d1, messageID);
  if(command=="SetOSD") return execSetOSD(fd, d1, messageID);
  if(command=="GetOSDOptions") return execGetOSDOptions(fd, d1, messageID);
  if(command=="GetProfiles") return execGetProfiles(fd, d1, messageID);
  if(command=="GenerateRecoverVideoConfig") return execGenerateRecoverVideoConfig(fd, d1, messageID);
  if(command=="SetVideoEncoderConfiguration") return execSetVideoEncoderConfiguration(fd, d1, messageID);
  if(command=="AddPTZConfiguration") return execAddPTZConfiguration(fd, d1, messageID);
  if(command=="GetCapabilities") return execGetCapabilities(fd, d1, messageID);

  if((command=="AbsoluteMove")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execAbsoluteMove(fd, d1, messageID);
  if((command=="ContinuousMove")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execContinuousMove(fd, d1, messageID);
  if((command=="RelativeMove")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execRelativeMove(fd, d1, messageID);
  if((command=="GeoMove")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execGeoMove(fd, d1, messageID);
  if((command=="PTZStop")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execPTZStop(fd, d1, messageID);
  if((command=="SendAuxiliaryCommand")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execSendAuxiliaryCommand(fd, d1, messageID);
  if((command=="GetServiceCapabilities")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execGetServiceCapabilities(fd, d1, messageID);
  if((command=="PTZGetStatus")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execPTZGetStatus(fd, d1, messageID);
  if((command=="GetConfigurations")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execGetConfigurations(fd, d1, messageID);
  if((command=="GetConfiguration")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execGetConfiguration(fd, d1, messageID);
  if((command=="GetNodes")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execGetNodes(fd, d1, messageID);
  if((command=="GetNode")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execGetNode(fd, d1, messageID);
  if((command=="GetConfigurationOptions")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execGetConfigurationOptions(fd, d1, messageID);
  if((command=="GetCompatibleConfigurations")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execGetCompatibleConfigurations(fd, d1, messageID);
  if((command=="GetPresets")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execGetPresets(fd, d1, messageID);
  if((command=="RemovePreset")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execRemovePreset(fd, d1, messageID);
  if((command=="GotoPreset")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execGotoPreset(fd, d1, messageID);
  if((command=="GotoHomePosition")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execGotoHomePosition(fd, d1, messageID);
  if((command=="SetHomePosition")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execSetHomePosition(fd, d1, messageID);
  if((command=="CreatePresetTour")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execCreatePresetTour(fd, d1, messageID);
  if((command=="RemovePresetTour")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execRemovePresetTour(fd, d1, messageID);
  if((command=="SetPreset")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execSetPreset(fd, d1, messageID);
  if((command=="OperatePresetTour")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execOperatePresetTour(fd, d1, messageID);
  if((command=="SetConfiguration")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execSetConfiguration(fd, d1, messageID);
  if((command=="GetPresetTours")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execGetPresetTours(fd, d1, messageID);
  if((command=="GetPresetTour")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execGetPresetTour(fd, d1, messageID);
  if((command=="GetPresetTourOptions")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execGetPresetTourOptions(fd, d1, messageID);
  if((command=="ModifyPresetTour")  && (proxyPTZ.soap_endpoint != "NOTAVAILABLE"))
    return execModifyPresetTour(fd, d1, messageID);

  //return error
sendResponse:
  unsigned char data[outStr.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=outStr.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)outStr.c_str(),outStr.length());
  sendData(fd, data, outStr.length()+sHeader);

}

int sendData(int fd, unsigned char *message, int mSize) {
  int wrslt=1;
  do {
    wrslt = write(fd, message, mSize);
  } while ((wrslt < 0) && ((errno == EAGAIN) || (errno == EINTR)));
  if(wrslt <= 0) {
    //handle error writing, close socket, set reconnect.
    return -18;
  }
  return 0;
}


int onDataReceived(int fd, unsigned char* data, uint32_t dataLen) {
  tClient* tmpClient=findConnection(fd);
  if(!tmpClient) return -26;

  if(!dataLen) return -29;
  unsigned char *recMessage=NULL;
  int recSize=0;

  if (verbosity>3)fprintf(stderr,"%s onDataReceived received\n", getTimeStamp().c_str());
  pHeader tmpHeader;
  if(!tmpClient->data) {
    tmpHeader=(pHeader)data;
    if(tmpHeader->marker!=ONVIF_PROT_MARKER) {
      fprintf(stderr,"%s onDataReceived got corrupted data\n", getTimeStamp().c_str());
      return -28;
    }
    if (verbosity>3)fprintf(stderr,"%s onDataReceived Start received\n", getTimeStamp().c_str());
    tmpClient->totalSize=tmpHeader->dataLen+sHeader;
    tmpClient->currentSize=0;
    tmpClient->data=new unsigned char [tmpClient->totalSize+1];
    tmpClient->data[tmpClient->totalSize]=0;
  }
  tmpClient->idleTime=0;
  if(tmpClient->totalSize < (tmpClient->currentSize + dataLen)) {
    recSize= (tmpClient->currentSize + dataLen) - tmpClient->totalSize;
    recMessage=data + (dataLen-recSize);
    memcpy((tmpClient->data)+tmpClient->currentSize,data,(dataLen-recSize));
    tmpClient->currentSize=tmpClient->currentSize+(dataLen-recSize);
  } else {
    memcpy((tmpClient->data)+tmpClient->currentSize,data,dataLen);
    tmpClient->currentSize=tmpClient->currentSize+dataLen;
  }
  if(tmpClient->totalSize == tmpClient->currentSize) {
    if (verbosity>3) fprintf(stderr,"%s onDataReceived : Received completed message\n", getTimeStamp().c_str());
    tmpHeader=(pHeader)tmpClient->data;
//Process message:
    std::string message((const char *)(tmpClient->data+sHeader));
    processReceivedData(fd, message, tmpHeader->mesID);
//End Process message
    delete [] tmpClient->data;
    tmpClient->data=NULL;
    tmpClient->totalSize = tmpClient->currentSize =0;
  }
  if(recSize) onDataReceived(fd, recMessage, recSize);

  return 0;
}


void processRcvData(int fd) {
  unsigned char buffer[BUFFER_LEN];
  int rslt=0;

//read data:
  do {
    rslt = read(fd, buffer, BUFFER_LEN);
  } while ((rslt < 0) && ((errno == EAGAIN) || (errno == EINTR)));

  if(rslt <= 0) {
    onConnectionClosed(fd); //check for the error in the future
  } else {
    onDataReceived(fd, buffer, rslt);
  }

  if(rslt==BUFFER_LEN) processRcvData(fd);

}

int onConnectionClosed(int fd) {
  tClient* tmpClient=findConnection(fd);
  if(!tmpClient) return -27;
  else return deleteConnection(tmpClient);
}

int deleteConnection(tClient* tmpClient) {
  clientConnections[tmpClient->srvSocket]=NULL;
  close(tmpClient->srvSocket);
  if(verbosity>1)std::cout << "Connection " << tmpClient->srvSocket << " is closed" << std::endl;
  if(tmpClient->data) delete[] tmpClient->data;
  delete tmpClient;
  return 0;
}

tClient* findConnection(int fd) {
  std::map<int, tClient*>::iterator it = clientConnections.find(fd);
  if(it != clientConnections.end()) {
    return clientConnections[fd];
  }
  return NULL;
}

int main(int argc, char **argv) {

  signal(SIGHUP, SIG_IGN); //to run in background
  signal(SIGPIPE, SIG_IGN); //to stop process interrupt when trying to write to broken socket

  progName = argv[0];
  verbosity=0;
  onvifLogin=ONVIFADMIN;
  onvifPass=ONVIFPASS;
  listenIP=LISTENIP;
  listenPort=LISTENPORT;
  cameraIP=CAMERAIP;
  maxIdleTime=MAX_IDLE_TIME;


  for (int i=1; i<argc; i++) {
    if(strcmp (argv[i],"-v") == 0) {
      i++;
      verbosity=atoi(argv[i]);
    } else if(strcmp (argv[i],"-P") == 0) {
      i++;
      listenPort=atoi(argv[i]);
    } else if(strcmp (argv[i],"-m") == 0) {
      i++;
      maxIdleTime=atoi(argv[i]);
    } else if(strcmp (argv[i],"-p") == 0) {
      i++;
      onvifPass=std::string((const char*)argv[i]);
    } else if(strcmp (argv[i],"-i") == 0) {
      i++;
      cameraIP=std::string((const char*)argv[i]);
    } else if(strcmp (argv[i],"-l") == 0) {
      i++;
      onvifLogin=std::string((const char*)argv[i]);
    } else if(strcmp (argv[i],"-L") == 0) {
      i++;
      listenIP=std::string((const char*)argv[i]);
    } else usage();
  }

//Gsoap setup

  cameraIP="http://"+cameraIP+"/onvif/device_service";
  proxyDevice.soap_endpoint = cameraIP.c_str();
  if(verbosity>2)std::cout <<  "Use onvif url: " << cameraIP << std::endl;

  glSoap = soap_new();

  soap_register_plugin(proxyDevice.soap, soap_wsse);
  soap_register_plugin(proxyMedia.soap, soap_wsse);
  soap_register_plugin(proxyImaging.soap, soap_wsse);
  soap_register_plugin(proxyPTZ.soap, soap_wsse);
  soap_register_plugin(proxyDevice.soap, http_da );
  soap_register_plugin(proxyMedia.soap, http_da );
  soap_register_plugin(proxyImaging.soap, http_da );
  soap_register_plugin(proxyPTZ.soap, http_da );

  int result = SOAP_ERR;

  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyDevice.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    return -1;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyDevice.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    return -2;
  }

  proxyDevice.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyDevice.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyDevice.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;


  _tds__GetCapabilities *tmpGetCapabilities = soap_new__tds__GetCapabilities(glSoap, -1);
  tmpGetCapabilities->Category.push_back(tt__CapabilityCategory__All);
  _tds__GetCapabilitiesResponse *tmpGetCapabilitiesResponse = soap_new__tds__GetCapabilitiesResponse(glSoap, -1);

  faultStr="";
  if(false == sendGetCapabilities(&proxyDevice, tmpGetCapabilities, tmpGetCapabilitiesResponse,
                                  &proxyMedia, &proxyImaging, &proxyPTZ)) {
    if(verbosity>2)std::cout <<  "sendGetCapabilities failed all attempts" << std::endl;
    return -11;
  }

  soap_destroy(glSoap);
  soap_end(glSoap);


//Video Service
  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyMedia.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    return -1;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyMedia.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    return -2;
  }

  proxyMedia.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;

  _trt__GetVideoSources *tmpGetVideoSources = soap_new__trt__GetVideoSources(glSoap, -1);
  _trt__GetVideoSourcesResponse* tmpGetVideoSourcesResponse = soap_new__trt__GetVideoSourcesResponse(glSoap, -1);

  faultStr="";
  if(false == sendGetVideoSources(&proxyMedia, tmpGetVideoSources, tmpGetVideoSourcesResponse)) {
    if(verbosity>2)std::cout <<  "sendGetVideoSources failed all attempts" << std::endl;
    return -10;
  }

  soap_destroy(glSoap);
  soap_end(glSoap);

  if(videoSources.size() <1) {
    if(verbosity>2)std::cout <<  "No valid video sources found" << std::endl;
    return -9;
  }

//OSD Options
//        virtual int GetOSDOptions(_trt__GetOSDOptions *trt__GetOSDOptions, _trt__GetOSDOptionsResponse //&trt__GetOSDOptionsResponse)
  if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyMedia.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
    std::cout << "Failed to assign user:password" << std::endl;
    return -1;
  }

  if (SOAP_OK != soap_wsse_add_Timestamp(proxyMedia.soap, "Time", 10)) {
    std::cout << "Failed to set a timestamp" << std::endl;
    return -2;
  }

  proxyMedia.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
  proxyMedia.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;


  _trt__GetOSDOptions *tmpGetOSDOptions = soap_new__trt__GetOSDOptions(glSoap, -1);
  _trt__GetOSDOptionsResponse* tmpGetOSDOptionsResponse = soap_new__trt__GetOSDOptionsResponse(glSoap, -1);
  tmpGetOSDOptions->ConfigurationToken=videoSources[0];

  faultStr="";
  if(false == sendGetOSDOptions(&proxyMedia, tmpGetOSDOptions, tmpGetOSDOptionsResponse)) {
    if(verbosity>2)std::cout <<  "sendGetOSDOptions failed all attempts" << std::endl;
    cachedOSDOptionsResponse="{\"status\":\"ERROR\", \"reason\":\"Unknown or unsupported command\"}";
  } else cachedOSDOptionsResponse=prepareOSDOptionsResponse(tmpGetOSDOptionsResponse);

  soap_destroy(glSoap);
  soap_end(glSoap);


//Imaging:
  if(proxyImaging.soap_endpoint != "NOTAVAILABLE") {
    //Imaging Options
    if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyImaging.soap, NULL,
        onvifLogin.c_str(), onvifPass.c_str())) {
      std::cout << "Failed to assign user:password" << std::endl;
      return -1;
    }

    if (SOAP_OK != soap_wsse_add_Timestamp(proxyImaging.soap, "Time", 10)) {
      std::cout << "Failed to set a timestamp" << std::endl;
      return -2;
    }

    proxyImaging.soap->recv_timeout=ONVIF_WAIT_TIMEOUT;
    proxyImaging.soap->send_timeout=ONVIF_WAIT_TIMEOUT;
    proxyImaging.soap->connect_timeout=ONVIF_WAIT_TIMEOUT;


    _timg__GetOptions *tmpGetOptions = soap_new__timg__GetOptions(glSoap, -1);
    _timg__GetOptionsResponse* tmpGetOptionsResponse = soap_new__timg__GetOptionsResponse(glSoap, -1);
    tmpGetOptions->VideoSourceToken=videoSources[0];

    faultStr="";
  if(false == sendGetOptions(&proxyImaging, tmpGetOptions, tmpGetOptionsResponse)) {
      if(verbosity>2)std::cout <<  "sendGetOptions failed all attempts" << std::endl;
      cachedImagingOptionsResponse="{\"status\":\"ERROR\", \"reason\":\"Unknown or unsupported command\"}";
    } else  cachedImagingOptionsResponse=prepareOptionsResponse(tmpGetOptionsResponse);

    soap_destroy(glSoap);
    soap_end(glSoap);

    //Imaging move options
    if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyImaging.soap, NULL, onvifLogin.c_str(),
        onvifPass.c_str())) {
      std::cout << "Failed to assign user:password" << std::endl;
      return -1;
    }


    _timg__GetMoveOptions *tmpGetMoveOptions = soap_new__timg__GetMoveOptions(glSoap, -1);
    _timg__GetMoveOptionsResponse* tmpGetMoveOptionsResponse = soap_new__timg__GetMoveOptionsResponse(glSoap, -1);
    tmpGetMoveOptions->VideoSourceToken=videoSources[0];

    faultStr="";
  if(false == sendGetMoveOptions(&proxyImaging, tmpGetMoveOptions, tmpGetMoveOptionsResponse)) {
      if(verbosity>2)std::cout <<  "sendGetMoveOptions failed all attempts" << std::endl;
      cachedMoveOptionsResponse="{\"status\":\"ERROR\", \"reason\":\"Unknown or unsupported command\"}";
    } else cachedMoveOptionsResponse=prepareMoveOptionsResponse(tmpGetMoveOptionsResponse);

    soap_destroy(glSoap);
    soap_end(glSoap);

  }


//PTZ:
  if(proxyPTZ.soap_endpoint != "NOTAVAILABLE") {



  }

//Network setup
  int  maxfd, sockfd, clifd;
  sockfd=setupListenSocket();
  if(sockfd < 0) exit(sockfd);

//Timer setup
  int rc = gettimeofday(&startTime, 0 /* timezone unused */);
  if(rc) {
    perror("ERROR on init timer");
    return -11;
  }
  startTime.tv_sec+=1;


//for select
  struct sockaddr_in cli_addr;
  int clilen = sizeof(cli_addr);
  int nready;
  fd_set readfds;
  struct timeval tv;


//Main loop:
  while(1) {


// Prepare for select:
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);
    maxfd = sockfd;


    for(std::map<int, tClient*>::iterator it = clientConnections.begin(); it != clientConnections.end();) {
      if(it->second == NULL) clientConnections.erase(it++);
      else {
        FD_SET(it->first, &readfds);
        maxfd = (maxfd < it->first)?it->first:maxfd;
        ++it;
      }
    }

    nready = select(maxfd+1, &readfds, NULL, NULL, &tv);

//Check select results:

    if (nready < 0 && errno != EINTR) {
      if (verbosity>0) cerr << getTimeStamp() << "Error in select():"  << strerror(errno) << endl;
    } else if (nready == 0) {
      if (verbosity>4) cerr << getTimeStamp() << "select() timed out!"  <<  endl;
    } else if (nready > 0) {
//////////////////// Process select results
      if (FD_ISSET(sockfd, &readfds)) {
        if (verbosity>1) cerr << getTimeStamp() << "New connection request:"  << endl;
        clifd = accept(sockfd, (struct sockaddr *)&cli_addr, (socklen_t *) &clilen);
        if (clifd < 0) {
          if (verbosity>0) cerr << getTimeStamp() << "Error in accept():" << strerror(errno) << endl;
        } else {
          if (verbosity>1) cerr << getTimeStamp() << "Accepted new connection:" << clifd
                                  << ", from IP: " << std::string(inet_ntoa(cli_addr.sin_addr))
                                  << ", port: " << std::to_string(htons(cli_addr.sin_port)) << endl;
          setupClientSocket(clifd);
        }
      } else {
        bool found=false;
        for(std::map<int, tClient*>::iterator it = clientConnections.begin();
            it != clientConnections.end();) {
          if(it->second == NULL) clientConnections.erase(it++);
          else {
            if (FD_ISSET(it->first, &readfds)) {
              FD_CLR(it->first, &readfds);
              processRcvData(it->first);
              found=true;
            }
            ++it;
          }
        }
        if (verbosity>0 && (!found)) cerr << getTimeStamp() << "Select returned EINTR:"  << endl;
      }
//////////////////// end Process select results
    }
    checkTime(); //check if timer should be triggered
  }

  return 0;
}
