/* 
 * gnumeric-optionmenu.h
 *
 * Copyright (C) 2002 Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * based extensively on:
 *
 * GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * Modified by the GTK+ Team and others 1997-2000.  See the GTK AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/. 
 *
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GNUMERIC_OPTION_MENU_H__
#define __GNUMERIC_OPTION_MENU_H__


#include <gdk/gdk.h>
#include <gtk/gtkbutton.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GNUMERIC_TYPE_OPTION_MENU              (gnumeric_option_menu_get_type ())
#define GNUMERIC_OPTION_MENU(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNUMERIC_TYPE_OPTION_MENU, GnumericOptionMenu))
#define GNUMERIC_OPTION_MENU_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GNUMERIC_TYPE_OPTION_MENU, GnumericOptionMenuClass))
#define GNUMERIC_IS_OPTION_MENU(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNUMERIC_TYPE_OPTION_MENU))
#define GNUMERIC_IS_OPTION_MENU_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GNUMERIC_TYPE_OPTION_MENU))
#define GNUMERIC_OPTION_MENU_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GNUMERIC_TYPE_OPTION_MENU, GnumericOptionMenuClass))


typedef struct _GnumericOptionMenu       GnumericOptionMenu;
typedef struct _GnumericOptionMenuClass  GnumericOptionMenuClass;

struct _GnumericOptionMenu
{
	GtkButton button;
	
	GtkWidget *menu;
	GtkWidget *select_menu;
	GtkWidget *menu_item;
	GtkWidget *old_menu_item;

	GtkWidget *last_signaled_menu_item;

	GSList *selection;
	gboolean new_selection;
	
	guint16 width;
	guint16 height;
};

struct _GnumericOptionMenuClass
{
  GtkButtonClass parent_class;

  void (*changed) (GnumericOptionMenu *option_menu);

  /* Padding for future expansion */
  void (*_gtk_reserved1) (void);
  void (*_gtk_reserved2) (void);
  void (*_gtk_reserved3) (void);
  void (*_gtk_reserved4) (void);
};


GType      gnumeric_option_menu_get_type    (void) G_GNUC_CONST;
GtkWidget* gnumeric_option_menu_new         (void);
GtkWidget* gnumeric_option_menu_get_menu    (GnumericOptionMenu *option_menu);
void       gnumeric_option_menu_set_menu    (GnumericOptionMenu *option_menu,
					     GtkWidget *menu);
void       gnumeric_option_menu_remove_menu (GnumericOptionMenu *option_menu);
void       gnumeric_option_menu_set_history (GnumericOptionMenu *option_menu,
					     GSList *selection);
GtkWidget *gnumeric_option_menu_get_history (GnumericOptionMenu *option_menu);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GNUMERIC _OPTION_MENU_H__ */
