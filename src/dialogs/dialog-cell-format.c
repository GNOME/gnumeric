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
#include "utils-dialog.h"
#include "format.h"
#include "formats.h"
#include "selection.h"
#include "pattern-selector.h"
#include "widgets/widget-font-selector.h"

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
				gtk_clist_select_row (GTK_CLIST (number_cat_list), i, 0);
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

	if (cell_format_prop_win)
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
create_number_format_page (GtkWidget *prop_win, MStyleElement *styles,
			   GtkWidget **focus_widget)
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
	format = NULL;
	if (styles [MSTYLE_FORMAT].type != MSTYLE_ELEMENT_CONFLICT &&
	    styles [MSTYLE_FORMAT].type)
		format = styles [MSTYLE_FORMAT].u.format;

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
	gtk_table_attach (tt, gtk_label_new (_("Code:")),
			  0, 1, 0, 1, 0, 0, 2, 0);
	
	number_input = gnumeric_dialog_entry_new (GNOME_DIALOG (prop_win));
	gtk_signal_connect (GTK_OBJECT (number_input), "changed",
			    GTK_SIGNAL_FUNC (format_code_changed), prop_win);

	gtk_table_attach (tt, number_input, 1, 2, 0, 1,
			  GTK_FILL | GTK_EXPAND, 0, 0, 2);
	
	/* 2.2 Sample */
	gtk_table_attach (tt, gtk_label_new (_("Sample:")),
			  0, 1, 1, 2, 0, 0, 2, 0);
	number_sample = gtk_label_new ("X");
	gtk_misc_set_alignment (GTK_MISC (number_sample), 0.0, 0.5);
	gtk_table_attach (tt, number_sample, 1, 2, 1, 2,
			  GTK_FILL | GTK_EXPAND, 0, 0, 2);

	gtk_table_attach (t, GTK_WIDGET (tt), 0, 2, INPUT_LINE, INPUT_LINE + 1,
			  GTK_FILL | GTK_EXPAND, 0, 0, 0);

	/* 3. Format codes */
	number_format_list = my_clist_new ();
	scrolled_list = gtk_scrolled_window_new (NULL, NULL);
	gtk_container_add (GTK_CONTAINER (scrolled_list), number_format_list);
	gtk_table_attach (t, l = gtk_label_new (_("Format codes")),
			  1, 2, BOXES_LINE, BOXES_LINE+1,
			  GTK_FILL | GTK_EXPAND, 0, 0, 0);
	gtk_misc_set_alignment (GTK_MISC (l), 0.0, 0.5);
	gtk_table_attach_defaults (t, scrolled_list, 1, 2,
				   BOXES_LINE + 1, BOXES_LINE + 2);
	format_list_fill (0);

	/* 3.1 connect the signal handled for row selected */
	gtk_signal_connect (GTK_OBJECT (number_format_list), "select_row",
			    GTK_SIGNAL_FUNC (format_selected), prop_win);


	/* 3.2: Invoke the current style for the cell if possible */
	if (format) {
		if (!format_find (format->format))
		    gtk_entry_set_text (GTK_ENTRY (number_input),
					format->format);
	}


	/* 4. finish */
	gtk_widget_show_all (GTK_WIDGET (t));
	
	return GTK_WIDGET (t);
}

