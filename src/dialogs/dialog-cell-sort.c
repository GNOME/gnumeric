/*
 * dialog-cell-sort.c:  Implements Cell Sort dialog boxes.
 *
 * Authors:
 *  JP Rosevear   <jpr@arcavia.com>
 *  Michael Meeks <michael@imaginator.com>
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <workbook-view.h>
#include <gui-util.h>
#include <cell.h>
#include <expr.h>
#include <selection.h>
#include <parse-util.h>
#include <utils-dialog.h>
#include <ranges.h>
#include <commands.h>
#include <workbook.h>
#include <sort.h>
#include <sheet.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include <gal/util/e-util.h>
#include <ctype.h>

#define GLADE_FILE "cell-sort.glade"

#define MAX_CLAUSE 6

#define BUTTON_OK      0
#define BUTTON_ADD     BUTTON_OK + 1
#define BUTTON_REMOVE  BUTTON_ADD + 1
#define BUTTON_CANCEL  BUTTON_REMOVE + 1

typedef struct {
	GtkWidget *parent;
	GtkWidget *main_frame;
	GtkWidget *rangetext;
	int        text_cursor_pos;
	int        asc;
	GtkWidget *asc_desc;
	GSList    *group;
	gboolean   cs;
	gboolean   val;
	GtkWidget *adv_button;
	WorkbookControlGUI  *wbcg;
} OrderBox;

typedef struct {
	Range     *sel;
	Sheet     *sheet;
	WorkbookControlGUI  *wbcg;
	int        num_clause;
	int        max_col_clause;
	int        max_row_clause;
	OrderBox  *clauses[MAX_CLAUSE];
	GtkWidget *dialog;
	GtkWidget *clause_box;
	gboolean   header;
	gboolean   top;
	GList     *colnames_plain;
	GList     *colnames_header;
	GList     *rownames_plain;
	GList     *rownames_header;
} SortFlow;


static gchar *
col_row_name (Sheet *sheet, int col, int row, gboolean header, gboolean is_cols)
{
	Cell *cell;
	gchar *str = NULL;

	if (header) {
		cell = sheet_cell_get (sheet, col, row);
		if (cell)
			str = cell_get_rendered_text (cell);
		else if (is_cols)
			str = g_strdup (col_name (col));
		else
			str = g_strdup_printf (_("Row %s"), row_name (row));
	} else {
		if (is_cols)
			str = g_strdup (col_name (col));
		else
			str = g_strdup_printf (_("Row %s"), row_name (row));
	}

	return str;
}

static GList *
col_row_name_list (Sheet *sheet, int start, int end,
		   int index, gboolean header, gboolean is_cols)
{
	GList *list = NULL;
	gchar *str;
	int i;

	for (i = start; i <= end; i++) {
		str  = is_cols
			? col_row_name (sheet, i, index, header, TRUE)
			: col_row_name (sheet, index, i, header, FALSE);
		list = g_list_append (list, str);
	}

	return list;
}

static gint
string_pos_in_list (const char *str, GList *list)
{
	GList *l;
	int i = 0;

	for (l = list; l; l = l->next) {
		if (!strcmp (str, (char *) l->data))
			return i;
		i++;
	}

	return -1;
}

/* Advanced dialog */
static void
dialog_cell_sort_adv (GtkWidget *widget, OrderBox *orderbox)
{
	GladeXML *gui;
	GtkWidget *check;
	GtkWidget *rb1, *rb2;
	GtkWidget *dialog;
	gint btn;

	/* Get the dialog and check for errors */
	gui = gnumeric_glade_xml_new (orderbox->wbcg, GLADE_FILE);
        if (gui == NULL)
                return;

	dialog = glade_xml_get_widget (gui, "CellSortAdvanced");
	check  = glade_xml_get_widget (gui, "cell_sort_adv_case");
	rb1    = glade_xml_get_widget (gui, "cell_sort_adv_value");
	rb2    = glade_xml_get_widget (gui, "cell_sort_adv_text");
	if (!dialog || !check || !rb1 || !rb2) {
		g_warning ("Corrupt file " GLADE_FILE "\n");
		gtk_object_unref (GTK_OBJECT (gui));
		return;
	}

	/* Set the button states */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
				      orderbox->cs);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rb1),
				      orderbox->val);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rb2),
				      !(orderbox->val));

	/* Run the dialog and save the state if necessary */
	btn = gnumeric_dialog_run (orderbox->wbcg, GNOME_DIALOG (dialog));
	if (btn == 0) {
		orderbox->cs  = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check));
		orderbox->val = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rb1));
	}

	if (btn != -1)
		gtk_object_destroy (GTK_OBJECT (dialog));
	gtk_object_unref (GTK_OBJECT (gui));
}

