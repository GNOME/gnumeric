/*
 * dialog-stf-preview.c : by utilizing the stf-parse engine this unit can
 *                        render sheet previews on the gnomecanvas and offers
 *                        functions for making this preview more interactive.
 *
 * Copyright (C) Almer S. Tigelaar <almer@gnome.org>
 *
 * NOTES :
 * 1) This is rather sucky, it works, reasonably fast, but it's not ideal.
 * 2) There is a distinct difference between a formatted and non-formatted preview
 *    non-formatted previews are somewhat faster.
 *
 * MEMORY MANAGEMENT NOTES AND "HOW IT WORKS" :
 * In fact the preview does its work trough the stf_preview_render call
 * this takes a GSList as argument which on its turns also contains GSLists
 * which hold strings, so :
 *
 * GList (Main)
 *  |
 *  |--- GList (Sub) --> Contains strings as GList->Data
 *  |
 *  |--- GList (Sub) --> Contains strings as GList->Data
 *
 */
#include <config.h>
#include "format.h"
#include "number-match.h"
#include "value.h"
#include "../portability.h"

#include "dialog-stf.h"
#include "dialog-stf-preview.h"

/******************************************************************************************************************
 * BASIC DRAWING FUNCTIONS
 ******************************************************************************************************************/

/**
 * stf_preview_draw_text
 * @group : group to add a new canvas item to
 * @text : text to render
 * @font : font to use
 * @color : render color
 * @x : x position to place text
 * @y : y position to place text
 *
 * will place @text at the @x, @y coordinates
 *
 * returns : the width of the rendered text
 **/
static double
stf_preview_draw_text (GnomeCanvasGroup *group, char *text, GdkFont *font, char *color, double x, double y)
{
	GnomeCanvasText *canvastext;
	double textwidth;

	g_return_val_if_fail (group != NULL, 0);
	g_return_val_if_fail (text != NULL, 0);

	canvastext = GNOME_CANVAS_TEXT (gnome_canvas_item_new (group,
							       gnome_canvas_text_get_type (),
							       "text", text,
							       "font_gdk", font,
							       "fill_color", color,
							       "anchor", GTK_ANCHOR_NW,
							       "x", x,
							       "y", y,
							       NULL));
	gtk_object_get (GTK_OBJECT (canvastext),
			"text_width", &textwidth,
			NULL);

	return textwidth;
}

/**
 * stf_preview_draw_line
 * @group : the gnome canvas group to add the line to
 * @color : color of the line
 * @x1 : first x coordinate
 * @y1 : first y coordinate
 * @x2 : second x coordinate
 * @y2 : second y coordinate
 *
 * Draws a line between two points
 *
 * returns : nothing
 **/
static void
stf_preview_draw_line (GnomeCanvasGroup *group, char *color, double x1, double y1, double x2, double y2)
{
	GnomeCanvasPoints *points = gnome_canvas_points_new (2);

	points->coords[0] = x1;
	points->coords[1] = y1;
	points->coords[2] = x2;
	points->coords[3] = y2;

	/* Render bottom and topline of the row */
	gnome_canvas_item_new (group,
			       gnome_canvas_line_get_type (),
			       "fill_color", color,
			       "points", points,
			       "width_pixels", 1,
			       NULL);

	gnome_canvas_points_unref (points);
}

/**
 * stf_preview_draw_box
 * @group : gnomecanvasgroup to add the box too
 * @color : fill color of the box
 * @x1 : left coordinate
 * @y1 : top coordinate
 * @x2 : right coordinate
 * @y2 : bottom coordinate
 *
 * Draws a box with @color as color and a transparant outline
 *
 * returns : nothing
 **/
static void
stf_preview_draw_box (GnomeCanvasGroup *group, char *color, double x1, double y1, double x2, double y2)
{

	gnome_canvas_item_new (group,
			       gnome_canvas_rect_get_type (),
			       "x1", x1, "y1", y1,
			       "x2", x2, "y2", y2,
			       "width_pixels", (int) 0,
			       "fill_color", color,
			       "outline_color", NULL,
			       NULL);
}

