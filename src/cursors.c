#include <config.h>
#include <gnome.h>
#include "color.h"
#include "cursors.h"
#include "pixmaps.h"

#define GNUMERIC_CURSORS 10

GdkCursor *gnumeric_cursors [GNUMERIC_CURSORS] = { NULL, };


void
cursors_init (void)
{
	GdkImlibImage *image;
	GdkPixmap *pixmap;
	GdkBitmap *bitmap;
	
	image = gdk_imlib_create_image_from_xpm_data (arrow_xpm);
	if (!image){
		g_warning ("Could not create image\n");
		return;
	}

	gdk_imlib_render (image, image->rgb_width, image->rgb_height);
	pixmap = gdk_imlib_move_image (image);
	bitmap = gdk_imlib_move_mask  (image);

	gnumeric_cursors [0] = gdk_cursor_new_from_pixmap (bitmap, bitmap, &gs_white, &gs_black, 1, 1);
	gdk_imlib_destroy_image (image);
}

void
cursors_shutdown (void)
{
	gdk_cursor_destroy (gnumeric_cursors [0]);
}