static void
apply_number_formats (Style *style, Sheet *sheet, MStyleElement *styles)
{
	MStyleElement e;
	char *str = gtk_entry_get_text (GTK_ENTRY (number_input));
	
	if (!strcmp (str, ""))
		return;

	e.type = MSTYLE_FORMAT;
	e.u.format = style_format_new (str);
	sheet_selection_apply_style (sheet, mstyle_new_elem (NULL, e));
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
create_align_page (GtkWidget *prop_win, MStyleElement *styles,
		  GtkWidget **focus_widget)
{
	GtkTable  *t;
	GtkWidget *w;
	int        n;
	
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
	
	if (styles [MSTYLE_ALIGN_H].type != MSTYLE_ELEMENT_CONFLICT &&
	    styles [MSTYLE_ALIGN_H].type)
		for (n = 0; horizontal_aligns [n].name; n++)
			if (horizontal_aligns [n].flag ==
			    styles [MSTYLE_ALIGN_H].u.align.h) {
				gtk_radio_button_select (hradio_list, n);
				break;
			}
	if (styles [MSTYLE_ALIGN_V].type != MSTYLE_ELEMENT_CONFLICT &&
	    styles [MSTYLE_ALIGN_V].type)
		for (n = 0; vertical_aligns [n].name; n++)
			if (vertical_aligns [n].flag ==
			    styles [MSTYLE_ALIGN_V].u.align.v) {
				gtk_radio_button_select (vradio_list, n);
				break;
			}
			

	if (styles [MSTYLE_FIT_IN_CELL].type != MSTYLE_ELEMENT_CONFLICT &&
	    styles [MSTYLE_FIT_IN_CELL].type)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (auto_return),
					      styles [MSTYLE_FIT_IN_CELL].u.fit_in_cell);

	/* Now after we *potentially* toggled the radio button above, we
	 * connect the signals to activate the propertybox
	 */

	make_radio_notify_change (hradio_list, prop_win);
	make_radio_notify_change (vradio_list, prop_win);
	gtk_widget_show_all (GTK_WIDGET (t));
	*focus_widget = (GTK_WIDGET(g_slist_last(hradio_list)->data));

	return GTK_WIDGET (t);
}

static void
apply_align_format (Style *style, Sheet *sheet, MStyleElement *styles)
{
	int i;
	int halign, valign, autor;
	MStyleElement e;

	i = gtk_radio_group_get_selected (hradio_list);
	halign = horizontal_aligns [i].flag;
	if (halign) {
		e.type = MSTYLE_ALIGN_H;
		e.u.align.h = halign;
		sheet_selection_apply_style (sheet, mstyle_new_elem (NULL, e));
	}

	i = gtk_radio_group_get_selected (vradio_list);
	valign = vertical_aligns [i].flag;
	if (valign) {
		e.type = MSTYLE_ALIGN_V;
		e.u.align.v = valign;
		sheet_selection_apply_style (sheet, mstyle_new_elem (NULL, e));
	}

	autor = GTK_TOGGLE_BUTTON (auto_return)->active;
	if (autor) {
		e.type = MSTYLE_FIT_IN_CELL;
		e.u.fit_in_cell = autor;
		sheet_selection_apply_style (sheet, mstyle_new_elem (NULL, e));
	}

	e.type = MSTYLE_ORIENTATION;
	e.u.orientation = ORIENT_HORIZ;
	sheet_selection_apply_style (sheet, mstyle_new_elem (NULL, e));
}

static void
font_changed (GtkWidget *widget, GtkStyle *previous_style, GnomePropertyBox *prop_win)
{
	gnome_property_box_changed (prop_win);
}

static GtkWidget *
create_font_page (GtkWidget *prop_win, MStyleElement *styles,
		  GtkWidget **focus_widget)
{
	font_widget = font_selector_new ();
	gtk_widget_show (font_widget);

	gtk_signal_connect (GTK_OBJECT (FONT_SELECTOR (font_widget)->font_preview),
			    "style_set",
			    GTK_SIGNAL_FUNC (font_changed), prop_win);

	gnome_dialog_editable_enters
	  (GNOME_DIALOG(prop_win), 
	   GTK_EDITABLE(FONT_SELECTOR (font_widget)->font_name_entry));
	gnome_dialog_editable_enters
	  (GNOME_DIALOG(prop_win), 
	   GTK_EDITABLE(FONT_SELECTOR (font_widget)->font_style_entry));
	gnome_dialog_editable_enters
	  (GNOME_DIALOG(prop_win), 
	   GTK_EDITABLE(FONT_SELECTOR (font_widget)->font_size_entry));
	gnome_dialog_editable_enters
	  (GNOME_DIALOG(prop_win), 
	   GTK_EDITABLE(FONT_SELECTOR (font_widget)->font_preview));

	/* Focus alternatives: */
	/*     Size entry  (font_widget->font_size_entry) */
	/* or  Font listbox (font_widget->font_name_list). */
	/* Font entry is not editable. */
	*focus_widget = 
		GTK_WIDGET (FONT_SELECTOR (font_widget)->font_size_entry);
	return font_widget;
}

