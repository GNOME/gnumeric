/*
 * dialog-printer-setup.c: Printer setup dialog box
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "print-info.h"
#include "print.h"
#include "ranges.h"
#include "utils-dialog.h"

#define PREVIEW_X 170
#define PREVIEW_Y 170
#define PREVIEW_MARGIN_X 20
#define PREVIEW_MARGIN_Y 20
#define PAGE_X (PREVIEW_X - PREVIEW_MARGIN_X)
#define PAGE_Y (PREVIEW_Y - PREVIEW_MARGIN_Y)

typedef struct {
	UnitName   unit;
	double     value;
	GtkSpinButton *spin;
	GtkAdjustment *adj;
} UnitInfo;

typedef struct {
	/* THe Canvas object */
	GtkWidget        *canvas;

	/* Objects in the Preview Canvas */
	GnomeCanvasItem  *group;
	
	/* Values for the scaling of the nice preview */
	int offset_x, offset_y;	/* For centering the small page preview */
	double scale;
} PreviewInfo;

typedef struct {
	Sheet            *sheet;
	GladeXML         *gui;
	PrintInformation *pi;
	GtkWidget        *dialog;

	struct {
		UnitInfo top, bottom;
		UnitInfo left, right;
		UnitInfo header, footer;
	} margins;

	const GnomePaper *paper;
	const GnomePaper *current_paper;

	PreviewInfo preview;

	GtkWidget *icon_rd;
	GtkWidget *icon_dr;

	/*
	 * The header/footers formats 
	 */
	PrintHF *header;
	PrintHF *footer;
} dialog_print_info_t;

static void fetch_settings (dialog_print_info_t *dpi);

#if 0
static double
unit_into_to_points (UnitInfo *ui)
{
	return unit_convert (ui->value, ui->unit, UNIT_POINTS);
}
#endif

static void
preview_page_destroy (dialog_print_info_t *dpi)
{
	if (dpi->preview.group) {
		gtk_object_destroy (GTK_OBJECT (dpi->preview.group));

		dpi->preview.group = NULL;
	}
}

static void
make_line (GnomeCanvasGroup *g, double x1, double y1, double x2, double y2)
{
	GnomeCanvasPoints *points;

	points = gnome_canvas_points_new (2);
	points->coords [0] = x1;
	points->coords [1] = y1;
	points->coords [2] = x2;
	points->coords [3] = y2;
	gnome_canvas_item_new (
		GNOME_CANVAS_GROUP (g), gnome_canvas_line_get_type (),
		"points", points,
		"width_pixels", 1,
		"fill_color",   "gray",
		NULL);
	gnome_canvas_points_unref (points);
}

static void
draw_margins (dialog_print_info_t *dpi, double x1, double y1, double x2, double y2)
{
	GnomeCanvasGroup *g = GNOME_CANVAS_GROUP (dpi->preview.group);

	/* Margins */
	make_line (g, x1 + 8, y1, x1 + 8, y2);
	make_line (g, x2 - 8, y1, x2 - 8, y2);
	make_line (g, x1, y1 + 8, x2, y1 + 8);
	make_line (g, x1, y2 - 8, x2, y2 - 8);

	/* Headers & footers */
	make_line (g, x1, y1 + 13, x2, y1 + 13);
	make_line (g, x1, y2 - 13, x2, y2 - 13);
}

