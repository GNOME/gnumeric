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
#include "gnumeric-sheet.h"
#include "dialogs.h"
#include "format.h"

/* The main dialog box */
static GtkWidget *cell_format_prop_win = 0;

/* These point to the various widgets in the format/number page */
static GtkWidget *number_sample;
static GtkWidget *number_input;
static GtkWidget *number_cat_list;
static GtkWidget *number_format_list;

static GtkWidget *font_widget;

/* There point to the radio groups in the format/alignment page */
static GSList *hradio_list;
static GSList *vradio_list;
static GtkWidget *auto_return;

/* These points to the radio buttons of the coloring page */
static GSList *foreground_radio_list;
static GSList *background_radio_list;
static GdkPixmap *patterns [GNUMERIC_SHEET_PATTERNS];

/* Points to the first cell in the selection */
static Cell *first_cell;

/* The various formats */
static char *cell_format_numbers [] = {
	N_("General"),
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
 * This routine is just used at startup to find the current
 * format that applies to this cell
 */
static int
format_find (char *format)
{
	int i, row;
	char **p;
	
	for (i = 0; cell_formats [i].name; i++){
		p = cell_formats [i].formats;

		for (row = 0; *p; p++, row++){
			if (strcmp (format, *p) == 0){
				format_list_fill (i);
				gtk_clist_select_row (GTK_CLIST (number_format_list), row, 0);
				return 1;
			}
		}
	}
	return 0;
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
		str = format_value (style_format, v, NULL);

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

	t = (GtkTable *) gtk_table_new (0, 0, 0);

	/* try to select the current format the user is using */
	format = cells_get_format (cells);

	/* 1. Categories */
	gtk_table_attach (t, l = gtk_label_new (_("Categories")),
			  0, 1, BOXES_LINE, BOXES_LINE+1,
			  GTK_FILL | GTK_EXPAND, 0, 0, 0);
	gtk_misc_set_alignment (GTK_MISC (l), 0.0, 0.5);

	number_cat_list = my_clist_new ();
	gtk_table_attach (t, number_cat_list, 0, 1, BOXES_LINE+1, BOXES_LINE+2,
			  GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 4, 0);

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
	gtk_table_attach (t, l = gtk_label_new (_("Format codes")),
			  1, 2, BOXES_LINE, BOXES_LINE+1,
			  GTK_FILL | GTK_EXPAND, 0, 0, 0);
	gtk_misc_set_alignment (GTK_MISC (l), 0.0, 0.5);
	gtk_table_attach_defaults (t, number_format_list, 1, 2, BOXES_LINE + 1, BOXES_LINE + 2);
	format_list_fill (0);

	/* 3.1 connect the signal handled for row selected */
	gtk_signal_connect (GTK_OBJECT (number_format_list), "select_row",
			    GTK_SIGNAL_FUNC (format_selected), prop_win);


	/* 3.2: Invoke the current style for the cell if possible */
	if (format){
		if (!format_find (format->format))
		    gtk_entry_set_text (GTK_ENTRY (number_input), format->format);
	} else 
		gtk_clist_select_row (GTK_CLIST (number_format_list), 0, 0);


	/* 4. finish */
	gtk_widget_show_all (GTK_WIDGET (t));
	
	return GTK_WIDGET (t);
}

static void
apply_number_formats (Style *style, Sheet *sheet, CellList *list)
{
	char *str = gtk_entry_get_text (GTK_ENTRY (number_input));
	
	for (;list; list = list->next){
		Cell *cell = list->data;

		cell_set_format (cell, str);
	}

	style->valid_flags |= STYLE_FORMAT;
	style->format = style_format_new (str);
}

typedef struct {
	char *name;
	int  flag;
} align_def_t;

static align_def_t horizontal_aligns [] = {
	{ N_("General"),   HALIGN_GENERAL },
	{ N_("Left"),      HALIGN_LEFT    },
	{ N_("Center"),    HALIGN_CENTER  },
	{ N_("Right"),     HALIGN_RIGHT   },
	{ N_("Fill"),      HALIGN_FILL    },
	{ N_("Justify"),   HALIGN_JUSTIFY },
	{ NULL, 0 }
};

static align_def_t vertical_aligns [] = {
	{ N_("Top"),       VALIGN_TOP     },
	{ N_("Center"),    VALIGN_CENTER  },
	{ N_("Bottom"),    VALIGN_BOTTOM  },
	{ N_("Justify"),   VALIGN_JUSTIFY },
	{ NULL, 0 }
};

static void
do_disable (GtkWidget *widget, GtkWidget *op)
{
	int v;
	
	if (GTK_TOGGLE_BUTTON (widget)->active)
		v = FALSE;
	else
		v = TRUE;
	
	gtk_widget_set_sensitive (op, v);
}

static GtkWidget *
make_radio_selection (GtkWidget *prop_win, char *title, align_def_t *array, GSList **dest_list, GtkWidget *disable)
{
	GtkWidget *frame, *vbox;
	GSList *group;

	frame = gtk_frame_new (title);
	vbox = gtk_vbox_new (0, 0);
	gtk_container_add (GTK_CONTAINER (frame), vbox);

	for (group = NULL;array->name; array++){
		GtkWidget *item;

		item = gtk_radio_button_new_with_label (group, _(array->name));
		group = gtk_radio_button_group (GTK_RADIO_BUTTON (item));
		gtk_box_pack_start_defaults (GTK_BOX (vbox), item);

		if (disable && strcmp (array->name, "Fill") == 0){
			gtk_signal_connect (GTK_OBJECT (item), "toggled",
					    GTK_SIGNAL_FUNC (do_disable), disable);
		}
	}
	
	*dest_list = group;
	return frame;
}

static void
prop_modified (GtkWidget *widge, GnomePropertyBox *box)
{
	gnome_property_box_changed (box);
}

static GtkWidget *
create_align_page (GtkWidget *prop_win, CellList *cells)
{
	GtkTable *t;
	GtkWidget *w;
	int ha, va, autor, ok = 0;
	GList *l;
	GSList *sl;
	
	t = (GtkTable *) gtk_table_new (0, 0, 0);

	/* Vertical alignment */
	w = make_radio_selection (prop_win, _("Vertical"), vertical_aligns, &vradio_list, NULL);
	gtk_table_attach (t, w, 1, 2, 0, 1, 0, GTK_FILL, 4, 0);

	/* Horizontal alignment */
	w = make_radio_selection (prop_win, _("Horizontal"), horizontal_aligns, &hradio_list, w);
	gtk_table_attach (t, w, 0, 1, 0, 2, 0, GTK_FILL, 4, 0);

	auto_return = gtk_check_button_new_with_label (_("Auto return"));
	gtk_table_attach (t, auto_return, 0, 3, 2, 3, 0, 0, 0, 0);
	
	/* Check if all cells have the same properties */
	/*
	 * FIXME: This should check the cells *AND* the
	 * style regions to figure out what to check and what
	 * not, right now this is broken in that regard
	 */
	if (cells){
		ha    = ((Cell *) (cells->data))->style->halign;
		va    = ((Cell *) (cells->data))->style->valign;
		autor = ((Cell *) (cells->data))->style->fit_in_cell;
		
		for (ok = 1, l = cells; l; l = l->next){
			Cell *cell = l->data;
			
			if (cell->style->halign != ha ||
			    cell->style->valign != va ||
			    cell->style->fit_in_cell != autor){
				ok = 0;
				break;
			}
		}

		/* If all the cells share the same alignment, select that on the radio boxes */
		if (ok){
			int n;

			for (n = 0; horizontal_aligns [n].name; n++)
				if (horizontal_aligns [n].flag == ha){
					gtk_radio_button_select (hradio_list, n);
					break;
				}
			
			for (n = 0; vertical_aligns [n].name; n++)
				if (vertical_aligns [n].flag == va){
					gtk_radio_button_select (vradio_list, n);
					break;
				}

			if (autor)
				gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (auto_return), 1);
		}
	}

	/* Now after we *potentially* toggled the radio button above, we
	 * connect the signals to activate the propertybox
	 */
	for (sl = hradio_list; sl; sl = sl->next){
		gtk_signal_connect (GTK_OBJECT (sl->data), "toggled",
				    GTK_SIGNAL_FUNC (prop_modified), prop_win);
	}

	for (sl = vradio_list; sl; sl = sl->next){
		gtk_signal_connect (GTK_OBJECT (sl->data), "toggled",
				    GTK_SIGNAL_FUNC (prop_modified), prop_win);
	}
	
	gtk_widget_show_all (GTK_WIDGET (t));

	return GTK_WIDGET (t);
}

