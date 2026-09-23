/* Emacs style mode select    -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *  Misc system stuff needed by Doom, implemented for Linux.
 *  Mainly timer handling, and ENDOOM/ENDBOOM.
 *
 *-----------------------------------------------------------------------------
 */

#include <stdio.h>

#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <signal.h>
#ifdef _MSC_VER
#define    F_OK    0    /* Check for file existence */
#define    W_OK    2    /* Check for write permission */
#define    R_OK    4    /* Check for read permission */
#include <io.h>
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <sys/stat.h>

#include "config.h"
#include <unistd.h>
#include <sched.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include "m_argv.h"
#include "lprintf.h"
#include "doomtype.h"
#include "doomdef.h"
#include "lprintf.h"
#include "m_fixed.h"
#include "r_fps.h"
#include "i_system.h"
#include "i_joy.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_partition.h"
#include "esp_spi_flash.h"
#include "esp_task_wdt.h"

#ifdef __GNUG__
#pragma implementation "i_system.h"
#endif
#include "i_system.h"

#include <sys/time.h>

int realtime=0;

void I_uSleep(unsigned long usecs)
{
  vTaskDelay(usecs/1000);
}

static unsigned long getMsTicks() {
  struct timeval tv;
  struct timezone tz;

  gettimeofday(&tv, &tz);

  //convert to ms
  unsigned long now = tv.tv_usec/1000+tv.tv_sec*1000;
  return now;
}

int I_GetTime_RealTime (void)
{
  struct timeval tv;
  struct timezone tz;
  unsigned long thistimereply;

  gettimeofday(&tv, &tz);

  thistimereply = (tv.tv_sec * TICRATE + (tv.tv_usec * TICRATE) / 1000000);

  return thistimereply;

}

const int displaytime=0;

fixed_t I_GetTimeFrac (void)
{
  unsigned long now;
  fixed_t frac;

  now = getMsTicks();

  if (tic_vars.step == 0)
    return FRACUNIT;
  else
  {
    frac = (fixed_t)((now - tic_vars.start + displaytime) * FRACUNIT / tic_vars.step);
    if (frac < 0)
      frac = 0;
    if (frac > FRACUNIT)
      frac = FRACUNIT;
    return frac;
  }
}

void I_GetTime_SaveMS(void)
{
  if (!movement_smooth)
    return;

  tic_vars.start = getMsTicks();
  tic_vars.next = (unsigned int) ((tic_vars.start * tic_vars.msec + 1.0f) / tic_vars.msec);
  tic_vars.step = tic_vars.next - tic_vars.start;
}

unsigned long I_GetRandomTimeSeed(void)
{
  return 4; //per https://xkcd.com/221/
}

const char* I_GetVersionString(char* buf, size_t sz)
{
  sprintf(buf,"%s v%s (http://prboom.sourceforge.net/)",PACKAGE,VERSION);
  return buf;
}

const char* I_SigString(char* buf, size_t sz, int signum)
{
  return buf;
}

extern unsigned char *doom1waddata;

char doomWadFilePath[256] = "/sd/doom1.wad";
extern int doom_open(const char* path, int flags);
extern void doom_close(int fd);
extern int doom_read(int fd, void* buf, int len);
extern int doom_lseek(int fd, int offset, int whence);
extern int doom_filelength(int fd);

#include <string.h>

int I_Open(const char *wad, int flags) {
    int fd = -1;
    
    // Extract filenames
    const char* p1 = strrchr(doomWadFilePath, '/');
    if (!p1) p1 = doomWadFilePath;
    else p1++;
    
    const char* p2 = strrchr(wad, '/');
    if (!p2) p2 = wad;
    else p2++;
    
    // Only use doomWadFilePath if filenames match (case-insensitive)
    if (strcasecmp(p1, p2) == 0) {
        fd = doom_open(doomWadFilePath, flags);
    }
    
    if (fd == -1) {
        fd = doom_open(wad, flags);
        if (fd == -1 && wad[0] != '/') {
            char tempPath[256];
            snprintf(tempPath, sizeof(tempPath), "/sd/%s", wad);
            fd = doom_open(tempPath, flags);
        }
    }
    
    if (fd == -1) {
        lprintf(LO_INFO, "I_Open: Failed to open file %s via Arduino SD\n", wad);
    } else {
        lprintf(LO_INFO, "I_Open: Opened file %s via Arduino SD, size %d bytes\n", wad, doom_filelength(fd));
    }
    return fd;
}

int I_Lseek(int ifd, off_t offset, int whence) {
    int pos = doom_lseek(ifd, offset, whence);
    // UART spamini engellemek icin log yoruma alindi
    // lprintf(LO_INFO, "I_Lseek: fd %d, offset %ld, whence %d -> pos %d\n", ifd, (long)offset, whence, pos);
    return pos;
}

int I_Filelength(int ifd) {
    return doom_filelength(ifd);
}

void I_Close(int fd) {
    doom_close(fd);
}

void I_Read(int ifd, void* vbuf, size_t sz) {
    int readBytes = doom_read(ifd, vbuf, sz);
    // UART spamini engellemek icin log yoruma alindi
    // lprintf(LO_INFO, "I_Read: fd %d, requested %d, read %d bytes\n", ifd, (int)sz, readBytes);
}

void *I_Mmap(void *addr, size_t length, int prot, int flags, int ifd, off_t offset) {
    // 1. WATCHDOG BESLEME DOKUNUSU (CPU 0 kilitlenmesin diye FreeRTOS'a nefes aldiriyoruz)
    vTaskDelay(1 / portTICK_PERIOD_MS);

    void* buf = heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        lprintf(LO_INFO, "I_Mmap: Out of memory trying to allocate %d bytes in PSRAM\n", (int)length);
        return NULL;
    }

    long cur = doom_lseek(ifd, 0, SEEK_CUR);
    doom_lseek(ifd, offset, SEEK_SET);
    int readBytes = doom_read(ifd, buf, length);
    doom_lseek(ifd, cur, SEEK_SET);

    // Seri portu kilitledigi ve WDT tetikledigi icin mmap logunu kapatiyoruz
    // lprintf(LO_INFO, "I_Mmap: fd %d, offset %ld, requested %d, read %d bytes\n", ifd, (long)offset, (int)length, readBytes);

    if (readBytes < length && readBytes >= 0) {
        memset((char*)buf + readBytes, 0, length - readBytes);
    }

    return buf;
}

int I_Munmap(void *addr, size_t length) {
    if (addr) {
        heap_caps_free(addr);
    }
    return 0;
}

const char *I_DoomExeDir(void)
{
  return "";
}

char* I_FindFile(const char* wfname, const char* ext)
{
  char *p;
  p = malloc(strlen(wfname)+4);
  sprintf(p, "%s.%s", wfname, ext);
  return NULL;
}

void I_SetAffinityMask(void)
{
}

// access function removed because it is defined by ESP-IDF vfs.c