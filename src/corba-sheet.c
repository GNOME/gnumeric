/*
 * corba-sheet.c: The implementation of the Sheet CORBA interfaces
 * defined by Gnumeric
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org)
 *
 * Notes:
 *
 * Strings representing ranges, when parsed ignore relative strings.
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include <idl/Gnumeric.h>

#include "sheet.h"
#include "selection.h"
#include "corba.h"
#include "parse-util.h"
#include "ranges.h"
#include "selection.h"
#include "commands.h"
#include "cell.h"
#include "sheet.h"
#include "colrow.h"
#include "value.h"
#include "format.h"
#include "sheet-private.h"
#include "cell-comment.h"
#include "cmd-edit.h"

#include <libgnorba/gnome-factory.h>
#include <gnome.h>

#define verify(cond)          if (!(cond)){ out_of_range (ev); return; }
#define verify_val(cond,val)  if (!(cond)){ out_of_range (ev); return (val); }
#define verify_col(c)         verify (((c) >= 0 && (c < SHEET_MAX_COLS)))
#define verify_col_val(c,val) verify_val (((c) >= 0 && (c < SHEET_MAX_COLS)),val)
#define verify_row(c)         verify (((c) >= 0 && (c < SHEET_MAX_ROWS)))
#define verify_row_val(c,val) verify_val (((c) >= 0 && (c < SHEET_MAX_ROWS)),val)
#define verify_region(c1,r1,c2,r2) \
	verify_col(c1); verify_col(c2); verify_row(r1); verify_row(r2); \
	verify(c1 <= c2);\
	verify (r1 <= r2);

#define verify_range(sheet,range,l) \
        if (!corba_range_parse (sheet,range,l)) \
	{ out_of_range (ev); return; }

#define verify_range_val(sheet,range,l,val) \
        if (!corba_range_parse (sheet,range,l)) \
	{ out_of_range (ev); return (val); }

static POA_GNOME_Gnumeric_Sheet__vepv gnome_gnumeric_sheet_vepv;
static POA_GNOME_Gnumeric_Sheet__epv gnome_gnumeric_sheet_epv;

typedef struct {
	POA_GNOME_Gnumeric_Sheet servant;
	Sheet     *sheet;
	SheetView *sheet_view;
} SheetServant;

static inline void
out_of_range (CORBA_Environment *ev)
{
	CORBA_exception_set (ev, CORBA_USER_EXCEPTION, ex_GNOME_Gnumeric_Sheet_OutOfRange, NULL);
}

/*
 * Parses a list of ranges, returns a GList containing pointers to
 * Value structures.  Sets the return_list pointer to point to a
 * a list of those values.
 *
 * Returns TRUE on successfully parsing the string, FALSE otherwise
 */
static gboolean
corba_range_parse (Sheet *sheet, const char *range_spec, GSList **return_list)
{
	GSList *list;

	list = range_list_parse (sheet, range_spec, TRUE);
	if (list) {
		*return_list = list;
		return TRUE;
	} else {
		*return_list = NULL;
		return FALSE;
	}
}

static inline Sheet *
sheet_from_servant (PortableServer_Servant servant)
{
	SheetServant *ss = (SheetServant *) servant;

	return ss->sheet;
}
static inline SheetView *
sv_from_servant (PortableServer_Servant servant)
{
	SheetServant *ss = (SheetServant *) servant;

	return ss->sheet_view;
}

static void
Sheet_cursor_set (PortableServer_Servant servant,
		  const CORBA_long base_col,
		  const CORBA_long base_row,
		  const CORBA_long start_col,
		  const CORBA_long start_row,
		  const CORBA_long end_col,
		  const CORBA_long end_row,
		  CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_region (start_col, start_row, end_col, end_row);
	verify ((base_col > 0)  && (base_row > 0));
	verify ((base_row >= start_row) && (base_row <= end_row) &&
		(base_col >= start_col) && (base_col <= end_col));

	sv_selection_set (sheet, base_col, base_row, start_col, start_row, end_col, end_row);
}

static void
Sheet_cursor_move (PortableServer_Servant servant, const CORBA_long col, const CORBA_long row, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_col (col);
	verify_row (row);

	sv_selection_set (sheet, col, row, col, row, col, row);
}

static void
Sheet_make_cell_visible (PortableServer_Servant servant, const CORBA_long col, const CORBA_long row, CORBA_Environment *ev)
{
	SheetView *sv = sheet_view_from_servant (servant);

	verify_col (col);
	verify_row (row);

	sv_make_cell_visible (sv, col, row, FALSE);
}

static void
Sheet_selection_append (PortableServer_Servant servant,
			const CORBA_long col, const CORBA_long row,
			CORBA_Environment *ev)
{
	SheetView *sv = sv_from_servant (servant);

	verify_col (col);
	verify_row (row);

	sv_selection_add_pos (sheet, col, row);
}

static void
Sheet_selection_append_range (PortableServer_Servant servant,
			      const CORBA_long start_col, const CORBA_long start_row,
			      const CORBA_long end_col, const CORBA_long end_row,
			      CORBA_Environment *ev)
{
	SheetView *sv = sv_from_servant (servant);

	verify_region (start_col, start_row, end_col, end_row);

	sv_selection_add_range (sv,
				start_col, start_row,
				start_col, start_row,
				end_col, end_row);
}

