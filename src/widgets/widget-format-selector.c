/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * widget-number-format-selector.c:  Implements a widget to select number format.
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

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>

#include <string.h>
#include <format.h>
#include <formats.h>
#include <mstyle.h>
#include <sheet.h>
#include <value.h>
#include <workbook.h>
#include <locale.h>
#include <gnm-marshalers.h>

#include <widgets/gnumeric-combo-text.h>

#include <gsf/gsf-impl-utils.h>

#include "widget-format-selector.h"

/* The maximum number of chars in the formatting sample */
#define FORMAT_PREVIEW_MAX 40

#define SETUP_LOCALE_SWITCH char *oldlocale = NULL

#define START_LOCALE_SWITCH if (nfs->locale) {\
currency_date_format_shutdown(); \
oldlocale = g_strdup(gnumeric_setlocale (LC_ALL, NULL)); \
gnumeric_setlocale(LC_ALL, nfs->locale);\
currency_date_format_init();}

#define END_LOCALE_SWITCH if (oldlocale) {\
currency_date_format_shutdown(); \
gnumeric_setlocale(LC_ALL, oldlocale);\
g_free (oldlocale);\
currency_date_format_init();}

static GtkHBoxClass *nfs_parent_class;

/* Signals we emit */
enum {
	NUMBER_FORMAT_CHANGED,
	LAST_SIGNAL
};

static guint nfs_signals[LAST_SIGNAL] = { 0 };

static void
generate_format (NumberFormatSelector *nfs)
{
	FormatFamily const page = nfs->format.current_type;
	GString		*new_format = g_string_new (NULL);

	/* It is a strange idea not to reuse FormatCharacteristics
	 in this file */
	/* So build one to pass to the style_format_... function */
	FormatCharacteristics format;

	format.thousands_sep = nfs->format.use_separator;
	format.num_decimals = nfs->format.num_decimals;
	format.negative_fmt = nfs->format.negative_format;
	format.currency_symbol_index = nfs->format.currency_index;
	format.list_element = 0; /* Don't need this one. */
	format.date_has_days = FALSE;
	format.date_has_months = FALSE;

	/* Update the format based on the current selections and page */
	switch (page) {
	case FMT_GENERAL :
	case FMT_TEXT :
		g_string_append (new_format, cell_formats[page][0]);
		break;

	case FMT_NUMBER :
		/* Make sure no currency is selected */
		format.currency_symbol_index = 0;

	case FMT_CURRENCY :
		style_format_number(new_format, &format);
		break;

	case FMT_ACCOUNT :
		style_format_account(new_format, &format);
		break;

	case FMT_PERCENT :
		style_format_percent(new_format, &format);
		break;

	case FMT_SCIENCE :
		style_format_science(new_format, &format);
		break;

	default :
		break;
	};

	if (new_format->len > 0) {
		char *tmp = style_format_str_as_XL (new_format->str, TRUE);
		gtk_entry_set_text (GTK_ENTRY (nfs->format.widget[F_ENTRY]),
				    tmp);
		g_free (tmp);
	}

	g_string_free (new_format, TRUE);
}

static void
draw_format_preview (NumberFormatSelector *nfs, gboolean regen_format)
{
	gchar		*preview;
	StyleFormat	*sf = NULL;

	if (regen_format)
		generate_format (nfs);

	/* Nothing to sample. */
	if (nfs->value == NULL)
		return;

	sf = nfs->format.spec;

	if (sf == NULL || nfs->value == NULL)
		return;

	if (style_format_is_general (sf) &&
	    VALUE_FMT (nfs->value) != NULL)
		sf = VALUE_FMT (nfs->value);

	preview = format_value (sf, nfs->value, NULL, -1, nfs->date_conv);
	if (strlen (preview) > FORMAT_PREVIEW_MAX)
		strcpy (&preview[FORMAT_PREVIEW_MAX - 5], " ...");

	gtk_label_set_text (nfs->format.preview, preview);
	g_free (preview);
}

