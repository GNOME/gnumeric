/*
 * MStyle.c: The guts of the style engine.
 *
 * Author:
 *   Michael Meeks <mmeeks@gnu.org>
 */
#include <config.h>
#include "mstyle.h"
#include "str.h"
#include "border.h"
#include "pattern.h"
#include "main.h"

#define STYLE_DEBUG (style_debugging > 2)

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
			MStyleBorder *top;
			MStyleBorder *bottom;
			MStyleBorder *left;
			MStyleBorder *right;
			MStyleBorder *diagonal;
			MStyleBorder *rev_diagonal;

			/* Used for loading */
			MStyleBorder *any;
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
		StyleOrientation orientation;
		gboolean         fit_in_cell;

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
	gchar         *name;
	MStyleElement  elements [MSTYLE_ELEMENT_MAX];
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
				case MSTYLE_FORMAT

#define MSTYLE_ANY_BOOLEAN           MSTYLE_FONT_BOLD: \
				case MSTYLE_FONT_ITALIC: \
				case MSTYLE_FONT_STRIKETHROUGH: \
				case MSTYLE_FIT_IN_CELL

#define MSTYLE_ANY_GUINT16           MSTYLE_ALIGN_V: \
                                case MSTYLE_ALIGN_H

#define MSTYLE_ANY_GUINT32           MSTYLE_PATTERN

#define MSTYLE_ANY_FLOAT             MSTYLE_FONT_SIZE


const char *mstyle_names [MSTYLE_ELEMENT_MAX] = {
	"--UnSet--",
	"--Conflict--",
	"Color.Back",
	"Color.Pattern",
	"Border.Top",
	"Border.Bottom",
	"Border.Left",
	"Border.Right",
	"Border.Diagonal",
	"Border.RevDiagonal",
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
	"Orientation",
	"FitInCell"
};

