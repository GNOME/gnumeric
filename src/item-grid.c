/*
 * The Grid Gnome Canvas Item: Implements the grid and
 * spreadsheet information display.
 *
 * (C) 1998 The Free Software Foundation
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>

#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "item-debug.h"
#include "dialogs.h"

static GnomeCanvasItem *item_grid_parent_class;

/* The arguments we take */
enum {
	ARG_0,
	ARG_SHEET_VIEW,
};

static void
item_grid_destroy (GtkObject *object)
{
	ItemGrid *grid;

	grid = ITEM_GRID (object);

	if (GTK_OBJECT_CLASS (item_grid_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_grid_parent_class)->destroy)(object);
}

static void
item_grid_realize (GnomeCanvasItem *item)
{
	GnomeCanvas *canvas = item->canvas;
	GdkVisual *visual;
	GdkWindow *window;
	ItemGrid *item_grid;
	GdkGC *gc;
	
	if (GNOME_CANVAS_ITEM_CLASS (item_grid_parent_class)->realize)
		(*GNOME_CANVAS_ITEM_CLASS (item_grid_parent_class)->realize)(item);
	
	item_grid = ITEM_GRID (item);
	window = GTK_WIDGET (item->canvas)->window;
	
	/* Configure the default grid gc */
	item_grid->grid_gc = gc = gdk_gc_new (window);
	item_grid->fill_gc = gdk_gc_new (window);
	item_grid->gc = gdk_gc_new (window);
	
	gdk_gc_set_line_attributes (gc, 1, GDK_LINE_SOLID,
				    GDK_CAP_PROJECTING, GDK_JOIN_MITER);

	gnumeric_sheet_color_alloc (item->canvas);
	
	/* Allocate the default colors */
	item_grid->background = gs_white;
	item_grid->grid_color = gs_light_gray;
	item_grid->default_color = gs_black;
	
	gdk_gc_set_foreground (gc, &item_grid->grid_color);
	gdk_gc_set_background (gc, &item_grid->background);
	
	gdk_gc_set_foreground (item_grid->fill_gc, &item_grid->background);
	gdk_gc_set_background (item_grid->fill_gc, &item_grid->grid_color);


	/* Find out how we need to draw the selection with the current visual */
	visual = gtk_widget_get_visual (GTK_WIDGET (canvas));

	switch (visual->type){
	case GDK_VISUAL_STATIC_GRAY:
	case GDK_VISUAL_TRUE_COLOR:
	case GDK_VISUAL_STATIC_COLOR:
		item_grid->visual_is_paletted = 0;
		break;

	default:
		item_grid->visual_is_paletted = 1;
	}
}

static void
item_grid_unrealize (GnomeCanvasItem *item)
{
	ItemGrid *item_grid = ITEM_GRID (item);
	
	gdk_gc_unref (item_grid->grid_gc);
	item_grid->grid_gc = 0;
	
	if (GNOME_CANVAS_ITEM_CLASS (item_grid_parent_class)->unrealize)
		(*GNOME_CANVAS_ITEM_CLASS (item_grid_parent_class)->unrealize)(item);
}

static void
item_grid_reconfigure (GnomeCanvasItem *item)
{
	item->x1 = 0;
	item->y1 = 0;
	item->x2 = INT_MAX;
	item->y2 = INT_MAX;
	
	gnome_canvas_group_child_bounds (GNOME_CANVAS_GROUP (item->parent), item);
}

/*
 * item_grid_find_col: return the column where x belongs to
 */
int
item_grid_find_col (ItemGrid *item_grid, int x, int *col_origin)
{
	int col   = item_grid->left_col;
	int pixel = item_grid->left_offset;

	do {
		ColRowInfo *ci = sheet_col_get_info (item_grid->sheet, col);
		
		if (x >= pixel && x <= pixel + ci->pixels){
			if (col_origin)
				*col_origin = pixel;
			return col;
		}
		col++;
		pixel += ci->pixels;
	} while (1);
}

/*
 * item_grid_find_row: return the row where y belongs to
 */
int
item_grid_find_row (ItemGrid *item_grid, int y, int *row_origin)
{
	int row   = item_grid->top_row;
	int pixel = item_grid->top_offset;

	do {
		ColRowInfo *ri = sheet_row_get_info (item_grid->sheet, row);
		
		if (y >= pixel && y <= pixel + ri->pixels){
			if (row_origin)
				*row_origin = pixel;
			return row;
		}
		row++;
		pixel += ri->pixels;
	} while (1);
}

