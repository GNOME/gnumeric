/* vim: set sw=8: */

/**
 * ms-container.c: A meta container to handle object import for charts,
 * 		workbooks and sheets.
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 2000-2002 Jody Goldberg
 **/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "ms-container.h"
#include "ms-escher.h"
#include "ms-obj.h"

#include <expr-name.h>
#include <str.h>
#include <value.h>

void
ms_container_init (MSContainer *container, MSContainerClass const *vtbl,
		   MSContainer *parent_container,
		   ExcelWorkbook *ewb, MsBiffVersion ver)
{
	container->vtbl = vtbl;
	container->ver = ver;
	container->ewb = ewb;
	container->free_blips = TRUE;
	container->blips = NULL;
	container->obj_queue  = NULL;
	container->parent_container = parent_container;

	container->names = NULL;
	container->v7.externsheet = NULL;
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
		GList *l;
		for (l = container->obj_queue; l != NULL; l = l->next)
			ms_obj_delete (l->data);

		g_list_free (container->obj_queue);
		container->obj_queue = NULL;
	}

	if (container->v7.externsheet != NULL) {
		g_ptr_array_free (container->v7.externsheet, TRUE);
		container->v7.externsheet = NULL;
	}
	if (container->names != NULL) {
		for (i = container->names->len; i-- > 0 ; )
			if (g_ptr_array_index (container->names, i) != NULL) {
				GnmNamedExpr *nexpr = g_ptr_array_index (container->names, i);
				if (nexpr != NULL) {
					/* NAME placeholders need removal, EXTERNNAME placeholders
					 * will no be active */
					if (nexpr->active && nexpr->is_placeholder && nexpr->ref_count == 2)
						expr_name_remove (nexpr);
					expr_name_unref (nexpr);
				}
			}
		g_ptr_array_free (container->names, TRUE);
		container->names = NULL;
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

	if (container->parent_container != NULL &&
	    (container->blips == NULL || container->blips->len == 0))
		    return ms_container_get_blip (container->parent_container,
						  blip_id);

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
	container->obj_queue = g_list_prepend (container->obj_queue, obj);
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
	GList *l;

	g_return_if_fail (container != NULL);
	g_return_if_fail (container->vtbl != NULL);
	g_return_if_fail (container->vtbl->realize_obj != NULL);

	for (l = container->obj_queue; l != NULL; l = l->next)
		(void) (*container->vtbl->realize_obj) (container, l->data);
}

/**
 * ms_container_parse_expr:
 *
 * @c : The container
 * @data : the encoded expression
 * @length : the size of the encoded expression
 *
 * Attempts to parse the encoded expression in the context of the container.
 */
GnmExpr const *
ms_container_parse_expr (MSContainer *c, guint8 const *data, int length)
{
	g_return_val_if_fail (c != NULL, NULL);
	g_return_val_if_fail (c->vtbl != NULL, NULL);
	g_return_val_if_fail (c->vtbl->parse_expr != NULL, NULL);
	return (*c->vtbl->parse_expr) (c, data, length);
}

/**
 * ms_container_sheet:
 *
 * @c : The container
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

StyleFormat *
ms_container_get_fmt (MSContainer const *c, guint16 indx)
{
	g_return_val_if_fail (c != NULL, NULL);
	g_return_val_if_fail (c->vtbl != NULL, NULL);
	g_return_val_if_fail (c->vtbl->get_fmt != NULL, NULL);
	return (*c->vtbl->get_fmt) (c, indx);
}

