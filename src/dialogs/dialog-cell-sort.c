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
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "dialogs.h"
#include "cell.h"
#include "expr.h"
#include "selection.h"
#include "utils.h"
#include "utils-dialog.h"

#define MAX_CLAUSE 6

typedef struct {
	int offset;
	int asc;
} ClauseData;

typedef struct {
	Sheet      *sheet;
	ClauseData *clauses;
	Cell      **cells;
	int         num_clause;
	int         col;
	int         row;
} SortData;

typedef struct {
	GtkWidget *parent;
	GtkWidget *main_frame;
	GtkWidget *rangetext;
	int        text_cursor_pos;
	int        asc;
	GtkWidget *asc_desc;
	GSList    *group;
} OrderBox;

typedef struct {
	int        retry;
	int        force_redisplay;
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
	Workbook  *wb;
} SortFlow;

static OrderBox *
order_box_new (GtkWidget * parent, const gchar *frame_text,
	       GList *names, gboolean empty)
{
	OrderBox *orderbox;
	GtkWidget *hbox = gtk_hbox_new(FALSE, 2);

	orderbox  = g_new(OrderBox, 1);
	orderbox->parent = parent;
	orderbox->main_frame = gtk_frame_new(frame_text);
	orderbox->asc = 1;

	/* Set up the column names combo boxes */
	orderbox->rangetext = gtk_combo_new ();
	gtk_combo_set_popdown_strings (GTK_COMBO (orderbox->rangetext), names);
	if (empty)
		gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (orderbox->rangetext)->entry), "");
	gtk_box_pack_start (GTK_BOX (hbox), orderbox->rangetext, FALSE, FALSE, 0);

	{	/* Ascending / Descending buttons */
		GtkWidget *item;
		GSList *group = NULL;

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

	gtk_container_add  (GTK_CONTAINER (orderbox->main_frame), hbox);
	gtk_box_pack_start (GTK_BOX (parent), GTK_WIDGET(orderbox->main_frame),
			    FALSE, TRUE, 0);

	return orderbox;
}