typedef struct {
	ItemGrid      *item_grid;
	GdkDrawable   *drawable;
	GnumericSheet *gsheet;
	int   	      x_paint;		/* the offsets */
	int   	      y_paint;
} paint_data;

/*
 * Draw a cell.  It gets pixel level coordinates
 */
static void
item_grid_draw_cell (GdkDrawable *drawable, ItemGrid *item_grid,
		     int x1, int y1, int width, int height, int col, int row)
{
	GnomeCanvas   *canvas   = GNOME_CANVAS_ITEM (item_grid)->canvas;
	GdkGC         *white_gc = GTK_WIDGET (canvas)->style->white_gc;
	GdkGC         *gc       = item_grid->gc;
	Sheet         *sheet    = item_grid->sheet;
	GdkFont       *font;
	Cell          *cell, *clip_left, *clip_right;
	Style         *style;
	int           x_offset, y_offset, text_base, pixels;
	GdkRectangle  rect;
	int           halign;
	int           cell_is_selected;
	
	cell_is_selected = sheet_selection_is_cell_selected (sheet, col, row);
		
	cell = sheet_cell_get (sheet, col, row);

	/*
	 * If the cell does not exist, there is little to do: only
	 * check if we should paint it as a selected cell
	 */
	if (!cell){
		if (cell_is_selected){
			GdkGC *black_gc = GTK_WIDGET (canvas)->style->black_gc;
			
			if (!(sheet->cursor_col == col && sheet->cursor_row == row))
				gdk_draw_rectangle (drawable, black_gc, TRUE,
						    x1+1, y1+1, width - 2, height - 2);
		}
		
		return;
	}

	/* The offsets where we start drawing the text */
	x_offset = y_offset = 0;

	/* True if we have a sibling cell in the direction where we paint */
	clip_left  = NULL;
	clip_right = NULL;
	
	style = cell->style;
	font = style->font->font;

	halign = cell_get_horizontal_align (cell);

	switch (halign){
	case HALIGN_LEFT:
		if (col < SHEET_MAX_COLS-1)
			clip_right = sheet_cell_get (sheet, col+1, row);
		x_offset = cell->col->margin_a;
		break;
		
	case HALIGN_RIGHT:
		if (col > 0)
			clip_left = sheet_cell_get (sheet, col-1, row);
		x_offset = cell->col->pixels - cell->width - (cell->col->margin_b + cell->col->margin_a);
		break;
		
	case HALIGN_CENTER:
		if (col > 0)
			clip_left = sheet_cell_get (sheet, col-1, row);
		if (col < SHEET_MAX_COLS-1)
			clip_right = sheet_cell_get (sheet, col-1, row);
		x_offset = (cell->col->pixels - cell->width)/2;
		break;
		
	case HALIGN_FILL:
		if (col < SHEET_MAX_COLS-1)
			clip_right = sheet_cell_get (sheet, col-1, row);
		clip_left = clip_right = (Cell *) TRUE;
		x_offset = cell->col->margin_a;
		break;
			
	case HALIGN_JUSTIFY:
		g_warning ("No horizontal justification supported yet\n");
		break;
	}

	if ((cell->style->valid_flags & STYLE_COLOR) && cell->style->color){
		gdk_gc_set_foreground (gc, &cell->style->color->foreground);
		gdk_gc_set_background (gc, &cell->style->color->background);
	} else 
		gdk_gc_set_foreground (gc, &item_grid->default_color);

	text_base = y1 + cell->row->pixels - cell->row->margin_b - font->descent + 1;

	gdk_gc_set_foreground (gc, &item_grid->default_color);
	gdk_gc_set_function (gc, GDK_COPY);
	
	if (clip_left || clip_right){
		rect.x = x1;
		rect.y = y1;
		rect.width = width;
		rect.height = height;
		gdk_gc_set_clip_rectangle (gc, &rect);
	} else
		gdk_gc_set_clip_rectangle (gc, NULL);

	gdk_draw_rectangle (drawable, white_gc, TRUE,
			    x1 + x_offset, 
			    y1 + cell->row->margin_a,
			    cell->width - (cell->col->margin_a + cell->col->margin_b),
			    height - (cell->row->margin_a + cell->row->margin_b));

	pixels = 0;
	if (!cell->entered_text){
		printf ("No entered text in cell %d,%d\n", cell->col->pos, cell->row->pos);
		return;
	}

	do {
		char *text;

		text = CELL_TEXT_GET (cell);
		gdk_draw_text (drawable, font, gc,
			       x1 + x_offset,
			       text_base + y_offset,
			       text, strlen (text));
		
		pixels += cell->width;
	} while (style->halign == HALIGN_FILL &&
		 pixels < cell->col->pixels);

	/*
	 * If the cell is selected, turn the inverse video on
	 */
	if (cell_is_selected){
		if (sheet->cursor_col == col && sheet->cursor_row == row)
			return;

		if (item_grid->visual_is_paletted){
			gdk_gc_set_function (gc, GDK_XOR);
			gdk_gc_set_foreground (gc, &gs_white);
		} else {
			gdk_gc_set_function (gc, GDK_INVERT);
			gdk_gc_set_foreground (gc, &gs_black);
		}
		
		gdk_draw_rectangle (drawable, gc, TRUE,
				    x1 + 1,
				    y1 + 1,
				    width - 1, height - 1);
	}
}