static void
Sheet_selection_copy (PortableServer_Servant servant, CORBA_Environment *ev)
{
	SheetView *sv = sv_from_servant (servant);
	sv_selection_copy (sv, command_context_corba (sheet->workbook))
}

static void
Sheet_selection_cut (PortableServer_Servant servant, CORBA_Environment *ev)
{
	SheetView *sv = sv_from_servant (servant);
	sv_selection_cut (sv, command_context_corba (sheet->workbook))
}

static void
Sheet_clear_region (PortableServer_Servant servant,
		    const CORBA_long start_col, const CORBA_long start_row,
		    const CORBA_long end_col, const CORBA_long end_row,
		    CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_region (start_col, start_row, end_col, end_row);

	sheet_clear_region (
		command_context_corba (sheet->workbook),
		sheet, start_col, start_row, end_col, end_row,
		CLEAR_VALUES|CLEAR_FORMATS|CLEAR_COMMENTS|CLEAR_RECALC_DEPS);
}

static void
Sheet_clear_region_content (PortableServer_Servant servant,
			    const CORBA_long start_col, const CORBA_long start_row,
			    const CORBA_long end_col, const CORBA_long end_row,
			    CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_region (start_col, start_row, end_col, end_row);

	sheet_clear_region (
		command_context_corba (sheet->workbook),
		sheet, start_col, start_row, end_col, end_row,
		CLEAR_VALUES|CLEAR_COMMENTS|CLEAR_RECALC_DEPS);
}

static void
Sheet_clear_region_comments (PortableServer_Servant servant,
			     const CORBA_long start_col, const CORBA_long start_row,
			     const CORBA_long end_col, const CORBA_long end_row,
			     CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_region (start_col, start_row, end_col, end_row);

	sheet_clear_region (
		command_context_corba (sheet->workbook),
		sheet, start_col, start_row, end_col, end_row,
		CLEAR_COMMENTS);
}

static void
Sheet_clear_region_formats (PortableServer_Servant servant,
			    const CORBA_long start_col, const CORBA_long start_row,
			    const CORBA_long end_col, const CORBA_long end_row,
			    CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_region (start_col, start_row, end_col, end_row);

	sheet_clear_region (
		command_context_corba (sheet->workbook),
		sheet, start_col, start_row, end_col, end_row,
		CLEAR_FORMATS);
}

static void
Sheet_cell_set_value (PortableServer_Servant servant,
		      const CORBA_long col, const CORBA_long row,
		      const GNOME_Gnumeric_Value *value,
		      CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Cell *cell;
	Value *v;

	verify_col (col);
	verify_row (row);

	cell = sheet_cell_fetch (sheet, col, row);

	switch (value->_d){
	case GNOME_Gnumeric_VALUE_EMPTY:
		v = value_new_empty ();
		break;

	case GNOME_Gnumeric_VALUE_BOOLEAN:
		v = value_new_bool (value->_u.v_bool);
		break;

	case GNOME_Gnumeric_VALUE_ERROR:
		v = value_new_error (NULL, value->_u.error);
		break;

	case GNOME_Gnumeric_VALUE_STRING:
		v = value_new_string (value->_u.str);
		break;

	case GNOME_Gnumeric_VALUE_INTEGER:
		v = value_new_int (value->_u.v_int);
		break;

	case GNOME_Gnumeric_VALUE_FLOAT:
		v = value_new_float (value->_u.v_float);
		break;

	case GNOME_Gnumeric_VALUE_CELLRANGE: {
		CellRef a, b;

		parse_cell_name (value->_u.cell_range.cell_a, &a, TRUE, NULL);
		parse_cell_name (value->_u.cell_range.cell_b, &b, TRUE, NULL);
		a.sheet = sheet;
		b.sheet = sheet;
		a.col_relative = 0;
		b.col_relative = 0;
		a.row_relative = 0;
		b.row_relative = 0;
		/* We can dummy these out because everything is absolute */
		v = value_new_cellrange (&a, &b, 0, 0);
		break;
	}

	case GNOME_Gnumeric_VALUE_ARRAY:
		v = NULL;
		g_error ("FIXME: Implement me");
		break;

	default:
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION, ex_GNOME_Gnumeric_Sheet_InvalidCmd, NULL);
		return;
	}

	sheet_cell_set_value (cell, v, NULL);
}

