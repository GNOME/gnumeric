#ifndef GNUMERIC_STYLE_H
#define GNUMERIC_STYLE_H

#include <gdk/gdk.h>
#include <libgnomeprint/gnome-font.h>
#include "gnumeric.h"

#define DEFAULT_FONT "Helvetica"
#define DEFAULT_SIZE 9.0

/* Alignment definitions */
typedef enum _StyleHAlignFlags {
	HALIGN_GENERAL =  0x01,
	HALIGN_LEFT    =  0x02,
	HALIGN_RIGHT   =  0x04,
	HALIGN_CENTER  =  0x08,
	HALIGN_FILL    =  0x10,
	HALIGN_JUSTIFY =  0x20,
	HALIGN_CENTER_ACROSS_SELECTION =  0x40
} StyleHAlignFlags;

typedef enum _StyleVAlignFlags {
	VALIGN_TOP     = 1,
	VALIGN_BOTTOM  = 2,
	VALIGN_CENTER  = 4,
	VALIGN_JUSTIFY = 8
} StyleVAlignFlags;

typedef enum _StyleUnderlineType {
	UNDERLINE_NONE   = 0,
	UNDERLINE_SINGLE = 1,
	UNDERLINE_DOUBLE = 2
} StyleUnderlineType;

typedef enum _StyleOrientation {
	ORIENT_HORIZ           = 1,
	ORIENT_VERT_HORIZ_TEXT = 2,
	ORIENT_VERT_VERT_TEXT  = 4,
	ORIENT_VERT_VERT_TEXT2 = 8
} StyleOrientation;

struct _StyleFont {
	int	 ref_count;
	char	*font_name;
	float	 size_pts;
	float	 scale;
	struct {
		float pixels;   /* this does not belong here */
		float pts;
	} approx_width;
	struct {
		PangoContext		*context;
		PangoFontMetrics	*metrics;
		PangoFont	  	*font;
	} pango;

	GdkFont	*gdk_font;

	unsigned int is_bold:1;
	unsigned int is_italic:1;
};

void           style_init  	      (void);
void	       style_shutdown         (void);

StyleFont     *style_font_new         (const char *font_name,
				       double size_pts, double scale,
				       gboolean bold, gboolean italic);
StyleFont     *style_font_new_simple  (const char *font_name,
				       double size_pts, double scale,
				       gboolean bold, gboolean italic);
GdkFont       *style_font_gdk_font    (StyleFont const *sf);
int            style_font_get_height  (StyleFont const *sf);
float	       style_font_get_width_pts(StyleFont const *sf);
void           style_font_ref         (StyleFont *sf);
void           style_font_unref       (StyleFont *sf);

/*
 * For hashing Styles
 */
guint          style_hash    (gconstpointer a);
gint           style_compare (gconstpointer a, gconstpointer b);

guint          style_font_hash_func (gconstpointer v);
gint           style_font_equal (gconstpointer v, gconstpointer v2);

SpanCalcFlags	 required_updates_for_style (MStyle *style);
StyleHAlignFlags style_default_halign (MStyle const *mstyle, Cell const *c);

extern StyleFont *gnumeric_default_font;
extern StyleFont *gnumeric_default_bold_font;
extern StyleFont *gnumeric_default_italic_font;

#include "mstyle.h"

#endif /* GNUMERIC_STYLE_H */
