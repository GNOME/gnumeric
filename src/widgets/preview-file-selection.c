/* -*- mode: c; style: linux -*- */

/* preview-file-selection.c
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Written by Rachel Hestilow <hestilow@ximian.com> 
 *
 * Modified for use in gnumeric by
 *            Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>

#include "gui-util.h"
#include <gtk/gtkimage.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkframe.h>
#include <gtk/gtklabel.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtkmain.h>
#include "preview-file-selection.h"
#include <gsf/gsf-utils.h>

#define HSCALE 100
#define VSCALE 120

struct _PreviewFileSelectionPrivate
{
	GtkWidget *preview;	
	GtkWidget *label;
};

enum
{
	PROP_0,
	PROP_DO_PREVIEW
};

static GObjectClass *parent_class;

static void preview_file_selection_set_property (GObject *object, guint arg_id, const GValue *value, GParamSpec *spec);
static void preview_file_selection_get_property (GObject *object, guint arg_id, GValue *value, GParamSpec *spec);

static void
preview_file_selection_finalize (GObject *object)
{
	PreviewFileSelection *fsel = PREVIEW_FILE_SELECTION (object);

	g_free (fsel->priv);

	parent_class->finalize (object);
}

static void
preview_file_selection_class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (GTK_TYPE_FILE_SELECTION);
	
	object_class->set_property = preview_file_selection_set_property; 
	object_class->get_property = preview_file_selection_get_property;
	object_class->finalize = preview_file_selection_finalize;

	g_object_class_install_property
		(object_class, PROP_DO_PREVIEW,
		 g_param_spec_boolean ("do_preview",
			 	       "Preview images",
			 	       "Whether to preview images",
				       TRUE,
				       G_PARAM_READWRITE |
				       G_PARAM_CONSTRUCT_ONLY));
}

static void
preview_file_selection_init (GObject *object)
{
	PreviewFileSelection *fsel = PREVIEW_FILE_SELECTION (object);

	fsel->priv = g_new0 (PreviewFileSelectionPrivate, 1);
}

GType
preview_file_selection_get_type (void)
{
	static GType type = 0;

	if (!type)
	{
		GTypeInfo info = 
		{
			sizeof (PreviewFileSelectionClass),
			NULL,
			NULL,
			(GClassInitFunc) preview_file_selection_class_init,
			NULL,
			NULL,
			sizeof (PreviewFileSelection),
			0,
			(GInstanceInitFunc) preview_file_selection_init,
			NULL
		};

		type = g_type_register_static (GTK_TYPE_FILE_SELECTION,
					       "PreviewFileSelection",
					       &info, 0);
	}

	return type;
}

GtkWidget*
preview_file_selection_new (const gchar *title, gboolean do_preview)
{
	return GTK_WIDGET (
		g_object_new (PREVIEW_FILE_SELECTION_TYPE,
			      "title", title,
			      "do_preview", do_preview,
			      NULL));
}

static void
preview_file_selection_update (PreviewFileSelection *fsel, gpointer data)
{
	GdkPixbuf *buf;
	const gchar *filename;

	g_return_if_fail (IS_PREVIEW_FILE_SELECTION (fsel));
	
	filename = gtk_file_selection_get_filename (GTK_FILE_SELECTION (fsel));

	if (filename) {
		char const *name = gsf_extension_pointer (filename);

		
		if ((name == NULL 
		     || g_ascii_strcasecmp (name, "gnumeric") != 0)
		    && (buf = gdk_pixbuf_new_from_file (filename, NULL)))
		{
			int w, h;
			char *size;
			GdkPixbuf *scaled = gnm_pixbuf_intelligent_scale 
				(buf, HSCALE, VSCALE); 
			gtk_image_set_from_pixbuf 
				(GTK_IMAGE (fsel->priv->preview),
				 scaled);
			g_object_unref (scaled);
			
			w = gdk_pixbuf_get_width (buf);
			h = gdk_pixbuf_get_height (buf);
			
			size = g_strdup_printf (_("%d x %d"), w, h);
			gtk_label_set_text (GTK_LABEL (fsel->priv->label), 
					    size);
			g_free (size);
			
			g_object_unref (buf);
			return;
		}
	}

	gtk_image_set_from_file (GTK_IMAGE (fsel->priv->preview), NULL);
	gtk_label_set_text (GTK_LABEL (fsel->priv->label), " ");
}

static void
preview_file_selection_add_preview (PreviewFileSelection *fsel)
{
	GtkWidget *hbox, *frame, *vbox;
	
	g_return_if_fail (IS_PREVIEW_FILE_SELECTION (fsel));

	hbox = GTK_FILE_SELECTION (fsel)->file_list;
	do
	{
		hbox = hbox->parent;
		if (!hbox)
		{
			g_warning ("Can't find an hbox, using a normal file selection");
			return;
		}
	} while (!GTK_IS_HBOX (hbox));
	
	frame = gtk_frame_new (_("Preview"));
	gtk_widget_set_size_request (frame, HSCALE + 10, VSCALE + 10);
	gtk_widget_show (frame);
	gtk_box_pack_end (GTK_BOX (hbox), frame, FALSE, FALSE, 0);

	vbox = gtk_vbox_new (FALSE, 2);
	gtk_widget_show (vbox);
	gtk_container_add (GTK_CONTAINER (frame), vbox);
	
	fsel->priv->preview = gtk_image_new ();
	gtk_box_pack_start (GTK_BOX (vbox), fsel->priv->preview, FALSE, FALSE, 0);
	gtk_widget_show (fsel->priv->preview);
	
	fsel->priv->label = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (vbox), fsel->priv->label, FALSE, FALSE, 0);
	gtk_widget_show (fsel->priv->label);

	g_signal_connect_data (G_OBJECT (gtk_tree_view_get_selection (GTK_TREE_VIEW (GTK_FILE_SELECTION (fsel)->file_list))), "changed", (GCallback) preview_file_selection_update, fsel, NULL, G_CONNECT_AFTER | G_CONNECT_SWAPPED);

	preview_file_selection_update (fsel, NULL);
}

static void
preview_file_selection_set_property (GObject *object, guint arg_id, const GValue *value, GParamSpec *spec)
{
	PreviewFileSelection *fsel = PREVIEW_FILE_SELECTION (object);

	switch (arg_id)
	{
	case PROP_DO_PREVIEW:
		if (g_value_get_boolean (value))
			preview_file_selection_add_preview (fsel);
		break;
	}
}

static void preview_file_selection_get_property (GObject *object, guint arg_id, GValue *value, GParamSpec *spec)
{
	PreviewFileSelection *fsel = PREVIEW_FILE_SELECTION (object);

	switch (arg_id)
	{
	case PROP_DO_PREVIEW:
		g_value_set_boolean (value, fsel->priv->preview != NULL);
		break;
	}
}

void 
preview_file_selection_set_filename (GtkFileSelection *filesel,
				     const gchar      *filename)
{
	gtk_file_selection_set_filename (filesel, filename);
	preview_file_selection_update (PREVIEW_FILE_SELECTION (filesel), NULL);
}
