#ifndef GNUMERIC_STYLE_H
#define GNUMERIC_STYLE_H

#include "gnumeric.h"
#include <pango/pango-context.h>

#define DEFAULT_FONT "Sans"
#define DEFAULT_SIZE 10.0

/* Alignment definitions */
/* Do not change these flags they are used as keys in the 1.0.x xml format.  */
typedef enum {
	HALIGN_GENERAL =  0x01,
	HALIGN_LEFT    =  0x02,
	HALIGN_RIGHT   =  0x04,
	HALIGN_CENTER  =  0x08,
	HALIGN_FILL    =  0x10,
	HALIGN_JUSTIFY =  0x20,
	HALIGN_CENTER_ACROSS_SELECTION =  0x40,
	HALIGN_DISTRIBUTED = 0x80
} GnmHAlign;

typedef enum {
	VALIGN_TOP     = 1,
	VALIGN_BOTTOM  = 2,
	VALIGN_CENTER  = 4,
	VALIGN_JUSTIFY = 8,
	VALIGN_DISTRIBUTED = 16
} GnmVAlign;

typedef enum {
	UNDERLINE_NONE   = 0,
	UNDERLINE_SINGLE = 1,
	UNDERLINE_DOUBLE = 2
} GnmUnderline;

typedef enum {
	GNM_TEXT_DIR_RTL	= -1,
	GNM_TEXT_DIR_CONTEXT	=  0,
	GNM_TEXT_DIR_LTR	=  1
} GnmTextDir;

#include "mstyle.h"

void           style_init  	      (void);
void	       style_shutdown         (void);

GnmFont     *style_font_new         (PangoContext *context,
				       const char *font_name,
				       double size_pts, double scale,
				       gboolean bold, gboolean italic);
void style_font_ref          (GnmFont *sf);
void style_font_unref        (GnmFont *sf);

guint          style_font_hash_func (gconstpointer v);
gint           style_font_equal (gconstpointer v, gconstpointer v2);

SpanCalcFlags	 required_updates_for_style (GnmStyle const *style);
GnmHAlign style_default_halign (GnmStyle const *mstyle, GnmCell const *c);

extern double gnumeric_default_font_width;

#endif /* GNUMERIC_STYLE_H */
