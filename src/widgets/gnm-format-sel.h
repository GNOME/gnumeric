/*
 * gnm-format-sel.h: Some gnumeric specific utilities for the format selector
 *
 * Copyright (c) 2005 Jody Goldberg <jody@gnome.org>
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

#ifndef GNM_FORMAT_SEL_H
#define GNM_FORMAT_SEL_H

#include <gnumeric.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

GtkWidget *gnm_format_sel_new (void);
void       gnm_format_sel_set_value (GOFormatSel *nfs, GnmValue const *value);

G_END_DECLS

#endif /* GNM_FORMAT_SEL_H */
