/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-data-simple.h : 
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
#ifndef GO_DATA_SIMPLE_H
#define GO_DATA_SIMPLE_H

#include <goffice/graph/goffice-graph.h>
#include <goffice/graph/go-data.h>

#define GO_DATA_SCALAR_VAL_TYPE  (go_data_scalar_val_get_type ())
#define GO_DATA_SCALAR_VAL(o)	 (G_TYPE_CHECK_INSTANCE_CAST ((o), GO_DATA_SCALAR_VAL_TYPE, GODataScalarVal))
#define IS_GO_DATA_SCALAR_VAL(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GO_DATA_SCALAR_VAL_TYPE))

typedef struct _GODataScalarVal GODataScalarVal;
GType	 go_data_scalar_val_get_type (void);
GOData	*go_data_scalar_val_new      (double val);

#define GO_DATA_SCALAR_STR_TYPE  (go_data_scalar_str_get_type ())
#define GO_DATA_SCALAR_STR(o)	 (G_TYPE_CHECK_INSTANCE_CAST ((o), GO_DATA_SCALAR_STR_TYPE, GODataScalarStr))
#define IS_GO_DATA_SCALAR_STR(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GO_DATA_SCALAR_STR_TYPE))

typedef struct _GODataScalarStr GODataScalarStr;
GType	 go_data_scalar_str_get_type (void);
GOData	*go_data_scalar_str_new      (char const *str, gboolean needs_free);

#endif /* GO_DATA_SIMPLE_H */
