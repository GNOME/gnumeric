#include "stf-util.h"

/**
 * stf_is_line_terminator
 * @character : character that has to be checked
 *
 * returns : if @character points to a character which is a newline or a return it will return
 *           true, false otherwise.
 **
gboolean
stf_is_line_terminator (const char *character)
{
	if (*character == '\n' || *character == '\r')
		return TRUE;

	return FALSE;
}*/

/**
 * set_center_prevent_rectangle_size
 * @canvas : canvas where the @rectangle is located on
 * @rectangle : a rectangle on the @canvas which can be used for centering purposes
 * @height : the height of the region which is covered on the canvas
 * @width : the width of the region which is covered on the canvas
 *
 * This is merely a hack to prevent the canvas from centering on the text if the text 
 * width and/or height are smaller than the width and/or height of the GnomeCanvas.
 * Warning 1 : Don't remove this, this is necessary!!
 * Warning 2 : Be sure that the @canvas has both his width and height set to something other than 0
 *
 * returns : nothing
 **/
void
stf_set_scroll_region_and_prevent_center (GnomeCanvas *canvas, GnomeCanvasRect *rectangle, double width, double height)
{
	int canvaswidth, canvasheight;
	double rectwidth, rectheight;

	g_return_if_fail (canvas != NULL);
	g_return_if_fail (rectangle != NULL);
		
	gtk_object_get (GTK_OBJECT (canvas),
			"width", &canvaswidth,
			"height", &canvasheight,
			NULL);

	if (width < canvaswidth) 
		rectwidth = canvaswidth;
	else
		rectwidth = width;
	
        if (height < canvasheight)
		rectheight = canvasheight;
	else
		rectheight = height;

	gnome_canvas_item_set (GNOME_CANVAS_ITEM (rectangle),
			       "x1", 0,         "y1", 0,
			       "x2", rectwidth,	"y2", rectheight,
			       NULL);

	gnome_canvas_set_scroll_region (canvas, 0, 0, rectwidth, rectheight);
}

/**
 * stf_source_get_extent
 * @src : A filesource_t record which has gone trough a parsing routine
 *
 * This will do the same as sheet_get_extent and is actually just a
 * temporary workaround, because sheet_get_extent () because that does
 * not seem to work :-(
 *
 * Returns : a range
 **/
Range
stf_source_get_extent (FileSource_t *src)
{
	Range range;

	g_return_val_if_fail (src != NULL, range);
	
	range.start.col = 0;
	range.start.row = 0;
	range.end.col   = src->colcount;
	range.end.row   = src->rowcount;

	return range;
}

