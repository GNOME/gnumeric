/*
 * gnm-dao.h:  Implements a widget to specify tool output location.
 *
 * Copyright (c) 2003 Andreas J. Guelzow <aguelzow@taliesin.ca>
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

#ifndef __GNM_DAO_H__
#define __GNM_DAO_H__

#include <gnumeric-fwd.h>
#include <gui-util.h>
#include <tools/dao.h>

#define GNM_DAO_TYPE        (gnm_dao_get_type ())
#define GNM_DAO(obj)        (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_DAO_TYPE, GnmDao))
#define GNM_IS_DAO(obj)     (G_TYPE_CHECK_INSTANCE_TYPE((obj), GNM_DAO_TYPE))

typedef struct _GnmDao GnmDao;


GType		gnm_dao_get_type	(void);
GtkWidget *	gnm_dao_new	(WBCGtk *wbcg, gchar *inplace_str);
gboolean        gnm_dao_get_data (GnmDao *gdao, data_analysis_output_t **dao);
void            gnm_dao_set_put (GnmDao *gdao, gboolean show_put,
				 gboolean put_formulas);
gboolean        gnm_dao_is_ready (GnmDao *gdao);
gboolean        gnm_dao_is_finite (GnmDao *gdao);
void            gnm_dao_load_range (GnmDao *gdao, GnmRange const *range);
void            gnm_dao_focus_output_range (GnmDao *gdao);
void            gnm_dao_set_inplace (GnmDao *gdao, gchar *inplace_str);

#endif /*__GNM_DAO_H__*/

