/*
 * dialog-cell-format.c:  Implements the Cell Format dialog box.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include <libgnomeprint/gnome-font-dialog.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "dialogs.h"
#include "format.h"
#include "formats.h"
#include "pattern-selector.h"

/* The main dialog box */
static GtkWidget *cell_format_prop_win = 0;
static int cell_format_last_page_used = 0;

/* These point to the various widgets in the format/number page */
static GtkWidget *number_sample;
static GtkWidget *number_input;
static GtkWidget *number_cat_list;
static GtkWidget *number_format_list;

static GtkWidget *font_widget;

/* These point to the radio groups in the format/alignment page */
static GSList *hradio_list;
static GSList *vradio_list;
static GtkWidget *auto_return;

/* These point to the radio buttons of the coloring page */
static GSList *foreground_radio_list;
static GSList *background_radio_list;

static GtkWidget *foreground_cs;
static GtkWidget *background_cs;

/* Points to the first cell in the selection */
static Cell *first_cell;

static void
prop_modified (GtkWidget *widget, GnomePropertyBox *box)
{
	gnome_property_box_changed (box);
}

static void
make_radio_notify_change (GSList *list, GtkWidget *prop_win)
{
	GSList *sl;
	
	for (sl = list; sl; sl = sl->next){
		gtk_signal_connect (GTK_OBJECT (sl->data), "toggled",
				    GTK_SIGNAL_FUNC (prop_modified), prop_win);
	}
}

static struct {
	const char *name;
	const char *const *formats;	
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
	const char *const *texts;
	int i;

	g_return_if_fail (n >= 0);
	g_return_if_fail (cell_formats [n].name != NULL);

	texts = cell_formats [n].formats;
	
	gtk_clist_freeze (cl);
	gtk_clist_clear (cl);

	for (i = 0; texts [i]; i++){
		gchar *t [1];

		t [0] = _(texts [i]);
		
		gtk_clist_append (cl, t);
	}
	gtk_clist_thaw (cl);
}

/*
 * This routine is just used at startup to find the current
 * format that applies to this cell
 */
static int
format_find (const char *format)
{
	int i, row;
	const char *const *p;
	
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
		gtk_label_set_text (GTK_LABEL (number_sample), "");
	else {
		StyleFormat *style_format;
		Value *v = first_cell->value;
		char *str;

		if (v == NULL)
			return;
		
		style_format = style_format_new (format);
		str = format_value (style_format, v, NULL);

		gtk_label_set_text (GTK_LABEL (number_sample), str);
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
	GtkWidget *l, *scrolled_list;
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
	scrolled_list = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_list),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (scrolled_list), number_cat_list);
	gtk_table_attach (t, scrolled_list, 0, 1, BOXES_LINE+1, BOXES_LINE+2,
			  GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 4, 0);

	/* 1.1 Connect our signal handler */
	gtk_signal_connect (GTK_OBJECT (number_cat_list), "select_row",
			    GTK_SIGNAL_FUNC (format_number_select_row), prop_win);

	/* 1.2 Fill the category list */
	gtk_clist_freeze (GTK_CLIST (number_cat_list));
	for (i = 0; cell_formats [i].name; i++){
		gchar *text [1];

		text [0] = _(cell_formats [i].name);
		
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
	scrolled_list = gtk_scrolled_window_new (NULL, NULL);
	gtk_container_add (GTK_CONTAINER (scrolled_list), number_format_list);
	gtk_table_attach (t, l = gtk_label_new (_("Format codes")),
			  1, 2, BOXES_LINE, BOXES_LINE+1,
			  GTK_FILL | GTK_EXPAND, 0, 0, 0);
	gtk_misc_set_alignment (GTK_MISC (l), 0.0, 0.5);
	gtk_table_attach_defaults (t, scrolled_list, 1, 2, BOXES_LINE + 1, BOXES_LINE + 2);
	format_list_fill (0);

	/* 3.1 connect the signal handled for row selected */
	gtk_signal_connect (GTK_OBJECT (number_format_list), "select_row",
			    GTK_SIGNAL_FUNC (format_selected), prop_win);


	/* 3.2: Invoke the current style for the cell if possible */
	if (format){
		if (!format_find (format->format))
		    gtk_entry_set_text (GTK_ENTRY (number_input), format->format);
	}


	/* 4. finish */
	gtk_widget_show_all (GTK_WIDGET (t));
	
	return GTK_WIDGET (t);
}