static void
fillin_negative_samples (NumberFormatSelector *nfs)
{
	static char const *const decimals = "098765432109876543210987654321";
	static char const *const formats[4] = {
		"-%s%s3%s210%s%s%s%s",
		"%s%s3%s210%s%s%s%s",
		"(%s%s3%s210%s%s%s%s)",
		"(%s%s3%s210%s%s%s%s)"
	};
	int const n = 30 - nfs->format.num_decimals;

	int const page = nfs->format.current_type;
	char const *space_b = "", *currency_b;
	char const *space_a = "", *currency_a;
	const char *decimal;
	const char *thousand_sep;
	int i;
	GtkTreeIter  iter;
	GtkTreePath *path;
	SETUP_LOCALE_SWITCH;

	g_return_if_fail (page == 1 || page == 2);
	g_return_if_fail (nfs->format.num_decimals <= 30);

	START_LOCALE_SWITCH;
		
	if (nfs->format.use_separator)
		thousand_sep = format_get_thousand ();
	else
		thousand_sep = "";
	if (nfs->format.num_decimals > 0)
		decimal = format_get_decimal ();
	else
		decimal = "";

	if (page == 2) {
		currency_b = (const gchar *)currency_symbols[nfs->format.currency_index].symbol;
		/*
		 * FIXME : This should be better hidden.
		 * Ideally the render would do this for us.
		 */
		if (currency_b[0] == '[' && currency_b[1] == '$') {
			char const *end = strchr (currency_b+2, '-');
			if (end == NULL)
				end = strchr (currency_b+2, ']');
			currency_b = g_strndup (currency_b+2, end-currency_b-2);
		} else
			currency_b = g_strdup (currency_b);

		if (currency_symbols[nfs->format.currency_index].has_space)
			space_b = " ";

		if (!currency_symbols[nfs->format.currency_index].precedes) {
			currency_a = currency_b;
			currency_b = "";
			space_a = space_b;
			space_b = "";
		} else {
			currency_a = "";
		}
	} else
		currency_a = currency_b = "";

	gtk_list_store_clear (nfs->format.negative_types.model);

	for (i = 0 ; i < 4; i++) {
		char *buf = g_strdup_printf (formats[i],
					     currency_b, space_b, thousand_sep, decimal,
					     decimals + n, space_a, currency_a);
		gtk_list_store_append (nfs->format.negative_types.model, &iter);
		gtk_list_store_set (nfs->format.negative_types.model, &iter,
			0, i,
			1, buf,
			2, (i % 2) ? "red" : NULL,
			-1);
		g_free (buf);
	}

	/* If non empty then free the string */
	if (*currency_a)
		g_free ((char*)currency_a);
	if (*currency_b)
		g_free ((char*)currency_b);

	path = gtk_tree_path_new ();
	gtk_tree_path_append_index (path, nfs->format.negative_format);
	gtk_tree_selection_select_path (nfs->format.negative_types.selection, path);
	gtk_tree_path_free (path);

	END_LOCALE_SWITCH;
}

static void
cb_decimals_changed (GtkEditable *editable, NumberFormatSelector *nfs)
{
	int const page = nfs->format.current_type;

	nfs->format.num_decimals =
		gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (editable));

	if (page == 1 || page == 2)
		fillin_negative_samples (nfs);

	draw_format_preview (nfs, TRUE);
}

static void
cb_separator_toggle (GtkObject *obj, NumberFormatSelector *nfs)
{
	nfs->format.use_separator =
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (obj));
	fillin_negative_samples (nfs);

	draw_format_preview (nfs, TRUE);
}

static void
fmt_dialog_init_fmt_list (NumberFormatSelector *nfs, char const *const *formats,
			  GtkTreeIter *select)
{
	GtkTreeIter iter;
	char *fmt;
	char const *cur_fmt = nfs->format.spec->format;

	for (; *formats; formats++) {
		gtk_list_store_append (nfs->format.formats.model, &iter);
		fmt = style_format_str_as_XL (*formats, TRUE);
		gtk_list_store_set (nfs->format.formats.model, &iter,
			0, fmt, -1);
		g_free (fmt);

		if (!strcmp (*formats, cur_fmt))
			*select = iter;
	}
}

