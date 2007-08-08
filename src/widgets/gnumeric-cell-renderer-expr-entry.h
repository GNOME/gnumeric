/* gnumeric-cell-renderer-expr-entry.h
 * Copyright (C) 2002  Andreas J. Guelzow <aguelzow@taliesin.ca>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __GNUMERIC_CELL_RENDERER_EXPR_ENTRY_H__
#define __GNUMERIC_CELL_RENDERER_EXPR_ENTRY_H__

#include "gnumeric-cell-renderer-text.h"
#include "gnumeric-expr-entry.h"
#include <gui-gnumeric.h>

G_BEGIN_DECLS

#define GNUMERIC_TYPE_CELL_RENDERER_EXPR_ENTRY		(gnumeric_cell_renderer_expr_entry_get_type ())
#define GNUMERIC_CELL_RENDERER_EXPR_ENTRY(obj)		(GTK_CHECK_CAST ((obj), GNUMERIC_TYPE_CELL_RENDERER_EXPR_ENTRY, GnumericCellRendererExprEntry))
#define GNUMERIC_CELL_RENDERER_EXPR_ENTRY_CLASS(klass)	(GTK_CHECK_CLASS_CAST ((klass), GNUMERIC_TYPE_CELL_RENDERER_EXPR_ENTRY, GnumericCellRendererExprEntryClass))
#define GNUMERIC_IS_CELL_RENDERER_EXPR_ENTRY(obj)		(GTK_CHECK_TYPE ((obj), GNUMERIC_TYPE_CELL_RENDERER_EXPR_ENTRY))
#define GNUMERIC_IS_CELL_RENDERER_EXPR_ENTRY_CLASS(klass)	(GTK_CHECK_CLASS_TYPE ((klass), GNUMERIC_TYPE_CELL_RENDERER_EXPR_ENTRY))
#define GNUMERIC_CELL_RENDERER_EXPR_ENTRY_GET_CLASS(obj)   (GTK_CHECK_GET_CLASS ((obj), GNUMERIC_TYPE_CELL_RENDERER_EXPR_ENTRY, GnumericCellRendererExprEntryClass))

typedef struct _GnumericCellRendererExprEntry      GnumericCellRendererExprEntry;
typedef struct _GnumericCellRendererExprEntryClass GnumericCellRendererExprEntryClass;

struct _GnumericCellRendererExprEntry {
	GnumericCellRendererText parent;

	WBCGtk *wbcg;
	GnmExprEntry	   *entry;
};

struct _GnumericCellRendererExprEntryClass {
	GnumericCellRendererTextClass parent_class;
};

GType            gnumeric_cell_renderer_expr_entry_get_type (void);
GtkCellRenderer *gnumeric_cell_renderer_expr_entry_new      (WBCGtk *wbcg);
void             gnumeric_cell_renderer_expr_entry_editing_done (GtkCellEditable *entry,
						 GnumericCellRendererExprEntry *celltext);

G_END_DECLS


#endif /* __GNUMERIC_CELL_RENDERER_EXPR_ENTRY_H__ */
