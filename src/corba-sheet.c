/*
 * corba-sheet.c: The implementation of the Sheet CORBA interfaces
 * defined by Gnumeric
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <libgnorba/gnome-factory.h>
#include <gnome.h>
#include "sheet.h"
#include "gnumeric.h"
#include "Gnumeric.h"
#include "corba.h"
#include "utils.h"
#include "ranges.h"

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
        if (!corba_range_parse (sheet,range,l)) { out_of_range (ev); return; }

#define verify_range_val(sheet,range,l,val) \
        if (!corba_range_parse (sheet,range,l)) { out_of_range (ev); return (val); }

static POA_GNOME_Gnumeric_Sheet__vepv gnome_gnumeric_sheet_vepv;
static POA_GNOME_Gnumeric_Sheet__epv gnome_gnumeric_sheet_epv;

typedef struct {
	POA_GNOME_Gnumeric_Sheet servant;
	Sheet *sheet;
} SheetServant;

static void
out_of_range (CORBA_Environment *ev)
{
	CORBA_exception_set (ev, CORBA_USER_EXCEPTION, ex_GNOME_Gnumeric_Sheet_OutOfRange, NULL);
}

/*
 * Parses a list of ranges, returns a GList containing pointers to
 * Value structures.  Sets the return_list pointer to point to a
 * a listof those values.
 *
 * Returns TRUE on successfully parsing the string, FALSE otherwise
 */
static gboolean
corba_range_parse (Sheet *sheet, const char *range_spec, GSList **return_list)
{
	GSList *list;
	
	list = range_list_parse (sheet, range_spec);
	if (list){
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

	sheet_cursor_set (sheet, base_col, base_row, start_col, start_row, end_col, end_row);
}

static void
Sheet_cursor_move (PortableServer_Servant servant, const CORBA_long col, const CORBA_long row, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	
	verify_col (col);
	verify_row (row);

	sheet_cursor_move (sheet, col, row);
}

static void
Sheet_make_cell_visible (PortableServer_Servant servant, const CORBA_long col, const CORBA_long row, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_col (col);
	verify_row (row);

	sheet_make_cell_visible (sheet, col, row);
}

static void
Sheet_select_all (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
       
	sheet_select_all (sheet);
}

static CORBA_boolean
Sheet_is_all_selected (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	return sheet_is_all_selected (sheet);
}

static void
Sheet_selection_reset (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	sheet_selection_reset (sheet);
}

static void
Sheet_selection_append (PortableServer_Servant servant,
			const CORBA_long col, const CORBA_long row,
			CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_col (col);
	verify_row (row);
	
	sheet_selection_append (sheet, col, row);
}

static void
Sheet_selection_append_range (PortableServer_Servant servant,
			      const CORBA_long start_col, const CORBA_long start_row,
			      const CORBA_long end_col, const CORBA_long end_row,
			      CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_region (start_col, start_row, end_col, end_row);

	sheet_selection_append_range (sheet, start_col, start_row,
				      start_col, start_row,
				      end_col, end_row);
}

static void
Sheet_selection_copy (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	sheet_selection_copy (sheet);
}

static void
Sheet_selection_cut (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	sheet_selection_cut (sheet);
}

static void
Sheet_selection_paste (PortableServer_Servant servant,
		       const CORBA_long dest_col, const CORBA_long dest_row,
		       const CORBA_long paste_flags, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_col (dest_col);
	verify_row (dest_row);
	
	sheet_selection_paste (sheet, dest_col, dest_row, paste_flags, 0);
}

static void
Sheet_clear_region (PortableServer_Servant servant,
		    const CORBA_long start_col, const CORBA_long start_row,
		    const CORBA_long end_col, const CORBA_long end_row,
		    CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_region (start_col, start_row, end_col, end_row);

	sheet_clear_region (sheet, start_col, start_row, end_col, end_row);
}

static void
Sheet_clear_region_content (PortableServer_Servant servant,
			    const CORBA_long start_col, const CORBA_long start_row,
			    const CORBA_long end_col, const CORBA_long end_row,
			    CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_region (start_col, start_row, end_col, end_row);
	sheet_clear_region_content (sheet, start_col, start_row, end_col, end_row);
}

static void
Sheet_clear_region_comments (PortableServer_Servant servant,
			     const CORBA_long start_col, const CORBA_long start_row,
			     const CORBA_long end_col, const CORBA_long end_row,
			     CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_region (start_col, start_row, end_col, end_row);
	sheet_clear_region_comments (sheet, start_col, start_row, end_col, end_row);
}

static void
Sheet_clear_region_formats (PortableServer_Servant servant,
			    const CORBA_long start_col, const CORBA_long start_row,
			    const CORBA_long end_col, const CORBA_long end_row,
			    CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_region (start_col, start_row, end_col, end_row);
	sheet_clear_region_formats (sheet, start_col, start_row, end_col, end_row);
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

		parse_cell_name (value->_u.cell_range.cell_a, &a.col, &a.row);
		parse_cell_name (value->_u.cell_range.cell_b, &b.col, &b.row);
		a.sheet = sheet;
		b.sheet = sheet;
		a.col_relative = 0;
		b.col_relative = 0;
		a.row_relative = 0;
		b.row_relative = 0;
		v = value_new_cellrange (&a, &b);
		break;
	}
		
	case GNOME_Gnumeric_VALUE_ARRAY:
		v = NULL;
		g_error ("FIXME: Implement me");
		break;

	default:
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION, ex_GNOME_Gnumeric_Sheet_InvalidValue, NULL);
		return;
	}

	cell_set_value (cell, v);
}

