/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-color.h
 *
 * Copyright (C) 1999, 2000 EMC Capital Management, Inc.
 *
 * Developed by Jon Trowbridge <trow@gnu.org> and
 * Havoc Pennington <hp@pobox.com>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifndef GO_COLOR_H
#define GO_COLOR_H

#include <glib.h>
#include <goffice/utils/goffice-utils.h>
#include <libart_lgpl/art_render.h>
#include <libart_lgpl/art_svp.h>
#include <pango/pango.h>

#ifdef WITH_GTK
#include <gdk/gdktypes.h>
#endif

G_BEGIN_DECLS

/*
  Some convenient macros for drawing into an RGB buffer.
  Beware of side effects, code-bloat, and all of the other classic
  cpp-perils...
*/

#define GDK_TO_GO_COLOR(c)	GO_COLOR_FROM_RGBA(((c).red>>8), ((c).green>>8), ((c).blue>>8), 0xff)

#define GO_COLOR_FROM_RGBA(r,g,b,a)	((((guint)(r))<<24)|(((guint)(g))<<16)|(((guint)(b))<<8)|(guint)(a))
#define GO_COLOR_WHITE   GO_COLOR_FROM_RGBA(0xff, 0xff, 0xff, 0xff)
#define GO_COLOR_BLACK   GO_COLOR_FROM_RGBA(0x00, 0x00, 0x00, 0xff)
#define GO_COLOR_RED     GO_COLOR_FROM_RGBA(0xff, 0x00, 0x00, 0xff)
#define GO_COLOR_GREEN   GO_COLOR_FROM_RGBA(0x00, 0xff, 0x00, 0xff)
#define GO_COLOR_BLUE    GO_COLOR_FROM_RGBA(0x00, 0x00, 0xff, 0xff)
#define GO_COLOR_YELLOW  GO_COLOR_FROM_RGBA(0xff, 0xff, 0x00, 0xff)
#define GO_COLOR_VIOLET  GO_COLOR_FROM_RGBA(0xff, 0x00, 0xff, 0xff)
#define GO_COLOR_CYAN    GO_COLOR_FROM_RGBA(0x00, 0xff, 0xff, 0xff)
#define GO_COLOR_GREY(x) GO_COLOR_FROM_RGBA(x, x, x, 0xff)

#define GO_COLOR_R(x) (((GOColor)(x))>>24)
#define GO_COLOR_G(x) ((((GOColor)(x))>>16)&0xff)
#define GO_COLOR_B(x) ((((GOColor)(x))>>8)&0xff)
#define GO_COLOR_A(x) (((GOColor)(x))&0xff)
#define GO_COLOR_CHANGE_R(x, r) (((x)&(~(0xff<<24)))|(((r)&0xff)<<24))
#define GO_COLOR_CHANGE_G(x, g) (((x)&(~(0xff<<16)))|(((g)&0xff)<<16))
#define GO_COLOR_CHANGE_B(x, b) (((x)&(~(0xff<<8)))|(((b)&0xff)<<8))
#define GO_COLOR_CHANGE_A(x, a) (((x)&(~0xff))|((a)&0xff))

/* These APIs take pointers. */
#define GO_COLOR_TO_RGB(u,rp,gp,bp) \
{ (*(rp)) = ((u)>>16)&0xff; (*(gp)) = ((u)>>8)&0xff; (*(bp)) = (u)&0xff; }
#define GO_COLOR_TO_RGBA(u,rp,gp,bp,ap) \
{ GO_COLOR_TO_RGB(((u)>>8),rp,gp,bp); (*(ap)) = (u)&0xff; }
#define MONO_INTERPOLATE(v1, v2, t) ((gint)rint((v2)*(t)+(v1)*(1-(t))))
#define GO_COLOR_INTERPOLATE(c1, c2, t) \
  GO_COLOR_FROM_RGBA( MONO_INTERPOLATE(GO_COLOR_R(c1), GO_COLOR_R(c2), t), \
		      MONO_INTERPOLATE(GO_COLOR_G(c1), GO_COLOR_G(c2), t), \
		      MONO_INTERPOLATE(GO_COLOR_B(c1), GO_COLOR_B(c2), t), \
		      MONO_INTERPOLATE(GO_COLOR_A(c1), GO_COLOR_A(c2), t) )

void go_color_to_artpix  (ArtPixMaxDepth *res, GOColor rgba);
void go_color_render_svp (GOColor color, ArtSVP const *svp,
			  int x0, int y0, int x1, int y1,
			  art_u8 *buf, int rowstride);

GOColor		 go_color_from_str (char const *string);
gchar		*go_color_as_str   (GOColor color);
PangoAttribute	*go_color_to_pango (GOColor color, gboolean is_fore);
#ifdef WITH_GTK
GdkColor	*go_color_to_gdk   (GOColor color, GdkColor *res);
#endif

G_END_DECLS

#endif /* GO_COLOR_H */
