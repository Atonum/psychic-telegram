#
# Makefile for TI's shared transport line discipline
# and its protocol drivers (BT, FM, GPS)
#
obj-$(CONFIG_FPR_SYN) 		+= vfsSpiDrv.o

INCLUDE_DIR += -I$(PWD)
include $(srctree)/drivers/misc/mediatek/Makefile.custom
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/mach/$(MTK_PLATFORM)/include

EXTRA_CFLAGS += $(INCLUDE_DIR) -DPLATFORM_BIG_ENDIAN=1 -DVCS_FEATURE_SENSOR_WINDSOR=0

#ifeq (${BUILDTYPE}, Debug)
#	EXTRA_CFLAGS += -DDEBUG=0
#else
#	EXTRA_CFLAGS += -DDEBUG=0
#endif

