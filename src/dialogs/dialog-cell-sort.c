/*
 * dialog-cell-sort.c:  Implements Cell Sort dialog boxes.
 *
 * Author:
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

typedef struct {
	GtkWidget *parent;
	GtkWidget *main_frame;
	GtkWidget *rangetext;
	int text_cursor_pos;
	int asc;
	GtkWidget *asc_desc;
	GSList *group;
} ORDER_BOX;

static ORDER_BOX *
order_box_new(GtkWidget * parent, const gchar *frame_text, GList *colnames, gboolean empty)
{
	ORDER_BOX *This = g_new(ORDER_BOX, 1);
	GtkWidget *hbox = gtk_hbox_new(FALSE, 2);

	This->parent = parent;
	This->main_frame = gtk_frame_new(frame_text);
	This->asc = 1;
	{
		This->rangetext = gtk_combo_new();
		gtk_combo_set_popdown_strings(GTK_COMBO(This->rangetext), colnames);
		if (empty)
			gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(This->rangetext)->entry), "");
		gtk_box_pack_start(GTK_BOX(hbox), This->rangetext, FALSE, FALSE,  0);
	}

	{			/* Asc / Desc buttons */
		GtkWidget *item;
		GSList *group = NULL;

		This->asc_desc = gtk_hbox_new(FALSE, 0);

		item = gtk_radio_button_new_with_label(group, _("Ascending"));
		gtk_box_pack_start(GTK_BOX(This->asc_desc), item, FALSE, FALSE,  0);
		group = gtk_radio_button_group(GTK_RADIO_BUTTON(item));
		item = gtk_radio_button_new_with_label(group, _("Descending"));
		gtk_box_pack_start(GTK_BOX(This->asc_desc), item, FALSE, FALSE,  0);
		group = gtk_radio_button_group(GTK_RADIO_BUTTON(item));
		This->group = group;

		gtk_box_pack_start(GTK_BOX(hbox), This->asc_desc, FALSE, FALSE,  0);
	}	

	gtk_container_add(GTK_CONTAINER(This->main_frame), hbox);
	gtk_box_pack_start(GTK_BOX(parent), GTK_WIDGET(This->main_frame), FALSE, TRUE, 0);
	return This;
}

static void
order_box_set_default (ORDER_BOX * This)
{
	gtk_widget_grab_focus(This->rangetext);
}

static void
order_box_remove (ORDER_BOX * This)
{
	g_return_if_fail(This);
	if (This->main_frame)
		gtk_container_remove(GTK_CONTAINER(This->parent), This->main_frame);
	This->main_frame = NULL;
}

static void
order_box_destroy (ORDER_BOX * This)
{
	g_return_if_fail(This);
	g_free(This);
}

static char *
order_box_get_text (ORDER_BOX * This, int *asc)
{
	*asc = gtk_radio_group_get_selected(This->group);
	return gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(This->rangetext)->entry));
}

typedef struct {
	int col_offset;
	int asc;
} ClauseData;

typedef struct {
	Sheet *sheet;
	ClauseData *clauses;
	Cell **cells;
	int num_clause;
	int col;
	int row;
} SortData;

