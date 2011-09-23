/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * High-level firmware wrapper API - entry points for kernel selection
 */

#include "gbb_header.h"
#include "load_kernel_fw.h"
#include "rollback_index.h"
#include "utility.h"
#include "vboot_api.h"
#include "vboot_common.h"
#include "vboot_display.h"
#include "vboot_nvstorage.h"


/* Global variables */
static VbNvContext vnc;


#ifdef CHROMEOS_ENVIRONMENT
/* Global variable accessors for unit tests */
VbNvContext* VbApiKernelGetVnc(void) {
  return &vnc;
}
#endif


/* Set recovery request */
static void VbSetRecoveryRequest(uint32_t recovery_request) {
  VBDEBUG(("VbSetRecoveryRequest(%d)\n", (int)recovery_request));
  VbNvSet(&vnc, VBNV_RECOVERY_REQUEST, recovery_request);
}



/* Attempt loading a kernel from the specified type(s) of disks.  If
 * successful, sets p->disk_handle to the disk for the kernel and returns
 * VBERROR_SUCCESS.
 *
 * Returns VBERROR_NO_DISK_FOUND if no disks of the specified type were found.
 *
 * May return other VBERROR_ codes for other failures. */
uint32_t VbTryLoadKernel(VbCommonParams* cparams, LoadKernelParams* p,
                         uint32_t get_info_flags) {
  VbError_t retval = VBERROR_UNKNOWN;
  VbDiskInfo* disk_info = NULL;
  uint32_t disk_count = 0;
  uint32_t i;

  VBDEBUG(("VbTryLoadKernel() start, get_info_flags=0x%x\n",
          (unsigned)get_info_flags));

  p->disk_handle = NULL;

  /* Find disks */
  if (VBERROR_SUCCESS != VbExDiskGetInfo(&disk_info, &disk_count,
                                         get_info_flags))
    disk_count = 0;

  VBDEBUG(("VbTryLoadKernel() found %d disks\n", (int)disk_count));
  if (0 == disk_count) {
    VbSetRecoveryRequest(VBNV_RECOVERY_RW_NO_DISK);
    return VBERROR_NO_DISK_FOUND;
  }

  /* Loop over disks */
  for (i = 0; i < disk_count; i++) {
    VBDEBUG(("VbTryLoadKernel() trying disk %d\n", (int)i));
    p->disk_handle = disk_info[i].handle;
    p->bytes_per_lba = disk_info[i].bytes_per_lba;
    p->ending_lba = disk_info[i].lba_count - 1;
    retval = LoadKernel(p);
    VBDEBUG(("VbTryLoadKernel() LoadKernel() returned %d\n", retval));

    /* Stop now if we found a kernel */
    /* TODO: If recovery requested, should track the farthest we get, instead
     * of just returning the value from the last disk attempted. */
    if (VBERROR_SUCCESS == retval)
      break;
  }

  /* If we didn't succeed, don't return a disk handle */
  if (VBERROR_SUCCESS != retval)
    p->disk_handle = NULL;

  VbExDiskFreeInfo(disk_info, p->disk_handle);

  /* Pass through return code.  Recovery reason (if any) has already been set
   * by LoadKernel(). */
  return retval;
}


/* Handle a normal boot. */
VbError_t VbBootNormal(VbCommonParams* cparams, LoadKernelParams* p) {

  /* Force dev_boot_usb flag disabled.  This ensures the flag will be
   * initially disabled if the user later transitions back into
   * developer mode. */
  VbNvSet(&vnc, VBNV_DEV_BOOT_USB, 0);

  /* Boot from fixed disk only */
  return VbTryLoadKernel(cparams, p, VB_DISK_FLAG_FIXED);
}

#define DEV_LOOP_TIME 10  /* Minimum note granularity in msecs */

static uint16_t VbMsecToLoops(uint16_t msec) {
  return (DEV_LOOP_TIME / 2 + msec) / DEV_LOOP_TIME;
}