static void
order_box_set_default (OrderBox *orderbox)
{
	gtk_widget_grab_focus (orderbox->rangetext);
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

static char *
order_box_get_text (OrderBox *orderbox, int *asc)
{
	*asc = gtk_radio_group_get_selected (orderbox->group);
	return gtk_entry_get_text (GTK_ENTRY (GTK_COMBO (orderbox->rangetext)->entry));
}

static int
compare_values (const SortData * ain, const SortData * bin, int clause)
{
	Cell  *ca, *cb;
	Value *a,  *b;
	int ans = 0, fans = 0;

	ca = ain->cells[ain->clauses[clause].offset];
	cb = bin->cells[bin->clauses[clause].offset];

	if (!ca)
		a = value_new_int (0);
	else
		a = ca->value;
	if (!cb)
		b = value_new_int (0);
	else
		b = cb->value;

	switch (a->type) {
	case VALUE_EMPTY:
	case VALUE_BOOLEAN:
	case VALUE_FLOAT:
	case VALUE_INTEGER:
		switch (b->type) {
		case VALUE_EMPTY:
		case VALUE_BOOLEAN:
		case VALUE_FLOAT:
		case VALUE_INTEGER:
		{
			float_t fa, fb;
			fa = value_get_as_float (a);
			fb = value_get_as_float (b);
			if (fa < fb)
				ans = -1;
			else if (fa == fb)
				ans = 0;
			else
				ans = 1;
			break;
		}
		default:
			ans = -1;
			break;
		}
		break;
	default: {
			switch (b->type) {
			case VALUE_EMPTY:
			case VALUE_BOOLEAN:
			case VALUE_FLOAT:
			case VALUE_INTEGER:
				ans = 1;
				break;
			default: {
					char *sa, *sb;
					sa  = value_get_as_string (a);
					sb  = value_get_as_string (b);
					ans = strcasecmp(sa, sb);
					g_free (sa);
					g_free (sb);
					break;
				}
			}
			break;
		}
	}

/*      fans = ans; */
	if (ans == 0)
		if (clause < ain->num_clause - 1)
			fans = compare_values(ain, bin, ++clause);
		else
			fans = ans;
	else if (ans < 0)
		fans = ain->clauses [clause].asc ?  1 : -1;
	else
		fans = ain->clauses [clause].asc ? -1 :  1;

	if (!ca)
		value_release(a);
	if (!cb)
		value_release(b);

	return fans;
}

static int
qsort_func (const void *a, const void *b)
{
	return compare_values (a, b, 0);
}

static void
sort_cell_range (Sheet * sheet, ClauseData * clauses, int num_clause,
		 int start_col, int start_row,
		 int end_col, int end_row, gboolean columns)
{
	SortData *array;
	int lp, length, divisions, lp2;

	if (columns) {
		length    = end_row - start_row + 1;
		divisions = end_col - start_col + 1;
	} else {
		length    = end_col - start_col + 1;
		divisions = end_row - start_row + 1;
	}

	array = g_new (SortData, length);

	for (lp = 0; lp < length; lp++) {
		array [lp].sheet = sheet;
		array [lp].clauses = clauses;
		array [lp].num_clause = num_clause;
		array [lp].col = start_col;
		array [lp].row = start_row + lp;
		if (columns)
			array [lp].row += lp;
		else
			array [lp].col += lp;
		array [lp].cells = g_new (Cell *, divisions);

		for (lp2 = 0; lp2 < divisions; lp2++) {
			Cell *cell;
			if (columns)
				cell = sheet_cell_get (sheet,
						       start_col + lp2, start_row + lp);
			else
				cell = sheet_cell_get (sheet,
						       start_col + lp, start_row + lp2);
			array[lp].cells[lp2] = cell;
			if (cell)
				sheet_cell_remove(sheet, cell);
		}
	}

	qsort (array, length, sizeof(SortData), qsort_func);
	{
		Cell *cell;
		for (lp = 0; lp < length; lp++) {
			for (lp2 = 0; lp2 < divisions; lp2++) {
				cell = array [lp].cells [lp2];
				if (cell) {
					if (columns)
						sheet_cell_add (sheet, cell, start_col + lp2,
								start_row + lp);
					else
						sheet_cell_add (sheet, cell, start_col + lp,
								start_row + lp2);
				}
			}
			g_free (array [lp].cells);
		}
	}
	g_free (array);
}

static gchar *
cell_sort_col_name (Sheet *sheet, int row, int col, gboolean header)
{
	Cell *cell;
	gchar *str = NULL;

	if (header) {
		cell = sheet_cell_get (sheet, col, row);
		if (cell)
			str = cell_get_text (cell);
		else
			str = strdup (col_name (col));
	} else
		str = strdup (col_name (col));
	return str;
}

static gchar *
cell_sort_row_name (Sheet *sheet, int row, int col, gboolean header)
{
	Cell *cell;
	gchar *str = NULL;

	if (header) {
		cell = sheet_cell_get (sheet, col, row);
		if (cell)
			str = cell_get_text (cell);
		else
			str = g_strdup_printf ("%d", row + 1);
	} else
		str = g_strdup_printf ("%d", row + 1);
	return str;
}

static GList *
cell_sort_col_name_list (Sheet *sheet, int start_col, int end_col,
			 int row, gboolean header)
{
	gchar *str;
	GList *list;
	int i;

	list = NULL;
	for (i = start_col; i <= end_col; i++) {
		str  = cell_sort_col_name (sheet, row, i, header);
		list = g_list_append (list, (gpointer) str);
	}
	return list;
}

static GList *
cell_sort_row_name_list (Sheet *sheet, int start_row, int end_row,
			 int col, gboolean header)
{
	gchar *str;
	GList *list;
	int i;

	list = NULL;
	for (i = start_row; i <= end_row; i++) {
		str  = cell_sort_row_name (sheet, i, col, header);
		list = g_list_append (list, (gpointer) str);
	}
	return list;
}

static void
dialog_cell_sort_del_clause (GtkWidget *widget, SortFlow *sf)
{
	if (sf->num_clause > 1) {
		sf->num_clause--;
		order_box_remove  (sf->clauses [sf->num_clause]);
		order_box_destroy (sf->clauses [sf->num_clause]);
/* Fixme: bit nasty ! */
		gtk_container_queue_resize (GTK_CONTAINER (sf->dialog));
		gtk_widget_show_all (sf->dialog);
		sf->clauses [sf->num_clause] = NULL;
	} else
		gnumeric_notice (sf->wb, GNOME_MESSAGE_BOX_ERROR,
				 _("At least one clause is required."));
}

static void
dialog_cell_sort_add_clause(GtkWidget *widget, SortFlow *sf)
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
dialog_cell_sort_set_clauses(SortFlow *sf, int clauses) {
	int i;

	if (sf->num_clause <= clauses) return;

	for (i = 0; i < (sf->num_clause - clauses); i++)
		dialog_cell_sort_del_clause (NULL, sf);
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

static void
string_list_free (GList *list)
{
	GList *l;
	
	for (l = list; l; l = l->next)
		g_free (l->data);

	g_list_free (list);
}

/*
 * Main entry point for the Cell Sort dialog box
 */
void
dialog_cell_sort (Workbook * inwb, Sheet * sheet)
{
	GladeXML  *gui;
	GtkWidget *table, *check, *rb1, *rb2;
	int btn, lp, i;
	int start_col, start_row, end_col, end_row;
	const Range *sel;
	SortFlow sort_flow;

	g_return_if_fail(inwb);
	g_return_if_fail(sheet);
	g_return_if_fail(IS_SHEET(sheet));

	/* Find out what we are sorting and if we can sort it */
	if ((sel = selection_first_range (sheet, FALSE)) == NULL) {
		gnumeric_notice (inwb, GNOME_MESSAGE_BOX_ERROR,
				 _("Selection must be a single range"));
		return;
	}

	start_row = sel->start.row;
	start_col = sel->start.col;
	end_row   = sel->end.row;
	end_col   = sel->end.col;

	if (end_row >= SHEET_MAX_ROWS - 2 ||
	    end_col >= SHEET_MAX_COLS - 2){
		gnumeric_notice (inwb, GNOME_MESSAGE_BOX_ERROR,
				 _("Selection must be a finite range"));
		return;
	}

	/* Init clauses */
	sort_flow.max_col_clause = end_col - start_col + 1;
	sort_flow.max_row_clause = end_row - start_row + 1;
	sort_flow.num_clause = sort_flow.max_col_clause > 1 ? 2 : 1;
	for (lp = 0; lp < MAX_CLAUSE; lp++)
		sort_flow.clauses[lp] = NULL;


	/* Get the dialog and check for errors */
	gui = glade_xml_new (GNUMERIC_GLADEDIR "/cell-sort.glade", NULL);
	if (!gui) {
		g_warning ("Could not find cell-sort.glade\n");
		return;
	}

	sort_flow.dialog = glade_xml_get_widget (gui, "CellSort");
	table = glade_xml_get_widget (gui, "cell_sort_table");
	check = glade_xml_get_widget (gui, "cell_sort_header_check");
	rb1   = glade_xml_get_widget (gui, "cell_sort_row_rb");
	rb2   = glade_xml_get_widget (gui, "cell_sort_col_rb");
	if (!sort_flow.dialog || !table || !check || !rb1 || !rb2) {
		g_warning ("Corrupt file cell-sort.glade\n");
		return;
	}

	/* Set up the dialog */
	sort_flow.wb = inwb;
	sort_flow.header = FALSE;
	sort_flow.columns = TRUE;
	sort_flow.colnames_plain  = cell_sort_col_name_list (sheet, start_col, end_col, start_row, FALSE);
	sort_flow.colnames_header = cell_sort_col_name_list (sheet, start_col, end_col, start_row, TRUE);
	sort_flow.rownames_plain  = cell_sort_row_name_list (sheet, start_row, end_row, start_col, FALSE);
	sort_flow.rownames_header = cell_sort_row_name_list (sheet, start_row, end_row, start_col, TRUE);
	gtk_signal_connect (GTK_OBJECT (check), "toggled",
			    GTK_SIGNAL_FUNC (dialog_cell_sort_header_toggled), &sort_flow);
	gtk_signal_connect (GTK_OBJECT (rb1), "toggled",
			    GTK_SIGNAL_FUNC (dialog_cell_sort_rows_toggled),   &sort_flow);	
	gtk_signal_connect (GTK_OBJECT (rb2), "toggled",
			    GTK_SIGNAL_FUNC (dialog_cell_sort_cols_toggled),   &sort_flow);

	gnome_dialog_set_parent (GNOME_DIALOG (sort_flow.dialog), GTK_WINDOW (sort_flow.wb->toplevel));

	sort_flow.clause_box = gtk_vbox_new (FALSE, FALSE);
	gtk_table_attach_defaults (GTK_TABLE (table), sort_flow.clause_box, 0, 1, 0, 1);

	for (lp = 0; lp < sort_flow.num_clause; lp++) {
		sort_flow.clauses [lp] = order_box_new (sort_flow.clause_box, lp ? _("then by") : _("Sort by"),
							sort_flow.colnames_plain, lp ? TRUE : FALSE);

		if (!lp)
			order_box_set_default (sort_flow.clauses [lp]);
	}
	gtk_widget_show_all (sort_flow.dialog);
	
	/* Run the dialog */
	do {			
		sort_flow.retry = 0;
		sort_flow.force_redisplay = 0;
		btn = gnome_dialog_run (GNOME_DIALOG (sort_flow.dialog)) ;
		if (btn == 0) {
			ClauseData *array;

			array = g_new (ClauseData, sort_flow.num_clause);
			for (lp = 0; lp < sort_flow.num_clause; lp++) {
				int division;
				char *txt = order_box_get_text (sort_flow.clauses [lp],
								&array [lp].asc);
				if (strlen (txt)) {
					division = -1;
					if (sort_flow.columns) {
						if (sort_flow.header) {
							for (i = 0; i < end_col - start_col + 1; i++) {
								if (!strcmp (txt, g_list_nth_data (sort_flow.colnames_header, i))) {
									division = i;
									break;
								}
							}
						} else
							division = col_from_name(txt);
						if (division < start_col || division > end_col) {
							gnumeric_notice (sort_flow.wb, GNOME_MESSAGE_BOX_ERROR,
									 _("Column must be within range"));
							sort_flow.retry = 1;
						}
						array [lp].offset = division - start_col;
					} else {
						if (sort_flow.header) {
							for (i = 0; i < end_row - start_row + 1; i++) {
								if (!strcmp (txt, g_list_nth_data (sort_flow.rownames_header, i))) {
									division = i;
									break;
								}
							}
						} else {
							division = atoi(txt) - 1;
						}
						if (division < start_row || division > end_row) {
							gnumeric_notice (sort_flow.wb, GNOME_MESSAGE_BOX_ERROR,
									 _("Row must be within range"));
							sort_flow.retry = 1;
						}
						array [lp].offset = division - start_row;
					}
				} else if (lp <= 0) {
					gnumeric_notice (sort_flow.wb, GNOME_MESSAGE_BOX_ERROR,
							 sort_flow.columns ? _("First column must be valid") : _("First row must be valid"));
					sort_flow.retry = 1;
				} else	/* Just duplicate the last condition: slow but sure */
					array [lp].offset = array [lp - 1].offset;
			}
			if (!sort_flow.retry) {
				if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check)))
					if (sort_flow.columns)
						sort_cell_range (sheet, array, sort_flow.num_clause, start_col, start_row+1,
								 end_col, end_row, sort_flow.columns);
					else
						sort_cell_range (sheet, array, sort_flow.num_clause, start_col+1, start_row,
								 end_col, end_row, sort_flow.columns);
				else
					sort_cell_range (sheet, array, sort_flow.num_clause, start_col, start_row,
							 end_col, end_row, sort_flow.columns);
			}
			g_free (array);
		} else if (btn == 1) {
			dialog_cell_sort_add_clause (NULL, &sort_flow);
			sort_flow.retry = 1;
		} else if (btn == 2) {
			dialog_cell_sort_del_clause (NULL, &sort_flow);
			sort_flow.retry = 1;
		} else  {
			sort_flow.retry = 0;
		}
	}
	while (sort_flow.retry || sort_flow.force_redisplay);

	/* Clean up */
	if (sort_flow.dialog)
		gtk_object_destroy (GTK_OBJECT (sort_flow.dialog));

	for (lp = 0; lp < sort_flow.num_clause; lp++)
		order_box_destroy (sort_flow.clauses [lp]);

	string_list_free (sort_flow.colnames_plain);
	string_list_free (sort_flow.colnames_header);
	string_list_free (sort_flow.rownames_plain);
	string_list_free (sort_flow.rownames_header);
}
