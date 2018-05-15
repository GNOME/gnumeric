/*
 * gnm-notebook.h: Implements a button-only notebook.
 *
 * Copyright (c) 2008 Morten Welinder <terra@gnome.org>
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

#ifndef __GNM_NOTEBOOK_H__
#define __GNM_NOTEBOOK_H__

#include <gtk/gtk.h>

GType		gnm_notebook_button_get_type	(void);
typedef struct GnmNotebookButton_ GnmNotebookButton;
#define GNM_NOTEBOOK_BUTTON_TYPE        (gnm_notebook_button_get_type ())
#define GNM_NOTEBOOK_BUTTON(obj)        (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_NOTEBOOK_BUTTON_TYPE, GnmNotebookButton))
#define GNM_IS_NOTEBOOK_BUTTON(obj)     (G_TYPE_CHECK_INSTANCE_TYPE((obj), GNM_NOTEBOOK_BUTTON_TYPE))

#define GNM_NOTEBOOK_TYPE        (gnm_notebook_get_type ())
#define GNM_NOTEBOOK(obj)        (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_NOTEBOOK_TYPE, GnmNotebook))
#define GNM_IS_NOTEBOOK(obj)     (G_TYPE_CHECK_INSTANCE_TYPE((obj), GNM_NOTEBOOK_TYPE))

typedef struct _GnmNotebook GnmNotebook;

GType		gnm_notebook_get_type	(void);

int             gnm_notebook_get_n_visible (GnmNotebook *nb);
GtkWidget *     gnm_notebook_get_nth_label (GnmNotebook *nb, int n);
GtkWidget *     gnm_notebook_get_current_label (GnmNotebook *nb);
void            gnm_notebook_insert_tab (GnmNotebook *nb, GtkWidget *label,
					 int pos);
void            gnm_notebook_move_tab (GnmNotebook *nb, GtkWidget *label, int newpos);
void            gnm_notebook_set_current_page (GnmNotebook *nb, int page);
void            gnm_notebook_prev_page (GnmNotebook *nb);
void            gnm_notebook_next_page (GnmNotebook *nb);

#endif /*__GNM_NOTEBOOK_H__*/