/******************************************************************************************************************
 * INTERNAL HELPER FUNCTIONS
 ******************************************************************************************************************/

/**
 * stf_preview_get_table_pixel_width
 * @renderdata : struct containing rendering info
 *
 * will return the width in pixels of the table
 *
 * returns : width of table
 **/
static double
stf_preview_get_table_pixel_width (RenderData_t *renderdata)
{
	int tablewidth = 0;
	guint i;

	for (i = 0; i < renderdata->colwidths->len; i++) {

		if (i > SHEET_MAX_COLS)
			break;

		tablewidth += g_array_index (renderdata->colwidths, int, i);
	}

	return (renderdata->charwidth * tablewidth) + (CELL_HPAD * renderdata->colwidths->len);
}

/**
 * stf_preview_get_table_pixel_height
 * @renderdata : struct containing rendering info
 * @rowcount : number of rows to be rendered
 *
 * will return the height in pixels of the table
 *
 * returns : height of table
 **/
static double
stf_preview_get_table_pixel_height (RenderData_t *renderdata, int rowcount)
{

	return (renderdata->charheight + CELL_VPAD) * rowcount;
}


/******************************************************************************************************************
 * ADVANCED DRAWING FUNCTIONS
 ******************************************************************************************************************/

/**
 * stf_preview_draw_grid
 * @renderdata : struct containing rendering info
 * @rowcount : number of rows to be rendered
 * @colcount : number of columns to be rendered
 *
 * Will draw a grid on a canvas
 *
 * returns : nothing
 **/
static void
stf_preview_draw_grid (RenderData_t *renderdata, int rowcount, int colcount)
{
	double rowheight   = renderdata->charheight + CELL_VPAD;
	double tableheight = stf_preview_get_table_pixel_height (renderdata, rowcount);
	double tablewidth  = stf_preview_get_table_pixel_width (renderdata);
	double xpos        = 0;
	double ypos        = 0;
	char *tempcolor;
	int i;

	if (renderdata->gridgroup != NULL)
		gtk_object_destroy (GTK_OBJECT (renderdata->gridgroup));

	renderdata->gridgroup = GNOME_CANVAS_GROUP (gnome_canvas_item_new (gnome_canvas_root (GNOME_CANVAS (renderdata->canvas)),
									   gnome_canvas_group_get_type(),
									   "x", 0.0,
									   "y", 0.0,
									   NULL));

	/* Color the first row */
	stf_preview_draw_box (renderdata->gridgroup, CAPTION_COLOR, 0, 0, tablewidth, rowheight);

	/* Color the rest of the table */
	stf_preview_draw_box (renderdata->gridgroup, ROW_COLOR, 0, rowheight, tablewidth, tableheight);

	/* now color the active column */
	if (renderdata->activecolumn != -1) {
		double colleft = 0;
		double colright = 0;

		for (i = 0; i < renderdata->activecolumn; i++) {

			colleft += renderdata->charwidth * g_array_index (renderdata->colwidths, int, i) + CELL_HPAD;
		}
		colright = colleft + (renderdata->charwidth * g_array_index (renderdata->colwidths, int, renderdata->activecolumn)) + CELL_HPAD;

		stf_preview_draw_box (renderdata->gridgroup, CAPTION_COLOR_ACTIVE, colleft, 0, colright, rowheight);
		stf_preview_draw_box (renderdata->gridgroup, ROW_COLOR_ACTIVE, colleft, rowheight, colright, tableheight);
	}

	tempcolor = CAPTION_LINE_COLOR;
	/* horizontal lines */
	for (i = 0; i < rowcount + 1; i++) {

		if (i == 2)
			tempcolor = LINE_COLOR;

		stf_preview_draw_line (renderdata->gridgroup, tempcolor, 0, ypos, tablewidth, ypos);

		ypos += rowheight;
	}

	/* vertical lines */
	i = 0;
	while (1) {

		stf_preview_draw_line (renderdata->gridgroup, CAPTION_LINE_COLOR, xpos, 0, xpos, rowheight);
		stf_preview_draw_line (renderdata->gridgroup, LINE_COLOR, xpos, rowheight, xpos, tableheight);

		if (i == colcount + 1)
			break;

		xpos += (renderdata->charwidth * g_array_index (renderdata->colwidths, int, i)) + CELL_HPAD;

		i++;
	}
	stf_preview_draw_line (renderdata->gridgroup, CAPTION_LINE_COLOR, xpos, 0, xpos, rowheight);

	gnome_canvas_item_lower_to_bottom ( (GnomeCanvasItem *) renderdata->gridgroup);

	return;
}

