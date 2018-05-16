/*
 * gnm-workbook-sel.h: A selector for workbooks.
 *
 * Copyright (c) 2018 Morten Welinder
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

#ifndef GNM_WORKBOOK_SEL_H
#define GNM_WORKBOOK_SEL_H

#include <gnumeric.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

GType gnm_workbook_sel_get_type (void);

#define GNM_TYPE_WORKBOOK_SEL             (gnm_workbook_sel_get_type ())
#define GNM_WORKBOOK_SEL(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNM_TYPE_WORKBOOK_SEL, GnmWorkbookSel))
#define GNM_IS_WORKBOOK_SEL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNM_TYPE_WORKBOOK_SEL))

typedef struct GnmWorkbookSel_ GnmWorkbookSel;

GtkWidget *gnm_workbook_sel_new (void);

void       gnm_workbook_sel_set_workbook (GnmWorkbookSel *wbs, Workbook *wb);
Workbook  *gnm_workbook_sel_get_workbook (GnmWorkbookSel *wbs);

G_END_DECLS

#endif /* GNM_WORKBOOK_SEL_H */