guint
mstyle_hash (gconstpointer st)
{
	const MStyle *mstyle = (const MStyle *)st;
	int     i;
	guint32 hash = 0;

	for (i = MSTYLE_ELEMENT_CONFLICT + 1; i < MSTYLE_ELEMENT_MAX; i++) {
		const MStyleElement *e = &mstyle->elements [i];
		hash = hash << 7;
		switch (i) {
		case MSTYLE_ANY_COLOR:
			hash = hash ^ GPOINTER_TO_UINT (e->u.color.any);
			break;
		case MSTYLE_ANY_BORDER:
			hash = hash ^ GPOINTER_TO_UINT (e->u.border.any);
			break;
		case MSTYLE_ANY_POINTER:
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
			g_string_sprintf (ans, "%s %d", mstyle_names [e->type], e->u.border.any->line_type);
		else
			g_string_sprintf (ans, "%s blank", mstyle_names [e->type]);
		break;
	case MSTYLE_FORMAT:
		g_string_sprintf (ans, "format '%s'", e->u.format->format);
		break;
	default:
		g_string_sprintf (ans, "%s", mstyle_names [e->type]);
		break;
	}

	txt_ans = ans->str;
	g_string_free (ans, FALSE);

	return txt_ans;
}

/*
 * This should probably be unrolled into mstyle_elements_equal.
 */
static gboolean
mstyle_element_equal (const MStyleElement a,
		      const MStyleElement b)
{
	if ((a.type == MSTYLE_ELEMENT_UNSET ||
	     b.type == MSTYLE_ELEMENT_UNSET) && a.type != b.type)
		return FALSE;

	g_return_val_if_fail (a.type == b.type, FALSE);

	switch (a.type) {
	case MSTYLE_ANY_COLOR:
		if (a.u.color.fore == b.u.color.fore)
			return TRUE;
		break;
	case MSTYLE_ANY_BORDER:
		if (a.u.border.any == b.u.border.any)
			return TRUE;
		break;
	case MSTYLE_PATTERN:
		if (a.u.pattern == b.u.pattern)
			return TRUE;
		break;
	case MSTYLE_FONT_NAME:
		if (a.u.font.name == b.u.font.name)
			return TRUE;
		break;
	case MSTYLE_FONT_BOLD:
		if (a.u.font.bold == b.u.font.bold)
			return TRUE;
		break;
	case MSTYLE_FONT_ITALIC:
		if (a.u.font.italic == b.u.font.italic)
			return TRUE;
		break;
	case MSTYLE_FONT_UNDERLINE:
		if (a.u.font.underline == b.u.font.underline)
			return TRUE;
		break;
	case MSTYLE_FONT_STRIKETHROUGH:
		if (a.u.font.strikethrough == b.u.font.strikethrough)
			return TRUE;
	case MSTYLE_FONT_SIZE:
		if (a.u.font.size == b.u.font.size)
			return TRUE;
		break;
	case MSTYLE_FORMAT:
		if (a.u.format == b.u.format)
			return TRUE;
		break;
	case MSTYLE_ALIGN_V:
		if (a.u.align.v == b.u.align.v)
			return TRUE;
		break;
	case MSTYLE_ALIGN_H:
		if (a.u.align.h == b.u.align.h)
			return TRUE;
		break;
	case MSTYLE_ORIENTATION:
		if (a.u.orientation == b.u.orientation)
			return TRUE;
		break;
	case MSTYLE_FIT_IN_CELL:
		if (a.u.fit_in_cell == b.u.fit_in_cell)
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

		g_assert (i < MSTYLE_ELEMENT_MAX);

		if (a [i].type != b [i].type)
			return FALSE;

		if (!mstyle_element_equal (a [i], b [i])) {
			g_assert (i < MSTYLE_ELEMENT_MAX);
			if (STYLE_DEBUG)
				printf ("%s mismatch\n", mstyle_names [i]);
			return FALSE;
		}
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
		if (b [i].type == MSTYLE_ELEMENT_UNSET ||
		    b [i].type == MSTYLE_ELEMENT_CONFLICT ||
		    a [i].type == MSTYLE_ELEMENT_CONFLICT)
			continue;
		if (a [i].type == MSTYLE_ELEMENT_UNSET) {
			mstyle_element_ref (&b [i]);
			a [i] = b [i];
		} else if (!mstyle_element_equal (a [i], b [i])) {
			mstyle_element_unref (a [i]);
			a [i].type = MSTYLE_ELEMENT_CONFLICT;
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
			mstyle_element_unref (e [i]);
			e [i].type = MSTYLE_ELEMENT_UNSET;
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
		mstyle_element_ref (&e [i]);
		ans [i] = e [i];
	}
}

MStyle *
mstyle_new (void)
{
	MStyle *style = g_new0 (MStyle, 1);

	style->ref_count = 1;
	style->name = NULL;

	return style;
}

MStyle *
mstyle_copy (const MStyle *style)
{
	MStyle *new_style = g_new (MStyle, 1);

	new_style->ref_count = 1;
	if (style->name)
		new_style->name = g_strdup (style->name);
	else
		new_style->name = NULL;
	mstyle_elements_copy (new_style, style);

	return new_style;
}

MStyle *
mstyle_new_name (const gchar *name)
{
	MStyle *style = mstyle_new ();

	if (name) {
		g_warning ("names not yet supported");
		style->name = g_strdup (name);
	}

	return style;
}

static MStyle *default_mstyle = NULL;

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
	MStyle *mstyle;

	if (default_mstyle) {
		mstyle_ref (default_mstyle);
		return default_mstyle;
	}
	
	mstyle = mstyle_new ();

	mstyle_set_format      (mstyle, "General");
	mstyle_set_align_v     (mstyle, VALIGN_BOTTOM);
	mstyle_set_align_h     (mstyle, HALIGN_GENERAL);
	mstyle_set_orientation (mstyle, ORIENT_HORIZ);
	mstyle_set_fit_in_cell (mstyle, FALSE);
	mstyle_set_font_name   (mstyle, DEFAULT_FONT);
	mstyle_set_font_bold   (mstyle, FALSE);
	mstyle_set_font_italic (mstyle, FALSE);
	mstyle_set_font_uline  (mstyle, UNDERLINE_NONE);
	mstyle_set_font_strike (mstyle, FALSE);
	mstyle_set_font_size   (mstyle, DEFAULT_SIZE);

	mstyle_set_color       (mstyle, MSTYLE_COLOR_FORE,
				style_color_new (0, 0, 0));
	mstyle_set_color       (mstyle, MSTYLE_COLOR_BACK,
				style_color_new (0xffff, 0xffff, 0xffff));
	mstyle_set_color       (mstyle, MSTYLE_COLOR_PATTERN,
				style_color_new (0, 0, 0));

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

	default_mstyle = mstyle;

	return mstyle;
}

void
mstyle_ref (MStyle *style)
{
	g_return_if_fail (style->ref_count > 0);

	style->ref_count++;
}

void
mstyle_unref (MStyle *style)
{
	g_return_if_fail (style->ref_count > 0);

	style->ref_count--;

	if (style->ref_count == 0)
		mstyle_destroy (style);
}

MStyle *
mstyle_do_merge (const GList *list, MStyleElementType max)
{
	const GList *l = list;

	/* Find the intersection */
	MStyle         *ans;
	MStyleElement *mash;

	g_return_val_if_fail (list != NULL, 
			      mstyle_new_default ());

	g_return_val_if_fail (max <= MSTYLE_ELEMENT_MAX,
			      mstyle_new_default ());
	g_return_val_if_fail (max > 0,
			      mstyle_new_default ());

	/* Short circuit the common default case */ 
	if (!list->next) {
		mstyle_ref (list->data);
		return list->data;
	}

	ans  = mstyle_new ();
	mash = ans->elements;
 
	while (l) {
		guint j;
		MStyle *style = l->data;
		MStyleElement *e = style->elements;
		MStyleElement *m = mash;
		
		for (j = 0; j < MSTYLE_ELEMENT_MAX; j++) {

			if (m->type == MSTYLE_ELEMENT_UNSET &&
			    e->type != MSTYLE_ELEMENT_UNSET)

				*m = mstyle_element_ref (e);
			e++;
			m++;
		}
		l = g_list_next (l);
	}
	
	return ans;
}

/**
 * mstyle_merge:
 * @master: the master style
 * @slave:  the slave style
 * 
 *   This function removes any style elements from the slave
 * that are masked by the master style. Thus eventualy the
 * slave style becomes redundant and can be removed.
 * NB. if slave->ref_count == 1 we operate on it directly
 * otherwise we must copy.
 * 
 * Returns: the masked style.
 **/
MStyle *
mstyle_merge (MStyle *master, MStyle *slave)
{
	MStyle *psts;
	int i;

	g_return_val_if_fail (slave != NULL, NULL);
	g_return_val_if_fail (master != NULL, NULL);

	psts = slave;

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
		if (master->elements [i].type && psts->elements [i].type) {
			if (psts->ref_count > 1) {
				psts = mstyle_copy (slave);
				mstyle_unref (slave);
			}
			mstyle_element_unref (psts->elements [i]);
			psts->elements [i].type = MSTYLE_ELEMENT_UNSET;
		}
	}

	return psts;
}

