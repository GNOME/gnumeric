
#ifndef GNUMERIC_STYLE_H
#define GNUMERIC_STYLE_H

typedef struct {
	int      ref_count;
	char     *format;
} StyleFormat;

typedef struct {
	int      ref_count;
	char     *font_name;
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
	HALIGN_CENTER  =     8
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

typedef struct {
	StyleFormat   *format;
	StyleFont     *font;
	StyleBorder   *border;
	StyleShade    *shading;
	
	unsigned int halign:6;
	unsigned int valign:4;
	unsigned int orientation:4;
} Style;

typedef struct {
	int   row;
	int   height;
	Style style;
} RowStyle;

typedef struct {
	int   col;
	int   width;
	Style style;
} ColStyle;

typedef struct {
	int    sheet;
	Style  style;
} SheetStyle;

typedef struct {
	Style  style;
} WorkbookStyle;

#endif /* GNUMERIC_STYLE_H */
