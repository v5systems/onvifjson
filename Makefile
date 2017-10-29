CC = g++
#To make debug build
#CPPFLAG = -Wall -std=c++11 -g -w -fPIC -DWITH_NONAMESPACES -fno-use-cxa-atexit -fexceptions -DWITH_DOM  -DWITH_OPENSSL -DSOAP_DEBUG

#To make normal release build
CPPFLAG = -Wall -O2 -std=c++11 -w -fPIC -DWITH_NONAMESPACES -fno-use-cxa-atexit -fexceptions -DWITH_DOM  -DWITH_OPENSSL

BASE_DIR=.
SOURCE=$(BASE_DIR)
INCLUDE +=-I$(SOURCE)/include -I$(BASE_DIR)
LIB= -lssl -lcrypto

PROXYSOURCE=$(BASE_DIR)/proxy
ProxyOBJ=$(PROXYSOURCE)/soapDeviceBindingProxy.o $(PROXYSOURCE)/soapMediaBindingProxy.o \
  $(PROXYSOURCE)/soapImagingBindingProxy.o $(PROXYSOURCE)/soapPTZBindingProxy.o

PluginSOURCE=$(BASE_DIR)/plugin
PluginOBJ=$(PluginSOURCE)/wsseapi.o $(PluginSOURCE)/httpda.o \
  $(PluginSOURCE)/smdevp.o $(PluginSOURCE)/mecevp.o $(PluginSOURCE)/wsaapi.o

SRC= $(SOURCE)/stdsoap2.o $(SOURCE)/duration.o $(SOURCE)/dom.o $(SOURCE)/soapC.o $(SOURCE)/main.o $(PluginOBJ) $(ProxyOBJ)

OBJECTS = $(patsubst %.cpp,%.o,$(SRC))
TARGET=onvifjson
all: $(TARGET)
$(TARGET):$(OBJECTS)
	$(CC) $(CPPFLAG) $(OBJECTS)  $(INCLUDE)  $(LIB) -o $(TARGET)
$(OBJECTS):%.o : %.cpp
	$(CC) -c $(CPPFLAG) $(INCLUDE) $< -o $@
clean:
	rm -rf  $(OBJECTS)

