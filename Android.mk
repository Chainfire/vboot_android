# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Minimal makefile capable of compiling futility to sign images

# Adjusted for Android target build

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

ifeq ($(HOST_OS),darwin)
LOCAL_CFLAGS += -DHAVE_MACOS -DO_LARGEFILE=0
endif

# These are required to access large disks and files on 32-bit systems.
LOCAL_CFLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64

LOCAL_C_INCLUDES += \
	$(LOCAL_PATH)/firmware/include \
	$(LOCAL_PATH)/firmware/lib/include \
	$(LOCAL_PATH)/firmware/lib/cgptlib/include \
	$(LOCAL_PATH)/firmware/lib/cryptolib/include \
	$(LOCAL_PATH)/firmware/lib/tpm_lite/include \
	$(LOCAL_PATH)/firmware/2lib/include \
	$(LOCAL_PATH)/host/include \
	$(LOCAL_PATH)/host/lib/include

# Firmware library sources needed by VbInit() call
VBINIT_SRCS = \
	firmware/lib/crc8.c \
	firmware/lib/utility.c \
	firmware/lib/vboot_api_init.c \
	firmware/lib/vboot_common_init.c \
	firmware/lib/vboot_nvstorage.c \
	firmware/lib/vboot_nvstorage_rollback.c \
	firmware/lib/region-init.c \

# Additional firmware library sources needed by VbSelectFirmware() call
VBSF_SRCS = \
	firmware/lib/cryptolib/padding.c \
	firmware/lib/cryptolib/rsa.c \
	firmware/lib/cryptolib/rsa_utility.c \
	firmware/lib/cryptolib/sha1.c \
	firmware/lib/cryptolib/sha256.c \
	firmware/lib/cryptolib/sha512.c \
	firmware/lib/cryptolib/sha_utility.c \
	firmware/lib/stateful_util.c \
	firmware/lib/vboot_api_firmware.c \
	firmware/lib/vboot_common.c \
	firmware/lib/vboot_firmware.c \
	firmware/lib/region-fw.c \

# Additional firmware library sources needed by VbSelectAndLoadKernel() call
VBSLK_SRCS = \
	firmware/lib/cgptlib/cgptlib.c \
	firmware/lib/cgptlib/cgptlib_internal.c \
	firmware/lib/cgptlib/crc32.c \
	firmware/lib/gpt_misc.c \
	firmware/lib/utility_string.c \
	firmware/lib/vboot_api_kernel.c \
	firmware/lib/vboot_audio.c \
	firmware/lib/vboot_display.c \
	firmware/lib/vboot_kernel.c \
	firmware/lib/region-kernel.c \

VBINIT_SRCS += \
	firmware/stub/tpm_lite_stub.c \
	firmware/stub/utility_stub.c \
	firmware/stub/vboot_api_stub_init.c \
	firmware/stub/vboot_api_stub_region.c

VBSF_SRCS += \
	firmware/stub/vboot_api_stub_sf.c

VBSLK_SRCS += \
	firmware/stub/vboot_api_stub.c \
	firmware/stub/vboot_api_stub_disk.c \
	firmware/stub/vboot_api_stub_stream.c

UTILLIB_SRCS = \
	cgpt/cgpt_create.c \
	cgpt/cgpt_add.c \
	cgpt/cgpt_boot.c \
	cgpt/cgpt_show.c \
	cgpt/cgpt_repair.c \
	cgpt/cgpt_prioritize.c \
	cgpt/cgpt_common.c \
	futility/dump_kernel_config_lib.c \
	host/lib/crossystem.c \
	host/lib/file_keys.c \
	host/lib/fmap.c \
	host/lib/host_common.c \
	host/lib/host_key.c \
	host/lib/host_keyblock.c \
	host/lib/host_misc.c \
	host/lib/util_misc.c \
	host/lib/host_signature.c \
	host/lib/signature_digest.c

FUTIL_STATIC_SRCS = \
	futility/futility.c \
	futility/cmd_dump_fmap.c \
	futility/cmd_gbb_utility.c \
	futility/misc.c

FUTIL_SRCS = \
	${FUTIL_STATIC_SRCS} \
	futility/cmd_dump_kernel_config.c \
	futility/cmd_load_fmap.c \
	futility/cmd_pcr.c \
	futility/cmd_show.c \
	futility/cmd_sign.c \
	futility/cmd_vbutil_firmware.c \
	futility/cmd_vbutil_kernel.c \
	futility/cmd_vbutil_key.c \
	futility/cmd_vbutil_keyblock.c \
	futility/file_type.c \
	futility/traversal.c \
	futility/vb1_helper.c

LOCAL_SRC_FILES := \
	$(VBINIT_SRCS) \
	$(VBSF_SRCS) \
	$(VBSLK_SRCS) \
	$(UTILLIB_SRCS) \
	$(FUTIL_SRCS) \
        futility/futility_cmds.c \
        execinfo.c

LOCAL_MODULE := futility-android
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_STATIC_LIBRARIES := libcrypto_static
LOCAL_FORCE_STATIC_EXECUTABLE := true
include $(BUILD_EXECUTABLE)