/**
 * stf_preview_render_row
 * @renderdata : a renderdata struct
 * @rowy : the vertical position on which to draw the top of the row
 * @row : the row data itself
 * @colcount : the number of columns that must be rendered (if necessary empty) regardless of the contents of @row
 *
 * Basically renders @row's text on the canvas
 *
 * returns : the bottom position of the row
 **/
static double
stf_preview_render_row (RenderData_t *renderdata, double rowy, GList *row, int colcount)
{
	GList *iterator = row;
	double xpos = 0.0;
	double textwidth = 0;
	int col = 0;

	if (iterator == NULL) { /* empty row */

		return rowy + CELL_VPAD + renderdata->charheight;
	}


	for (col = 0; col <= colcount; col++)  {
		int widthwanted;

		if (iterator != NULL && iterator->data != NULL) {
			char *text = NULL;

			/*
			 * XFREE86 Overflow protection
			 *
			 * There is a bug in XFree86 (at least up until 4.0.3)
			 * which takes down the whole server if very large strings
			 * are drawn on a local display. We therefore simply truncate
			 * the string if it is 'too large' to display.
			 */
			if (strlen (iterator->data) > X_OVERFLOW_PROTECT - 1)
				text = g_strndup (iterator->data, X_OVERFLOW_PROTECT - 1);
			
			/* In case the active color differs from the inactive color
			 * this code can be activated
			 *
			 * if (col == renderdata->activecolumn && rowy != 0)
			 *	 color = text_color_active;
			 * else
			 *	 color = text_color;
			 */
			
			textwidth = stf_preview_draw_text (renderdata->group,
							   text ? text : iterator->data,
							   renderdata->font,
							   TEXT_COLOR,
							   xpos + (CELL_HPAD / 2),
							   rowy + (CELL_VPAD / 2));

			if (text)
				g_free (text);
		}

		widthwanted = renderdata->charwidth * g_array_index (renderdata->colwidths, int, col);

		if (textwidth > widthwanted)
			xpos += textwidth;
		else
			xpos += widthwanted;


		xpos += CELL_HPAD;

		iterator = g_list_next (iterator);
	}

	return rowy + CELL_VPAD + renderdata->charheight;
}

/**
 * stf_preview_format_recalc_colwidths
 * @renderdata : renderdata struct
 * @data : a list containing lists with strings
 * @colcount : number of items in each list in @data
 *
 * This routine will iterate trough the list and calculate the ACTUAL widths
 * the actual widths are those as seen on screen by the user.
 * The reason for doing this is that the widths that are manually set can
 * differ from the widths of the formatted strings if the the strings have
 * to be rendered formatted (@renderdata->formatted == TRUE).
 *
 * returns : nothing. (swaps renderdata->temp and renderdata->colwidths);
 **/
