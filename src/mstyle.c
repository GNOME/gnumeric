#include <config.h>
#include <gnome.h>
#include <string.h>
#include "style.h"
#include "sheet.h"
#include "mstyle.h"
#include "border.h"
#include "main.h"

#define STYLE_DEBUG (gnumeric_debugging > 0)

typedef struct {
	guint32        ref_count;
	gchar         *name;
	guint32        stamp;
	MStyleElement *elements;
} PrivateStyle;

static guint32 stamp = 0;

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

/* For a quick hash function */
static int element_size[] = {
	0, 0,
	sizeof (StyleColor *),
	sizeof (StyleColor *),
	sizeof (MStyleBorder *),
	sizeof (MStyleBorder *),
	sizeof (MStyleBorder *),
	sizeof (MStyleBorder *),
	sizeof (MStyleBorder *),
	sizeof (MStyleBorder *),
	sizeof (guint32),
	0, /* MAX_BLANK */
	sizeof (StyleColor *),
	sizeof (String *),
	sizeof (gboolean),
	sizeof (gboolean),
	sizeof (gboolean),
	sizeof (StyleFormat *),
	sizeof (StyleVAlignFlags),
	sizeof (StyleHAlignFlags),
	sizeof (StyleOrientation),
	sizeof (gboolean)
};

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
		g_string_sprintf (ans, "border top %d", e->u.border.top->line_type);
		break;
	case MSTYLE_BORDER_BOTTOM:
		g_string_sprintf (ans, "border bottom %d", e->u.border.bottom->line_type);
		break;
	case MSTYLE_BORDER_LEFT:
		g_string_sprintf (ans, "border left %d", e->u.border.left->line_type);
		break;
	case MSTYLE_BORDER_RIGHT:
		g_string_sprintf (ans, "border right %d", e->u.border.right->line_type);
		break;
	case MSTYLE_BORDER_DIAGONAL:
		g_string_sprintf (ans, "border diagonal %d", e->u.border.diagonal->line_type);
		break;
	case MSTYLE_BORDER_REV_DIAGONAL:
		g_string_sprintf (ans, "border reverse diagonal %d", e->u.border.rev_diagonal->line_type);
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
		if (a.u.border.top == b.u.border.top)
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
	int i, shift;
	guint32 crca, crcb;

	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	/* Quick check first */
	crca = crcb = 0;
	shift = 0;
	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
		crca = crca ^ (a[i].type << shift);
		crcb = crcb ^ (b[i].type << shift);
		shift++;
		if (shift > 22)
			shift = 0;
	}
	if (crca != crcb)
		return FALSE;

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++)
		if (!mstyle_element_equal (a[i], b[i])) {
			if (STYLE_DEBUG)
				printf ("%s mismatch\n", mstyle_names[i]);
			return FALSE;
		}

	return TRUE;
}

MStyleElement
mstyle_element_copy (MStyleElement e)
{
	switch (e.type) {
	case MSTYLE_ANY_COLOR:
		style_color_ref (e.u.color.fore);
		break;
	case MSTYLE_ANY_BORDER:
		border_ref (e.u.border.any);
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
mstyle_element_destroy (MStyleElement e)
{
	switch (e.type) {
	case MSTYLE_ANY_COLOR:
		style_color_unref (e.u.color.fore);
		break;
	case MSTYLE_ANY_BORDER:
		border_unref (e.u.border.any);
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
		if (b[i].type == MSTYLE_ELEMENT_UNSET)
			continue;

		if (!mstyle_element_equal (a[i], b[i])) {
			mstyle_element_destroy (a[i]);
			a[i].type = MSTYLE_ELEMENT_CONFLICT;
		}
	}

}

void
mstyle_elements_destroy (MStyleElement *e)
{
	int i;
	if (e)
		for (i = 0; i < MSTYLE_ELEMENT_MAX; i++)
			mstyle_element_destroy (e[i]);
}

void
mstyle_elements_init (MStyleElement *e)
{
	int i;
	g_return_if_fail (e != NULL);

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++)
		e[i].type = MSTYLE_ELEMENT_UNSET;
}

MStyle *
mstyle_new (const gchar *name)
{
	PrivateStyle *pst = g_new (PrivateStyle, 1);
	int i;

	pst->ref_count = 1;
	if (name) {
		g_warning ("names not yet supported");
		pst->name = g_strdup (name);
	} else
		pst->name = NULL;
	pst->stamp = stamp++;
	pst->elements  = g_new (MStyleElement, MSTYLE_ELEMENT_MAX);

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++)
		pst->elements[i].type = MSTYLE_ELEMENT_UNSET;

	return (MStyle *)pst;
}

