#ifndef GNUMERIC_BORDER_H
#define GNUMERIC_BORDER_H

#include "mstyle.h"

typedef enum {
	BORDER_HORIZONTAL,
	BORDER_VERTICAL,
	BORDER_DIAGONAL
} StyleBorderOrientation;

typedef enum {
 	BORDER_NONE			= 0x0,
 	BORDER_THIN			= 0x1,
 	BORDER_MEDIUM			= 0x2,
 	BORDER_DASHED			= 0x3,
 	BORDER_DOTTED			= 0x4,
 	BORDER_THICK			= 0x5,
 	BORDER_DOUBLE			= 0x6,
 	BORDER_HAIR			= 0x7,
	BORDER_MEDIUM_DASH		= 0x8,
	BORDER_DASH_DOT			= 0x9,
	BORDER_MEDIUM_DASH_DOT		= 0xa,
	BORDER_DASH_DOT_DOT		= 0xb,
	BORDER_MEDIUM_DASH_DOT_DOT	= 0xc,
	BORDER_SLANTED_DASH_DOT		= 0xd,

 	BORDER_MAX
} StyleBorderType;

struct _MStyleBorder {
	/* Key elements */
	StyleBorderType	 line_type;
	StyleColor     	*color;

	/* Private */
	GdkGC	*gc;
	gint	ref_count;
};

void	      border_unref (MStyleBorder *border);
MStyleBorder *border_ref   (MStyleBorder *border);
MStyleBorder *border_fetch (StyleBorderType const	 line_type,
			    StyleColor 			*color,
			    StyleBorderOrientation       orientation);
StyleBorderOrientation border_get_orientation (MStyleElementType type);

gint   border_get_width   (StyleBorderType const line_type);
void   border_set_gc_dash (GdkGC *gc, StyleBorderType const line_type);
GdkGC *border_get_gc      (MStyleBorder *border, GdkWindow *window);

void border_draw (GdkDrawable * drawable, const MStyleBorder *border,
		  int x1, int y1, int x2, int y2);

#endif /* GNUMERIC_BORDER_H */
