
#ifndef GNUMERIC_STYLE_H
#define GNUMERIC_STYLE_H

typedef struct {
	int      ref_count;
	char     *format;
} StyleFormat;

typedef struct {
	int      ref_count;
	char     *font_name;
	int      units;
	GdkFont  *font;
} StyleFont;

typedef enum {
	BORDER_NONE,
	BORDER_SOLID
} StyleBorderType;

typedef struct {
	int      ref_count;

	/*
	 * if the value is BorderNone, then the respective
	 * color is not allocated, otherwise, it has a
	 * valid color
	 */
	unsigned int left:4;
	unsigned int right:4;
	unsigned int top:4;
	unsigned int bottom:4;

	GdkColor left_color;
	GdkColor right_color;
	GdkColor top_color;
	GdkColor bottom_color;
} StyleBorder;

typedef struct {
	int ref_count;
	int pattern;
} StyleShade;

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

#define STYLE_FORMAT   1
#define STYLE_FONT     2
#define STYLE_BORDER   4
#define STYLE_SHADING  8
#define STYLE_ALIGN   16

#define STYLE_ALL (STYLE_FORMAT | STYLE_FONT | STYLE_BORDER | STYLE_SHADING | STYLE_ALIGN)

typedef struct {
	StyleFormat   *format;
	StyleFont     *font;
	StyleBorder   *border;
	StyleShade    *shading;
	
	unsigned int halign:6;
	unsigned int valign:4;
	unsigned int orientation:4;
	unsigned char valid_flags;
} Style;

void           style_init  	      (void);
Style         *style_new   	      (void);
Style         *style_duplicate        (Style *style);
void           style_destroy          (Style *style);
Style         *style_new_empty        (void);

StyleFormat   *style_format_new       (char *name);
void           style_format_ref       (StyleFormat *sf);
void           style_format_unref     (StyleFormat *sf);
				      
StyleFont     *style_font_new         (char *font_name, int units);
void           style_font_ref         (StyleFont *sf);
void           style_font_unref       (StyleFont *sf);

StyleBorder   *style_border_new_plain (void);
void           style_border_ref       (StyleBorder *sb);
void           style_border_unref     (StyleBorder *sb);
StyleBorder   *style_border_new       (StyleBorderType left,
				       StyleBorderType right,
				       StyleBorderType top,
				       StyleBorderType bottom,
				       GdkColor *left_color,
				       GdkColor *right_color,
				       GdkColor *top_color,
				       GdkColor *bottom_color);

#endif /* GNUMERIC_STYLE_H */