static void
stf_preview_format_recalc_colwidths (RenderData_t *renderdata, GList *data, int colcount)
{
	GArray *newwidths;
	GList *iterator;
	gint i;
	gint *widths = g_alloca ((colcount + 1) * sizeof (gint));

	for (i = 0; i <= colcount; i++) {
		widths[i] = g_array_index (renderdata->colwidths, int, i);
	}

	iterator = data;
	while (iterator) {
		GList *subiterator = iterator->data;
		int col;

		for (col = 0; col <= colcount; col++)  {
			int width;

			if (!subiterator || !subiterator->data) {

				subiterator = g_list_next (subiterator);
				continue;
			}

			/* New width calculation */
			width = gdk_string_width (renderdata->font, subiterator->data) / renderdata->charwidth;

			if (width > widths[col])
				widths[col] = width;

			subiterator = g_list_next (subiterator);
		}

		iterator = g_list_next (iterator);
	}

	newwidths = g_array_new (FALSE, FALSE, sizeof (int));

	for (i = 0; i <= colcount; i++) {

		g_array_append_val (newwidths, widths[i]);
	}

	/* We were called before, free our old 'actual' columnwidths */
	if (renderdata->temp != 0)
		g_array_free (renderdata->temp, TRUE);

	renderdata->temp = renderdata->colwidths;
	renderdata->colwidths = newwidths;
}


/**
 * stf_preview_format_line
 * @renderdata : renderdata struct
 * @data : a list containing strings
 * @colcount : number of items in @list
 *
 * formats a single list of strings
 *
 * returns : nothing
 **/
static void
stf_preview_format_line (RenderData_t *renderdata, GList *data, int colcount)
{
	int col;
	GList *iterator = data;

	for (col = 0; col <= colcount; col++)  {
		Value *value;
		StyleFormat *sf;
		char *celltext;

		if (!iterator || !iterator->data) {
			iterator = g_list_next (iterator);
			continue;
		}
		
		sf = g_ptr_array_index (renderdata->colformats, col);
		
		/* Formatting */
		if (NULL == (value = format_match (iterator->data, sf, NULL)))
			value = value_new_string (iterator->data);

		celltext = format_value (sf, value, NULL, -1);

		value_release (value);

		/* Replacement of old data */
		g_free (iterator->data);
		iterator->data = celltext;

		iterator = g_list_next (iterator);
	}
}

/**
 * stf_preview_render
 * @renderdata : a renderdata struct
 * @list : a list containing the rows of of data to display
 * @rowcount : number of rows in @list
 * @colcount : number of cols in @list
 *
 * This will render a preview on the canvas.
 *
 * returns : nothing
 **/
