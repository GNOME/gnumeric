/*
 * sheet-object-component.h
 *
 * Copyright (C) 2009 Jean Bréfort <jean.brefort@normalesup.org>
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

#ifndef _GNM_SHEET_OBJECT_COMPONENT_H_
#define _GNM_SHEET_OBJECT_COMPONENT_H_

#include <sheet-object.h>
#include <gnumeric-fwd.h>
#include <goffice/component/go-component.h>

G_BEGIN_DECLS

#define GNM_SO_COMPONENT_TYPE  (sheet_object_component_get_type ())
#define GNM_IS_SO_COMPONENT(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SO_COMPONENT_TYPE))
#define GNM_SO_COMPONENT(o)	 (G_TYPE_CHECK_INSTANCE_CAST((o), GNM_SO_COMPONENT_TYPE, SheetObjectComponent))

GType	     sheet_object_component_get_type (void);
SheetObject *sheet_object_component_new  (GOComponent *component);

GOComponent *sheet_object_component_get_component (SheetObject *soc);
void	     sheet_object_component_set_component (SheetObject *soc, GOComponent *component);

G_END_DECLS

#endif	/* _GNM_SHEET_OBJECT_COMPONENT_H_ */