static void
preview_page_create (dialog_print_info_t *dpi)
{
	double x1, y1, x2, y2;
	double width, height;
	PreviewInfo *pi = &dpi->preview;

	width  = gnome_paper_pswidth  (dpi->paper);
	height = gnome_paper_psheight (dpi->paper);

	if (width < height)
		pi->scale = PAGE_Y / height;
	else
		pi->scale = PAGE_X / width;

	pi->offset_x = (PREVIEW_X - (width  * pi->scale)) / 2;
	pi->offset_y = (PREVIEW_Y - (height * pi->scale)) / 2;
/*	pi->offset_x = pi->offset_y = 0; */
	x1 = pi->offset_x + 0 * pi->scale;
	y1 = pi->offset_y + 0 * pi->scale;
	x2 = pi->offset_x + width * pi->scale;
	y2 = pi->offset_y + height * pi->scale;

	pi->group = gnome_canvas_item_new (
		gnome_canvas_root (GNOME_CANVAS (pi->canvas)),
		gnome_canvas_group_get_type (),
		"x", 0.0,
		"y", 0.0,
		NULL);
	
	gnome_canvas_item_new (
		GNOME_CANVAS_GROUP (pi->group),
		gnome_canvas_rect_get_type (),
		"x1",  	      	 (double) x1+2,
		"y1",  	      	 (double) y1+2,
		"x2",  	      	 (double) x2+2,
		"y2",         	 (double) y2+2,
		"fill_color",    "black",
		"outline_color", "black",
		"width_pixels",   1,
		NULL);
		
	gnome_canvas_item_new (
		GNOME_CANVAS_GROUP (pi->group),
		gnome_canvas_rect_get_type (),
		"x1",  	      	 (double) x1,
		"y1",  	      	 (double) y1,
		"x2",  	      	 (double) x2,
		"y2",         	 (double) y2,
		"fill_color",    "white",
		"outline_color", "black",
		"width_pixels",   1,
		NULL);

	draw_margins (dpi, x1, y1, x2, y2);
}

static void
canvas_update (dialog_print_info_t *dpi)
{
	if (dpi->current_paper != dpi->paper){
		preview_page_destroy (dpi);
		dpi->current_paper = dpi->paper;
		preview_page_create (dpi);
	}
}

static void
do_convert (UnitInfo *target, UnitName new_unit)
{
	if (target->unit == new_unit)
		return;

	target->value = unit_convert (target->value, target->unit, new_unit);
	target->unit = new_unit;

	gtk_spin_button_set_value (target->spin, target->value);
}

static void
convert_to_pt (GtkWidget *widget, UnitInfo *target)
{
	do_convert (target, UNIT_POINTS);
}

static void
convert_to_mm (GtkWidget *widget, UnitInfo *target)
{
	do_convert (target, UNIT_MILLIMETER);
}

static void
convert_to_cm (GtkWidget *widget, UnitInfo *target)
{
	do_convert (target, UNIT_CENTIMETER);
}

static void
convert_to_in (GtkWidget *widget, UnitInfo *target)
{
	do_convert (target, UNIT_INCH);
}

static void
add_unit (GtkWidget *menu, int i, void (*convert)(GtkWidget *, UnitInfo *), void *data)
{
	GtkWidget *item;

	item = gtk_menu_item_new_with_label (_(unit_name_get_short_name (i)));
	gtk_widget_show (item);
	gtk_menu_append (GTK_MENU (menu), item);

	gtk_signal_connect (
		GTK_OBJECT (item), "activate",
		GTK_SIGNAL_FUNC (convert), data);
}

static void
unit_changed (GtkSpinButton *spin_button, UnitInfo *target)
{
	target->value = target->adj->value;
}

