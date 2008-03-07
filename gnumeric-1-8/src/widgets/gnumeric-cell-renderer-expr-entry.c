/* gnumeric-cell-renderer-expr-entry.c
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

#include <gnumeric-config.h>
#include "gnumeric-cell-renderer-expr-entry.h"
#include "gnumeric-expr-entry.h"
#include "wbc-gtk.h"

#define GNUMERIC_CELL_RENDERER_EXPR_ENTRY_PATH "gnumeric-cell-renderer-expr-entry-path"

static void gnumeric_cell_renderer_expr_entry_class_init
            (GnumericCellRendererExprEntryClass *cell_expr_entry_class)           ;

static GnumericCellRendererTextClass *parent_class = NULL;

static GtkCellEditable *gnumeric_cell_renderer_expr_entry_start_editing
                                                             (GtkCellRenderer      *cell,
							      GdkEvent             *event,
							      GtkWidget            *widget,
							      const gchar          *path,
							      GdkRectangle        *background_area,
							      GdkRectangle         *cell_area,
							      GtkCellRendererState  flags);

GType
gnumeric_cell_renderer_expr_entry_get_type (void)
{
	static GType cell_expr_entry_type = 0;

	if (!cell_expr_entry_type)
	{
		static const GTypeInfo cell_expr_entry_info =
			{
				sizeof (GnumericCellRendererExprEntryClass),
				NULL,		/* base_init */
				NULL,		/* base_finalize */
				(GClassInitFunc)gnumeric_cell_renderer_expr_entry_class_init,
				NULL,		/* class_finalize */
				NULL,		/* class_data */
				sizeof (GnumericCellRendererExprEntry),
				0,              /* n_preallocs */
				(GInstanceInitFunc) NULL,
			};

		cell_expr_entry_type = g_type_register_static (GNUMERIC_TYPE_CELL_RENDERER_TEXT,
							       "GnumericCellRendererExprEntry",
							       &cell_expr_entry_info, 0);
	}

	return cell_expr_entry_type;
}


static void
gnumeric_cell_renderer_expr_entry_class_init (GnumericCellRendererExprEntryClass *class)
{
	GtkCellRendererClass *cell_class = GTK_CELL_RENDERER_CLASS  (class);
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	parent_class = g_type_class_peek_parent (object_class);

	cell_class->start_editing = gnumeric_cell_renderer_expr_entry_start_editing;

}


GtkCellRenderer *
gnumeric_cell_renderer_expr_entry_new (WBCGtk *wbcg)
{
	GnumericCellRendererExprEntry *this =
		GNUMERIC_CELL_RENDERER_EXPR_ENTRY(g_object_new
						  (GNUMERIC_TYPE_CELL_RENDERER_EXPR_ENTRY, NULL));
	this->wbcg = wbcg;
	return GTK_CELL_RENDERER (this);
}

void
gnumeric_cell_renderer_expr_entry_editing_done (GtkCellEditable *entry,
						GnumericCellRendererExprEntry *celltext)
{
  const gchar *path;
  const gchar *new_text;

  celltext->entry = NULL;
  if (gnm_expr_entry_editing_canceled (GNM_EXPR_ENTRY (entry)))
	  return;

  wbcg_set_entry (celltext->wbcg, NULL);
  path = g_object_get_data (G_OBJECT (entry), GNUMERIC_CELL_RENDERER_EXPR_ENTRY_PATH);
  new_text = gnm_expr_entry_get_text (GNM_EXPR_ENTRY (entry));

  g_signal_emit_by_name (G_OBJECT (celltext), "edited", path, new_text);
}

static GtkCellEditable *
gnumeric_cell_renderer_expr_entry_start_editing (GtkCellRenderer      *cell,
				      G_GNUC_UNUSED GdkEvent *event,
				      G_GNUC_UNUSED GtkWidget *widget,
				      const gchar          *path,
				      G_GNUC_UNUSED GdkRectangle *background_area,
				      G_GNUC_UNUSED GdkRectangle *cell_area,
				      G_GNUC_UNUSED GtkCellRendererState flags)
{
  GnumericCellRendererExprEntry *celltext;
  GtkEntry *entry;
  GnmExprEntry *gentry;

  celltext = GNUMERIC_CELL_RENDERER_EXPR_ENTRY (cell);

  /* If the cell isn't editable we return NULL. */
  if (celltext->parent.parent.editable == FALSE)
    return NULL;

  gentry = gnm_expr_entry_new (celltext->wbcg, FALSE);
  celltext->entry = gentry;
  entry  = gnm_expr_entry_get_entry (gentry);

  gtk_entry_set_text (entry, celltext->parent.parent.text);
  g_object_set_data_full (G_OBJECT (gentry), GNUMERIC_CELL_RENDERER_EXPR_ENTRY_PATH, g_strdup (path), g_free);

  gtk_editable_select_region (GTK_EDITABLE (entry), 0, -1);

  gtk_widget_show_all (GTK_WIDGET (gentry));
  g_signal_connect (gentry,
		    "editing_done",
		    G_CALLBACK (gnumeric_cell_renderer_expr_entry_editing_done),
		    celltext);

  wbcg_set_entry (celltext->wbcg, gentry);

  return GTK_CELL_EDITABLE (gentry);
}
