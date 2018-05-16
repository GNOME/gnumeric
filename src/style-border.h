#ifndef _GNM_STYLE_BORDER_H_
# define _GNM_STYLE_BORDER_H_

#include <gnumeric.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

typedef enum {
	GNM_STYLE_BORDER_HORIZONTAL,
	GNM_STYLE_BORDER_VERTICAL,
	GNM_STYLE_BORDER_DIAGONAL
} GnmStyleBorderOrientation;

typedef enum {
	GNM_STYLE_BORDER_NONE			= 0x0,
	GNM_STYLE_BORDER_THIN			= 0x1,
	GNM_STYLE_BORDER_MEDIUM			= 0x2,
	GNM_STYLE_BORDER_DASHED			= 0x3,
	GNM_STYLE_BORDER_DOTTED			= 0x4,
	GNM_STYLE_BORDER_THICK			= 0x5,
	GNM_STYLE_BORDER_DOUBLE			= 0x6,
	GNM_STYLE_BORDER_HAIR			= 0x7,
	GNM_STYLE_BORDER_MEDIUM_DASH		= 0x8,
	GNM_STYLE_BORDER_DASH_DOT			= 0x9,
	GNM_STYLE_BORDER_MEDIUM_DASH_DOT		= 0xa,
	GNM_STYLE_BORDER_DASH_DOT_DOT		= 0xb,
	GNM_STYLE_BORDER_MEDIUM_DASH_DOT_DOT	= 0xc,
	GNM_STYLE_BORDER_SLANTED_DASH_DOT		= 0xd,

	/* ONLY for internal use */
	GNM_STYLE_BORDER_INCONSISTENT		= 0xe,

	GNM_STYLE_BORDER_MAX
} GnmStyleBorderType;

/* The order corresponds to the border_buttons name list
 * in dialog_cell_format_impl
 * GNM_STYLE_BORDER_TOP must be 0 */
typedef enum {
	GNM_STYLE_BORDER_TOP,		GNM_STYLE_BORDER_BOTTOM,
	GNM_STYLE_BORDER_LEFT,		GNM_STYLE_BORDER_RIGHT,
	GNM_STYLE_BORDER_REV_DIAG,	GNM_STYLE_BORDER_DIAG,

	/* These are special.
	 * They are logical rather than actual borders, however, they
	 * require extra lines to be drawn so they need to be here.
	 */
	GNM_STYLE_BORDER_HORIZ,		GNM_STYLE_BORDER_VERT,

	GNM_STYLE_BORDER_EDGE_MAX
} GnmStyleBorderLocation;

#define GNM_STYLE_BORDER_LOCATION_TO_STYLE_ELEMENT(sbl) ((GnmStyleElement)(MSTYLE_BORDER_TOP + (int)((sbl) - GNM_STYLE_BORDER_TOP)))

struct _GnmBorder {
	/* Key elements */
	GnmStyleBorderType line_type;
	GnmColor	*color;
	int		 begin_margin, end_margin, width;

	/* Private */
	gint	        ref_count;
};

void        gnm_border_shutdown (void);
GType       gnm_border_get_type    (void);
void	    gnm_style_border_unref (GnmBorder *border);
GnmBorder  *gnm_style_border_ref   (GnmBorder *border);

#define	gnm_style_border_is_blank(b) ((b) == NULL || (b)->line_type == GNM_STYLE_BORDER_NONE)
GnmBorder  *gnm_style_border_none  (void);
void        gnm_style_border_none_set_color (GnmColor *color);

GnmBorder  *gnm_style_border_fetch (GnmStyleBorderType line_type,
				    GnmColor *color,
				    GnmStyleBorderOrientation orientation);
gboolean gnm_style_border_visible_in_blank (GnmBorder const *border);

GnmStyleBorderOrientation gnm_style_border_get_orientation (GnmStyleBorderLocation type);

gint   gnm_style_border_get_width   (GnmStyleBorderType const line_type);
void gnm_style_border_set_dash (GnmStyleBorderType const i, cairo_t *context);

void gnm_style_borders_row_draw (GnmBorder const * const * prev_vert,
				 GnmStyleRow const *sr,
				 cairo_t *cr,
				 int x, int y1, int y2,
				 int *colwidths,
				 gboolean draw_vertical, int dir);
void gnm_style_border_draw_diag  (GnmStyle const *style,
				  cairo_t *cr,
				  int x1, int y1, int x2, int y2);

void gnm_style_borders_row_print_gtk (GnmBorder const * const * prev_vert,
				      GnmStyleRow const *sr,
				      cairo_t *context,
				      double x, double y1, double y2,
				      Sheet const *sheet,
				      gboolean draw_vertical, int dir);
void gnm_style_border_print_diag_gtk (GnmStyle const *style,
				      cairo_t *context,
				      double x1, double y1,
				      double x2, double y2);

G_END_DECLS

#endif /* _GNM_STYLE_BORDER_H_ */