char *
mstyle_to_string (const MStyle *style)
{
	guint i;
	GString *ans;
	char *txt_ans;

	g_return_val_if_fail (style != NULL, "(null)");
	
	ans = g_string_new ("Elements : ");
	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
		char *txt;

		if (style->elements [i].type) {
			txt = mstyle_element_dump (&style->elements [i]);
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

	printf ("Style '%s' Ref s%d\n",
		style->name ? style->name : "unnamed",
		style->ref_count);
	txt = mstyle_to_string (style);
	printf ("%s\n", txt);
	g_free (txt);
}

void
mstyle_destroy (MStyle *style)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (style->ref_count == 0);
	
	if (style->name)
		g_free (style->name);
	style->name = NULL;

	if (style->elements)
		mstyle_elements_unref (style->elements);
		
	g_free (style);
}

gboolean
mstyle_equal (const MStyle *a, const MStyle *b)
{
	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	if (a == b)
		return TRUE;

	if (a->name || b->name)
		g_warning ("Named style equal unimplemented");

	return mstyle_elements_equal (a->elements, b->elements);
}

gboolean
mstyle_empty (const MStyle *style)
{
	int i;

	g_return_val_if_fail (style != NULL, FALSE);

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++)
		if (style->elements [i].type)
			return FALSE;
	return TRUE;
}

gboolean
mstyle_verify (const MStyle *style)
{
	int j;

	for (j = 0; j < MSTYLE_ELEMENT_MAX; j++) {
		MStyleElement e = style->elements [j];

		g_return_val_if_fail (e.type <  MSTYLE_ELEMENT_MAX, FALSE);
		g_return_val_if_fail (e.type != MSTYLE_ELEMENT_CONFLICT, FALSE);
	}
	return TRUE;
}

