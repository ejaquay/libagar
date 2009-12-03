/*
 * Copyright (c) 2009 Hypertriton, Inc. <http://hypertriton.com/>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Common code for SDL drivers.
 */

#include <config/have_sdl.h>
#ifdef HAVE_SDL

#include <core/core.h>
#include <core/config.h>

#include "gui.h"
#include "window.h"
#include "packedpixel.h"
#include "cursors.h"
#include "perfmon.h"

#include "drv_sdl_common.h"

#define AG_SDL_CLIPPED_PIXEL(s, ax, ay)			\
	((ax) < (s)->clip_rect.x ||			\
	 (ax) >= (s)->clip_rect.x+(s)->clip_rect.w ||	\
	 (ay) < (s)->clip_rect.y ||			\
	 (ay) >= (s)->clip_rect.y+(s)->clip_rect.h)

/*
 * Initialize Agar with an existing SDL display. If the display surface has
 * flags SDL_OPENGL / SDL_OPENGLBLIT set, the "sdlgl" driver is selected;
 * otherwise, the "sdlfb" driver is used.
 */
int
AG_InitVideoSDL(void *pDisplay, Uint flags)
{
	SDL_Surface *display = pDisplay;
	AG_Driver *drv = NULL;
	AG_DriverClass *dc = NULL;
	Uint dcFlags = AG_DRIVER_SDL;
	int i;

	if (AG_InitGUIGlobals() == -1)
		return (-1);
	
	/* Enable OpenGL mode if the surface has SDL_OPENGL set. */
	if (display->flags & (SDL_OPENGL|SDL_OPENGLBLIT)) {
		if (flags & AG_VIDEO_SDL) {
			AG_SetError("AG_VIDEO_SDL flag requested, but "
			            "display surface has SDL_OPENGL set");
			goto fail;
		}
		dcFlags |= AG_DRIVER_OPENGL;
	} else {
		if (flags & AG_VIDEO_OPENGL) {
			AG_SetError("AG_VIDEO_OPENGL flag requested, but "
			            "display surface is missing SDL_OPENGL");
			goto fail;
		}
	}
	for (i = 0; i < agDriverListSize; i++) {
		dc = agDriverList[i];
		if (dc->wm == AG_WM_SINGLE &&
		    (dc->flags & dcFlags) &&
		    (drv = AG_DriverOpen(dc)) != NULL)
			break;
	}
	if (i == agDriverListSize) {
		AG_SetError("No compatible SDL driver is available");
		goto fail;
	}

	/* Open a video display. */
	if (AGDRIVER_SW_CLASS(drv)->openVideoContext(drv, (void *)display,
	    flags) == -1) {
		AG_DriverClose(drv);
		goto fail;
	}
#ifdef AG_DEBUG
	if (drv->videoFmt == NULL)
		AG_FatalError("Driver did not set video format");
#endif

	/* Generic Agar-GUI initialization. */
	if (AG_InitGUI(0) == -1) {
		AG_DriverClose(drv);
		goto fail;
	}

	agDriverOps = dc;
	agDriverSw = AGDRIVER_SW(drv);
#ifdef AG_LEGACY
	agView = drv;
#endif
	return (0);
fail:
	AG_DestroyGUIGlobals();
	return (-1);
}

/* Return the corresponding Agar PixelFormat structure for a SDL_Surface. */
AG_PixelFormat *
AG_SDL_GetPixelFormat(SDL_Surface *su)
{
	switch (su->format->BytesPerPixel) {
	case 1:
		return AG_PixelFormatIndexed(su->format->BitsPerPixel);
	case 2:
	case 3:
	case 4:
		if (su->format->Amask != 0) {
			return AG_PixelFormatRGBA(su->format->BitsPerPixel,
			                          su->format->Rmask,
			                          su->format->Gmask,
			                          su->format->Bmask,
						  su->format->Amask);
		} else {
			return AG_PixelFormatRGB(su->format->BitsPerPixel,
			                         su->format->Rmask,
			                         su->format->Gmask,
			                         su->format->Bmask);
		}
	default:
		AG_SetError("Unsupported pixel depth (%d bpp)",
		    (int)su->format->BitsPerPixel);
		return (NULL);
	}
}

