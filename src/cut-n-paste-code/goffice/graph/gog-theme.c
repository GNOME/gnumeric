/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-theme.c :
 *
 * Copyright (C) 2003 Jody Goldberg (jody@gnome.org)
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

#include <gnumeric-config.h>
#include <goffice/graph/gog-theme.h>
#include <goffice/graph/gog-style.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>

struct _GogTheme {
	GObject	base;
};

typedef GObjectClass GogThemeClass;
static GObjectClass *parent_klass;

static void
gog_theme_finalize (GObject *obj)
{
	GogTheme *theme = GOG_THEME (obj);

	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
}

static void
gog_theme_class_init (GogThemeClass *klass)
{
	GObjectClass *gobject_klass   = (GObjectClass *) klass;

	parent_klass = g_type_class_peek_parent (klass);
	gobject_klass->finalize	    = gog_theme_finalize;
}

static void
gog_theme_init (GogTheme *theme)
{
}

GSF_CLASS (GogTheme, gog_theme,
	   gog_theme_class_init, gog_theme_init,
	   G_TYPE_OBJECT)

/* g_type_class_peek_parent */
static GOColor ms_palette [] = {
	0x9c9cffff, 0x9c3163ff, 0xffffceff, 0xceffffff, 0x630063ff,
	0xff8484ff, 0x0063ceff, 0xceceffff, 0x000084ff, 0xff00ffff,
	0xffff00ff, 0x00ffffff, 0x840084ff, 0x840000ff, 0x008484ff,
	0x0000ffff, 0x00ceffff, 0xceffffff, 0xceffceff, 0xffff9cff,
	0x9cceffff, 0xff9cceff, 0xce9cffff, 0xffce9cff, 0x3163ffff,
	0x31ceceff, 0x9cce00ff, 0xffce00ff, 0xff9c00ff, 0xff6300ff,
	0x63639cff, 0x949494ff, 0x003163ff, 0x319c63ff, 0x003100ff,
	0x313100ff, 0x9c3100ff, 0x9c3163ff, 0x31319cff, 0x313131ff,
	0xffffffff, 0xff0000ff, 0x00ff00ff, 0x0000ffff, 0xffff00ff,
	0xff00ffff, 0x00ffffff, 0x840000ff, 0x008400ff, 0x000084ff,
	0x848400ff, 0x840084ff, 0x008484ff, 0xc6c6c6ff, 0x848484ff
};
static GOColor guppi_palette[] = {
	0xff3000ff, 0x80ff00ff, 0x00ffcfff, 0x2000ffff,
	0xff008fff, 0xffbf00ff, 0x00ff10ff, 0x009fffff,
	0xaf00ffff, 0xff0000ff, 0xafff00ff, 0x00ff9fff,
	0x0010ffff, 0xff00bfff, 0xff8f00ff, 0x20ff00ff,
	0x00cfffff, 0x8000ffff, 0xff0030ff, 0xdfff00ff,
	0x00ff70ff, 0x0040ffff, 0xff00efff, 0xff6000ff,
	0x50ff00ff, 0x00ffffff, 0x5000ffff, 0xff0060ff,
	0xffef00ff, 0x00ff40ff, 0x0070ffff, 0xdf00ffff
};

void
gog_theme_init_style (GogStyle *style, unsigned i)
{
	if (style != NULL) {
		i %= G_N_ELEMENTS (ms_palette);
		style->fill.type = GOG_FILL_STYLE_SOLID;
		style->fill.u.solid.color = ms_palette[i];
	}
}
