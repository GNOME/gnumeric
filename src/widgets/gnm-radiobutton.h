/*
 * gnm-radiobutton.h: Implements a special radiobutton.
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

#ifndef __GNM_RADIO_BUTTON_H__
#define __GNM_RADIO_BUTTON_H__

#include <gtk/gtk.h>

#define GNM_TYPE_RADIO_BUTTON        (gnm_radiobutton_get_type ())
#define GNM_RADIO_BUTTON(obj)        (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_RADIO_BUTTON_TYPE, GnmRadioButton))
#define GNM_IS_RADIO_BUTTON(obj)     (G_TYPE_CHECK_INSTANCE_TYPE((obj), GNM_RADIO_BUTTON_TYPE))

typedef GtkRadioButton GnmRadioButton;

GType		gnm_radiobutton_get_type	(void);

#endif /*__GNM_RADIO_BUTTON_H__*/