static GtkWidget *
unit_editor_new (UnitInfo *target, PrintUnit init)
{
	GtkWidget *box, *om, *menu;
	
	/*
	 * FIXME: Hardcoded for now
	 */
	target->unit = UNIT_CENTIMETER;
	target->value = unit_convert (init.points, UNIT_POINTS, UNIT_CENTIMETER);

	target->adj = GTK_ADJUSTMENT (gtk_adjustment_new (
		target->value,
		0.0, 1000.0, 0.5, 1.0, 1.0));
	target->spin = GTK_SPIN_BUTTON (gtk_spin_button_new (target->adj, 1, 1));
	gtk_widget_set_usize (GTK_WIDGET (target->spin), 60, 0);
	gtk_signal_connect (
		GTK_OBJECT (target->spin), "changed",
		GTK_SIGNAL_FUNC (unit_changed), target);

	/*
	 * We need to figure out a way to make the dialog box look good
	 *
	 * Currently the result from using this is particularly ugly.
	 */
	if (1) {
		gtk_widget_show (GTK_WIDGET (target->spin));
		return GTK_WIDGET (target->spin);
	} else {
		box = gtk_hbox_new (0, 0);
		
		gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (target->spin), TRUE, TRUE, 0);
		om = gtk_option_menu_new ();
		gtk_box_pack_start (GTK_BOX (box), om, FALSE, FALSE, 0);
		gtk_widget_show_all (box);
		
		menu = gtk_menu_new ();
		
		add_unit (menu, UNIT_POINTS, convert_to_pt, target);
		add_unit (menu, UNIT_MILLIMETER, convert_to_mm, target);
		add_unit (menu, UNIT_CENTIMETER, convert_to_cm, target);
		add_unit (menu, UNIT_INCH, convert_to_in, target);
		
		gtk_menu_set_active (GTK_MENU (menu), target->unit);
		gtk_option_menu_set_menu (GTK_OPTION_MENU (om), menu);
		gtk_option_menu_set_history (GTK_OPTION_MENU (om), init.desired_display);
		
		return box;
	}
}

static void
tattach (GtkTable *table, int x, int y, PrintUnit init, UnitInfo *target)
{
	GtkWidget *w;
	
	w = unit_editor_new (target, init);
	gtk_table_attach (
		table, w, x, x+1, y, y+1,
		0, 0, 0, 0);
}

static void
do_setup_margin (dialog_print_info_t *dpi)
{
	GtkTable *table;
	PrintMargins *pm = &dpi->pi->margins;

	dpi->preview.canvas = gnome_canvas_new ();
	gnome_canvas_set_scroll_region (
		GNOME_CANVAS (dpi->preview.canvas),
		0.0, 0.0, PREVIEW_X, PREVIEW_Y);
	gtk_widget_set_usize (dpi->preview.canvas, PREVIEW_X, PREVIEW_Y);
	gtk_widget_show (dpi->preview.canvas);
	
	table = GTK_TABLE (glade_xml_get_widget (dpi->gui, "margin-table"));

	tattach (table, 1, 1, pm->top,    &dpi->margins.top);
	tattach (table, 2, 1, pm->header, &dpi->margins.header);
	tattach (table, 0, 4, pm->left,   &dpi->margins.left);
	tattach (table, 2, 4, pm->right,  &dpi->margins.right);
	tattach (table, 1, 7, pm->bottom, &dpi->margins.bottom);
	tattach (table, 2, 7, pm->footer, &dpi->margins.footer);

	gtk_table_attach (table, dpi->preview.canvas,
			  1, 2, 3, 6, GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
	
	if (dpi->pi->center_vertically)
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (
				glade_xml_get_widget (dpi->gui, "center-vertical")),
			TRUE);

	if (dpi->pi->center_horizontally)
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (
				glade_xml_get_widget (dpi->gui, "center-horizontal")),
			TRUE);
}

static void
header_changed (GtkObject *object, dialog_print_info_t *dpi)
{
	PrintHF *format = gtk_object_get_user_data (object);

	print_hf_free (dpi->header);
	dpi->header = print_hf_copy (format);
}

static void
footer_changed (GtkObject *object, dialog_print_info_t *dpi)
{
	PrintHF *format = gtk_object_get_user_data (object);

	print_hf_free (dpi->footer);
	dpi->footer = print_hf_copy (format);
}

/*
 * Fills one of the GtkCombos for headers or footers with the list
 * of existing header/footer formats
 */