static VbDevMusicNote default_notes[] = { {20000, 0}, /* 20 seconds */
                                          {250, 400}, /* two beeps */
                                          {250, 0},
                                          {250, 400},
                                          {9250, 0} }; /* total 30 seconds */

static VbDevMusicNote short_notes[] = { {2000, 0} };   /* two seconds */

/* Return a valid set of note events. */
static VbDevMusicNote* VbGetDevMusicNotes(uint32_t *count, int use_short) {

  if (use_short) {
    *count = sizeof(short_notes) / sizeof(short_notes[0]);
    return short_notes;
  }

  *count = sizeof(default_notes) / sizeof(default_notes[0]);
  return default_notes;
}


/* Handle a developer-mode boot */
VbError_t VbBootDeveloper(VbCommonParams* cparams, LoadKernelParams* p) {
  GoogleBinaryBlockHeader* gbb = (GoogleBinaryBlockHeader*)cparams->gbb_data;
  uint32_t allow_usb = 0;
  uint32_t note_count = 0;
  VbDevMusicNote* music_notes = 0;
  uint32_t current_note = 0;
  uint32_t current_note_loops = 0;
  int background_beep = 1;

  /* Check if USB booting is allowed */
  VbNvGet(&vnc, VBNV_DEV_BOOT_USB, &allow_usb);

  /* Show the dev mode warning screen */
  VbDisplayScreen(cparams, VB_SCREEN_DEVELOPER_WARNING, 0, &vnc);

  /* See if we have full background sound capability or not. */
  if (VBERROR_SUCCESS != VbExBeep(0,0)) {
    VBDEBUG(("VbBootDeveloper: VbExBeep() is limited\n"));
    background_beep = 0;
  }

  /* Prepare to generate audio/delay event. Use a short developer screen delay
   * if indicated by GBB flags.
   */
  if (gbb->major_version == GBB_MAJOR_VER && gbb->minor_version >= 1
      && (gbb->flags & GBB_FLAG_DEV_SCREEN_SHORT_DELAY)) {
    VBDEBUG(("VbBootDeveloper() - using short developer screen delay\n"));
    music_notes = VbGetDevMusicNotes(&note_count, 1);
  } else {
    music_notes = VbGetDevMusicNotes(&note_count, 0);
  }

  VBDEBUG(("VbBootDeveloper() - note count %d\n", note_count));

  /* We'll loop until we finish the notes or are interrupted */
  while(1) {
    uint32_t key;

    if (VbExIsShutdownRequested())
      return VBERROR_SHUTDOWN_REQUESTED;

    key = VbExKeyboardRead();
    switch (key) {
      case 0:
        /* nothing pressed */
        break;
      case '\r':
      case ' ':
      case 0x1B:
        /* Enter, space, or ESC = reboot to recovery */
        VBDEBUG(("VbBootDeveloper() - user pressed ENTER/SPACE/ESC\n"));
        VbExBeep(0, 0);                /* sound off */
        VbSetRecoveryRequest(VBNV_RECOVERY_RW_DEV_SCREEN);
        return 1;
      case 0x04:
        /* Ctrl+D = dismiss warning; advance to timeout */
        VBDEBUG(("VbBootDeveloper() - user pressed Ctrl+D; skip delay\n"));
        goto fallout;
        break;
      case 0x15:
        /* Ctrl+U = try USB boot, or beep if failure */
        VBDEBUG(("VbBootDeveloper() - user pressed Ctrl+U; try USB\n"));
        VbExBeep(0, 0);                /* sound off */
        if (!allow_usb) {
          VBDEBUG(("VbBootDeveloper() - USB booting is disabled\n"));
          VbExBeep(120, 400);
          VbExSleepMs(120);
          VbExBeep(120, 400);
        } else if (VBERROR_SUCCESS ==
                   VbTryLoadKernel(cparams, p, VB_DISK_FLAG_REMOVABLE)) {
          VBDEBUG(("VbBootDeveloper() - booting USB\n"));
          return VBERROR_SUCCESS;
        } else {
          VBDEBUG(("VbBootDeveloper() - no kernel found on USB\n"));
          VbExBeep(250, 200);
          VbExBeep(100, 0);
          /* Clear recovery requests from failed kernel loading, so
           * that powering off at this point doesn't put us into
           * recovery mode. */
          VbSetRecoveryRequest(VBNV_RECOVERY_NOT_REQUESTED);
        }
        break;
      default:
        VbCheckDisplayKey(cparams, key, &vnc);
        break;
    }

    /* Time to play a note? */
    if (!current_note_loops) {
      VBDEBUG(("VbBootDeveloper() - current_note is %d\n", current_note));

      /* Sorry, out of notes */
      if (current_note >= note_count)
        break;

      /* For how many loops do we hold this note? */
      current_note_loops = VbMsecToLoops(music_notes[current_note].msec);
      VBDEBUG(("VbBootDeveloper() - new current_note_loops == %d\n",
               current_note_loops));

      if (background_beep) {

        /* start (or stop) the sound */
        VbExBeep(0, music_notes[current_note].frequency);

      } else if (music_notes[current_note].frequency) {

        /* the sound will block, so don't loop repeatedly */
        current_note_loops = 1;
        VbExBeep(music_notes[current_note].msec,
                 music_notes[current_note].frequency);
      }

      current_note++;
    }

    /* Wait a bit. Yes, one extra loop sometimes, but it's only 10msec */
    VbExSleepMs(DEV_LOOP_TIME);

    /* That's one... */
    if (current_note_loops)
      current_note_loops--;
  }

fallout:
  /* Timeout or Ctrl+D; attempt loading from fixed disk */
  VbExBeep(0, 0);                /* sound off */
  VBDEBUG(("VbBootDeveloper() - trying fixed disk\n"));
  return VbTryLoadKernel(cparams, p, VB_DISK_FLAG_FIXED);
}