static void
fill_corba_value (GNOME_Gnumeric_Value *value, Sheet *sheet, CORBA_long col, CORBA_long row)
{
	Cell *cell;
	ParsePos pp;

	g_assert (value != NULL);
	g_assert (sheet != NULL);

	parse_pos_init (&pp, NULL, sheet, col, row);
	cell = sheet_cell_get (sheet, col, row);
	if (cell && cell->value) {
		switch (cell->value->type) {
		case VALUE_EMPTY:
			value->_d = GNOME_Gnumeric_VALUE_EMPTY;
			break;

		case VALUE_BOOLEAN:
			value->_d = GNOME_Gnumeric_VALUE_BOOLEAN;
			value->_u.v_bool = cell->value->v_bool.val;
			break;

		case VALUE_ERROR:
			value->_d = GNOME_Gnumeric_VALUE_ERROR;
			value->_u.error = CORBA_string_dup (cell->value->v_err.mesg->str);
			break;

		case VALUE_STRING:
			value->_d = GNOME_Gnumeric_VALUE_STRING;
			value->_u.str = CORBA_string_dup (cell->value->v_str.val->str);
			break;

		case VALUE_INTEGER:
			value->_d = GNOME_Gnumeric_VALUE_INTEGER;
			value->_u.v_int = cell->value->v_int.val;
			break;

		case VALUE_FLOAT:
			value->_d = GNOME_Gnumeric_VALUE_FLOAT;
			value->_u.v_float = cell->value->v_float.val;
			break;

		case VALUE_CELLRANGE: {
			char *a, *b;
			Value const *v = cell->value;

			a = cellref_name (&v->v_range.cell.a, &pp, FALSE);
			b = cellref_name (&v->v_range.cell.b, &pp,
					  v->v_range.cell.a.sheet ==
					  v->v_range.cell.b.sheet);

			value->_d = GNOME_Gnumeric_VALUE_CELLRANGE;
			value->_u.cell_range.cell_a = CORBA_string_dup (a);
			value->_u.cell_range.cell_b = CORBA_string_dup (b);
			g_free (a);
			g_free (b);
			break;
		}

		case VALUE_ARRAY:
			g_error ("FIXME: Implement me");
			break;
		}
	} else {
		value->_d = GNOME_Gnumeric_VALUE_INTEGER;
		value->_u.v_int = 0;
	}

}

static GNOME_Gnumeric_Value *
Sheet_cell_get_value (PortableServer_Servant servant,
		      const CORBA_long col, const CORBA_long row,
		      CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	GNOME_Gnumeric_Value *value;

	verify_col_val (col, NULL);
	verify_row_val (row, NULL);

	value = GNOME_Gnumeric_Value__alloc ();

	fill_corba_value (value, sheet, col, row);
	return value;
}

static void
Sheet_cell_set_text (PortableServer_Servant servant,
		     const CORBA_long col, const CORBA_long row,
		     const CORBA_char *text, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Cell *cell;

	verify_col (col);
	verify_row (row);

	cell = sheet_cell_fetch (sheet, col, row);
	sheet_cell_set_text (cell, text);
}

static CORBA_char *
Sheet_cell_get_text (PortableServer_Servant servant,
		     const CORBA_long col,
		     const CORBA_long row,
		     CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Cell  *cell;

	verify_col_val (col, NULL);
	verify_row_val (row, NULL);

	cell = sheet_cell_get (sheet, col, row);
	if (cell) {
		char *str;

		str = cell_get_entered_text (cell);
		return CORBA_string_dup (str);
	} else {
		return CORBA_string_dup ("");
	}
}

static void
Sheet_cell_set_format (PortableServer_Servant servant,
		       const CORBA_long col,
		       const CORBA_long row,
		       const CORBA_char *format,
		       CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Cell *cell;

	verify_col (col);
	verify_row (row);

	cell = sheet_cell_fetch (sheet, col, row);
	cell_set_format (cell, format);
}

static CORBA_char *
Sheet_cell_get_format (PortableServer_Servant servant,
		       const CORBA_long col,
		       const CORBA_long row,
		       CORBA_Environment *ev)
{
	CORBA_char *ans;
	Sheet *sheet = sheet_from_servant (servant);
	MStyle *mstyle;
	char *fmt;

	verify_col_val (col, NULL);
	verify_row_val (row, NULL);

	mstyle = sheet_style_get (sheet, col, row);
	/* FIXME : Can this be localized ?? */
	fmt = style_format_as_XL (mstyle_get_format (mstyle), FALSE);
	ans = CORBA_string_dup (fmt);
	g_free (fmt);

	return ans;
}

static void
Sheet_cell_set_font (PortableServer_Servant servant,
		     const CORBA_long col,
		     const CORBA_long row,
		     const CORBA_char *font,
		     const CORBA_double points,
		     CORBA_Environment *ev)
{
	MStyle *mstyle;

	verify(points >= 1.);
	verify_col(col);
	verify_row(row);

	mstyle = mstyle_new ();
	mstyle_set_font_name (mstyle, font);
	mstyle_set_font_size (mstyle, points);
	sheet_style_attach_single (sheet_from_servant (servant),
				   col, row, mstyle);
}

static CORBA_char *
Sheet_cell_get_font (PortableServer_Servant servant, const CORBA_long col, const CORBA_long row, CORBA_Environment *ev)
{
	CORBA_char *ans;
	Sheet *sheet = sheet_from_servant (servant);
	MStyle *mstyle;

	verify_col_val (col, NULL);
	verify_row_val (row, NULL);

	mstyle = sheet_style_get (sheet, col, row);
	ans = CORBA_string_dup (mstyle_get_font_name (mstyle));

	return ans;
}

static void
Sheet_cell_set_comment (PortableServer_Servant servant,
			const CORBA_long col, const CORBA_long row,
			const CORBA_char *comment, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Cell *cell;

	verify_col (col);
	verify_row (row);

	cell = sheet_cell_fetch (sheet, col, row);
	cell_set_comment (cell, comment);
}

static CORBA_char *
Sheet_cell_get_comment (PortableServer_Servant servant,
			const CORBA_long col, const CORBA_long row,
			CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Cell *cell;

	verify_col_val (col, NULL);
	verify_row_val (row, NULL);

	cell = sheet_cell_get (sheet, col, row);
	if (cell)
		return CORBA_string_dup (cell->comment->comment->str);
	else
		return CORBA_string_dup ("");
}

