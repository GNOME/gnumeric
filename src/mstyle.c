/* vim: set sw=8: */
/*
 * MStyle.c: The guts of the style engine.
 *
 * Author:
 *   Michael Meeks <mmeeks@gnu.org>
 *
 * Contributors:
 *   Almer S. Tigelaar <almer@gnome.org>
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "mstyle.h"

#include "str.h"
#include "style-border.h"
#include "style-color.h"
#include "validation.h"
#include "pattern.h"
#include "format.h"
#include "sheet-style.h"

typedef struct {
	MStyleElementType type;
	union {
		union {
			StyleColor *any;
			StyleColor *fore;
			StyleColor *back;
			StyleColor *pattern;
		}                color;
		union {
			StyleBorder *top;
			StyleBorder *bottom;
			StyleBorder *left;
			StyleBorder *right;
			StyleBorder *diagonal;
			StyleBorder *rev_diagonal;

			/* Used for loading */
			StyleBorder *any;
		}                border;
		guint32          pattern;

		union {
			String   *name;
			gboolean  bold;
			gboolean  italic;
			StyleUnderlineType  underline;
			gboolean  strikethrough;
			float     size;
		}                font;
		StyleFormat     *format;
		union {
			guint16   v;
			guint16   h;
		}                align;
		int		 indent;
		StyleOrientation orientation;
		gboolean         wrap_text;
		gboolean         content_locked;
		gboolean         content_hidden;

		Validation      *validation;

		/* Convenience members */
		gpointer         any_pointer;
		gboolean         any_boolean;
		float            any_float;
		guint16          any_guint16;
		guint32          any_guint32;
	} u;
} MStyleElement;

struct _MStyle {
	guint32        ref_count;
	guint32        link_count;
	Sheet	      *linked_sheet;
	MStyleElement  elements[MSTYLE_ELEMENT_MAX];
};

#define MSTYLE_ANY_COLOR             MSTYLE_COLOR_FORE: \
				case MSTYLE_COLOR_BACK: \
				case MSTYLE_COLOR_PATTERN

#define MSTYLE_ANY_BORDER            MSTYLE_BORDER_TOP: \
				case MSTYLE_BORDER_BOTTOM: \
				case MSTYLE_BORDER_LEFT: \
				case MSTYLE_BORDER_RIGHT: \
				case MSTYLE_BORDER_DIAGONAL: \
				case MSTYLE_BORDER_REV_DIAGONAL

#define MSTYLE_ANY_POINTER           MSTYLE_FONT_NAME: \
				case MSTYLE_FORMAT: \
				case MSTYLE_VALIDATION

#define MSTYLE_ANY_BOOLEAN           MSTYLE_FONT_BOLD: \
				case MSTYLE_FONT_ITALIC: \
				case MSTYLE_FONT_STRIKETHROUGH: \
				case MSTYLE_WRAP_TEXT:\
				case MSTYLE_CONTENT_LOCKED:\
				case MSTYLE_CONTENT_HIDDEN

#define MSTYLE_ANY_GUINT16           MSTYLE_ALIGN_V: \
                                case MSTYLE_ALIGN_H

#define MSTYLE_ANY_GUINT32           MSTYLE_PATTERN

#define MSTYLE_ANY_FLOAT             MSTYLE_FONT_SIZE


const char *mstyle_names[MSTYLE_ELEMENT_MAX] = {
	"--UnSet--",
	"--Conflict--",
	"Color.Back",
	"Color.Pattern",
	"Border.Top",
	"Border.Bottom",
	"Border.Left",
	"Border.Right",
	"Border.RevDiagonal",
	"Border.Diagonal",
	"Pattern",
	"--MaxBlank--",
	"Color.Fore",
	"Font.Name",
	"Font.Bold",
	"Font.Italic",
	"Font.Underline",
	"Font.Strikethrough",
	"Font.Size",
	"Format",
	"Align.v",
	"Align.h",
	"Indent",
	"Orientation",
	"WrapText",
	"Content.Locked",
	"Content.Hidden",
	"Validation"
};

/* Some ref/link count debugging */
#if 0
#define d(arg)	printf arg
#else
#define d(arg)	do { } while (0)
#endif

