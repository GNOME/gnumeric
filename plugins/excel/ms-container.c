/**
 * ms-container.c: A meta container to handle object import for charts,
 *		workbooks and sheets.
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 2000-2005 Jody Goldberg
 **/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "ms-container.h"
#include "ms-escher.h"
#include "ms-obj.h"
#include "ms-excel-util.h"

#include <expr-name.h>
#include <value.h>

#include <gsf/gsf-utils.h>

void
ms_container_init (MSContainer *container, MSContainerClass const *vtbl,
		   MSContainer *parent,	GnmXLImporter *importer)
{
	container->vtbl = vtbl;
	container->importer = importer;
	container->free_blips = TRUE;
	container->blips = NULL;
	container->obj_queue  = NULL;
	container->parent = parent;

	container->v7.externsheets = NULL;
	container->v7.externnames = NULL;
}

void
ms_container_finalize (MSContainer *container)
{
	int i;

	g_return_if_fail (container != NULL);

	if (container->free_blips && container->blips != NULL) {
		for (i = container->blips->len; i-- > 0 ; ) {
			MSEscherBlip *blip = g_ptr_array_index (container->blips, i);
			if (blip != NULL)
				ms_escher_blip_free (blip);
		}

		g_ptr_array_free (container->blips, TRUE);
		container->blips = NULL;
	}

	if (container->obj_queue != NULL) {
		GSList *ptr;
		for (ptr = container->obj_queue; ptr != NULL; ptr = ptr->next)
			ms_obj_delete (ptr->data);

		g_slist_free (container->obj_queue);
		container->obj_queue = NULL;
	}

	if (container->v7.externsheets != NULL) {
		g_ptr_array_free (container->v7.externsheets, TRUE);
		container->v7.externsheets = NULL;
	}
	if (container->v7.externnames != NULL) {
		for (i = container->v7.externnames->len; i-- > 0 ; )
			if (g_ptr_array_index (container->v7.externnames, i) != NULL) {
				GnmNamedExpr *nexpr = g_ptr_array_index (container->v7.externnames, i);
				if (nexpr != NULL) {
					/* NAME placeholders need removal, EXTERNNAME placeholders
					 * will no be active */
					if (expr_name_is_active (nexpr) &&
					    expr_name_is_placeholder (nexpr) &&
					    /* FIXME: Why do we need this?  */
					    nexpr->ref_count == 2)
						expr_name_remove (nexpr);
					expr_name_unref (nexpr);
				}
			}
		g_ptr_array_free (container->v7.externnames, TRUE);
		container->v7.externnames = NULL;
	}
}

void
ms_container_add_blip (MSContainer *container, MSEscherBlip *blip)
{
	if (container->blips == NULL)
		container->blips = g_ptr_array_new ();
	g_ptr_array_add (container->blips, blip);
}

MSEscherBlip *
ms_container_get_blip (MSContainer *container, int blip_id)
{
	g_return_val_if_fail (container != NULL, NULL);
	g_return_val_if_fail (blip_id >= 0, NULL);

	if (container->parent != NULL &&
	    (container->blips == NULL || container->blips->len == 0))
		    return ms_container_get_blip (container->parent, blip_id);

	g_return_val_if_fail (container->blips != NULL, NULL);
	g_return_val_if_fail (blip_id < (int)container->blips->len, NULL);

	return g_ptr_array_index (container->blips, blip_id);
}

void
ms_container_set_blips (MSContainer *container, GPtrArray *blips)
{
	g_return_if_fail (container != NULL);
	g_return_if_fail (container->blips == NULL || container->blips == blips);

	container->blips = blips;
	container->free_blips = FALSE;
}

void
ms_container_add_obj (MSContainer *container, MSObj *obj)
{
#if 0
	g_warning ("registered obj %d\n", obj->id);
#endif
	container->obj_queue = g_slist_prepend (container->obj_queue, obj);
}

MSObj *
ms_container_get_obj (MSContainer *c, int obj_id)
{
	GSList *ptr;

	for (ptr = c->obj_queue ; ptr != NULL ; ptr = ptr->next) {
		MSObj *obj = ptr->data;
		if (obj != NULL && obj->id == obj_id)
			return obj;
	}
	g_warning ("did not find %d\n", obj_id);
	return NULL;
}

/**
 * ms_container_realize_objs:
 * @container:
 *
 *   This realizes the objects after the zoom factor has been
 * loaded.
 **/
