/*
 * Style.c: Style resource management
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  (C) 1998, 1999 Miguel de Icaza
 */
#include <config.h>
#include <gnome.h>
#include <string.h>
#include "gnumeric.h"
#include "format.h"
#include "color.h"
#include "gnumeric-util.h"

#undef DEBUG_FONTS

static GHashTable *style_format_hash;
static GHashTable *style_font_hash;
static GHashTable *style_font_negative_hash;
static GHashTable *style_color_hash;

StyleFont *gnumeric_default_font;
StyleFont *gnumeric_default_bold_font;
StyleFont *gnumeric_default_italic_font;

StyleFormat *
style_format_new (const char *name)
{
	StyleFormat *format;

	g_return_val_if_fail (name != NULL, NULL);

	format = (StyleFormat *) g_hash_table_lookup (style_format_hash, name);

	if (!format){
		format = g_new0 (StyleFormat, 1);
		format->format = g_strdup (name);
		format_compile (format);
		g_hash_table_insert (style_format_hash, format->format, format);
	}
	format->ref_count++;

	return format;
}

void
style_format_ref (StyleFormat *sf)
{
	g_return_if_fail (sf != NULL);

	sf->ref_count++;
}

void
style_format_unref (StyleFormat *sf)
{
	g_return_if_fail (sf->ref_count > 0);

	sf->ref_count--;
	if (sf->ref_count != 0)
		return;

	g_hash_table_remove (style_format_hash, sf->format);

	format_destroy (sf);
	g_free (sf->format);
	g_free (sf);
}

#ifdef DEBUG_FONTS
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include "gdk/gdkprivate.h" /* Sorry */

static char *
my_gdk_actual_font_name (GdkFont *font)
{
	GdkFontPrivate *private;
	Atom atom, font_atom;

	private = (GdkFontPrivate *)font;
	font_atom = XInternAtom (private->xdisplay, "FONT", True);
	if (font_atom != None) {
		if (XGetFontProperty (private->xfont, font_atom, &atom) == True) {
			char *xname, *gname;
			xname = XGetAtomName (private->xdisplay, atom);
			gname = g_strdup ((xname && *xname) ? xname : "<unspecified>");
			XFree (xname);
			return gname;
		}
	}
	return NULL;
}
#endif

StyleFont *
style_font_new_simple (const char *font_name, double size, double scale, int bold, int italic)
{
	StyleFont *font;
	StyleFont key;

	g_return_val_if_fail (font_name != NULL, NULL);
	g_return_val_if_fail (size != 0, NULL);

	/* This cast does not mean we will change the name.  */
	key.font_name = (char *)font_name;
	key.size      = size;
	key.is_bold   = bold;
	key.is_italic = italic;
	key.scale     = scale;
	
	font = (StyleFont *) g_hash_table_lookup (style_font_hash, &key);
	if (!font){
		if (g_hash_table_lookup (style_font_negative_hash, &key))
			return NULL;

		font = g_new0 (StyleFont, 1);
		font->font_name = g_strdup (font_name);
		font->size      = size;
		font->scale     = scale;
		font->is_bold   = bold;
		font->is_italic = italic;

		font->dfont = gnome_get_display_font (
			font_name,
			bold ? GNOME_FONT_BOLD : GNOME_FONT_BOOK,
			italic,
			size, scale);

		if (!font->dfont) {
			g_hash_table_insert (style_font_negative_hash,
					     font, font);
			return NULL;
		}

		/*
		 * Worst case scenario
		 */
		if (font->dfont->gdk_font == NULL){
			font->dfont->gdk_font = gdk_font_load ("fixed");
		}
		
		font->font = gnome_font_new_closest (
			font_name,
			bold ? GNOME_FONT_BOLD : GNOME_FONT_BOOK,
			italic,
			size);

		g_hash_table_insert (style_font_hash, font, font);
	}

	font->ref_count++;
	return font;
}

StyleFont *
style_font_new (const char *font_name, double size, double scale, int bold, int italic)
{
	StyleFont *font;

	g_return_val_if_fail (font_name != NULL, NULL);
	g_return_val_if_fail (size != 0, NULL);

	font = style_font_new_simple (font_name, size, scale, bold, italic);
	if (!font){
		if (bold)
			font = gnumeric_default_bold_font;
		else if (italic)
			font = gnumeric_default_italic_font;
		else
			font = gnumeric_default_font;
		style_font_ref (font);
	}

	return font;
}

/*
 * Creates a new StyleFont from an existing StyleFont
 * at the scale @scale
 */
StyleFont *
style_font_new_from (StyleFont *sf, double scale)
{
	StyleFont *new_sf;
	
	g_return_val_if_fail (sf != NULL, NULL);
	g_return_val_if_fail (scale != 0.0, NULL);

	new_sf = style_font_new_simple (sf->font_name, sf->size, scale, sf->is_bold, sf->is_italic);
	if (!new_sf){
	        new_sf = gnumeric_default_font;
		style_font_ref (new_sf);
	}
	return new_sf;
}

