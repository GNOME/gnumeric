#include <config.h>
#include <gnome.h>
#include <string.h>
#include "style.h"
#include "sheet.h"
#include "mstyle.h"

typedef MStyle PrivateStyle; /* For future */

static guint32 stamp = 0;

static char *
mstyle_element_dump (const MStyleElement *e)
{
	GString *ans = g_string_new ("");
	char    *txt_ans;

	g_return_val_if_fail (e != NULL, g_strdup ("Duff element"));

	switch (e->type) {
	case MSTYLE_ELEMENT_ZERO:
		g_string_sprintf (ans, "Unset");
		break;
	case MSTYLE_COLOR_FORE:
		g_string_sprintf (ans, "foregnd col");
		break;
	case MSTYLE_COLOR_BACK:
		g_string_sprintf (ans, "backgnd col");
		break;		
	case MSTYLE_FONT_NAME:
		g_string_sprintf (ans, "name '%s'", e->u.font.name);
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
	default:
		g_string_sprintf (ans, "Unknown type %d", e->type);
		break;
	}

	txt_ans = ans->str;
	g_string_free (ans, FALSE);

	return txt_ans;
}

MStyle *
mstyle_new (const gchar *name)
{
	PrivateStyle *pst = g_new (PrivateStyle, 1);
	pst->stamp = stamp++;
	if (name) {
		g_warning ("names not yet supported");
		pst->name = g_strdup (name);
	} else
		pst->name = NULL;
	pst->elements = g_array_new (FALSE, FALSE, sizeof (MStyleElement));

	return (MStyle *)pst;
}

MStyle *
mstyle_new_elem (const gchar *name, MStyleElement e)
{
	PrivateStyle *pst = (PrivateStyle *)mstyle_new (name);

	mstyle_add ((MStyle *)pst, e);

	return (MStyle *)pst;
}

MStyle *
mstyle_new_array (const gchar *name, const GArray *elements)
{
	PrivateStyle *pst = (PrivateStyle *)mstyle_new (name);

	mstyle_add_array ((MStyle *)pst, elements);

	return (MStyle *)pst;
}

void
mstyle_add (MStyle *st, MStyleElement e)
{
	PrivateStyle *pst = st;
	g_return_if_fail (pst != NULL);

	g_array_append_val (pst->elements, e);
}

void
mstyle_set (MStyle *st, MStyleElement e)
{
	PrivateStyle *pst = st;
	int i;
	g_return_if_fail (pst != NULL);

	for (i = 0; i < pst->elements->len; i++)
		if (g_array_index (pst->elements, MStyleElement,i).type
		    == e.type) {
			g_array_index (pst->elements, MStyleElement,i) = e;
			return;
		}
	g_array_append_val (pst->elements, e);
}

void
mstyle_add_array (MStyle *st, const GArray *elements)
{
	PrivateStyle *pst = st;
	g_return_if_fail (pst != NULL);

	g_array_append_vals (st->elements, elements->data,
			     elements->len);
}

MStyle *
mstyle_merge (const MStyle *sta, const MStyle *stb)
{
	PrivateStyle *pstm, *psts; /* Master, slave */
	g_return_val_if_fail (sta != NULL, NULL);
	g_return_val_if_fail (stb != NULL, NULL);
	g_return_val_if_fail (sta->stamp != stb->stamp, NULL);

	if (sta->stamp > stb->stamp) {
		pstm = (PrivateStyle *)sta;
		psts = (PrivateStyle *)stb;
	} else {
		pstm = (PrivateStyle *)stb;
		psts = (PrivateStyle *)sta;
	}

	g_warning ("Merge unimplemented");
	return NULL;
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
	for (i = 0; i < pst->elements->len; i++) {
		char *txt = mstyle_element_dump (&g_array_index (pst->elements,
								 MStyleElement, i));
		g_string_sprintfa (ans, "%s ", txt);
		g_free (txt);
	}
	txt_ans = ans->str;
	g_string_free (ans, FALSE);

	return txt_ans;
}

void
mstyle_dump (const MStyle *st)
{
	char *txt;
	const PrivateStyle *pst = st;

	printf ("Style '%s', stamp %d\n", pst->name?pst->name:"unnamed",
		pst->stamp);
	txt = mstyle_to_string (st);
	printf ("%s\n", txt);
	g_free (txt);
}

void
mstyle_destroy (MStyle *st)
{
	PrivateStyle *pst = st;
	g_return_if_fail (pst != NULL);

	if (pst->name)
		g_free (pst->name);
	pst->name = NULL;

	g_array_free (pst->elements, TRUE);
	pst->elements = NULL;

	g_free (pst);
}

static void
dump_style_list (const GList *l)
{
	printf ("Style list:\n");
	while (l) {
		PrivateStyle *pst = l->data;
		printf ("%d: '%s' ", pst->stamp,
			mstyle_to_string (pst));
		l = g_list_next (l);
	}
	printf ("End of style list\n");
}

static Style *
do_merge (const GList *list, MStyleElementType max)
{
	const GList *l = list;
	MStyleElement mash[MSTYLE_ELEMENT_MAX];
	/* Find the intersection */
	guint i;
	
	for (i = 0; i < max; i++)
		mash[i].type = MSTYLE_ELEMENT_ZERO;
	
	while (l) {
		guint j;
		PrivateStyle *pst = l->data;
		for (j = 0; j < pst->elements->len; j++) {
			MStyleElement e = g_array_index (pst->elements,
							 MStyleElement, j);
			if (e.type < max &&
			    mash[e.type].type == MSTYLE_ELEMENT_ZERO)
				mash[e.type] = e;
		}
		l = g_list_next (l);
	}

/*	printf ("do merge:\n");
	for (i = 0; i < max; i++) {
		char *txt = mstyle_element_dump (&mash[i]);
		printf ("%s\n", txt);
		g_free (txt);
		}*/

	return style_mstyle_new (mash, max);
}

static gboolean
check_sorted (const GList *list)
{
	const GList *l = list;
	guint32 stamp = -1; /* max guint32 */

	while (l) {
		PrivateStyle *pst = l->data;
		if (pst->stamp >= stamp) {
			printf ("Error on style sorting:");
			dump_style_list (list);
			return FALSE;
		}
		stamp = pst->stamp;
		l = g_list_next (l);
	}
	return TRUE;
}

Style *
render_merge (const GList *styles)
{
	if (!check_sorted (styles))
	    g_warning ("Styles not sorted");
	return do_merge (styles, MSTYLE_ELEMENT_MAX);
}

Style *
render_merge_blank (const GList *styles)
{
	if (!check_sorted (styles))
	    g_warning ("Styles not sorted");
	return do_merge (styles, MSTYLE_ELEMENT_MAX_BLANK);
}
