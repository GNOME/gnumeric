/*
 * dialog-cell-format.c:  Implements the Cell Format dialog box.
 * (C) 1998 The Free Software Foundation
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "format.h"

/* The main dialog box */
static GtkWidget *cell_format_prop_win = 0;

/* These point to the various widgets in the format/number page */
static GtkWidget *number_sample;
static GtkWidget *number_input;
static GtkWidget *number_cat_list;
static GtkWidget *number_format_list;

/* Points to the first cell in the selection */
static Cell *first_cell;

/* The various formats */
static char *cell_format_numbers [] = {
	"0",
	"0.00",
	"#,##0",
	"#,##0.00",
	"#,##0_);(#,##0)",
	"#,##0_);[red](#,##0)",
	"#,##0.00_);(#,##0.00)",
	"#,##0.00_);[red](#,##0.00)",
	"0.0",
	NULL
};

static char *cell_format_accounting [] = {
	"_($*#,##0_);_($*(#,##0);_($*\"-\"_);_(@_)",
	"_(*$,$$0_);_(*(#,##0);_(*\"-\"_);_(@_)",
	"_($*#,##0.00_);_($*(#,##0.00);_($*\"-\"??_);_(@_)",
	"_(*#,##0.00_);_(*(#,##0.00);_(*\"-\"??_);_(@_)",
	NULL
};

static char *cell_format_date [] = {
	"m/d/yy",
	"d-mmm-yy",
	"d-mmm",
	"mmm-yy",
	"m/d/yy h:mm",
	NULL
};

static char *cell_format_hour [] = {
	"h:mm AM/PM",
	"h:mm:ss AM/PM",
	"h:mm",
	"h:mm:ss",
	"m/d/yy h:mm",
	"mm:ss",
	"mm:ss.0",
	"[h]:mm:ss",
	NULL
};

static char *cell_format_percent [] = {
	"0%",
	"0.00%",
	NULL,
};

static char *cell_format_fraction [] = {
	"# ?/?",
	"# ??/??",
	NULL
};

static char *cell_format_scientific [] = {
	"0.00E+00",
	"##0.0E+0",
	NULL
};

static char *cell_format_text [] = {
	"@",
	NULL,
};

static char *cell_format_money [] = {
	"$#,##0_);($#,##0)",
	"$#,##0_);[red]($#,##0)",
	"$#,##0.00_);($#,##0.00)",
	"$#,##0.00_);[red]($#,##0.00)",
	NULL,

};

static struct {
	char *name;
	char **formats;	
} cell_formats [] = {
	{ N_("Numbers"),    cell_format_numbers    },
	{ N_("Accounting"), cell_format_accounting },
	{ N_("Date"),       cell_format_date       },
	{ N_("Time"),       cell_format_hour       },
	{ N_("Percent"),    cell_format_percent    },
	{ N_("Fraction"),   cell_format_fraction   },
	{ N_("Scientific"), cell_format_scientific },
	{ N_("Text"),       cell_format_text       },
	{ N_("Money"),      cell_format_money      },
	{ NULL, NULL }
};

static void
format_list_fill (int n)
{
	GtkCList *cl = GTK_CLIST (number_format_list);
	char **texts;
	int i;

	g_return_if_fail (n >= 0);
	g_return_if_fail (cell_formats [n].name != NULL);

	texts = cell_formats [n].formats;
	
	gtk_clist_freeze (cl);
	gtk_clist_clear (cl);

	for (i = 0; texts [i]; i++){
		char *t [1] = { texts [i] };
		
		gtk_clist_append (cl, t);
	}
	gtk_clist_thaw (cl);
}

/*
 * Invoked when the user has selected a new format category
 */
static void
format_number_select_row (GtkCList *clist, gint row, gint col, GdkEvent *event, GtkWidget *prop_win)
{
	format_list_fill (row);
	gtk_clist_select_row (GTK_CLIST (number_format_list), 0, 0);

	gnome_property_box_changed (GNOME_PROPERTY_BOX (prop_win));
}

static void
render_formated_version (char *format)
{
	if (!first_cell)
		gtk_label_set (GTK_LABEL (number_sample), "");
	else {
		StyleFormat *style_format;
		Value *v = first_cell->value;
		char *str;

		style_format = style_format_new (format);
		str = value_format (v, style_format, NULL);

		if (!str)
			str = g_strdup (_("Format error"));
		
		gtk_label_set (GTK_LABEL (number_sample), str);
		g_free (str);
		style_format_unref (style_format);
	}
}

