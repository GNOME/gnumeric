/*
 * Gnumeric, the GNOME spreadhseet
 *
 * Graphics Wizard's Graphic Context manager
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 * (C) 1999 Miguel de Icaza.
 * (C) 2000 Helix Code, Inc.
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "workbook.h"
#include <glade/glade.h>
#include <bonobo.h>
#include "graphic-context.h"
#include "graphic-type.h"
#include "sheet.h"
#include "parse-util.h"
#include "expr.h"
#include "value.h"

#define GRAPH_GOADID "GOADID:embeddable:Graph:Layout"

static BonoboObjectClient *
get_graphics_component (void)
{
	BonoboObjectClient *object_server;

	object_server = bonobo_object_activate_with_goad_id (NULL, GRAPH_GOADID, 0, NULL);

	return object_server;
}

static void
graphic_context_load_widget_pointers (WizardGraphicContext *gc, GladeXML *gui)
{
	gc->dialog_toplevel = glade_xml_get_widget (gui, "graphics-wizard-dialog");
	gc->steps_notebook = GTK_NOTEBOOK (glade_xml_get_widget (gui, "main-notebook"));
}

static void
gc_selection_get_sizes (Sheet *sheet, int *cols, int *rows)
{
	GList *l;

	*cols = *rows = 0;
	
	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		const int range_cols = ss->user.end.col - ss->user.start.col;
		const int range_rows = ss->user.end.row - ss->user.start.row;

		*cols += range_cols;
		*rows += range_rows;
	}
}

void
graphic_wizard_guess_series (WizardGraphicContext *gc, SeriesOrientation orientation,
			     gboolean first_item_is_series_name)
{
	Sheet *sheet = gc->workbook->current_sheet;
	GList *l;
	int series = 0;
	int offset;
	
	if (first_item_is_series_name)
		offset = 1;
	else
		offset = 0;
	
	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = l->data;
		SheetVector *vector;
		const CellPos *start = &ss->user.start;
		const CellPos *end   = &ss->user.end;
		Range range;
		int start_pos, end_pos, item;
		int header_col, header_row;
		
		if (orientation == SERIES_COLUMNS){
			start_pos = start->col;
			end_pos   = end->col;
		} else {
			start_pos = start->row;
			end_pos   = end->row;
		}
		
		for (item = start_pos; item <= end_pos; item++){
			DataRange *data_range;
			char *expr;
			
			if (orientation == SERIES_COLUMNS){
				range.start.col = item;
				range.end.col   = item;
				range.start.row = start->row + offset;
				range.end.row   = end->row;

				if (range.start.row > range.end.row)
					continue;
				
				header_col = item;
				header_row = start->row;
			} else {
				range.start.row = item;
				range.end.row   = item;
				range.start.col = start->col + offset;
				range.end.col   = end->col;

				if (range.start.col > range.end.col)
					continue;

				header_col = start->col;
				header_row = item;
			}
			

			if (first_item_is_series_name){
				expr = g_strdup_printf (
					"%s!$%s$%d",
					sheet->name_quoted,
					col_name (header_col), header_row+1);
			} else {
				expr = g_strdup_printf (
					_("\"Series %d\""), series);
			}
			vector = sheet_vector_new (sheet);
			sheet_vector_append_range (vector, &range);
			printf ("Range: (%d %d) (%d %d)\n",
				range.start.col, range.start.row,
				range.end.col, range.end.row);
			
			data_range = data_range_new_from_vector (gc->workbook, expr, vector);
			g_free (expr);

			graphic_context_data_range_add (gc, data_range);
		}
	}
}

static GNOME_Gnumeric_Vector
vector_from_data_range (DataRange *data_range)
{
	return (GNOME_Gnumeric_Vector) bonobo_object_corba_objref (BONOBO_OBJECT (data_range->vector));
}

static void
graphic_context_auto_guess_series (WizardGraphicContext *gc)
{
	CORBA_Environment ev;
	GList *vector_list;
	Sheet *sheet = gc->workbook->current_sheet;
	int cols, rows;
	
	gc_selection_get_sizes (sheet, &cols, &rows);

	if (cols > rows)
		graphic_wizard_guess_series (gc, SERIES_ROWS, TRUE);
	else
		graphic_wizard_guess_series (gc, SERIES_COLUMNS, TRUE);
	
	CORBA_exception_init (&ev);
	GNOME_Graph_Layout_reset_series (gc->layout, &ev);
	
	vector_list = gc->data_range_list;
	for (; vector_list != NULL; vector_list = vector_list->next){
		GNOME_Gnumeric_Vector vector;

		vector = vector_from_data_range (vector_list->data);
		GNOME_Graph_Layout_add_series (gc->layout, vector, "FIXME", &ev);
	}

	graphic_type_init_preview (gc);
/*	graphic_series_fill_data (gc);*/
	
	CORBA_exception_free (&ev);
}