static void
fmt_dialog_enable_widgets (NumberFormatSelector *nfs, int page)
{
	SETUP_LOCALE_SWITCH;
	static FormatWidget const contents[][12] = {
		/* General */
		{
			F_GENERAL_EXPLANATION,
			F_MAX_WIDGET
		},
		/* Number */
		{
			F_NUMBER_EXPLANATION,
			F_DECIMAL_BOX,
			F_DECIMAL_LABEL,
			F_DECIMAL_SPIN,
			F_SEPARATOR,
			F_LIST_BOX,
			F_NEGATIVE_SCROLL,
			F_NEGATIVE,
			F_MAX_WIDGET
		},
		/* Currency */
		{
			F_CURRENCY_EXPLANATION,
			F_DECIMAL_BOX,
			F_DECIMAL_LABEL,
			F_DECIMAL_SPIN,
			F_SEPARATOR,
			F_SYMBOL_BOX,
			F_SYMBOL_LABEL,
			F_SYMBOL,
			F_LIST_BOX,
			F_NEGATIVE_SCROLL,
			F_NEGATIVE,
			F_MAX_WIDGET
		},
		/* Accounting */
		{
			F_ACCOUNTING_EXPLANATION,
			F_DECIMAL_BOX,
			F_DECIMAL_LABEL,
			F_DECIMAL_SPIN,
			F_SYMBOL_BOX,
			F_SYMBOL_LABEL,
			F_SYMBOL,
			F_MAX_WIDGET
		},
		/* Date */
		{
			F_DATE_EXPLANATION,
			F_LIST_BOX,
			F_LIST_SCROLL,
			F_LIST,
			F_MAX_WIDGET
		},
		/* Time */
		{
			F_TIME_EXPLANATION,
			F_LIST_BOX,
			F_LIST_SCROLL,
			F_LIST,
			F_MAX_WIDGET
		},
		/* Percentage */
		{
			F_PERCENTAGE_EXPLANATION,
			F_DECIMAL_BOX,
			F_DECIMAL_LABEL,
			F_DECIMAL_SPIN,
			F_MAX_WIDGET
		},
		/* Fraction */
		{
			F_FRACTION_EXPLANATION,
			F_LIST_BOX,
			F_LIST_SCROLL,
			F_LIST,
			F_MAX_WIDGET
		},
		/* Scientific */
		{
			F_SCIENTIFIC_EXPLANATION,
			F_DECIMAL_BOX,
			F_DECIMAL_LABEL,
			F_DECIMAL_SPIN,
			F_MAX_WIDGET
		},
		/* Text */
		{
			F_TEXT_EXPLANATION,
			F_MAX_WIDGET
		},
		/* Special */
		{
			F_SPECIAL_EXPLANATION,
			F_MAX_WIDGET
		},
		/* Custom */
		{
			F_CUSTOM_EXPLANATION,
			F_CODE_BOX,
			F_CODE_LABEL,
			F_ENTRY,
			F_DELETE,
			F_LIST_BOX,
			F_LIST_SCROLL,
			F_LIST,
			F_MAX_WIDGET
		}
	};

	int const old_page = nfs->format.current_type;
	int i;
	FormatWidget tmp;

	START_LOCALE_SWITCH;

	/* Hide widgets from old page */
	if (old_page >= 0)
		for (i = 0; (tmp = contents[old_page][i]) != F_MAX_WIDGET ; ++i)
			gtk_widget_hide (nfs->format.widget[tmp]);

	/* Set the default format if appropriate */
	if (page == FMT_GENERAL || page == FMT_ACCOUNT || page == FMT_FRACTION || page == FMT_TEXT) {
		FormatCharacteristics info;
		int list_elem = 0;
		char *tmp;
		if (page == cell_format_classify (nfs->format.spec, &info))
			list_elem = info.list_element;

		tmp = style_format_str_as_XL (cell_formats[page][list_elem], TRUE);
		gtk_entry_set_text (GTK_ENTRY (nfs->format.widget[F_ENTRY]), tmp);
		g_free (tmp);
	}

	nfs->format.current_type = page;
	for (i = 0; (tmp = contents[page][i]) != F_MAX_WIDGET ; ++i) {
		GtkWidget *w = nfs->format.widget[tmp];
		gtk_widget_show (w);

		if (tmp == F_LIST) {
			int start = 0, end = -1;
			GtkTreeIter select;

			switch (page) {
			case 4: case 5: case 7:
				start = end = page;
				break;

			case 11:
				start = 0; end = 8;
				break;

			default :
				g_assert_not_reached ();
			};

			select.stamp = 0;
			gtk_list_store_clear (nfs->format.formats.model);
			for (; start <= end ; ++start)
			  fmt_dialog_init_fmt_list (nfs,
						    cell_formats[start], &select);

			/* If this is the custom page and the format has
			 * not been found append it */
			/* TODO We should add the list of other custom formats created.
			 *      It should be easy.  All that is needed is a way to differentiate
			 *      the std formats and the custom formats in the StyleFormat hash.
			 */
			if  (page == 11 && select.stamp == 0) {
				char *tmp = style_format_as_XL (nfs->format.spec, TRUE);
				gtk_entry_set_text (GTK_ENTRY (nfs->format.widget[F_ENTRY]), tmp);
				g_free (tmp);
			} else if (select.stamp == 0)
				gtk_tree_model_get_iter_first (
					GTK_TREE_MODEL (nfs->format.formats.model),
					&select);

			if (select.stamp != 0)
				gtk_tree_selection_select_iter (
					nfs->format.formats.selection, &select);
		} else if (tmp == F_NEGATIVE)
			fillin_negative_samples (nfs);
		else if (tmp == F_DECIMAL_SPIN)
			gtk_spin_button_set_value (GTK_SPIN_BUTTON (nfs->format.widget[F_DECIMAL_SPIN]),
				nfs->format.num_decimals);
		else if (tmp == F_SEPARATOR)
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (nfs->format.widget[F_SEPARATOR]),
				nfs->format.use_separator);
	}

