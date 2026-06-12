/* util.c -- misc utility functions
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "util.h"
#include "config.h"

#ifdef DEBUG_LOG

static int s_nxlinkSock = -1;

static void initNxLink(void) {
  if (R_FAILED(socketInitializeDefault()))
    return;
  s_nxlinkSock = nxlinkStdio();
  if (s_nxlinkSock < 0)
    socketExit();
}

static void deinitNxLink(void) {
  if (s_nxlinkSock >= 0) {
    close(s_nxlinkSock);
    socketExit();
    s_nxlinkSock = -1;
  }
}

void userAppInit(void) {
  initNxLink();
}

void userAppExit(void) {
  deinitNxLink();
}

#endif

int debugPrintf(char *text, ...) {
#ifdef DEBUG_LOG
  // Keep the log file open for the life of the process. Reopening it on every
  // call (fopen/fclose) was thousands of SD round-trips per frame once any hot
  // GL path logged -- that, not the GPU, was the low frame rate. We still
  // fflush each line so a crash leaves the most recent output on disk. Hot GL
  // wrappers should still avoid logging in steady state; this just makes the
  // remaining occasional logs cheap and crash-safe.
  static Mutex log_mutex;
  static FILE *log_file = NULL;
  static int tried_open = 0;

  va_list list;
  mutexLock(&log_mutex);
  if (!log_file && !tried_open) {
    tried_open = 1;
    log_file = fopen(LOG_NAME, "w");
  }
  if (log_file) {
    va_start(list, text);
    vfprintf(log_file, text, list);
    va_end(list);
    fflush(log_file);
  }
  mutexUnlock(&log_mutex);
#endif
  return 0;
}

// boost the CPU to 1785MHz while loading
void cpu_boost(int on) {
  appletSetCpuBoostMode(on ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
}

int ret0(void) { return 0; }

int retm1(void) { return -1; }