static void
apply_number_formats (Style *style, Sheet *sheet, CellList *list)
{
	char *str = gtk_entry_get_text (GTK_ENTRY (number_input));
	
	if (!strcmp (str, ""))
		return;

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

static GtkWidget *
create_align_page (GtkWidget *prop_win, CellList *cells)
{
	GtkTable *t;
	GtkWidget *w;
	int ha, va, autor, ok = 0;
	GList *l;
	
	t = (GtkTable *) gtk_table_new (0, 0, 0);

	/* Vertical alignment */
	w = make_radio_selection (prop_win, _("Vertical"), vertical_aligns, &vradio_list, NULL);
	gtk_table_attach (t, w, 1, 2, 0, 1, 0, GTK_FILL, 4, 0);

	/* Horizontal alignment */
	w = make_radio_selection (prop_win, _("Horizontal"), horizontal_aligns, &hradio_list, w);
	gtk_table_attach (t, w, 0, 1, 0, 2, 0, GTK_FILL, 4, 0);

	auto_return = gtk_check_button_new_with_label (_("Auto return"));
	gtk_table_attach (t, auto_return, 0, 3, 2, 3, 0, 0, 0, 0);
	gtk_signal_connect (GTK_OBJECT (auto_return), "toggled",
			    GTK_SIGNAL_FUNC (prop_modified), prop_win);
	
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
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (auto_return), 1);
		}
	}

	/* Now after we *potentially* toggled the radio button above, we
	 * connect the signals to activate the propertybox
	 */

	make_radio_notify_change (hradio_list, prop_win);
	make_radio_notify_change (vradio_list, prop_win);
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
	GtkWidget *font_widget;
	
	font_widget = gnome_font_selection_new ();
	gtk_widget_show (font_widget);

	gtk_signal_connect (GTK_OBJECT (GNOME_FONT_SELECTION (font_widget)->preview),
			    "style_set",
			    GTK_SIGNAL_FUNC (font_changed), prop_win);
	return font_widget;
}

static void
apply_font_format (Style *style, Sheet *sheet, CellList *cells)
{
	GnomeFontSelection *font_sel = GNOME_FONT_SELECTION (font_widget);
	GnomeDisplayFont *gnome_display_font;
	GnomeFont *gnome_font;
	GList *l;
	char *font_name;
	double height;

	gnome_display_font = gnome_font_selection_get_font (font_sel);
	if (!gnome_display_font)
		return;

	gnome_font = gnome_display_font->gnome_font;
	font_name = gnome_font->fontmap_entry->font_name;
	height = gnome_display_font->gnome_font->size;
	
	/* Apply the new font to all of the cell rows */
	for (; cells; cells = cells->next){
		Cell *cell = cells->data;
		
		cell_set_font (cell, font_name);
	}

	/* Now apply it to every row in the selection */
	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		GList *rl;
		
		/* Special case, the whole spreadsheet */
		if (ss->start_row == 0 && ss->end_row == SHEET_MAX_ROWS-1)
			sheet_row_set_internal_height (sheet, &sheet->default_row_style, height);

		for (rl = sheet->rows_info; rl; rl = rl->next){
			ColRowInfo *ri = rl->data;

			if (ri->pos < ss->start_row)
				break;
			if (ri->pos > ss->end_row)
				break;
			
			sheet_row_set_internal_height (sheet, ri, height);
		}
	}
	style->valid_flags |= STYLE_FONT;
	style->font = style_font_new (
		font_name,
		gnome_font->size,
		sheet->last_zoom_factor_used,
		gnome_font->fontmap_entry->weight_code >= GNOME_FONT_BOLD,
		gnome_font->fontmap_entry->italic);
}


static void
color_pick_change_notify (GnomeColorPicker *cp, guint r, guint g, guint b, guint a, GnomePropertyBox *pbox)
{
	gnome_property_box_changed (pbox);
}

static void
make_color_picker_notify (GtkWidget *widget, GtkWidget *prop_win)
{
	gtk_signal_connect (GTK_OBJECT (widget), "color_set",
			    GTK_SIGNAL_FUNC (color_pick_change_notify), prop_win);
}