GdkFont *
style_font_gdk_font (StyleFont *sf)
{
	g_return_val_if_fail (sf != NULL, NULL);

	return sf->dfont->gdk_font;
}

GnomeFont *
style_font_gnome_font (StyleFont *sf)
{
	g_return_val_if_fail (sf != NULL, NULL);

	return sf->dfont->gnome_font;
}

int
style_font_get_height (StyleFont *sf)
{
	GdkFont *gdk_font;
	GnomeFont *gnome_font;
	static int warning_shown;
	int height;

	g_return_val_if_fail (sf != NULL, 0);

	gdk_font = sf->dfont->gdk_font;

	height = gdk_font->ascent + gdk_font->descent;

	gnome_font = sf->dfont->gnome_font;
	
	if (height < gnome_font->size)
		height = gnome_font->size;

	if (!warning_shown){
		g_warning ("this should use a gnome-print provided method");
		warning_shown = 1;
	}
	return height;
}

void
style_font_ref (StyleFont *sf)
{
	g_return_if_fail (sf != NULL);

	sf->ref_count++;
}

void
style_font_unref (StyleFont *sf)
{
	g_return_if_fail (sf != NULL);
	g_return_if_fail (sf->ref_count > 0);

	sf->ref_count--;
	if (sf->ref_count != 0)
		return;

	gtk_object_unref (GTK_OBJECT (sf->font));

	g_hash_table_remove (style_font_hash, sf);
	g_free (sf->font_name);
	g_free (sf);
}

StyleColor *
style_color_new (gushort red, gushort green, gushort blue)
{
	StyleColor *sc;
	StyleColor key;

	key.red   = red;
	key.green = green;
	key.blue  = blue;

	sc = g_hash_table_lookup (style_color_hash, &key);
	if (!sc){
		sc = g_new (StyleColor, 1);

		key.color.red = red;
		key.color.green = green;
		key.color.blue = blue;
		sc->color = key.color;
		sc->red = red;
		sc->green = green;
		sc->blue = blue;
		
		sc->color.pixel = color_alloc (red, green, blue);

		g_hash_table_insert (style_color_hash, sc, sc);
		sc->ref_count = 0;
	}
	sc->ref_count++;

	return sc;
}

void
style_color_ref (StyleColor *sc)
{
	g_return_if_fail (sc != NULL);

	sc->ref_count++;
}

void
style_color_unref (StyleColor *sc)
{
	g_return_if_fail (sc != NULL);
	g_return_if_fail (sc->ref_count > 0);

	sc->ref_count--;
	if (sc->ref_count != 0)
		return;

	/*
	 * There is no need to deallocate colors, as they come from
	 * the GDK Color Context
	 */
	g_hash_table_remove (style_color_hash, sc);
	g_free (sc);
}

Style *
style_new (void)
{
	Style *style;

	style = g_new0 (Style, 1);

	style->valid_flags = STYLE_ALL;

	style->format      = style_format_new ("General");
	style->font        = style_font_new (DEFAULT_FONT, DEFAULT_SIZE, 1.0, FALSE, FALSE);
	style->fore_color  = style_color_new (0, 0, 0);
	style->pattern_color  = style_color_new (0, 0, 0);
	style->back_color  = style_color_new (0xffff, 0xffff, 0xffff);
	style->halign      = HALIGN_GENERAL;
	style->valign      = VALIGN_CENTER;
	style->orientation = ORIENT_HORIZ;
	style->border_top = NULL;
	style->border_left = NULL;
	style->border_bottom = NULL;
	style->border_right = NULL;
	style->border_diagonal = NULL;		/* Unsupported */
	style->border_rev_diagonal = NULL;	/* Unsupported */

	g_warning ("Style new is deprecated: root it out");
	if (0) {
		static int warning_shown;

		/* Why this warning?  */
		if (!warning_shown){
			g_message ("Font style created at zoom factor 1.0");
			warning_shown = TRUE;
		}
	}

	return style;
}

