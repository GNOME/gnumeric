#ifndef GNUMERIC_BORDER_H
#define GNUMERIC_BORDER_H

#include "gnumeric.h"
#include <libgnomeprint/gnome-print.h>

typedef enum {
	STYLE_BORDER_HORIZONTAL,
	STYLE_BORDER_VERTICAL,
	STYLE_BORDER_DIAGONAL
} StyleBorderOrientation;

typedef enum {
 	STYLE_BORDER_NONE			= 0x0,
 	STYLE_BORDER_THIN			= 0x1,
 	STYLE_BORDER_MEDIUM			= 0x2,
 	STYLE_BORDER_DASHED			= 0x3,
 	STYLE_BORDER_DOTTED			= 0x4,
 	STYLE_BORDER_THICK			= 0x5,
 	STYLE_BORDER_DOUBLE			= 0x6,
 	STYLE_BORDER_HAIR			= 0x7,
	STYLE_BORDER_MEDIUM_DASH		= 0x8,
	STYLE_BORDER_DASH_DOT			= 0x9,
	STYLE_BORDER_MEDIUM_DASH_DOT		= 0xa,
	STYLE_BORDER_DASH_DOT_DOT		= 0xb,
	STYLE_BORDER_MEDIUM_DASH_DOT_DOT	= 0xc,
	STYLE_BORDER_SLANTED_DASH_DOT		= 0xd,

	/* ONLY for internal use */
	STYLE_BORDER_INCONSISTENT		= 0xe,

 	STYLE_BORDER_MAX
} StyleBorderType;

/* The order corresponds to the border_buttons name list
 * in dialog_cell_format_impl */
enum _StyleBorderLocation {
	STYLE_BORDER_TOP,	STYLE_BORDER_BOTTOM,
	STYLE_BORDER_LEFT,	STYLE_BORDER_RIGHT,
	STYLE_BORDER_REV_DIAG,	STYLE_BORDER_DIAG,

	/* These are special.
	 * They are logical rather than actual borders, however, they
	 * require extra lines to be drawn so they need to be here.
	 */
	STYLE_BORDER_HORIZ, STYLE_BORDER_VERT,

	STYLE_BORDER_EDGE_MAX
};

struct _StyleBorder {
	/* Key elements */
	StyleBorderType	 line_type;
	StyleColor     	*color;

	/* Private */
	GdkGC	*gc;
	gint	ref_count;
};

void	      style_border_unref (StyleBorder *border);
StyleBorder  *style_border_ref   (StyleBorder *border);

StyleBorder  *style_border_none  (void);
StyleBorder  *style_border_fetch (StyleBorderType const	 line_type,
				  StyleColor 			*color,
				  StyleBorderOrientation       orientation);
gboolean style_border_visible_in_blank (StyleBorder const *border);

StyleBorderOrientation style_border_get_orientation (StyleBorderLocation type);

gint   style_border_get_width   (StyleBorderType const line_type);
void   style_border_set_gc_dash (GdkGC *gc, StyleBorderType const line_type);
GdkGC *style_border_get_gc      (StyleBorder *border, GdkWindow *window);

void style_border_draw  (StyleBorder const * const st, StyleBorderLocation const t,
			 GdkDrawable * const drawable,
			 int x1, int y1, int x2, int y2,
			 StyleBorder const * const extend_begin,
			 StyleBorder const * const extend_end);
void style_border_print (StyleBorder const * const border, StyleBorderLocation const t,
			 GnomePrintContext *context,
			 double x1, double y1, double x2, double y2,
			 StyleBorder const * const extend_begin,
			 StyleBorder const * const extend_end);

#endif /* GNUMERIC_BORDER_H */