#if 0
	if ((cl = GTK_CLIST (nfs->format.widget[F_LIST])) != NULL)
		gnumeric_clist_make_selection_visible (cl);
#endif

	draw_format_preview (nfs, TRUE);

	END_LOCALE_SWITCH;
}

/*
 * Callback routine to manage the relationship between the number
 * formating radio buttons and the widgets required for each mode.
 */

static void
cb_format_class_changed (GtkOptionMenu *menu, NumberFormatSelector *nfs)
{
	int selection;

	selection = gtk_option_menu_get_history (menu);

	if (selection >= 0) {
		fmt_dialog_enable_widgets (nfs, selection);
	}
}

static void
cb_format_entry_changed (GtkEditable *w, NumberFormatSelector *nfs)
{
	char *fmt;
	if (!nfs->enable_edit)
		return;

	fmt = style_format_delocalize (gtk_entry_get_text (GTK_ENTRY (w)));
	if (strcmp (nfs->format.spec->format, fmt)) {
		style_format_unref (nfs->format.spec);
		nfs->format.spec = style_format_new_XL (fmt, FALSE);
		g_signal_emit (GTK_OBJECT (nfs),
			       nfs_signals[NUMBER_FORMAT_CHANGED], 0,
			       fmt);
		draw_format_preview (nfs, FALSE);
	}
	g_free (fmt);
}

static void
cb_format_list_select (GtkTreeSelection *selection, NumberFormatSelector *nfs)
{
	GtkTreeIter iter;
	gchar *text;

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (nfs->format.formats.model),
		&iter, 0, &text, -1);
	gtk_entry_set_text (GTK_ENTRY (nfs->format.widget[F_ENTRY]), text);
}

static gboolean
cb_format_currency_select (G_GNUC_UNUSED GtkWidget *ct,
			   char * new_text, NumberFormatSelector *nfs)
{
	int i;

	/* ignore the clear while assigning a new value */
	if (!nfs->enable_edit || new_text == NULL || *new_text == '\0')
		return FALSE;

	for (i = 0; currency_symbols[i].symbol != NULL ; ++i)
		if (!strcmp (_(currency_symbols[i].description), new_text)) {
			nfs->format.currency_index = i;
			break;
		}

	if (nfs->format.current_type == 1 || nfs->format.current_type == 2)
		fillin_negative_samples (nfs);
	draw_format_preview (nfs, TRUE);

	return TRUE;
}

static void
cb_format_negative_form_selected (GtkTreeSelection *selection, NumberFormatSelector *nfs)
{
	GtkTreeIter iter;
	int type;

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (nfs->format.negative_types.model),
		&iter, 0, &type, -1);
	nfs->format.negative_format = type;
	draw_format_preview (nfs, TRUE);
}

static gint
funny_currency_order (gconstpointer _a, gconstpointer _b)
{
	char const *a = (char const *)_a;
	char const *b = (char const *)_b;

	/* One letter versions?  */
	gboolean a1 = (a[0] && *(g_utf8_next_char(a)) == '\0');
	gboolean b1 = (b[0] && *(g_utf8_next_char(b)) == '\0');

	if (a1) {
		if (b1) {
			return strcmp (a, b);
		} else {
			return -1;
		}
	} else {
		if (b1) {
			return +1;
		} else {
			return strcmp (a, b);
		}
	}
}