WizardGraphicContext *
graphic_context_new (Workbook *wb, GladeXML *gui)
{
	WizardGraphicContext *gc;
	BonoboClientSite     *client_site;
	BonoboObjectClient   *object_server;
	CORBA_Environment     ev;
	GNOME_Graph_Layout    layout;
	GNOME_Graph_Layout    chart;
	
	g_return_val_if_fail (wb != NULL, NULL);

	/*
	 * Configure our container end
	 */
	client_site = bonobo_client_site_new (wb->bonobo_container);
	bonobo_container_add (wb->bonobo_container, BONOBO_OBJECT (client_site));
	
	/*
	 * Launch server
	 */
	object_server = get_graphics_component ();
	if (!object_server)
		goto error_activation;

	/*
	 * Bind them together
	 */
	if (!bonobo_client_site_bind_embeddable (client_site, object_server))
		goto error_binding;

	layout = bonobo_object_query_interface (BONOBO_OBJECT (object_server),
						    "IDL:GNOME/Graph/Layout:1.0");
	if (layout == CORBA_OBJECT_NIL)
		goto error_qi;

	CORBA_exception_init (&ev);
	chart = GNOME_Graph_Layout_get_chart (layout, &ev);
	if (ev._major != CORBA_NO_EXCEPTION)
		goto error_get_chart;
	CORBA_exception_free (&ev);

	/*
	 * Create the graphic context
	 */
	gc = g_new0 (WizardGraphicContext, 1);
	gc->workbook = wb;
	gc->layout = layout;
	gc->chart = chart;
	gc->signature = GC_SIGNATURE;
	gc->current_page = 0;
	gc->gui = gui;
	gc->last_graphic_type_page = -1;
	
	graphic_context_load_widget_pointers (gc, gui);
	
	gc->client_site = client_site;
	gc->graphics_server = object_server;

	graphic_context_auto_guess_series (gc);

	return gc;

error_get_chart:
	CORBA_exception_free (&ev);

error_qi:
	
error_binding:
	bonobo_object_unref (BONOBO_OBJECT (object_server));
	
error_activation:
	bonobo_object_unref (BONOBO_OBJECT (client_site));

	return NULL;
}

void
graphic_context_destroy (WizardGraphicContext *gc)
{
	GList             *l;
	CORBA_Environment  ev;
	
	g_return_if_fail (gc != NULL);
	g_return_if_fail (IS_GRAPHIC_CONTEXT (gc));

	gtk_object_destroy (GTK_OBJECT (gc->dialog_toplevel));
	gtk_object_unref (GTK_OBJECT (gc->gui));

	if (gc->data_range)
		string_unref (gc->data_range);
	if (gc->x_axis_label)
		string_unref (gc->x_axis_label);
	if (gc->plot_title)
		string_unref (gc->plot_title);
	if (gc->y_axis_label)
		string_unref (gc->y_axis_label);

	for (l = gc->data_range_list; l; l = l->next){
		DataRange *data_range = l->data;

		data_range_destroy (data_range, TRUE);
	}
	g_list_free (gc->data_range_list);

	bonobo_object_unref (BONOBO_OBJECT (gc->graphics_server));

	for (l = gc->view_frames; l; l = l->next){
		BonoboViewFrame *view_frame = BONOBO_VIEW_FRAME (l->data);

		bonobo_object_unref (BONOBO_OBJECT (view_frame));
	}
	CORBA_exception_init (&ev);
	CORBA_Object_release (gc->layout, &ev);
	CORBA_Object_release (gc->chart, &ev);
	CORBA_exception_free (&ev);
	
	g_free (gc);
}

void
graphic_context_data_range_add (WizardGraphicContext *gc, DataRange *data_range)
{
	g_return_if_fail (gc != NULL);
	g_return_if_fail (IS_GRAPHIC_CONTEXT (gc));
	g_return_if_fail (data_range != NULL);

	gc->data_range_list = g_list_prepend (gc->data_range_list, data_range);
}

void
graphic_context_data_range_remove (WizardGraphicContext *gc, DataRange *data_range)
{
	g_return_if_fail (gc != NULL);
	g_return_if_fail (IS_GRAPHIC_CONTEXT (gc));
	g_return_if_fail (data_range != NULL);

	gc->data_range_list = g_list_remove (gc->data_range_list, data_range);

}

static void
graphic_context_data_range_clear (WizardGraphicContext *gc)
{
	GList *l;
	
	g_return_if_fail (gc != NULL);

	for (l = gc->data_range_list; l; l = l->next){
		DataRange *data_range = l->data;

		data_range_destroy (data_range, TRUE);
	}
	g_list_free (gc->data_range_list);
	gc->data_range_list = NULL;
}