static void
Sheet_cell_set_foreground (PortableServer_Servant servant,
			   const CORBA_long col, const CORBA_long row,
			   const CORBA_char *color, CORBA_Environment *ev)
{
	GdkColor c;
	MStyle *mstyle = mstyle_new ();

	verify_col (col);
	verify_row (row);

	gdk_color_parse (color, &c);
	mstyle_set_color (mstyle, MSTYLE_COLOR_FORE,
			  style_color_new (c.red, c.green, c.blue));
	sheet_style_attach_single (sheet_from_servant (servant),
				   col, row, mstyle);
}

static CORBA_char *
Sheet_cell_get_foreground (PortableServer_Servant servant,
			   const CORBA_long col, const CORBA_long row,
			   CORBA_Environment *ev)
{
	g_warning ("cell get foreground deprecated");

	return NULL;
}

static void
Sheet_cell_set_background (PortableServer_Servant servant,
			   const CORBA_long col, const CORBA_long row,
			   const CORBA_char *color, CORBA_Environment *ev)
{
	GdkColor c;
	MStyle *mstyle = mstyle_new ();

	verify_col (col);
	verify_row (row);

	gdk_color_parse (color, &c);
	mstyle_set_color (mstyle, MSTYLE_COLOR_BACK,
			  style_color_new (c.red, c.green, c.blue));
	sheet_style_attach_single (sheet_from_servant (servant),
				   col, row, mstyle);
}

static CORBA_char *
Sheet_cell_get_background (PortableServer_Servant servant,
			   const CORBA_long col, const CORBA_long row,
			   CORBA_Environment *ev)
{
	g_warning ("Deprecated cell get background");
	return NULL;
}

static void
Sheet_cell_set_pattern (PortableServer_Servant servant,
			const CORBA_long col, const CORBA_long row,
			const CORBA_long pattern, CORBA_Environment *ev)
{
	MStyle *mstyle = mstyle_new ();

	verify_col (col);
	verify_row (row);

	mstyle_set_pattern (mstyle, pattern);
	sheet_style_attach_single (sheet_from_servant (servant),
				   col, row, mstyle);
}

static CORBA_long
Sheet_cell_get_pattern (PortableServer_Servant servant,
			const CORBA_long col, const CORBA_long row,
			CORBA_Environment *ev)
{
	CORBA_long ans;
	Sheet *sheet = sheet_from_servant (servant);
	MStyle *mstyle;

	verify_col_val (col, 0);
	verify_row_val (row, 0);

	mstyle = sheet_style_get (sheet, col, row);
	ans = mstyle_get_pattern (mstyle);

	return ans;
}

static void
Sheet_cell_set_alignment (PortableServer_Servant servant,
			  const CORBA_long col, const CORBA_long row,
			  const CORBA_long halign, const CORBA_long valign,
			  const CORBA_long orientation, const CORBA_boolean wrap_text,
			  CORBA_Environment *ev)
{
	int v, h;
	MStyle *mstyle = mstyle_new ();

	verify_col (col);
	verify_row (row);

	switch (halign) {
	case GNOME_Gnumeric_Sheet_HALIGN_GENERAL:
		h = HALIGN_GENERAL;
		break;

	case GNOME_Gnumeric_Sheet_HALIGN_LEFT:
		h = HALIGN_LEFT;
		break;

	case GNOME_Gnumeric_Sheet_HALIGN_RIGHT:
		h = HALIGN_RIGHT;
		break;

	case GNOME_Gnumeric_Sheet_HALIGN_CENTER:
		h = HALIGN_CENTER;
		break;

	case GNOME_Gnumeric_Sheet_HALIGN_FILL:
		h = HALIGN_FILL;
		break;

	case GNOME_Gnumeric_Sheet_HALIGN_JUSTIFY:
		h = HALIGN_JUSTIFY;
		break;

	case GNOME_Gnumeric_Sheet_HALIGN_CENTER_ACROSS_SELECTION:
		h = HALIGN_CENTER_ACROSS_SELECTION;
		break;

	default:
		h = HALIGN_GENERAL;
	}

	switch (valign) {
	case GNOME_Gnumeric_Sheet_VALIGN_TOP:
		v = VALIGN_TOP;
		break;

	case GNOME_Gnumeric_Sheet_VALIGN_BOTTOM:
		v = VALIGN_BOTTOM;
		break;

	case GNOME_Gnumeric_Sheet_VALIGN_CENTER:
		v = VALIGN_CENTER;
		break;

	case GNOME_Gnumeric_Sheet_VALIGN_JUSTIFY:
		v = VALIGN_JUSTIFY;
		break;

	default:
		v = VALIGN_TOP;
		break;
	}

	mstyle_set_align_v (mstyle, v);
	mstyle_set_align_h (mstyle, h);
	mstyle_set_wrap_text (mstyle, (gboolean) wrap_text);
	sheet_style_attach_single (sheet_from_servant (servant),
				   col, row, mstyle);
}