static void
set_format_category_menu_from_style (NumberFormatSelector *nfs)
{
  	int page;
	FormatCharacteristics info;

  	g_return_if_fail (IS_NUMBER_FORMAT_SELECTOR (nfs));

	/* Attempt to extract general parameters from the current format */
	if ((page = cell_format_classify (nfs->format.spec, &info)) < 0)
		page = 11; /* Default to custom */

	gtk_option_menu_set_history (nfs->format.menu, page);
}

/*
 * static void
 * fmt_dialog_init_format_page (FormatState *state)
 */

static void
nfs_init (NumberFormatSelector *nfs)
{
	/* The various format widgets */
	static char const *const widget_names[] = {
		"format_general_explanation",
		"format_number_explanation",
		"format_currency_explanation",
		"format_accounting_explanation",
		"format_date_explanation",
		"format_time_explanation",
		"format_percentage_explanation",
		"format_fraction_explanation",
		"format_scientific_explanation",
		"format_text_explanation",
		"format_special_explanation",
		"format_custom_explanation",

		"format_separator",	"format_symbol_label",
		"format_symbol_select",	"format_delete",
		"format_entry",
		"format_list_scroll",	"format_list",
		"format_number_decimals",
		"format_negatives_scroll",
		"format_negatives",	"format_list_box",
		"format_decimal_label",	"format_code_label",
		"format_symbol_box",	"format_decimal_box",
		"format_code_box",
		NULL
	};

	GtkWidget *tmp;
	GtkTreeViewColumn *column;
	GnmComboText *combo;
	char const *name;
	int i;
	int page;
	FormatCharacteristics info;

	GtkWidget *toplevel;
	GtkWidget *old_parent;

	nfs->enable_edit = FALSE;
	nfs->locale = NULL;

	nfs->gui = gnm_glade_xml_new (NULL, "format-selector.glade", NULL, NULL);
	if (nfs->gui == NULL)
		return;

	toplevel = glade_xml_get_widget (nfs->gui, "number_box");
	old_parent = gtk_widget_get_toplevel (toplevel);
	gtk_widget_reparent (toplevel, GTK_WIDGET (nfs));
	gtk_widget_destroy (old_parent);
	gtk_widget_queue_resize (toplevel);

	nfs->format.spec = style_format_general ();
	style_format_ref (nfs->format.spec);

	nfs->format.preview = NULL;

	/* The handlers will set the format family later.  -1 flags that
	 * all widgets are already hidden. */
	nfs->format.current_type = -1;

	cell_format_classify (nfs->format.spec, &info);

	/* Even if the format was not recognized it has set intelligent defaults */
	nfs->format.use_separator = info.thousands_sep;
	nfs->format.num_decimals = info.num_decimals;
	nfs->format.negative_format = info.negative_fmt;
	nfs->format.currency_index = info.currency_symbol_index;

	nfs->format.preview_frame = GTK_FRAME (glade_xml_get_widget (nfs->gui, "preview_frame"));
	nfs->format.preview = GTK_LABEL (glade_xml_get_widget (nfs->gui, "preview"));

	nfs->format.menu = GTK_OPTION_MENU (glade_xml_get_widget (nfs->gui, "format_menu"));

	/* Collect all the required format widgets and hide them */
	for (i = 0; (name = widget_names[i]) != NULL; ++i) {
		tmp = glade_xml_get_widget (nfs->gui, name);

		if (tmp == NULL) {
			g_warning ("nfs_init : failed to load widget %s", name);
		}

		g_return_if_fail (tmp != NULL);

		gtk_widget_hide (tmp);
		nfs->format.widget[i] = tmp;
	}

	/* set minimum heights */
	gtk_widget_set_size_request (nfs->format.widget[F_LIST], -1, 100);
	gtk_widget_set_size_request (nfs->format.widget[F_NEGATIVE], -1, 100);

	/* use size group for better widget alignement */
	nfs->format.size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	gtk_size_group_add_widget (nfs->format.size_group,
				   nfs->format.widget[F_SYMBOL_LABEL]);
	gtk_size_group_add_widget (nfs->format.size_group,
				   nfs->format.widget[F_DECIMAL_LABEL]);

	/* hide preview by default until a value is set */
	gtk_widget_hide (GTK_WIDGET (nfs->format.preview_frame));

	/* setup the structure of the negative type list */
	nfs->format.negative_types.model = gtk_list_store_new (3,
							       G_TYPE_INT,
							       G_TYPE_STRING,
							       G_TYPE_STRING);
	nfs->format.negative_types.view = GTK_TREE_VIEW (nfs->format.widget[F_NEGATIVE]);
	gtk_tree_view_set_model (nfs->format.negative_types.view,
				 GTK_TREE_MODEL (nfs->format.negative_types.model));
	column = gtk_tree_view_column_new_with_attributes (_("Negative Number Format"),
							   gtk_cell_renderer_text_new (),
							   "text",		1,
							   "foreground",	2,
							   NULL);
	gtk_tree_view_append_column (nfs->format.negative_types.view, column);
	nfs->format.negative_types.selection =
		gtk_tree_view_get_selection (nfs->format.negative_types.view);
	gtk_tree_selection_set_mode (nfs->format.negative_types.selection,
				     GTK_SELECTION_SINGLE);
	g_signal_connect (G_OBJECT (nfs->format.negative_types.selection),
			  "changed",
			  G_CALLBACK (cb_format_negative_form_selected), nfs);

	/* Catch changes to the spin box */
	g_signal_connect (G_OBJECT (nfs->format.widget[F_DECIMAL_SPIN]),
			  "changed",
			  G_CALLBACK (cb_decimals_changed), nfs);

	/* Setup special handlers for : Numbers */
	g_signal_connect (G_OBJECT (nfs->format.widget[F_SEPARATOR]),
			  "toggled",
			  G_CALLBACK (cb_separator_toggle), nfs);

	/* setup custom format list */
	nfs->format.formats.model =
		gtk_list_store_new (1, G_TYPE_STRING);
	nfs->format.formats.view =
		GTK_TREE_VIEW (nfs->format.widget[F_LIST]);
	gtk_tree_view_set_model (nfs->format.formats.view,
				 GTK_TREE_MODEL (nfs->format.formats.model));
	column = gtk_tree_view_column_new_with_attributes (_("Number Formats"),
							   gtk_cell_renderer_text_new (),
							   "text",		0,
							   NULL);
	gtk_tree_view_append_column (nfs->format.formats.view, column);
	nfs->format.formats.selection =
		gtk_tree_view_get_selection (nfs->format.formats.view);
	gtk_tree_selection_set_mode (nfs->format.formats.selection,
				     GTK_SELECTION_BROWSE);
	g_signal_connect (G_OBJECT (nfs->format.formats.selection),
			  "changed",
			  G_CALLBACK (cb_format_list_select), nfs);

	/* Setup handler Currency & Accounting currency symbols */
	combo = GNM_COMBO_TEXT (nfs->format.widget[F_SYMBOL]);
	if (combo != NULL) {
		GList *ptr, *l = NULL;

		for (i = 0; currency_symbols[i].symbol != NULL ; ++i)
			l = g_list_append (l, _((gchar *)currency_symbols[i].description));
		l = g_list_sort (l, funny_currency_order);

		for (ptr = l; ptr != NULL ; ptr = ptr->next)
			gnm_combo_text_add_item	(combo, ptr->data);
		g_list_free (l);
		gnm_combo_text_set_text (combo,
					 _((const gchar *)currency_symbols[nfs->format.currency_index].description),
					 GNM_COMBO_TEXT_FROM_TOP);
		g_signal_connect (G_OBJECT (combo),
				  "entry_changed",
				  G_CALLBACK (cb_format_currency_select), nfs);
	}

	/* Setup special handler for Custom */
	g_signal_connect (G_OBJECT (nfs->format.widget[F_ENTRY]),
			  "changed",
			  G_CALLBACK (cb_format_entry_changed), nfs);

	/* Connect signal for format menu */

	g_signal_connect (G_OBJECT (nfs->format.menu), "changed",
			  G_CALLBACK (cb_format_class_changed), nfs);

	set_format_category_menu_from_style (nfs);

	if ((page = cell_format_classify (nfs->format.spec, &info)) < 0)
		page = 11; /* Default to custom */
	fmt_dialog_enable_widgets (nfs, page);

	nfs->enable_edit = TRUE;
}