static void
fill_hf (dialog_print_info_t *dpi, GtkOptionMenu *om, GtkSignalFunc callback, PrintHF *select)
{
	GList *l;
	HFRenderInfo *hfi;
	GtkWidget *menu;
	int i, idx = 0;
	
	hfi = hf_render_info_new ();
	hfi->page = 1;
	hfi->pages = 1;

	menu = gtk_menu_new ();

	for (i = 0, l = hf_formats; l; l = l->next, i++) {
		GtkWidget *li;
		PrintHF *format = l->data;
		char *left, *middle, *right;
		char *res;

		if (print_hf_same (format, select))
			idx = i;
		
		left   = hf_format_render (format->left_format, hfi, HF_RENDER_PRINT);
		middle = hf_format_render (format->middle_format, hfi, HF_RENDER_PRINT);
		right  = hf_format_render (format->right_format, hfi, HF_RENDER_PRINT);

		res = g_strdup_printf (
			"%s%s%s%s%s",
			left, *left ? "," : "",
			right, *right ? "," : "",
			middle);
			
		li = gtk_menu_item_new_with_label (res);
		gtk_widget_show (li);
		gtk_container_add (GTK_CONTAINER (menu), li);
		gtk_object_set_user_data (GTK_OBJECT (li), format);
		gtk_signal_connect (GTK_OBJECT (li), "activate", callback, dpi);
		
		g_free (res);
		g_free (left);
		g_free (middle);
		g_free (right);
	}
	gtk_option_menu_set_menu (om, menu);
	gtk_option_menu_set_history (om, idx);
	
	hf_render_info_destroy (hfi);
}

static void
text_insert (GtkText *text_widget, const char *text)
{
	int len = strlen (text);
	gint pos = 0;
	
	gtk_editable_insert_text (GTK_EDITABLE (text_widget), text, len, &pos);
}

static char *
text_get (GtkText *text_widget)
{
	return gtk_editable_get_chars (GTK_EDITABLE (text_widget), 0, -1);
}

static PrintHF *
do_hf_config (const char *title, PrintHF **config, Workbook *wb)
{
	GladeXML *gui = glade_xml_new (GNUMERIC_GLADEDIR "/hf-config.glade", NULL);
	GtkText *left, *middle, *right;
	GtkWidget *dialog;
	PrintHF *ret = NULL;
	int v;
	
	if (!gui){
		g_warning ("Could not find hf-config.glade");
		return NULL;
	}
	left   = GTK_TEXT (glade_xml_get_widget (gui, "left-format"));
	middle = GTK_TEXT (glade_xml_get_widget (gui, "center-format"));
	right  = GTK_TEXT (glade_xml_get_widget (gui, "right-format"));
	dialog = glade_xml_get_widget (gui, "hf-config");
	
	text_insert (left, (*config)->left_format);
	text_insert (middle, (*config)->middle_format);
	text_insert (right, (*config)->right_format);

	v = gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));

	if (v == 0) {
		char *left_format, *right_format, *middle_format;

		left_format   = text_get (left);
		middle_format = text_get (middle);
		right_format  = text_get (right);

		print_hf_free (*config);
		*config = print_hf_new (left_format, middle_format, right_format);

		g_free (left_format);
		g_free (middle_format);
		g_free (right_format);

		ret = print_hf_register (*config);
	}

	if (v != -1)
		gtk_object_destroy (GTK_OBJECT (dialog));
	
	gtk_object_unref (GTK_OBJECT (gui));

	return ret;
}
	      
static void
do_setup_hf_menus (dialog_print_info_t *dpi, PrintHF *header_sel, PrintHF *footer_sel)
{
	GtkOptionMenu *header = GTK_OPTION_MENU (glade_xml_get_widget (dpi->gui, "option-menu-header"));
	GtkOptionMenu *footer = GTK_OPTION_MENU (glade_xml_get_widget (dpi->gui, "option-menu-footer"));

	if (header_sel)
		fill_hf (dpi, header, GTK_SIGNAL_FUNC (header_changed), header_sel);
	if (footer_sel)
		fill_hf (dpi, footer, GTK_SIGNAL_FUNC (footer_changed), footer_sel);
}