/* Order boxes */
static OrderBox *
order_box_new (GtkWidget * parent, const gchar *frame_text,
	       GList *names, gboolean empty, WorkbookControlGUI *wbcg)
{
	OrderBox *orderbox;
	GtkWidget *hbox = gtk_hbox_new (FALSE, 2);

	gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);

	orderbox  = g_new (OrderBox, 1);
	orderbox->parent = parent;
	orderbox->main_frame = gtk_frame_new (frame_text);
	orderbox->wbcg = wbcg;

	/* Set up the column names combo boxes */
	orderbox->rangetext = gtk_combo_new ();
	gtk_combo_set_popdown_strings (GTK_COMBO (orderbox->rangetext), names);
	if (empty)
		gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (orderbox->rangetext)->entry), "");
	gtk_box_pack_start (GTK_BOX (hbox), orderbox->rangetext, FALSE, FALSE, 0);

	{	/* Ascending / Descending buttons */
		GtkWidget *item;
		GSList *group = NULL;

		orderbox->asc = 1;
		orderbox->asc_desc = gtk_hbox_new (FALSE, 0);

		item = gtk_radio_button_new_with_label (group, _("Ascending"));
		gtk_box_pack_start (GTK_BOX (orderbox->asc_desc), item, FALSE, FALSE, 0);
		group = gtk_radio_button_group (GTK_RADIO_BUTTON (item));
		item = gtk_radio_button_new_with_label (group, _("Descending"));
		gtk_box_pack_start (GTK_BOX (orderbox->asc_desc), item, FALSE, FALSE, 0);
		group = gtk_radio_button_group (GTK_RADIO_BUTTON (item));
		orderbox->group = group;

		gtk_box_pack_start (GTK_BOX (hbox), orderbox->asc_desc, FALSE, FALSE, 0);
	}

	/* Advanced button */
	orderbox->cs = FALSE;
	orderbox->val = TRUE;

	{
		GtkWidget *pm = gnome_stock_new_with_icon (GNOME_STOCK_PIXMAP_PROPERTIES);
		orderbox->adv_button = gnome_pixmap_button (pm, _("Advanced..."));
	}

	gtk_box_pack_start (GTK_BOX (hbox), orderbox->adv_button,
			    FALSE, FALSE, 0);
	gtk_signal_connect (GTK_OBJECT (orderbox->adv_button), "clicked",
		GTK_SIGNAL_FUNC (dialog_cell_sort_adv),  orderbox);

	gtk_container_add  (GTK_CONTAINER (orderbox->main_frame), hbox);
	gtk_box_pack_start (GTK_BOX (parent), GTK_WIDGET (orderbox->main_frame),
			    FALSE, TRUE, 0);

	return orderbox;
}

static char const *
order_box_get_text (OrderBox *orderbox)
{
	return gtk_entry_get_text (GTK_ENTRY (
		GTK_COMBO (orderbox->rangetext)->entry));
}

static void
order_box_get_clause (OrderBox *orderbox, SortClause *clause)
{
	clause->asc = gtk_radio_group_get_selected (orderbox->group);
	clause->cs  = orderbox->cs;
	clause->val = orderbox->val;
}

static void
order_box_set_default (OrderBox *orderbox)
{
	gtk_widget_grab_focus (GTK_COMBO (orderbox->rangetext)->entry);
}

static void
order_box_remove (OrderBox *orderbox)
{
	g_return_if_fail (orderbox != NULL);
	if (orderbox->main_frame)
		gtk_container_remove (GTK_CONTAINER (orderbox->parent),
				      orderbox->main_frame);
	orderbox->main_frame = NULL;
}