static GtkWidget *
create_foreground_radio (GtkWidget *prop_win)
{
	GtkWidget *frame, *table, *r1, *r2, *r3;
	int e = GTK_FILL | GTK_EXPAND;
	
	frame = gtk_frame_new (_("Text color"));
        table = gtk_table_new (2, 2, 0);
	gtk_container_add (GTK_CONTAINER (frame), table);

	r1 = gtk_radio_button_new_with_label (NULL, _("None"));
	r2 = gtk_radio_button_new_with_label_from_widget (
		GTK_RADIO_BUTTON (r1), _("Use this color"));
	r3 = gtk_radio_button_new_with_label_from_widget (
		GTK_RADIO_BUTTON (r1), _("No change"));

	foreground_radio_list = GTK_RADIO_BUTTON (r3)->group;

	foreground_cs = gnome_color_picker_new ();

	make_color_picker_notify (foreground_cs, prop_win);
	
	gtk_table_attach (GTK_TABLE (table), r1, 0, 1, 0, 1, e, 0, 4, 2);
	gtk_table_attach (GTK_TABLE (table), r2, 0, 1, 1, 2, e, 0, 4, 2);
	gtk_table_attach (GTK_TABLE (table), r3, 0, 1, 2, 3, e, 0, 4, 2);
	gtk_table_attach (GTK_TABLE (table), foreground_cs,
			  1, 2, 1, 2, 0, 0, 0, 0); 

	return frame;
}