static void
do_header_config (GtkWidget *button, dialog_print_info_t *dpi)
{
	PrintHF *hf;
	
	hf = do_hf_config (_("Custom header configuration"),
			   &dpi->header, dpi->sheet->workbook);

	if (hf)
		do_setup_hf_menus (dpi, hf, NULL);
}

static void
do_footer_config (GtkWidget *button, dialog_print_info_t *dpi)
{
	PrintHF *hf;
	
	hf = do_hf_config (_("Custom footer configuration"),
			   &dpi->footer, dpi->sheet->workbook);

	if (hf)
		do_setup_hf_menus (dpi, NULL, hf);
}

static void
do_setup_hf (dialog_print_info_t *dpi)
{
	dpi->header = print_hf_copy (dpi->pi->header ? dpi->pi->header :
				     hf_formats->data);
	dpi->footer = print_hf_copy (dpi->pi->footer ? dpi->pi->footer :
				     hf_formats->data);

	do_setup_hf_menus (dpi, dpi->header, dpi->footer);
	
	gtk_signal_connect (
		GTK_OBJECT (glade_xml_get_widget (dpi->gui, "configure-header")),
		"clicked", GTK_SIGNAL_FUNC (do_header_config), dpi);

	gtk_signal_connect (
		GTK_OBJECT (glade_xml_get_widget (dpi->gui, "configure-footer")),
		"clicked", GTK_SIGNAL_FUNC (do_footer_config), dpi);
}

static void
display_order_icon (GtkToggleButton *toggle, dialog_print_info_t *dpi)
{
	GtkWidget *show, *hide;
	
	if (toggle->active){
		show = dpi->icon_rd;
		hide = dpi->icon_dr;
	} else {
		hide = dpi->icon_rd;
		show = dpi->icon_dr;
	}

	gtk_widget_show (show);
	gtk_widget_hide (hide);
}

static void
do_setup_page_info (dialog_print_info_t *dpi)
{
	GtkWidget *divisions = glade_xml_get_widget (dpi->gui, "check-print-divisions");
	GtkWidget *bw        = glade_xml_get_widget (dpi->gui, "check-black-white");
	GtkWidget *titles    = glade_xml_get_widget (dpi->gui, "check-print-titles");
	GtkWidget *order_rd  = glade_xml_get_widget (dpi->gui, "radio-order-right");
	GtkWidget *order_dr  = glade_xml_get_widget (dpi->gui, "radio-order-down");
	GtkWidget *table     = glade_xml_get_widget (dpi->gui, "page-order-table");
	GtkEntry *entry_top, *entry_left;
	GtkWidget *order;

	dpi->icon_rd = gnumeric_load_image ("right-down.png");
	dpi->icon_dr = gnumeric_load_image ("down-right.png");

	gtk_widget_hide (dpi->icon_dr);
	gtk_widget_hide (dpi->icon_rd);

	gtk_table_attach (
		GTK_TABLE (table), dpi->icon_rd,
		1, 2, 0, 2, GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
	gtk_table_attach (
		GTK_TABLE (table), dpi->icon_dr,
		1, 2, 0, 2, GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);

	gtk_signal_connect (GTK_OBJECT (order_rd), "toggled", GTK_SIGNAL_FUNC (display_order_icon), dpi);
	
	if (dpi->pi->print_line_divisions)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (divisions), TRUE);

	if (dpi->pi->print_black_and_white)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (bw), TRUE);

	if (dpi->pi->print_titles)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (titles), TRUE);

	if (dpi->pi->print_order == PRINT_ORDER_DOWN_THEN_RIGHT)
		order = order_dr;
	else
		order = order_rd;

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (order), TRUE);
	display_order_icon (GTK_TOGGLE_BUTTON (order_rd), dpi);

	if (dpi->pi->repeat_top.use){
		char *s;
		entry_top  = GTK_ENTRY (glade_xml_get_widget (dpi->gui, "repeat-rows-entry"));

		s = value_cellrange_get_as_string (&dpi->pi->repeat_top.range, FALSE);
		gtk_entry_set_text (entry_top, s);
		g_free (s);
	}

	if (dpi->pi->repeat_left.use){
		char *s;
		entry_left = GTK_ENTRY (glade_xml_get_widget (dpi->gui, "repeat-cols-entry"));

		s = value_cellrange_get_as_string (&dpi->pi->repeat_left.range, FALSE);
		gtk_entry_set_text (entry_left, s);
		g_free (s);
	}
}