static void
order_box_destroy (OrderBox *orderbox)
{
	g_return_if_fail (orderbox != NULL);
	g_free (orderbox);
}

/* The cell sort dialog and its callbacks */
static gboolean
dialog_cell_sort_ok (SortFlow *sf)
{
	SortData *data;
	SortClause *array;
	gint divstart, divend;
	int lp;

	if (sf->top) {
		divstart = sf->sel->start.col;
		divend = sf->sel->end.col;
	} else {
		divstart = sf->sel->start.row;
		divend = sf->sel->end.row;
	}

	array = g_new (SortClause, sf->num_clause);
	for (lp = 0; lp < sf->num_clause; lp++) {
		int division;
		const char *txt = order_box_get_text (sf->clauses[lp]);

		order_box_get_clause (sf->clauses[lp], &array[lp]);
		if (strlen (txt)) {
			division = -1;
			if (sf->top) {
				if (sf->header)
					division = divstart + string_pos_in_list (
						txt, sf->colnames_header);
				else
					division = parse_col_name (txt, NULL);
			} else {
				if (sf->header)
					division = divstart + string_pos_in_list (
						txt, sf->rownames_header);
				else {
					/*
					 * FIXME: this is a bit of a hack.  We need
					 * to skip "Row ".
					 */
					while (*txt && !isdigit ((unsigned char)*txt))
						txt++;
					division = atoi (txt) - 1;
				}
			}
			if (division < divstart || division > divend) {
				gnumeric_notice (sf->wbcg,
						 GNOME_MESSAGE_BOX_ERROR,
						 sf->top
						 ? _("Column must be within range")
						 : _("Row must be within range"));
				g_free (array);
				return TRUE;
			}
			array[lp].offset = division - divstart;
		} else if (lp <= 0) {
			gnumeric_notice (sf->wbcg, GNOME_MESSAGE_BOX_ERROR,
					 sf->top
					 ? _("First column must be valid")
					 : _("First row must be valid"));
			g_free (array);
			return TRUE;
		} else	/* Just duplicate the last condition: slow but sure */
			array[lp].offset = array[lp - 1].offset;
	}

	if (sf->header) {
		if (sf->top)
			sf->sel->start.row += 1;
		else
			sf->sel->start.col += 1;
	}

	data = g_new (SortData, 1);
	data->sheet = sf->sheet;
	data->range = range_dup (sf->sel);
	data->num_clause = sf->num_clause;
	data->clauses = array;
	data->top = sf->top;

	cmd_sort (WORKBOOK_CONTROL (sf->wbcg), data);

	return FALSE;
}

static void
dialog_cell_sort_del_clause (SortFlow *sf)
{
	if (sf->num_clause > 1) {
		sf->num_clause--;
		order_box_remove  (sf->clauses[sf->num_clause]);
		order_box_destroy (sf->clauses[sf->num_clause]);
/* FIXME: bit nasty ! */
		gtk_container_queue_resize (GTK_CONTAINER (sf->dialog));
		gtk_widget_show_all (sf->dialog);
		sf->clauses[sf->num_clause] = NULL;
	} else
		gnumeric_notice (sf->wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("At least one clause is required."));
}