void
stf_preview_render (RenderData_t *renderdata, GList *list, int rowcount, int colcount)
{
	GnomeCanvasRect *centerrect;
	GList *iterator;
	GList *captions;
	GArray *dummy;
	int i;
	double ypos = 0.0;

	g_return_if_fail (renderdata != NULL);
	g_return_if_fail (renderdata->canvas != NULL);
	g_return_if_fail (list != NULL);

	if (renderdata->group != NULL)
		gtk_object_destroy (GTK_OBJECT (renderdata->group));

	if (rowcount < 1)
		return;

	/*
	 * Don't display more then the maximum amount of columns
	 * in a sheet
	 */
	if (colcount > SHEET_MAX_COLS)
		colcount = SHEET_MAX_COLS;

	renderdata->group = GNOME_CANVAS_GROUP (gnome_canvas_item_new (gnome_canvas_root (GNOME_CANVAS (renderdata->canvas)),
							   gnome_canvas_group_get_type(),
							   "x", 0.0,
							   "y", 0.0,
							   NULL));

	if (renderdata->formatted)
		stf_preview_format_recalc_colwidths (renderdata, list, colcount);

	/* Generate column captions and prepend them */
	captions = NULL;
	for (i = 0; i <= colcount; i++) {
		char *text = g_strdup_printf (_(COLUMN_CAPTION), i);

		captions = g_list_append (captions, text);
	}

	ypos = stf_preview_render_row (renderdata, ypos, captions, colcount);

	/* Render line by line */
	iterator = list;
	i = 1;
 	while (iterator) {
		if (i >= renderdata->startrow) {
			if (renderdata->formatted && iterator->data != NULL)
				stf_preview_format_line (renderdata, iterator->data, colcount);
			ypos = stf_preview_render_row (renderdata, ypos, iterator->data, colcount);
		}

		iterator = g_list_next (iterator);
		i++;
	}

	stf_preview_draw_grid (renderdata, rowcount, colcount);

	centerrect = GNOME_CANVAS_RECT (gnome_canvas_item_new (renderdata->group,
							       gnome_canvas_rect_get_type (),
							       "x1", 0,	 "y1", 0,
							       "x2", 10, "y2", 10,
							       "width_pixels", (int) 0,
							       "fill_color", "white",
							       NULL));

	stf_dialog_set_scroll_region_and_prevent_center (renderdata->canvas,
							 centerrect,
							 stf_preview_get_table_pixel_width (renderdata),
							 stf_preview_get_table_pixel_height (renderdata, rowcount));

	iterator = captions;
	while (iterator) {
		g_free (iterator->data);
		iterator = g_list_next (iterator);
	}
	g_list_free (captions);

	/* Swap the actual column widths with the set columnwidths */
	if (renderdata->formatted) {
		dummy                 = renderdata->temp;
		renderdata->temp      = renderdata->colwidths;
		renderdata->colwidths = dummy;
	}

	/* Free all the data */
	iterator = list;
	while (iterator) {
		GList *subiterator = iterator->data;
		
		while (subiterator) {
			g_free ((char *) subiterator->data);
			subiterator = g_list_next (subiterator);
		}
		g_list_free (subiterator);
		iterator = g_list_next (iterator);
	}
	g_list_free (list);
}

/******************************************************************************************************************
 * STRUCTURE MANIPULATION FUNCTIONS
 ******************************************************************************************************************/

/**
 * stf_preview_new
 * @canvas : a gnomecanvas
 * @formatted : if set the values will be rendered formatted
 *
 * returns : a new renderdata struct
 **/
RenderData_t*
stf_preview_new (GnomeCanvas *canvas, gboolean formatted)
{
	RenderData_t* renderdata;

	g_return_val_if_fail (canvas != NULL, NULL);

	renderdata = g_new (RenderData_t, 1);

	renderdata->canvas       = canvas;
	renderdata->formatted    = formatted;
	renderdata->startrow     = 1;
	renderdata->colwidths    = g_array_new (FALSE, FALSE, sizeof (int));
	renderdata->actualwidths = g_array_new (FALSE, FALSE, sizeof (int));
	renderdata->colformats   = g_ptr_array_new ();
	renderdata->temp         = NULL;
	renderdata->group        = NULL;
	renderdata->gridgroup    = NULL;

	renderdata->font       = gdk_font_load ("fixed");
	renderdata->charwidth  = gdk_string_width (renderdata->font, "W");
	renderdata->charheight = gdk_string_height (renderdata->font, "W");

	renderdata->activecolumn = -1;

	return renderdata;
}

/**
 * stf_preview_free
 * @renderdata : a renderdata struct
 *
 * This will free the @renderdata
 *
 * returns : nothing
 **/
void
stf_preview_free (RenderData_t *renderdata)
{
	g_return_if_fail (renderdata != NULL);

	if (renderdata->group != NULL)
		gtk_object_destroy (GTK_OBJECT (renderdata->group));

	if (renderdata->formatted && renderdata->temp != NULL)
		g_array_free (renderdata->temp, TRUE);

	g_array_free (renderdata->colwidths, TRUE);
	g_array_free (renderdata->actualwidths, TRUE);

	stf_preview_colformats_clear (renderdata);
	g_ptr_array_free (renderdata->colformats, TRUE);

	gdk_font_unref (renderdata->font);

	g_free (renderdata);
}

/**
 * stf_preview_set_startrow
 * @renderdata : struct containing rendering information
 * @startrow : the new row to start rendering at
 *
 * This will set a new row to start rendering.
 *
 * returns : nothing
 **/
