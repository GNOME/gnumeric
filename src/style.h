#ifndef GNUMERIC_STYLE_H
#define GNUMERIC_STYLE_H

#include <gdk/gdk.h>

typedef struct {
        char     *format;
	int      want_am_pm;
        char     restriction_type;
        int      restriction_value;
} StyleFormatEntry;

typedef struct {
	int      ref_count;
        GList    *format_list;  /* Of type StyleFormatEntry. */
	char     *format;
} StyleFormat;

typedef struct {
	int      ref_count;
	char     *font_name;
	int      units;
	GdkFont  *font;

	/*
	 * These are runtime optimizations, there is no need to save these
	 * they get computed at StyleFont creation time.
	 */
	unsigned int hint_is_bold:1;
	unsigned int hint_is_italic:1;
} StyleFont;

typedef struct {
	int      ref_count;
	GdkColor color;
	char     *name;
} StyleColor;

/**
 *  The order or the following two records
 * is assumed in xml-io
 **/
typedef enum {
 	BORDER_NONE=0,
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
	STYLE_TOP=0,
 	STYLE_BOTTOM=1,
 	STYLE_LEFT=2,
 	STYLE_RIGHT=3
} StyleSide;

typedef struct {
	int      ref_count;

	/**
	 * if the value is BORDER_NONE, then the respective
	 * color is not allocated, otherwise, it has a
	 * valid color.
	 * NB. Use StyleSide to get orientation
	 **/
 	StyleBorderType type[4] ;
 	StyleColor  *color[4] ;
} StyleBorder;

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

typedef struct {
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
} Style;

void           style_init  	      (void);
void	       style_shutdown         (void);

Style         *style_new   	      (void);
void           style_merge_to         (Style *target, Style *source);
Style         *style_duplicate        (const Style *style);
void           style_destroy          (Style *style);
Style         *style_new_empty        (void);

StyleFormat   *style_format_new       (const char *name);
void           style_format_ref       (StyleFormat *sf);
void           style_format_unref     (StyleFormat *sf);
				      
StyleFont     *style_font_new         (const char *font_name, int units);
StyleFont     *style_font_new_simple  (const char *font_name, int units);
void           style_font_ref         (StyleFont *sf);
void           style_font_unref       (StyleFont *sf);

StyleColor    *style_color_new        (gushort red, gushort green, gushort blue);
void           style_color_ref        (StyleColor *sc);
void           style_color_unref      (StyleColor *sc);

StyleBorder   *style_border_new_plain (void);
void           style_border_ref       (StyleBorder *sb);
void           style_border_unref     (StyleBorder *sb);
StyleBorder   *style_border_new       (StyleBorderType border_type[4],
 				       StyleColor *border_color[4]);

extern StyleFont *gnumeric_default_font;
extern StyleFont *gnumeric_default_bold_font;
extern StyleFont *gnumeric_default_italic_font;

#endif /* GNUMERIC_STYLE_H */