static void
dialog_cell_sort_add_clause (SortFlow *sf, WorkbookControlGUI *wbcg)
{
	if ((sf->num_clause >= sf->max_col_clause && sf->top)
	    || (sf->num_clause >= sf->max_row_clause && !(sf->top)))
		gnumeric_notice (sf->wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Can't add more than the selection length."));
	else if (sf->num_clause >= MAX_CLAUSE)
		gnumeric_notice (sf->wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Maximum number of clauses has been reached."));
	else {
		if (sf->header)
			sf->clauses[sf->num_clause] = order_box_new (sf->clause_box, _("then by"),
								      sf->colnames_header, TRUE, wbcg);
		else
			sf->clauses[sf->num_clause] = order_box_new (sf->clause_box, _("then by"),
								      sf->colnames_plain, TRUE, wbcg);

		gtk_widget_show_all (sf->dialog);
		sf->num_clause++;
	}
}

static void
dialog_cell_sort_set_clauses (SortFlow *sf, int clauses) {
	int i;

	if (sf->num_clause <= clauses) return;

	for (i = 0; i < (sf->num_clause - clauses); i++)
		dialog_cell_sort_del_clause (sf);
}

static void
dialog_cell_sort_header_toggled (GtkWidget *widget, SortFlow *sf)
{
	int i;

	sf->header = GTK_TOGGLE_BUTTON (widget)->active;
	for (i = 0; i < sf->num_clause; i++) {
		if (sf->header) {
			if (sf->top)
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses[i]->rangetext),
					 sf->colnames_header);
			else
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses[i]->rangetext),
					 sf->rownames_header);
		} else {
			if (sf->top)
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses[i]->rangetext),
					 sf->colnames_plain);
			else
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses[i]->rangetext),
					 sf->rownames_plain);
		}
		if (i > 0)
			gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (
				sf->clauses[i]->rangetext)->entry), "");
	}
}


static void
dialog_cell_sort_rows_toggled (GtkWidget *widget, SortFlow *sf)
{
	int i;

	sf->top = !(GTK_TOGGLE_BUTTON (widget)->active);
	if (!(sf->top)) {
		if (sf->num_clause > sf->max_row_clause)
			dialog_cell_sort_set_clauses (sf, sf->max_row_clause);
		for (i=0; i<sf->num_clause; i++) {
			if (sf->header)
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses[i]->rangetext), sf->rownames_header);
			else
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses[i]->rangetext), sf->rownames_plain);
			if (i > 0)
				gtk_entry_set_text
					(GTK_ENTRY (GTK_COMBO (sf->clauses[i]->rangetext)->entry), "");
		}
	}
}

static void
dialog_cell_sort_cols_toggled (GtkWidget *widget, SortFlow *sf)
{
	int i;

	sf->top = GTK_TOGGLE_BUTTON (widget)->active;
	if ((sf->top)) {
		if (sf->num_clause > sf->max_col_clause)
			dialog_cell_sort_set_clauses (sf, sf->max_col_clause);
		for (i = 0; i < sf->num_clause; i++) {
			if (sf->header)
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses[i]->rangetext), sf->colnames_header);
			else
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses[i]->rangetext), sf->colnames_plain);
			if (i > 0)
				gtk_entry_set_text
					(GTK_ENTRY (GTK_COMBO (sf->clauses[i]->rangetext)->entry), "");
		}
	}
}

/*
 * Main entry point for the Cell Sort dialog box
 */