guint
mstyle_hash (gconstpointer st)
{
	const MStyle *mstyle = (const MStyle *)st;
	int     i;
	guint32 hash = 0;

	for (i = MSTYLE_ELEMENT_CONFLICT + 1; i < MSTYLE_ELEMENT_MAX; i++) {
		const MStyleElement *e = &mstyle->elements[i];
		hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
		switch (i) {
		case MSTYLE_ANY_COLOR:
			hash = hash ^ GPOINTER_TO_UINT (e->u.color.any);
			break;
		case MSTYLE_ANY_BORDER:
			hash = hash ^ GPOINTER_TO_UINT (e->u.border.any);
			break;
		case MSTYLE_ANY_POINTER:
			/*
			 * FIXME FIXME FIXME
			 * Will someone please convince me that it is safe
			 * to use the raw pointers here?  -- MW.
			 */
			hash = hash ^ GPOINTER_TO_UINT (e->u.any_pointer);
			break;
		case MSTYLE_ELEMENT_MAX_BLANK: /* A dummy element */
			break;
		case MSTYLE_ANY_BOOLEAN:
			if (e->u.any_boolean)
				hash = hash ^ 0x1379;
			break;
		case MSTYLE_ANY_FLOAT:
			hash = hash ^ ((int)(e->u.any_float * 97));
			break;
		case MSTYLE_ANY_GUINT16:
			hash = hash ^ e->u.any_guint16;
			break;
		case MSTYLE_ANY_GUINT32:
			hash = hash ^ e->u.any_guint32;
			break;
		case MSTYLE_INDENT:
			hash = hash ^ e->u.indent;
			break;
		case MSTYLE_ORIENTATION:
			hash = hash ^ e->u.orientation;
			break;
		case MSTYLE_FONT_UNDERLINE:
			hash = hash ^ e->u.font.underline;
			break;
		default:
			g_warning ("Unimplemented hash item");
			break;
		}
	}

	return hash;
}

static char *
mstyle_element_dump (const MStyleElement *e)
{
	GString *ans = g_string_new ("");
	char    *txt_ans;

	/* This leaks ans from above.  Let's consider that a feature.  */
	g_return_val_if_fail (e != NULL, g_strdup ("Duff element"));

	switch (e->type) {
	case MSTYLE_ELEMENT_UNSET:
		g_string_sprintf (ans, "Unset");
		break;
	case MSTYLE_COLOR_FORE:
		g_string_sprintf (ans, "foregnd col");
		break;
	case MSTYLE_COLOR_BACK:
		g_string_sprintf (ans, "backgnd col");
		break;
	case MSTYLE_COLOR_PATTERN:
		g_string_sprintf (ans, "pattern col");
		break;
	case MSTYLE_FONT_NAME:
		g_string_sprintf (ans, "name '%s'", e->u.font.name->str);
		break;
	case MSTYLE_FONT_BOLD:
		if (e->u.font.bold)
			g_string_sprintf (ans, "bold");
		else
			g_string_sprintf (ans, "not bold");
		break;
	case MSTYLE_FONT_ITALIC:
		if (e->u.font.italic)
			g_string_sprintf (ans, "italic");
		else
			g_string_sprintf (ans, "not italic");
		break;
	case MSTYLE_FONT_UNDERLINE:
		switch (e->u.font.underline) {
		default :
		case UNDERLINE_NONE :
			g_string_sprintf (ans, "not underline");
		case UNDERLINE_SINGLE :
			g_string_sprintf (ans, "single underline");
		case UNDERLINE_DOUBLE :
			g_string_sprintf (ans, "double underline");
		};
		break;
	case MSTYLE_FONT_STRIKETHROUGH:
		if (e->u.font.strikethrough)
			g_string_sprintf (ans, "strikethrough");
		else
			g_string_sprintf (ans, "not strikethrough");
		break;
	case MSTYLE_FONT_SIZE:
		g_string_sprintf (ans, "size %f", e->u.font.size);
		break;
	case MSTYLE_BORDER_TOP:
	case MSTYLE_BORDER_BOTTOM:
	case MSTYLE_BORDER_LEFT:
	case MSTYLE_BORDER_RIGHT:
	case MSTYLE_BORDER_DIAGONAL:
	case MSTYLE_BORDER_REV_DIAGONAL:
		if (e->u.border.any)
			g_string_sprintf (ans, "%s %d", mstyle_names[e->type], e->u.border.any->line_type);
		else
			g_string_sprintf (ans, "%s blank", mstyle_names[e->type]);
		break;
	case MSTYLE_FORMAT:
	{
		char *fmt = style_format_as_XL (e->u.format, TRUE);
		g_string_sprintf (ans, "format '%s'", fmt);
		g_free (fmt);

		break;
	}
	case MSTYLE_PATTERN :
		g_string_sprintf (ans, "pattern %d", e->u.pattern);
		break;
		
	case MSTYLE_VALIDATION :
		g_string_sprintf (ans, "validation ref_count '%d'", e->u.validation->ref_count);
		break;

	default:
		g_string_sprintf (ans, "%s", mstyle_names[e->type]);
		break;
	}

	txt_ans = ans->str;
	g_string_free (ans, FALSE);

	return txt_ans;
}