/*
 * Invoked when a specific format has been selected
 */
static void
format_selected (GtkCList *clist, gint row, gint col, GdkEvent *event, GnomePropertyBox *prop_win)
{
	char *format;

	gtk_clist_get_text (clist, row, col, &format);

	/* Set the input line to reflect the selected format */
	gtk_entry_set_text (GTK_ENTRY (number_input), format);

	/* Notify gnome-property-box that a change has happened */
	if (cell_format_prop_win)
		gnome_property_box_changed (prop_win);
		
}

/*
 * cells_get_format:
 *
 * Checks if all the cells in the list share the same format string
 */
static StyleFormat *
cells_get_format (CellList *cells)
{
	StyleFormat *last_format;
	
	for (last_format = NULL; cells; cells = cells->next){
		Cell *cell = cells->data;

		if (!last_format){
			last_format = cell->style->format;
			continue;
		}

		if (cell->style->format != last_format)
			return NULL;
	}

	return last_format;
}

/*
 * Creates the lists for the number display with our defaults
 */
static GtkWidget *
my_clist_new (void)
{
	GtkCList *cl;
	GdkFont *font;
	
	cl = GTK_CLIST (gtk_clist_new (1));
	gtk_clist_column_titles_hide (cl);
	gtk_clist_set_policy (cl, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_clist_set_selection_mode (cl,GTK_SELECTION_SINGLE);
				      
	/* Configure the size */
	font = GTK_WIDGET (cl)->style->font;
	gtk_widget_set_usize (GTK_WIDGET (cl), 0, 11 * (font->ascent + font->descent));

	return GTK_WIDGET (cl);
}

static void
format_code_changed (GtkEntry *entry, GnomePropertyBox *prop_win)
{
	render_formated_version (gtk_entry_get_text (entry));
	if (cell_format_prop_win)
		gnome_property_box_changed (prop_win);
}

/*
 * Creates the widget that represents the number format configuration page
 */
static GtkWidget *
create_number_format_page (GtkWidget *prop_win, CellList *cells)
{
	StyleFormat *format;
	GtkWidget *l;
	GtkTable *t, *tt;
	int i;
	
	enum {
		BOXES_LINE  = 1,
		INPUT_LINE  = 3,
	};

	t = GTK_TABLE (gtk_table_new (0, 0, 0));

	/* try to select the current format the user is using */
	format = cells_get_format (cells);

	/* 1. Categories */
	gtk_table_attach_defaults (t, l = gtk_label_new (_("Categories")), 0, 1, BOXES_LINE, BOXES_LINE+1);
	gtk_misc_set_alignment (GTK_MISC (l), 0.0, 0.5);

	number_cat_list = my_clist_new ();
	gtk_table_attach (t, number_cat_list, 0, 1, BOXES_LINE+1, BOXES_LINE+2,
			  GTK_FILL | GTK_EXPAND, 0, 4, 0);

	/* 1.1 Connect our signal handler */
	gtk_signal_connect (GTK_OBJECT (number_cat_list), "select_row",
			    GTK_SIGNAL_FUNC (format_number_select_row), prop_win);

	/* 1.2 Fill the category list */
	gtk_clist_freeze (GTK_CLIST (number_cat_list));
	for (i = 0; cell_formats [i].name; i++){
		char *text [1] = { cell_formats [i].name };
		
		gtk_clist_append (GTK_CLIST (number_cat_list), text);
	}
	gtk_clist_thaw (GTK_CLIST (number_cat_list));
	
	/* 2. Code input and sample display */
	tt = GTK_TABLE (gtk_table_new (0, 0, 0));
	
	/* 2.1 Input line */
	gtk_table_attach (tt, gtk_label_new (_("Code:")), 0, 1, 0, 1, 0, 0, 2, 0);
	
	number_input = gtk_entry_new ();
	gtk_signal_connect (GTK_OBJECT (number_input), "changed",
			    GTK_SIGNAL_FUNC (format_code_changed), prop_win);
	
	gtk_table_attach (tt, number_input, 1, 2, 0, 1, GTK_FILL | GTK_EXPAND, 0, 0, 2);
	
	/* 2.2 Sample */
	gtk_table_attach (tt, gtk_label_new (_("Sample:")), 0, 1, 1, 2, 0, 0, 2, 0);
	number_sample = gtk_label_new ("X");
	gtk_misc_set_alignment (GTK_MISC (number_sample), 0.0, 0.5);
	gtk_table_attach (tt, number_sample, 1, 2, 1, 2, GTK_FILL | GTK_EXPAND, 0, 0, 2);

	gtk_table_attach (t, GTK_WIDGET (tt), 0, 2, INPUT_LINE, INPUT_LINE+1,
			  GTK_FILL | GTK_EXPAND, 0, 0, 0);

	/* 3. Format codes */
	number_format_list = my_clist_new ();
	gtk_table_attach_defaults (t, l = gtk_label_new (_("Format codes")), 1, 2, BOXES_LINE, BOXES_LINE+1);
	gtk_misc_set_alignment (GTK_MISC (l), 0.0, 0.5);
	gtk_table_attach_defaults (t, number_format_list, 1, 2, BOXES_LINE + 1, BOXES_LINE + 2);
	format_list_fill (0);

	/* 3.1 connect the signal handled for row selected */
	gtk_signal_connect (GTK_OBJECT (number_format_list), "select_row",
			    GTK_SIGNAL_FUNC (format_selected), prop_win);


	/* 3.2: Invoke the current style for the cell if possible */
	/* FIXME:  Currently we just select the first format */
	gtk_clist_select_row (GTK_CLIST (number_format_list), 0, 0);


	/* 4. finish */
	gtk_widget_show_all (GTK_WIDGET (t));
	
	return GTK_WIDGET (t);
}

static void
apply_number_formats (Sheet *sheet, CellList *list)
{
	char *str = gtk_entry_get_text (GTK_ENTRY (number_input));
	Style *style;
	GList *l;
	
	for (;list; list = list->next){
		Cell *cell = list->data;

		cell_set_format (cell, str);
	}

	style = style_new_empty ();
	style->valid_flags = STYLE_FORMAT;

	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		
		sheet_style_attach (
			sheet,
			ss->start_col, ss->start_row,
			ss->end_col,   ss->end_row,
			style);
	}
}

