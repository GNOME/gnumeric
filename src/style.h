#ifndef GNUMERIC_STYLE_H
#define GNUMERIC_STYLE_H

#include <gdk/gdk.h>
#include <libgnomeprint/gnome-font.h>

typedef struct _Style            Style;
typedef struct _StyleFont        StyleFont;
typedef struct _StyleColor       StyleColor;
typedef struct _StyleBorder      StyleBorder;
typedef struct _StyleFormat      StyleFormat;
typedef struct _StyleFormatEntry StyleFormatEntry;

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

/**
 *  The order of the following two records
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
 	BORDER_HAIR,

 	BORDER_MAX
} StyleBorderType;

typedef enum {
	STYLE_TOP    = 0,
 	STYLE_BOTTOM = 1,
 	STYLE_LEFT   = 2,
 	STYLE_RIGHT  = 3,
 	STYLE_ORIENT_MAX  = 4
} StyleBorderOrient;

#include "mstyle.h"

struct _StyleFormatEntry {
        char     *format;
	int      want_am_pm;
        char     restriction_type;
        int      restriction_value;
};

struct _StyleFormat {
	int      ref_count;
        GList    *format_list;  /* Of type StyleFormatEntry. */
	char     *format;
};

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
	gushort  red;
	gushort  green;
	gushort  blue;
};

struct _StyleBorder {
	int      ref_count;

	/**
	 * if the value is BORDER_NONE, then the respective
	 * color is not allocated, otherwise, it has a
	 * valid color.
	 * NB. Use StyleBorderOrient to get orientation
	 **/
 	StyleBorderType type[STYLE_ORIENT_MAX];
 	StyleColor  *color[STYLE_ORIENT_MAX];
};

#define STYLE_FORMAT       1
#define STYLE_FONT         2
#define STYLE_BORDER       4
#define STYLE_PATTERN      8
#define STYLE_ALIGN       16
#define STYLE_FORE_COLOR  32
#define STYLE_BACK_COLOR  64
#define STYLE_MAXIMUM    128

/* Define all of the styles we actually know about */
#define STYLE_ALL (STYLE_FORMAT | STYLE_FONT | STYLE_BORDER | STYLE_ALIGN | \
		   STYLE_PATTERN | STYLE_FORE_COLOR | STYLE_BACK_COLOR)

struct _Style {
	StyleFormat   *format;
	StyleFont     *font;
	StyleBorder   *border;
	StyleColor    *fore_color;
	StyleColor    *back_color;

	unsigned int pattern:4;
	unsigned int valign:4;
	unsigned int halign:6;
	unsigned int orientation:4;
	unsigned int fit_in_cell:1;
	
	unsigned char valid_flags;
};

void           style_init  	      (void);
void	       style_shutdown         (void);

Style         *style_new   	      (void);
Style         *style_mstyle_new       (MStyleElement *e, guint len);
void           style_merge_to         (Style *target, Style *source);
Style         *style_duplicate        (const Style *style);
void           style_destroy          (Style *style);
Style         *style_new_empty        (void);

StyleFormat   *style_format_new       (const char *name);
void           style_format_ref       (StyleFormat *sf);
void           style_format_unref     (StyleFormat *sf);
				      
StyleFont     *style_font_new         (const char *font_name,
				       double size, double scale,
				       int bold, int italic);
StyleFont     *style_font_new_from    (StyleFont *sf, double scale);
StyleFont     *style_font_new_simple  (const char *font_name,
				       double size, double scale,
				       int bold, int italic);
GdkFont       *style_font_gdk_font    (StyleFont *sf);
GnomeFont     *style_font_gnome_font  (StyleFont *sf);
int            style_font_get_height  (StyleFont *sf);
void           style_font_ref         (StyleFont *sf);
void           style_font_unref       (StyleFont *sf);

StyleColor    *style_color_new        (gushort red, gushort green, gushort blue);
void           style_color_ref        (StyleColor *sc);
void           style_color_unref      (StyleColor *sc);

StyleBorder   *style_border_new_plain (void);
void           style_border_ref       (StyleBorder *sb);
void           style_border_unref     (StyleBorder *sb);
StyleBorder   *style_border_new       (StyleBorderType const border_type[STYLE_ORIENT_MAX],
 				       StyleColor *border_color[STYLE_ORIENT_MAX]);

/*
 * For hashing Styles
 */
guint          style_hash    (gconstpointer a);
gint           style_compare (gconstpointer a, gconstpointer b);

extern StyleFont *gnumeric_default_font;
extern StyleFont *gnumeric_default_bold_font;
extern StyleFont *gnumeric_default_italic_font;

#endif /* GNUMERIC_STYLE_H */

