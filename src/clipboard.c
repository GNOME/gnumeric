/*
 * Clipboard.c: Implements the copy/paste operations
 * (C) 1998 The Free Software Foundation.
 *
 * Author:
 *  MIguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "clipboard.h"
#include "eval.h"

typedef struct {
	int        base_col, base_row;
	CellRegion *r;
} append_cell_closure_t;

static int
clipboard_append_cell (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	append_cell_closure_t *c = user_data;
	CellCopy *copy;

	copy = g_new (CellCopy, 1);

	copy->cell = cell_copy (cell);
	copy->col_offset  = col - c->base_col;
	copy->row_offset  = row - c->base_row;
	
	/* Now clear the traces and dependencies on the copied Cell */
	copy->cell->col   = NULL;
	copy->cell->row   = NULL;
	copy->cell->sheet = NULL;
	
	c->r->list = g_list_prepend (c->r->list, copy);

	return TRUE;
}

CellRegion *
clipboard_copy_cell_range (Sheet *sheet, int start_col, int start_row, int end_col, int end_row)
{
	append_cell_closure_t c;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (start_col <= end_col, NULL);
	g_return_val_if_fail (start_row <= end_row, NULL);
	
	c.r = g_new0 (CellRegion, 1);

	c.base_col = start_col;
	c.base_row = start_row;
	c.r->cols = end_col - start_col + 1;
	c.r->rows = end_row - start_row + 1;
	
	sheet_cell_foreach_range (
		sheet, 1, start_col, start_row, end_col, end_row,
		clipboard_append_cell, &c);

	return c.r;
}

void
clipboard_paste_region (CellRegion *region, Sheet *dest_sheet, int dest_col, int dest_row, int paste_flags)
{
	CellCopyList *l;
	int paste_formulas = paste_flags & PASTE_FORMULAS;
	int paste_formats = paste_formulas & PASTE_FORMATS;
	
	g_return_if_fail (region != NULL);
	g_return_if_fail (dest_sheet != NULL);
	g_return_if_fail (IS_SHEET (dest_sheet));

	/* Clear the region where we will paste */
	sheet_clear_region (dest_sheet,
			    dest_col, dest_row,
			    dest_col + region->cols - 1,
			    dest_row + region->rows - 1);

	sheet_redraw_cell_region (dest_sheet,
				  dest_col, dest_row,
				  dest_col + region->cols - 1,
				  dest_row + region->rows - 1);
	
	/* Paste each element */
	for (l = region->list; l; l = l->next){
		CellCopy *c_copy = l->data;
		Cell *new_cell;
		int target_col, target_row;

		target_col = dest_col + c_copy->col_offset;
		target_row = dest_row + c_copy->row_offset;
		
		new_cell = cell_copy (c_copy->cell);
		
		sheet_cell_add (dest_sheet, new_cell, target_col, target_row);

		if (new_cell->parsed_node){
			char *new_text, *formula;

			if (paste_formulas){
				string_unref (new_cell->entered_text);
				
				new_text = expr_decode_tree (
					new_cell->parsed_node,
					target_col, target_row);
				
				formula = g_copy_strings ("=", new_text, NULL);
				new_cell->entered_text = string_get (formula);
				g_free (formula);
				g_free (new_text);
				
				cell_formula_changed (new_cell);
			} else {
				expr_tree_unref (new_cell->parsed_node);
				new_cell->parsed_node = NULL;
			}
		}

		sheet_redraw_cell_region (dest_sheet,
					  target_col, target_row,
					  target_col, target_row);
	}
}

void
clipboard_release (CellRegion *region)
{
	CellCopyList *l;

	g_return_if_fail (region != NULL);
	
	l = region->list;

	for (; l; l = l->next){
		CellCopy *this_cell = l->data;

		cell_destroy (this_cell->cell);
		g_free (this_cell);
	}
	g_list_free (region->list);
	g_free (region);
}

static struct {
	char *name;
	int  disables_second_group;
} paste_types [] = {
	{ N_("All"),      0 },
	{ N_("Formulas"), 0 },
	{ N_("Values"),   0 },
	{ N_("Formats"),  1 },
	{ NULL, 0 }
};

static char *paste_ops [] = {
	N_("None"),
	N_("Add"),
	N_("Substract"),
	N_("Multiply"),
	N_("Divide"),
	NULL
};

static void
disable_op_group (GtkWidget *widget, GtkWidget *group)
{
	gtk_widget_set_sensitive (group, FALSE);
}

static void
enable_op_group (GtkWidget *widget, GtkWidget *group)
{
	gtk_widget_set_sensitive (group, TRUE);
}

int
dialog_paste_special (void)
{
	GtkWidget *dialog, *hbox;
	GtkWidget *f1, *f1v, *f2, *f2v;
	GSList *group_type, *group_ops, *l;
	int i, result;
	
	dialog = gnome_dialog_new (_("Paste special"),
				   GNOME_STOCK_BUTTON_OK,
				   GNOME_STOCK_BUTTON_CANCEL,
				   NULL);
	f1  = gtk_frame_new (_("Paste type"));
	f1v = gtk_vbox_new (TRUE, 0);
	gtk_container_add (GTK_CONTAINER (f1), f1v);

	f2  = gtk_frame_new (_("Operation"));
	f2v = gtk_vbox_new (TRUE, 0);
	gtk_container_add (GTK_CONTAINER (f2), f2v);

	group_type = NULL;
	for (i = 0; paste_types [i].name; i++){
		GtkSignalFunc func;
		GtkWidget *r;

		if (paste_types [i].disables_second_group)
			func = GTK_SIGNAL_FUNC (disable_op_group);
		else
			func = GTK_SIGNAL_FUNC (enable_op_group);
		
		r = gtk_radio_button_new_with_label (group_type, _(paste_types [i].name));
		group_type = GTK_RADIO_BUTTON (r)->group;
		
		gtk_signal_connect (GTK_OBJECT (r), "toggled", func, f2);

		gtk_box_pack_start_defaults (GTK_BOX (f1v), r);
	}

	group_ops = NULL;
	for (i = 0; paste_ops [i]; i++){
		GtkWidget *r;
		
		r = gtk_radio_button_new_with_label (group_ops, _(paste_ops [i]));
		group_ops = GTK_RADIO_BUTTON (r)->group;
		gtk_box_pack_start_defaults (GTK_BOX (f2v), r);
	}
	
	hbox = gtk_hbox_new (TRUE, 0);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), f1);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), f2);


	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), hbox, TRUE, TRUE, 0);
	gtk_widget_show_all (hbox);

	/* Run the dialog */
	gnome_dialog_run_modal (GNOME_DIALOG (dialog));

	/* Fetch the results */

	result = 0;
	i = gtk_radio_group_get_selected (group_type);
	switch (i){
	case 0: /* all */
		result = PASTE_ALL_TYPES;
		break;
			      
	case 1: /* formulas */
		result = PASTE_FORMULAS;
		break;
		
	case 2: /* values */
		result = PASTE_VALUES;
		break;
		
	case 3: /* formats */
		result = PASTE_FORMATS;
		break;
	}

	/* If it was not just formats, check operation */
	if (i != 3){
		i = gtk_radio_group_get_selected (group_ops);
		switch (i){
		case 1:		/* Add */
			result |= PASTE_OP_ADD;
			break;

		case 2:
			result |= PASTE_OP_SUB;
			break;

		case 3:
			result |= PASTE_OP_MULT;
			break;

		case 4:
			result |= PASTE_OP_DIV;
			break;
		}
	}
		
	gtk_object_destroy (GTK_OBJECT (dialog));

	return result;
}