static gboolean
mstyle_element_equal (MStyleElement const *a,
		      MStyleElement const *b)
{
	if ((a->type == MSTYLE_ELEMENT_UNSET ||
	     b->type == MSTYLE_ELEMENT_UNSET) && a->type != b->type)
		return FALSE;

	g_return_val_if_fail (a->type == b->type, FALSE);

	switch (a->type) {
	case MSTYLE_ANY_COLOR:
		if (a->u.color.fore == b->u.color.fore)
			return TRUE;
		break;
	case MSTYLE_ANY_BORDER:
		if (a->u.border.any == b->u.border.any)
			return TRUE;
		break;
	case MSTYLE_PATTERN:
		if (a->u.pattern == b->u.pattern)
			return TRUE;
		break;
	case MSTYLE_FONT_NAME:
		if (a->u.font.name == b->u.font.name)
			return TRUE;
		break;
	case MSTYLE_FONT_BOLD:
		if (a->u.font.bold == b->u.font.bold)
			return TRUE;
		break;
	case MSTYLE_FONT_ITALIC:
		if (a->u.font.italic == b->u.font.italic)
			return TRUE;
		break;
	case MSTYLE_FONT_UNDERLINE:
		if (a->u.font.underline == b->u.font.underline)
			return TRUE;
		break;
	case MSTYLE_FONT_STRIKETHROUGH:
		if (a->u.font.strikethrough == b->u.font.strikethrough)
			return TRUE;
	case MSTYLE_FONT_SIZE:
		if (a->u.font.size == b->u.font.size)
			return TRUE;
		break;
	case MSTYLE_FORMAT:
		if (a->u.format == b->u.format)
			return TRUE;
		break;
	case MSTYLE_ALIGN_V:
		if (a->u.align.v == b->u.align.v)
			return TRUE;
		break;
	case MSTYLE_ALIGN_H:
		if (a->u.align.h == b->u.align.h)
			return TRUE;
		break;
	case MSTYLE_INDENT:
		if (a->u.indent == b->u.indent)
			return TRUE;
		break;
	case MSTYLE_ORIENTATION:
		if (a->u.orientation == b->u.orientation)
			return TRUE;
		break;
	case MSTYLE_WRAP_TEXT:
		if (a->u.wrap_text == b->u.wrap_text)
			return TRUE;
		break;
	case MSTYLE_CONTENT_LOCKED:
		if (a->u.content_locked == b->u.content_locked)
			return TRUE;
		break;
	case MSTYLE_CONTENT_HIDDEN:
		if (a->u.content_hidden == b->u.content_hidden)
			return TRUE;
		break;
	case MSTYLE_VALIDATION:
		if (a->u.validation == b->u.validation)
			return TRUE;
		break;
	default:
		return TRUE;
	}

	return FALSE;
}

/**
 * mstyle_elements_equal:
 * @a: a style
 * @b: another style
 *
 * Compares for identical style element arrays,
 * fully commutative.
 *
 * Return value: TRUE if equal.
 **/
static gboolean
mstyle_elements_equal (const MStyleElement *a,
		       const MStyleElement *b)
{
	int i;

	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	for (i = 1; i < MSTYLE_ELEMENT_MAX; i++) {
		/* Elements in the same position should have the same types */
		if (a[i].type != b[i].type) {
			if (a[i].type != MSTYLE_ELEMENT_UNSET &&
			    b[i].type != MSTYLE_ELEMENT_UNSET)
				g_warning ("%s mismatched types\n", mstyle_names[i]);
			return FALSE;
		}

		if (!mstyle_element_equal (a+i, b+i))
			return FALSE;
	}

	return TRUE;
}

static inline MStyleElement
mstyle_element_ref (const MStyleElement *e)
{
	switch (e->type) {
	case MSTYLE_ANY_COLOR:
		style_color_ref (e->u.color.any);
		break;
	case MSTYLE_ANY_BORDER:
		style_border_ref (e->u.border.any);
		break;
	case MSTYLE_FONT_NAME:
		string_ref (e->u.font.name);
		break;
	case MSTYLE_FORMAT:
		style_format_ref (e->u.format);
		break;
	case MSTYLE_VALIDATION:
		if (e->u.validation)
			validation_ref (e->u.validation);
		break;
	default:
		break;
	}
	return *e;
}

static inline void
mstyle_element_unref (MStyleElement e)
{
	switch (e.type) {
	case MSTYLE_ANY_COLOR:
		style_color_unref (e.u.color.fore);
		break;
	case MSTYLE_ANY_BORDER:
		style_border_unref (e.u.border.any);
		break;
	case MSTYLE_FONT_NAME:
		string_unref (e.u.font.name);
		break;
	case MSTYLE_FORMAT:
		style_format_unref (e.u.format);
		break;
	case MSTYLE_VALIDATION:
		if (e.u.validation)
			validation_unref (e.u.validation);
		break;
	default:
		break;
	}
}