static void
Sheet_cell_get_alignment (PortableServer_Servant servant,
			  const CORBA_long col, const CORBA_long row,
			  CORBA_long * halign, CORBA_long * valign,
			  CORBA_long * orientation, CORBA_boolean *wrap_text,
			  CORBA_Environment *ev)
{
	MStyle *mstyle;

	verify_col (col);
	verify_row (row);

	mstyle = sheet_style_get (sheet_from_servant (servant), col, row);

	switch (mstyle_get_align_h (mstyle)) {
	case HALIGN_GENERAL:
		*halign = GNOME_Gnumeric_Sheet_HALIGN_GENERAL;
		break;

	case HALIGN_LEFT:
		*halign = GNOME_Gnumeric_Sheet_HALIGN_LEFT;
		break;

	case HALIGN_RIGHT:
		*halign = GNOME_Gnumeric_Sheet_HALIGN_RIGHT;
		break;

	case HALIGN_CENTER:
		*halign = GNOME_Gnumeric_Sheet_HALIGN_CENTER;
		break;

	case HALIGN_FILL:
		*halign = GNOME_Gnumeric_Sheet_HALIGN_FILL;
		break;

	case HALIGN_JUSTIFY:
		*halign = GNOME_Gnumeric_Sheet_HALIGN_JUSTIFY;
		break;

	case HALIGN_CENTER_ACROSS_SELECTION:
		*halign = GNOME_Gnumeric_Sheet_HALIGN_CENTER_ACROSS_SELECTION;
		break;

	default:
		g_assert_not_reached ();
	}

	switch (mstyle_get_align_v (mstyle)) {
	case VALIGN_TOP:
		*valign = GNOME_Gnumeric_Sheet_VALIGN_TOP;
		break;

	case VALIGN_BOTTOM:
		*valign = GNOME_Gnumeric_Sheet_VALIGN_BOTTOM;
		break;

	case VALIGN_CENTER:
		*valign = GNOME_Gnumeric_Sheet_VALIGN_CENTER;
		break;

	case VALIGN_JUSTIFY:
		*valign = GNOME_Gnumeric_Sheet_VALIGN_JUSTIFY;
		break;
	}
	*orientation = mstyle_get_orientation (mstyle);
	*wrap_text = mstyle_get_wrap_text (mstyle);
}

static void
Sheet_set_dirty (PortableServer_Servant servant, const CORBA_boolean is_dirty, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	sheet->modified = is_dirty;
}

static void
Sheet_insert_col (PortableServer_Servant servant,
		  const CORBA_long col, const CORBA_long count,
		  CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_col (col);
	cmd_insert_cols (
		command_context_corba (sheet->workbook), sheet,
		col, count);
}

static void
Sheet_delete_col (PortableServer_Servant servant,
		  const CORBA_long col, const CORBA_long count,
		  CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_col (col);
	cmd_delete_cols (
		command_context_corba (sheet->workbook), sheet,
		col, count);
}

static void
Sheet_insert_row (PortableServer_Servant servant,
		  const CORBA_long row, const CORBA_long count,
		  CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_row (row);
	cmd_insert_rows (
		command_context_corba (sheet->workbook), sheet,
		row, count);
}

static void
Sheet_delete_row (PortableServer_Servant servant,
		  const CORBA_long row, const CORBA_long count,
		  CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_row (row);
	cmd_delete_rows (
		command_context_corba (sheet->workbook), sheet,
		row, count);
}

static void
Sheet_shift_rows (PortableServer_Servant servant,
		  const CORBA_long col, const CORBA_long start_row,
		  const CORBA_long end_row, const CORBA_long count,
		  CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_row (start_row);
	verify_row (end_row);
	verify_col (col);

	cmd_shift_rows (
		command_context_corba (sheet->workbook), sheet,
		col, start_row, end_row, count);
}

static void
Sheet_shift_cols (PortableServer_Servant servant,
		  const CORBA_long col,
		  const CORBA_long start_row, const CORBA_long end_row,
		  const CORBA_long count, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_col (col);
	verify_row (start_row);
	verify_row (end_row);

	cmd_shift_cols (
		command_context_corba (sheet->workbook), sheet,
		col, start_row, end_row, count);
}

static GNOME_Gnumeric_Sheet_ValueVector *
Sheet_range_get_values (PortableServer_Servant servant,
			const CORBA_char *range, CORBA_Environment *ev)
{
	GNOME_Gnumeric_Sheet_ValueVector *vector;
	Sheet *sheet = sheet_from_servant (servant);
	GSList *ranges, *l;
	int size, i;

	verify_range_val (sheet, range, &ranges, NULL);

	/*
	 * Find out how big is the array we need to return
	 */
	size = 0;
	for (l = ranges; l; l = l->next){
		Value *value = l->data;
		CellRef a, b;
		int cols, rows;

		g_assert (value->type == VALUE_CELLRANGE);

		/*
		 * NOTE : These are absolute references
		 * by construction
		 */
		a = value->v_range.cell.a;
		b = value->v_range.cell.b;

		cols = abs (b.col - a.col) + 1;
		rows = abs (b.row - a.row) + 1;

		size += cols * rows;
	}

	vector = GNOME_Gnumeric_Sheet_ValueVector__alloc ();
	vector->_buffer = CORBA_sequence_GNOME_Gnumeric_Value_allocbuf (size);

	/* No memory, return an empty vector */
	if (vector->_buffer == NULL) {
		vector->_length = 0;
		vector->_maximum = 0;

		return vector;
	}

	/*
	 * Fill in the vector
	 */
	for (i = 0, l = ranges; l; l = l->next, i++) {
		Value *value = l->data;
		CellRef a, b;
		int col, row;

		a = value->v_range.cell.a;
		b = value->v_range.cell.b;

		for (col = a.col; col <= b.col; col++)
			for (row = a.row; row < b.row; row++)
				fill_corba_value (&vector->_buffer [i], sheet, col, row);
	}

	range_list_destroy (ranges);
	return vector;
}

