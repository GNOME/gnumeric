/*
 * dialog-cell-sort.c:  Implements Cell Sort dialog boxes.
 *
 * Authors:
 *  JP Rosevear   <jpr@arcavia.com>
 *  Michael Meeks <michael@imaginator.com>
 *
 */
#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "workbook-view.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "cell.h"
#include "expr.h"
#include "selection.h"
#include "parse-util.h"
#include "utils-dialog.h"
#include "ranges.h"
#include "commands.h"
#include "workbook.h"
#include "rendered-value.h"
#include "sort.h"

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
	gboolean cs;
	gboolean val;
	GtkWidget *adv_button;
} OrderBox;

typedef struct {
	Range     *sel;
	Sheet     *sheet;
	Workbook  *wb;
	int        num_clause;
	int        max_col_clause;
	int        max_row_clause;
	OrderBox  *clauses[MAX_CLAUSE];
	GtkWidget *dialog;
	GtkWidget *clause_box;
	gboolean   header;
	gboolean   columns;
	GList     *colnames_plain;
	GList     *colnames_header;
	GList     *rownames_plain;
	GList     *rownames_header;
} SortFlow;


static gchar *
column_name (Sheet *sheet, int row, int col, gboolean header)
{
	Cell *cell;
	gchar *str = NULL;

	if (header) {
		cell = sheet_cell_get (sheet, col, row);
		if (cell)
			str = cell_get_entered_text (cell);
		else
			str = strdup (col_name (col));
	} else
		str = strdup (col_name (col));
	return str;
}

static gchar *
row_name (Sheet *sheet, int row, int col, gboolean header)
{
	Cell *cell;
	gchar *str = NULL;

	if (header) {
		cell = sheet_cell_get (sheet, col, row);
		if (cell)
			str = cell_get_entered_text (cell);
		else
			str = g_strdup_printf ("%d", row + 1);
	} else
		str = g_strdup_printf ("%d", row + 1);
	return str;
}

static GList *
column_name_list (Sheet *sheet, int start_col, int end_col,
			 int row, gboolean header)
{
	gchar *str;
	GList *list;
	int i;

	list = NULL;
	for (i = start_col; i <= end_col; i++) {
		str  = column_name (sheet, row, i, header);
		list = g_list_append (list, (gpointer) str);
	}
	return list;
}

static GList *
row_name_list (Sheet *sheet, int start_row, int end_row,
			 int col, gboolean header)
{
	gchar *str;
	GList *list;
	int i;

	list = NULL;
	for (i = start_row; i <= end_row; i++) {
		str  = row_name (sheet, i, col, header);
		list = g_list_append (list, (gpointer) str);
	}
	return list;
}

static gint
string_pos_in_list (gchar *str, GList *list)
{
	gchar *item;
	gint length;
	int i;
	
	length = g_list_length(list);
	for (i = 0; i < length; i++) {
		item = (gchar *)g_list_nth_data(list, i);
		if (!strcmp (str, item)) {
			return i;
		}
	}
	return -1;
}

static void
free_string_list (GList *list)
{
	GList *l;
	
	for (l = list; l; l = l->next)
		g_free (l->data);

	g_list_free (list);
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
	gui = glade_xml_new (GNUMERIC_GLADEDIR "/" GLADE_FILE, NULL);
	if (!gui) {
		g_warning ("Could not find " GLADE_FILE "\n");
		return;
	}

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
	btn = gnome_dialog_run (GNOME_DIALOG (dialog));
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
	       GList *names, gboolean empty)
{
	OrderBox *orderbox;
	GtkWidget *hbox = gtk_hbox_new (FALSE, 2);

	orderbox  = g_new (OrderBox, 1);
	orderbox->parent = parent;
	orderbox->main_frame = gtk_frame_new (frame_text);

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
	orderbox->adv_button = gtk_button_new_with_label(_("Advanced..."));
	gtk_box_pack_start (GTK_BOX (hbox), orderbox->adv_button, FALSE, FALSE, 0);
	gtk_signal_connect (GTK_OBJECT (orderbox->adv_button), "clicked",
		GTK_SIGNAL_FUNC (dialog_cell_sort_adv),  orderbox);

	gtk_container_add  (GTK_CONTAINER (orderbox->main_frame), hbox);
	gtk_box_pack_start (GTK_BOX (parent), GTK_WIDGET(orderbox->main_frame),
			    FALSE, TRUE, 0);

	return orderbox;
}

static char *
order_box_get_text (OrderBox *orderbox)
{
	return gtk_entry_get_text (GTK_ENTRY (
		GTK_COMBO (orderbox->rangetext)->entry));
}

