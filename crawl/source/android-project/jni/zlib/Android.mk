LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := zlib 

LOCAL_C_INCLUDES := $(LOCAL_PATH)/src $(LOCAL_PATH)/include 
LOCAL_CFLAGS := -O3

LOCAL_CPP_EXTENSION := .cpp

LOCAL_SRC_FILES := \
    adler32.c crc32.c deflate.c infback.c inffast.c inflate.c \
    inftrees.c trees.c zutil.c compress.c uncompr.c gzclose.c \
    gzlib.c gzread.c gzwrite.c

LOCAL_STATIC_LIBRARIES :=

LOCAL_SHARED_LIBRARIES :=

LOCAL_LDLIBS :=

include $(BUILD_SHARED_LIBRARY)

