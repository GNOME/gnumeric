/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-pattern.c :
 *
 * Copyright (C) 2003-2004 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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

#include <goffice/goffice-config.h>
#include "go-pattern.h"
#include "go-color.h"
#ifdef WITH_GTK
#include <goffice/gui-utils/go-combo-pixmaps.h>
#include <gdk-pixbuf/gdk-pixdata.h>
#endif

#include <libart_lgpl/libart.h>
#include <glib/gi18n.h>
#include <string.h>

typedef struct {
	int const x, y;
	char const pattern[8];
} GOPatternSpec;

static GOPatternSpec const go_patterns [] = {
	{ 8, 8, /* Solid */
		{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } },
	{ 8, 8, /* 75% */
		{ 0xbb, 0xee, 0xbb, 0xee, 0xbb, 0xee, 0xbb, 0xee } },
	{ 8, 8, /* 50% */
		{ 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 } },
	{ 8, 8, /* 25% */
		{ 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88 } },
	{ 8, 8, /* 12.5% */
		{ 0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00 } },
	{ 8, 8, /* 6.25% */
		{ 0x20, 0x00, 0x02, 0x00, 0x20, 0x00, 0x02, 0x00 } },
	{ 8, 8, /* Horizontal Stripe */
		{ 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff } },
	{ 8, 8, /* Vertical Stripe */
		{ 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33 } },
	{ 8, 8, /* Reverse Diagonal Stripe */
		{ 0x33, 0x66, 0xcc, 0x99, 0x33, 0x66, 0xcc, 0x99 } },
	{ 8, 8, /* Diagonal Stripe */
		{ 0xcc, 0x66, 0x33, 0x99, 0xcc, 0x66, 0x33, 0x99 } },
	{ 8, 8, /* Diagonal Crosshatch */
		{ 0x99, 0x66, 0x66, 0x99, 0x99, 0x66, 0x66, 0x99 } },
	{ 8, 8, /* Thick Diagonal Crosshatch */
		{ 0xff, 0x66, 0xff, 0x99, 0xff, 0x66, 0xff, 0x99 } },
	{ 8, 8, /* Thin Horizontal Stripe */
		{ 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00 } },
	{ 8, 8, /* Thin Vertical Stripe */
		{ 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22 } },
	{ 8, 8, /* Thin Reverse Diagonal Stripe */
		{ 0x11, 0x22, 0x44, 0x88, 0x11, 0x22, 0x44, 0x88 } },
	{ 8, 8, /* Thin Diagonal Stripe */
		{ 0x88, 0x44, 0x22, 0x11, 0x88, 0x44, 0x22, 0x11 } },
	{ 8, 8, /* Thin Crosshatch */
		{ 0x22, 0x22, 0xff, 0x22, 0x22, 0x22, 0xff, 0x22 } },
	{ 8, 8, /* Thin Diagonal Crosshatch */
		{ 0x88, 0x55, 0x22, 0x55, 0x88, 0x55, 0x22, 0x55 } },
	{ 8, 8, /* 100% */
		{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ 8, 8, /* Applix small circle */
		{ 0x99, 0x55, 0x33, 0xff, 0x99, 0x55, 0x33, 0xff } },
	{ 8, 8, /* Applix semicircle */
		{ 0x10, 0x10, 0x28, 0xc7, 0x01, 0x01, 0x82, 0x7c } },
	{ 8, 8, /* Applix small thatch */
		{ 0x22, 0x74, 0xf8, 0x71, 0x22, 0x17, 0x8f, 0x47 } },
	{ 8, 8, /* Applix round thatch */
		{ 0xc1, 0x80, 0x1c, 0x3e, 0x3e, 0x3e, 0x1c, 0x80 } },
	{ 8, 8, /* Applix Brick */
		{ 0x20, 0x20, 0x20, 0xff, 0x02, 0x02, 0x02, 0xff } }
};

static struct {
	GOPatternType pattern;
	char const *label;
	char const *name;
} pattern_names[] = {
	{ GO_PATTERN_SOLID,            N_("Solid"),			"solid" },
	{ GO_PATTERN_GREY75,           N_("75% Grey"),			"grey75" },
	{ GO_PATTERN_GREY50,           N_("50% Grey"),			"grey50" },
	{ GO_PATTERN_GREY25,           N_("25% Grey"),			"grey25" },
	{ GO_PATTERN_GREY125,          N_("12.5% Grey"),		"grey12.5" },
	{ GO_PATTERN_GREY625,          N_("6.25% Grey"),		"grey6.25" },
	{ GO_PATTERN_HORIZ,            N_("Horizontal Stripe"),		"horiz" },
	{ GO_PATTERN_VERT,             N_("Vertical Stripe"),		"vert" },
	{ GO_PATTERN_REV_DIAG,         N_("Reverse Diagonal Stripe"),	"rev-diag" },
	{ GO_PATTERN_DIAG,             N_("Diagonal Stripe"),		"diag" },
	{ GO_PATTERN_DIAG_CROSS,       N_("Diagonal Crosshatch"),	"diag-cross" },
	{ GO_PATTERN_THICK_DIAG_CROSS, N_("Thick Diagonal Crosshatch"),	"thick-diag-cross" },
	{ GO_PATTERN_THIN_HORIZ,       N_("Thin Horizontal Stripe"),	"thin-horiz" },
	{ GO_PATTERN_THIN_VERT,        N_("Thin Vertical Stripe"),	"thin-vert" },
	{ GO_PATTERN_THIN_REV_DIAG,    N_("Thin Reverse Diagonal Stripe"), "rev-diag" },
	{ GO_PATTERN_THIN_DIAG,        N_("Thin Diagonal Stripe"),	"thin-diag" },
	{ GO_PATTERN_THIN_HORIZ_CROSS, N_("Thin Horizontal Crosshatch"),"thin-horiz-cross" },
	{ GO_PATTERN_THIN_DIAG_CROSS,  N_("Thin Diagonal Crosshatch"),	"thin-diag-cross" },
	{ GO_PATTERN_FOREGROUND_SOLID, N_("Foreground Solid"),		"foreground-solid" },
	{ GO_PATTERN_SMALL_CIRCLES,    N_("Small Circles"),		"small-circles" },
	{ GO_PATTERN_SEMI_CIRCLES,     N_("Semi Circles"),		"semi-circles" },
	{ GO_PATTERN_THATCH,           N_("Thatch"),			"thatch" },
	{ GO_PATTERN_LARGE_CIRCLES,    N_("Large Circles"),		"large-circles" },
	{ GO_PATTERN_BRICKS,           N_("Bricks"),			"bricks"  }
};


GOPatternType
go_pattern_from_str (char const *name)
{
	unsigned i;
	GOPatternType ret = GO_PATTERN_SOLID;

	for (i = 0; i < sizeof pattern_names / sizeof pattern_names[0]; i++) {
		if (strcmp (pattern_names[i].name, name) == 0) {
			ret = pattern_names[i].pattern;
			break;
		}
	}
	return ret;
}
char const *
go_pattern_as_str (GOPatternType pattern)
{
	unsigned i;
	char const *ret = "none";

	for (i = 0; i < sizeof pattern_names / sizeof pattern_names[0]; i++) {
		if (pattern_names[i].pattern == pattern) {
			ret = pattern_names[i].name;
			break;
		}
	}
	return ret;
}

/**
 * go_pattern_is_solid :
 * @pat : #GOPattern
 * @color : #GOColor
 * 
 * Returns if @pat is solid, and stores the color in @color.
 * If @pat is not solid @color is not touched.
 **/
gboolean
go_pattern_is_solid (GOPattern const *pat, GOColor *color)
{
	g_return_val_if_fail (pat != NULL, FALSE);

	if (pat->pattern == GO_PATTERN_SOLID || pat->fore == pat->back) {
		*color = pat->back;
		return TRUE;
	}
	if (pat->pattern == GO_PATTERN_FOREGROUND_SOLID) {
		*color = pat->fore;
		return TRUE;
	}
	return FALSE;
}

/**
 * go_pattern_set_solid :
 * @pat  : #GOPattern
 * @fore : #GOColor
 *
 * Makes @pat a solid pattern with colour @fore.
 **/
void
go_pattern_set_solid (GOPattern *pat, GOColor fore)
{
	g_return_if_fail (pat != NULL);
	pat->pattern = GO_PATTERN_SOLID;
	pat->fore = RGBA_BLACK;
	pat->back = fore;
}

#ifdef WITH_GTK
gpointer
go_pattern_selector (GOColor fore, GOColor back,
		     GOPatternType default_pat)
{
	static GOPatternType const elements[] = {
		GO_PATTERN_SOLID,	GO_PATTERN_GREY75,	GO_PATTERN_GREY50,	GO_PATTERN_GREY25,
		GO_PATTERN_GREY125,	GO_PATTERN_GREY625,	GO_PATTERN_HORIZ,	GO_PATTERN_VERT,
		GO_PATTERN_REV_DIAG,	GO_PATTERN_DIAG,	GO_PATTERN_DIAG_CROSS,	GO_PATTERN_THICK_DIAG_CROSS,
		GO_PATTERN_THIN_HORIZ,		GO_PATTERN_THIN_VERT,
		GO_PATTERN_THIN_REV_DIAG,	GO_PATTERN_THIN_DIAG,
		GO_PATTERN_THIN_HORIZ_CROSS,	GO_PATTERN_THIN_DIAG_CROSS,
		GO_PATTERN_FOREGROUND_SOLID,	GO_PATTERN_SMALL_CIRCLES,
		GO_PATTERN_SEMI_CIRCLES, GO_PATTERN_THATCH,	GO_PATTERN_LARGE_CIRCLES,   GO_PATTERN_BRICKS,
		GO_PATTERN_MAX	/* fill with auto */
	};
	int const W = 20, H = 20;
	unsigned	 i;
	gboolean	 is_auto;
	GOComboPixmaps	*w;
	GdkPixbuf	*pixbuf;
	GOPattern	 pat;
	ArtVpath	 path[6];
	ArtSVP		*svp;

	pat.fore = fore;
	pat.back = back;

	path[0].code = ART_MOVETO;
	path[1].code = ART_LINETO;
	path[2].code = ART_LINETO;
	path[3].code = ART_LINETO;
	path[4].code = ART_LINETO;
	path[5].code = ART_END;
	path[0].x = path[1].x = path[4].x = 0;
	path[2].x = path[3].x = W;
	path[0].y = path[3].y = path[4].y = 0;
	path[1].y = path[2].y = H;
	svp = art_svp_from_vpath (path);

	w = go_combo_pixmaps_new (5);
	for (i = 0; i < G_N_ELEMENTS (elements); i++) {
		pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, W, H);
		gdk_pixbuf_fill (pixbuf, 0); /* in case the fill colours have alpha = 0 */
		is_auto = elements[i] == GO_PATTERN_MAX;
		pat.pattern = is_auto ? default_pat : i;
		go_pattern_render_svp (&pat, svp, 0, 0, W, H,
			gdk_pixbuf_get_pixels (pixbuf),
			gdk_pixbuf_get_rowstride (pixbuf));
		if (is_auto) {
			/* xgettext : this will appear as 'Automatic (patternname)' */
			char *name = g_strdup_printf (_("Automatic (%s)"),
				_(pattern_names [default_pat].label));
			go_combo_pixmaps_add_element (w, pixbuf,
				-default_pat, name);
			g_free (name);
		} else
			go_combo_pixmaps_add_element (w, pixbuf, pat.pattern,
				_(pattern_names[pat.pattern].label));
	}
	art_svp_free (svp);
	return w;
}
#endif /* WITH_GTK */