static void
order_box_get_clause (OrderBox *orderbox, SortClause *clause) {
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
	SortClause *array;
	gint divstart, divend;
	int lp;

	if (sf->columns) {
		divstart = sf->sel->start.col;
		divend = sf->sel->end.col;
	} else {
		divstart = sf->sel->start.row;
		divend = sf->sel->end.row;
	}
	
	array = g_new (SortClause, sf->num_clause);
	for (lp = 0; lp < sf->num_clause; lp++) {
		int division;
		char *txt = order_box_get_text (sf->clauses [lp]);

		order_box_get_clause (sf->clauses [lp], &array [lp]);
		if (strlen (txt)) {
			division = -1;
			if (sf->columns) {
				if (sf->header) {
					division = divstart + string_pos_in_list(txt, sf->colnames_header);
				} else {
					division = col_from_name(txt);
				}
			} else {
				if (sf->header) {
					division = divstart + string_pos_in_list(txt, sf->rownames_header);
				} else {
					division = atoi(txt) - 1;
				}
			}
			if (division < divstart || division > divend) {
				gnumeric_notice (sf->wb,
						 GNOME_MESSAGE_BOX_ERROR,
						 sf->columns ?
						 _("Column must be within range") :						_("Row must be within range"));
				return TRUE;
			}
			array [lp].offset = division - divstart;
		} else if (lp <= 0) {
			gnumeric_notice (sf->wb, GNOME_MESSAGE_BOX_ERROR,
					 sf->columns ?
					 _("First column must be valid") :
					 _("First row must be valid"));
			return TRUE;
		} else	/* Just duplicate the last condition: slow but sure */
			array [lp].offset = array [lp - 1].offset;
	}

	if (sf->header) {
		if (sf->columns)
			sf->sel->start.row += 1;
		else
			sf->sel->start.col += 1;
	}
	cmd_sort (NULL, sf->sheet, sf->sel, array,
		  sf->num_clause, sf->columns);

	return FALSE;
}

static void
dialog_cell_sort_del_clause (SortFlow *sf)
{
	if (sf->num_clause > 1) {
		sf->num_clause--;
		order_box_remove  (sf->clauses [sf->num_clause]);
		order_box_destroy (sf->clauses [sf->num_clause]);
/* FIXME: bit nasty ! */
		gtk_container_queue_resize (GTK_CONTAINER (sf->dialog));
		gtk_widget_show_all (sf->dialog);
		sf->clauses [sf->num_clause] = NULL;
	} else
		gnumeric_notice (sf->wb, GNOME_MESSAGE_BOX_ERROR,
				 _("At least one clause is required."));
}

static void
dialog_cell_sort_add_clause(SortFlow *sf)
{
	if ((sf->num_clause >= sf->max_col_clause && sf->columns)
	    || (sf->num_clause >= sf->max_row_clause && !(sf->columns)))
		gnumeric_notice (sf->wb, GNOME_MESSAGE_BOX_ERROR,
				 _("Can't add more than the selection length."));
	else if (sf->num_clause >= MAX_CLAUSE)
		gnumeric_notice (sf->wb, GNOME_MESSAGE_BOX_ERROR,
				 _("Maximum number of clauses has been reached."));
	else {
		if (sf->header)
			sf->clauses [sf->num_clause] = order_box_new (sf->clause_box, "then by",
								      sf->colnames_header, TRUE);
		else
			sf->clauses [sf->num_clause] = order_box_new (sf->clause_box, "then by",
								      sf->colnames_plain, TRUE);
		
		gtk_widget_show_all (sf->dialog);
		sf->num_clause++;
	}
}

static void
dialog_cell_sort_set_clauses(SortFlow *sf, int clauses) {
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
			if (sf->columns)
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses [i]->rangetext),
					 g_list_copy (sf->colnames_header));
			else
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses [i]->rangetext),
					 g_list_copy (sf->rownames_header));
		} else {
			if (sf->columns)
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses [i]->rangetext),
					 g_list_copy (sf->colnames_plain));
			else
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses [i]->rangetext),
					 g_list_copy (sf->rownames_plain));
		}	
		if (i > 0)
			gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (sf->clauses [i]->rangetext)->entry), "");
	}
}


static void
dialog_cell_sort_rows_toggled(GtkWidget *widget, SortFlow *sf)
{
	int i;

	sf->columns = !(GTK_TOGGLE_BUTTON (widget)->active);
	if (!(sf->columns)) {
		if (sf->num_clause > sf->max_row_clause)
			dialog_cell_sort_set_clauses(sf, sf->max_row_clause);
		for (i=0; i<sf->num_clause; i++) {
			if (sf->header)
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses [i]->rangetext), sf->rownames_header);
			else
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses [i]->rangetext), sf->rownames_plain);
			if (i > 0)
				gtk_entry_set_text
					(GTK_ENTRY (GTK_COMBO (sf->clauses [i]->rangetext)->entry), "");
		}
	}
}