/**
 * mstyle_elements_compare:
 * @a: style to be tagged
 * @b: style to compare.
 *
 * Compares styles and tags conflicts into a.
 **/
static inline void
mstyle_elements_compare (MStyleElement *a,
			 const MStyleElement *b)
{
	int i;

	g_return_if_fail (a != NULL);
	g_return_if_fail (b != NULL);

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
		if (b[i].type == MSTYLE_ELEMENT_UNSET ||
		    b[i].type == MSTYLE_ELEMENT_CONFLICT ||
		    a[i].type == MSTYLE_ELEMENT_CONFLICT)
			continue;
		if (a[i].type == MSTYLE_ELEMENT_UNSET) {
			mstyle_element_ref (&b[i]);
			a[i] = b[i];
		} else if (!mstyle_element_equal (a+i, b+i)) {
			mstyle_element_unref (a[i]);
			a[i].type = MSTYLE_ELEMENT_CONFLICT;
		}
	}

}

void
mstyle_compare (MStyle *a, const MStyle *b)
{
	mstyle_elements_compare (a->elements,
				 b->elements);
}

static void
mstyle_elements_unref (MStyleElement *e)
{
	int i;

	if (e)
		for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
			mstyle_element_unref (e[i]);
			e[i].type = MSTYLE_ELEMENT_UNSET;
		}
}

static void
mstyle_elements_copy (MStyle *new_style, const MStyle *old_style)
{
	int                  i;
	MStyleElement       *ans;
	const MStyleElement *e;

	e   = old_style->elements;
	ans = new_style->elements;

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
		mstyle_element_ref (&e[i]);
		ans[i] = e[i];
	}
}

MStyle *
mstyle_new (void)
{
	MStyle *style = g_new0 (MStyle, 1);

	style->ref_count = 1;
	style->link_count = 0;
	style->linked_sheet = NULL;
	d(("new %p\n", style));

	return style;
}

MStyle *
mstyle_copy (const MStyle *style)
{
	MStyle *new_style = g_new (MStyle, 1);

	new_style->ref_count = 1;
	new_style->link_count = 0;
	new_style->linked_sheet = NULL;
	mstyle_elements_copy (new_style, style);

	d(("copy %p\n", new_style));
	return new_style;
}

MStyle *
mstyle_copy_merge (const MStyle *orig, const MStyle *overlay)
{
	int i;
	MStyle *res = g_new0 (MStyle, 1);

	MStyleElement       *res_e;
	const MStyleElement *orig_e;
	const MStyleElement *overlay_e;

	res->ref_count = 1;
	res->link_count = 0;
	res->linked_sheet = NULL;
	res_e = res->elements;
	orig_e = orig->elements;
	overlay_e = overlay->elements;

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++)
		res_e [i] = mstyle_element_ref (
			(overlay_e [i].type ? overlay_e : orig_e) + i); 

	d(("copy merge %p\n", res));
	return res;
}

/**
 * mstyle_new_default:
 *
 * Return the default style,
 * this should _never_ _ever_ have any of its elements
 * set.
 *
 * Return value: the default style.
 **/
MStyle *
mstyle_new_default (void)
{
	MStyle *mstyle = mstyle_new ();

	mstyle_set_format_text (mstyle, "General");
	mstyle_set_align_v     (mstyle, VALIGN_BOTTOM);
	mstyle_set_align_h     (mstyle, HALIGN_GENERAL);
	mstyle_set_indent      (mstyle, 0);
	mstyle_set_orientation (mstyle, ORIENT_HORIZ);
	mstyle_set_wrap_text   (mstyle, FALSE);
	mstyle_set_content_locked (mstyle, TRUE);
	mstyle_set_content_hidden (mstyle, FALSE);
	mstyle_set_font_name   (mstyle, DEFAULT_FONT);
	mstyle_set_font_bold   (mstyle, FALSE);
	mstyle_set_font_italic (mstyle, FALSE);
	mstyle_set_font_uline  (mstyle, UNDERLINE_NONE);
	mstyle_set_font_strike (mstyle, FALSE);
	mstyle_set_font_size   (mstyle, DEFAULT_SIZE);

	mstyle_set_color       (mstyle, MSTYLE_COLOR_FORE,
				style_color_black ());
	mstyle_set_color       (mstyle, MSTYLE_COLOR_BACK,
				style_color_white ());
	mstyle_set_color       (mstyle, MSTYLE_COLOR_PATTERN,
				style_color_black ());

	/* To negate borders */
	mstyle_set_border      (mstyle, MSTYLE_BORDER_TOP,
				style_border_ref (style_border_none ()));
	mstyle_set_border      (mstyle, MSTYLE_BORDER_LEFT,
				style_border_ref (style_border_none ()));
	mstyle_set_border      (mstyle, MSTYLE_BORDER_BOTTOM,
				style_border_ref (style_border_none ()));
	mstyle_set_border      (mstyle, MSTYLE_BORDER_RIGHT,
				style_border_ref (style_border_none ()));
	mstyle_set_border      (mstyle, MSTYLE_BORDER_DIAGONAL,
				style_border_ref (style_border_none ()));
	mstyle_set_border      (mstyle, MSTYLE_BORDER_REV_DIAGONAL,
				style_border_ref (style_border_none ()));

	/* This negates the back and pattern colors */
	mstyle_set_pattern     (mstyle, 0);

	return mstyle;
}