Style *
style_mstyle_new (MStyleElement *e, guint len,
		  gdouble zoom)
{
	Style *style;

	style = g_new0 (Style, 1);

	style->format = style_format_new ("General");
	style->valign = VALIGN_CENTER;
	style->halign = HALIGN_GENERAL;
	style->orientation = ORIENT_HORIZ;

	if (len > MSTYLE_ELEMENT_MAX_BLANK) {
		gchar *name;
		int    bold, italic;
		double size;

		style->valid_flags = STYLE_ALL;
		
		if (e[MSTYLE_FONT_NAME].type)
			name = e[MSTYLE_FONT_NAME].u.font.name->str;
		else
			name = DEFAULT_FONT;
		if (e[MSTYLE_FONT_BOLD].type)
			bold = e[MSTYLE_FONT_BOLD].u.font.bold;
		else
			bold = 0;
		if (e[MSTYLE_FONT_ITALIC].type)
			italic = e[MSTYLE_FONT_ITALIC].u.font.italic;
		else
			italic = 0;
		if (e[MSTYLE_FONT_SIZE].type)
			size =  e[MSTYLE_FONT_SIZE].u.font.size;
		else
			size = DEFAULT_SIZE;

		if (bold || italic || (name != DEFAULT_FONT) ||
		    (size != DEFAULT_SIZE))
			style->font = style_font_new (name, size, zoom,
						      bold, italic);
		else {
			style->font = gnumeric_default_font;
			style_font_ref (style->font);
		}

		if (e[MSTYLE_ALIGN_V].type)
			style->valign = e[MSTYLE_ALIGN_V].u.align.v;

		if (e[MSTYLE_ALIGN_H].type)
			style->halign = e[MSTYLE_ALIGN_V].u.align.h;

		if (e[MSTYLE_ORIENTATION].type)
			style->orientation = e[MSTYLE_ORIENTATION].u.orientation;

		if (e[MSTYLE_COLOR_FORE].type) {
			style->fore_color = e[MSTYLE_COLOR_FORE].u.color.fore;
			style_color_ref (style->fore_color);
		} else
			style->fore_color = style_color_new (0, 0, 0);
	} else
		style->valid_flags = STYLE_PATTERN_COLOR | STYLE_BACK_COLOR | STYLE_PATTERN
			| STYLE_BORDER_TOP | STYLE_BORDER_LEFT
			| STYLE_BORDER_BOTTOM | STYLE_BORDER_RIGHT;

	/* Styles that will show up on blank cells */
	if (e[MSTYLE_COLOR_BACK].type) {
		style->back_color = e[MSTYLE_COLOR_BACK].u.color.back;
		style_color_ref (style->back_color);
	} else
		style->back_color = style_color_new (0xffff, 0xffff, 0xffff);

	if (e[MSTYLE_COLOR_PATTERN].type) {
		style->pattern_color = e[MSTYLE_COLOR_PATTERN].u.color.pattern;
		style_color_ref (style->pattern_color);
	} else
		style->pattern_color = style_color_new (0x0000, 0x0000, 0x0000);

	if (e[MSTYLE_FIT_IN_CELL].type)
		style->fit_in_cell = e[MSTYLE_FIT_IN_CELL].u.fit_in_cell;
	else
		style->fit_in_cell = 0;

	if (e[MSTYLE_BORDER_TOP].type)
		style->border_top = e[MSTYLE_BORDER_TOP].u.border.top;
	else
		style->border_top = NULL;
	if (e[MSTYLE_BORDER_LEFT].type)
		style->border_left = e[MSTYLE_BORDER_LEFT].u.border.left;
	else
		style->border_left = NULL;
	if (e[MSTYLE_BORDER_BOTTOM].type)
		style->border_bottom = e[MSTYLE_BORDER_BOTTOM].u.border.bottom;
	else
		style->border_bottom = NULL;
	if (e[MSTYLE_BORDER_RIGHT].type)
		style->border_right = e[MSTYLE_BORDER_RIGHT].u.border.right;
	else
		style->border_right = NULL;
	if (e[MSTYLE_BORDER_DIAGONAL].type)
		style->border_diagonal = e[MSTYLE_BORDER_DIAGONAL].u.border.diagonal;
	else
		style->border_diagonal = NULL;
	if (e[MSTYLE_BORDER_REV_DIAGONAL].type)
		style->border_rev_diagonal = e[MSTYLE_BORDER_REV_DIAGONAL].u.border.rev_diagonal;
	else
		style->border_rev_diagonal = NULL;

	return style;
}

Style *
style_new_empty (void)
{
	Style *style;

	style = g_new0 (Style, 1);

	style->valid_flags = 0;

	return style;
}

void
style_destroy (Style *style)
{
	g_return_if_fail (style != NULL);

	if (style->valid_flags & STYLE_FORMAT)
		style_format_unref (style->format);

	if (style->valid_flags & STYLE_FONT)
		style_font_unref (style->font);

	if (style->valid_flags & STYLE_FORE_COLOR)
		if (style->fore_color)
			style_color_unref (style->fore_color);

	if (style->valid_flags & STYLE_BACK_COLOR)
		if (style->back_color)
			style_color_unref (style->back_color);

	g_free (style);
}