static void
paper_size_changed (GtkEntry *entry, dialog_print_info_t *dpi)
{
	char *text;
	
	text = gtk_entry_get_text (entry);

	dpi->paper = gnome_paper_with_name (text);
	canvas_update (dpi);
}

static void
do_setup_page (dialog_print_info_t *dpi)
{
	PrintInformation *pi = dpi->pi;
	GtkWidget *image;
	GtkCombo *combo;
	GtkTable *table;
	GladeXML *gui;
	char *toggle;

	gui = dpi->gui;
	table = GTK_TABLE (glade_xml_get_widget (gui, "table-orient"));
	
	image = gnumeric_load_image ("orient-vertical.png");
	gtk_widget_show (image);
	gtk_table_attach_defaults (table, image, 0, 1, 0, 1);
	image = gnumeric_load_image ("orient-horizontal.png");
	gtk_widget_show (image);
	gtk_table_attach_defaults (table, image, 2, 3, 0, 1);

	/*
	 * Select the proper radio for orientation
	 */
	if (pi->orientation == PRINT_ORIENT_VERTICAL)
		toggle = "vertical-radio";
	else
		toggle = "horizontal-radio";

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, toggle)), 1);

	/*
	 * Set the scale
	 */
	if (pi->scaling.type == PERCENTAGE)
		toggle = "scale-percent-radio";
	else
		toggle = "scale-size-fit-radio";

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, toggle)), TRUE);

	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (glade_xml_get_widget (
			gui, "scale-percent-spin")), pi->scaling.percentage);

	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (glade_xml_get_widget (
			gui, "scale-width-spin")), pi->scaling.dim.cols);
	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (glade_xml_get_widget (
			gui, "scale-height-spin")), pi->scaling.dim.rows);

	/*
	 * Fill the paper sizes
	 */
	combo = GTK_COMBO (glade_xml_get_widget (
		gui, "paper-size-combo"));
	gtk_combo_set_value_in_list (combo, TRUE, 0);
	gtk_combo_set_popdown_strings (combo, gnome_paper_name_list ());
	gtk_signal_connect (GTK_OBJECT (combo->entry), "changed",
			    paper_size_changed, dpi);

	if (dpi->pi->paper == NULL)
		dpi->pi->paper = gnome_paper_with_name (gnome_paper_name_default ());
	dpi->paper = dpi->pi->paper;
	
	gtk_entry_set_text (GTK_ENTRY (combo->entry), gnome_paper_name (dpi->pi->paper));
}

static void
do_print_cb (GtkWidget *w, dialog_print_info_t *dpi)
{
	fetch_settings (dpi);
	sheet_print (dpi->sheet, FALSE, PRINT_ACTIVE_SHEET);
}

static void
do_print_preview_cb (GtkWidget *w, dialog_print_info_t *dpi)
{
	fetch_settings (dpi);
	sheet_print (dpi->sheet, TRUE, PRINT_ACTIVE_SHEET);
}

