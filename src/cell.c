#include <config.h>
#include <gnome.h>
#include <gnumeric.h>

void
sheet_cell_foreach_range (Sheet *sheet,
			  int start_col, int start_row,
			  int end_col, int end_row,
			  CellCallback callback,
			  void *closure)
{
	GList *col = sheet->cols_info;

	for (; col; col = col->next){
		ColRowInfo *ci = l->data;

		if (ci->pos < start_col)
			continue;
		if (ci->pos > end_col)
			break;

		for (row = (GList *) col->data; row; row = row->data){
			ColRowInfo *ri = l->data;

			if (ri->pos < start_row)
				continue;

			if (ri->pos > end_row)
				break;

			(*callback)(sheet, (Cell *) ri->data);
		}
	}
}

Cell *
sheet_cell_new (Sheet *sheet, int col, int row)
{
	Cell *cell = g_new0 (cell, 1);

	cell->col = sheet_col_find (sheet, col);
	cell->row = sheet_row_find (sheet, row);

	cell->style = sheet_compute_style (sheet, col, row);
	return cell;
}

Cell *
sheet_cell_new_with_text (Sheet *sheet, int col, int row, char *text)
{
	Cell *cell;
	GdkFont *font;
	
	cell = sheet_cell_new (sheet, col, row, text);
	cell->text = g_strdup (text);
	font = cell->style->font->font;
	cell->width = gdk_text_width (font, text, strlen (text));
	cell->height = font->ascent + font->descent;
	
	return cell;
}