static GtkWidget *
create_background_radio (GtkWidget *prop_win)
{
	GtkWidget *frame, *table, *r1, *r2, *r3, *r4, *p;
	int e = GTK_FILL | GTK_EXPAND;
	
	frame = gtk_frame_new (_("Background configuration"));
        table = gtk_table_new (2, 2, 0);
	gtk_container_add (GTK_CONTAINER (frame), table);
	gtk_container_set_border_width (GTK_CONTAINER (frame), 5);

	/* The radio buttons */
	r1 = gtk_radio_button_new_with_label (NULL, _("None"));
	r2 = gtk_radio_button_new_with_label_from_widget (
		GTK_RADIO_BUTTON (r1), _("Use solid color"));
	r3 = gtk_radio_button_new_with_label_from_widget (
		GTK_RADIO_BUTTON (r1), _("Use a pattern"));
	r4 = gtk_radio_button_new_with_label_from_widget (
		GTK_RADIO_BUTTON (r1), _("No change"));

	background_radio_list = GTK_RADIO_BUTTON (r4)->group;

	/* The color selectors */
	background_cs = gnome_color_picker_new ();

	make_color_picker_notify (background_cs, prop_win);
	
	/* Create the pattern preview */
	p = pattern_selector_new (0);
	
	gtk_table_attach (GTK_TABLE (table), r1, 0, 1, 0, 1, e, 0, 4, 2);
	gtk_table_attach (GTK_TABLE (table), r2, 0, 1, 1, 2, e, 0, 4, 2);
	gtk_table_attach (GTK_TABLE (table), r3, 0, 1, 2, 3, e, 0, 4, 2);
	gtk_table_attach (GTK_TABLE (table), r4, 0, 1, 4, 5, e, 0, 4, 2);

	gtk_table_attach (GTK_TABLE (table), background_cs, 1, 2, 1, 2, 0, 0, 4, 2);
	gtk_table_attach (GTK_TABLE (table), p, 0, 2, 3, 4, GTK_FILL | GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
	return frame;
}

static GtkWidget *
create_coloring_page (GtkWidget *prop_win, CellList *cells)
{
	GtkTable *t;
	GtkWidget *fore, *back;
	int e = GTK_FILL | GTK_EXPAND;

	gdouble rd, gd, bd, ad;
	gushort fore_red, fore_green, fore_blue;
	gushort back_red, back_green, back_blue;
	GList *l;
	int ok_fore, ok_back, foreground_flag, background_flag;

	t = (GtkTable *) gtk_table_new (0, 0, 0);

	fore = create_foreground_radio (prop_win);
	back = create_background_radio (prop_win);

	/* Check if all cells have the same properties */
	/*
	 * FIXME: This should check the cells *AND* the
	 * style regions to figure out what to check and what
	 * not, right now this is broken in that regard
	 */
	if (cells){
		Cell *cell = (Cell *) cells->data;
		
		fore_red   = cell->style->fore_color->color.red;
		fore_green = cell->style->fore_color->color.green;
		fore_blue  = cell->style->fore_color->color.blue;
			     
		back_red   = cell->style->back_color->color.red;
		back_green = cell->style->back_color->color.green;
		back_blue  = cell->style->back_color->color.blue;

		/*
		 * What follows is ugly: I believe we should use the method illustrated
		 * in the following two lines:
		 * foreground_flag = (((Cell *) (cells->data))->style->valid_flags & STYLE_FORE_COLOR);
		 * background_flag = (((Cell *) (cells->data))->style->valid_flags & STYLE_BACK_COLOR);
		 * instead of what we are using, but it just does not work (even though the
		 * flag is being set/cleared cell by cell and style-wise in function apply_coloring_format)
		 */
		if (fore_red   == 0 &&
		    fore_green == 0 &&
		    fore_blue  == 0){
			foreground_flag = 0;
		} else {
			foreground_flag = STYLE_FORE_COLOR;
		}
		if (back_red   == 0xffff &&
		    back_green == 0xffff &&
		    back_blue  == 0xffff){
			background_flag = 0;
		} else {
			background_flag = STYLE_BACK_COLOR;
		}
		
		/*
		 * First scan is to find out whether all cells have the same foreground color,
		 * second one is the equivalent for background
		 */
		for (ok_fore = 1, l = cells; l; l = l->next){
			Cell *cell = l->data;

			if (cell->style->fore_color->color.red != fore_red ||
			    cell->style->fore_color->color.green != fore_green ||
			    cell->style->fore_color->color.blue != fore_blue){
				ok_fore = 0;
				break;
			}
		}
		for (ok_back = 1, l = cells; l; l = l->next){
			Cell *cell = l->data;

			if (cell->style->back_color->color.red != back_red ||
			    cell->style->back_color->color.green != back_green ||
			    cell->style->back_color->color.blue != back_blue){
				ok_back = 0;
				break;
			}
		}

		if (ok_fore != 0){
			if (foreground_flag == 0){
				gtk_radio_button_select (foreground_radio_list, 0);
				gnome_color_picker_set_d (GNOME_COLOR_PICKER (foreground_cs), 0, 0, 0, 0);
			} else {
				rd = (gdouble) fore_red / 65535;
				gd = (gdouble) fore_green / 65535;
				bd = (gdouble) fore_blue / 65535;
				ad = 1;
				gtk_radio_button_select (foreground_radio_list, 1);
				gnome_color_picker_set_d (GNOME_COLOR_PICKER (foreground_cs), rd, gd, bd, ad);
			}
		} else {
			gtk_radio_button_select (foreground_radio_list, 2);
		}
		if (ok_back != 0){
			if (background_flag == 0){
				gtk_radio_button_select (background_radio_list, 0);
				gnome_color_picker_set_d (GNOME_COLOR_PICKER (background_cs), 1, 1, 1, 1);
			} else {
				rd = (gdouble) back_red / 65535;
				gd = (gdouble) back_green / 65535;
				bd = (gdouble) back_blue / 65535;
				ad = 1;
				gtk_radio_button_select (background_radio_list, 1);
				gnome_color_picker_set_d (GNOME_COLOR_PICKER (background_cs), rd, gd, bd, ad);
			}
		} else {
			gtk_radio_button_select (background_radio_list, 3);
		}
	}

	make_radio_notify_change (foreground_radio_list, prop_win);
	make_radio_notify_change (background_radio_list, prop_win);
	
	gtk_table_attach (t, fore, 0, 1, 0, 1, e, 0, 4, 4);
	gtk_table_attach (t, back, 0, 1, 1, 2, e, 0, 4, 4);

	gtk_widget_show_all (GTK_WIDGET (t));

	return GTK_WIDGET (t);
}

static void
apply_coloring_format (Style *style, Sheet *sheet, CellList *cells)
{
	double rd, gd, bd, ad;
	gushort fore_change = FALSE, back_change = FALSE;
	gushort fore_red=0, fore_green=0, fore_blue=0;
	gushort back_red=0xff, back_green=0xff, back_blue=0xff;

	Cell *cell;

	/*
	 * Let's check the foreground first
	 */
	switch (gtk_radio_group_get_selected (foreground_radio_list)) {
	/*
	 * case 0 means no foreground
	 */
	case 0:
		fore_red   = 0;
		fore_green = 0;
		fore_blue  = 0;
		style->valid_flags &= ~STYLE_FORE_COLOR;
		fore_change = TRUE;
		break;
	/*
	 * case 1 means colored foreground
	 */
	case 1:
		gnome_color_picker_get_d (GNOME_COLOR_PICKER (foreground_cs), &rd, &gd, &bd, &ad);
		fore_red   = rd * 65535;
		fore_green = gd * 65535;
		fore_blue  = bd * 65535;
		style->valid_flags |= STYLE_FORE_COLOR;
		fore_change = TRUE;
		break;
	/*
	 * case 2 means no change
	 */
	case 2:
		fore_change = FALSE;
		break;
	}

	/*
	 * Now, the background
	 * FIXME: What is going on with the cell patterns?
	 */
	switch (gtk_radio_group_get_selected (background_radio_list)) {
	/*
	 * case 0 means no background
	 */
	case 0:
		back_red   = 0xffff;
		back_green = 0xffff;
		back_blue  = 0xffff;
		style->valid_flags &= ~STYLE_BACK_COLOR;
		style->valid_flags &= ~STYLE_PATTERN;
		back_change = TRUE;
		break;

	/*
	 * case 1 means solid color background
	 */
	case 1:
		gnome_color_picker_get_d (GNOME_COLOR_PICKER (background_cs), &rd, &gd, &bd, &ad);

		back_red   = rd * 65535;
		back_green = gd * 65535;
		back_blue  = bd * 65535;
		style->valid_flags |= STYLE_BACK_COLOR;
		style->valid_flags &= ~STYLE_PATTERN;
		back_change = TRUE;
		break;

	/*
	 * case 2 means a pattern background
	 */
	case 2:
		back_red = 0xffff;
		back_green = 0xffff;
		back_blue = 0xffff;
		
		style->valid_flags &= ~STYLE_BACK_COLOR;
		style->valid_flags |= STYLE_PATTERN;
		back_change = TRUE;
		break;
	/*
	 * case 3 means no change
	 */
	case 3:
		back_change = FALSE;
		break;
	}

	/* Apply the color to the cells */
	for (; cells; cells = cells->next){
		cell = cells->data;

		if (fore_change==TRUE) {
			cell_set_foreground (
				cell, fore_red, fore_green, fore_blue);
		}
		if (back_change==TRUE) {
			cell_set_background (cell, back_red, back_green, back_blue);
/*			cell_set_pattern    (cell, 2); */
		}
	}

	if (fore_change==TRUE) {
		style->fore_color  = style_color_new (fore_red, fore_green, fore_blue);
	}
	if (back_change==TRUE) {
		style->back_color  = style_color_new (back_red, back_green, back_blue);
	}
}

static struct {
	char       *title;
	GtkWidget *(*create_page)(GtkWidget *prop_win, CellList *cells);
	void       (*apply_page)(Style *style, Sheet *sheet, CellList *cells);
} cell_format_pages [] = {
	{ N_("Number"),    create_number_format_page,  apply_number_formats  },
	{ N_("Alignment"), create_align_page,          apply_align_format    },
	{ N_("Font"),      create_font_page,           apply_font_format     },
	{ N_("Coloring"),  create_coloring_page,       apply_coloring_format },
	{ NULL, NULL, NULL }
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

	for (l = cells; l; l = l->next){
		Cell *cell = l->data;

		cell_queue_redraw (cell);
	}

	cell_freeze_redraws ();
	
	for (i = 0; cell_format_pages [i].title; i++)
		(*cell_format_pages [i].apply_page)(style, sheet, cells);

	cell_thaw_redraws ();
	
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
	GnomePropertyBox *pbox = GNOME_PROPERTY_BOX (cell_format_prop_win);
	
	gtk_main_quit ();
	cell_format_last_page_used = gtk_notebook_get_current_page (
		GTK_NOTEBOOK (pbox->notebook));
	gtk_widget_destroy (cell_format_prop_win);
	cell_format_prop_win = 0;
}

/*
 * Main entry point for the Cell Format dialog box
 */
void
dialog_cell_format (Workbook *wb, Sheet *sheet)
{
	static GnomeHelpMenuEntry help_ref = { "gnumeric", "formatting.html" };
	GtkWidget *prop_win;
	CellList  *cells;
	int i;


	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_assert (cell_format_prop_win == NULL);

	cells = sheet_selection_to_list (sheet);
	
	prop_win = gnome_property_box_new ();
	gnome_dialog_set_parent (GNOME_DIALOG (prop_win), GTK_WINDOW (wb->toplevel));
	
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
	gtk_signal_connect (GTK_OBJECT (prop_win), "help",
			    GTK_SIGNAL_FUNC (gnome_help_pbox_goto), &help_ref);
	
	gtk_object_set_data (GTK_OBJECT (prop_win), "Sheet", sheet);
	
	gtk_signal_connect (GTK_OBJECT (prop_win), "destroy",
			    GTK_SIGNAL_FUNC (cell_properties_close), NULL);

	gtk_notebook_set_page (
		GTK_NOTEBOOK (GNOME_PROPERTY_BOX(prop_win)->notebook),
		cell_format_last_page_used);
	
	gtk_widget_show (prop_win);
	gtk_grab_add (prop_win);

	cell_format_prop_win = prop_win;
	gtk_main ();
	cell_format_prop_win = NULL;

	g_list_free (cells);
}
