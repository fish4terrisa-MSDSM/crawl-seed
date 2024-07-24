LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := freetype

APP_SUBDIRS := $(patsubst $(LOCAL_PATH)/%, %, $(shell find $(LOCAL_PATH)/src -type d))

LOCAL_C_INCLUDES := $(foreach D, $(APP_SUBDIRS), $(LOCAL_PATH)/$(D)) $(LOCAL_PATH)/include
LOCAL_CFLAGS := -Os -DFT2_BUILD_LIBRARY


LOCAL_CPP_EXTENSION := .cpp

LOCAL_SRC_FILES := \
    src/base/ftbase.c \
    src/base/ftbbox.c \
	src/base/ftbdf.c \
	src/base/ftbitmap.c \
    src/base/ftdebug.c \
	src/base/ftfstype.c \
    src/base/ftglyph.c \
	src/base/ftgasp.c \
    src/base/ftinit.c \
    src/base/ftsystem.c \
	src/autofit/autofit.c \
	src/cache/ftcache.c \
	src/gzip/ftgzip.c \
	src/sfnt/sfnt.c \
	src/smooth/ftgrays.c \
	src/smooth/ftsmooth.c \
	src/truetype/truetype.c

LOCAL_SHARED_LIBRARIES := 

LOCAL_STATIC_LIBRARIES := 

LOCAL_LDLIBS :=

include $(BUILD_STATIC_LIBRARY)
