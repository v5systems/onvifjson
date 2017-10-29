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


using namespace std;
using namespace rapidjson;

struct tHeader{
  uint32_t marker;
  uint32_t mesID;
  uint32_t dataLen;
  unsigned char data;
};

typedef tHeader* pHeader;
const size_t sHeader = 12;

struct tClient{
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

char szVideoSourceToken[32] = {0};

//Helper functions:
int onDataReceived(int fd, unsigned char* data, uint32_t dataLen);
void processReceivedData(int fd, std::string message, uint32_t messageID);
int sendData(int fd, unsigned char *message, int mSize);
void processRcvData(int fd);
std::string getTimeStamp();
const char * getLogTimestamp();
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
bool sendGetVideoSources(MediaBindingProxy* tProxyMedia, _trt__GetVideoSources * imagingSettings,
                    _trt__GetVideoSourcesResponse * imagingSettingsResponse);
bool sendGetProfiles(MediaBindingProxy* tProxyMedia, _trt__GetProfiles * getProfiles,
                     _trt__GetProfilesResponse * getProfilesResponse, soap * tSoap);
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
void execGetOSDOptions(int fd, rapidjson::Document &d1, uint32_t messageID);

std::string prepareOptionsResponse(_timg__GetOptionsResponse* optionsResponse);
std::string prepareMoveOptionsResponse(_timg__GetMoveOptionsResponse* moveOptionsResponse);
std::string prepareOSDOptionsResponse(_trt__GetOSDOptionsResponse* OSDOptionsResponse);

//###################### Implementation: ##################################################

void printError(soap* _psoap) {
  fprintf(stderr,"error:%d faultstring:%s faultcode:%s faultsubcode:%s faultdetail:%s\n",
          _psoap->error,	*soap_faultstring(_psoap), *soap_faultcode(_psoap),*soap_faultsubcode(_psoap),
          *soap_faultdetail(_psoap));
}

bool sendGetWsdlUrl(DeviceBindingProxy* tProxyDevice, _tds__GetWsdlUrl * wsdURL,
                    _tds__GetWsdlUrlResponse * wsdURLResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > 4){
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
                         _tds__SystemRebootResponse * sysRebootResponse){
  static int tCount=0;
  tCount++;
  if(tCount > 4){
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

bool sendGetCapabilities(DeviceBindingProxy* tProxyDevice, _tds__GetCapabilities * getCap,
                         _tds__GetCapabilitiesResponse * getCapResponse, MediaBindingProxy * tProxyMedia,
                             ImagingBindingProxy* tProxyImaging, PTZBindingProxy* tProxyPTZ) {
  static int tCount=0;
  tCount++;
  if(tCount > 4){
    tCount = 0;
    return false;
  }
  int result = tProxyDevice->GetCapabilities(getCap, *getCapResponse);
  if (result == SOAP_OK) {
    if (getCapResponse->Capabilities->Media != NULL) {
      tProxyMedia->soap_endpoint = getCapResponse->Capabilities->Media->XAddr.c_str();
      if(verbosity>2)fprintf(stderr, "MediaUrl Found: %s\n",getCapResponse->Capabilities->Media->XAddr.c_str());
    } else {
      if(verbosity>1)std::cout <<  "sendGetCapabilities Media not found: "  << std::endl;
      tCount = 0;
      return false;
    }
    if (getCapResponse->Capabilities->Imaging != NULL) {
      tProxyImaging->soap_endpoint = getCapResponse->Capabilities->Imaging->XAddr.c_str();
      if(verbosity>2)fprintf(stderr, "ImagingUrl Found: %s\n",getCapResponse->Capabilities->Imaging->XAddr.c_str());
    } else {
      if(verbosity>1)std::cout <<  "sendGetCapabilities Imaging not found: "  << std::endl;
      tCount = 0;
      return false;
    }
    if (getCapResponse->Capabilities->PTZ != NULL) {
      tProxyPTZ->soap_endpoint = getCapResponse->Capabilities->PTZ->XAddr.c_str();
      if(verbosity>2)fprintf(stderr, "PTZUrl Found: %s\n",getCapResponse->Capabilities->PTZ->XAddr.c_str());
    } else {
      if(verbosity>1)std::cout <<  "sendGetCapabilities PTZ not found: "  << std::endl;
    }
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
  if(tCount > 4){
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
                    _trt__GetVideoSourcesResponse * imagingSettingsResponse){
  static int tCount=0;
  tCount++;
  if(tCount > 4){
    tCount = 0;
    return false;
  }

  int result = tProxyMedia->GetVideoSources(imagingSettings, *imagingSettingsResponse);
  if (result == SOAP_OK) {
    if(verbosity>2) {
      fprintf(stderr, "VideoSources Found: \n");
    }
    for(unsigned i=0; i<imagingSettingsResponse->VideoSources.size(); i++){
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
  if(tCount > 4){
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
  if(tCount > 4){
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
                    _trt__GetOSDsResponse * imagingSettingsResponse){
  static int tCount=0;
  tCount++;
  if(tCount > 4){
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
                    _trt__GetOSDResponse * imagingSettingsResponse){
  static int tCount=0;
  tCount++;
  if(tCount > 4){
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

bool sendSetOSD(MediaBindingProxy* tProxyMedia, _trt__SetOSD * imagingSettings,
                    _trt__SetOSDResponse * imagingSettingsResponse){
  static int tCount=0;
  tCount++;
  if(tCount > 4){
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
                    _trt__GetOSDOptionsResponse * imagingSettingsResponse){
  static int tCount=0;
  tCount++;
  if(tCount > 4){
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
  if(tCount > 4){
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

bool sendStop(ImagingBindingProxy* tProxyImaging, _timg__Stop * imagingSettings,
                    _timg__StopResponse * imagingSettingsResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > 4){
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
  if(tCount > 4){
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

bool sendGetProfiles(MediaBindingProxy* tProxyMedia, _trt__GetProfiles * getProfiles,
                     _trt__GetProfilesResponse * getProfilesResponse, soap * tSoap) {
  static int tCount=0;
  tCount++;
  if(tCount > 4){
    tCount = 0;
    return false;
  }

  int result = tProxyMedia->GetProfiles(getProfiles, *getProfilesResponse);
  if (result == SOAP_OK) {
    _trt__GetStreamUri *tmpGetStreamUri = soap_new__trt__GetStreamUri(tSoap, -1);
    tmpGetStreamUri->StreamSetup = soap_new_tt__StreamSetup(tSoap, -1);
    tmpGetStreamUri->StreamSetup->Stream = tt__StreamType__RTP_Unicast;
    tmpGetStreamUri->StreamSetup->Transport = soap_new_tt__Transport(tSoap, -1);
    tmpGetStreamUri->StreamSetup->Transport->Protocol = tt__TransportProtocol__RTSP;

    _trt__GetStreamUriResponse *tmpGetStreamUriResponse = soap_new__trt__GetStreamUriResponse(tSoap, -1);

    if(verbosity>2)fprintf(stderr, "*****MediaProfilesFound:\n");

    for (int i = 0; i < getProfilesResponse->Profiles.size(); i++) {
      if(verbosity>2)fprintf(stderr, "\t%d Name:%s\n\t\tToken:%s\n", i, getProfilesResponse->Profiles[i]->Name.c_str(),
                               getProfilesResponse->Profiles[i]->token.c_str());
      tmpGetStreamUri->ProfileToken = getProfilesResponse->Profiles[i]->token;
      if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(tProxyMedia->soap, NULL, onvifLogin.c_str(),  onvifPass.c_str())) {
        tCount = 0;
        return false;
      }
      if (false == sendGetStreamUri(tProxyMedia, tmpGetStreamUri, tmpGetStreamUriResponse)) {
        continue;
      }
      //profNames.push_back(getProfilesResponse->Profiles[i]->Name);
      //profTokens.push_back(getProfilesResponse->Profiles[i]->token);
    }
    tCount = 0;
    return true;
  } else {
    if(verbosity>2)std::cout <<  "sendGetProfiles return result: " << result << std::endl;
    if(verbosity>1)printError(tProxyMedia->soap);
    tProxyMedia->soap->userid = onvifLogin.c_str();
    tProxyMedia->soap->passwd = onvifPass.c_str();
    return sendGetProfiles(tProxyMedia, getProfiles, getProfilesResponse, tSoap);
  }
}

bool sendGetStreamUri(MediaBindingProxy* tProxyMedia, _trt__GetStreamUri * streamUri,
                      _trt__GetStreamUriResponse * streamUriResponse) {
  static int tCount=0;
  tCount++;
  if(tCount > 4){
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

void execSystemReboot(int fd, rapidjson::Document &d1, uint32_t messageID){
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

    if(false == sendSystemReboot(&proxyDevice, tmpSystemReboot, tmpSystemRebootResponse)) {
      if(verbosity>2)std::cout <<  "sendSystemReboot failed all attempts" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"sendSystemReboot failed all attempts\"}";
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



void execGetImagingSettings(int fd, rapidjson::Document &d1, uint32_t messageID){
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

    if(false == sendGetImagingSettings(&proxyImaging, GetImagingSettings, GetImagingSettingsResponse)) {
      if(verbosity>2)std::cout <<  "sendGetImagingSettings failed all attempts" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetImagingSettings failed all attempts\"}";
      goto cleanSendResponse;
    }
//Process response
    outStr="{\"status\":\"OK\", \"parameters\":{";

    if(GetImagingSettingsResponse->ImagingSettings->BacklightCompensation !=NULL){
      outStr+="\"BacklightCompensation\":{";
      if(GetImagingSettingsResponse->ImagingSettings->BacklightCompensation->Mode == tt__BacklightCompensationMode__OFF){
        outStr+="\"Mode\":\"OFF\", ";
      }
      else{
        outStr+="\"Mode\":\"ON\", ";
      }
      if(GetImagingSettingsResponse->ImagingSettings->BacklightCompensation->Level!=NULL){
        outStr+="\"Level\":\""+
            std::to_string(*GetImagingSettingsResponse->ImagingSettings->BacklightCompensation->Level)+"\"";
      }
      if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
      outStr+="}, ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Brightness !=NULL){
      outStr+="\"Brightness\":\""+std::to_string(*GetImagingSettingsResponse->ImagingSettings->Brightness)+"\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->ColorSaturation !=NULL){
      outStr+="\"ColorSaturation\":\""
            +std::to_string(*GetImagingSettingsResponse->ImagingSettings->ColorSaturation) +"\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Contrast !=NULL){
      outStr+="\"Contrast\":\""+std::to_string(*GetImagingSettingsResponse->ImagingSettings->Contrast)+"\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Sharpness !=NULL){
      outStr+="\"Sharpness\":\""+std::to_string(*GetImagingSettingsResponse->ImagingSettings->Sharpness)+"\", ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Exposure !=NULL){
      outStr+="\"Exposure\":{";
      if(GetImagingSettingsResponse->ImagingSettings->Exposure->Mode == tt__ExposureMode__AUTO){
        outStr+="\"Mode\":\"AUTO\", ";
      }
      else{
        outStr+="\"Mode\":\"MANUAL\", ";
      }
      if(GetImagingSettingsResponse->ImagingSettings->Exposure->Priority != NULL){
        if(*GetImagingSettingsResponse->ImagingSettings->Exposure->Priority == tt__ExposurePriority__LowNoise){
          outStr+="\"Mode\":\"LowNoise\", ";
        }
        else{
          outStr+="\"Mode\":\"FrameRate\", ";
        }
      }
      if(GetImagingSettingsResponse->ImagingSettings->Exposure->ExposureTime!=NULL){
        outStr+="\"ExposureTime\":\""+
            std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->ExposureTime)+"\", ";
      }
      if(GetImagingSettingsResponse->ImagingSettings->Exposure->Gain!=NULL){
        outStr+="\"Gain\":\""+
            std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->Gain)+"\", ";
      }
      if(GetImagingSettingsResponse->ImagingSettings->Exposure->Iris!=NULL){
        outStr+="\"Iris\":\""+
            std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->Iris)+"\", ";
      }
      if(GetImagingSettingsResponse->ImagingSettings->Exposure->MaxExposureTime!=NULL){
        outStr+="\"MaxExposureTime\":\""+
            std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->MaxExposureTime)+"\", ";
      }
      if(GetImagingSettingsResponse->ImagingSettings->Exposure->MaxGain!=NULL){
        outStr+="\"MaxGain\":\""+
            std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->MaxGain)+"\", ";
      }
      if(GetImagingSettingsResponse->ImagingSettings->Exposure->MaxIris!=NULL){
        outStr+="\"MaxIris\":\""+
            std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->MaxIris)+"\", ";
      }
      if(GetImagingSettingsResponse->ImagingSettings->Exposure->MinExposureTime!=NULL){
        outStr+="\"MinExposureTime\":\""+
            std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->MinExposureTime)+"\", ";
      }
      if(GetImagingSettingsResponse->ImagingSettings->Exposure->MinGain!=NULL){
        outStr+="\"MinGain\":\""+
            std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->MinGain)+"\", ";
      }
      if(GetImagingSettingsResponse->ImagingSettings->Exposure->MinIris!=NULL){
        outStr+="\"MinIris\":\""+
            std::to_string(*GetImagingSettingsResponse->ImagingSettings->Exposure->MinIris)+"\"";
      }
      if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
      outStr+="}, ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->Focus !=NULL){
      outStr+="\"Focus\":{";
      if(GetImagingSettingsResponse->ImagingSettings->Focus->AutoFocusMode == tt__AutoFocusMode__AUTO){
        outStr+="\"AutoFocusMode\":\"AUTO\", ";
      }
      else{
        outStr+="\"AutoFocusMode\":\"MANUAL\", ";
      }
      if(GetImagingSettingsResponse->ImagingSettings->Focus->DefaultSpeed!=NULL){
        outStr+="\"DefaultSpeed\":\""+
            std::to_string(*GetImagingSettingsResponse->ImagingSettings->Focus->DefaultSpeed)+"\", ";
      }
      if(GetImagingSettingsResponse->ImagingSettings->Focus->FarLimit!=NULL){
        outStr+="\"FarLimit\":\""+
            std::to_string(*GetImagingSettingsResponse->ImagingSettings->Focus->FarLimit)+"\", ";
      }
      if(GetImagingSettingsResponse->ImagingSettings->Focus->NearLimit!=NULL){
        outStr+="\"NearLimit\":\""+
            std::to_string(*GetImagingSettingsResponse->ImagingSettings->Focus->NearLimit)+"\"";
      }
      if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
      outStr+="}, ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->WideDynamicRange !=NULL){
      outStr+="\"WideDynamicRange\":{";
      if(GetImagingSettingsResponse->ImagingSettings->WideDynamicRange->Mode == tt__WideDynamicMode__OFF){
        outStr+="\"Mode\":\"OFF\", ";
      }
      else{
        outStr+="\"Mode\":\"ON\", ";
      }
      if(GetImagingSettingsResponse->ImagingSettings->WideDynamicRange->Level!=NULL){
        outStr+="\"Level\":\""+
            std::to_string(*GetImagingSettingsResponse->ImagingSettings->WideDynamicRange->Level)+"\"";
      }
      if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
      outStr+="}, ";
    }
    if(GetImagingSettingsResponse->ImagingSettings->IrCutFilter !=NULL){
      outStr+="\"IrCutFilter\":";
      if(*GetImagingSettingsResponse->ImagingSettings->IrCutFilter == tt__IrCutFilterMode__OFF){
        outStr+="\"OFF\", ";
      }
      else if(*GetImagingSettingsResponse->ImagingSettings->IrCutFilter == tt__IrCutFilterMode__ON){
        outStr+="\"ON\", ";
      }
      else{
        outStr+="\"AUTO\", ";
      }
    }
    if(GetImagingSettingsResponse->ImagingSettings->WhiteBalance !=NULL){
      outStr+="\"WhiteBalance\":{";
      if(GetImagingSettingsResponse->ImagingSettings->WhiteBalance->Mode == tt__WhiteBalanceMode__AUTO){
        outStr+="\"Mode\":\"AUTO\", ";
      }
      else{
        outStr+="\"Mode\":\"MANUAL\", ";
      }
      if(GetImagingSettingsResponse->ImagingSettings->WhiteBalance->CbGain!=NULL){
        outStr+="\"CbGain\":\""+
            std::to_string(*GetImagingSettingsResponse->ImagingSettings->WhiteBalance->CbGain)+"\", ";
      }
      if(GetImagingSettingsResponse->ImagingSettings->WhiteBalance->CrGain!=NULL){
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

void execSetImagingSettings(int fd, rapidjson::Document &d1, uint32_t messageID){
    std::string outStr="{\"status\":\"OK\"}";
    std::string tmpVar;
    _timg__GetImagingSettings * GetImagingSettings;
    _timg__GetImagingSettingsResponse * GetImagingSettingsResponse;
    _timg__SetImagingSettings * SetImagingSettings;
    _timg__SetImagingSettingsResponse * SetImagingSettingsResponse;

    if (d1.HasMember(CMD_PARAMS)){
      //tmpVar=std::string(d1[CMD_PARAMS].GetString());
    }
    else{
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

    if(false == sendGetImagingSettings(&proxyImaging, GetImagingSettings, GetImagingSettingsResponse)) {
      if(verbosity>2)std::cout <<  "sendGetImagingSettings failed all attempts" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"sendGetImagingSettings failed all attempts\"}";
      goto cleanSendResponse;
    }

    SetImagingSettings = soap_new__timg__SetImagingSettings(glSoap, -1);
    SetImagingSettingsResponse = soap_new__timg__SetImagingSettingsResponse(glSoap, -1);
    SetImagingSettings->ImagingSettings=GetImagingSettingsResponse->ImagingSettings;
    SetImagingSettings->VideoSourceToken=videoSources[0];

//Process response

    if((SetImagingSettings->ImagingSettings->BacklightCompensation !=NULL) &&
                      (d1[CMD_PARAMS].HasMember("BacklightCompensation"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["BacklightCompensation"]["Mode"].GetString());
      if(tmpVar=="OFF"){
        SetImagingSettings->ImagingSettings->BacklightCompensation->Mode = tt__BacklightCompensationMode__OFF;
      }
      else{
        SetImagingSettings->ImagingSettings->BacklightCompensation->Mode = tt__BacklightCompensationMode__ON;
      }
      if((SetImagingSettings->ImagingSettings->BacklightCompensation->Level!=NULL) &&
        (d1[CMD_PARAMS]["BacklightCompensation"].HasMember("Level"))){
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
      if(tmpVar=="AUTO"){
        SetImagingSettings->ImagingSettings->Exposure->Mode = tt__ExposureMode__AUTO;
      }
      else{
        SetImagingSettings->ImagingSettings->Exposure->Mode = tt__ExposureMode__MANUAL;
      }
      if((SetImagingSettings->ImagingSettings->Exposure->Priority != NULL) &&
                      (d1[CMD_PARAMS]["Exposure"].HasMember("Priority"))) {
        tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["Priority"].GetString());
        if(tmpVar=="LowNoise"){
          (*SetImagingSettings->ImagingSettings->Exposure->Priority)=tt__ExposurePriority__LowNoise;
        }
        else{
          (*SetImagingSettings->ImagingSettings->Exposure->Priority)=tt__ExposurePriority__FrameRate;
        }
      }
      if((SetImagingSettings->ImagingSettings->Exposure->ExposureTime!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("ExposureTime"))){
         tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["ExposureTime"].GetString());
         (*SetImagingSettings->ImagingSettings->Exposure->ExposureTime)=std::stof(tmpVar);
      }
      if((SetImagingSettings->ImagingSettings->Exposure->Gain!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("Gain"))){
         tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["Gain"].GetString());
         (*SetImagingSettings->ImagingSettings->Exposure->Gain)=std::stof(tmpVar);
      }
      if((SetImagingSettings->ImagingSettings->Exposure->Iris!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("Iris"))){
         tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["Iris"].GetString());
         (*SetImagingSettings->ImagingSettings->Exposure->Iris)=std::stof(tmpVar);
      }
      if((SetImagingSettings->ImagingSettings->Exposure->MaxExposureTime!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("MaxExposureTime"))){
         tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["MaxExposureTime"].GetString());
         (*SetImagingSettings->ImagingSettings->Exposure->MaxExposureTime)=std::stof(tmpVar);
      }
      if((SetImagingSettings->ImagingSettings->Exposure->MaxGain!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("MaxGain"))){
         tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["MaxGain"].GetString());
         (*SetImagingSettings->ImagingSettings->Exposure->MaxGain)=std::stof(tmpVar);
      }
      if((SetImagingSettings->ImagingSettings->Exposure->MaxIris!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("MaxIris"))){
         tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["MaxIris"].GetString());
         (*SetImagingSettings->ImagingSettings->Exposure->MaxIris)=std::stof(tmpVar);
      }
      if((SetImagingSettings->ImagingSettings->Exposure->MinExposureTime!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("MinExposureTime"))){
         tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["MinExposureTime"].GetString());
         (*SetImagingSettings->ImagingSettings->Exposure->MinExposureTime)=std::stof(tmpVar);
      }
      if((SetImagingSettings->ImagingSettings->Exposure->MinGain!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("MinGain"))){
         tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["MinGain"].GetString());
         (*SetImagingSettings->ImagingSettings->Exposure->MinGain)=std::stof(tmpVar);
      }
      if((SetImagingSettings->ImagingSettings->Exposure->MinIris!=NULL) &&
        (d1[CMD_PARAMS]["Exposure"].HasMember("MinIris"))){
         tmpVar=std::string(d1[CMD_PARAMS]["Exposure"]["MinIris"].GetString());
         (*SetImagingSettings->ImagingSettings->Exposure->MinIris)=std::stof(tmpVar);
      }
    }
    if((SetImagingSettings->ImagingSettings->Focus !=NULL) &&
                      (d1[CMD_PARAMS].HasMember("Focus"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["Focus"]["AutoFocusMode"].GetString());
      if(tmpVar=="AUTO"){
        SetImagingSettings->ImagingSettings->Focus->AutoFocusMode = tt__AutoFocusMode__AUTO;
      }
      else{
        SetImagingSettings->ImagingSettings->Focus->AutoFocusMode = tt__AutoFocusMode__MANUAL;
      }
      if((SetImagingSettings->ImagingSettings->Focus->DefaultSpeed!=NULL) &&
        (d1[CMD_PARAMS]["Focus"].HasMember("DefaultSpeed"))){
         tmpVar=std::string(d1[CMD_PARAMS]["Focus"]["DefaultSpeed"].GetString());
         (*SetImagingSettings->ImagingSettings->Focus->DefaultSpeed)=std::stof(tmpVar);
      }
      if((SetImagingSettings->ImagingSettings->Focus->FarLimit!=NULL) &&
        (d1[CMD_PARAMS]["Focus"].HasMember("FarLimit"))){
         tmpVar=std::string(d1[CMD_PARAMS]["Focus"]["FarLimit"].GetString());
         (*SetImagingSettings->ImagingSettings->Focus->FarLimit)=std::stof(tmpVar);
      }
      if((SetImagingSettings->ImagingSettings->Focus->NearLimit!=NULL) &&
        (d1[CMD_PARAMS]["Focus"].HasMember("NearLimit"))){
         tmpVar=std::string(d1[CMD_PARAMS]["Focus"]["NearLimit"].GetString());
         (*SetImagingSettings->ImagingSettings->Focus->NearLimit)=std::stof(tmpVar);
      }
    }
    if((SetImagingSettings->ImagingSettings->WideDynamicRange !=NULL) &&
                      (d1[CMD_PARAMS].HasMember("WideDynamicRange"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["WideDynamicRange"]["Mode"].GetString());
      if(tmpVar=="OFF"){
        SetImagingSettings->ImagingSettings->WideDynamicRange->Mode = tt__WideDynamicMode__OFF;
      }
      else{
        SetImagingSettings->ImagingSettings->WideDynamicRange->Mode = tt__WideDynamicMode__ON;
      }

      if((SetImagingSettings->ImagingSettings->WideDynamicRange->Level!=NULL) &&
        (d1[CMD_PARAMS]["WideDynamicRange"].HasMember("Level"))){
         tmpVar=std::string(d1[CMD_PARAMS]["WideDynamicRange"]["Level"].GetString());
         (*SetImagingSettings->ImagingSettings->WideDynamicRange->Level)=std::stof(tmpVar);
      }
    }
    if((SetImagingSettings->ImagingSettings->IrCutFilter !=NULL) &&
                      (d1[CMD_PARAMS].HasMember("IrCutFilter"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["IrCutFilter"].GetString());
      if(tmpVar=="OFF"){
        (*SetImagingSettings->ImagingSettings->IrCutFilter) = tt__IrCutFilterMode__OFF;
      }
      else if(tmpVar=="ON"){
        (*SetImagingSettings->ImagingSettings->IrCutFilter) = tt__IrCutFilterMode__ON;
      }
      else{
        (*SetImagingSettings->ImagingSettings->IrCutFilter) = tt__IrCutFilterMode__AUTO;
      }
    }
    if((SetImagingSettings->ImagingSettings->WhiteBalance !=NULL) &&
                      (d1[CMD_PARAMS].HasMember("WhiteBalance"))) {
      tmpVar=std::string(d1[CMD_PARAMS]["WhiteBalance"]["Mode"].GetString());
      if(tmpVar=="AUTO"){
        SetImagingSettings->ImagingSettings->WhiteBalance->Mode = tt__WhiteBalanceMode__AUTO;
      }
      else{
        SetImagingSettings->ImagingSettings->WhiteBalance->Mode = tt__WhiteBalanceMode__MANUAL;
      }
      if((SetImagingSettings->ImagingSettings->WhiteBalance->CbGain!=NULL) &&
        (d1[CMD_PARAMS]["WhiteBalance"].HasMember("CbGain"))){
         tmpVar=std::string(d1[CMD_PARAMS]["WhiteBalance"]["CbGain"].GetString());
         (*SetImagingSettings->ImagingSettings->WhiteBalance->CbGain)=std::stof(tmpVar);
      }
      if((SetImagingSettings->ImagingSettings->WhiteBalance->CrGain!=NULL) &&
        (d1[CMD_PARAMS]["WhiteBalance"].HasMember("CrGain"))){
         tmpVar=std::string(d1[CMD_PARAMS]["WhiteBalance"]["CrGain"].GetString());
         (*SetImagingSettings->ImagingSettings->WhiteBalance->CrGain)=std::stof(tmpVar);
      }
    }

//End process response

    if(false == sendSetImagingSettings(&proxyImaging, SetImagingSettings, SetImagingSettingsResponse)) {
      if(verbosity>2)std::cout <<  "sendSetImagingSettings failed all attempts" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"sendSetImagingSettings failed all attempts\"}";
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

void execGetOSDs(int fd, rapidjson::Document &d1, uint32_t messageID){
  std::string outStr="{\"status\":\"ERROR\", \"reason\":\"Not implemented yet\"}";
  goto cleanSendResponse;
/*
bool sendStop(ImagingBindingProxy* tProxyImaging, _timg__Stop * imagingSettings,
                    _timg__StopResponse * imagingSettingsResponse);
*/

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

void execGetOSD(int fd, rapidjson::Document &d1, uint32_t messageID){
  std::string outStr="{\"status\":\"ERROR\", \"reason\":\"Not implemented yet\"}";
  goto cleanSendResponse;
/*
bool sendStop(ImagingBindingProxy* tProxyImaging, _timg__Stop * imagingSettings,
                    _timg__StopResponse * imagingSettingsResponse);
*/

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

void execSetOSD(int fd, rapidjson::Document &d1, uint32_t messageID){
  std::string outStr="{\"status\":\"ERROR\", \"reason\":\"Not implemented yet\"}";
  goto cleanSendResponse;
/*
bool sendStop(ImagingBindingProxy* tProxyImaging, _timg__Stop * imagingSettings,
                    _timg__StopResponse * imagingSettingsResponse);
*/

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

void execGetOSDOptions(int fd, rapidjson::Document &d1, uint32_t messageID){
  unsigned char data[cachedOSDOptionsResponse.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=cachedOSDOptionsResponse.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)cachedOSDOptionsResponse.c_str(),cachedOSDOptionsResponse.length());
  sendData(fd, data, cachedOSDOptionsResponse.length()+sHeader);
}


void execGetOptions(int fd, rapidjson::Document &d1, uint32_t messageID){
  unsigned char data[cachedImagingOptionsResponse.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=cachedImagingOptionsResponse.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)cachedImagingOptionsResponse.c_str(),cachedImagingOptionsResponse.length());
  sendData(fd, data, cachedImagingOptionsResponse.length()+sHeader);
}

void execGetMoveOptions(int fd, rapidjson::Document &d1, uint32_t messageID){
  unsigned char data[cachedMoveOptionsResponse.length()+sHeader];
  pHeader tmpHeader= (pHeader)data;
  tmpHeader->dataLen=cachedMoveOptionsResponse.length();
  tmpHeader->mesID=messageID;
  tmpHeader->marker=ONVIF_PROT_MARKER;
  memcpy(data+sHeader,(unsigned char*)cachedMoveOptionsResponse.c_str(),cachedMoveOptionsResponse.length());
  sendData(fd, data, cachedMoveOptionsResponse.length()+sHeader);
}

void execStop(int fd, rapidjson::Document &d1, uint32_t messageID){
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

    if(false == sendStop(&proxyImaging, GetStop, GetStopResponse)) {
      if(verbosity>2)std::cout <<  "sendStop failed all attempts" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"sendStop failed all attempts\"}";
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


void execMove(int fd, rapidjson::Document &d1, uint32_t messageID){
    std::string outStr="{\"status\":\"OK\"}";

    _timg__Move * GetMove;
    _timg__MoveResponse * GetMoveResponse;

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
/*
{"status":"OK", "parameters":{"Absolute":{"Position":{"Min":"0.000000", "Max":"100.000000"}, "Speed":{"Min":"-100.000000", "Max":"100.000000"}}, "Relative":{"Distance":{"Min":"-100.000000", "Max":"100.000000"}, "Speed":{"Min":"-100.000000", "Max":"100.000000"}}, "Continuous":{"Speed":{"Min":"-100.000000", "Max":"100.000000"}}}}
*/


    if(false == sendMove(&proxyImaging, GetMove, GetMoveResponse)) {
      if(verbosity>2)std::cout <<  "sendMove failed all attempts" << std::endl;
      outStr="{\"status\":\"ERROR\", \"reason\":\"sendMove failed all attempts\"}";
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


std::string prepareOptionsResponse(_timg__GetOptionsResponse* optionsResponse){
  std::string outStr="{\"status\":\"OK\", \"parameters\":{";
  if(optionsResponse->ImagingOptions->BacklightCompensation!=NULL){
    outStr+="\"BacklightCompensation\":{";
    if(optionsResponse->ImagingOptions->BacklightCompensation->Mode.size()>0){
      outStr+="\"Mode\":[\"ON\", \"OFF\"], ";
    }
    if(optionsResponse->ImagingOptions->BacklightCompensation->Level!=NULL){
      outStr+="\"Level\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->BacklightCompensation->Level->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->BacklightCompensation->Level->Max)+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(optionsResponse->ImagingOptions->Brightness !=NULL){
    outStr+="\"Brightness\":{";
    outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Brightness->Min)+"\", ";
    outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Brightness->Max)+"\"";
    outStr+="}, ";
  }
  if(optionsResponse->ImagingOptions->ColorSaturation !=NULL){
    outStr+="\"ColorSaturation\":{";
    outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->ColorSaturation->Min)+"\", ";
    outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->ColorSaturation->Max)+"\"";
    outStr+="}, ";
  }
  if(optionsResponse->ImagingOptions->Contrast !=NULL){
    outStr+="\"ColorSaturation\":{";
    outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Contrast->Min)+"\", ";
    outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Contrast->Max)+"\"";
    outStr+="}, ";
  }
  if(optionsResponse->ImagingOptions->Exposure !=NULL){
    outStr+="\"Exposure\":{";
    if(optionsResponse->ImagingOptions->Exposure->Mode.size()>0){
      outStr+="\"Mode\":[\"AUTO\", \"MANUAL\"], ";
    }
    if(optionsResponse->ImagingOptions->Exposure->Priority.size()>0){
      outStr+="\"Mode\":[\"LowNoise\", \"FrameRate\"], ";
    }
    if(optionsResponse->ImagingOptions->Exposure->ExposureTime !=NULL){
      outStr+="\"ExposureTime\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->ExposureTime->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->ExposureTime->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->Exposure->Gain !=NULL){
      outStr+="\"Gain\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->Gain->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->Gain->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->Exposure->Iris !=NULL){
      outStr+="\"Iris\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->Iris->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->Iris->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->Exposure->MaxExposureTime !=NULL){
      outStr+="\"MaxExposureTime\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MaxExposureTime->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MaxExposureTime->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->Exposure->MaxGain !=NULL){
      outStr+="\"MaxGain\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MaxGain->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MaxGain->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->Exposure->MaxIris !=NULL){
      outStr+="\"MaxIris\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MaxIris->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MaxIris->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->Exposure->MinExposureTime !=NULL){
      outStr+="\"MinExposureTime\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MinExposureTime->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MinExposureTime->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->Exposure->MinGain !=NULL){
      outStr+="\"MinGain\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MinGain->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MinGain->Max)+"\"";
      outStr+="}, ";
    }
    if(optionsResponse->ImagingOptions->Exposure->MinIris !=NULL){
      outStr+="\"MinIris\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MinIris->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Exposure->MinIris->Max)+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(optionsResponse->ImagingOptions->Sharpness !=NULL){
    outStr+="\"Sharpness\":{";
    outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Sharpness->Min)+"\", ";
    outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Sharpness->Max)+"\"";
    outStr+="}, ";
  }
  if(optionsResponse->ImagingOptions->IrCutFilterModes.size() > 0){
    outStr+="\"IrCutFilterModes\":[\"ON\", \"OFF\", \"AUTO\"], ";
  }
  if(optionsResponse->ImagingOptions->Focus != NULL){
    outStr+="\"Focus\":{";
    if(optionsResponse->ImagingOptions->Focus->AutoFocusModes.size()>0){
      outStr+="\"AutoFocusModes\":[\"AUTO\", \"MANUAL\"], ";
    }
    if(optionsResponse->ImagingOptions->Focus->DefaultSpeed !=NULL){
      outStr+="\"DefaultSpeed\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Focus->DefaultSpeed->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Focus->DefaultSpeed->Max)+"\"";
      outStr+="}";
    }
    if(optionsResponse->ImagingOptions->Focus->FarLimit !=NULL){
      outStr+="\"FarLimit\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Focus->FarLimit->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Focus->FarLimit->Max)+"\"";
      outStr+="}";
    }
    if(optionsResponse->ImagingOptions->Focus->NearLimit !=NULL){
      outStr+="\"NearLimit\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->Focus->NearLimit->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->Focus->NearLimit->Max)+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(optionsResponse->ImagingOptions->WhiteBalance){
    outStr+="\"WhiteBalance\":{";
    if(optionsResponse->ImagingOptions->WhiteBalance->Mode.size()>0){
      outStr+="\"Mode\":[\"AUTO\", \"MANUAL\"], ";
    }
    if(optionsResponse->ImagingOptions->WhiteBalance->YbGain !=NULL){
      outStr+="\"YbGain\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->WhiteBalance->YbGain->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->WhiteBalance->YbGain->Max)+"\"";
      outStr+="}";
    }
    if(optionsResponse->ImagingOptions->WhiteBalance->YrGain !=NULL){
      outStr+="\"YrGain\":{";
      outStr+="\"Min\":\""+ std::to_string(optionsResponse->ImagingOptions->WhiteBalance->YrGain->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(optionsResponse->ImagingOptions->WhiteBalance->YrGain->Max)+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(optionsResponse->ImagingOptions->WideDynamicRange){
    outStr+="\"WideDynamicRange\":{";
    if(optionsResponse->ImagingOptions->WideDynamicRange->Mode.size()>0){
      outStr+="\"Mode\":[\"ON\", \"OFF\"], ";
    }
    if(optionsResponse->ImagingOptions->WideDynamicRange->Level!=NULL){
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

std::string prepareOSDOptionsResponse(_trt__GetOSDOptionsResponse* OSDOptionsResponse){
  std::string outStr="{\"status\":\"OK\", \"parameters\":{";
  // continue here
  if(OSDOptionsResponse->OSDOptions->TextOption!=NULL){
    outStr+="\"TextOption\":{";
    if(OSDOptionsResponse->OSDOptions->TextOption->BackgroundColor!=NULL){
      outStr+="\"BackgroundColor\":{";
      if(OSDOptionsResponse->OSDOptions->TextOption->BackgroundColor->Color!=NULL){
        outStr+="\"Color\":{";
        if (OSDOptionsResponse->OSDOptions->TextOption->BackgroundColor->Color->union_ColorOptions.ColorList !=NULL){
          outStr+="\"ColorList\":[";
          for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->TextOption->
                  BackgroundColor->Color->union_ColorOptions.ColorList->size(); i++){
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
        if (OSDOptionsResponse->OSDOptions->TextOption->
                  BackgroundColor->Color->union_ColorOptions.ColorspaceRange !=NULL){
          outStr+="\"ColorspaceRange\":[";
          for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->TextOption->
                      BackgroundColor->Color->union_ColorOptions.ColorspaceRange->size(); i++){
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
      if(OSDOptionsResponse->OSDOptions->TextOption->BackgroundColor->Transparent!=NULL){
        outStr+="\"Transparent\":{";
        outStr+="\"Min\":\""+
            std::to_string(OSDOptionsResponse->OSDOptions->TextOption->BackgroundColor->Transparent->Min)+"\", ";
        outStr+="\"Max\":\""+
            std::to_string(OSDOptionsResponse->OSDOptions->TextOption->BackgroundColor->Transparent->Max)+"\"";
        outStr+="}";
      }
      outStr+="}, ";
    }
    if(OSDOptionsResponse->OSDOptions->TextOption->DateFormat.size()>0){
      outStr+="\"DateFormat\":[";
      for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->TextOption->DateFormat.size(); i++){
        if(i!=0)outStr+=", ";
        outStr+="\""+OSDOptionsResponse->OSDOptions->TextOption->DateFormat[i]+"\"";
      }
      outStr+="], ";
    }
    if(OSDOptionsResponse->OSDOptions->TextOption->FontColor!=NULL){
      outStr+="\"FontColor\":{";
      if(OSDOptionsResponse->OSDOptions->TextOption->FontColor->Color!=NULL){
        outStr+="\"Color\":{";
        if (OSDOptionsResponse->OSDOptions->TextOption->FontColor->Color->union_ColorOptions.ColorList !=NULL){
          outStr+="\"ColorList\":[";
          for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->TextOption->
                  FontColor->Color->union_ColorOptions.ColorList->size(); i++){
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
        if (OSDOptionsResponse->OSDOptions->TextOption->
                  FontColor->Color->union_ColorOptions.ColorspaceRange !=NULL){
          outStr+="\"ColorspaceRange\":[";
          for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->TextOption->
                      FontColor->Color->union_ColorOptions.ColorspaceRange->size(); i++){
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
      if(OSDOptionsResponse->OSDOptions->TextOption->FontColor->Transparent!=NULL){
        outStr+="\"Transparent\":{";
        outStr+="\"Min\":\""+
            std::to_string(OSDOptionsResponse->OSDOptions->TextOption->FontColor->Transparent->Min)+"\", ";
        outStr+="\"Max\":\""+
            std::to_string(OSDOptionsResponse->OSDOptions->TextOption->FontColor->Transparent->Max)+"\"";
        outStr+="}";
      }
      outStr+="}, ";
    }

    if(OSDOptionsResponse->OSDOptions->TextOption->FontSizeRange!=NULL){
      outStr+="\"FontSizeRange\":{";
      outStr+="\"Min\":\""+
          std::to_string(OSDOptionsResponse->OSDOptions->TextOption->FontSizeRange->Min)+"\", ";
      outStr+="\"Max\":\""+
          std::to_string(OSDOptionsResponse->OSDOptions->TextOption->FontSizeRange->Max)+"\"";
      outStr+="}, ";
    }
    if(OSDOptionsResponse->OSDOptions->TextOption->TimeFormat.size()>0){
      outStr+="\"TimeFormat\":[";
      for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->TextOption->TimeFormat.size(); i++){
        if(i!=0)outStr+=", ";
        outStr+="\""+OSDOptionsResponse->OSDOptions->TextOption->TimeFormat[i]+"\"";
      }
      outStr+="], ";
    }
    if(OSDOptionsResponse->OSDOptions->TextOption->Type.size()>0){
      outStr+="\"Type\":[";
      for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->TextOption->Type.size(); i++){
        if(i!=0)outStr+=", ";
        outStr+="\""+OSDOptionsResponse->OSDOptions->TextOption->Type[i]+"\"";
      }
      outStr+="]";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(OSDOptionsResponse->OSDOptions->PositionOption.size()>0){
    outStr+="\"PositionOption\":[";
    for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->PositionOption.size(); i++){
      if(i!=0)outStr+=", ";
      outStr+="\""+OSDOptionsResponse->OSDOptions->PositionOption[i]+"\"";
    }
    outStr+="], ";
  }
  if(OSDOptionsResponse->OSDOptions->Type.size()>0){
    outStr+="\"Type\":[";
    for(unsigned i=0; i<OSDOptionsResponse->OSDOptions->Type.size(); i++){
      if(i!=0)outStr+=", ";
      switch(OSDOptionsResponse->OSDOptions->Type[i]){
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
  if(OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs!=NULL){
    outStr+="\"MaximumNumberOfOSDs\":{";
    if(OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->Date !=NULL){
      outStr+="\"Date\":\""+ std::to_string(*OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->Date)+"\", ";
    }
    if(OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->DateAndTime !=NULL){
      outStr+="\"DateAndTime\":\""+
        std::to_string(*OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->DateAndTime)+"\", ";
    }
    if(OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->Image !=NULL){
      outStr+="\"Image\":\""+
        std::to_string(*OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->Image)+"\", ";
    }
    if(OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->PlainText !=NULL){
      outStr+="\"PlainText\":\""+
        std::to_string(*OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->PlainText)+"\", ";
    }
    if(OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->Time !=NULL){
      outStr+="\"Time\":\""+ std::to_string(*OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->Time)+"\", ";
    }
    outStr+="\"Total\":\""+ std::to_string(OSDOptionsResponse->OSDOptions->MaximumNumberOfOSDs->Total)+"\"";
    outStr+="}, ";
  }

  if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
  outStr+="}}";
  return outStr;
}


std::string prepareMoveOptionsResponse(_timg__GetMoveOptionsResponse* moveOptionsResponse){
  std::string outStr="{\"status\":\"OK\", \"parameters\":{";
  if(moveOptionsResponse->MoveOptions->Absolute!=NULL){
    outStr+="\"Absolute\":{";
    if(moveOptionsResponse->MoveOptions->Absolute->Position !=NULL){
      outStr+="\"Position\":{";
      outStr+="\"Min\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Absolute->Position->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Absolute->Position->Max)+"\"";
      outStr+="}, ";
    }
    if(moveOptionsResponse->MoveOptions->Absolute->Speed!=NULL){
      outStr+="\"Speed\":{";
      outStr+="\"Min\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Absolute->Speed->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Absolute->Speed->Max)+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(moveOptionsResponse->MoveOptions->Relative!=NULL){
    outStr+="\"Relative\":{";
    if(moveOptionsResponse->MoveOptions->Relative->Distance !=NULL){
      outStr+="\"Distance\":{";
      outStr+="\"Min\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Relative->Distance->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Relative->Distance->Max)+"\"";
      outStr+="}, ";
    }
    if(moveOptionsResponse->MoveOptions->Relative->Speed!=NULL){
      outStr+="\"Speed\":{";
      outStr+="\"Min\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Relative->Speed->Min)+"\", ";
      outStr+="\"Max\":\""+ std::to_string(moveOptionsResponse->MoveOptions->Relative->Speed->Max)+"\"";
      outStr+="}";
    }
    if(outStr.at(outStr.length()-1)==' ') outStr.erase (outStr.length()-2, 2);
    outStr+="}, ";
  }
  if(moveOptionsResponse->MoveOptions->Continuous!=NULL){
    outStr+="\"Continuous\":{";
    if(moveOptionsResponse->MoveOptions->Continuous->Speed!=NULL){
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

std::string getTimeStamp(){
    time_t now;
    char the_date[MAX_DATE];
    the_date[0] = '\0';
    now = time(NULL);
    if (now != -1){
       strftime(the_date, MAX_DATE, "%d%b%y-%X", gmtime(&now));
    }
    return std::string(the_date);
}

const char * getLogTimestamp(){
    std::string rslt=getTimeStamp();
    return rslt.c_str();
}

void onTimer(){
  if(maxIdleTime){
    for(std::map<int, tClient*>::iterator it = clientConnections.begin(); it != clientConnections.end();){
        if(it->second == NULL) clientConnections.erase(it++);
        else{
            it->second->idleTime++;
            if(it->second->idleTime > maxIdleTime){
                close(it->second->srvSocket);
                clientConnections[it->second->srvSocket]=NULL;
                delete it->second;
            }
            ++it;
        }
    }
  }
}

void checkTime(){
    struct timeval current, timeout;
    int rc = gettimeofday(&current, 0 /* timezone unused */);
    if(rc != 0){
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

//##############################

int setupListenSocket(){
    struct sockaddr_in serv_addr;
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(listenIP.c_str());
    serv_addr.sin_port = htons(listenPort);

    int sockfd = socket(AF_INET , SOCK_STREAM , 0);
    if (sockfd  < 0){
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

    if (listen(sockfd, 5) < 0){
        perror("ERROR on listen");
        return -8;
    }
    return sockfd;
}

void setupClientSocket(int sockfd){
        tClient* tmpClient = new tClient();
        tmpClient->srvSocket=sockfd;
        tmpClient->data=NULL;
        clientConnections[sockfd]=tmpClient;
        if(verbosity>0)fprintf(stderr,"%s setupClientSocket: new client socket %d is connected \n",
                         getTimeStamp().c_str(), sockfd);
}

//################

void processReceivedData(int fd, std::string message, uint32_t messageID){
    rapidjson::Document d1;
    std::string command="";
    std::string outStr="{\"status\":\"ERROR\", \"reason\":\"Unknown command\"}";

    if (!d1.Parse(message.c_str()).HasParseError()) {
        if (d1.HasMember(CMD_COMMAND)){
          command=std::string(d1[CMD_COMMAND].GetString());
        }
        else{
          fprintf(stderr,"%s processReceivedData: Command not found\n", getTimeStamp().c_str());
          outStr="{\"status\":\"ERROR\", \"reason\":\"Command not found\"}";
          goto sendResponse;
        }
    }
    else{
        fprintf(stderr,"%s processReceivedData: Request parsing error\n", getTimeStamp().c_str());
        outStr="{\"status\":\"ERROR\", \"reason\":\"Request parsing error\"}";
        goto sendResponse;
    }
    if (verbosity>3) fprintf(stderr,"%s processReceivedData: : Executing command: %s\n",
                              getTimeStamp().c_str(), message.c_str());

    if(command=="GetImagingSettings") return execGetImagingSettings(fd, d1, messageID);
    if(command=="SetImagingSettings") return execSetImagingSettings(fd, d1, messageID);
    if(command=="GetOptions") return execGetOptions(fd, d1, messageID);
    if(command=="GetMoveOptions") return execGetMoveOptions(fd, d1, messageID);
    if(command=="Stop") return execStop(fd, d1, messageID);
    if(command=="Move") return execMove(fd, d1, messageID);
    if(command=="SystemReboot") return execSystemReboot(fd, d1, messageID);
    if(command=="GetOSDs") return execGetOSDs(fd, d1, messageID);
    if(command=="GetOSD") return execGetOSD(fd, d1, messageID);
    if(command=="SetOSD") return execSetOSD(fd, d1, messageID);
    if(command=="GetOSDOptions") return execGetOSDOptions(fd, d1, messageID);

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

int sendData(int fd, unsigned char *message, int mSize){
  int wrslt=1;
  do {
    wrslt = write(fd, message, mSize);
  } while ((wrslt < 0) && ((errno == EAGAIN) || (errno == EINTR)));
  if(wrslt <= 0){
    //handle error writing, close socket, set reconnect.
    return -18;
  }
  return 0;
}


int onDataReceived(int fd, unsigned char* data, uint32_t dataLen){
  tClient* tmpClient=findConnection(fd);
  if(!tmpClient) return -26;

  if(!dataLen) return -29;
  unsigned char *recMessage=NULL;
  int recSize=0;

  if (verbosity>3)fprintf(stderr,"%s onDataReceived received\n", getTimeStamp().c_str());
  pHeader tmpHeader;
  if(!tmpClient->data){
    tmpHeader=(pHeader)data;
    if(tmpHeader->marker!=ONVIF_PROT_MARKER){
      fprintf(stderr,"%s onDataReceived got corrupted data\n", getTimeStamp().c_str());
      return -28;
    }
    if (verbosity>3)fprintf(stderr,"%s onDataReceived Start received\n", getTimeStamp().c_str());
    tmpClient->totalSize=tmpHeader->dataLen+sHeader;
    tmpClient->currentSize=0;
    tmpClient->data=new unsigned char [tmpClient->totalSize+1];
    tmpClient->data[tmpClient->totalSize]=0;
  }
  if(tmpClient->totalSize < (tmpClient->currentSize + dataLen)){
    recSize= (tmpClient->currentSize + dataLen) - tmpClient->totalSize;
    recMessage=data + (dataLen-recSize);
    memcpy((tmpClient->data)+tmpClient->currentSize,data,(dataLen-recSize));
    tmpClient->currentSize=tmpClient->currentSize+(dataLen-recSize);
  }
  else{
    memcpy((tmpClient->data)+tmpClient->currentSize,data,dataLen);
    tmpClient->currentSize=tmpClient->currentSize+dataLen;
  }
  if(tmpClient->totalSize == tmpClient->currentSize){
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


void processRcvData(int fd){
    unsigned char buffer[BUFFER_LEN];
    int rslt=0;

//read data:
    do {
        rslt = read(fd, buffer, BUFFER_LEN);
    } while ((rslt < 0) && ((errno == EAGAIN) || (errno == EINTR)));

    if(rslt <= 0){
      onConnectionClosed(fd); //check for the error in the future
    }
    else{
      onDataReceived(fd, buffer, rslt);
    }

    if(rslt==BUFFER_LEN) processRcvData(fd);

}

int onConnectionClosed(int fd){
  tClient* tmpClient=findConnection(fd);
  if(!tmpClient) return -27;
  else return deleteConnection(tmpClient);
}

int deleteConnection(tClient* tmpClient){
  clientConnections[tmpClient->srvSocket]=NULL;
  close(tmpClient->srvSocket);
  if(tmpClient->data) delete[] tmpClient->data;
  delete tmpClient;
  return 0;
}

tClient* findConnection(int fd){
  std::map<int, tClient*>::iterator it = clientConnections.find(fd);
  if(it != clientConnections.end()){
    return clientConnections[fd];
  }
  return NULL;
}

int main(int argc, char **argv)
{

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


    for (int i=1; i<argc; i++){
      if(strcmp (argv[i],"-v") == 0){
        i++;
        verbosity=atoi(argv[i]);
      }
      else if(strcmp (argv[i],"-P") == 0){
        i++;
        listenPort=atoi(argv[i]);
      }
      else if(strcmp (argv[i],"-m") == 0){
        i++;
        maxIdleTime=atoi(argv[i]);
      }
      else if(strcmp (argv[i],"-p") == 0){
        i++;
        onvifPass=std::string((const char*)argv[i]);
      }
      else if(strcmp (argv[i],"-i") == 0){
        i++;
        cameraIP=std::string((const char*)argv[i]);
      }
      else if(strcmp (argv[i],"-l") == 0){
        i++;
        onvifLogin=std::string((const char*)argv[i]);
      }
      else if(strcmp (argv[i],"-L") == 0){
        i++;
        listenIP=std::string((const char*)argv[i]);
      }
      else usage();
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

    if(false == sendGetVideoSources(&proxyMedia, tmpGetVideoSources, tmpGetVideoSourcesResponse)) {
      if(verbosity>2)std::cout <<  "sendGetVideoSources failed all attempts" << std::endl;
      return -10;
    }

    soap_destroy(glSoap);
    soap_end(glSoap);

    if(videoSources.size() <1){
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

    if(false == sendGetOSDOptions(&proxyMedia, tmpGetOSDOptions, tmpGetOSDOptionsResponse)) {
      if(verbosity>2)std::cout <<  "sendGetOSDOptions failed all attempts" << std::endl;
      return -12;
    }

    cachedOSDOptionsResponse=prepareOSDOptionsResponse(tmpGetOSDOptionsResponse);

    soap_destroy(glSoap);
    soap_end(glSoap);


//Imaging Options


    if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyImaging.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
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

    if(false == sendGetOptions(&proxyImaging, tmpGetOptions, tmpGetOptionsResponse)) {
      if(verbosity>2)std::cout <<  "sendGetOptions failed all attempts" << std::endl;
      return -12;
    }

    cachedImagingOptionsResponse=prepareOptionsResponse(tmpGetOptionsResponse);

    soap_destroy(glSoap);
    soap_end(glSoap);

//Imaging move options
    if (SOAP_OK != soap_wsse_add_UsernameTokenDigest(proxyImaging.soap, NULL, onvifLogin.c_str(), onvifPass.c_str())) {
      std::cout << "Failed to assign user:password" << std::endl;
      return -1;
    }


    _timg__GetMoveOptions *tmpGetMoveOptions = soap_new__timg__GetMoveOptions(glSoap, -1);
    _timg__GetMoveOptionsResponse* tmpGetMoveOptionsResponse = soap_new__timg__GetMoveOptionsResponse(glSoap, -1);
    tmpGetMoveOptions->VideoSourceToken=videoSources[0];

    if(false == sendGetMoveOptions(&proxyImaging, tmpGetMoveOptions, tmpGetMoveOptionsResponse)) {
      if(verbosity>2)std::cout <<  "sendGetMoveOptions failed all attempts" << std::endl;
      return -11;
    }

    cachedMoveOptionsResponse=prepareMoveOptionsResponse(tmpGetMoveOptionsResponse);

    soap_destroy(glSoap);
    soap_end(glSoap);

//Network setup
    int  maxfd, sockfd, clifd;
    sockfd=setupListenSocket();
    if(sockfd < 0) exit(sockfd);

//Timer setup
    int rc = gettimeofday(&startTime, 0 /* timezone unused */);
    if(rc){
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
    while(1){


// Prepare for select:
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        maxfd = sockfd;


        for(std::map<int, tClient*>::iterator it = clientConnections.begin(); it != clientConnections.end();){
            if(it->second == NULL) clientConnections.erase(it++);
            else{
                FD_SET(it->first, &readfds);
                maxfd = (maxfd < it->first)?it->first:maxfd;
                ++it;
            }
        }

        nready = select(maxfd+1, &readfds, NULL, NULL, &tv);

//Check select results:

        if (nready < 0 && errno != EINTR) {
            if (verbosity>0) cerr << getTimeStamp() << "Error in select():"  << strerror(errno) << endl;
        }
        else if (nready == 0) {
            if (verbosity>4) cerr << getTimeStamp() << "select() timed out!"  <<  endl;
        }
        else if (nready > 0) {
//////////////////// Process select results
            if (FD_ISSET(sockfd, &readfds)) {
               if (verbosity>1) cerr << getTimeStamp() << "New connection request:"  << endl;
               clifd = accept(sockfd, (struct sockaddr *)&cli_addr, (socklen_t *) &clilen);
               if (clifd < 0) {
                   if (verbosity>0) cerr << getTimeStamp() << "Error in accept():" << strerror(errno) << endl;
               }
               else {
                   if (verbosity>1) cerr << getTimeStamp() << "Accepted new connection:" << clifd
                      << ", from IP: " << std::string(inet_ntoa(cli_addr.sin_addr))
                      << ", port: " << std::to_string(htons(cli_addr.sin_port)) << endl;
                   setupClientSocket(clifd);
               }
            }
            else {
                bool found=false;
                for(std::map<int, tClient*>::iterator it = clientConnections.begin();
                                                        it != clientConnections.end();){
                    if(it->second == NULL) clientConnections.erase(it++);
                    else{
                        if (FD_ISSET(it->first, &readfds)){
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