static void
cb_range_set_text (Cell *cell, void *data)
{
	sheet_cell_set_text (cell, data);
}

/**
 * range_list_parse:
 * @sheet: Sheet where the range specification is relatively parsed to
 * @range_spec: a range or list of ranges to parse (ex: "A1", "A1:B1,C2,D2:D4")
 * @strict: whether we should be strict during the parsing or allow for trailing garbage
 *
 * Parses a list of ranges, relative to the @sheet and returns a list with the
 * results.
 *
 * Returns a GSList containing Values of type VALUE_CELLRANGE, or NULL on failure
 */
static GSList *
range_list_parse (Sheet *sheet, char const *range_spec, gboolean strict)
{
	char *copy, *range_copy, *r;
	GSList *ranges = NULL;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (range_spec != NULL, NULL);

	range_copy = copy = g_strdup (range_spec);

	while ((r = strtok (range_copy, ",")) != NULL){
		Value *v;

		v = range_parse (sheet, r, strict);
		if (!v){
			range_list_destroy (ranges);
			g_free (copy);
			return NULL;
		}

		ranges = g_slist_prepend (ranges, v);
		range_copy = NULL;
	}

	g_free (copy);

	return ranges;
}

typedef enum {
        RANGE_CREATE_EMPTY_CELLS,  /* call for each cell, creating non-existing cells      */
	RANGE_ONLY_EXISTING_CELLS, /* call for each existing cell                          */
	RANGE_ALL_CELLS            /* call for each cell, do not create non-existing cells */
} range_list_foreach_t;

/**
 * range_list_foreach_full:
 *
 * foreach cell in the range, make sure it exists, and invoke the routine
 * @callback on the resulting cell, passing @data to it
 */
static void
range_list_foreach_full (GSList *ranges, void (*callback)(Cell *cell, void *data),
			 void *data, range_list_foreach_t the_type)
{
	GSList *l;

	{
		static int message_shown;

		if (!message_shown){
			g_warning ("This routine should also iterate "
				   "through the sheets in the ranges");
			message_shown = TRUE;
		}
	}

	for (l = ranges; l; l = l->next){
		Value *value = l->data;
		CellRef a, b;
		int col, row;

		g_assert (value->type == VALUE_CELLRANGE);

		/*
		 * FIXME : Are these ranges normalized ?
		 *         Are they absolute ?
		 */
		a = value->v_range.cell.a;
		b = value->v_range.cell.b;

		for (col = a.col; col <= b.col; col++)
			for (row = a.row; row <= b.row; row++){
				Cell *cell;

				if (the_type == RANGE_CREATE_EMPTY_CELLS)
					cell = sheet_cell_fetch (a.sheet, col, row);
				else
					cell = sheet_cell_get (a.sheet, col, row);
				if (cell || (the_type == RANGE_ALL_CELLS))
					(*callback)(cell, data);
			}
	}
}

static void
range_list_foreach_all (GSList *ranges,
			void (*callback)(Cell *cell, void *data),
			void *data)
{
	range_list_foreach_full (ranges, callback, data, RANGE_CREATE_EMPTY_CELLS);
}

static void
Sheet_range_set_text (PortableServer_Servant servant,
		      const CORBA_char *range,
		      const CORBA_char *text,
		      CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	GSList *ranges;

	verify_range (sheet, range, &ranges);

	range_list_foreach_all (ranges, cb_range_set_text, (char *) text);

	range_list_destroy (ranges);
}

/**
 * range_list_foreach_area:
 * @sheet:     The current sheet.
 * @ranges:    The list of ranges.
 * @callback:  The user function
 * @user_data: Passed to callback intact.
 *
 * Iterates over the ranges calling the callback with the
 * range, sheet and user data set
 **/
static void
range_list_foreach_area (Sheet *sheet, GSList *ranges,
			 void (*callback)(Sheet       *sheet,
					  Range const *range,
					  gpointer     user_data),
			 gpointer user_data)
{
	GSList *l;

	g_return_if_fail (IS_SHEET (sheet));

	for (l = ranges; l; l = l->next) {
		Value *value = l->data;
		Sheet *s;
		Range   range;

		/*
		 * FIXME : Are these ranges normalized ?
		 *         Are they absolute ?
		 */

		setup_range_from_value (&range, value, FALSE);

		s = sheet;
		if (value->v_range.cell.b.sheet)
			s = value->v_range.cell.b.sheet;
		if (value->v_range.cell.a.sheet)
			s = value->v_range.cell.a.sheet;
		callback (s, &range, user_data);
	}
}

static void
range_style_apply_cb (Sheet *sheet, Range const *range, gpointer user_data)
{
	mstyle_ref ((MStyle *)user_data);
	sheet_style_apply_range (sheet, range, (MStyle *)user_data);
}

void
ranges_set_style (Sheet *sheet, GSList *ranges, MStyle *mstyle)
{
	range_list_foreach_area (sheet, ranges,
				 range_style_apply_cb, mstyle);
	mstyle_unref (mstyle);
}