void
mstyle_ref (MStyle *style)
{
	g_return_if_fail (style->ref_count > 0);

	style->ref_count++;
	d(("ref %p = %d\n", style, style->ref_count));
}

void
mstyle_unref (MStyle *style)
{
	g_return_if_fail (style->ref_count > 0);

	d(("unref %p = %d\n", style, style->ref_count-1));
	if (style->ref_count-- <= 1) {
		g_return_if_fail (style->link_count == 0);
		g_return_if_fail (style->linked_sheet == NULL);

		if (style->elements)
			mstyle_elements_unref (style->elements);

		g_free (style);
	}
}

/**
 * mstyle_link_sheet :
 * @style :
 * @sheet :
 *
 * ABSORBS a reference to the style and sets the link count to 1.
 */
MStyle *
mstyle_link_sheet (MStyle *style, Sheet *sheet)
{
	if (style->linked_sheet != NULL) {
		MStyle *orig = style;
		style = mstyle_copy (style);
		mstyle_unref (orig);

		/* safety test */
		g_return_val_if_fail (style->linked_sheet != sheet, style);
	}

	g_return_val_if_fail (style->link_count == 0, style);
	g_return_val_if_fail (style->linked_sheet == NULL, style);

	style->linked_sheet = sheet;
	style->link_count = 1;

#if 0
	/* Not needed for validation anymore, leave it as template for conditionals */
	if (mstyle_is_element_set (style, MSTYLE_VALIDATION))
		validation_link (style->elements[MSTYLE_VALIDATION].u.validation, sheet);
#endif

	d(("link sheet %p = 1\n", style));
	return style;
}

void
mstyle_link (MStyle *style)
{
	g_return_if_fail (style->link_count > 0);

	style->link_count++;
	d(("link %p = %d\n", style, style->link_count));
}

void
mstyle_link_multiple (MStyle *style, int count)
{
	g_return_if_fail (style->link_count > 0);

	style->link_count += count;
	d(("multiple link %p + %d = %d\n", style, count, style->link_count));
}

void
mstyle_unlink (MStyle *style)
{
	g_return_if_fail (style->link_count > 0);

	d(("unlink %p = %d\n", style, style->link_count-1));
	if (style->link_count-- == 1) {
#if 0
		/* Not needed for validation anymore, leave it as template for conditionals */
		if (mstyle_is_element_set (style, MSTYLE_VALIDATION))
			validation_unlink (style->elements[MSTYLE_VALIDATION].u.validation);
#endif
		sheet_style_unlink (style->linked_sheet, style);
		style->linked_sheet = NULL;
		mstyle_unref (style);
	}
}

char *
mstyle_to_string (const MStyle *style)
{
	guint i;
	GString *ans;
	char *txt_ans;

	g_return_val_if_fail (style != NULL, g_strdup ("(null)"));

	ans = g_string_new ("Elements : ");
	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
		char *txt;

		if (style->elements[i].type) {
			txt = mstyle_element_dump (&style->elements[i]);
			g_string_sprintfa (ans, "%s ", txt);
			g_free (txt);
		} else
			g_string_sprintfa (ans, ".");
	}
	txt_ans = ans->str;
	g_string_free (ans, FALSE);

	return txt_ans;
}

void
mstyle_dump (const MStyle *style)
{
	char *txt;

	fprintf (stderr, "Style Refs %d\n",
		 style->ref_count);
	txt = mstyle_to_string (style);
	fprintf (stderr, "%s\n", txt);
	g_free (txt);
}

gboolean
mstyle_equal (const MStyle *a, const MStyle *b)
{
	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	if (a == b)
		return TRUE;

	return mstyle_elements_equal (a->elements, b->elements);
}