void
stf_preview_set_startrow (RenderData_t *renderdata, int startrow)
{
	g_return_if_fail (renderdata != NULL);
	g_return_if_fail (startrow >= 0);

	renderdata->startrow = startrow;
}

/**
 * stf_preview_set_activecolumn
 * @renderdata : struct containing rendering information
 * @column : column to set as active column
 *
 * This will set the active column, if you pass -1 as @column no column
 * will be drawn in 'active' state
 *
 * returns : nothing
 **/
void
stf_preview_set_activecolumn (RenderData_t *renderdata, int column)
{
	g_return_if_fail (renderdata != NULL);
	g_return_if_fail (column >= -1);

	renderdata->activecolumn = column;
}

/**
 * stf_preview_colwidths_clear
 * @renderdata : a struct containing rendering information
 *
 * This will clear the @renderdata->colwidths array which contains the minimum width of
 * each column.
 *
 * returns : nothing
 **/
void
stf_preview_colwidths_clear (RenderData_t *renderdata)
{
	g_return_if_fail (renderdata != NULL);

	g_array_free (renderdata->colwidths, TRUE);
	renderdata->colwidths = g_array_new (FALSE, FALSE, sizeof (int));

	g_array_free (renderdata->actualwidths, TRUE);
	renderdata->actualwidths = g_array_new (FALSE, FALSE, sizeof (int));
}

/**
 * stf_preview_colwidths_add
 * @renderdata : a struct containing rendering information
 * @width : the width of the next column
 *
 * This will add an entry to the @renderdata->colwidths array.
 *
 * returns : nothing
 **/
void
stf_preview_colwidths_add (RenderData_t *renderdata, int width)
{
	char *caption;
	int captionwidth;

	g_return_if_fail (renderdata != NULL);

	if (width < 0)
		width = 0;

	caption = g_strdup_printf (_(COLUMN_CAPTION), renderdata->colwidths->len);
	captionwidth = gdk_string_width (renderdata->font, caption) / renderdata->charwidth;

	if (captionwidth > width)
		g_array_append_val (renderdata->colwidths, captionwidth);
	else
		g_array_append_val (renderdata->colwidths, width);

	g_array_append_val (renderdata->actualwidths, width);

	g_free (caption);
}

/**
 * stf_preview_colformats_clear
 * @renderdata : a struct containing rendering information
 *
 * This will clear the @renderdata->colformats array which contains the format of
 * each column.
 *
 * returns : nothing
 **/
void
stf_preview_colformats_clear (RenderData_t *renderdata)
{
	guint i;
	g_return_if_fail (renderdata != NULL);

	for (i = 0; i < renderdata->colformats->len; i++)
		style_format_unref (g_ptr_array_index (renderdata->colformats, i));
	g_ptr_array_free (renderdata->colformats, TRUE);
	renderdata->colformats = g_ptr_array_new ();
}

/**
 * stf_preview_colformats_add
 * @renderdata : a struct containing rendering information
 * @format : the format of the column
 *
 * This will add an entry to the @renderdata->colformats array.
 * The widths of the columns will be set to at least have the width of
 * the @format.
 *
 * returns : nothing
 **/
void
stf_preview_colformats_add (RenderData_t *renderdata, StyleFormat *format)
{

	g_return_if_fail (renderdata != NULL);
	g_return_if_fail (format != NULL);

	style_format_ref (format);
	g_ptr_array_add (renderdata->colformats, format);
}

/******************************************************************************************************************
 * PUBLIC UTILITY FUNCTIONS
 ******************************************************************************************************************/

/**
 * stf_preview_get_displayed_rowcount
 * @renderdata : a struct containing rendering information
 *
 * returns : number of rows that can be displayed on the @renderdata->canvas
 **/