/*
 *  A slightly modified version of art_rgb_svp to render into rgba buffer
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public License
 *  as published by the Free Software Foundation; either version 2 of
 *  the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Authors:
 *    Raph Levien <raph@acm.org>
 *    Lauris Kaplinski <lauris@ariman.ee>
 *
 *  Copyright (C) 1998 Raph Levien
 *
 */

/* Render a sorted vector path into an RGBA buffer. */
#include <libart_lgpl/art_misc.h>
#include <libart_lgpl/art_svp.h>
#include <libart_lgpl/art_svp_render_aa.h>
#include <libart_lgpl/art_rgb.h>

typedef struct {
	struct {
		art_u8 r, g, b;
		int alphatab[256];
	} fore, back;
	art_u8 *buf;
	int rowstride;
	int x0, x1;
	guint8 const *pattern;
} pattern_data;

static void
pattern_solid (pattern_data const *state, guint8 pat, int offset, int n)
{
	art_u8 r1, g1, b1, r2, g2, b2;
	art_u8 *buf = state->buf + 4*offset;
	int mask = 1 << (offset % 8);

	r1 = state->fore.r;
	g1 = state->fore.g;
	b1 = state->fore.b;
	r2 = state->back.r;
	g2 = state->back.g;
	b2 = state->back.b;
	while (n-- > 0) {
		if (pat & mask) {
			*buf++ = r1;
			*buf++ = g1;
			*buf++ = b1;
		} else {
			*buf++ = r2;
			*buf++ = g2;
			*buf++ = b2;
		}
		* buf++ = 255;

		if (mask != 0x80)
			mask <<= 1;
		else
			mask = 1;
	}
}