static void
item_grid_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	ItemGrid *item_grid = ITEM_GRID (item);
	Sheet *sheet = item_grid->sheet;
	GdkGC *grid_gc = item_grid->grid_gc;
	int end_x, end_y;
	int paint_col, paint_row, max_paint_col, max_paint_row;
	int col, row;
	int x_paint, y_paint;
	int diff_x, diff_y;
	paint_data pd;

	if (x < 0){
		g_warning ("x < 0\n");
		return;
	}

	if (y < 0){
		g_warning ("y < 0\n");
		return;
	}
	
	max_paint_col = max_paint_row = 0;
	paint_col = item_grid_find_col (item_grid, x, &x_paint);
	paint_row = item_grid_find_row (item_grid, y, &y_paint);

	col = paint_col;

	diff_x = x - x_paint;
	end_x = width + diff_x;
	diff_y = y - y_paint;
	end_y = height + diff_y;

	/* 1. The default background */
	gdk_draw_rectangle (drawable, item_grid->fill_gc, TRUE,
			    0, 0, width, height);
	
	/* 2. the grids */
	for (x_paint = -diff_x; x_paint < end_x; col++){
		ColRowInfo *ci;

		ci = sheet_col_get_info (sheet, col);
		g_assert (ci->pixels != 0);

		gdk_draw_line (drawable, grid_gc, x_paint, 0, x_paint, height);
		
		x_paint += ci->pixels;
		max_paint_col = col;
	}

	row = paint_row;
	for (y_paint = -diff_y; y_paint < end_y; row++){
		ColRowInfo *ri;

		ri = sheet_row_get_info (sheet, row);
		gdk_draw_line (drawable, grid_gc, 0, y_paint, width, y_paint);
		y_paint += ri->pixels;
		max_paint_row = row;
	}

	/* The cells */
	pd.x_paint   = -diff_x;
	pd.y_paint   = -diff_y;
	pd.drawable  = drawable;
	pd.item_grid = item_grid;
	pd.gsheet    = GNUMERIC_SHEET (item->canvas);

#if 0
	printf ("Painting the (%d,%d)-(%d,%d) region\n",
		paint_col, paint_row, max_paint_col, max_paint_row);
#endif
	
	col = paint_col;
	for (x_paint = -diff_x; x_paint < end_x; col++){
		ColRowInfo *ci;

		ci = sheet_col_get_info (sheet, col);

		row = paint_row;
		for (y_paint = -diff_y; y_paint < end_y; row++){
			ColRowInfo *ri;

			ri = sheet_row_get_info (sheet, row);
			item_grid_draw_cell (drawable, item_grid,
					     x_paint, y_paint,
					     ci->pixels,
					     ri->pixels,
					     col, row);
			y_paint += ri->pixels;
		}

		x_paint += ci->pixels;
	}
	
#undef DEBUG_EXPOSES
#ifdef DEBUG_EXPOSES
	item_debug_cross (drawable, item_grid->grid_gc, 0, 0, width, height);
#endif
}

static double
item_grid_point (GnomeCanvasItem *item, double x, double y, int cx, int cy,
		 GnomeCanvasItem **actual_item)
{
	*actual_item = item;
	return 0.0;
}

static void
item_grid_translate (GnomeCanvasItem *item, double dx, double dy)
{
	printf ("item_grid_translate %g, %g\n", dx, dy);
}

static void
context_destroy_menu (GtkWidget *widget)
{
	gtk_widget_destroy (gtk_widget_get_toplevel (widget));
}

static void
context_cut_cmd (GtkWidget *widget, ItemGrid *item_grid)
{
	sheet_selection_cut (item_grid->sheet);
	context_destroy_menu (widget);
}