static gboolean
cb_set_row_height(Sheet *sheet, ColRowInfo *info, void *height)
{
	sheet_row_set_internal_height (sheet, info, *((double *)height));
	return FALSE;
}

static void
apply_font_format (Style *style, Sheet *sheet, MStyleElement *styles)
{
	FontSelector *font_sel = FONT_SELECTOR (font_widget);
	GnomeDisplayFont *gnome_display_font;
	GnomeFont *gnome_font;
	MStyleElement e;
	char *family_name;
	double height;

	gnome_display_font = font_sel->display_font;
	if (!gnome_display_font)
		return;

	gnome_font = gnome_display_font->gnome_font;
	family_name = gnome_font->fontmap_entry->familyname;
	height = gnome_display_font->gnome_font->size;

/*	style->valid_flags |= STYLE_FONT;
	style->font = style_font_new (
		family_name,
		gnome_font->size,
		sheet->last_zoom_factor_used,
		gnome_font->fontmap_entry->weight_code >= GNOME_FONT_BOLD,
		gnome_font->fontmap_entry->italic);*/

	e.type = MSTYLE_FONT_NAME;
	e.u.font.name = g_strdup (family_name);
	sheet_selection_apply_style (sheet, mstyle_new_elem (NULL, e));

	e.type = MSTYLE_FONT_SIZE;
	e.u.font.size = gnome_font->size;
	sheet_selection_apply_style (sheet, mstyle_new_elem (NULL, e));
		
	e.type = MSTYLE_FONT_BOLD;
	e.u.font.bold = gnome_font->fontmap_entry->weight_code >= GNOME_FONT_BOLD;
	sheet_selection_apply_style (sheet, mstyle_new_elem (NULL, e));
		
	e.type = MSTYLE_FONT_ITALIC;
	e.u.font.italic = gnome_font->fontmap_entry->italic;
	sheet_selection_apply_style (sheet, mstyle_new_elem (NULL, e));
		
	/* Apply the new font to all of the cell rows */
/*	for (; cells; cells = cells->next){
		Cell *cell = cells->data;
		
		cell_set_font_from_style (cell, style->font);
		}*/

	/* Now apply it to every row in the selection */
/*	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		GList *rl;*/
		
		/* Special case, the whole spreadsheet */
/*		if (ss->user.start.row == 0 && ss->user.end.row == SHEET_MAX_ROWS-1)
			sheet_row_set_internal_height (sheet, &sheet->default_row_style, height);

		for (rl = sheet->rows_info; rl; rl = rl->next){
			ColRowInfo *ri = rl->data;

			if (ri->pos < ss->user.start.row)
				break;
			if (ri->pos > ss->user.end.row)
				break;
			
			sheet_row_set_internal_height (sheet, ri, height);
		}
		}
		if (ss->user.start.row == 0 && ss->user.end.row == SHEET_MAX_ROWS-1)
			sheet_row_set_internal_height (sheet, &sheet->rows.default_style, height);
		else
			sheet_foreach_colrow (sheet, &sheet->rows,
					      ss->user.start.row, ss->user.end.row,
					      &cb_set_row_height, &height);
	}*/
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

static void
activate_toggle (GtkWidget *color_sel, GtkToggleButton *button)
{
	if (button->active)
		return;

	gtk_toggle_button_set_active (button, TRUE);
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
	gtk_signal_connect (
		GTK_OBJECT (foreground_cs), "clicked",
		activate_toggle, r2);
		
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
	gtk_signal_connect (GTK_OBJECT (background_cs), "clicked",
			    GTK_SIGNAL_FUNC (activate_toggle), r2);
			    
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

static void
set_color_picker_from_style (GnomeColorPicker *cp, MStyleElement e)
{
	gdouble rd, gd, bd, ad;
	gushort red, green, blue;

	g_return_if_fail (cp != NULL);

	if (e.type == MSTYLE_ELEMENT_UNSET)
		return;

	g_return_if_fail (e.type == MSTYLE_COLOR_FORE ||
			  e.type == MSTYLE_COLOR_BACK);

	red   = e.u.color.fore->red;
	green = e.u.color.fore->green;
	blue  = e.u.color.fore->blue;
	
	rd = (gdouble) red / 65535;
	gd = (gdouble) green / 65535;
	bd = (gdouble) blue / 65535;
	ad = 1.0;
	gnome_color_picker_set_d (cp, rd, gd, bd, ad);
}

static GtkWidget *
create_coloring_page (GtkWidget *prop_win,
		      MStyleElement *styles,
		      GtkWidget **focus_widget)
{
	GtkTable *t;
	GtkWidget *fore, *back;
	int e = GTK_FILL | GTK_EXPAND;

	t = (GtkTable *) gtk_table_new (0, 0, 0);

	fore = create_foreground_radio (prop_win);
	back = create_background_radio (prop_win);

	if (styles [MSTYLE_COLOR_FORE].type != MSTYLE_ELEMENT_CONFLICT)
		if (styles [MSTYLE_COLOR_FORE].type) {
			gtk_radio_button_select (foreground_radio_list, 0);
			gnome_color_picker_set_d (GNOME_COLOR_PICKER (foreground_cs), 0, 0, 0, 0);
		} else {
			gtk_radio_button_select (foreground_radio_list, 1);
			set_color_picker_from_style (GNOME_COLOR_PICKER (foreground_cs),
						     styles [MSTYLE_COLOR_FORE]);
		}
	else
		gtk_radio_button_select (foreground_radio_list, 2);

	if (styles [MSTYLE_COLOR_BACK].type != MSTYLE_ELEMENT_CONFLICT)
		if (styles [MSTYLE_COLOR_BACK].type) {
			gtk_radio_button_select (background_radio_list, 0);
			gnome_color_picker_set_d (GNOME_COLOR_PICKER (background_cs), 0, 0, 0, 0);
		} else {
			gtk_radio_button_select (background_radio_list, 1);
			set_color_picker_from_style (GNOME_COLOR_PICKER (background_cs),
						     styles [MSTYLE_COLOR_BACK]);
		}
	else
		gtk_radio_button_select (foreground_radio_list, 2);

	make_radio_notify_change (foreground_radio_list, prop_win);
	make_radio_notify_change (background_radio_list, prop_win);
	
	gtk_table_attach (t, fore, 0, 1, 0, 1, e, 0, 4, 4);
	gtk_table_attach (t, back, 0, 1, 1, 2, e, 0, 4, 4);

	gtk_widget_show_all (GTK_WIDGET (t));
	*focus_widget = 
	  (GTK_WIDGET(g_slist_last(foreground_radio_list)->data));

	return GTK_WIDGET (t);
}

static void
apply_coloring_format (Style *style, Sheet *sheet, MStyleElement *styles)
{
	double rd, gd, bd, ad;
	gushort fore_change = FALSE, back_change = FALSE;
	gushort fore_red=0, fore_green=0, fore_blue=0;
	gushort back_red=0xff, back_green=0xff, back_blue=0xff;
	MStyleElement e;

	/*
	 * Let's check the foreground first
	 */
	switch (gtk_radio_group_get_selected (foreground_radio_list)) {
	/*
	 * case 0 means no foreground
	 */
	case 0:
		g_warning ("Default unimplemented");
/*		fore_red   = 0;
		fore_green = 0;
		fore_blue  = 0;
		style->valid_flags &= ~STYLE_FORE_COLOR;
		fore_change = TRUE;*/
		break;
	/*
	 * case 1 means colored foreground
	 */
	case 1:
		gnome_color_picker_get_d (GNOME_COLOR_PICKER (foreground_cs), &rd, &gd, &bd, &ad);
		fore_red   = rd * 65535;
		fore_green = gd * 65535;
		fore_blue  = bd * 65535;
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
		g_warning ("Default unimplemented");
/*		back_red   = 0xffff;
		back_green = 0xffff;
		back_blue  = 0xffff;
		style->valid_flags &= ~STYLE_BACK_COLOR;
		style->valid_flags &= ~STYLE_PATTERN;*/
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
		back_change = TRUE;
		break;

	/*
	 * case 2 means a pattern background
	 */
	case 2:
		back_red = 0xffff;
		back_green = 0xffff;
		back_blue = 0xffff;
		
		g_warning ("Pattern rendering unimplemented");
		back_change = TRUE;
		break;
	/*
	 * case 3 means no change
	 */
	case 3:
		back_change = FALSE;
		break;
	}

	if (fore_change) {
		e.type = MSTYLE_COLOR_FORE;
		e.u.color.fore = style_color_new (fore_red, fore_green, fore_blue);
		sheet_selection_apply_style (sheet, mstyle_new_elem (NULL, e));
	}

	if (back_change) {
		e.type = MSTYLE_COLOR_BACK;
		e.u.color.fore = style_color_new (back_red, back_green, back_blue);
		sheet_selection_apply_style (sheet, mstyle_new_elem (NULL, e));
	}
}

static struct {
	char       *title;
	GtkWidget *(*create_page) (GtkWidget *prop_win,
				   MStyleElement *styles, 
				   GtkWidget **focus_widget);
	void       (*apply_page)  (Style *style, Sheet *sheet,
				   MStyleElement *styles);
} cell_format_pages [] = {
	{ N_("Number"),    create_number_format_page,  apply_number_formats  },
	{ N_("Alignment"), create_align_page,          apply_align_format    },
	{ N_("Font"),      create_font_page,           apply_font_format     },
	{ N_("Coloring"),  create_coloring_page,       apply_coloring_format },
	{ NULL, NULL, NULL }
};

static void
cell_properties_apply (GtkObject *w, int page, MStyleElement *styles)
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

	cell_freeze_redraws ();
	
	for (i = 0; cell_format_pages [i].title; i++)
		(*cell_format_pages [i].apply_page)(style, sheet, styles);

	cell_thaw_redraws ();
	
	/* Attach this style to all of the selections */
	for (l = sheet->selections; l; l = l->next){
/*		SheetSelection *ss = l->data;*/
		
		g_warning ("No style attachment");
/*		sheet_style_attach (
			sheet,
			ss->user.start.col, ss->user.start.row,
			ss->user.end.col,   ss->user.end.row,
			style);*/
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
	GtkWidget     *prop_win;
	GtkWidget     *focus_widgets [sizeof cell_format_pages/
				     sizeof cell_format_pages[0]];
	MStyleElement *styles;
	int            i;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_assert (cell_format_prop_win == NULL);

	styles = sheet_selection_get_uniq_style (sheet);
	
	prop_win = gnome_property_box_new ();
	gnome_dialog_set_parent (GNOME_DIALOG (prop_win),
				 GTK_WINDOW (wb->toplevel));
	
	g_warning ("First cell should be setup");
	
	for (i = 0; cell_format_pages [i].title; i++){
		GtkWidget *page;

		focus_widgets[i] = NULL;
		page = (*cell_format_pages [i].create_page) 
			(prop_win, styles, &focus_widgets[i]);
		gnome_property_box_append_page (
			GNOME_PROPERTY_BOX (prop_win), page,
			gtk_label_new (_(cell_format_pages [i].title)));

	}

	gtk_signal_connect (GTK_OBJECT (prop_win), "apply",
			    GTK_SIGNAL_FUNC (cell_properties_apply), styles);
	gtk_signal_connect (GTK_OBJECT (prop_win), "help",
			    GTK_SIGNAL_FUNC (gnome_help_pbox_goto), &help_ref);
	
	gtk_object_set_data (GTK_OBJECT (prop_win), "Sheet", sheet);
	
	gtk_signal_connect (GTK_OBJECT (prop_win), "destroy",
			    GTK_SIGNAL_FUNC (cell_properties_close), NULL);

	gtk_notebook_set_page (
		GTK_NOTEBOOK (GNOME_PROPERTY_BOX(prop_win)->notebook),
		cell_format_last_page_used);
	
	gtk_widget_show (prop_win);
	if (focus_widgets [cell_format_last_page_used])
		gtk_widget_grab_focus 
			(focus_widgets [cell_format_last_page_used]);

	gtk_grab_add (prop_win);

	cell_format_prop_win = prop_win;
	gtk_main ();
	cell_format_prop_win = NULL;

	g_free (styles);
}