static void
pattern_blend (pattern_data const *state, guint8 pat, int offset, int alpha, int n)
{
	int br, bg, bb, ba;
	int cr, cg, cb;
	int r1, g1, b1, r2, g2, b2;
	art_u8 *buf = state->buf + 4*offset;
	int mask = 1 << (offset % 8);
	int alpha1 = state->fore.alphatab[alpha];
	int alpha2 = state->back.alphatab[alpha];

	r1 = state->fore.r;
	g1 = state->fore.g;
	b1 = state->fore.b;
	r2 = state->back.r;
	g2 = state->back.g;
	b2 = state->back.b;

	while (n-- > 0) {
		br = * (buf + 0);
		bg = * (buf + 1);
		bb = * (buf + 2);
		ba = * (buf + 3);

		cr = (br * ba + 0x80) >> 8;
		cg = (bg * ba + 0x80) >> 8;
		cb = (bb * ba + 0x80) >> 8;

		if (pat & mask) {
			*buf++ = cr + (((r1 - cr) * alpha1 + 0x80) >> 8);
			*buf++ = cg + (((g1 - cg) * alpha1 + 0x80) >> 8);
			*buf++ = cb + (((b1 - cb) * alpha1 + 0x80) >> 8);
		} else {
			*buf++ = cr + (((r2 - cr) * alpha2 + 0x80) >> 8);
			*buf++ = cg + (((g2 - cg) * alpha2 + 0x80) >> 8);
			*buf++ = cb + (((b2 - cb) * alpha2 + 0x80) >> 8);
		}
		*buf++ = ba + (((255 - ba) * alpha + 0x80) >> 8);

		if (mask != 0x80)
			mask <<= 1;
		else
			mask = 1;
	}
}

