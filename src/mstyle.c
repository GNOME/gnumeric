/*
 * MStyle.c: The guts of the style engine.
 *
 * Author:
 *   Michael Meeks <mmeeks@gnu.org>
 */
#include <config.h>
#include <gnome.h>
#include <string.h>
#include "style.h"
#include "sheet.h"
#include "mstyle.h"
#include "border.h"
#include "main.h"

#define STYLE_DEBUG (style_debugging > 2)

typedef struct _MStyleElement     MStyleElement;

#define MSTYLE_ANY_COLOR             MSTYLE_COLOR_FORE: \
				case MSTYLE_COLOR_BACK: \
				case MSTYLE_COLOR_PATTERN
#define MSTYLE_ANY_BORDER            MSTYLE_BORDER_TOP: \
				case MSTYLE_BORDER_BOTTOM: \
				case MSTYLE_BORDER_LEFT: \
				case MSTYLE_BORDER_RIGHT: \
				case MSTYLE_BORDER_DIAGONAL: \
				case MSTYLE_BORDER_REV_DIAGONAL

extern const char *mstyle_names[MSTYLE_ELEMENT_MAX];

struct _MStyleElement {
/*	MStyleElementType type; - memory efficiency */
	guint8 type;
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
			float     size;
		}                font;
		StyleFormat     *format;
		union {
			guint16   v;
			guint16   h;
		}                align;
		StyleOrientation orientation;
		gboolean         fit_in_cell;
	} u;
};

MStyleElement  mstyle_element_ref      (MStyleElement e);
void           mstyle_element_unref    (MStyleElement e);
gboolean       mstyle_elements_equal   (const MStyleElement *a, const MStyleElement *b);
void           mstyle_elements_compare (MStyleElement *a, const MStyleElement *b);
void           mstyle_elements_unref   (MStyleElement *e);

typedef struct {
	guint32        ref_count;
	gchar         *name;
	MStyleElement *elements;
} PrivateStyle;
#define MSTYLE_ELEMENTS(s) (((PrivateStyle *)s)->elements)

const char *mstyle_names[MSTYLE_ELEMENT_MAX] = {
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
	guint32 hash;

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
		MStyleElement *e = &MSTYLE_ELEMENTS (mstyle) [i];
		hash = hash << 8;
		switch (i) {
		case MSTYLE_ANY_COLOR:
			hash = hash ^ GPOINTER_TO_UINT (e->u.color.any);
			break;
		case MSTYLE_ANY_BORDER:
			hash = hash ^ GPOINTER_TO_UINT (e->u.border.any);
			break;
		default:
			g_warning ("Unimplemented hash item");
			break;
		}
	}

	return 0;
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
gboolean
mstyle_elements_equal (const MStyleElement *a,
		       const MStyleElement *b)
{
	int i;

	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	for (i = 1; i < MSTYLE_ELEMENT_MAX; i++) {

		g_assert (i < MSTYLE_ELEMENT_MAX);

		if (a[i].type != b[i].type)
			return FALSE;

		if (!mstyle_element_equal (a[i], b[i])) {
			g_assert (i < MSTYLE_ELEMENT_MAX);
			if (STYLE_DEBUG)
				printf ("%s mismatch\n", mstyle_names[i]);
			return FALSE;
		}
	}

	return TRUE;
}

MStyleElement
mstyle_element_ref (MStyleElement e)
{
	switch (e.type) {
	case MSTYLE_ANY_COLOR:
		style_color_ref (e.u.color.any);
		break;
	case MSTYLE_ANY_BORDER:
		style_border_ref (e.u.border.any);
		break;
	case MSTYLE_FONT_NAME:
		string_ref (e.u.font.name);
		break;
	case MSTYLE_FORMAT:
		style_format_ref (e.u.format);
		break;
	default:
		break;
	}
	return e;
}

void
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
void
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
			mstyle_element_ref (b[i]);
			a[i] = b[i];
		} else if (!mstyle_element_equal (a[i], b[i])) {
			mstyle_element_unref (a[i]);
			a[i].type = MSTYLE_ELEMENT_CONFLICT;
		}
	}

}

void
mstyle_compare (MStyle *a, const MStyle *b)
{
	mstyle_elements_compare (((PrivateStyle *)a)->elements,
				 ((const PrivateStyle *)b)->elements);
}

void
mstyle_elements_unref (MStyleElement *e)
{
	int i;
	if (e)
		for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
			mstyle_element_unref (e[i]);
			e[i].type = MSTYLE_ELEMENT_UNSET;
		}
}