static struct {
	char      *title;
	GtkWidget *(*create_page)(GtkWidget *prop_win, CellList *cells);
	void      (*apply_page)(Sheet *sheet, CellList *cells);
} cell_format_pages [] = {
	{ N_("Number"), create_number_format_page,  apply_number_formats },
	{ NULL, NULL }
};

static void
cell_properties_apply (GtkObject *w, int page, CellList *cells)
{
	Sheet *sheet;
	int i;
	
	if (page != -1)
		return;

	sheet = (Sheet *) gtk_object_get_data (w, "Sheet");

	for (i = 0; cell_format_pages [i].title; i++)
		(*cell_format_pages [i].apply_page)(sheet, cells);
}

static void
cell_properties_close (void)
{
	gtk_main_quit ();
	cell_format_prop_win = 0;
}
		      
void
dialog_cell_format (Sheet *sheet)
{
	GtkWidget *prop_win;
	CellList  *cells;
	int i;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_assert (cell_format_prop_win == NULL);

	cells = sheet_selection_to_list (sheet);
	
	prop_win = gnome_property_box_new ();
	if (cells)
		first_cell = cells->data;
	else
		first_cell = NULL;
	
	for (i = 0; cell_format_pages [i].title; i++){
		GtkWidget *page;

		page = (*cell_format_pages [i].create_page)(prop_win, cells);
		gnome_property_box_append_page (
			GNOME_PROPERTY_BOX (prop_win), page,
			gtk_label_new (_(cell_format_pages [i].title)));

	}

	gtk_signal_connect (GTK_OBJECT (prop_win), "apply",
			    GTK_SIGNAL_FUNC (cell_properties_apply), cells);
	gtk_object_set_data (GTK_OBJECT (prop_win), "Sheet", sheet);
	
	gtk_signal_connect (GTK_OBJECT (prop_win), "destroy",
			    GTK_SIGNAL_FUNC (cell_properties_close), NULL);
	
	gtk_widget_show (prop_win);
	gtk_grab_add (prop_win);
	cell_format_prop_win = prop_win;
	gtk_main ();
	cell_format_prop_win = NULL;
}