/* Delay between disk checks in recovery mode */
#define REC_DELAY_INCREMENT 250

/* Handle a recovery-mode boot */
VbError_t VbBootRecovery(VbCommonParams* cparams, LoadKernelParams* p) {
  VbSharedDataHeader* shared = (VbSharedDataHeader*)cparams->shared_data_blob;
  uint32_t retval;
  int i;

  VBDEBUG(("VbBootRecovery() start\n"));

  /* If dev mode switch is off, require removal of all external media. */
  if (!(shared->flags & VBSD_BOOT_DEV_SWITCH_ON)) {
    VbDiskInfo* disk_info = NULL;
    uint32_t disk_count = 0;

    VBDEBUG(("VbBootRecovery() forcing device removal\n"));

    while (1) {
      if (VBERROR_SUCCESS != VbExDiskGetInfo(&disk_info, &disk_count,
          VB_DISK_FLAG_REMOVABLE))
        disk_count = 0;
      VbExDiskFreeInfo(disk_info, NULL);

      if (0 == disk_count) {
        VbDisplayScreen(cparams, VB_SCREEN_BLANK, 0, &vnc);
        break;
      }

      VBDEBUG(("VbBootRecovery() waiting for %d disks to be removed\n",
               (int)disk_count));

      VbDisplayScreen(cparams, VB_SCREEN_RECOVERY_REMOVE, 0, &vnc);

      /* Scan keyboard more frequently than media, since x86 platforms
       * don't like to scan USB too rapidly. */
      for (i = 0; i < 4; i++) {
        VbCheckDisplayKey(cparams, VbExKeyboardRead(), &vnc);
        if (VbExIsShutdownRequested())
          return VBERROR_SHUTDOWN_REQUESTED;
        VbExSleepMs(REC_DELAY_INCREMENT);
      }
    }
  }

  /* Loop and wait for a recovery image */
  while (1) {
    VBDEBUG(("VbBootRecovery() attempting to load kernel\n"));
    retval = VbTryLoadKernel(cparams, p, VB_DISK_FLAG_REMOVABLE);

    /* Clear recovery requests from failed kernel loading, since we're
     * already in recovery mode.  Do this now, so that powering off after
     * inserting an invalid disk doesn't leave us stuck in recovery mode. */
    VbSetRecoveryRequest(VBNV_RECOVERY_NOT_REQUESTED);

    if (VBERROR_SUCCESS == retval)
      break;  /* Found a recovery kernel */

    VbDisplayScreen(cparams, VBERROR_NO_DISK_FOUND == retval ?
                    VB_SCREEN_RECOVERY_INSERT : VB_SCREEN_RECOVERY_NO_GOOD,
                    0, &vnc);

    /* Scan keyboard more frequently than media, since x86 platforms don't like
     * to scan USB too rapidly. */
    for (i = 0; i < 4; i++) {
      VbCheckDisplayKey(cparams, VbExKeyboardRead(), &vnc);
      if (VbExIsShutdownRequested())
        return VBERROR_SHUTDOWN_REQUESTED;
      VbExSleepMs(REC_DELAY_INCREMENT);
    }
  }

  return VBERROR_SUCCESS;
}