static void
cb_pattern_alpha (void *callback_data, int y, int start,
		  ArtSVPRenderAAStep *steps, int n_steps)
{
	pattern_data *state = callback_data;
	int run_x0, run_x1;
	art_u32 running_sum = start;
	int k;
	int alpha;
	int x0 = state->x0;
	int x1 = state->x1;
	int pat = state->pattern [y % 8];

	if (n_steps > 0) {
		run_x1 = steps[0].x;
		if (run_x1 > x0) {
			alpha = (running_sum >> 16) & 0xff;
			if (alpha)
				pattern_blend (state, pat, 0, alpha, run_x1 - x0);
		}

		/* render the steps into tmpbuf */
		for (k = 0; k < n_steps - 1; k++) {
			running_sum += steps[k].delta;
			run_x0 = run_x1;
			run_x1 = steps[k + 1].x;
			if (run_x1 > run_x0) {
				alpha = (running_sum >> 16) & 0xff;
				if (alpha)
					pattern_blend (state, pat, run_x0 - x0,
						       alpha, run_x1 - run_x0);
			}
		}
		running_sum += steps[k].delta;
		if (x1 > run_x1) {
			alpha = (running_sum >> 16) & 0xff;
			if (alpha)
				pattern_blend (state, pat, run_x1 - x0,
					       alpha, x1 - run_x1);
		}
	} else {
		alpha = (running_sum >> 16) & 0xff;
		if (alpha)
			pattern_blend (state, pat, 0, alpha, x1 - x0);
	}

	state->buf += state->rowstride;
}

