/*
 * font.h
 *
 * Copyright (C) 1999 Rasca, Berlin
 * EMail: thron@gmx.de
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
 */

#ifndef G_PLUGIN_FONT_H_
#define G_PLUGIN_FONT_H_

#include <gnumeric.h>

int font_is_monospaced (GnmStyle const *mstyle);
int font_is_helvetica  (GnmStyle const *mstyle);
int font_is_sansserif  (GnmStyle const *mstyle);

#endif