/*
 * Blend the specified components with the pixel at s:[x,y], using the
 * given alpha function. No clipping is done.
 */
static void
AG_SDL_SurfaceBlendPixel(SDL_Surface *s, Uint8 *pDst, AG_Color Cnew,
    AG_BlendFn fn)
{
	Uint32 px, pxDst;
	AG_Color Cdst;
	Uint8 a;

	AG_PACKEDPIXEL_GET(s->format->BytesPerPixel, pxDst, pDst);
	if ((s->flags & SDL_SRCCOLORKEY) && (pxDst == s->format->colorkey)) {
		px = SDL_MapRGBA(s->format, Cnew.r, Cnew.g, Cnew.b, Cnew.a);
	 	AG_PACKEDPIXEL_PUT(s->format->BytesPerPixel, pDst, px);
	} else {
		SDL_GetRGBA(pxDst, s->format, &Cdst.r, &Cdst.g, &Cdst.b,
		    &Cdst.a);
		switch (fn) {
		case AG_ALPHA_DST:
			a = Cdst.a;
			break;
		case AG_ALPHA_SRC:
			a = Cnew.a;
			break;
		case AG_ALPHA_ZERO:
			a = 0;
			break;
		case AG_ALPHA_OVERLAY:
			a = (Uint8)((Cdst.a+Cnew.a) > 255) ? 255 :
			            (Cdst.a+Cnew.a);
			break;
		case AG_ALPHA_ONE_MINUS_DST:
			a = 255-Cdst.a;
			break;
		case AG_ALPHA_ONE_MINUS_SRC:
			a = 255-Cnew.a;
			break;
		case AG_ALPHA_ONE:
		default:
			a = 255;
			break;
		}
		px = SDL_MapRGBA(s->format,
		    (((Cnew.r - Cdst.r)*Cnew.a) >> 8) + Cdst.r,
		    (((Cnew.g - Cdst.g)*Cnew.a) >> 8) + Cdst.g,
		    (((Cnew.b - Cdst.b)*Cnew.a) >> 8) + Cdst.b,
		    a);
		AG_PACKEDPIXEL_PUT(s->format->BytesPerPixel, pDst, px);
	}
}

/* Blit an AG_Surface to a target SDL_Surface. */
void
AG_SDL_BlitSurface(const AG_Surface *ss, const AG_Rect *srcRect,
    SDL_Surface *ds, int xDst, int yDst)
{
	AG_Rect sr, dr;
	AG_Color C;
	Uint32 px;
	int x, y;
	Uint8 *pSrc, *pDst;

	/* Compute the effective source and destination rectangles. */
	if (srcRect != NULL) {
		sr = *srcRect;
		if (sr.x < 0) { sr.x = 0; }
		if (sr.y < 0) { sr.y = 0; }
		if (sr.x+sr.w >= ss->w) { sr.w = ss->w - sr.x; }
		if (sr.y+sr.h >= ss->h) { sr.h = ss->h - sr.y; }
	} else {
		sr.x = 0;
		sr.y = 0;
		sr.w = ss->w;
		sr.h = ss->h;
	}
	dr.x = xDst;
	dr.y = yDst;
	dr.w = (xDst+sr.w >= ds->w) ? (ds->w - xDst) : sr.w;
	dr.h = (yDst+sr.h >= ds->h) ? (ds->h - yDst) : sr.h;
 
	/* XXX TODO optimized cases */
	for (y = 0; y < dr.h; y++) {
		pSrc = (Uint8 *)ss->pixels + (sr.y+y)*ss->pitch +
		    sr.x*ss->format->BytesPerPixel;
		pDst = (Uint8 *)ds->pixels + (dr.y+y)*ds->pitch +
		    dr.x*ds->format->BytesPerPixel;
		for (x = 0; x < dr.w; x++) {
			AG_PACKEDPIXEL_GET(ss->format->BytesPerPixel, px, pSrc);
			if (((ss->flags & AG_SRCCOLORKEY) &&
			    (ss->format->colorkey == px)) ||
			    AG_SDL_CLIPPED_PIXEL(ds, x,y)) { /* XXX optimize */
				goto next_pixel;
			}
			C = AG_GetColorRGBA(px, ss->format);
			if ((C.a != AG_ALPHA_OPAQUE) &&
			    (ss->flags & AG_SRCALPHA)) {
				AG_SDL_SurfaceBlendPixel(ds, pDst, C,
				    AG_ALPHA_SRC);
			} else {
				px = SDL_MapRGB(ds->format, C.r, C.g, C.b);
				AG_PACKEDPIXEL_PUT(ds->format->BytesPerPixel,
				    pDst, px);
			}
next_pixel:
			pSrc += ss->format->BytesPerPixel;
			pDst += ds->format->BytesPerPixel;
		}
	}
}