MStyle *
mstyle_new (void)
{
	PrivateStyle *pst = g_new (PrivateStyle, 1);
	int i;

	pst->ref_count = 1;
	pst->name = NULL;
	pst->elements  = g_new (MStyleElement, MSTYLE_ELEMENT_MAX);

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++)
		pst->elements[i].type = MSTYLE_ELEMENT_UNSET;

	return (MStyle *)pst;
}

MStyle *
mstyle_new_name (const gchar *name)
{
	PrivateStyle *pst = (PrivateStyle *)mstyle_new ();

	if (name) {
		g_warning ("names not yet supported");
		pst->name = g_strdup (name);
	}

	return (MStyle *)pst;
}

static MStyle *default_mstyle = NULL;

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
	mstyle_set_align_v     (mstyle, VALIGN_CENTER);
	mstyle_set_align_h     (mstyle, HALIGN_GENERAL);
	mstyle_set_orientation (mstyle, ORIENT_HORIZ);
	mstyle_set_fit_in_cell (mstyle, 0);
	mstyle_set_font_name   (mstyle, DEFAULT_FONT);
	mstyle_set_font_bold   (mstyle, 0);
	mstyle_set_font_italic (mstyle, 0);
	mstyle_set_font_size   (mstyle, DEFAULT_SIZE);

	mstyle_set_color       (mstyle, MSTYLE_COLOR_FORE,
				style_color_new (0, 0, 0));
	mstyle_set_color       (mstyle, MSTYLE_COLOR_BACK,
				style_color_new (0xffff, 0xffff, 0xffff));
	mstyle_set_color       (mstyle, MSTYLE_COLOR_PATTERN,
				style_color_new (0, 0, 0));

	/* To negate borders */
	mstyle_set_border      (mstyle, MSTYLE_BORDER_TOP, NULL);
	mstyle_set_border      (mstyle, MSTYLE_BORDER_LEFT, NULL);
	mstyle_set_border      (mstyle, MSTYLE_BORDER_BOTTOM, NULL);
	mstyle_set_border      (mstyle, MSTYLE_BORDER_RIGHT, NULL);
	mstyle_set_border      (mstyle, MSTYLE_BORDER_DIAGONAL, NULL);
	mstyle_set_border      (mstyle, MSTYLE_BORDER_REV_DIAGONAL, NULL);

	/* This negates the back and pattern colors */
	mstyle_set_pattern     (mstyle, 0);

	default_mstyle = mstyle;

	return mstyle;
}

void
mstyle_ref (MStyle *st)
{
	PrivateStyle *pst = (PrivateStyle *)st;
	g_return_if_fail (pst->ref_count > 0);
	pst->ref_count++;
}

void
mstyle_unref (MStyle *st)
{
	PrivateStyle *pst = (PrivateStyle *)st;

	g_return_if_fail (pst->ref_count > 0);

	pst->ref_count--;

	if (pst->ref_count == 0)
		mstyle_destroy (st);
}

