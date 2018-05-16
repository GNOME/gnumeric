/*
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 **/

#include <gnumeric-config.h>
#include <widgets/gnm-format-sel.h>
#include <src/value.h>
#include <src/gnm-format.h>
#include <src/style-font.h>

static char *
cb_generate_preview (GOFormatSel *gfs, PangoAttrList **attrs)
{
	GnmValue const *v = g_object_get_data (G_OBJECT (gfs), "value");

	if (NULL == v)
		return NULL;
	else {
		GOFormat const *fmt = go_format_sel_get_fmt (gfs);
		GtkWidget *w = GTK_WIDGET (gfs);
		PangoContext *context = gtk_widget_get_pango_context (w);
		PangoLayout *layout = pango_layout_new (context);
		char *str;
		GOFormatNumberError err;

		if (go_format_is_general (fmt) && VALUE_FMT (v) != NULL)
			fmt = VALUE_FMT (v);
		err = format_value_layout (layout, fmt, v, -1,
					   go_format_sel_get_dateconv (gfs));
		if (err) {
			str = NULL;
			*attrs = NULL;
		} else {
			str = g_strdup (pango_layout_get_text (layout));
			go_pango_translate_layout (layout);
			*attrs = pango_attr_list_ref (pango_layout_get_attributes (layout));
		}
		g_object_unref (layout);
		return str;
	}
}

/**
 * gnm_format_sel_new:
 *
 * Returns: (transfer full): a new format selector
 */
GtkWidget *
gnm_format_sel_new (void)
{
	GObject *w = G_OBJECT (go_format_sel_new_full (TRUE));
	g_signal_connect (w, "generate-preview",
		G_CALLBACK (cb_generate_preview), NULL);
	return GTK_WIDGET (w);
}

void
gnm_format_sel_set_value (GOFormatSel *gfs, GnmValue const *value)
{
  	g_return_if_fail (GO_IS_FORMAT_SEL (gfs));
	g_return_if_fail (value != NULL);

	g_object_set_data_full (G_OBJECT (gfs),
		"value", value_dup (value), (GDestroyNotify) value_release);
	go_format_sel_show_preview (gfs);
}