gboolean
mstyle_empty (const MStyle *style)
{
	int i;

	g_return_val_if_fail (style != NULL, FALSE);

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++)
		if (style->elements[i].type)
			return FALSE;
	return TRUE;
}

gboolean
mstyle_verify (const MStyle *style)
{
	int j;

	for (j = 0; j < MSTYLE_ELEMENT_MAX; j++) {
		MStyleElement e = style->elements[j];

		g_return_val_if_fail (e.type <  MSTYLE_ELEMENT_MAX, FALSE);
		g_return_val_if_fail (e.type != MSTYLE_ELEMENT_CONFLICT, FALSE);
	}
	return TRUE;
}

gboolean
mstyle_is_element_set (const MStyle *st, MStyleElementType t)
{
	g_return_val_if_fail (st != NULL, FALSE);
	g_return_val_if_fail (t > 0 && t < MSTYLE_ELEMENT_MAX, FALSE);

	return  st->elements[t].type != MSTYLE_ELEMENT_UNSET &&
		st->elements[t].type != MSTYLE_ELEMENT_CONFLICT;
}

gboolean
mstyle_is_element_conflict (const MStyle *st, MStyleElementType t)
{
	g_return_val_if_fail (st != NULL, FALSE);
	g_return_val_if_fail (t > 0 && t < MSTYLE_ELEMENT_MAX, FALSE);

	return st->elements[t].type == MSTYLE_ELEMENT_CONFLICT;
}

void
mstyle_unset_element (MStyle *st, MStyleElementType t)
{
	g_return_if_fail (st != NULL);
	g_return_if_fail (t > 0 && t < MSTYLE_ELEMENT_MAX);

	mstyle_element_unref (st->elements[t]);
	st->elements[t].type = MSTYLE_ELEMENT_UNSET;
}

/**
 * mstyle_replace_element:
 * @src: Source mstyle
 * @dst: Destination mstyle
 * @t: Element to replace
 *
 * This function replaces element 't' in mstyle 'dst' with element 't'
 * in mstyle 'src'. (If element 't' was already set in mstyle 'dst' then
 * the element will first be unset)
 **/
void
mstyle_replace_element (MStyle *src, MStyle *dst, MStyleElementType t)
{
	g_return_if_fail (src != NULL);
	g_return_if_fail (dst != NULL);

	mstyle_element_ref (&src->elements[t]);

	if (mstyle_is_element_set (dst, t))
		mstyle_unset_element (dst, t);

	dst->elements[t] = src->elements[t];
}

void
mstyle_set_color (MStyle *st, MStyleElementType t,
		  StyleColor *col)
{
	g_return_if_fail (st != NULL);
	g_return_if_fail (col != NULL);

	switch (t) {
	case MSTYLE_ANY_COLOR:
		mstyle_element_unref (st->elements[t]);
		st->elements[t].type = t;
		st->elements[t].u.color.any = col;
		break;
	default:
		g_warning ("Not a color element");
		break;
	}
}

StyleColor *
mstyle_get_color (MStyle const *st, MStyleElementType t)
{
	g_return_val_if_fail (mstyle_is_element_set (st, t), NULL);

	switch (t) {
	case MSTYLE_ANY_COLOR:
		return st->elements[t].u.color.any;

	default:
		g_warning ("Not a color element");
		return NULL;
	}
}

void
mstyle_set_border (MStyle *st, MStyleElementType t,
		   StyleBorder *border)
{
	g_return_if_fail (st != NULL);

	/* NOTE : It is legal for border to be NULL */
	switch (t) {
	case MSTYLE_ANY_BORDER:
		mstyle_element_unref (st->elements[t]);
		st->elements[t].type = t;
		st->elements[t].u.border.any = border;
		break;
	default:
		g_warning ("Not a color element");
		break;
	}

}

StyleBorder *
mstyle_get_border (const MStyle *st, MStyleElementType t)
{
	switch (t) {
	case MSTYLE_ANY_BORDER:
		return st->elements[t].u.border.any;

	default:
		g_warning ("Not a border element");
		return NULL;
	}
}

void
mstyle_set_pattern (MStyle *st, int pattern)
{
	g_return_if_fail (st != NULL);
	g_return_if_fail (pattern >= 0);
	g_return_if_fail (pattern <= GNUMERIC_SHEET_PATTERNS);

	st->elements[MSTYLE_PATTERN].type = MSTYLE_PATTERN;
	st->elements[MSTYLE_PATTERN].u.pattern = pattern;
}

int
mstyle_get_pattern (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_PATTERN), 0);

	return style->elements[MSTYLE_PATTERN].u.pattern;
}

