#ifndef GNUMERIC_STYLE_H
#define GNUMERIC_STYLE_H

#include <gdk/gdk.h>
#include <libgnomeprint/gnome-font.h>

#define DEFAULT_FONT "Helvetica"
#define DEFAULT_SIZE 10.0

typedef struct _MStyleBorder      MStyleBorder;

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
	int                ref_count;
	char              *font_name;
	double             size;
	double             scale;
	GnomeDisplayFont  *dfont;
	GnomeFont         *font;

	unsigned int is_bold:1;
	unsigned int is_italic:1;
} StyleFont;

typedef struct {
	int      ref_count;
	GdkColor color;
	char     *name;
	gushort  red;
	gushort  green;
	gushort  blue;
} StyleColor;

/* The order corresponds to the border_buttons name list
 * in dialog_cell_format_impl */
typedef enum
{
	STYLE_BORDER_TOP,	STYLE_BORDER_BOTTOM,
	STYLE_BORDER_LEFT,	STYLE_BORDER_RIGHT,
	STYLE_BORDER_REV_DIAG,STYLE_BORDER_DIAG,

	/* These are special.
	 * They are logical rather than actual borders, however, they
	 * require extra lines to be drawn so they need to be here.
	 */
	STYLE_BORDER_HORIZ, STYLE_BORDER_VERT,

	STYLE_BORDER_EDGE_MAX
} StyleBorderLocation;

void           style_init  	      (void);
void	       style_shutdown         (void);

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