static void
apply_align_format (Style *style, Sheet *sheet, CellList *cells)
{
	int i;
	int halign, valign, autor;

	i = gtk_radio_group_get_selected (hradio_list);
	halign = horizontal_aligns [i].flag;
	i = gtk_radio_group_get_selected (vradio_list);
	valign = vertical_aligns [i].flag;
	autor = GTK_TOGGLE_BUTTON (auto_return)->active;

	for (; cells; cells = cells->next){
		Cell *cell = cells->data;
		
		cell_set_alignment (cell, halign, valign, ORIENT_HORIZ, autor);
	}
	style->halign = halign;
	style->valign = valign;
	style->orientation = ORIENT_HORIZ;
	style->fit_in_cell = autor;
	style->valid_flags |= STYLE_ALIGN;
}

static void
font_changed (GtkWidget *widget, GtkStyle *previous_style, GnomePropertyBox *prop_win)
{
	gnome_property_box_changed (prop_win);
}

static GtkWidget *
create_font_page (GtkWidget *prop_win, CellList *cells)
{
	font_widget = gtk_font_selection_new ();
	gtk_widget_show (font_widget);

	gtk_signal_connect (GTK_OBJECT (GTK_FONT_SELECTION (font_widget)->preview_entry),
			    "style_set",
			    GTK_SIGNAL_FUNC (font_changed), prop_win);
	
	return font_widget;
}

