/*
 *  Copyright (C) 2003 Andreas J. Guelzow
 *
 *  based on code by:
 *  Copyright (C) 2000 Marco Pesenti Gritti
 *  from the galeon code base
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef GNUMERIC_WIDGET_LOCALE_SELECTOR_H
#define GNUMERIC_WIDGET_LOCALE_SELECTOR_H

G_BEGIN_DECLS

#include <gui-gnumeric.h>
#include <gtk/gtkhbox.h>

#define LOCALE_SELECTOR_TYPE        (locale_selector_get_type ())
#define LOCALE_SELECTOR(obj)        (G_TYPE_CHECK_INSTANCE_CAST((obj), LOCALE_SELECTOR_TYPE, LocaleSelector))
#define IS_LOCALE_SELECTOR(obj)     (G_TYPE_CHECK_INSTANCE_TYPE((obj), LOCALE_SELECTOR_TYPE))

typedef struct _LocaleSelector LocaleSelector;

GType        locale_selector_get_type (void);
GtkWidget *  locale_selector_new (void);

gchar       *locale_selector_get_locale (LocaleSelector *cs);
gboolean     locale_selector_set_locale (LocaleSelector *cs, const char *loc);

void         locale_selector_set_sensitive (LocaleSelector *cs, gboolean sensitive);

const char  *locale_selector_get_locale_name (LocaleSelector *cs, const char *loc);

G_END_DECLS

#endif /* GNUMERIC_WIDGET_LOCALE_SELECTOR_H */