static void
Sheet_range_set_format (PortableServer_Servant servant,
			const CORBA_char *range,
			const CORBA_char *format,
			CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	GSList *ranges;
	MStyle *mstyle;

	verify_range (sheet, range, &ranges);

	mstyle = mstyle_new ();
	mstyle_set_format_text (mstyle, format);
	ranges_set_style (sheet, ranges, mstyle);

	range_list_destroy (ranges);
}

static void
Sheet_range_set_font (PortableServer_Servant servant,
		      const CORBA_char *range,
		      const CORBA_char *font,
		      CORBA_short points,
		      CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	GSList *ranges;
	MStyle *mstyle;

	verify_range (sheet, range, &ranges);

	mstyle = mstyle_new ();
	mstyle_set_font_name (mstyle, font);
	mstyle_set_font_size (mstyle, points);
	ranges_set_style (sheet, ranges, mstyle);

	range_list_destroy (ranges);
}

static void
Sheet_range_set_foreground (PortableServer_Servant servant,
			    const CORBA_char *range,
			    const CORBA_char *color,
			    CORBA_Environment *ev)
{
	Sheet   *sheet = sheet_from_servant (servant);
	GSList  *ranges;
	MStyle  *mstyle;
	GdkColor c;

	verify_range (sheet, range, &ranges);

	gdk_color_parse (color, &c);

	mstyle = mstyle_new ();
	mstyle_set_color (mstyle, MSTYLE_COLOR_FORE,
			  style_color_new (c.red, c.green, c.blue));
	ranges_set_style (sheet, ranges, mstyle);

	range_list_destroy (ranges);
}

static void
Sheet_range_set_background (PortableServer_Servant servant,
			    const CORBA_char *range,
			    const CORBA_char *color,
			    CORBA_Environment *ev)
{
	Sheet   *sheet = sheet_from_servant (servant);
	GSList  *ranges;
	MStyle  *mstyle;
	GdkColor c;

	verify_range (sheet, range, &ranges);

	gdk_color_parse (color, &c);

	mstyle = mstyle_new ();
	mstyle_set_color (mstyle, MSTYLE_COLOR_BACK,
			  style_color_new (c.red, c.green, c.blue));
	ranges_set_style (sheet, ranges, mstyle);

	range_list_destroy (ranges);
}

static void
Sheet_range_set_pattern (PortableServer_Servant servant,
			 const CORBA_char      *range,
			 CORBA_long             pattern,
			 CORBA_Environment     *ev)
{
	Sheet   *sheet = sheet_from_servant (servant);
	GSList  *ranges;
	MStyle  *mstyle;

	verify_range (sheet, range, &ranges);

	mstyle = mstyle_new ();
	mstyle_set_pattern (mstyle, pattern);
	ranges_set_style (sheet, ranges, mstyle);

	range_list_destroy (ranges);
}

static void
Sheet_range_set_alignment (PortableServer_Servant servant,
			   const CORBA_char      *range,
			   CORBA_long             halign,
			   CORBA_long             valign,
			   CORBA_long             orientation,
			   CORBA_boolean          wrap_text,
			   CORBA_Environment     *ev)
{
	Sheet   *sheet = sheet_from_servant (servant);
	GSList  *ranges;
	MStyle  *mstyle;

	verify_range (sheet, range, &ranges);

	mstyle = mstyle_new ();
	mstyle_set_align_h (mstyle, halign);
	mstyle_set_align_v (mstyle, valign);
	mstyle_set_orientation (mstyle, orientation);
	mstyle_set_wrap_text (mstyle, (gboolean) wrap_text);
	ranges_set_style (sheet, ranges, mstyle);

	range_list_destroy (ranges);
}

static CORBA_long
Sheet_max_cols_used (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Sheet *sheet  = sheet_from_servant (servant);
	Range  extent = sheet_get_extent (sheet, FALSE);

	return extent.end.col;
}

static CORBA_long
Sheet_max_rows_used (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Range  extent = sheet_get_extent (sheet, FALSE);

	return extent.end.row;
}

static CORBA_double
Sheet_col_width (PortableServer_Servant servant, CORBA_long col, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	ColRowInfo *ci;

	verify_col_val (col, 0.0);

	ci = sheet_col_get_info (sheet, col);
	return ci->size_pts;
}

static CORBA_double
Sheet_row_height (PortableServer_Servant servant, CORBA_long row, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	ColRowInfo *ri;

	verify_row_val (row, 0.0);

	ri = sheet_row_get_info (sheet, row);
	return ri->size_pts;
}

