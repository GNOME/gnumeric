/*
 * dialog-cell-sort.c:  Implements Cell Sort dialog boxes.
 *
 * Author:
 *  Michael Meeks <michael@imaginator.com>
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "dialogs.h"
#include "cell.h"
#include "expr.h"

typedef struct {
	GtkWidget *main_frame ;
	GtkWidget *rangetext ;
	int text_cursor_pos ;
	int asc ;
	GtkWidget *asc_desc ;
	GSList *group ;
} ORDER_BOX ;

static ORDER_BOX *
order_box_new (GtkWidget *parent, char *frame_text, char *default_text)
{
	ORDER_BOX *this = g_new (ORDER_BOX,1);
	GtkWidget *box = gtk_hbox_new(0,0);

	this->main_frame = gtk_frame_new (frame_text);
	this->asc = 1 ;
	{
		GtkTable *tt ;
		tt = GTK_TABLE (gtk_table_new (0, 0, 0));
		gtk_table_attach (tt, gtk_label_new (_("Column:")), 0, 1, 0, 1, 0, 0, 2, 0);
		this->rangetext = gtk_entry_new_with_max_length (5);
		gtk_entry_set_text (GTK_ENTRY(this->rangetext), default_text);
		gtk_table_attach (tt, this->rangetext, 1, 2, 0, 1, 0, 0, 0, 2);
/*		gtk_table_attach (tt, this->rangetext, 1, 2, 0, 1, GTK_FILL | GTK_EXPAND, 0, 0, 2); */
		gtk_box_pack_start (GTK_BOX(box), GTK_WIDGET(tt), TRUE, FALSE, 2); 
/*		gtk_box_pack_start (GTK_BOX(box), GTK_WIDGET(this->rangetext), TRUE, FALSE, 2); */
	}

	{ /* Asc / Desc buttons */
		GtkWidget *item ;
		GSList *group = NULL ;
		
		this->asc_desc = gtk_vbox_new (0, 0);

		item = gtk_radio_button_new_with_label (group, _("Asc"));
		gtk_box_pack_start_defaults (GTK_BOX (this->asc_desc), item);
		group = gtk_radio_button_group (GTK_RADIO_BUTTON (item));
		item = gtk_radio_button_new_with_label (group, _("Desc"));
		gtk_box_pack_start_defaults (GTK_BOX (this->asc_desc), item);
		group = gtk_radio_button_group (GTK_RADIO_BUTTON (item));
		this->group = group ;

		gtk_box_pack_start (GTK_BOX(box), this->asc_desc, TRUE, FALSE, 0);
	}
	gtk_container_add (GTK_CONTAINER(this->main_frame), box);
	gtk_box_pack_start (GTK_BOX(parent), GTK_WIDGET(this->main_frame), TRUE, TRUE, 0);
	return this ;
}

static void
order_box_set_default (ORDER_BOX *this)
{
	gtk_widget_grab_focus (this->rangetext);
}

/**
 * Return value must be g_freed
 **/
static char *
order_box_get_text (ORDER_BOX *this, int *asc)
{
	*asc = gtk_radio_group_get_selected (this->group);
	return gtk_editable_get_chars (GTK_EDITABLE (this->rangetext), 0, -1);
}

typedef struct {
	int col_offset ;
	int asc ;
} CLAUSE_DATA ;

typedef struct {
	Sheet *sheet ;
	CLAUSE_DATA *clauses ;
	Cell **cells ;
	int num_clause ;
	int col ;
	int row ;
} SORT_DATA ;

static int
compare_values (const SORT_DATA *ain, const SORT_DATA *bin, int clause)
{
	Cell *ca, *cb ;
	Value *a, *b ;
	int ans=0, fans=0 ;

	ca = ain->cells[ain->clauses[clause].col_offset] ;
	cb = bin->cells[bin->clauses[clause].col_offset] ;

	if (!ca)
		a = value_int (0);
	else
		a = ca->value ;
	if (!cb)
		b = value_int (0);
	else
		b = cb->value ;

	switch (a->type)
	{
	case VALUE_FLOAT:
	case VALUE_INTEGER:
		switch (b->type)
		{
		case VALUE_FLOAT:
		case VALUE_INTEGER:
		{
			float_t fa, fb ;
			fa = value_get_as_double (a);
			fb = value_get_as_double (b);
			if      (fa<fb)  ans=-1 ;
			else if (fa==fb) ans= 0 ;
			else             ans= 1 ;
			break ;
		}
		default:
			ans = -1 ;
			break ;
		}
		break ;
	default: {
			switch (b->type){
			case VALUE_FLOAT:
			case VALUE_INTEGER:
				ans = 1 ;
				break ;
			default: {
					char *sa, *sb ;
					sa  = value_string (a);
					sb  = value_string (b);
					ans = strcasecmp (sa, sb);
					g_free (sa);
					g_free (sb);
					break ;
				}
			}
			break ;
		}
	}

/*	fans = ans ; */
	if (ans == 0)
		if (clause<ain->num_clause-1)
			fans = compare_values (ain, bin, ++clause);
		else
			fans = ans ;
	else if (ans < 0)
		fans = ain->clauses[clause].asc?1:-1 ;
	else
		fans = ain->clauses[clause].asc?-1:1 ;

	if (!ca)
		value_release (a);
	if (!cb)
		value_release (b);

	return fans ;
}