gboolean inline
mstyle_is_element_set (const MStyle *st, MStyleElementType t)
{
	g_return_val_if_fail (st != NULL, FALSE);
	g_return_val_if_fail (t > 0 && t < MSTYLE_ELEMENT_MAX, FALSE);

	return  st->elements [t].type != MSTYLE_ELEMENT_UNSET &&
		st->elements [t].type != MSTYLE_ELEMENT_CONFLICT;
}

gboolean
mstyle_is_element_conflict (const MStyle *st, MStyleElementType t)
{
	g_return_val_if_fail (st != NULL, FALSE);
	g_return_val_if_fail (t > 0 && t < MSTYLE_ELEMENT_MAX, FALSE);

	return st->elements [t].type == MSTYLE_ELEMENT_CONFLICT;
}

void
mstyle_unset_element (MStyle *st, MStyleElementType t)
{
	g_return_if_fail (st != NULL);
	g_return_if_fail (t > 0 && t < MSTYLE_ELEMENT_MAX);

	mstyle_element_unref (st->elements [t]);
	st->elements [t].type = MSTYLE_ELEMENT_UNSET;
}

void
mstyle_set_color (MStyle *st, MStyleElementType t,
		  StyleColor *col)
{
	g_return_if_fail (st != NULL);
	g_return_if_fail (col != NULL);

	switch (t) {
	case MSTYLE_ANY_COLOR:
		mstyle_element_unref (st->elements [t]);
		st->elements [t].type = t;
		st->elements [t].u.color.any = col;
		break;
	default:
		g_warning ("Not a color element");
		break;
	}
}

StyleColor *
mstyle_get_color (MStyle *st, MStyleElementType t)
{
	g_return_val_if_fail (mstyle_is_element_set (st, t), NULL);

	switch (t) {
	case MSTYLE_ANY_COLOR:
		return st->elements [t].u.color.any;

	default:
		g_warning ("Not a color element");
		return NULL;
	}
}

void
mstyle_set_border (MStyle *st, MStyleElementType t,
		   MStyleBorder *border)
{
	g_return_if_fail (st != NULL);

	/* NOTE : It is legal for border to be NULL */
	switch (t) {
	case MSTYLE_ANY_BORDER:
		mstyle_element_unref (st->elements [t]);
		st->elements [t].type = t;
		st->elements [t].u.border.any = border;
		break;
	default:
		g_warning ("Not a color element");
		break;
	}

}

const MStyleBorder *
mstyle_get_border (const MStyle *st, MStyleElementType t)
{
	g_return_val_if_fail (mstyle_is_element_set (st, t), NULL);

	switch (t) {
	case MSTYLE_ANY_BORDER:
		return st->elements [t].u.border.any;

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

	st->elements [MSTYLE_PATTERN].type = MSTYLE_PATTERN;
	st->elements [MSTYLE_PATTERN].u.pattern = pattern;
}

int
mstyle_get_pattern (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_PATTERN), 0);

	return style->elements [MSTYLE_PATTERN].u.pattern;
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
	
	mstyle_element_unref (style->elements [MSTYLE_FONT_NAME]);
	style->elements [MSTYLE_FONT_NAME].type = MSTYLE_FONT_NAME;
	style->elements [MSTYLE_FONT_NAME].u.font.name = string_get (name);
}

const char *
mstyle_get_font_name (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_NAME), NULL);

	return style->elements [MSTYLE_FONT_NAME].u.font.name->str;
}

void
mstyle_set_font_bold (MStyle *style, gboolean bold)
{
	g_return_if_fail (style != NULL);

	style->elements [MSTYLE_FONT_BOLD].type = MSTYLE_FONT_BOLD;
	style->elements [MSTYLE_FONT_BOLD].u.font.bold = bold;
}

gboolean
mstyle_get_font_bold (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_BOLD), FALSE);

	return style->elements [MSTYLE_FONT_BOLD].u.font.bold;
}

void
mstyle_set_font_italic (MStyle *style, gboolean italic)
{
	g_return_if_fail (style != NULL);

	style->elements [MSTYLE_FONT_ITALIC].type = MSTYLE_FONT_ITALIC;
	style->elements [MSTYLE_FONT_ITALIC].u.font.italic = italic;
}

