#include <config.h>
#include <gnome.h>
#include <string.h>
#include "style.h"
#include "render.h"

typedef Style PrivateStyle; /* For future */

static guint32 stamp = 0;

Style *
style_new (const gchar *name)
{
	PrivateStyle *pst = g_new (PrivateStyle, 1);
	pst->stamp = stamp++;
	if (name) {
		g_warning ("names not yet supported");
		pst->name = g_strdup (name);
	} else
		pst->name = NULL;
	pst->elements = g_array_new (FALSE, FALSE, sizeof (StyleElement));

	return (Style *)pst;
}

Style *
style_new_elem (const gchar *name, StyleElement e)
{
	PrivateStyle *pst = style_new (name);

	style_add ((Style *)pst, e);

	return (Style *)pst;
}

Style *
style_new_array (const gchar *name, const GArray *elements)
{
	PrivateStyle *pst = style_new (name);

	style_add_array ((Style *)pst, elements);

	return (Style *)pst;
}

void
style_add (Style *st, StyleElement e)
{
	PrivateStyle *pst = st;
	g_return_if_fail (pst != NULL);

	g_array_append_val (pst->elements, e);
}

void
style_set (Style *st, StyleElement e)
{
	PrivateStyle *pst = st;
	int i;
	g_return_if_fail (pst != NULL);

	for (i = 0; i < pst->elements->len; i++)
		if (g_array_index (pst->elements, StyleElement,i).type
		    == e.type) {
			g_array_index (pst->elements, StyleElement,i) = e;
			return;
		}
	g_array_append_val (pst->elements, e);
}

void
style_add_array (Style *st, const GArray *elements)
{
	PrivateStyle *pst = st;
	g_return_if_fail (pst != NULL);

	g_array_append_vals (st->elements, elements->data,
			     elements->len);
}

Style *
style_merge (const Style *sta, const Style *stb)
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
style_to_string (const Style *st)
{
	guint i;
	GString *ans;
	const PrivateStyle *pst = (PrivateStyle *)st;

	g_return_val_if_fail (pst, "(null)");
	
	ans = g_string_new ("Elements : ");
	for (i = 0; i < pst->elements->len; i++) {
		guint n = g_array_index (pst->elements,
					 StyleElement, i).type;
		g_string_sprintfa (ans, "%d ", n);
	}
	
	return ans->str;
}

void
style_destroy (Style *st)
{
	PrivateStyle *pst = st;
	g_return_if_fail (pst != NULL);

	if (pst->name)
		g_free (pst->name);
	g_array_free (pst->elements, TRUE);
	g_free (pst);
}

static RenderInfo *
do_merge (const GList *l, StyleElementType max)
{
	StyleElement mash[STYLE_ELEMENT_MAX];
	/* Find the intersection */
	guint i;
	
	for (i = 0; i < max; i++)
		mash[i].type = STYLE_ELEMENT_ZERO;
	
	while (l) {
		guint j;
		PrivateStyle *pst = l->data;
		for (j = 0; j < pst->elements->len; j++) {
			StyleElement e = g_array_index (pst->elements, StyleElement, j);
			if (mash[e.type].type != STYLE_ELEMENT_ZERO)
				mash[e.type] = e;
		}
		l = g_list_next (l);
	}
	return render_info_new (mash, max);
}

static gboolean
check_sorted (const GList *l)
{
	guint32 stamp = -1;
	while (l) {
		PrivateStyle *pst = l->data;
		if (pst->stamp <= stamp) {
			printf ("Error on style '%s'\n", style_to_string (pst));
			return FALSE;
		}
		stamp = pst->stamp;
		l = g_list_next (l);
	}
	return TRUE;
}

RenderInfo *
render_merge (const GList *styles)
{
	g_return_val_if_fail (styles != NULL, NULL);
	g_return_val_if_fail (check_sorted (styles), NULL);
	return do_merge (styles, STYLE_ELEMENT_MAX);
}

RenderInfo *
render_merge_blank (const GList *styles)
{
	g_return_val_if_fail (styles != NULL, NULL);
	g_return_val_if_fail (check_sorted (styles), NULL);
	return do_merge (styles, STYLE_ELEMENT_MAX_BLANK);
}