MStyle *
mstyle_new_elems (const gchar *name, const MStyleElement *e)
{
	PrivateStyle *pst = (PrivateStyle *)mstyle_new (name);

	memcpy (pst->elements, e, sizeof (MStyleElement) * MSTYLE_ELEMENT_MAX);

	return (MStyle *)pst;
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
mstyle_new_elem (const gchar *name, MStyleElement e)
{
	PrivateStyle *pst = (PrivateStyle *)mstyle_new (name);

	mstyle_add ((MStyle *)pst, e);

	return (MStyle *)pst;
}

void
mstyle_add (MStyle *st, MStyleElement e)
{
	PrivateStyle *pst = (PrivateStyle *)st;

	g_return_if_fail (pst != NULL);
	g_return_if_fail (e.type >= MSTYLE_ELEMENT_UNSET);
	g_return_if_fail (e.type <  MSTYLE_ELEMENT_MAX);
	g_return_if_fail (pst->elements[e.type].type == MSTYLE_ELEMENT_UNSET);

	pst->elements[e.type] = e;
}

void
mstyle_set (MStyle *st, MStyleElement e)
{
	PrivateStyle *pst = (PrivateStyle *)st;

	g_return_if_fail (pst != NULL);
	g_return_if_fail (e.type >= MSTYLE_ELEMENT_UNSET);
	g_return_if_fail (e.type <  MSTYLE_ELEMENT_MAX);
	g_return_if_fail (pst->elements[e.type].type == MSTYLE_ELEMENT_UNSET);

	if (pst->elements[e.type].type)
		mstyle_element_destroy (pst->elements[e.type]);

	pst->elements[e.type] = e;
}

const MStyleElement *
mstyle_get_elements (MStyle *st)
{
	g_return_val_if_fail (st != NULL, NULL);
	return ((PrivateStyle *)st)->elements;
}


MStyle *
mstyle_merge (const MStyle *sta, const MStyle *stb)
{
	PrivateStyle *pstm, *psts; /* Master, slave */
	PrivateStyle *ans;
	int i;

	g_return_val_if_fail (sta != NULL, NULL);
	g_return_val_if_fail (stb != NULL, NULL);

	if (((PrivateStyle *)sta)->stamp >
	    ((PrivateStyle *)stb)->stamp) {
		pstm = (PrivateStyle *)sta;
		psts = (PrivateStyle *)stb;
	} else {
		pstm = (PrivateStyle *)stb;
		psts = (PrivateStyle *)sta;
	}

	if (pstm->stamp == psts->stamp)
		g_warning ("Odd merging regions with same stamp");

	ans = (PrivateStyle *)mstyle_new (NULL);

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
		if (pstm->elements[i].type)
			ans->elements[i] = mstyle_element_copy (pstm->elements[i]);
		else if (psts->elements[i].type)
			ans->elements[i] = mstyle_element_copy (psts->elements[i]);
	}

	return (MStyle *)ans;
}

char *
mstyle_to_string (const MStyle *st)
{
	guint i;
	GString *ans;
	char    *txt_ans;
	const PrivateStyle *pst = (PrivateStyle *)st;

	g_return_val_if_fail (pst, "(null)");
	
	ans = g_string_new ("Elements : ");
	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
		char *txt;
		if (pst->elements[i].type) {
			txt = mstyle_element_dump (&pst->elements[i]);
			g_string_sprintfa (ans, "%s ", txt);
			g_free (txt);
		}
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

	printf ("Style '%s', stamp %d\n", pst->name?pst->name:"unnamed",
		pst->stamp);
	txt = mstyle_to_string (st);
	printf ("%s\n", txt);
	g_free (txt);
}

void
mstyle_destroy (MStyle *st)
{
	if (!st) {
		PrivateStyle *pst = (PrivateStyle *)st;

		g_return_if_fail (pst->ref_count > 1);

		if (pst->name)
			g_free (pst->name);
		pst->name = NULL;

		mstyle_elements_destroy (pst->elements);
		pst->elements = NULL;
		
		g_free (pst);
	}
}

static void
dump_style_list (const GList *l)
{
	printf ("Style list:\n");
	while (l) {
		PrivateStyle *pst = l->data;
		printf ("%d: '%s' ", pst->stamp,
			mstyle_to_string ((MStyle *)pst));
		l = g_list_next (l);
	}
	printf ("End of style list\n");
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

void
mstyle_do_merge (const GList *list, MStyleElementType max,
		 MStyleElement *mash)
{
	const GList *l = list;
	/* Find the intersection */
	guint i, numset=0;
	
	g_return_if_fail (mash != NULL);

	for (i = 0; i < max; i++)
		mash[i].type = MSTYLE_ELEMENT_UNSET;
	
	while (l && (numset < max)) {
		guint j;
		PrivateStyle *pst = l->data;
		for (j = 0; j < max; j++) {
			MStyleElement e = pst->elements[j];
			if (e.type > MSTYLE_ELEMENT_UNSET &&
			    mash[e.type].type == MSTYLE_ELEMENT_UNSET) {
				mash[e.type] = e;
				numset++;
			}
		}
		l = g_list_next (l);
	}

/*	printf ("do merge:\n");
	for (i = 0; i < max; i++) {
		char *txt = mstyle_element_dump (&mash[i]);
		printf ("%s\n", txt);
		g_free (txt);
		}*/
}

gboolean
mstyle_list_check_sorted (const GList *list)
{
	const GList *l = list;
	guint32 stamp = -1; /* max guint32 */

	while (l) {
		PrivateStyle *pst = l->data;
		/*
		 *  We can have several copies of a style with the same stamp
		 * in the queue, each is ref-counted.
		 */
		if (pst->stamp > stamp &&
		    STYLE_DEBUG) {
			g_warning ("Error on style sorting");
			dump_style_list (list);
			return FALSE;
		}
		stamp = pst->stamp;
		l = g_list_next (l);
	}
	return TRUE;
}