static void
dialog_cell_sort_cols_toggled (GtkWidget *widget, SortFlow *sf)
{
	int i;

	sf->columns = GTK_TOGGLE_BUTTON (widget)->active;
	if ((sf->columns)) {
		if (sf->num_clause > sf->max_col_clause)
			dialog_cell_sort_set_clauses (sf, sf->max_col_clause);
		for (i = 0; i < sf->num_clause; i++) {
			if (sf->header)
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses [i]->rangetext), sf->colnames_header);
			else
				gtk_combo_set_popdown_strings
					(GTK_COMBO (sf->clauses [i]->rangetext), sf->colnames_plain);
			if (i > 0)
				gtk_entry_set_text
					(GTK_ENTRY (GTK_COMBO (sf->clauses [i]->rangetext)->entry), "");
		}
	}
}

/*
 * Main entry point for the Cell Sort dialog box
 */
void
dialog_cell_sort (Workbook *inwb, Sheet *sheet)
{
	GladeXML  *gui;
	GtkWidget *table, *check, *rb1, *rb2;
	SortFlow sort_flow;
	gboolean cont;
	int lp, btn;

	g_return_if_fail (inwb != NULL);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* Initialize some important stuff */
	sort_flow.sel = range_copy (selection_first_range (sheet, TRUE));
	sort_flow.sheet = sheet;
	sort_flow.wb = inwb;

	/* We can't sort complex ranges */
	if (!selection_is_simple (workbook_command_context_gui (inwb),
				  sheet, _("sort")))
		return;	

	/* Correct selection if necessary */
	range_clip_to_finite (sort_flow.sel, sort_flow.sheet);
	
	/* Set up the dialog information */
	sort_flow.header = FALSE;
	sort_flow.columns = TRUE;
	sort_flow.colnames_plain  = column_name_list (sort_flow.sheet, 
						      sort_flow.sel->start.col,
						      sort_flow.sel->end.col,
						      sort_flow.sel->start.row,
						      FALSE);
	sort_flow.colnames_header = column_name_list (sort_flow.sheet,
						      sort_flow.sel->start.col,
						      sort_flow.sel->end.col,
						      sort_flow.sel->start.row,
						      TRUE);
	sort_flow.rownames_plain  = row_name_list (sort_flow.sheet,
						   sort_flow.sel->start.row,
						   sort_flow.sel->end.row,
						   sort_flow.sel->start.col,
						   FALSE);
	sort_flow.rownames_header = row_name_list (sort_flow.sheet, 
						   sort_flow.sel->start.row,
						   sort_flow.sel->end.row,
						   sort_flow.sel->start.col,
						   TRUE);

	/* Get the dialog and check for errors */
	gui = glade_xml_new (GNUMERIC_GLADEDIR "/" GLADE_FILE, NULL);
	if (!gui) {
		g_warning ("Could not find " GLADE_FILE "\n");
		return;
	}

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
	gnome_dialog_close_hides(GNOME_DIALOG (sort_flow.dialog), TRUE);
	
	sort_flow.clause_box = gtk_vbox_new (FALSE, FALSE);
	gtk_table_attach_defaults (GTK_TABLE (table), 
				   sort_flow.clause_box, 0, 1, 0, 1);

	for (lp = 0; lp < sort_flow.num_clause; lp++) {
		sort_flow.clauses [lp] = order_box_new (sort_flow.clause_box,
							lp 
							? _("then by") 
							: _("Sort by"),
							sort_flow.colnames_plain,
							lp ? TRUE : FALSE);
	}
	order_box_set_default (sort_flow.clauses [0]);
	
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

	gtk_widget_show_all (sort_flow.clause_box);
	
	/* Run the dialog */
	cont = TRUE;
	while (cont) {
		btn = gnumeric_dialog_run (inwb, GNOME_DIALOG (sort_flow.dialog));
		if (btn == BUTTON_OK)
			cont = dialog_cell_sort_ok (&sort_flow);
		else if (btn == BUTTON_ADD)
			dialog_cell_sort_add_clause (&sort_flow);
		else if (btn == BUTTON_REMOVE)
			dialog_cell_sort_del_clause (&sort_flow);
		else
			cont = FALSE;
	}
	
	/* Clean up */
	if (sort_flow.dialog)
		gtk_object_destroy (GTK_OBJECT (sort_flow.dialog));
	
	for (lp = 0; lp < sort_flow.num_clause; lp++)
		order_box_destroy (sort_flow.clauses [lp]);
	
	free_string_list (sort_flow.colnames_plain);
	free_string_list (sort_flow.colnames_header);
	free_string_list (sort_flow.rownames_plain);
	free_string_list (sort_flow.rownames_header);
	
	gtk_object_unref (GTK_OBJECT (gui));
}