static int
qsort_func (const SORT_DATA *a, const SORT_DATA *b)
{
	return compare_values (a, b, 0);
}

static void
sort_cell_range (Sheet *sheet, CLAUSE_DATA *clauses, int num_clause, int start_col, int start_row,
		 int end_col, int end_row)
{
	SORT_DATA *array ;
	int lp,height,width,lp2 ;

	height = end_row - start_row + 1 ;
	width  = end_col - start_col + 1 ;
	array = g_new (SORT_DATA, height);

	for (lp = 0; lp < height; lp++){
		array[lp].sheet   = sheet ;
		array[lp].clauses = clauses ;
		array[lp].num_clause = num_clause ;
		array[lp].col     = start_col ;
		array[lp].row     = start_row + lp ;
		array[lp].cells   = g_new (Cell *, width);
		for (lp2 = 0;lp2 < width; lp2++){
			Cell *cell ;
			cell = sheet_cell_get (sheet,
					       start_col+lp2, start_row + lp);
			array[lp].cells[lp2] = cell ;
			if (cell)
				sheet_cell_remove (sheet, cell);
		}
	}

	qsort (array, height, sizeof(SORT_DATA), qsort_func);
/*	       (int *(const void *, const void *))qsort_func); */
	{
		Cell *cell ;
		for (lp = 0; lp < height; lp++){
			for (lp2 = 0; lp2 < width; lp2++) {
				cell = array[lp].cells[lp2] ;
/*				printf ("%s\n", cell?value_string(cell->value):"Null"); */
				if (cell)
					sheet_cell_add (sheet, cell, start_col + lp2, start_row + lp);
			}
			g_free (array [lp].cells);
		}
	}
	g_free (array);
}

/*
 * Main entry point for the Cell Sort dialog box
 */
void
dialog_cell_sort (Workbook *wb, Sheet *sheet)
{
	GtkWidget *dialog ;
	GtkWidget *button ;
	int lp, num_clause, retry ;
	int base_col, base_row, start_col, start_row, end_col, end_row ;
	ORDER_BOX *clauses[2] ;
	char      *txt ;

	g_return_if_fail (wb);
	g_return_if_fail (sheet);
	g_return_if_fail (IS_SHEET (sheet));

	if (!sheet_selection_first_range(sheet, &base_col, &base_row,
					 &start_col, &start_row,
					 &end_col, &end_row)) {
		gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("Selection must be a single range"));
		return ;
	}
	if (end_row >= SHEET_MAX_ROWS-2 ||
	    end_col >= SHEET_MAX_COLS-2) {
		gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("Selection must be a finite range")) ;
		return ;		
	}

	num_clause = 1; 
	
	do
	{
		retry = 0 ;
		dialog = gnome_dialog_new (_("Sort Cells"),
					   GNOME_STOCK_BUTTON_OK,
					   GNOME_STOCK_BUTTON_CANCEL,
					   NULL);
	
		gnome_dialog_set_parent (GNOME_DIALOG (dialog), GTK_WINDOW (wb->toplevel));
		
		clauses[0] = order_box_new (GNOME_DIALOG (dialog)->vbox, _("Sort by"), col_name(start_col));
		gtk_widget_show_all (dialog);
		gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
		order_box_set_default (clauses[0]);
		
		button = gtk_button_new_with_label ("Add clause");
		gtk_box_pack_start (GTK_BOX(GNOME_DIALOG(dialog)->vbox), button, TRUE, TRUE, 0);
		
		if (gnome_dialog_run (GNOME_DIALOG (dialog)) == 0) 
		{
			CLAUSE_DATA *array ;
			array = g_new (CLAUSE_DATA, num_clause);
			for (lp=0;lp<num_clause;lp++) {
				int col ;
				char *txt = order_box_get_text (clauses[lp],
								&array[lp].asc);
				col = col_from_name (txt);
				if (col<start_col || col>end_col) {
					gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
							 _("Column must be within range"));
					retry = 1 ;
				}
				array[lp].col_offset = col - start_col ;
				g_free (txt);
			}
			if (!retry)
				sort_cell_range (sheet, array, num_clause,
						 start_col, start_row,
						 end_col, end_row);
		}
		else
			retry = 0 ;
		gtk_object_destroy (GTK_OBJECT (dialog));
	}
	while (retry);
}