static void
do_setup_main_dialog (dialog_print_info_t *dpi)
{
	GtkWidget *notebook, *old_parent, *focus_target;
	int i;

	g_return_if_fail (dpi != NULL);
	g_return_if_fail (dpi->sheet != NULL);
	g_return_if_fail (dpi->sheet->workbook != NULL);
	
	/*
	 * Moves the whole thing into a GnomeDialog, needed until
	 * we get GnomeDialog support in Glade.
	 */
	dpi->dialog = gnome_dialog_new (
		_("Print setup"),
		GNOME_STOCK_BUTTON_OK,
		GNOME_STOCK_BUTTON_CANCEL,
		NULL);
	gtk_window_set_policy (GTK_WINDOW (dpi->dialog), FALSE, TRUE, TRUE);

	notebook = glade_xml_get_widget (dpi->gui, "print-setup-notebook");
	old_parent = gtk_widget_get_toplevel (notebook->parent);
	gtk_widget_reparent (notebook, GNOME_DIALOG (dpi->dialog)->vbox);
	gtk_widget_destroy (old_parent);
	
	gtk_widget_queue_resize (notebook);

	focus_target = glade_xml_get_widget (dpi->gui, "vertical-radio");
	gtk_widget_grab_focus (focus_target);
	
	for (i = 1; i < 5; i++) {
		GtkWidget *w;
		char *print = g_strdup_printf ("print-%d", i);
		char *preview = g_strdup_printf ("preview-%d", i);

		w = glade_xml_get_widget (dpi->gui, print);
		gtk_signal_connect (GTK_OBJECT (w), "clicked",
				    GTK_SIGNAL_FUNC (do_print_cb), dpi);
		w = glade_xml_get_widget (dpi->gui, preview);
		gtk_signal_connect (GTK_OBJECT (w), "clicked",
				    GTK_SIGNAL_FUNC (do_print_preview_cb), dpi);
		g_free (print);
		g_free (preview);
	}

	/*
	 * Hide non-functional buttons for now
	 */
	for (i = 1; i < 5; i++) {
		char *options = g_strdup_printf ("options-%d", i);
		GtkWidget *w;

		w = glade_xml_get_widget (dpi->gui, options);
		gtk_widget_hide (w);

		g_free (options);
	}
}

static dialog_print_info_t *
dialog_print_info_new (Sheet *sheet)
{
	dialog_print_info_t *dpi;
	GladeXML *gui;
	
	gui = glade_xml_new (GNUMERIC_GLADEDIR "/print.glade", NULL);
	if (!gui){
		g_error ("Could not load print.glade");
		return NULL;
	}

	dpi = g_new0 (dialog_print_info_t, 1);
	dpi->sheet = sheet;
	dpi->gui   = gui;
	dpi->pi    = sheet->print_info;

	do_setup_main_dialog (dpi);
	do_setup_margin (dpi);
	do_setup_hf (dpi);
	do_setup_page_info (dpi);
	do_setup_page (dpi);
	
	return dpi;
}

static void
do_fetch_page (dialog_print_info_t *dpi)
{
	GtkWidget *w;
	GladeXML *gui = dpi->gui;
	char *t;
	
	w = glade_xml_get_widget (gui, "vertical-radio");
	if (GTK_TOGGLE_BUTTON (w)->active)
		dpi->pi->orientation = PRINT_ORIENT_VERTICAL;
	else
		dpi->pi->orientation = PRINT_ORIENT_HORIZONTAL;

	w = glade_xml_get_widget (gui, "scale-percent-radio");
	if (GTK_TOGGLE_BUTTON (w)->active)
		dpi->pi->scaling.type = PERCENTAGE;
	else
		dpi->pi->scaling.type = SIZE_FIT;

	w = glade_xml_get_widget (gui, "scale-percent-spin");
	dpi->pi->scaling.percentage = GTK_SPIN_BUTTON (w)->adjustment->value;

	w = glade_xml_get_widget (gui, "scale-width-spin");
	dpi->pi->scaling.dim.cols = GTK_SPIN_BUTTON (w)->adjustment->value;

	w = glade_xml_get_widget (gui, "scale-height-spin");
	dpi->pi->scaling.dim.rows = GTK_SPIN_BUTTON (w)->adjustment->value;

	w = glade_xml_get_widget (gui, "paper-size-combo");
	t = gtk_entry_get_text (GTK_ENTRY (GTK_COMBO (w)->entry));
	
	if (gnome_paper_with_name (t) != NULL)
		dpi->pi->paper = gnome_paper_with_name (t);
}