static void
context_copy_cmd (GtkWidget *widget, ItemGrid *item_grid)
{
	sheet_selection_copy (item_grid->sheet);
	context_destroy_menu (widget);
}

static void
context_paste_cmd (GtkWidget *widget, ItemGrid *item_grid)
{
	Sheet *sheet = item_grid->sheet;
	
	sheet_selection_paste (sheet,
			       sheet->cursor_col,
			       sheet->cursor_row,
			       PASTE_DEFAULT);
	context_destroy_menu (widget);
}

static void
context_paste_special_cmd (GtkWidget *widget, ItemGrid *item_grid)
{
	Sheet *sheet = item_grid->sheet;
	int flags;

	flags = dialog_paste_special ();
	sheet_selection_paste (sheet,
			       sheet->cursor_col,
			       sheet->cursor_row,
			       flags);
	context_destroy_menu (widget);
}

static void
context_insert_cmd (GtkWidget *widget, ItemGrid *item_grid)
{
	dialog_insert_cells (item_grid->sheet);
	context_destroy_menu (widget);
}

static void
context_delete_cmd (GtkWidget *widget, ItemGrid *item_grid)
{
	dialog_delete_cells (item_grid->sheet);
	context_destroy_menu (widget);
}

static void
context_clear_cmd (GtkWidget *widget, ItemGrid *item_grid)
{
	sheet_selection_clear_content (item_grid->sheet);
	context_destroy_menu (widget);
}

static void
context_cell_format_cmd (GtkWidget *widget, ItemGrid *item_grid)
{
	dialog_cell_format (item_grid->sheet);
	context_destroy_menu (widget);
}

typedef enum {
	IG_ALWAYS,
	IG_HAVE_SELECTION,
	IG_SEPARATOR,
} popup_types;

struct {
	char         *name;
	void         (*fn)(GtkWidget *widget, ItemGrid *item_grid);
	popup_types  type;
} item_grid_context_menu [] = {
	{ N_("Cut"),           context_cut_cmd,           IG_ALWAYS         },
	{ N_("Copy"),          context_copy_cmd,          IG_ALWAYS         },
	{ N_("Paste"),         context_paste_cmd,         IG_HAVE_SELECTION },
	{ N_("Paste special"), context_paste_special_cmd, IG_HAVE_SELECTION },
	{ "",                  NULL,                      IG_SEPARATOR      },
	{ N_("Insert"),        context_insert_cmd,        IG_ALWAYS  	    },
	{ N_("Delete"),        context_delete_cmd,        IG_ALWAYS  	    },
	{ N_("Erase content"), context_clear_cmd,         IG_ALWAYS         },
	{ "",                  NULL,                      IG_SEPARATOR      },
	{ N_("Cell format"),   context_cell_format_cmd,   IG_ALWAYS         },
	{ NULL,                NULL,                      0 }
};

static GtkWidget *
create_popup_menu (ItemGrid *item_grid, int include_paste)
{
	GtkWidget *menu, *item;
	int i;

	menu = gtk_menu_new ();
	item = NULL;
	
	for (i = 0; item_grid_context_menu [i].name; i++){
		switch (item_grid_context_menu [i].type){
		case IG_SEPARATOR:
			item = gtk_menu_item_new ();
			break;
			
		case IG_HAVE_SELECTION:
			if (!include_paste)
				continue;
			/* fall down */
			
		case IG_ALWAYS:
			item = gtk_menu_item_new_with_label (
				_(item_grid_context_menu [i].name));
			break;
		default:
			g_warning ("Never reached");
		}

		gtk_signal_connect (
			GTK_OBJECT (item), "activate",
			GTK_SIGNAL_FUNC (item_grid_context_menu [i].fn),
			item_grid);

		gtk_widget_show (item);
		gtk_menu_append (GTK_MENU (menu), item);
	}

	return menu;
}	   
		   
static void
item_grid_popup_menu (ItemGrid *item_grid, GdkEvent *event, int col, int row)
{
	GtkWidget *menu;
	int show_paste;
		
	show_paste = item_grid->sheet->workbook->clipboard_contents != NULL;

	menu = create_popup_menu (item_grid, show_paste);

	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 3, event->button.time);
}

		      
/*
 * Handle the selection
 */
#define convert(c,sx,sy,x,y) gnome_canvas_w2c (c,sx,sy,x,y)