StyleFont *
mstyle_get_font (const MStyle *style, double zoom)
{
	StyleFont *font;
	const gchar *name;
	gboolean bold, italic;
	double size;

	g_return_val_if_fail (style != NULL, NULL);

	if (mstyle_is_element_set (style, MSTYLE_FONT_NAME))
		name = mstyle_get_font_name (style);
	else
		name = DEFAULT_FONT;

	if (mstyle_is_element_set (style, MSTYLE_FONT_BOLD))
		bold = mstyle_get_font_bold (style);
	else
		bold = FALSE;

	if (mstyle_is_element_set (style, MSTYLE_FONT_ITALIC))
		italic = mstyle_get_font_italic (style);
	else
		italic = FALSE;

	if (mstyle_is_element_set (style, MSTYLE_FONT_SIZE))
		size = mstyle_get_font_size (style);
	else
		size = DEFAULT_SIZE;

	font = style_font_new (
		name, size, zoom, bold, italic);

	return font;
}

void
mstyle_set_font_name (MStyle *style, const char *name)
{
	g_return_if_fail (name != NULL);
	g_return_if_fail (style != NULL);

	mstyle_element_unref (style->elements[MSTYLE_FONT_NAME]);
	style->elements[MSTYLE_FONT_NAME].type = MSTYLE_FONT_NAME;
	style->elements[MSTYLE_FONT_NAME].u.font.name = string_get (name);
}

const char *
mstyle_get_font_name (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_NAME), NULL);

	return style->elements[MSTYLE_FONT_NAME].u.font.name->str;
}

void
mstyle_set_font_bold (MStyle *style, gboolean bold)
{
	g_return_if_fail (style != NULL);

	style->elements[MSTYLE_FONT_BOLD].type = MSTYLE_FONT_BOLD;
	style->elements[MSTYLE_FONT_BOLD].u.font.bold = bold;
}

gboolean
mstyle_get_font_bold (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_BOLD), FALSE);

	return style->elements[MSTYLE_FONT_BOLD].u.font.bold;
}

void
mstyle_set_font_italic (MStyle *style, gboolean italic)
{
	g_return_if_fail (style != NULL);

	style->elements[MSTYLE_FONT_ITALIC].type = MSTYLE_FONT_ITALIC;
	style->elements[MSTYLE_FONT_ITALIC].u.font.italic = italic;
}

gboolean
mstyle_get_font_italic (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_ITALIC), FALSE);

	return style->elements[MSTYLE_FONT_ITALIC].u.font.italic;
}

void
mstyle_set_font_uline (MStyle *style, StyleUnderlineType const underline)
{
	g_return_if_fail (style != NULL);

	style->elements[MSTYLE_FONT_UNDERLINE].type = MSTYLE_FONT_UNDERLINE;
	style->elements[MSTYLE_FONT_UNDERLINE].u.font.underline = underline;
}

StyleUnderlineType
mstyle_get_font_uline (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_UNDERLINE), FALSE);

	return style->elements[MSTYLE_FONT_UNDERLINE].u.font.underline;
}

void
mstyle_set_font_strike (MStyle *style, gboolean const strikethrough)
{
	g_return_if_fail (style != NULL);

	style->elements[MSTYLE_FONT_STRIKETHROUGH].type = MSTYLE_FONT_STRIKETHROUGH;
	style->elements[MSTYLE_FONT_STRIKETHROUGH].u.font.strikethrough = strikethrough;
}

gboolean
mstyle_get_font_strike (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_STRIKETHROUGH), FALSE);

	return style->elements[MSTYLE_FONT_STRIKETHROUGH].u.font.strikethrough;
}
void
mstyle_set_font_size (MStyle *style, double size)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (size >= 1.);

	style->elements[MSTYLE_FONT_SIZE].type = MSTYLE_FONT_SIZE;
	style->elements[MSTYLE_FONT_SIZE].u.font.size = size;
}

double
mstyle_get_font_size (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_SIZE), 12.0);

	return style->elements[MSTYLE_FONT_SIZE].u.font.size;
}

void
mstyle_set_format (MStyle *style, StyleFormat *format)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (format != NULL);

	style_format_ref (format);
	mstyle_element_unref (style->elements[MSTYLE_FORMAT]);
	style->elements[MSTYLE_FORMAT].type = MSTYLE_FORMAT;
	style->elements[MSTYLE_FORMAT].u.format = format;
}

void
mstyle_set_format_text (MStyle *style, const char *format)
{
	StyleFormat *sf;

	g_return_if_fail (style != NULL);
	g_return_if_fail (format != NULL);

	/* FIXME FIXME FIXME : This is a potential problem
	 * I am not sure people are feeding us only translated formats.
	 * This entire function should be deleted.
	 */
	sf = style_format_new_XL (format, FALSE);
	mstyle_set_format (style, sf);
	style_format_unref (sf);
}

