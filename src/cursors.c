#include <config.h>
#include <gnome.h>
#include "color.h"
#include "cursors.h"
#include "pixmaps/cursor_cross.xpm"
#include "pixmaps/cursor_zoom_in.xpm"
#include "pixmaps/cursor_zoom_out.xpm"

#define GDK_INTERNAL_CURSOR -1

GnumericCursorDef gnumeric_cursors [] = {
	{ NULL, 17, 17, cursor_cross_xpm },
	{ NULL, GDK_INTERNAL_CURSOR,   GDK_CROSSHAIR,         NULL },
	{ NULL, GDK_INTERNAL_CURSOR,   GDK_ARROW,             NULL },
	{ NULL, GDK_INTERNAL_CURSOR,   GDK_FLEUR,             NULL },
	{ NULL, 24, 24, cursor_zoom_in_xpm }, 
	{ NULL, 24, 24, cursor_zoom_out_xpm }, 
	{ NULL, GDK_INTERNAL_CURSOR,   GDK_SB_H_DOUBLE_ARROW, NULL },
	{ NULL, GDK_INTERNAL_CURSOR,   GDK_SB_V_DOUBLE_ARROW, NULL },
	{ NULL, GDK_INTERNAL_CURSOR,   GDK_SIZING,            NULL },
	{ NULL, GDK_INTERNAL_CURSOR,   GDK_SIZING,            NULL },
	{ NULL, GDK_INTERNAL_CURSOR,   GDK_HAND2,             NULL },
	{ NULL, 0,    0,  NULL }
};


static void
create_bitmap_and_mask_from_xpm (GdkBitmap **bitmap, GdkBitmap **mask, gchar **xpm)
{
	int height, width, colors;
	char pixmap_buffer [(32 * 32)/8];
	char mask_buffer [(32 * 32)/8];
	int x, y, pix, yofs;
	int transparent_color, black_color;

	sscanf (xpm [0], "%d %d %d %d", &height, &width, &colors, &pix);

	g_assert (height == 32);
	g_assert (width  == 32);
	g_assert (colors <= 3);

	transparent_color = ' ';
	black_color = '.';

	yofs = colors + 1;
	for (y = 0; y < 32; y++){
		for (x = 0; x < 32;){
			char value = 0, maskv = 0;
			
			for (pix = 0; pix < 8; pix++, x++){
				if (xpm [y + yofs][x] != transparent_color){
					maskv |= 1 << pix;

					if (xpm [y + yofs][x] != black_color){
						value |= 1 << pix;
					}
				}
			}
			pixmap_buffer [(y * 4 + x/8)-1] = value;
			mask_buffer [(y * 4 + x/8)-1] = maskv;
		}
	}
	*bitmap = gdk_bitmap_create_from_data (NULL, pixmap_buffer, 32, 32);
	*mask   = gdk_bitmap_create_from_data (NULL, mask_buffer, 32, 32);
}

void
cursors_init (void)
{
	int i;

	for (i = 0; gnumeric_cursors [i].hot_x; i++){
		GdkBitmap *bitmap, *mask;


		if (gnumeric_cursors [i].hot_x < 0)
			gnumeric_cursors [i].cursor = gdk_cursor_new (
				gnumeric_cursors [i].hot_y);
		else {
			create_bitmap_and_mask_from_xpm (
				&bitmap, &mask, gnumeric_cursors [i].xpm);
			gnumeric_cursors [i].cursor =
				gdk_cursor_new_from_pixmap (
					bitmap, mask,
					&gs_white, &gs_black,
					gnumeric_cursors [i].hot_x,
					gnumeric_cursors [i].hot_y);
		}
	}
}

void
cursors_shutdown (void)
{
	int i;
	
	for (i = 0; gnumeric_cursors [i].hot_x; i++)
		gdk_cursor_destroy (gnumeric_cursors [i].cursor);
}