/* Convert a SDL_Surface to an AG_Surface. */
AG_Surface *
AG_SDL_ImportSurface(SDL_Surface *ss)
{
	AG_PixelFormat *pf;
	AG_Surface *ds;
	Uint8 *pSrc, *pDst;
	int y;

	if ((pf = AG_SDL_GetPixelFormat(ss)) == NULL) {
		return (NULL);
	}
	if (pf->palette != NULL) {
		AG_SetError("Indexed formats not supported");
		AG_PixelFormatFree(pf);
		return (NULL);
	}

	if ((ds = AG_SurfaceNew(AG_SURFACE_PACKED, ss->w, ss->h, pf, 0))
	    == NULL)
		goto out;

	pSrc = (Uint8 *)ss->pixels;
	pDst = (Uint8 *)ds->pixels;
	for (y = 0; y < ss->h; y++) {
		memcpy(pDst, pSrc, ss->pitch);
		pSrc += ss->pitch;
		pDst += ss->pitch;
	}
out:
	AG_PixelFormatFree(pf);
	return (ds);
}

/* Convert a SDL surface to Agar surface. */
AG_Surface *
AG_SurfaceFromSDL(void *p)
{
	return AG_SDL_ImportSurface((SDL_Surface *)p);
}

/* Convert an Agar surface to an SDL surface. */
void *
AG_SurfaceExportSDL(const AG_Surface *ss)
{
	Uint32 sdlFlags = SDL_SWSURFACE;
	SDL_Surface *ds;

	if (ss->flags & AG_SRCCOLORKEY) { sdlFlags |= SDL_SRCCOLORKEY; }
	if (ss->flags & AG_SRCALPHA) { sdlFlags |= SDL_SRCALPHA; }
	ds = SDL_CreateRGBSurface(sdlFlags, ss->w, ss->h,
	    ss->format->BitsPerPixel,
	    ss->format->Rmask,
	    ss->format->Gmask,
	    ss->format->Bmask,
	    ss->format->Amask);
	if (ds == NULL) {
		AG_SetError("SDL_CreateRGBSurface: %s", SDL_GetError());
		return (NULL);
	}
	AG_SDL_BlitSurface(ss, NULL, ds, 0,0);
	return (void *)(ds);
}

#else /* HAVE_SDL */

AG_Surface *
AG_SurfaceFromSDL(void *p)
{
	AG_SetError("Agar compiled without SDL support");
	return (NULL);
}

void *
AG_SurfaceExportSDL(const AG_Surface *su)
{
	AG_SetError("Agar compiled without SDL support");
	return (NULL);
}
#endif /* HAVE_SDL */