StyleFormat *
mstyle_get_format (MStyle const *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_FORMAT), NULL);

	return style->elements[MSTYLE_FORMAT].u.format;
}

void
mstyle_set_align_h (MStyle *style, StyleHAlignFlags a)
{
	g_return_if_fail (style != NULL);

	style->elements[MSTYLE_ALIGN_H].type = MSTYLE_ALIGN_H;
	style->elements[MSTYLE_ALIGN_H].u.align.h = a;
}

StyleHAlignFlags
mstyle_get_align_h (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_ALIGN_H), 0);

	return style->elements[MSTYLE_ALIGN_H].u.align.h;
}

void
mstyle_set_align_v (MStyle *style, StyleVAlignFlags a)
{
	g_return_if_fail (style != NULL);

	style->elements[MSTYLE_ALIGN_V].type = MSTYLE_ALIGN_V;
	style->elements[MSTYLE_ALIGN_V].u.align.v = a;
}

StyleVAlignFlags
mstyle_get_align_v (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_ALIGN_V), 0);

	return style->elements[MSTYLE_ALIGN_V].u.align.v;
}

void
mstyle_set_indent (MStyle *style, int i)
{
	g_return_if_fail (style != NULL);

	style->elements[MSTYLE_INDENT].type = MSTYLE_INDENT;
	style->elements[MSTYLE_INDENT].u.indent = i;
}

int
mstyle_get_indent (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_INDENT), 0);

	return style->elements[MSTYLE_INDENT].u.indent;
}

void
mstyle_set_orientation (MStyle *style, StyleOrientation o)
{
	g_return_if_fail (style != NULL);

	style->elements[MSTYLE_ORIENTATION].type = MSTYLE_ORIENTATION;
	style->elements[MSTYLE_ORIENTATION].u.orientation = o;
}

StyleOrientation
mstyle_get_orientation (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_ORIENTATION), 0);

	return style->elements[MSTYLE_ORIENTATION].u.orientation;
}

void
mstyle_set_wrap_text (MStyle *style, gboolean f)
{
	g_return_if_fail (style != NULL);

	style->elements[MSTYLE_WRAP_TEXT].type = MSTYLE_WRAP_TEXT;
	style->elements[MSTYLE_WRAP_TEXT].u.wrap_text = f;
}

gboolean
mstyle_get_wrap_text (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_WRAP_TEXT), FALSE);

	return style->elements [MSTYLE_WRAP_TEXT].u.wrap_text;
}

void
mstyle_set_content_locked (MStyle *style, gboolean f)
{
	g_return_if_fail (style != NULL);

	style->elements[MSTYLE_CONTENT_LOCKED].type = MSTYLE_CONTENT_LOCKED;
	style->elements[MSTYLE_CONTENT_LOCKED].u.wrap_text = f;
}

gboolean
mstyle_get_content_locked (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_CONTENT_LOCKED), FALSE);

	return style->elements [MSTYLE_CONTENT_LOCKED].u.wrap_text;
}
void
mstyle_set_content_hidden (MStyle *style, gboolean f)
{
	g_return_if_fail (style != NULL);

	style->elements[MSTYLE_CONTENT_HIDDEN].type = MSTYLE_CONTENT_HIDDEN;
	style->elements[MSTYLE_CONTENT_HIDDEN].u.wrap_text = f;
}

gboolean
mstyle_get_content_hidden (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_CONTENT_HIDDEN), FALSE);

	return style->elements [MSTYLE_CONTENT_HIDDEN].u.wrap_text;
}

void
mstyle_set_validation (MStyle *style, Validation *v)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (v != NULL);
	
	mstyle_element_unref (style->elements[MSTYLE_VALIDATION]);
	style->elements[MSTYLE_VALIDATION].type = MSTYLE_VALIDATION;
	style->elements[MSTYLE_VALIDATION].u.validation = v;
	
}

Validation *
mstyle_get_validation (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_VALIDATION), NULL);

	return style->elements[MSTYLE_VALIDATION].u.validation;
}

gboolean
mstyle_visible_in_blank (const MStyle *st)
{
	MStyleElementType i;

	if (mstyle_is_element_set (st, MSTYLE_PATTERN) &&
	    mstyle_get_pattern (st) > 0)
		return TRUE;

	for (i = MSTYLE_BORDER_TOP ; i <= MSTYLE_BORDER_DIAGONAL ; ++i)
		if (mstyle_is_element_set (st, i) &&
		    style_border_visible_in_blank (mstyle_get_border (st, i)))
			return TRUE;

	return FALSE;
}