void
dialog_cell_sort (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	GladeXML  *gui;
	GtkWidget *table, *check, *rb1, *rb2;
	SortFlow sort_flow;
	gboolean cont;
	int lp, btn;
	Range const *sel;

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	if (!(sel = selection_first_range (sheet, WORKBOOK_CONTROL (wbcg), _("Sort"))))
		return;

	/* Initialize some important stuff */
	sort_flow.sel = range_dup (sel);
	sort_flow.sheet = sheet;
	sort_flow.wbcg = wbcg;

	/* Correct selection if necessary */
	range_clip_to_finite (sort_flow.sel, sort_flow.sheet);

	/* Set up the dialog information */
	sort_flow.header = FALSE;
	sort_flow.top = TRUE;
	sort_flow.colnames_plain  = col_row_name_list (sort_flow.sheet,
						       sort_flow.sel->start.col,
						       sort_flow.sel->end.col,
						       sort_flow.sel->start.row,
						       FALSE, TRUE);
	sort_flow.colnames_header = col_row_name_list (sort_flow.sheet,
						       sort_flow.sel->start.col,
						       sort_flow.sel->end.col,
						       sort_flow.sel->start.row,
						       TRUE, TRUE);
	sort_flow.rownames_plain  = col_row_name_list (sort_flow.sheet,
						       sort_flow.sel->start.row,
						       sort_flow.sel->end.row,
						       sort_flow.sel->start.col,
						       FALSE, FALSE);
	sort_flow.rownames_header = col_row_name_list (sort_flow.sheet,
						       sort_flow.sel->start.row,
						       sort_flow.sel->end.row,
						       sort_flow.sel->start.col,
						       TRUE, FALSE);

	/* Get the dialog and check for errors */
	gui = gnumeric_glade_xml_new (wbcg, GLADE_FILE);
        if (gui == NULL)
                return;

	sort_flow.dialog = glade_xml_get_widget (gui, "CellSort");
	table = glade_xml_get_widget (gui, "cell_sort_table");
	check = glade_xml_get_widget (gui, "cell_sort_header_check");
	rb1   = glade_xml_get_widget (gui, "cell_sort_row_rb");
	rb2   = glade_xml_get_widget (gui, "cell_sort_col_rb");
	if (!sort_flow.dialog || !table || !check || !rb1 || !rb2) {
		g_warning ("Corrupt file cell-sort.glade\n");
		gtk_object_unref (GTK_OBJECT (gui));
		return;
	}

	/* Init clauses */
	sort_flow.max_col_clause = sort_flow.sel->end.col
		- sort_flow.sel->start.col + 1;
	sort_flow.max_row_clause = sort_flow.sel->end.row
		- sort_flow.sel->start.row + 1;
	sort_flow.num_clause = sort_flow.max_col_clause > 1 ? 2 : 1;
	for (lp = 0; lp < MAX_CLAUSE; lp++)
		sort_flow.clauses[lp] = NULL;

	/* Build the rest of the dialog */
	gnome_dialog_close_hides (GNOME_DIALOG (sort_flow.dialog), TRUE);

	sort_flow.clause_box = gtk_vbox_new (FALSE, FALSE);
	gtk_table_attach_defaults (GTK_TABLE (table),
				   sort_flow.clause_box, 0, 1, 0, 1);

	for (lp = 0; lp < sort_flow.num_clause; lp++) {
		sort_flow.clauses[lp] = order_box_new (sort_flow.clause_box,
							lp
							? _("then by")
							: _("Sort by"),
							sort_flow.colnames_plain,
							lp ? TRUE : FALSE, wbcg);
	}
	order_box_set_default (sort_flow.clauses[0]);

	/* Hook up the signals */
	gtk_signal_connect (GTK_OBJECT (check), "toggled",
			    GTK_SIGNAL_FUNC (dialog_cell_sort_header_toggled),
			    &sort_flow);
	gtk_signal_connect (GTK_OBJECT (rb1),   "toggled",
			    GTK_SIGNAL_FUNC (dialog_cell_sort_rows_toggled),
			    &sort_flow);
	gtk_signal_connect (GTK_OBJECT (rb2),   "toggled",
			    GTK_SIGNAL_FUNC (dialog_cell_sort_cols_toggled),
			    &sort_flow);

	/* Set the header button and drop down boxes correctly */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
		range_has_header (sort_flow.sheet, sort_flow.sel, TRUE, FALSE));

	gtk_widget_show_all (sort_flow.clause_box);

	/* Run the dialog */
	cont = TRUE;
	while (cont) {
		btn = gnumeric_dialog_run (wbcg, GNOME_DIALOG (sort_flow.dialog));
		if (btn == BUTTON_OK)
			cont = dialog_cell_sort_ok (&sort_flow);
		else if (btn == BUTTON_ADD)
			dialog_cell_sort_add_clause (&sort_flow, wbcg);
		else if (btn == BUTTON_REMOVE)
			dialog_cell_sort_del_clause (&sort_flow);
		else
			cont = FALSE;
	}

	/* Clean up */
	g_free (sort_flow.sel);

	if (sort_flow.dialog)
		gtk_object_destroy (GTK_OBJECT (sort_flow.dialog));

	for (lp = 0; lp < sort_flow.num_clause; lp++)
		order_box_destroy (sort_flow.clauses[lp]);

	e_free_string_list (sort_flow.colnames_plain);
	e_free_string_list (sort_flow.colnames_header);
	e_free_string_list (sort_flow.rownames_plain);
	e_free_string_list (sort_flow.rownames_header);

	gtk_object_unref (GTK_OBJECT (gui));
}