static void
nfs_destroy (GtkObject *object)
{
  	NumberFormatSelector *nfs = NUMBER_FORMAT_SELECTOR (object);

	g_free (nfs->locale);
	nfs->locale = NULL;

	if (nfs->format.spec) {
		style_format_unref (nfs->format.spec);
		nfs->format.spec = NULL;
	}

	if (nfs->format.size_group) {
		g_object_unref (nfs->format.size_group);
		nfs->format.size_group = NULL;
	}

	if (nfs->value) {
		value_release (nfs->value);
		nfs->value = NULL;
	}

	if (nfs->gui) {
		g_object_unref (G_OBJECT (nfs->gui));
		nfs->gui = NULL;
	}

	((GtkObjectClass *)nfs_parent_class)->destroy (object);
}

static void
nfs_class_init (GtkObjectClass *klass)
{
	klass->destroy = nfs_destroy;

	nfs_parent_class = g_type_class_peek (gtk_hbox_get_type ());

	nfs_signals[NUMBER_FORMAT_CHANGED] =
		g_signal_new ("number_format_changed",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (NumberFormatSelectorClass, number_format_changed),
			      NULL, NULL,
			      gnm__VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
}

GSF_CLASS (NumberFormatSelector, number_format_selector,
	   nfs_class_init, nfs_init, GTK_TYPE_HBOX)

GtkWidget *
number_format_selector_new (void)
{
	return g_object_new (NUMBER_FORMAT_SELECTOR_TYPE, NULL);
}

void
number_format_selector_set_focus (NumberFormatSelector *nfs)
{
	g_return_if_fail (IS_NUMBER_FORMAT_SELECTOR (nfs));

	gtk_widget_grab_focus (GTK_WIDGET (nfs->format.menu));
}

void
number_format_selector_set_style_format (NumberFormatSelector *nfs,
					 StyleFormat *style_format)
{
	GnmComboText *combo;
	FormatCharacteristics info;

  	g_return_if_fail (IS_NUMBER_FORMAT_SELECTOR (nfs));
	g_return_if_fail (style_format != NULL);

  	style_format_unref (nfs->format.spec);

	nfs->format.spec = style_format;

	cell_format_classify (nfs->format.spec, &info);

	nfs->format.use_separator = info.thousands_sep;
	nfs->format.num_decimals = info.num_decimals;
	nfs->format.negative_format = info.negative_fmt;
	nfs->format.currency_index = info.currency_symbol_index;

	combo = GNM_COMBO_TEXT (nfs->format.widget[F_SYMBOL]);
	gnm_combo_text_set_text
		(combo,
		 _((const gchar *)currency_symbols[nfs->format.currency_index].description),
		 GNM_COMBO_TEXT_FROM_TOP);

	style_format_ref (style_format);

	set_format_category_menu_from_style (nfs);
	draw_format_preview (nfs, TRUE);
}

void
number_format_selector_set_value (NumberFormatSelector *nfs,
				  Value const *value)
{
  	g_return_if_fail (IS_NUMBER_FORMAT_SELECTOR (nfs));
	g_return_if_fail (value != NULL);

	if (nfs->value)	{
	  	value_release (nfs->value);
	}
	nfs->value = value_duplicate (value);

	gtk_widget_show (GTK_WIDGET (nfs->format.preview_frame));

	draw_format_preview (nfs, TRUE);
}

void
number_format_selector_set_date_conv (NumberFormatSelector *nfs,
				      GnmDateConventions const *date_conv)
{
  	g_return_if_fail (IS_NUMBER_FORMAT_SELECTOR (nfs));
	g_return_if_fail (date_conv != NULL);

	/* FIXME is it safe ? */

  	nfs->date_conv = date_conv;

	draw_format_preview (nfs, TRUE);
}

void
number_format_selector_editable_enters (NumberFormatSelector *nfs,
					GtkWindow *window)
{
	g_return_if_fail (IS_NUMBER_FORMAT_SELECTOR (nfs));

	gnumeric_editable_enters (window,
				  GTK_WIDGET (nfs->format.widget[F_DECIMAL_SPIN]));
	gnumeric_editable_enters (window,
				  GTK_WIDGET (nfs->format.widget[F_ENTRY]));
}


void		
number_format_selector_set_locale (NumberFormatSelector *nfs, 
				   char const *locale)
{
	g_free (nfs->locale);
	nfs->locale = g_strdup (locale);

	cb_format_class_changed (nfs->format.menu, nfs);
}