VbError_t VbSelectAndLoadKernel(VbCommonParams* cparams,
                                VbSelectAndLoadKernelParams* kparams) {
  VbSharedDataHeader* shared = (VbSharedDataHeader*)cparams->shared_data_blob;
  VbError_t retval = VBERROR_SUCCESS;
  LoadKernelParams p;
  uint32_t tpm_status = 0;

  VBDEBUG(("VbSelectAndLoadKernel() start\n"));

  /* Start timer */
  shared->timer_vb_select_and_load_kernel_enter = VbExGetTimer();

  VbExNvStorageRead(vnc.raw);
  VbNvSetup(&vnc);

  /* Clear output params in case we fail */
  kparams->disk_handle = NULL;
  kparams->partition_number = 0;
  kparams->bootloader_address = 0;
  kparams->bootloader_size = 0;
  Memset(kparams->partition_guid, 0, sizeof(kparams->partition_guid));

  /* Read the kernel version from the TPM.  Ignore errors in recovery mode. */
  tpm_status = RollbackKernelRead(&shared->kernel_version_tpm);
  if (0 != tpm_status) {
    VBDEBUG(("Unable to get kernel versions from TPM\n"));
    if (!shared->recovery_reason) {
      VbSetRecoveryRequest(VBNV_RECOVERY_RW_TPM_ERROR);
      retval = VBERROR_TPM_READ_KERNEL;
      goto VbSelectAndLoadKernel_exit;
    }
  }
  shared->kernel_version_tpm_start = shared->kernel_version_tpm;

  /* Fill in params for calls to LoadKernel() */
  Memset(&p, 0, sizeof(p));
  p.shared_data_blob = cparams->shared_data_blob;
  p.shared_data_size = cparams->shared_data_size;
  p.gbb_data = cparams->gbb_data;
  p.gbb_size = cparams->gbb_size;
  p.kernel_buffer = kparams->kernel_buffer;
  p.kernel_buffer_size = kparams->kernel_buffer_size;
  p.nv_context = &vnc;
  p.boot_flags = 0;
  if (shared->flags & VBSD_BOOT_DEV_SWITCH_ON)
    p.boot_flags |= BOOT_FLAG_DEVELOPER;

  /* Handle separate normal and developer firmware builds. */
#if defined(VBOOT_FIRMWARE_TYPE_NORMAL)
  /* Normal-type firmware always acts like the dev switch is off. */
  p.boot_flags &= ~BOOT_FLAG_DEVELOPER;
#elif defined(VBOOT_FIRMWARE_TYPE_DEVELOPER)
  /* Developer-type firmware fails if the dev switch is off. */
  if (!(p.boot_flags & BOOT_FLAG_DEVELOPER)) {
    /* Dev firmware should be signed with a key that only verifies
     * when the dev switch is on, so we should never get here. */
    VBDEBUG(("Developer firmware called with dev switch off!\n"));
    VbSetRecoveryRequest(VBNV_RECOVERY_RW_DEV_MISMATCH);
    retval = VBERROR_DEV_FIRMWARE_SWITCH_MISMATCH;
    goto VbSelectAndLoadKernel_exit;
  }
#else
  /* Recovery firmware, or merged normal+developer firmware.  No
   * need to override flags. */
#endif

  /* Select boot path */
  if (shared->recovery_reason) {
    /* Recovery boot */
    p.boot_flags |= BOOT_FLAG_RECOVERY;
    retval = VbBootRecovery(cparams, &p);
    VbDisplayScreen(cparams, VB_SCREEN_BLANK, 0, &vnc);

  } else if (p.boot_flags & BOOT_FLAG_DEVELOPER) {
    /* Developer boot */
    retval = VbBootDeveloper(cparams, &p);
    VbDisplayScreen(cparams, VB_SCREEN_BLANK, 0, &vnc);

  } else {
    /* Normal boot */
    retval = VbBootNormal(cparams, &p);

    if ((1 == shared->firmware_index) && (shared->flags & VBSD_FWB_TRIED)) {
      /* Special cases for when we're trying a new firmware B.  These are
       * needed because firmware updates also usually change the kernel key,
       * which means that the B firmware can only boot a new kernel, and the
       * old firmware in A can only boot the previous kernel. */

      /* Don't advance the TPM if we're trying a new firmware B, because we
       * don't yet know if the new kernel will successfully boot.  We still
       * want to be able to fall back to the previous firmware+kernel if the
       * new firmware+kernel fails. */

      /* If we found only invalid kernels, reboot and try again.  This allows
       * us to fall back to the previous firmware+kernel instead of giving up
       * and going to recovery mode right away.  We'll still go to recovery
       * mode if we run out of tries and the old firmware can't find a kernel
       * it likes. */
      if (VBERROR_INVALID_KERNEL_FOUND == retval) {
        VBDEBUG(("Trying firmware B, and only found invalid kernels.\n"));
        VbSetRecoveryRequest(VBNV_RECOVERY_NOT_REQUESTED);
        goto VbSelectAndLoadKernel_exit;
      }
    } else {
      /* Not trying a new firmware B. */
      /* See if we need to update the TPM. */
      VBDEBUG(("Checking if TPM kernel version needs advancing\n"));
      if (shared->kernel_version_tpm > shared->kernel_version_tpm_start) {
        tpm_status = RollbackKernelWrite(shared->kernel_version_tpm);
        if (0 != tpm_status) {
          VBDEBUG(("Error writing kernel versions to TPM.\n"));
          VbSetRecoveryRequest(VBNV_RECOVERY_RW_TPM_ERROR);
          retval = VBERROR_TPM_WRITE_KERNEL;
          goto VbSelectAndLoadKernel_exit;
        }
      }
    }
  }

  if (VBERROR_SUCCESS != retval)
    goto VbSelectAndLoadKernel_exit;

  /* Save disk parameters */
  kparams->disk_handle = p.disk_handle;
  kparams->partition_number = (uint32_t)p.partition_number;
  kparams->bootloader_address = p.bootloader_address;
  kparams->bootloader_size = (uint32_t)p.bootloader_size;
  Memcpy(kparams->partition_guid, p.partition_guid,
         sizeof(kparams->partition_guid));

  /* Lock the kernel versions.  Ignore errors in recovery mode. */
  tpm_status = RollbackKernelLock();
  if (0 != tpm_status) {
    VBDEBUG(("Error locking kernel versions.\n"));
    if (!shared->recovery_reason) {
      VbSetRecoveryRequest(VBNV_RECOVERY_RW_TPM_ERROR);
      retval = VBERROR_TPM_LOCK_KERNEL;
      goto VbSelectAndLoadKernel_exit;
    }
  }

VbSelectAndLoadKernel_exit:

  VbNvTeardown(&vnc);
  if (vnc.raw_changed)
    VbExNvStorageWrite(vnc.raw);

  /* Stop timer */
  shared->timer_vb_select_and_load_kernel_exit = VbExGetTimer();

  VBDEBUG(("VbSelectAndLoadKernel() returning %d\n", (int)retval));

  /* Pass through return value from boot path */
  return retval;
}