static gint
item_grid_event (GnomeCanvasItem *item, GdkEvent *event)
{
	GnomeCanvas *canvas = item->canvas;
	ItemGrid *item_grid = ITEM_GRID (item);
	Sheet *sheet = item_grid->sheet;
	int col, row, x, y;
	int scroll_x, scroll_y;

	switch (event->type){
	case GDK_BUTTON_RELEASE:
		if (event->button.button == 1){
			item_grid->selecting = 0;
			gnome_canvas_item_ungrab (item, event->button.time);
			return 1;
		}
		break;
		
	case GDK_MOTION_NOTIFY:
		if (item_grid->selecting){
			scroll_x = scroll_y = 0;
			if (event->motion.x < 0){
				event->motion.x = 0;
				scroll_x = 1;
			} if (event->motion.y < 0){
				event->motion.y = 0;
				scroll_y = 1;
			}
			convert (canvas, event->motion.x, event->motion.y, &x, &y);
			if (!item_grid->selecting)
				return 1;
			
			col = item_grid_find_col (item_grid, x, NULL);
			row = item_grid_find_row (item_grid, y, NULL);
			sheet_selection_extend_to (sheet, col, row);
			return 1;
		}
		break;
		
	case GDK_BUTTON_PRESS:
		switch (event->button.button){
		case 1:
			convert (canvas, event->button.x, event->button.y, &x, &y);
			col = item_grid_find_col (item_grid, x, NULL);
			row = item_grid_find_row (item_grid, y, NULL);
			
			sheet_accept_pending_output (sheet);
			sheet_cursor_move (sheet, col, row);
			if (!(event->button.state & GDK_CONTROL_MASK))
				sheet_selection_reset_only (sheet);
			
			item_grid->selecting = 1;
			sheet_selection_append (sheet, col, row);
			gnome_canvas_item_grab (item,
						GDK_POINTER_MOTION_MASK |
						GDK_BUTTON_RELEASE_MASK,
						NULL,
						event->button.time);
			return 1;

		case 3:
			convert (canvas, event->button.x, event->button.y, &x, &y);
			col = item_grid_find_col (item_grid, x, NULL);
			row = item_grid_find_row (item_grid, y, NULL);

			item_grid_popup_menu (item_grid, event, col, row);
			return 1;
		}
	default:
		return 0;	
	}
	
	return 0;
}

/*
 * Instance initialization
 */
static void
item_grid_init (ItemGrid *item_grid)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_grid);
	
	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 0;
	item->y2 = 0;
	
	item_grid->left_col = 0;
	item_grid->top_row  = 0;
	item_grid->top_offset = 0;
	item_grid->left_offset = 0;
	item_grid->selecting = 0;
}

static void
item_grid_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	ItemGrid *item_grid;

	item = GNOME_CANVAS_ITEM (o);
	item_grid = ITEM_GRID (o);
	
	switch (arg_id){
	case ARG_SHEET_VIEW:
		item_grid->sheet_view = GTK_VALUE_POINTER (*arg);
		item_grid->sheet = item_grid->sheet_view->sheet;
		break;
	}
}

/*
 * ItemGrid class initialization
 */
static void
item_grid_class_init (ItemGridClass *item_grid_class)
{
	GtkObjectClass  *object_class;
	GnomeCanvasItemClass *item_class;

	item_grid_parent_class = gtk_type_class (gnome_canvas_item_get_type());
	
	object_class = (GtkObjectClass *) item_grid_class;
	item_class = (GnomeCanvasItemClass *) item_grid_class;

	gtk_object_add_arg_type ("ItemGrid::SheetView", GTK_TYPE_POINTER, 
				 GTK_ARG_WRITABLE, ARG_SHEET_VIEW);
	
	object_class->set_arg = item_grid_set_arg;
	object_class->destroy = item_grid_destroy;

	/* GnomeCanvasItem method overrides */
	item_class->realize     = item_grid_realize;
	item_class->unrealize   = item_grid_unrealize;
	item_class->reconfigure = item_grid_reconfigure;
	item_class->draw        = item_grid_draw;
	item_class->point       = item_grid_point;
	item_class->translate   = item_grid_translate;
	item_class->event       = item_grid_event;
}

GtkType
item_grid_get_type (void)
{
	static GtkType item_grid_type = 0;

	if (!item_grid_type) {
		GtkTypeInfo item_grid_info = {
			"ItemGrid",
			sizeof (ItemGrid),
			sizeof (ItemGridClass),
			(GtkClassInitFunc) item_grid_class_init,
			(GtkObjectInitFunc) item_grid_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		item_grid_type = gtk_type_unique (gnome_canvas_item_get_type (), &item_grid_info);
	}

	return item_grid_type;
}
