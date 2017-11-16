/*
 * gnm-so-path.h
 *
 * Copyright (C) 2012 Jean Bréfort <jean.brefort@normalesup.org>
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>
 */

#ifndef _GNM_SO_PATH_H_
# define _GNM_SO_PATH_H_

#include   <glib-object.h>

G_BEGIN_DECLS

#define GNM_SO_PATH_TYPE  (gnm_so_path_get_type ())
#define GNM_IS_SO_PATH(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SO_PATH_TYPE))
GType gnm_so_path_get_type (void);

G_END_DECLS

#endif /* _GNM_SO_PATH_H_ */
