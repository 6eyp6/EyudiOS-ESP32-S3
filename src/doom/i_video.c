/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2006 by
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
 *  DOOM graphics stuff for SDL
 *
 *-----------------------------------------------------------------------------
 */

#include "config.h"
#include <stdlib.h>
#include <unistd.h>
#include "m_argv.h"
#include "doomstat.h"
#include "doomdef.h"
#include "doomtype.h"
#include "v_video.h"
#include "r_draw.h"
#include "d_main.h"
#include "d_event.h"
// #include "gamepad.h"
#include "i_video.h"
#include "z_zone.h"
#include "s_sound.h"
#include "sounds.h"
#include "w_wad.h"
#include "st_stuff.h"
#include "lprintf.h"

#include "rom/ets_sys.h"
// #include "spi_lcd.h"

#include "esp_heap_caps.h"

int use_fullscreen=0;
int use_doublebuffer=0;


void I_StartTic (void)
{
	// gamepadPoll();
}


static void I_InitInputs(void)
{
}



static void I_UploadNewPalette(int pal)
{
}

//////////////////////////////////////////////////////////////////////////////
// Graphics API

void I_ShutdownGraphics(void)
{
}

//
// I_UpdateNoBlit
//
void I_UpdateNoBlit (void)
{
}


extern volatile bool heldKeys[256];

void I_StartFrame (void)
{
	static bool lastKeysState[256] = {0};
	int i;
	for (i = 0; i < 256; i++) {
		bool cur = heldKeys[i];
		if (cur != lastKeysState[i]) {
			lastKeysState[i] = cur;
			int doomKey = -1;
			switch(i) {
				case 0x52: doomKey = 0xad; break; // Up Arrow -> KEYD_UPARROW
				case 0x51: doomKey = 0xaf; break; // Down Arrow -> KEYD_DOWNARROW
				case 0x50: doomKey = 0xac; break; // Left Arrow -> KEYD_LEFTARROW
				case 0x4F: doomKey = 0xae; break; // Right Arrow -> KEYD_RIGHTARROW
				
				case 0x1A: doomKey = 'w'; break; // W
				case 0x16: doomKey = 's'; break; // S
				case 0x04: doomKey = 'a'; break; // A
				case 0x07: doomKey = 'd'; break; // D
				
				case 0x2C: doomKey = 0x20; break; // Space -> KEYD_SPACEBAR (Open Doors/Use)
				case 0x28: doomKey = 13;   break; // Enter -> KEYD_ENTER (Select/Accept)
				case 0x29: doomKey = 27;   break; // Escape -> KEYD_ESCAPE (Game Menu)
				
				case 0x09: doomKey = 0x80+0x1d; break; // F key -> Shoot (KEYD_RCTRL)
				case 0xE0: case 0xE4: doomKey = 0x80+0x1d; break; // Left/Right Ctrl -> Shoot
			}
			
			if (doomKey != -1) {
				event_t ev;
				ev.type = cur ? ev_keydown : ev_keyup;
				ev.data1 = doomKey;
				ev.data2 = 0;
				ev.data3 = 0;
				D_PostEvent(&ev);
			}
		}
	}
}


int I_StartDisplay(void)
{
	// spi_lcd_wait_finish();
  return true;
}

void I_EndDisplay(void)
{
}



static uint16_t *screena, *screenb;


//
// I_FinishUpdate
//

extern void doom_display_frame(const uint8_t* scr, const uint16_t* palette);
int16_t lcdpal[256];

void I_FinishUpdate (void)
{
	uint8_t *scr=(uint8_t*)screens[0].data;
	doom_display_frame(scr, (uint16_t*)lcdpal);
}

void I_SetPalette (int pal)
{
	int i, r, g, b, v;
	int pplump = W_GetNumForName("PLAYPAL");
	const byte * palette = W_CacheLumpNum(pplump);
	palette+=pal*(3*256);
	for (i=0; i<256 ; i++) {
		v=((palette[0]>>3)<<11)+((palette[1]>>2)<<5)+(palette[2]>>3);
		lcdpal[i]=v;
		palette += 3;
	}
	W_UnlockLumpNum(pplump);
}


unsigned char *screenbuf;

#define INTERNAL_MEM_FB


void I_PreInitGraphics(void)
{
	lprintf(LO_INFO, "preinitgfx");
	// Allocate frame buffer in PSRAM to save internal DRAM
	screenbuf = (unsigned char*)heap_caps_malloc(320 * 240, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (!screenbuf) {
		I_Error("Yetersiz PSRAM! 320x240 frame buffer olusturulamadi.");
	}
}


// CPhipps -
// I_SetRes
// Sets the screen resolution
void I_SetRes(void)
{
  int i;

//  I_CalculateRes(SCREENWIDTH, SCREENHEIGHT);

  // set first three to standard values
  for (i=0; i<3; i++) {
    screens[i].width = SCREENWIDTH;
    screens[i].height = SCREENHEIGHT;
    screens[i].byte_pitch = SCREENPITCH;
    screens[i].short_pitch = SCREENPITCH / V_GetModePixelDepth(VID_MODE16);
    screens[i].int_pitch = SCREENPITCH / V_GetModePixelDepth(VID_MODE32);
  }

  // statusbar
  screens[4].width = SCREENWIDTH;
  screens[4].height = (ST_SCALED_HEIGHT+1);
  screens[4].byte_pitch = SCREENPITCH;
  screens[4].short_pitch = SCREENPITCH / V_GetModePixelDepth(VID_MODE16);
  screens[4].int_pitch = SCREENPITCH / V_GetModePixelDepth(VID_MODE32);

//Attempt at double-buffering. Does not work.
//  free(screena);
//  free(screenb);
//  screena=malloc(SCREENPITCH*SCREENHEIGHT);
//  screenb=malloc(SCREENPITCH*SCREENHEIGHT);


#ifdef INTERNAL_MEM_FB
  screens[0].not_on_heap=true;
  screens[0].data=screenbuf;
  assert(screens[0].data);
#endif

//  spi_lcd_init();

  lprintf(LO_INFO,"I_SetRes: Using resolution %dx%d\n", SCREENWIDTH, SCREENHEIGHT);
}

void I_InitGraphics(void)
{
  char titlebuffer[2048];
  static int    firsttime=1;

  if (firsttime)
  {
    firsttime = 0;

    atexit(I_ShutdownGraphics);
    lprintf(LO_INFO, "I_InitGraphics: %dx%d\n", SCREENWIDTH, SCREENHEIGHT);

    /* Set the video mode */
    I_UpdateVideoMode();

    /* Initialize the input system */
    I_InitInputs();
	// gamepadInit();

  }
}


void I_UpdateVideoMode(void)
{
  int init_flags;
  int i;
  video_mode_t mode;

  lprintf(LO_INFO, "I_UpdateVideoMode: %dx%d\n", SCREENWIDTH, SCREENHEIGHT);

//    mode = VID_MODE16;
    mode = VID_MODE8;

  V_InitMode(mode);
  V_DestroyUnusedTrueColorPalettes();
  V_FreeScreens();

  I_SetRes();

  V_AllocScreens();

  R_InitBuffer(SCREENWIDTH, SCREENHEIGHT);

}