MStyle *
mstyle_do_merge (const GList *list, MStyleElementType max)
{
	const GList *l = list;
	/* Find the intersection */
	guint numset = 0;
	MStyle *ans = mstyle_new ();
	MStyleElement *mash = MSTYLE_ELEMENTS (ans);
	g_return_val_if_fail (max <= MSTYLE_ELEMENT_MAX,
			      mstyle_new_default ());
	g_return_val_if_fail (max > 0,
			      mstyle_new_default ());
		
	while (l && (numset < max)) {
		guint j;
		PrivateStyle *pst = l->data;
		for (j = 0; j < max; j++) {
			MStyleElement e = pst->elements[j];
			if (e.type > MSTYLE_ELEMENT_UNSET &&
			    mash[e.type].type == MSTYLE_ELEMENT_UNSET) {
				g_return_val_if_fail (e.type < MSTYLE_ELEMENT_MAX,
						      ans);
				mash[e.type] = mstyle_element_ref (e);
				numset++;
			}
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
 * 
 **/
void
mstyle_merge (MStyle *master, MStyle *slave)
{
	PrivateStyle *pstm, *psts; /* Master, slave */
	int i;

	g_return_if_fail (slave != NULL);
	g_return_if_fail (master != NULL);

	psts = (PrivateStyle *)slave;
	pstm = (PrivateStyle *)master;

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
		if (pstm->elements[i].type && psts->elements[i].type) {
			mstyle_element_unref (psts->elements[i]);
			psts->elements[i].type = MSTYLE_ELEMENT_UNSET;
		}
	}
}

char *
mstyle_to_string (const MStyle *st)
{
	guint i;
	GString *ans;
	char    *txt_ans;
	const PrivateStyle *pst = (PrivateStyle *)st;

	g_return_val_if_fail (pst != NULL, "(null)");
	
	ans = g_string_new ("Elements : ");
	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
		char *txt;
		if (pst->elements[i].type) {
			txt = mstyle_element_dump (&pst->elements[i]);
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
mstyle_dump (const MStyle *st)
{
	char *txt;
	const PrivateStyle *pst = (PrivateStyle *)st;

	printf ("Style '%s'\n", pst->name?pst->name:"unnamed");
	txt = mstyle_to_string (st);
	printf ("%s\n", txt);
	g_free (txt);
}

void
mstyle_destroy (MStyle *st)
{
	PrivateStyle *pst = (PrivateStyle *)st;

	g_return_if_fail (pst != NULL);
	g_return_if_fail (pst->ref_count == 0);
	
	if (pst->name)
		g_free (pst->name);
	pst->name = NULL;

	if (pst->elements) {
		mstyle_elements_unref (pst->elements);
		g_free (pst->elements);
	}
	pst->elements = NULL;
		
	g_free (pst);
}

gboolean
mstyle_equal (const MStyle *a, const MStyle *b)
{
	PrivateStyle *pa = (PrivateStyle *)a;
	PrivateStyle *pb = (PrivateStyle *)b;

	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	if (a == b)
		return TRUE;

	if (pa->name || pb->name)
		g_warning ("Named style equal unimplemented");

	return mstyle_elements_equal (pa->elements, pb->elements);
}

gboolean
mstyle_empty (const MStyle *st)
{
	PrivateStyle *pst = (PrivateStyle *)st;
	int i;

	g_return_val_if_fail (st != NULL, FALSE);

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++)
		if (pst->elements[i].type)
			return FALSE;
	return TRUE;
}

gboolean
mstyle_verify (const MStyle *st)
{
	PrivateStyle *pst = (PrivateStyle *)st;
	int j;

	for (j = 0; j < MSTYLE_ELEMENT_MAX; j++) {
		MStyleElement e = pst->elements[j];

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

	return  MSTYLE_ELEMENTS (st) [t].type != MSTYLE_ELEMENT_UNSET &&
		MSTYLE_ELEMENTS (st) [t].type != MSTYLE_ELEMENT_CONFLICT;
}

gboolean
mstyle_is_element_conflict (const MStyle *st, MStyleElementType t)
{
	g_return_val_if_fail (st != NULL, FALSE);
	g_return_val_if_fail (t > 0 && t < MSTYLE_ELEMENT_MAX, FALSE);

	return MSTYLE_ELEMENTS (st) [t].type == MSTYLE_ELEMENT_CONFLICT;
}

void
mstyle_unset_element (MStyle *st, MStyleElementType t)
{
	g_return_if_fail (st != NULL);
	g_return_if_fail (t > 0 && t < MSTYLE_ELEMENT_MAX);

	mstyle_element_unref (MSTYLE_ELEMENTS (st) [t]);
	MSTYLE_ELEMENTS (st) [t].type = MSTYLE_ELEMENT_UNSET;
}

void
mstyle_set_color (MStyle *st, MStyleElementType t,
		  StyleColor *col)
{
	g_return_if_fail (st != NULL);
	g_return_if_fail (col != NULL);

	switch (t) {
	case MSTYLE_ANY_COLOR:
		mstyle_element_unref (MSTYLE_ELEMENTS (st) [t]);
		MSTYLE_ELEMENTS (st) [t].type = t;
		MSTYLE_ELEMENTS (st) [t].u.color.any = col;
		break;
	default:
		g_warning ("Not a color element");
		break;
	}
}

StyleColor *
mstyle_get_color (MStyle *st, MStyleElementType t)
{
	g_return_val_if_fail (st != NULL, NULL);
	g_return_val_if_fail (mstyle_is_element_set (st, t), NULL);

	switch (t) {
	case MSTYLE_ANY_COLOR:
		return MSTYLE_ELEMENTS (st) [t].u.color.any;
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
		mstyle_element_unref (MSTYLE_ELEMENTS (st) [t]);
		MSTYLE_ELEMENTS (st) [t].type = t;
		MSTYLE_ELEMENTS (st) [t].u.border.any = border;
		break;
	default:
		g_warning ("Not a color element");
		break;
	}

}

const MStyleBorder *
mstyle_get_border (const MStyle *st, MStyleElementType t)
{
	g_return_val_if_fail (st != NULL, NULL);
	g_return_val_if_fail (mstyle_is_element_set (st, t), NULL);

	switch (t) {
	case MSTYLE_ANY_BORDER:
		return MSTYLE_ELEMENTS (st) [t].u.border.any;
	default:
		g_warning ("Not a color element");
		return NULL;
	}
}

void
mstyle_set_pattern (MStyle *st, int pattern)
{
	g_return_if_fail (st != NULL);

	MSTYLE_ELEMENTS (st) [MSTYLE_PATTERN].type = MSTYLE_PATTERN;
	MSTYLE_ELEMENTS (st) [MSTYLE_PATTERN].u.pattern = pattern;
}

int
mstyle_get_pattern (const MStyle *st)
{
	g_return_val_if_fail (st != NULL, 0);
	g_return_val_if_fail (mstyle_is_element_set (st, MSTYLE_PATTERN), 0);

	return MSTYLE_ELEMENTS (st) [MSTYLE_PATTERN].u.pattern;
}

StyleFont *
mstyle_get_font (const MStyle *st, double zoom)
{
	StyleFont *font;
	const gchar *name;
	int    bold, italic;
	double size;

	g_return_val_if_fail (st != NULL, NULL);

	if (mstyle_is_element_set (st, MSTYLE_FONT_NAME))
		name = mstyle_get_font_name (st);
	else
		name = DEFAULT_FONT;
	if (mstyle_is_element_set (st, MSTYLE_FONT_BOLD))
		bold = mstyle_get_font_bold (st);
	else
		bold = 0;
	if (mstyle_is_element_set (st, MSTYLE_FONT_ITALIC))
		italic = mstyle_get_font_italic (st);
	else
		italic = 0;
	if (mstyle_is_element_set (st, MSTYLE_FONT_SIZE))
		size = mstyle_get_font_size (st);
	else
		size = DEFAULT_SIZE;

	font = style_font_new (name, size, zoom,
			       bold, italic);
	
	return font;
}

void
mstyle_set_font_name (MStyle *st, const char *name)
{
	g_return_if_fail (st != NULL);
	g_return_if_fail (name != NULL);
	
	mstyle_element_unref (MSTYLE_ELEMENTS (st) [MSTYLE_FONT_NAME]);
	MSTYLE_ELEMENTS (st) [MSTYLE_FONT_NAME].type = MSTYLE_FONT_NAME;
	MSTYLE_ELEMENTS (st) [MSTYLE_FONT_NAME].u.font.name = string_get (name);
}

const char *
mstyle_get_font_name (const MStyle *st)
{
	g_return_val_if_fail (st != NULL, NULL);
	g_return_val_if_fail (mstyle_is_element_set (st, MSTYLE_FONT_NAME), NULL);

	return MSTYLE_ELEMENTS (st) [MSTYLE_FONT_NAME].u.font.name->str;
}

void
mstyle_set_font_bold (MStyle *st, gboolean bold)
{
	g_return_if_fail (st != NULL);

	MSTYLE_ELEMENTS (st) [MSTYLE_FONT_BOLD].type = MSTYLE_FONT_BOLD;
	MSTYLE_ELEMENTS (st) [MSTYLE_FONT_BOLD].u.font.bold = bold;
}

gboolean
mstyle_get_font_bold (const MStyle *st)
{
	g_return_val_if_fail (st != NULL, FALSE);
	g_return_val_if_fail (mstyle_is_element_set (st, MSTYLE_FONT_BOLD), FALSE);

	return MSTYLE_ELEMENTS (st) [MSTYLE_FONT_BOLD].u.font.bold;
}

void
mstyle_set_font_italic (MStyle *st, gboolean italic)
{
	g_return_if_fail (st != NULL);

	MSTYLE_ELEMENTS (st) [MSTYLE_FONT_ITALIC].type = MSTYLE_FONT_ITALIC;
	MSTYLE_ELEMENTS (st) [MSTYLE_FONT_ITALIC].u.font.italic = italic;
}

gboolean
mstyle_get_font_italic (const MStyle *st)
{
	g_return_val_if_fail (st != NULL, FALSE);
	g_return_val_if_fail (mstyle_is_element_set (st, MSTYLE_FONT_ITALIC), FALSE);

	return MSTYLE_ELEMENTS (st) [MSTYLE_FONT_ITALIC].u.font.italic;
}

void
mstyle_set_font_size (MStyle *st, double size)
{
	g_return_if_fail (st != NULL);

	MSTYLE_ELEMENTS (st) [MSTYLE_FONT_SIZE].type = MSTYLE_FONT_SIZE;
	MSTYLE_ELEMENTS (st) [MSTYLE_FONT_SIZE].u.font.size = size;
}

double
mstyle_get_font_size (const MStyle *st)
{
	g_return_val_if_fail (st != NULL, 12.0);
	g_return_val_if_fail (mstyle_is_element_set (st, MSTYLE_FONT_ITALIC), 12.0);

	return MSTYLE_ELEMENTS (st) [MSTYLE_FONT_SIZE].u.font.size;
}

void
mstyle_set_format (MStyle *st, const char *format)
{
	g_return_if_fail (st != NULL);
	g_return_if_fail (format != NULL);

	mstyle_element_unref (MSTYLE_ELEMENTS (st) [MSTYLE_FORMAT]);
	MSTYLE_ELEMENTS (st) [MSTYLE_FORMAT].type = MSTYLE_FORMAT;
	MSTYLE_ELEMENTS (st) [MSTYLE_FORMAT].u.format = style_format_new (format);
}

StyleFormat *
mstyle_get_format (MStyle *st)
{
	g_return_val_if_fail (st != NULL, NULL);
	g_return_val_if_fail (mstyle_is_element_set (st, MSTYLE_FORMAT), NULL);

	return MSTYLE_ELEMENTS (st) [MSTYLE_FORMAT].u.format;
}

void
mstyle_set_align_h (MStyle *st, StyleHAlignFlags a)
{
	g_return_if_fail (st != NULL);

	MSTYLE_ELEMENTS (st) [MSTYLE_ALIGN_H].type = MSTYLE_ALIGN_H;
	MSTYLE_ELEMENTS (st) [MSTYLE_ALIGN_H].u.align.h = a;
}

StyleHAlignFlags
mstyle_get_align_h (const MStyle *st)
{
	g_return_val_if_fail (st != NULL, 0);
	g_return_val_if_fail (mstyle_is_element_set (st, MSTYLE_ALIGN_H), 0);

	return MSTYLE_ELEMENTS (st) [MSTYLE_ALIGN_H].u.align.h;
}

void
mstyle_set_align_v (MStyle *st, StyleVAlignFlags a)
{
	g_return_if_fail (st != NULL);

	MSTYLE_ELEMENTS (st) [MSTYLE_ALIGN_V].type = MSTYLE_ALIGN_V;
	MSTYLE_ELEMENTS (st) [MSTYLE_ALIGN_V].u.align.v = a;
}

StyleVAlignFlags
mstyle_get_align_v (const MStyle *st)
{
	g_return_val_if_fail (st != NULL, 0);
	g_return_val_if_fail (mstyle_is_element_set (st, MSTYLE_ALIGN_V), 0);

	return MSTYLE_ELEMENTS (st) [MSTYLE_ALIGN_V].u.align.v;
}

void
mstyle_set_orientation (MStyle *st, StyleOrientation o)
{
	g_return_if_fail (st != NULL);
	MSTYLE_ELEMENTS (st) [MSTYLE_ORIENTATION].type = MSTYLE_ORIENTATION;
	MSTYLE_ELEMENTS (st) [MSTYLE_ORIENTATION].u.orientation = o;
}

StyleOrientation
mstyle_get_orientation (const MStyle *st)
{
	g_return_val_if_fail (st != NULL, 0);
	g_return_val_if_fail (mstyle_is_element_set (st, MSTYLE_ORIENTATION), 0);

	return MSTYLE_ELEMENTS (st) [MSTYLE_ORIENTATION].u.align.v;
}

void
mstyle_set_fit_in_cell (MStyle *st, gboolean f)
{
	g_return_if_fail (st != NULL);

	MSTYLE_ELEMENTS (st) [MSTYLE_FIT_IN_CELL].type = MSTYLE_FIT_IN_CELL;
	MSTYLE_ELEMENTS (st) [MSTYLE_FIT_IN_CELL].u.fit_in_cell = f;
}

gboolean
mstyle_get_fit_in_cell (const MStyle *st)
{
	g_return_val_if_fail (st != NULL, FALSE);
	g_return_val_if_fail (mstyle_is_element_set (st, MSTYLE_FIT_IN_CELL), FALSE);

	return MSTYLE_ELEMENTS (st) [MSTYLE_FIT_IN_CELL].u.fit_in_cell;
}