static void
Sheet_corba_class_init (void)
{
	static int inited;

	if (inited)
		return;
	inited = TRUE;

	gnome_gnumeric_sheet_vepv.GNOME_Gnumeric_Sheet_epv =
		&gnome_gnumeric_sheet_epv;

	gnome_gnumeric_sheet_epv.cursor_set = Sheet_cursor_set;
	gnome_gnumeric_sheet_epv.cursor_move = Sheet_cursor_move;
	gnome_gnumeric_sheet_epv.make_cell_visible = Sheet_make_cell_visible;
	gnome_gnumeric_sheet_epv.is_all_selected = Sheet_is_all_selected;
	gnome_gnumeric_sheet_epv.selection_append = Sheet_selection_append;
	gnome_gnumeric_sheet_epv.selection_append_range = Sheet_selection_append_range;
	gnome_gnumeric_sheet_epv.selection_copy = Sheet_selection_copy;
	gnome_gnumeric_sheet_epv.selection_cut = Sheet_selection_cut;
	gnome_gnumeric_sheet_epv.clear_region = Sheet_clear_region;
	gnome_gnumeric_sheet_epv.clear_region_content = Sheet_clear_region_content;
	gnome_gnumeric_sheet_epv.clear_region_comments = Sheet_clear_region_comments;
	gnome_gnumeric_sheet_epv.clear_region_formats = Sheet_clear_region_formats;

	/*
	 * Cell based routines
	 */
	gnome_gnumeric_sheet_epv.cell_set_value = Sheet_cell_set_value;
	gnome_gnumeric_sheet_epv.cell_get_value = Sheet_cell_get_value;
	gnome_gnumeric_sheet_epv.cell_set_text = Sheet_cell_set_text;
	gnome_gnumeric_sheet_epv.cell_get_text = Sheet_cell_get_text;
	gnome_gnumeric_sheet_epv.cell_set_format = Sheet_cell_set_format;
	gnome_gnumeric_sheet_epv.cell_get_format = Sheet_cell_get_format;
	gnome_gnumeric_sheet_epv.cell_set_font = Sheet_cell_set_font;
	gnome_gnumeric_sheet_epv.cell_get_font = Sheet_cell_get_font;
	gnome_gnumeric_sheet_epv.cell_set_foreground = Sheet_cell_set_foreground;
	gnome_gnumeric_sheet_epv.cell_get_foreground = Sheet_cell_get_foreground;
	gnome_gnumeric_sheet_epv.cell_set_background = Sheet_cell_set_background;
	gnome_gnumeric_sheet_epv.cell_get_background = Sheet_cell_get_background;
	gnome_gnumeric_sheet_epv.cell_set_pattern = Sheet_cell_set_pattern;
	gnome_gnumeric_sheet_epv.cell_get_pattern = Sheet_cell_get_pattern;
	gnome_gnumeric_sheet_epv.cell_set_alignment = Sheet_cell_set_alignment;
	gnome_gnumeric_sheet_epv.cell_get_alignment = Sheet_cell_get_alignment;
	gnome_gnumeric_sheet_epv.cell_set_comment = Sheet_cell_set_comment;
	gnome_gnumeric_sheet_epv.cell_get_comment = Sheet_cell_get_comment;

	/*
	 * Column manipulation
	 */
	gnome_gnumeric_sheet_epv.insert_col = Sheet_insert_col;
	gnome_gnumeric_sheet_epv.delete_col = Sheet_delete_col;
	gnome_gnumeric_sheet_epv.insert_row = Sheet_insert_row;
	gnome_gnumeric_sheet_epv.delete_row = Sheet_delete_row;
	gnome_gnumeric_sheet_epv.shift_cols = Sheet_shift_cols;
	gnome_gnumeric_sheet_epv.shift_rows = Sheet_shift_rows;

	/*
	 * Region based routines
	 */
	gnome_gnumeric_sheet_epv.range_get_values  = Sheet_range_get_values;
	gnome_gnumeric_sheet_epv.range_set_text    = Sheet_range_set_text;
	gnome_gnumeric_sheet_epv.range_set_format  = Sheet_range_set_format;
	gnome_gnumeric_sheet_epv.range_set_font    = Sheet_range_set_font;
	gnome_gnumeric_sheet_epv.range_set_foreground = Sheet_range_set_foreground;
	gnome_gnumeric_sheet_epv.range_set_background = Sheet_range_set_background;
	gnome_gnumeric_sheet_epv.range_set_pattern    = Sheet_range_set_pattern;
	gnome_gnumeric_sheet_epv.range_set_alignment  = Sheet_range_set_alignment;

	gnome_gnumeric_sheet_epv.set_dirty = Sheet_set_dirty;

	/*
	 * Information
	 */
	gnome_gnumeric_sheet_epv.max_cols_used = Sheet_max_cols_used;
	gnome_gnumeric_sheet_epv.max_rows_used = Sheet_max_rows_used;
	gnome_gnumeric_sheet_epv.row_height    = Sheet_row_height;
	gnome_gnumeric_sheet_epv.col_width     = Sheet_col_width;
}

void
sheet_corba_setup (Sheet *sheet)
{
	SheetServant *ss;
	CORBA_Environment ev;
        PortableServer_ObjectId *objid;

	Sheet_corba_class_init ();

	ss = g_new0 (SheetServant, 1);
	ss->servant.vepv = &gnome_gnumeric_sheet_vepv;
	ss->sheet = sheet;

	CORBA_exception_init (&ev);
	POA_GNOME_Gnumeric_Sheet__init ((PortableServer_Servant) ss, &ev);
	objid = PortableServer_POA_activate_object (gnumeric_poa, ss, &ev);
	CORBA_free (objid);

	sheet->priv->corba_server = PortableServer_POA_servant_to_reference (gnumeric_poa, ss, &ev);

	CORBA_exception_free (&ev);
}

void
sheet_corba_shutdown (Sheet *sheet)
{
	CORBA_Environment ev;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->priv->corba_server != NULL);

	g_warning ("Should release all the corba resources here");

	CORBA_exception_init (&ev);
	PortableServer_POA_deactivate_object (gnumeric_poa, sheet->priv->corba_server, &ev);
	CORBA_exception_free (&ev);
}

