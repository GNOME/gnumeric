#ifndef GNUMERIC_STYLE_H
#define GNUMERIC_STYLE_H

#include <gdk/gdk.h>
#include <libgnomeprint/gnome-font.h>

typedef struct _StyleFont    StyleFont;
typedef struct _StyleColor   StyleColor;
typedef struct _StyleElement StyleElement;

#include "render.h"

/**
 *  The order or the following two records
 * is assumed in xml-io
 **/
typedef enum {
 	BORDER_NONE,
 	BORDER_THIN,
 	BORDER_MEDIUM,
 	BORDER_DASHED,
 	BORDER_DOTTED,
 	BORDER_THICK,
 	BORDER_DOUBLE,
 	BORDER_HAIR
} StyleBorderType;

#define NUM_STYLE_BORDER 8

typedef enum {
	STYLE_TOP,
 	STYLE_BOTTOM,
 	STYLE_LEFT,
 	STYLE_RIGHT
} StyleSide;

/* Alignment definitions */
typedef enum {
	HALIGN_GENERAL =     1,
	HALIGN_LEFT    =     2,
	HALIGN_RIGHT   =     4,
	HALIGN_CENTER  =     8,
	HALIGN_FILL    =  0x10,
	HALIGN_JUSTIFY =  0x20
} StyleHAlignFlags;

typedef enum {
	VALIGN_TOP     = 1,
	VALIGN_BOTTOM  = 2,
	VALIGN_CENTER  = 4,
	VALIGN_JUSTIFY = 8
} StyleVAlignFlags;

typedef enum {
	ORIENT_HORIZ           = 1,
	ORIENT_VERT_HORIZ_TEXT = 2,
	ORIENT_VERT_VERT_TEXT  = 4,
	ORIENT_VERT_VERT_TEXT2 = 8
} StyleOrientation;

struct _StyleFont {
	int                ref_count;
	char              *font_name;
	double             size;
	double             scale;
	GnomeDisplayFont  *dfont;
	GnomeFont         *font;

	unsigned int is_bold:1;
	unsigned int is_italic:1;
};

struct _StyleColor {
	int      ref_count;
	GdkColor color;
	char     *name;
};

typedef enum {
	/* Delimiter */
	STYLE_ELEMENT_ZERO = 0,
	/* Types that are visible in blank cells */
	        STYLE_COLOR_FORE,
		STYLE_COLOR_BACK,
	/* Delimiter */
	STYLE_ELEMENT_MAX_BLANK,
	/* Normal types */
		STYLE_FONT_NAME,
		STYLE_FONT_BOLD,
		STYLE_FONT_ITALIC,
	        STYLE_FONT_SCALE,
	/* Delimiter */
	STYLE_ELEMENT_MAX
} StyleElementType;

struct _StyleElement {
	StyleElementType type;
	union {
		union {
			StyleColor *fore;
			StyleColor *back;
		} color;
		union {
			gchar    *name;
			gboolean  bold;
			gboolean  italic;
			gdouble   scale;
		} font;
	} u;
};

typedef struct {
	gchar   *name;
	guint32  stamp;
	GArray  *elements;
} Style;

typedef struct {
	int start_col, start_row;
	int end_col, end_row;
} Range;

typedef struct {
	Range  range;
	Style  *style;
} StyleRegion;

Style      *style_new         (const gchar *name);
Style      *style_new_elem    (const gchar *name, StyleElement e);
Style      *style_new_array   (const gchar *name, const GArray *elements);
/* No pre-existance checking */
void        style_add         (Style *st, StyleElement e);
void        style_add_array   (Style *st, const GArray *elements);
/* Checks to see if it is alreqady in use */
void        style_set         (Style *st, StyleElement e);

Style      *style_merge       (const Style *sta,  const Style *stb); /* commutative */
void        style_destroy     (Style *st);
char        *style_to_string  (const Style *st); /* Debug only ! leaks like a sieve */

RenderInfo *render_merge       (const GList *styles);
RenderInfo *render_merge_blank (const GList *styles);

#endif /* GNUMERIC_STYLE_H */
