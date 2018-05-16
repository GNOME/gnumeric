/*
 * gnm-radiobutton.c: Implements a special radiobutton
 *
 * Copyright (c) 2009 Morten Welinder <terra@gnome.org>
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
#include <widgets/gnm-radiobutton.h>
#include <gsf/gsf-impl-utils.h>

typedef GtkRadioButtonClass GnmRadioButtonClass;

static void
gnm_radiobutton_class_init (GnmRadioButtonClass *class)
{
	GtkButtonClass *button_class = GTK_BUTTON_CLASS (class);

	if (gdk_screen_get_default ()) {
		GtkWidget *tb = gtk_toggle_button_new ();

		button_class->clicked = GTK_BUTTON_GET_CLASS(tb)->clicked;

		g_object_ref_sink (tb);
		gtk_widget_destroy (tb);
		g_object_unref (tb);
	} else {
		// Introspection
	}
}

GSF_CLASS (GnmRadioButton, gnm_radiobutton,
	   gnm_radiobutton_class_init, NULL, GTK_TYPE_RADIO_BUTTON)
