/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-gui-utils.h - Misc GTK+ utilities
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */
#ifndef GO_GUI_UTILS_H
#define GO_GUI_UTILS_H

#include <gtk/gtkmessagedialog.h>
#include <glade/glade-xml.h>
#include <gtk/gtkfilechooser.h>
#include <goffice/app/goffice-app.h>

G_BEGIN_DECLS

typedef struct {
	char *name;
	char *desc;
	char *ext;
	gboolean has_pixbuf_saver;
} GOImageType;

void	   go_editable_enters (GtkWindow *window, GtkWidget *w);

GladeXML  *go_libglade_new (char const *gladefile, char const *root,
			    char const *domain, GOCmdContext *cc);

GdkPixbuf *go_pixbuf_new_from_file	(char const *filename);
GdkPixbuf *go_pixbuf_intelligent_scale	(GdkPixbuf *pixbuf, 
					 guint width, guint height);

int	   go_pango_measure_string	(PangoContext *context,
					 PangoFontDescription const *font_desc,
					 char const *str);

gint       go_gtk_dialog_run		(GtkDialog *dialog, GtkWindow *parent);
void       go_gtk_notice_dialog		(GtkWindow *parent, GtkMessageType type, 
					 char const *str);
void       go_gtk_notice_nonmodal_dialog (GtkWindow *parent, GtkWidget **ref,
					  GtkMessageType type, char const *str);
gboolean   go_gtk_query_yes_no		(GtkWindow *toplevel, char const *message,
					 gboolean default_answer);

GtkWidget *go_gtk_button_new_with_stock (char const *text,
					 char const *stock_id);
void	   go_gtk_widget_disable_focus	(GtkWidget *w);
void       go_gtk_window_set_transient  (GtkWindow *parent,   GtkWindow *window);
void	   go_gtk_help_button_init	(GtkWidget *w, char const *data_dir,
					 char const *app, char const *link);
void       go_gtk_nonmodal_dialog	(GtkWindow *toplevel, GtkWindow *dialog);
gboolean   go_gtk_file_sel_dialog	(GtkWindow *toplevel, GtkWidget *w);
char	  *go_gtk_select_image		(GtkWindow *toplevel, const char *initial);
char	  *gui_get_image_save_info	(GtkWindow *toplevel, GSList *formats, 
					 GOImageType const **ret_format);
gboolean   go_gtk_url_is_writeable	(GtkWindow *parent, char const *url,
					 gboolean overwrite_by_default);

void	   go_atk_setup_label	 	(GtkWidget *label, GtkWidget *target);

G_END_DECLS

#endif /* GO_GUI_UTILS_H */