static void
apply_font_format (Style *style, Sheet *sheet, CellList *cells)
{
	GtkFontSelection *font_sel = GTK_FONT_SELECTION (font_widget);
	GdkFont   *gdk_font;
	GList *l;
	char *font_name;
	int  height;
	
	font_name = gtk_font_selection_get_font_name (font_sel);

	if (!font_name)
		return;

	gdk_font = gtk_font_selection_get_font (font_sel);
	height = gdk_font->ascent + gdk_font->descent;

	/* Apply the new font to all of the cell rows */
	for (; cells; cells = cells->next){
		Cell *cell = cells->data;
		
		cell_set_font (cell, font_name);
	}

	/* Now apply it to every row in the selection */
	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		int i;

		for (i = ss->start_row; i <= ss->end_row; i++){
			ColRowInfo *ri;

			ri = sheet_row_get (sheet, i);
			sheet_row_set_internal_height (sheet, ri, height);
		}
	}
	style->valid_flags |= STYLE_FONT;
	style->font = style_font_new (font_name, 10); 
}

static GtkWidget *
create_foreground_radio (GtkWidget *prop_win)
{
	GtkWidget *frame, *table, *r1, *r2;
	int e = GTK_FILL | GTK_EXPAND;
	
	frame = gtk_frame_new (_("Text color"));
        table = gtk_table_new (2, 2, 0);
	gtk_container_add (GTK_CONTAINER (frame), table);

	r1 = gtk_radio_button_new_with_label (NULL, _("None"));
	foreground_radio_list = GTK_RADIO_BUTTON (r1)->group;
	r2 = gtk_radio_button_new_with_label (foreground_radio_list,
					      _("Use this color"));

	gtk_table_attach (GTK_TABLE (table), r1, 0, 1, 0, 1, e, 0, 0, 0);
	gtk_table_attach (GTK_TABLE (table), r2, 0, 1, 1, 2, e, 0, 0, 0);
/*	gtk_table_attach (GTK_TABLE (table), cs, 1, 2, 1, 2, 0, 0, 0, 0); */

	return frame;
}

static void
create_stipples (GnomeCanvas *canvas)
{
	GnomeCanvasGroup *group;
	GdkWindow *window;
	int i;

	group = GNOME_CANVAS_GROUP (gnome_canvas_root (canvas));
	window = GTK_WIDGET (canvas)->window;
	
	for (i = 0; i < GNUMERIC_SHEET_PATTERNS; i++){
		GnomeCanvasRE *item;
		int x, y;

		x = i % 7;
		y = i / 7;
		
		item = GNOME_CANVAS_RE (gnome_canvas_item_new (
			group,
			gnome_canvas_rect_get_type (),
			"x1",           (double) x * 1.0 + 0.1,
			"y1",           (double) y * 1.0 + 0.1,
			"x2",           (double) x * 1.0 + 1.2,
			"y2",           (double) y * 1.0 + 1.2,
			"fill_color",   "black",
			"width_pixels", (int) 1,
			NULL));
#if 0
		patterns [i] = gdk_bitmap_create_from_data (
			window, gnumeric_sheet_patterns [i].pattern, 8, 8);

		gdk_gc_set_stipple (item->fill_gc, patterns [i]);
		gdk_gc_set_fill (item->fill_gc, GDK_STIPPLED);
#endif
	}
}