void
ms_container_realize_objs (MSContainer *container)
{
	GSList *ptr;

	g_return_if_fail (container != NULL);
	g_return_if_fail (container->vtbl != NULL);
	g_return_if_fail (container->vtbl->realize_obj != NULL);

	for (ptr = container->obj_queue; ptr != NULL; ptr = ptr->next) {
		MSObj *obj = ptr->data;
		if (obj->gnum_obj != NULL)
			(void) (*container->vtbl->realize_obj) (container, obj);
	}
}

/**
 * ms_container_parse_expr:
 *
 * @c: The container
 * @data: the encoded expression
 * @length: the size of the encoded expression
 *
 * Attempts to parse the encoded expression in the context of the container.
 */
GnmExprTop const *
ms_container_parse_expr (MSContainer *c, guint8 const *data, int length)
{
	g_return_val_if_fail (c != NULL, NULL);
	g_return_val_if_fail (c->vtbl != NULL, NULL);
	g_return_val_if_fail (c->vtbl->parse_expr != NULL, NULL);
	if (length == 0)
		return NULL;
	return (*c->vtbl->parse_expr) (c, data, length);
}

/**
 * ms_container_sheet:
 *
 * @c: The container
 *
 * DEPRECATED !
 * This will become dependent_container when that abstraction is added
 * in the code.  We will need it to support tabs with standalone charts.
 * DEPRECATED !
 */
Sheet *
ms_container_sheet (MSContainer const *c)
{
	g_return_val_if_fail (c != NULL, NULL);
	g_return_val_if_fail (c->vtbl != NULL, NULL);
	if (c->vtbl->sheet == NULL)
		return NULL;
	return (*c->vtbl->sheet) (c);
}

GOFormat *
ms_container_get_fmt (MSContainer const *c, unsigned indx)
{
	for ( ; TRUE ; c = c->parent) {
	g_return_val_if_fail (c != NULL, NULL);
	g_return_val_if_fail (c->vtbl != NULL, NULL);
		if (c->vtbl->get_fmt != NULL)
			break;
	}
	return (*c->vtbl->get_fmt) (c, indx);
}

/**
 * ms_container_get_markup :
 * @c: #MSContainer
 * @indx:
 *
 * Return a #PangoAttrList the caller should not modify or free the list.
 **/
PangoAttrList *
ms_container_get_markup (MSContainer const *c, unsigned indx)
{
	for ( ; TRUE ; c = c->parent) {
		g_return_val_if_fail (c != NULL, NULL);
		g_return_val_if_fail (c->vtbl != NULL, NULL);
		if (c->vtbl->get_markup != NULL)
			break;
	}
	return (*c->vtbl->get_markup) (c, indx);
}

typedef struct {
	unsigned first, last;
	PangoAttrList *accum;
} TXORun;

static gboolean
append_txorun (PangoAttribute *src, TXORun *run)
{
	PangoAttribute *dst = pango_attribute_copy (src);
	dst->start_index = run->first;	/* inclusive */
	dst->end_index = run->last;	/* exclusive */
	pango_attr_list_change (run->accum, dst);
	return FALSE;
}

PangoAttrList *
ms_container_read_markup (MSContainer const *c,
			  guint8 const *data, size_t txo_len,
			  char const *str)
{
	TXORun txo_run;
	size_t str_len;

	XL_CHECK_CONDITION_VAL (txo_len >= 16,
				pango_attr_list_new ()); /* min two records */

	str_len = g_utf8_strlen (str, -1);

	txo_run.last = G_MAXINT;
	txo_run.accum = NULL;
	for (txo_len -= 16 ; (gssize)txo_len >= 0 ; txo_len -= 8) {
		guint16 o = GSF_LE_GET_GUINT16 (data + txo_len);
		guint16 idx = GSF_LE_GET_GUINT16 (data + txo_len + 2);

		XL_CHECK_CONDITION_VAL (o <= str_len, txo_run.accum);
		txo_run.first = g_utf8_offset_to_pointer (str, o) - str;
		XL_CHECK_CONDITION_VAL (txo_run.first < txo_run.last, txo_run.accum);

		if (idx != 0) {
			if (!txo_run.accum)
				txo_run.accum = pango_attr_list_new ();
			pango_attr_list_filter
				(ms_container_get_markup (c, idx),
				 (PangoAttrFilterFunc) append_txorun,
				 &txo_run);
		}
		txo_run.last = txo_run.first;
	}
	return txo_run.accum;
}