static void
cb_pattern_opaque (void *callback_data, int y, int start,
		   ArtSVPRenderAAStep *steps, int n_steps)
{
	pattern_data *state = callback_data;
	int run_x0, run_x1;
	art_u32 running_sum = start;
	int x0, x1;
	int k;
	int alpha;
	int pat = state->pattern [y % 8];

	x0 = state->x0;
	x1 = state->x1;

	if (n_steps > 0) {
		run_x1 = steps[0].x;
		if (run_x1 > x0) {
			alpha = running_sum >> 16;
			if (alpha) {
				if (alpha >= 255)
					pattern_solid (state, pat,  0, run_x1 - x0);
				else
					pattern_blend (state, pat, 0, alpha, run_x1 - x0);
			}
		}

		/* render the steps into tmpbuf */
		for (k = 0; k < n_steps - 1; k++) {
			running_sum += steps[k].delta;
			run_x0 = run_x1;
			run_x1 = steps[k + 1].x;
			if (run_x1 > run_x0) {
				alpha = running_sum >> 16;
				if (alpha) {
					if (alpha >= 255)
						pattern_solid (state, pat, run_x0 - x0,
							       run_x1 - run_x0);
					else
						pattern_blend (state, pat, run_x0 - x0,
							       alpha, run_x1 - run_x0);
				}
			}
		}
		running_sum += steps[k].delta;
		if (x1 > run_x1) {
			alpha = running_sum >> 16;
			if (alpha) {
				if (alpha >= 255)
					pattern_solid (state, pat, run_x1 - x0,
						       x1 - run_x1);
				else
					pattern_blend (state, pat, run_x1 - x0,
						       alpha, x1 - run_x1);
			}
		}
	} else {
		alpha = running_sum >> 16;
		if (alpha) {
			if (alpha >= 255)
				pattern_solid (state, pat, 0, x1 - x0);
			else
				pattern_blend (state, pat, 0, alpha, x1 - x0);
		}
	}

	state->buf += state->rowstride;
}

/**
 * go_pattern_render_svp: 
 * @pat : #GOPattern
 * @svp: The source sorted vector path.
 * @x0: Left coordinate of destination rectangle.
 * @y0: Top coordinate of destination rectangle.
 * @x1: Right coordinate of destination rectangle.
 * @y1: Bottom coordinate of destination rectangle.
 * @buf: Destination RGBA buffer.
 * @rowstride: Rowstride of @buf buffer.
 *
 * Renders the shape specified with @svp over the @buf RGB buffer.
 * @x1 - @x0 specifies the width, and @y1 - @y0 specifies the height,
 * of the rectangle rendered. The new pixels are stored starting at
 * the first byte of @buf. Thus, the @x0 and @y0 parameters specify
 * an offset within @svp, and may be tweaked as a way of doing
 * integer-pixel translations without fiddling with @svp itself.
 *
 * The @pat argument specifies the pattern for the rendering. Pixels of
 * entirely 0 winding number are left untouched. Pixels of entirely
 * 1 winding number have the pattern @pat composited over them (ie,
 * are replaced by the red, green, blue components of @pat->fore or @pat->back
 * depending on the stipple associated with @pat->pattern. if the alpha
 * component is 0xff). Pixels of intermediate coverage are linearly
 * interpolated.
 **/
void
go_pattern_render_svp (GOPattern const *pat, ArtSVP const *svp,
		       int x0, int y0, int x1, int y1,
		       art_u8 *buf, int rowstride)
{
	pattern_data state;
	int i, a, da;
	gboolean opaque = TRUE;
	GOColor c;

	g_return_if_fail (pat != NULL);

	if (go_pattern_is_solid (pat, &c)) {
		go_color_render_svp (c, svp,
			  x0, y0, x1, y1, buf, rowstride);
		return;
	}

	state.fore.r = UINT_RGBA_R (pat->fore);
	state.fore.g = UINT_RGBA_G (pat->fore);
	state.fore.b = UINT_RGBA_B (pat->fore);
	state.back.r = UINT_RGBA_R (pat->back);
	state.back.g = UINT_RGBA_G (pat->back);
	state.back.b = UINT_RGBA_B (pat->back);
	state.buf = buf;
	state.rowstride = rowstride;
	state.x0 = x0;
	state.x1 = x1;
	state.pattern = go_patterns [pat->pattern].pattern;

	a = 0x8000;
	da = (UINT_RGBA_A (pat->fore) * 66051 + 0x80) >> 8; /* 66051 equals 2 ^ 32 / (255 * 255) */
	if (da != 65793) opaque = FALSE;
	for (i = 0; i < 256; i++) {
		state.fore.alphatab[i] = a >> 16;
		a += da;
	}
	a = 0x8000;
	da = (UINT_RGBA_A (pat->back) * 66051 + 0x80) >> 8; /* 66051 equals 2 ^ 32 / (255 * 255) */
	if (da != 65793) opaque = FALSE;
	for (i = 0; i < 256; i++) {
		state.back.alphatab[i] = a >> 16;
		a += da;
	}

	art_svp_render_aa (svp, x0, y0, x1, y1,
		(opaque ? &cb_pattern_opaque : &cb_pattern_alpha), &state);
}