static GtkWidget *
create_pattern_preview (GtkWidget *prop_win)
{
	GnomeCanvas *canvas;

	canvas = (GnomeCanvas *) gnome_canvas_new ();
	
	gnome_canvas_set_scroll_region (canvas, 0.0, 0.0, 14.0, 4.0);
	gnome_canvas_set_size (canvas, 112, 112);
	gtk_signal_connect_after (
		GTK_OBJECT (canvas), "realize",
		GTK_SIGNAL_FUNC (create_stipples), NULL);

	return GTK_WIDGET (canvas);
}

static GtkWidget *
create_background_radio (GtkWidget *prop_win)
{
	GtkWidget *frame, *table, *r1, *r2, *r3, *cs1, *cs2, *p;
	int e = GTK_FILL | GTK_EXPAND;
	
	frame = gtk_frame_new (_("Background configuration"));
        table = gtk_table_new (2, 2, 0);
	gtk_container_add (GTK_CONTAINER (frame), table);

	/* The radio buttons */
	r1 = gtk_radio_button_new_with_label (NULL, _("None"));
	background_radio_list = GTK_RADIO_BUTTON (r1)->group;
	r2 = gtk_radio_button_new_with_label (background_radio_list,
					      _("Use solid color"));
	r3 = gtk_radio_button_new_with_label (background_radio_list,
					      _("Use a pattern"));

	/* The color selectors */
	cs1 = gtk_label_new ("color selector goes here");
	cs2 = gtk_label_new ("color selector goes here");

	/* Create the pattern preview */
	p = create_pattern_preview (prop_win);
	
	gtk_table_attach (GTK_TABLE (table), r1, 0, 1, 0, 1, e, 0, 0, 0);
	gtk_table_attach (GTK_TABLE (table), r2, 0, 1, 1, 2, e, 0, 0, 0);
	gtk_table_attach (GTK_TABLE (table), r3, 0, 1, 2, 3, e, 0, 0, 0);

	gtk_table_attach (GTK_TABLE (table), cs1, 1, 2, 1, 2, 0, 0, 0, 0);
	gtk_table_attach (GTK_TABLE (table), cs2, 1, 2, 2, 3, 0, 0, 0, 0);
	gtk_table_attach (GTK_TABLE (table), p, 0, 2, 3, 4, 0, 0, 0, 0);
	return frame;
}

static GtkWidget *
create_coloring_page (GtkWidget *prop_win, CellList *cells)
{
	GtkTable *t;
	GtkWidget *fore, *back;
	int e = GTK_FILL | GTK_EXPAND;
	
	t = (GtkTable *) gtk_table_new (0, 0, 0);
	fore = create_foreground_radio (prop_win);
	back = create_background_radio (prop_win);

	gtk_table_attach (t, fore, 0, 1, 0, 1, e, 0, 4, 4);
	gtk_table_attach (t, back, 0, 1, 1, 2, e, 0, 4, 4);
	gtk_widget_show_all (GTK_WIDGET (t));
	return GTK_WIDGET (t);
}

static void
apply_coloring_format (Style *style, Sheet *sheet, CellList *cells)
{
}

static struct {
	char      *title;
	GtkWidget *(*create_page)(GtkWidget *prop_win, CellList *cells);
	void      (*apply_page)(Style *style, Sheet *sheet, CellList *cells);
} cell_format_pages [] = {
	{ N_("Number"),    create_number_format_page,  apply_number_formats  },
	{ N_("Alignment"), create_align_page,          apply_align_format    },
	{ N_("Font"),      create_font_page,           apply_font_format     },
	{ N_("Coloring"),  create_coloring_page,       apply_coloring_format },
	{ NULL, NULL }
};

static void
cell_properties_apply (GtkObject *w, int page, CellList *cells)
{
	Sheet *sheet;
	Style *style;
	GList *l;
	int i;
	
	if (page != -1)
		return;

	sheet = (Sheet *) gtk_object_get_data (w, "Sheet");

	/* Now, let each property page apply their style */
	style = style_new_empty ();
	style->valid_flags = 0;
	
	for (i = 0; cell_format_pages [i].title; i++)
		(*cell_format_pages [i].apply_page)(style, sheet, cells);

	/* Attach this style to all of the selections */
	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		
		sheet_style_attach (
			sheet,
			ss->start_col, ss->start_row,
			ss->end_col,   ss->end_row,
			style);
	}
}

static void
cell_properties_close (void)
{
	gtk_main_quit ();
	gtk_widget_destroy (cell_format_prop_win);
	cell_format_prop_win = 0;
}

/*
 * Main entry point for the Cell Format dialog box
 */
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

	g_list_free (cells);
}

