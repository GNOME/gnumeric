/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifndef _GNM_SHEET_OBJECT_COMPONENT_H_
#define _GNM_SHEET_OBJECT_COMPONENT_H_

#include "sheet-object.h"
#include "gnumeric-fwd.h"
#include <goffice/component/go-component.h>

G_BEGIN_DECLS

#define SHEET_OBJECT_COMPONENT_TYPE  (sheet_object_component_get_type ())
#define IS_SHEET_OBJECT_COMPONENT(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_COMPONENT_TYPE))
#define SHEET_OBJECT_COMPONENT(o)	 (G_TYPE_CHECK_INSTANCE_CAST((o), SHEET_OBJECT_COMPONENT_TYPE, SheetObjectComponent))

GType	     sheet_object_component_get_type (void);
SheetObject *sheet_object_component_new  (GOComponent *component);

GOComponent *sheet_object_component_get_component (SheetObject *soc);
void	     sheet_object_component_set_component (SheetObject *soc, GOComponent *component);

G_END_DECLS

#endif	/* _GNM_SHEET_OBJECT_COMPONENT_H_ */
