#ifndef GNUMERIC_STYLE_H
#define GNUMERIC_STYLE_H

#include <gdk/gdk.h>
#include <libgnomeprint/gnome-font.h>
#include "gnumeric.h"

#define DEFAULT_FONT "Helvetica"
#define DEFAULT_SIZE 9.0

/* Alignment definitions */
enum _StyleHAlignFlags {
	HALIGN_GENERAL =  0x01,
	HALIGN_LEFT    =  0x02,
	HALIGN_RIGHT   =  0x04,
	HALIGN_CENTER  =  0x08,
	HALIGN_FILL    =  0x10,
	HALIGN_JUSTIFY =  0x20,
	HALIGN_CENTER_ACROSS_SELECTION =  0x40
};

enum _StyleVAlignFlags {
	VALIGN_TOP     = 1,
	VALIGN_BOTTOM  = 2,
	VALIGN_CENTER  = 4,
	VALIGN_JUSTIFY = 8
};

enum _StyleUnderlineType {
	UNDERLINE_NONE   = 0,
	UNDERLINE_SINGLE = 1,
	UNDERLINE_DOUBLE = 2,
};

enum _StyleOrientation {
	ORIENT_HORIZ           = 1,
	ORIENT_VERT_HORIZ_TEXT = 2,
	ORIENT_VERT_VERT_TEXT  = 4,
	ORIENT_VERT_VERT_TEXT2 = 8
};

struct _StyleFont {
	int                ref_count;
	char              *font_name;
	float             size;
	float             scale;
	float		  approx_width;
	GnomeDisplayFont  *dfont;
	GnomeFont         *font;
	GdkFont		  *gdk_font;

	unsigned int is_bold:1;
	unsigned int is_italic:1;
};

struct _StyleColor {
	int      ref_count;
	GdkColor color;
	GdkColor selected_color;
	char     *name;
	gushort  red;
	gushort  green;
	gushort  blue;
};

/* The order corresponds to the border_buttons name list
 * in dialog_cell_format_impl */
enum _StyleBorderLocation {
	STYLE_BORDER_TOP,	STYLE_BORDER_BOTTOM,
	STYLE_BORDER_LEFT,	STYLE_BORDER_RIGHT,
	STYLE_BORDER_REV_DIAG,STYLE_BORDER_DIAG,

	/* These are special.
	 * They are logical rather than actual borders, however, they
	 * require extra lines to be drawn so they need to be here.
	 */
	STYLE_BORDER_HORIZ, STYLE_BORDER_VERT,

	STYLE_BORDER_EDGE_MAX
};

void           style_init  	      (void);
void	       style_shutdown         (void);

StyleFont     *style_font_new         (const char *font_name,
				       double size, double scale,
				       gboolean bold, gboolean italic);
StyleFont     *style_font_new_simple  (const char *font_name,
				       double size, double scale,
				       gboolean bold, gboolean italic);
GdkFont       *style_font_gdk_font    (StyleFont const *sf);
int            style_font_get_height  (StyleFont const *sf);
float	       style_font_get_width   (StyleFont const *sf);
void           style_font_ref         (StyleFont *sf);
void           style_font_unref       (StyleFont *sf);

StyleColor    *style_color_new        (gushort red, gushort green, gushort blue);
StyleColor    *style_color_ref        (StyleColor *sc);
void           style_color_unref      (StyleColor *sc);
StyleColor    *style_color_black      (void);
StyleColor    *style_color_white      (void);

/*
 * For hashing Styles
 */
guint          style_hash    (gconstpointer a);
gint           style_compare (gconstpointer a, gconstpointer b);

guint          style_font_hash_func (gconstpointer v);
gint           style_font_equal (gconstpointer v, gconstpointer v2);

extern StyleFont *gnumeric_default_font;
extern StyleFont *gnumeric_default_bold_font;
extern StyleFont *gnumeric_default_italic_font;

#include "mstyle.h"

#endif /* GNUMERIC_STYLE_H */