/**
 * graphic_context_set_data_range:
 * @gc: the graphics context
 * @data_range_spec: a string description of the cell ranges
 * @vertical: whether we need to create the data ranges in vertical mode
 *
 * This routine defines the data ranges based on the @data_range_spec, and
 * it guesses the series values using @vertical
 */
static void
graphic_context_set_data_range (WizardGraphicContext *gc,
				const char *data_range_spec,
				gboolean vertical)
{
	
	g_return_if_fail (gc != NULL);
	g_return_if_fail (data_range_spec != NULL);

	graphic_context_data_range_clear (gc);

}

DataRange *
data_range_new (Workbook *wb, const char *name_expr)
{
	DataRange *data_range;
	ParsePosition pp;
	ExprTree *tree;
	char *error;
	
	parse_pos_init (&pp, wb, NULL, 0, 0);
	
	data_range = g_new (DataRange, 1);
	tree = expr_parse_string (name_expr, &pp, NULL, &error);

	if (tree == NULL){
		data_range->entered_expr = string_get ("\"\"");
		tree = expr_parse_string ("\"\"", &pp, NULL, &error);
	} else 
		data_range->entered_expr = string_get (name_expr);

	data_range->name_expr = tree;
	
	return data_range;
}

static char *
data_range_get_name (DataRange *data_range)
{
	g_return_val_if_fail (data_range != NULL, NULL);

	g_error ("Implement me");
	
	return NULL;
}

DataRange *
data_range_new_from_expr (Workbook *wb, const char *name_expr, const char *expression)
{
	ParsePosition pp;
	GList *expressions;
	DataRange *data_range;
	ExprTree *tree;
	char *expr;
	char *error;

	/* Hack:
	 * Create a valid expression, parse as expression, and pull the
	 * parsed arguments.
	 */
	parse_pos_init (&pp, wb, NULL, 0, 0);
	expr = g_strconcat ("=SELECTION(", expression, ")", NULL);
	tree = expr_parse_string (expr, &pp, NULL, &error);
	g_free (expr);
	
	if (tree == NULL)
		return NULL;

	g_assert (tree->oper != OPER_FUNCALL);

	/*
	 * Verify that the entire tree contains cell references
	 */
	for (expressions = tree->u.function.arg_list; expressions; expressions = expressions->next){
		ExprTree *tree = expressions->data;

		if (tree->oper == OPER_CONSTANT){
			if (tree->u.constant->type != VALUE_CELLRANGE)
				return NULL;
		} else if (tree->oper != OPER_VAR)
			return NULL;
	}

	data_range = data_range_new (wb, name_expr);
	data_range->vector = sheet_vector_new (wb->current_sheet);

	for (expressions = tree->u.function.arg_list; expressions; expressions = expressions->next){
		ExprTree *tree = expressions->data;

		if (tree->oper == OPER_CONSTANT){
			Value *v = tree->u.constant;
			CellRef *cell_a = &v->v.cell_range.cell_a;
			CellRef *cell_b = &v->v.cell_range.cell_b;
			Range r;
			
			g_assert (tree->u.constant->type == VALUE_CELLRANGE);

			r.start.col = MIN (cell_a->col, cell_b->col);
			r.start.row = MIN (cell_a->row, cell_b->row);
			r.end.col   = MAX (cell_a->col, cell_b->col);
			r.end.row   = MAX (cell_a->row, cell_b->row);
			
			sheet_vector_append_range (data_range->vector, &r);
		} else if (tree->oper == OPER_VAR){
			CellRef *cr = &tree->u.ref;
			Range r;

			r.start.col = cr->col;
			r.start.row = cr->row;
			r.end.col   = cr->col;
			r.end.row   = cr->row;
			
			sheet_vector_append_range (data_range->vector, &r);
			return NULL;
		} else
			g_error ("This should not happen");
	}

	expr_tree_unref (tree);
	
	return data_range;
}

DataRange *
data_range_new_from_vector (Workbook *wb, const char *name_expr, SheetVector *vector)
{
	DataRange *data_range;
	
	data_range = data_range_new (wb, name_expr);
	data_range->vector = vector;

	return data_range;
}

void
data_range_destroy (DataRange *data_range, gboolean detach_from_sheet)
{
	expr_tree_unref (data_range->name_expr);

	if (detach_from_sheet && data_range->vector)
		sheet_vector_detach (data_range->vector);

	if (data_range->vector)
		bonobo_object_unref (BONOBO_OBJECT (data_range->vector));

	string_unref (data_range->entered_expr);
	g_free (data_range);
}

BonoboViewFrame *
graphic_context_new_chart_view_frame (WizardGraphicContext *gc)
{
	BonoboViewFrame *view_frame;

	view_frame = bonobo_client_site_new_view (
		gc->client_site, gc->workbook->uih->top_level_uih);

	gc->view_frames = g_list_append (gc->view_frames, view_frame);

	return view_frame;
}

