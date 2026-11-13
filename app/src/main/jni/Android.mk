# Para compilar no AIDE (ndk-build). O Android Studio/GitHub usa o CMakeLists.txt.
LOCAL_PATH := $(call my-dir)/../cpp
include $(CLEAR_VARS)
LOCAL_MODULE    := xlsmpdf
LOCAL_SRC_FILES := jni_bridge.cpp xlsx2pdf.cpp pdfmerge.cpp
LOCAL_CPPFLAGS  := -std=c++17 -O2 -fexceptions -frtti
LOCAL_LDLIBS    := -lz -llog
include $(BUILD_SHARED_LIBRARY)
