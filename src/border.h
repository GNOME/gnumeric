#ifndef GNUMERIC_BORDER_H
#define GNUMERIC_BORDER_H

#include "mstyle.h"

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

struct _MStyleBorder {
	/* Key elements */
	StyleBorderType	 line_type;
	StyleColor     	*color;

	/* Private */
	GdkGC	*gc;
	gint	ref_count;
};

void	      style_border_unref (MStyleBorder *border);
MStyleBorder *style_border_ref   (MStyleBorder *border);

MStyleBorder *style_border_none  (void);
MStyleBorder *style_border_fetch (StyleBorderType const	 line_type,
				  StyleColor 			*color,
				  StyleBorderOrientation       orientation);

StyleBorderOrientation style_border_get_orientation (MStyleElementType type);

gint   style_border_get_width   (StyleBorderType const line_type);
void   style_border_set_gc_dash (GdkGC *gc, StyleBorderType const line_type);
GdkGC *style_border_get_gc      (MStyleBorder *border, GdkWindow *window);

void   style_border_draw (GdkDrawable * drawable, const MStyleBorder *border,
			  int x1, int y1, int x2, int y2);

#endif /* GNUMERIC_BORDER_H */