static void
fill_corba_value (GNOME_Gnumeric_Value *value, Sheet *sheet, CORBA_long col, CORBA_long row)
{
	Cell *cell;

	g_assert (value != NULL);
	g_assert (sheet != NULL);

	cell = sheet_cell_get (sheet, col, row);
	if (cell && cell->value){
		switch (cell->value->type){
		case VALUE_EMPTY:
			value->_d = GNOME_Gnumeric_VALUE_EMPTY;
			break;

		case VALUE_BOOLEAN:
			value->_d = GNOME_Gnumeric_VALUE_BOOLEAN;
			value->_u.v_bool = cell->value->v.v_bool;
			break;
			
		case VALUE_ERROR:
			value->_d = GNOME_Gnumeric_VALUE_ERROR;
			value->_u.error = CORBA_string_dup (cell->value->v.error.mesg->str);
			break;
			
		case VALUE_STRING:
			value->_d = GNOME_Gnumeric_VALUE_STRING;
			value->_u.str = CORBA_string_dup (cell->value->v.str->str);
			break;
			
		case VALUE_INTEGER:
			value->_d = GNOME_Gnumeric_VALUE_INTEGER;
			value->_u.v_int = cell->value->v.v_int;
			break;
			
		case VALUE_FLOAT:
			value->_d = GNOME_Gnumeric_VALUE_FLOAT;
			value->_u.v_float = cell->value->v.v_float;
			break;
				
		case VALUE_CELLRANGE: {
			char *a, *b;
			
			a = cellref_name (&cell->value->v.cell_range.cell_a, col, row);
			b = cellref_name (&cell->value->v.cell_range.cell_b, col, row);

			value->_d = GNOME_Gnumeric_VALUE_CELLRANGE;
			value->_u.cell_range.cell_a = CORBA_string_dup (a);
			value->_u.cell_range.cell_b = CORBA_string_dup (b);
			g_free (a);
			g_free (b);
			break;
		}
				
		case VALUE_ARRAY:
			g_error ("FIXME: Implement me");
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
		     const CORBA_char * text, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Cell *cell;

	verify_col (col);
	verify_row (row);
	
	cell = sheet_cell_fetch (sheet, col, row);
	cell_set_text (cell, text);
}

static CORBA_char *
Sheet_cell_get_text (PortableServer_Servant servant,
		     const CORBA_long col,
		     const CORBA_long row,
		     CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Cell *cell;

	verify_col_val (col, NULL);
	verify_row_val (row, NULL);
	
	cell = sheet_cell_get (sheet, col, row);
	if (cell){
		char *str;

		str = cell_get_text (cell);
		return CORBA_string_dup (str);
	} else {
		return CORBA_string_dup ("");
	}
}

static void
Sheet_cell_set_formula (PortableServer_Servant servant,
			const CORBA_long col,
			const CORBA_long row,
			const CORBA_char * formula,
			CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Cell *cell;

	verify_col (col);
	verify_row (row);
	
	cell = sheet_cell_fetch (sheet, col, row);
	cell_set_formula (cell, formula);
}

static void
Sheet_cell_set_format (PortableServer_Servant servant,
		       const CORBA_long col,
		       const CORBA_long row,
		       const CORBA_char * format,
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
	Sheet *sheet = sheet_from_servant (servant);
	Cell *cell;

	verify_col_val (col, NULL);
	verify_row_val (row, NULL);
	
	cell = sheet_cell_get (sheet, col, row);
	if (cell){
		return CORBA_string_dup (cell->style->format->format);
	} else {
		return CORBA_string_dup ("");
	}
}

static void
Sheet_cell_set_font (PortableServer_Servant servant,
		     const CORBA_long col,
		     const CORBA_long row,
		     const CORBA_char * font,
		     const CORBA_double points,
		     CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	StyleFont *style_font;
	Cell *cell;

	verify_col (col);
	verify_row (row);
	
	cell = sheet_cell_fetch (sheet, col, row);

	style_font = style_font_new (
		font, points, sheet->last_zoom_factor_used,
		cell->style->font->is_bold,
		cell->style->font->is_italic);

	if (style_font)
		cell_set_font_from_style (cell, style_font);
}

static CORBA_char *
Sheet_cell_get_font (PortableServer_Servant servant, const CORBA_long col, const CORBA_long row, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Cell *cell;

	verify_col_val (col, NULL);
	verify_row_val (row, NULL);
	
	cell = sheet_cell_get (sheet, col, row);
	if (cell){
		return CORBA_string_dup (cell->style->font->font_name);
	} else {
		return CORBA_string_dup ("");
	}
}

static void
Sheet_cell_set_comment (PortableServer_Servant servant,
			const CORBA_long col, const CORBA_long row,
			const CORBA_char * comment, CORBA_Environment *ev)
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
			   const CORBA_char * color, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Cell *cell;
	GdkColor c;

	verify_col (col);
	verify_row (row);
	
	gdk_color_parse (color, &c);
	cell = sheet_cell_fetch (sheet, col, row);

	cell_set_foreground (cell, c.red, c.green, c.blue);
}

static CORBA_char *
Sheet_cell_get_foreground (PortableServer_Servant servant,
			   const CORBA_long col, const CORBA_long row,
			   CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	g_error ("Foreground for sheet %p", sheet);

	return NULL;
}

static void
Sheet_cell_set_background (PortableServer_Servant servant,
			   const CORBA_long col, const CORBA_long row,
			   const CORBA_char * color, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Cell *cell;
	GdkColor c;

	verify_col (col);
	verify_row (row);
	
	gdk_color_parse (color, &c);
	cell = sheet_cell_fetch (sheet, col, row);

	cell_set_background (cell, c.red, c.green, c.blue);
}

static CORBA_char *
Sheet_cell_get_background (PortableServer_Servant servant,
			   const CORBA_long col, const CORBA_long row,
			   CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	g_error ("Background");

	return NULL;
}

static void
Sheet_cell_set_pattern (PortableServer_Servant servant,
			const CORBA_long col, const CORBA_long row,
			const CORBA_long pattern, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Cell *cell;

	verify_col (col);
	verify_row (row);
	
	cell = sheet_cell_fetch (sheet, col, row);
	cell_set_pattern (cell, pattern);
}

static CORBA_long
Sheet_cell_get_pattern (PortableServer_Servant servant,
			const CORBA_long col, const CORBA_long row,
			CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Cell *cell;
	
	verify_col_val (col, 0);
	verify_row_val (row, 0);
	
	cell = sheet_cell_get (sheet, col, row);
	if (cell)
		return cell->style->pattern;
	else
		return 0;
}

static void
Sheet_cell_set_alignment (PortableServer_Servant servant,
			  const CORBA_long col, const CORBA_long row,
			  const CORBA_long halign, const CORBA_long valign,
			  const CORBA_long orientation, const CORBA_boolean auto_return,
			  CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Cell *cell;
	int h, v;
	
	verify_col (col);
	verify_row (row);
	
	cell = sheet_cell_fetch (sheet, col, row);
		
	switch (halign){
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
		
	default:
		h = HALIGN_GENERAL;
	}

	switch (valign){
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
	
	return cell_set_alignment (cell, h, v, orientation, auto_return);
}

static void
Sheet_cell_get_alignment (PortableServer_Servant servant,
			  const CORBA_long col, const CORBA_long row,
			  CORBA_long * halign, CORBA_long * valign,
			  CORBA_long * orientation, CORBA_boolean * auto_return,
			  CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	Cell *cell;
	
	verify_col (col);
	verify_row (row);

	cell = sheet_cell_get (sheet, col, row);
	if (cell){
		switch (cell->style->halign){
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
		default:
			g_assert_not_reached ();
		}

		switch (cell->style->valign){
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
		*orientation = cell->style->orientation;
		*auto_return = cell->style->fit_in_cell;
	} else {
		*halign = GNOME_Gnumeric_Sheet_HALIGN_GENERAL;
		*valign = GNOME_Gnumeric_Sheet_VALIGN_CENTER;
		*orientation = 0;
		*auto_return = 0;
		return;
	}
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
	sheet_insert_col (sheet, col, count);
}

static void
Sheet_delete_col (PortableServer_Servant servant,
		  const CORBA_long col, const CORBA_long count,
		  CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_col (col);
	sheet_delete_col (sheet, col, count);
}

static void
Sheet_insert_row (PortableServer_Servant servant,
		  const CORBA_long row, const CORBA_long count,
		  CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_row (row);
	sheet_insert_row (sheet, row, count);
}

static void
Sheet_delete_row (PortableServer_Servant servant,
		  const CORBA_long row, const CORBA_long count,
		  CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	verify_row (row);
	sheet_delete_row (sheet, row, count);
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

	sheet_shift_rows (sheet, col, start_row, end_row, count);
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

	sheet_shift_cols (sheet, col, start_row, end_row, count);
}

static GNOME_Gnumeric_Sheet_ValueVector *
Sheet_range_get_values (PortableServer_Servant servant, const CORBA_char *range, CORBA_Environment *ev)
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

		a = value->v.cell_range.cell_a;
		b = value->v.cell_range.cell_b;

		cols = abs (b.col - a.col) + 1;
		rows = abs (b.row - a.row) + 1;

		size += cols * rows;
	}

	vector = GNOME_Gnumeric_Sheet_ValueVector__alloc ();
	vector->_buffer = CORBA_sequence_GNOME_Gnumeric_Value_allocbuf (size);

	/* No memory, return an empty vector */
	if (vector->_buffer == NULL){
		vector->_length = 0;
		vector->_maximum = 0;

		return vector;
}

	/*
	 * Fill in the vector
	 */
	for (i = 0, l = ranges; l; l = l->next, i++){
		Value *value = l->data;
		CellRef a, b;
		int col, row;
		
		a = value->v.cell_range.cell_a;
		b = value->v.cell_range.cell_b;

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
	cell_set_text (cell, data);
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

static void
cb_range_set_formula (Cell *cell, void *data)
{
	cell_set_formula (cell, data);
}

static void
Sheet_range_set_formula (PortableServer_Servant servant,
			 const CORBA_char *range,
			 const CORBA_char *formula,
			 CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	GSList *ranges;

	verify_range (sheet, range, &ranges);

	range_list_foreach_all (ranges, cb_range_set_formula, (char *)formula);
	
	range_list_destroy (ranges);
}

static void
cb_range_set_format (Cell *cell, void *data)
{
	cell_set_format (cell, data);
}

static void
Sheet_range_set_format (PortableServer_Servant servant,
			const CORBA_char *range,
			const CORBA_char *format,
			CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	GSList *ranges;
	Style *style;

	verify_range (sheet, range, &ranges);

	cell_freeze_redraws ();

	/*
	 * Create a style for the region
	 */
	style = style_new_empty ();
	style->valid_flags = STYLE_FORMAT;
	style->format = style_format_new (format);

	/* Apply the style */
	range_set_style (ranges, style);
	range_list_foreach (ranges, cb_range_set_format, (char *) format);

	cell_thaw_redraws ();
	
	range_list_destroy (ranges);
}

static CORBA_long
Sheet_max_cols_used (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	return sheet->max_col_used;
}

static CORBA_long
Sheet_max_rows_used (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);

	return sheet->max_row_used;
}

static CORBA_double
Sheet_col_width (PortableServer_Servant servant, CORBA_long col, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	ColRowInfo *ci;
	
	verify_col_val (col, 0.0);

	ci = sheet_col_get_info (sheet, col);
	return ci->units;
}

static CORBA_double
Sheet_row_height (PortableServer_Servant servant, CORBA_long row, CORBA_Environment *ev)
{
	Sheet *sheet = sheet_from_servant (servant);
	ColRowInfo *ri;

	verify_row_val (row, 0.0);

	ri = sheet_row_get_info (sheet, row);
	return ri->units;
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
	gnome_gnumeric_sheet_epv.select_all = Sheet_select_all;
	gnome_gnumeric_sheet_epv.is_all_selected = Sheet_is_all_selected;
	gnome_gnumeric_sheet_epv.selection_reset = Sheet_selection_reset;
	gnome_gnumeric_sheet_epv.selection_append = Sheet_selection_append;
	gnome_gnumeric_sheet_epv.selection_append_range = Sheet_selection_append_range;
	gnome_gnumeric_sheet_epv.selection_copy = Sheet_selection_copy;
	gnome_gnumeric_sheet_epv.selection_cut = Sheet_selection_cut;
	gnome_gnumeric_sheet_epv.selection_paste = Sheet_selection_paste;
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
	gnome_gnumeric_sheet_epv.cell_set_formula = Sheet_cell_set_formula;
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
	gnome_gnumeric_sheet_epv.range_get_values = Sheet_range_get_values;
	gnome_gnumeric_sheet_epv.range_set_text = Sheet_range_set_text;
	gnome_gnumeric_sheet_epv.range_set_formula = Sheet_range_set_formula;
	gnome_gnumeric_sheet_epv.range_set_format = Sheet_range_set_format;

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
	sheet->corba_server = PortableServer_POA_servant_to_reference (gnumeric_poa, ss, &ev);
	
	CORBA_exception_free (&ev);
}

void
sheet_corba_shutdown (Sheet *sheet)
{
	CORBA_Environment ev;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->corba_server != NULL);

	g_warning ("Should release all the corba resources here");

	CORBA_exception_init (&ev);
	PortableServer_POA_deactivate_object (gnumeric_poa, sheet->corba_server, &ev);
	CORBA_exception_free (&ev);
}
