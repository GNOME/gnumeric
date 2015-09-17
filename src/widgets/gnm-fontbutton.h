/* GTK - The GIMP Toolkit
 * Copyright (C) 1998 David Abilleira Freijeiro <odaf@nexo.es>
 * All rights reserved
 * Based on gnome-color-picker by Federico Mena <federico@nuclecu.unam.mx>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */
/*
 * Modified by the GTK+ Team and others 2003.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */

#ifndef __GNM_FONT_BUTTON_H__
#define __GNM_FONT_BUTTON_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* GnmFontButton is a button widget that allow user to select a font.
 */

#define GNM_TYPE_FONT_BUTTON             (gnm_font_button_get_type ())
#define GNM_FONT_BUTTON(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNM_TYPE_FONT_BUTTON, GnmFontButton))
#define GNM_FONT_BUTTON_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GNM_TYPE_FONT_BUTTON, GnmFontButtonClass))
#define GNM_IS_FONT_BUTTON(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNM_TYPE_FONT_BUTTON))
#define GNM_IS_FONT_BUTTON_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GNM_TYPE_FONT_BUTTON))
#define GNM_FONT_BUTTON_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GNM_TYPE_FONT_BUTTON, GnmFontButtonClass))

typedef struct _GnmFontButton        GnmFontButton;
typedef struct _GnmFontButtonClass   GnmFontButtonClass;
typedef struct _GnmFontButtonPrivate GnmFontButtonPrivate;

struct _GnmFontButton {
  GtkButton button;

  /*< private >*/
  GnmFontButtonPrivate *priv;
};

struct _GnmFontButtonClass {
  GtkButtonClass parent_class;

  /* font_set signal is emitted when font is chosen */
  void (* font_set) (GnmFontButton *gfp);

  /* Padding for future expansion */
  void (*_gtk_reserved1) (void);
  void (*_gtk_reserved2) (void);
  void (*_gtk_reserved3) (void);
  void (*_gtk_reserved4) (void);
};


GType                 gnm_font_button_get_type       (void);
GtkWidget            *gnm_font_button_new            (void);
GtkWidget            *gnm_font_button_new_with_font  (const gchar   *fontname);

const gchar *         gnm_font_button_get_title      (GnmFontButton *font_button);
void                  gnm_font_button_set_title      (GnmFontButton *font_button,
                                                      const gchar   *title);
gboolean              gnm_font_button_get_use_font   (GnmFontButton *font_button);
void                  gnm_font_button_set_use_font   (GnmFontButton *font_button,
                                                      gboolean       use_font);
gboolean              gnm_font_button_get_use_size   (GnmFontButton *font_button);
void                  gnm_font_button_set_use_size   (GnmFontButton *font_button,
                                                      gboolean       use_size);
const gchar *         gnm_font_button_get_font_name  (GnmFontButton *font_button);
gboolean              gnm_font_button_set_font_name  (GnmFontButton *font_button,
                                                      const gchar   *fontname);
gboolean              gnm_font_button_get_show_style (GnmFontButton *font_button);
void                  gnm_font_button_set_show_style (GnmFontButton *font_button,
                                                      gboolean       show_style);
gboolean              gnm_font_button_get_show_size  (GnmFontButton *font_button);
void                  gnm_font_button_set_show_size  (GnmFontButton *font_button,
                                                      gboolean       show_size);

G_END_DECLS


#endif /* __GNM_FONT_BUTTON_H__ */