static PrintUnit
unit_info_to_print_unit (UnitInfo *ui)
{
	PrintUnit u;

	u.desired_display = ui->unit;
        u.points = unit_convert (ui->value, ui->unit, UNIT_POINTS);

	return u;
}

static void
do_fetch_margins (dialog_print_info_t *dpi)
{
	PrintMargins *m = &dpi->pi->margins;
	GtkToggleButton *t;
	
	m->top    = unit_info_to_print_unit (&dpi->margins.top);
	m->bottom = unit_info_to_print_unit (&dpi->margins.bottom);
	m->left   = unit_info_to_print_unit (&dpi->margins.left);
	m->right  = unit_info_to_print_unit (&dpi->margins.right);
	m->header = unit_info_to_print_unit (&dpi->margins.header);
	m->footer = unit_info_to_print_unit (&dpi->margins.footer);

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "center-horizontal"));
	dpi->pi->center_horizontally = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "center-vertical"));
	dpi->pi->center_vertically = t->active;
}

static void
do_fetch_hf (dialog_print_info_t *dpi)
{
	print_hf_free (dpi->pi->header);
	print_hf_free (dpi->pi->footer);

	dpi->pi->header = print_hf_copy (dpi->header);
	dpi->pi->footer = print_hf_copy (dpi->footer);
}

static void
do_fetch_page_info (dialog_print_info_t *dpi)
{
	GtkToggleButton *t;
	Value *top_range, *left_range;
	GtkEntry *entry_top, *entry_left;
	
	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "check-print-divisions"));
	dpi->pi->print_line_divisions = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "check-black-white"));
	dpi->pi->print_black_and_white = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "check-print-titles"));
	dpi->pi->print_titles = t->active;

	t = GTK_TOGGLE_BUTTON (glade_xml_get_widget (dpi->gui, "radio-order-right"));
	dpi->pi->print_order = t->active ? PRINT_ORDER_RIGHT_THEN_DOWN : PRINT_ORDER_DOWN_THEN_RIGHT;

	entry_top  = GTK_ENTRY (glade_xml_get_widget (dpi->gui, "repeat-rows-entry"));
	entry_left = GTK_ENTRY (glade_xml_get_widget (dpi->gui, "repeat-cols-entry"));

	top_range = range_parse (NULL, gtk_entry_get_text (entry_top), TRUE);

	if (top_range){
		dpi->pi->repeat_top.range = *top_range;
		dpi->pi->repeat_top.use = TRUE;
		value_release (top_range);
	}

	left_range = range_parse (NULL, gtk_entry_get_text (entry_left), TRUE);
	if (left_range){
		dpi->pi->repeat_left.range = *left_range;
		dpi->pi->repeat_left.use = TRUE;
		value_release (left_range);
	}
}

static void
fetch_settings (dialog_print_info_t *dpi)
{
	do_fetch_page (dpi);
	do_fetch_margins (dpi);
	do_fetch_hf (dpi);
	do_fetch_page_info (dpi);
}

static void
dialog_print_info_destroy (dialog_print_info_t *dpi)
{
	gtk_object_unref (GTK_OBJECT (dpi->gui));

	print_hf_free (dpi->header);
	print_hf_free (dpi->footer);
	g_free (dpi);
}

void
dialog_printer_setup (Workbook *wb, Sheet *sheet)
{
	dialog_print_info_t *dpi;
	int v;

	dpi = dialog_print_info_new (sheet);
	if (!dpi)
		return;

	v = gnumeric_dialog_run (wb, GNOME_DIALOG (dpi->dialog));
	
	if (v == 0) {
		fetch_settings (dpi);
		print_info_save (dpi->pi);
	}

	if (v != -1)
		gnome_dialog_close (GNOME_DIALOG (dpi->dialog));
	
	dialog_print_info_destroy (dpi);
}
