#include <config.h>
#include <gnome.h>

#include "dialog-stf.h"
#include "stf-util.h"

#include "dialog-stf-preview.h"



typedef struct {
	double textwidth, textheight;
} TextInfo_t;

typedef struct {
	double colwidth, colheight;
} ColumnInfo_t;


static TextInfo_t
stf_preview_place_text (GnomeCanvasGroup *group, char* text, double x, double y)
{
	GnomeCanvasText *canvastext;
	TextInfo_t textinfo;
	
	canvastext = GNOME_CANVAS_TEXT (gnome_canvas_item_new (group,
							       gnome_canvas_text_get_type (),
							       "text", text,
							       "font", "fixed",
							       "anchor", GTK_ANCHOR_NW,
							       "x", x,
							       "y", y,
							       NULL));
	gtk_object_get (GTK_OBJECT (canvastext),
			"text_width", &textinfo.textwidth,
			"text_height", &textinfo.textheight,
			NULL);

	return textinfo;
}

/**
 * stf_preview_render_col
 * @group : a gnomecanvasgroup
 * @sheet : a gnumeric sheet
 * @rowcount : the number of rows in the sheet
 * @col : the column to render
 * @colstart : the position the column will start on.
 * @formatted : if true it will display the RENDERED values otherwise the ENTERED values
 * @startrow : the row to start at. it will not render more then the height of the canvas
 *             if this is not -1, otherwise it will render everything
 *
 * returns : width and height of the rendered column
 **/
static ColumnInfo_t
stf_preview_render_col (RenderData_t *renderdata, unsigned long col, double colstart, int canvasheight)
{
	GnomeCanvasRect *colrect;
	Cell *cell;
	TextInfo_t textinfo;
	ColumnInfo_t colinfo;
	int i, loopstart;
	char *celltext, *color;
	double ypos = 0.0, colwidth = 0.0, cellheight;

	renderdata->rowsrendered = 0;
		
	if (renderdata->startrow == -1)
		loopstart = 0;
	else
		loopstart = renderdata->startrow - 1;
		
	for (i = loopstart; i < renderdata->src->rowcount + 1; i++) {
	
		if (i != loopstart) {
			cell = sheet_cell_get (renderdata->src->sheet, col, i - 1);
			if (cell) {
				if (renderdata->formatted) 
					celltext = cell_get_formatted_val (cell, NULL);
				else
					celltext = cell_get_content (cell);
			}
			else
				celltext = " ";
		}
		else {
			celltext = g_strdup_printf (_("Column %d"), col);
			cell = (Cell*) TRUE; /* to make sure celltext gets freed */
		}
			
		textinfo = stf_preview_place_text (renderdata->group,
						   celltext,
						   colstart + (TEXT_OFFSET / 2),
						   ypos + (TEXT_VPADDING / 2));
		
		if (textinfo.textwidth > colwidth)
			colwidth = textinfo.textwidth;

		/* I imagine this is overkill, I have not yet seen fonts with
		   a variable height */
		ypos += textinfo.textheight + TEXT_VPADDING;
		renderdata->rowsrendered++;
		
		if (cell)
			g_free (celltext);

		if (renderdata->startrow != -1 && ypos > canvasheight)
			break;
		
	}
	
	colwidth += TEXT_OFFSET;

	cellheight = textinfo.textheight + TEXT_VPADDING;
		
	for (i = 0; i < renderdata->rowsrendered; i++) {
		if (i != 0)
			color = "White";
		else
			color = "Gray";
		
		colrect = GNOME_CANVAS_RECT (gnome_canvas_item_new (renderdata->group,
								    gnome_canvas_rect_get_type (),
								    "x1", colstart,
								    "y1", i * cellheight,
								    "x2", colstart + colwidth,
								    "y2", (i + 1) * cellheight,
								    "width_pixels", (int) 1,
								    "outline_color", "Black",
								    "fill_color", color,
								    NULL));
		gnome_canvas_item_lower_to_bottom (GNOME_CANVAS_ITEM (colrect));
	}
		

	colinfo.colwidth  = colwidth;
	colinfo.colheight = ypos;
	
	return colinfo;
}

/**
 * stf_preview_render
 * @canvas : The canvas to render on
 * @sheet : The sheet containing the source data
 * @group : Result previously returned from stf_preview_render ()
 * @formatted : if true it will render the cell WITH formatting
 *
 * This will render a preview on the canvas. If you want to render the
 * preview on the same canvas multiple times, pass the result of the previous
 * call back into @group each time you make a call to this function or NULL
 * if this is the first time you call the function.
 *
 * returns : nothing
 **/
void
stf_preview_render (RenderData_t *renderdata)
{
	GnomeCanvasRect *centerrect;
	ColumnInfo_t colinfo;
	int i;
	double maxcolheight = 0.0, colstart = 0.0;
	int canvasheight;
	
	if (renderdata->group != NULL)
		gtk_object_destroy (GTK_OBJECT (renderdata->group));

	if (renderdata->src->rowcount < 1) 
		return;
		
	renderdata->group = GNOME_CANVAS_GROUP (gnome_canvas_item_new (gnome_canvas_root (GNOME_CANVAS (renderdata->canvas)),
							   gnome_canvas_group_get_type(),
							   "x", 0.0,
							   "y", 0.0,
							   NULL));

	gtk_object_get (GTK_OBJECT (renderdata->canvas),
			"height", &canvasheight,
			NULL);

	for (i = 0; i < renderdata->src->colcount + 1; i++) {
		colinfo = stf_preview_render_col (renderdata, i, colstart, canvasheight);

		colstart += colinfo.colwidth;

		if (colinfo.colheight > maxcolheight)
			maxcolheight = colinfo.colheight;
	}

	
	centerrect = GNOME_CANVAS_RECT (gnome_canvas_item_new (renderdata->group,
							       gnome_canvas_rect_get_type (),
							       "x1", 0,	"y1", 0,
							       "x2", 10,	"y2", 10,
							       "width_pixels", (int) 0,
							       "fill_color", "white",
							       NULL));
	stf_set_scroll_region_and_prevent_center (renderdata->canvas, centerrect, colstart - TEXT_OFFSET, maxcolheight);
}

/* These are for creation/deletion */
RenderData_t*
stf_preview_new (GnomeCanvas *canvas, FileSource_t *src, gboolean formatted)
{
	RenderData_t* renderdata;
	
	renderdata = g_new (RenderData_t, 1);

	renderdata->canvas    = canvas;
	renderdata->src       = src;
	renderdata->formatted = formatted;
	renderdata->startrow  = 1;

	renderdata->group        = NULL;
	renderdata->rowsrendered = 0;

	return renderdata;
}

void
stf_preview_free (RenderData_t *renderdata)
{
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
	renderdata->startrow = startrow;
}


