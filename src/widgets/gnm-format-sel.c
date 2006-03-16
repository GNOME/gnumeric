/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * gnm-format-sel.c: Gnumeric extensions to the format selector widget
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 **/

#include <gnumeric-config.h>
#include "gnm-format-sel.h"
#include "src/value.h"
#include "src/gnm-format.h"

static char *
cb_generate_preview (GOFormatSel *gfs, GOColor *c)
{
	GnmValue const *v = g_object_get_data (G_OBJECT (gfs), "value");
	GOFormat *fmt = go_format_sel_get_fmt (gfs);

	if (NULL == v)
		return NULL;
	if (go_format_is_general (fmt) && VALUE_FMT (v) != NULL)
		fmt = VALUE_FMT (v);
	return format_value (fmt, v, c, -1, go_format_sel_get_dateconv (gfs));
}

GtkWidget *
gnm_format_sel_new (void)
{
	GObject *w = g_object_new (GO_FORMAT_SEL_TYPE, NULL);
	g_signal_connect (w, "generate-preview",
		G_CALLBACK (cb_generate_preview), NULL);
	return GTK_WIDGET (w);
}

void
gnm_format_sel_set_value (GOFormatSel *gfs, GnmValue const *value)
{
  	g_return_if_fail (IS_GO_FORMAT_SEL (gfs));
	g_return_if_fail (value != NULL);

	g_object_set_data_full (G_OBJECT (gfs),
		"value", value_dup (value), (GDestroyNotify) value_release);
	go_format_sel_show_preview (gfs);
}
