/**
 * widget-number-format-selector.h:  Implements a widget to select number format.
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
 **/

#ifndef __WIDGET_FORMAT_SELECTOR_H__
#define __WIDGET_FORMAT_SELECTOR_H__

#include <gui-gnumeric.h>
#include <gui-util.h>

#define NUMBER_FORMAT_SELECTOR_TYPE        (number_format_selector_get_type ())
#define NUMBER_FORMAT_SELECTOR(obj)        (G_TYPE_CHECK_INSTANCE_CAST((obj), NUMBER_FORMAT_SELECTOR_TYPE, NumberFormatSelector))
#define IS_NUMBER_FORMAT_SELECTOR(obj)     (G_TYPE_CHECK_INSTANCE_TYPE((obj), NUMBER_FORMAT_SELECTOR_TYPE))

/* The available format widgets */
typedef enum {
    F_GENERAL,		F_SEPARATOR,
    F_SYMBOL_LABEL,	F_SYMBOL,	F_DELETE,
    F_ENTRY,		F_LIST_SCROLL,	F_LIST,
    F_TEXT,		F_DECIMAL_SPIN,	F_NEGATIVE_SCROLL,
    F_NEGATIVE,         F_LIST_BOX,
    F_DECIMAL_LABEL,	F_CODE_LABEL,	F_SYMBOL_BOX,
    F_DECIMAL_BOX,	F_CODE_BOX,	F_MAX_WIDGET
} FormatWidget;

typedef struct {
	GtkHBox 	box;
	GladeXML 	*gui;

	Value		*value;

	gboolean	enable_edit;

	GnmDateConventions const *date_conv;

	struct {
		GtkLabel	*preview;
		GtkFrame	*preview_frame;
		GtkWidget	*widget[F_MAX_WIDGET];
		GtkOptionMenu	*menu;
		GtkSizeGroup    *size_group;

		struct {
			GtkTreeView 		*view;
			GtkListStore		*model;
			GtkTreeSelection 	*selection;
		} negative_types;

		struct {
			GtkTreeView	 *view;
			GtkListStore	 *model;
			GtkTreeSelection *selection;
		} formats;

		StyleFormat	*spec;
		gint		current_type;
		int		num_decimals;
		int		negative_format;
		int		currency_index;
		gboolean	use_separator;
	} format;
} NumberFormatSelector;

typedef struct {
	GtkHBoxClass parent_class;

	gboolean (*number_format_changed) (NumberFormatSelector *nfs, const char *fmt);
} NumberFormatSelectorClass;

GType		number_format_selector_get_type	(void);
GtkWidget * 	number_format_selector_new  	(void);

void		number_format_selector_set_focus (NumberFormatSelector *nfs);
void		number_format_selector_set_style_format (NumberFormatSelector *nfs,
							 StyleFormat *style_format);
void		number_format_selector_set_value (NumberFormatSelector *nfs,
						  Value const *value);
void		number_format_selector_set_date_conv (NumberFormatSelector *nfs,
						      GnmDateConventions const *date_conv);
void		number_format_selector_editable_enters (NumberFormatSelector *nfs,
							GtkWindow *window);

#endif /*__WIDGET_FORMAT_SELECTOR_H__*/