Style *
style_duplicate (const Style *original)
{
	Style *style;

	style = g_new0 (Style, 1);

	/* Bit copy */
	*style = *original;

	/* Do the rest of the copy */
	if (style->valid_flags & STYLE_FORMAT)
		style_format_ref (style->format);
	else
		style->format = NULL;

	if (style->valid_flags & STYLE_FONT)
		style_font_ref (style->font);
	else
		style->font = NULL;

	if (style->valid_flags & STYLE_FORE_COLOR)
		style_color_ref (style->fore_color);
	else
		style->fore_color = NULL;

	if (style->valid_flags & STYLE_BACK_COLOR)
		style_color_ref (style->back_color);
	else
		style->back_color = NULL;

	return style;
}

/*
 * The routines used to hash and compare the different styles
 */
static gint
font_equal (gconstpointer v, gconstpointer v2)
{
	const StyleFont *k1 = (const StyleFont *) v;
	const StyleFont *k2 = (const StyleFont *) v2;

	if (k1->size != k2->size)
		return 0;

	if (k1->is_bold != k2->is_bold)
		return 0;
	if (k1->is_italic != k2->is_italic)
		return 0;
	if (k1->scale != k2->scale)
		return 0;

	return !strcmp (k1->font_name, k2->font_name);
}

static guint
font_hash (gconstpointer v)
{
	const StyleFont *k = (const StyleFont *) v;

	return k->size + g_str_hash (k->font_name);
}

static gint
color_equal (gconstpointer v, gconstpointer v2)
{
	const StyleColor *k1 = (const StyleColor *) v;
	const StyleColor *k2 = (const StyleColor *) v2;

	if (k1->red   == k2->red &&
	    k1->green == k2->green &&
	    k1->blue  == k2->blue)
		return 1;

	return 0;
}

static guint
color_hash (gconstpointer v)
{
	const StyleColor *k = (const StyleColor *)v;

	return (k->red << 16) | (k->green << 8) | (k->blue);
}

void
style_merge_to (Style *target, Style *source)
{
	g_warning ("Deprecated style_merge_to");
}

static void
font_init (void)
{
	gnumeric_default_font = style_font_new_simple (DEFAULT_FONT, DEFAULT_SIZE, 1.0, FALSE, FALSE);

	if (!gnumeric_default_font) {
		fprintf (stderr,
			 "Gnumeric failed to find a suitable default font.\n"
			 "\n"
			 "Please verify your gnome-print installation and that your fontmap file\n"
			 "(typically located in /usr/local/share/fonts/fontmap) is not empty or\n"
			 "near empty.\n"
			 "\n"
			 "If you still have no luck, please file a proper bug report (see\n"
			 "http://bugs.gnome.org) including the following extra items:\n"
			 "\n"
			 "1. Values of LC_ALL and LANG environment variables.\n"
			 "2. Your fontmap file, see above.\n"
			 "\n"
			 "Thanks -- the Gnumeric Team\n");
		exit (1);
	}

	/*
	 * Load bold font
	 */
	gnumeric_default_bold_font = style_font_new_simple (
		DEFAULT_FONT, DEFAULT_SIZE, 1.0, TRUE, FALSE);
	if (gnumeric_default_bold_font == NULL){
	    gnumeric_default_bold_font = gnumeric_default_font;
	    style_font_ref (gnumeric_default_bold_font);
	}

	/*
	 * Load italic font
	 */
	gnumeric_default_italic_font = style_font_new_simple (
		DEFAULT_FONT, DEFAULT_SIZE, 1.0, FALSE, TRUE);
	if (gnumeric_default_italic_font == NULL){
		gnumeric_default_italic_font = gnumeric_default_font;
		style_font_ref (gnumeric_default_italic_font);
	}
}

void
style_init (void)
{
	style_format_hash = g_hash_table_new (g_str_hash, g_str_equal);
	style_font_hash   = g_hash_table_new (font_hash, font_equal);
	style_color_hash  = g_hash_table_new (color_hash, color_equal);

	style_font_negative_hash = g_hash_table_new (font_hash, font_equal);

	font_init ();
}

static void
delete_neg_font (gpointer key, gpointer value, gpointer user_data)
{
	StyleFont *font = key;

	g_free (font->font_name);
	g_free (font);
}

/*
 * Release all resources allocated by style_init.
 */
void
style_shutdown (void)
{
	style_font_unref (gnumeric_default_font);
	gnumeric_default_font = NULL;
	style_font_unref (gnumeric_default_bold_font);
	gnumeric_default_bold_font = NULL;
	style_font_unref (gnumeric_default_italic_font);
	gnumeric_default_italic_font = NULL;

	g_hash_table_destroy (style_format_hash);
	style_format_hash = NULL;
	g_hash_table_destroy (style_font_hash);
	style_font_hash = NULL;
	g_hash_table_destroy (style_color_hash);
	style_color_hash = NULL;

	g_hash_table_foreach (style_font_negative_hash, delete_neg_font, NULL);
	g_hash_table_destroy (style_font_negative_hash);
	style_font_negative_hash = NULL;
}