int
stf_preview_get_displayed_rowcount (RenderData_t *renderdata)
{
	int canvasheight, rowcount = 0;

	g_return_val_if_fail (renderdata != NULL, 0);
	g_return_val_if_fail (renderdata->canvas != NULL, 0);


	gtk_object_get (GTK_OBJECT (renderdata->canvas),
			"height", &canvasheight,
			NULL);

	rowcount = (canvasheight / (gdk_string_height (renderdata->font, "Test"))) + 1;

	return rowcount;
}

/**
 * stf_preview_get_column_at_x
 * @renderdata : a struct containing rendering information
 * @x : x coordinate
 *
 * Given the @x coordinate this function will determine what column is located at the @x coordinate
 *
 * returns : the column number the user clicked on or -1 if the user did not click on a column at all
 **/
int
stf_preview_get_column_at_x (RenderData_t *renderdata, double x)
{
	int xpos = 0;
	guint i;
	GArray *sourcearray;

	g_return_val_if_fail (renderdata != NULL, -1);

	if (renderdata->formatted)
		sourcearray = renderdata->temp;
	else
		sourcearray = renderdata->colwidths;

	for (i = 0; i < renderdata->colwidths->len; i++) {

		xpos += (renderdata->charwidth * g_array_index (sourcearray, int, i)) + CELL_HPAD;

		if (xpos > x)
			break;
	}

	if (i > renderdata->colwidths->len - 1)
		return -1;
	else
		return i;
}

/**
 * stf_preview_get_column_border_at_x
 * @renderdata : a struct containing rendering information
 * @x : x coordinate
 *
 * Given the @x coordinate this function will determine weather @x is
 * over a column border. (the right side of a column)
 *
 * NOTE : Can't use this on a formatted preview
 *
 * returns : the number of the column @x is at or -1 if it is not positioned over a column border
 **/
int
stf_preview_get_column_border_at_x (RenderData_t *renderdata, double x)
{
	int xpos = 0;
	guint i;
	gboolean broken = FALSE;

	g_return_val_if_fail (renderdata != NULL, -1);
	g_return_val_if_fail (renderdata->formatted == FALSE, -1);

	for (i = 0; i < renderdata->colwidths->len; i++) {

		xpos += (renderdata->charwidth * g_array_index (renderdata->colwidths, int, i)) + CELL_HPAD;

		if ( (x >= xpos - MOUSE_SENSITIVITY) && (x <= xpos + MOUSE_SENSITIVITY)) {

			broken = TRUE;
			break;
		}
	}

	if (broken) {

		if (i >= renderdata->colwidths->len - 1)
			return -1;
		else
			return i;
	}
	else {

		return -1;
	}
}

/**
 * stf_preview_get_char_at_x
 * @renderdata : a struct containing rendering information
 * @x : x coordinate
 *
 * Given the @x coordinate this function will the index of the character @x is at, e.g.
 *
 * Column 0 | Column 1 | Column 2
 * a          b          cd
 *
 * if the mouse is over 'b' this function will return 1 because it is the second character,
 * and if it is over d it will return 3.
 *
 * NOTE : Can't use this on a formatted preview
 *
 * returns : the character index of the character below @x or -1 if none.
 **/
int
stf_preview_get_char_at_x (RenderData_t *renderdata, double x)
{
	guint i;
	double xpos = 0, subxpos = 0, tpos = 0;
	int charindex = 0;

	g_return_val_if_fail (renderdata != NULL, -1);
	g_return_val_if_fail (renderdata->formatted == FALSE, -1);

	for (i = 0; i < renderdata->actualwidths->len; i++) {

		tpos = xpos + (renderdata->charwidth * g_array_index (renderdata->actualwidths, int, i));

		while (subxpos < tpos) {

			if (x > subxpos && x < (subxpos + renderdata->charwidth + 1))
				return charindex;

			subxpos += renderdata->charwidth;
			charindex++;

		}

		xpos += (renderdata->charwidth * g_array_index (renderdata->colwidths, int, i)) + CELL_HPAD;
		subxpos = xpos;
	}

	return -1;
}