gboolean
mstyle_get_font_italic (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_ITALIC), FALSE);

	return style->elements [MSTYLE_FONT_ITALIC].u.font.italic;
}

void
mstyle_set_font_uline (MStyle *style, StyleUnderlineType const underline)
{
	g_return_if_fail (style != NULL);

	style->elements [MSTYLE_FONT_UNDERLINE].type = MSTYLE_FONT_UNDERLINE;
	style->elements [MSTYLE_FONT_UNDERLINE].u.font.underline = underline;
}

StyleUnderlineType
mstyle_get_font_uline (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_UNDERLINE), FALSE);

	return style->elements [MSTYLE_FONT_UNDERLINE].u.font.underline;
}

void
mstyle_set_font_strike (MStyle *style, gboolean const strikethrough)
{
	g_return_if_fail (style != NULL);

	style->elements [MSTYLE_FONT_STRIKETHROUGH].type = MSTYLE_FONT_STRIKETHROUGH;
	style->elements [MSTYLE_FONT_STRIKETHROUGH].u.font.strikethrough = strikethrough;
}

gboolean
mstyle_get_font_strike (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_STRIKETHROUGH), FALSE);

	return style->elements [MSTYLE_FONT_STRIKETHROUGH].u.font.strikethrough;
}
void
mstyle_set_font_size (MStyle *style, double size)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (size >= 1.);

	style->elements [MSTYLE_FONT_SIZE].type = MSTYLE_FONT_SIZE;
	style->elements [MSTYLE_FONT_SIZE].u.font.size = size;
}

double
mstyle_get_font_size (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_SIZE), 12.0);

	return style->elements [MSTYLE_FONT_SIZE].u.font.size;
}

void
mstyle_set_format (MStyle *style, const char *format)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (format != NULL);

	mstyle_element_unref (style->elements [MSTYLE_FORMAT]);
	style->elements [MSTYLE_FORMAT].type = MSTYLE_FORMAT;
	style->elements [MSTYLE_FORMAT].u.format = style_format_new (format);
}

StyleFormat *
mstyle_get_format (MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_FORMAT), NULL);

	return style->elements [MSTYLE_FORMAT].u.format;
}

void
mstyle_set_align_h (MStyle *style, StyleHAlignFlags a)
{
	g_return_if_fail (style != NULL);

	style->elements [MSTYLE_ALIGN_H].type = MSTYLE_ALIGN_H;
	style->elements [MSTYLE_ALIGN_H].u.align.h = a;
}

StyleHAlignFlags
mstyle_get_align_h (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_ALIGN_H), 0);

	return style->elements [MSTYLE_ALIGN_H].u.align.h;
}

void
mstyle_set_align_v (MStyle *style, StyleVAlignFlags a)
{
	g_return_if_fail (style != NULL);

	style->elements [MSTYLE_ALIGN_V].type = MSTYLE_ALIGN_V;
	style->elements [MSTYLE_ALIGN_V].u.align.v = a;
}

StyleVAlignFlags
mstyle_get_align_v (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_ALIGN_V), 0);

	return style->elements [MSTYLE_ALIGN_V].u.align.v;
}

void
mstyle_set_orientation (MStyle *style, StyleOrientation o)
{
	g_return_if_fail (style != NULL);
	
	style->elements [MSTYLE_ORIENTATION].type = MSTYLE_ORIENTATION;
	style->elements [MSTYLE_ORIENTATION].u.orientation = o;
}

StyleOrientation
mstyle_get_orientation (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_ORIENTATION), 0);

	return style->elements [MSTYLE_ORIENTATION].u.align.v;
}

void
mstyle_set_fit_in_cell (MStyle *style, gboolean f)
{
	g_return_if_fail (style != NULL);

	style->elements [MSTYLE_FIT_IN_CELL].type = MSTYLE_FIT_IN_CELL;
	style->elements [MSTYLE_FIT_IN_CELL].u.fit_in_cell = f;
}

gboolean
mstyle_get_fit_in_cell (const MStyle *style)
{
	g_return_val_if_fail (mstyle_is_element_set (style, MSTYLE_FIT_IN_CELL), FALSE);

	return style->elements [MSTYLE_FIT_IN_CELL].u.fit_in_cell;
}