static int
compare_values (const SortData * ain, const SortData * bin, int clause)
{
	Cell *ca, *cb;
	Value *a, *b;
	int ans = 0, fans = 0;

	ca = ain->cells[ain->clauses[clause].col_offset];
	cb = bin->cells[bin->clauses[clause].col_offset];

	if (!ca)
		a = value_new_int (0);
	else
		a = ca->value;
	if (!cb)
		b = value_new_int (0);
	else
		b = cb->value;

	switch (a->type){
	case VALUE_EMPTY:
	case VALUE_BOOLEAN:
	case VALUE_FLOAT:
	case VALUE_INTEGER:
		switch (b->type){
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
	default:{
			switch (b->type){
			case VALUE_EMPTY:
			case VALUE_BOOLEAN:
			case VALUE_FLOAT:
			case VALUE_INTEGER:
				ans = 1;
				break;
			default:{
					char *sa, *sb;
					sa = value_get_as_string (a);
					sb = value_get_as_string (b);
					ans = strcasecmp(sa, sb);
					g_free(sa);
					g_free(sb);
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
		fans = ain->clauses[clause].asc ? 1 : -1;
	else
		fans = ain->clauses[clause].asc ? -1 : 1;

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
sort_cell_range (Sheet * sheet, ClauseData * clauses, int num_clause, int start_col, int start_row,
		 int end_col, int end_row)
{
	SortData *array;
	int lp, height, width, lp2;

	height = end_row - start_row + 1;
	width = end_col - start_col + 1;
	array = g_new(SortData, height);

	for (lp = 0; lp < height; lp++){
		array [lp].sheet = sheet;
		array [lp].clauses = clauses;
		array [lp].num_clause = num_clause;
		array [lp].col = start_col;
		array [lp].row = start_row + lp;
		array [lp].cells = g_new(Cell *, width);

		for (lp2 = 0; lp2 < width; lp2++){
			Cell *cell;
			cell = sheet_cell_get(sheet,
					start_col + lp2, start_row + lp);
			array[lp].cells[lp2] = cell;
			if (cell)
				sheet_cell_remove(sheet, cell);
		}
	}

	qsort(array, height, sizeof(SortData), qsort_func);
/*             (int *(const void *, const void *))qsort_func); */
	{
		Cell *cell;
		for (lp = 0; lp < height; lp++){
			for (lp2 = 0; lp2 < width; lp2++){
				cell = array[lp].cells[lp2];
/*                              printf ("%s\n", cell?value_get_as_string (cell->value):"Null"); */
				if (cell)
					sheet_cell_add(sheet, cell, start_col + lp2, start_row + lp);
			}
			g_free(array[lp].cells);
		}
	}
	g_free(array);
}

#define MAX_CLAUSE 6
typedef struct {
	int retry;
	int force_redisplay;
	int num_clause;
	int max_clause;
	ORDER_BOX *clauses[MAX_CLAUSE];
	GtkWidget *dialog;
	GtkWidget *clause_box;
	gboolean header;
	GList *colnames_plain;
	GList *colnames_header;
	Workbook *wb;
} SORT_FLOW;

static gchar *cell_sort_name (Sheet *sheet, int col, int row, gboolean header)
{
	Cell *cell;
	gchar *str = NULL;

	if (header) {
		cell = sheet_cell_get(sheet, col, row);
		if (cell)
			str = cell_get_text(cell);
		else
			str = strdup(col_name(col));
	} else {
		str = strdup(col_name(col));
	}
	return str;
}

static GList *col_name_list (Sheet *sheet, int start_col, int end_col, int row, gboolean header)
{
	gchar *str;
	GList *list;
	int i;

	list = NULL;
	for (i=start_col; i<end_col-start_col+1; i++) {
		str = cell_sort_name(sheet, i, row, header);
		list = g_list_append(list, (gpointer) str);
	}
	return list;
}

static GList *row_name_list (Sheet *sheet, int start_row, int end_row, int col, gboolean header)
{
	gchar *str;
	GList *list;
	int i;

	list = NULL;
	for (i=start_row; i<end_row-start_row+1; i++) {
		str = cell_sort_name(sheet, i, col, header);
		list = g_list_append(list, (gpointer) str);
	}
	return list;
}

static void
del_clause (GtkWidget * widget, SORT_FLOW * sf)
{
	if (sf->num_clause > 1){
		sf->num_clause--;
		order_box_remove(sf->clauses[sf->num_clause]);
		order_box_destroy(sf->clauses[sf->num_clause]);
/* Fixme: bit nasty ! */
		gtk_container_queue_resize(GTK_CONTAINER(sf->dialog));
		gtk_widget_show_all(sf->dialog);
		sf->clauses[sf->num_clause] = NULL;
	} else
		gnumeric_notice(sf->wb, GNOME_MESSAGE_BOX_ERROR,
				_("At least one clause is required."));
}

static void add_clause(GtkWidget * widget, SORT_FLOW * sf)
{
	if (sf->num_clause >= sf->max_clause)
		gnumeric_notice(sf->wb, GNOME_MESSAGE_BOX_ERROR,
				_("Can't add more than the selection length."));
	else if (sf->num_clause >= MAX_CLAUSE)
		gnumeric_notice(sf->wb, GNOME_MESSAGE_BOX_ERROR,
				_("Maximum number of clauses has been reached."));
	else {
		if (sf->header)
			sf->clauses[sf->num_clause] = order_box_new(sf->clause_box, "then by", sf->colnames_header, TRUE );
		else
			sf->clauses[sf->num_clause] = order_box_new(sf->clause_box, "then by", sf->colnames_plain, TRUE );

		gtk_widget_show_all(sf->dialog);
		sf->num_clause++;
	}
}

static void header_toggled(GtkWidget *widget, SORT_FLOW *sf)
{
	int i;

	sf->header = GTK_TOGGLE_BUTTON (widget)->active;
	for (i=0; i<sf->num_clause; i++) {
		if (sf->header) {
			gtk_combo_set_popdown_strings(GTK_COMBO(sf->clauses[i]->rangetext), sf->colnames_header);
		} else {
			gtk_combo_set_popdown_strings(GTK_COMBO(sf->clauses[i]->rangetext), sf->colnames_plain);
		}
		if (i > 0)
			gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(sf->clauses[i]->rangetext)->entry), "");
	}

}

/*
 * Main entry point for the Cell Sort dialog box
 */
void
dialog_cell_sort (Workbook * inwb, Sheet * sheet)
{
	GladeXML  *gui = glade_xml_new (GNUMERIC_GLADEDIR "/cell-sort.glade", NULL);
	GtkWidget *table, *check;
	int btn, lp, i;
	int start_col, start_row, end_col, end_row;
	Range const * sel;
	SORT_FLOW sort_flow;

	g_return_if_fail(inwb);
	g_return_if_fail(sheet);
	g_return_if_fail(IS_SHEET(sheet));

	if (!gui) {
		printf ("Could not find cell-sort.glade\n");
		return;
	}
	
	sort_flow.dialog = glade_xml_get_widget (gui, "CellSort");
	table = glade_xml_get_widget (gui, "cell_sort_table");
	check = glade_xml_get_widget (gui, "cell_sort_header_check");
	if (!sort_flow.dialog && !table && !check) {
		printf ("Corrupt file cell-sort.glade\n");
		return;
	}

	if ((sel = selection_first_range (sheet, FALSE)) == NULL) {
		gnumeric_notice(inwb, GNOME_MESSAGE_BOX_ERROR,
				_("Selection must be a single range"));
		return;
	}

	start_row = sel->start.row;
	start_col = sel->start.col;
	end_row = sel->end.row;
	end_col = sel->end.col;

	if (end_row >= SHEET_MAX_ROWS - 2 ||
	    end_col >= SHEET_MAX_COLS - 2){
		gnumeric_notice(inwb, GNOME_MESSAGE_BOX_ERROR,
				_("Selection must be a finite range"));
		return;
	} {			/* Init clauses */
		sort_flow.max_clause = end_col - start_col + 1;
		sort_flow.num_clause = sort_flow.max_clause > 1 ? 2 : 1;
		for (lp = 0; lp < MAX_CLAUSE; lp++)
			sort_flow.clauses[lp] = NULL;
	}

	{			/* Setup the dialog */
		sort_flow.wb = inwb;
		sort_flow.header = FALSE;
		sort_flow.colnames_plain = col_name_list(sheet, start_col, end_col, start_row, FALSE);
		sort_flow.colnames_header = col_name_list(sheet, start_col, end_col, start_row, TRUE);
		gtk_signal_connect(GTK_OBJECT(check), "toggled",
			GTK_SIGNAL_FUNC(header_toggled), &sort_flow);	

		gnome_dialog_set_parent(GNOME_DIALOG(sort_flow.dialog), GTK_WINDOW(sort_flow.wb->toplevel));

		sort_flow.clause_box = gtk_vbox_new(FALSE, FALSE);
		gtk_table_attach_defaults(GTK_TABLE(table), sort_flow.clause_box, 0, 1, 0, 1);

		for (lp = 0; lp < sort_flow.num_clause; lp++){
			sort_flow.clauses[lp] = order_box_new(sort_flow.clause_box, lp ? _("then by") : _("Sort by"),
											  sort_flow.colnames_plain, lp ? TRUE : FALSE);

			if (!lp)
				order_box_set_default(sort_flow.clauses[lp]);
		}

		gtk_widget_show_all(sort_flow.dialog);
	}

	do {			/* Run the dialog */
		sort_flow.retry = 0;
		sort_flow.force_redisplay = 0;
		btn = gnome_dialog_run(GNOME_DIALOG(sort_flow.dialog)) ;
		if (btn == 0) {
			ClauseData *array;

			array = g_new(ClauseData, sort_flow.num_clause);
			for (lp = 0; lp < sort_flow.num_clause; lp++){
				int col;
				char *txt = order_box_get_text(sort_flow.clauses[lp],
							 &array[lp].asc);
				if (strlen(txt)){
					if (sort_flow.header) {
						col = -1;
						for (i=0; i<end_col-start_col+1; i++) {
							if (!strcmp(txt, g_list_nth_data(sort_flow.colnames_header, i))) {
								col = i; break;
							}
						}
					} else {
						col = col_from_name(txt);
					}
					if (col < start_col || col > end_col){
						gnumeric_notice(sort_flow.wb, GNOME_MESSAGE_BOX_ERROR,
								_("Column must be within range"));
						sort_flow.retry = 1;
					}
					array[lp].col_offset = col - start_col;
				} else if (lp <= 0){
					gnumeric_notice(sort_flow.wb, GNOME_MESSAGE_BOX_ERROR,
					_("First column must be valid"));
					sort_flow.retry = 1;
				} else	/* Just duplicate the last condition: slow but sure */
					array[lp].col_offset = array[lp - 1].col_offset;
			}
			if (!sort_flow.retry) {
				if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)))
					sort_cell_range(sheet, array, sort_flow.num_clause, start_col, start_row+1,
							end_col, end_row);
				else
					sort_cell_range(sheet, array, sort_flow.num_clause, start_col, start_row,
							end_col, end_row);
			}
			g_free (array);
		} else if (btn == 1) {
			add_clause (NULL, &sort_flow);
			sort_flow.retry = 1;
		} else if (btn == 2) {
			del_clause (NULL, &sort_flow);
			sort_flow.retry = 1;
		} else  {
			sort_flow.retry = 0;
		}
	}
	while (sort_flow.retry || sort_flow.force_redisplay);

	if (sort_flow.dialog)
		gtk_object_destroy (GTK_OBJECT(sort_flow.dialog));

	for (lp = 0; lp < sort_flow.num_clause; lp++)
		order_box_destroy (sort_flow.clauses[lp]);

	g_list_free(sort_flow.colnames_plain);
	g_list_free(sort_flow.colnames_header);
}
