/* vim: set sw=8: */

/**
 * ms-container.c: A meta container to handle object import for charts,
 * 		workbooks and sheets.
 *
 * Author:
 *    Jody Goldberg (jgoldberg@home.com)
 *
 * (C) 2000 Jody Goldberg
 **/

#include "config.h"
#include "ms-container.h"
#include "ms-escher.h"
#include "ms-obj.h"

void
ms_container_init (MSContainer *container,
		   MSContainerClass const *vtbl)
{
	container->vtbl = vtbl;
	container->blips = NULL;
	container->obj_queue  = NULL;
}

void
ms_container_finalize (MSContainer *container)
{
	int i;

	g_return_if_fail (container != NULL);

	if (container->blips != NULL) {
		for (i = container->blips->len; i-- > 0 ; )
			ms_escher_blip_destroy (g_ptr_array_index (container->blips, i));

		g_ptr_array_free (container->blips, TRUE);
		container->blips = NULL;
	}

	if (container->obj_queue != NULL) {
		GList *l;
		for (l = container->obj_queue; l; l = g_list_next (l))
			ms_destroy_OBJ (l->data);

		container->obj_queue = NULL;
	}
}

void
ms_container_add_blip (MSContainer *container, MSEscherBlip *blip)
{
	if (container->blips == NULL)
		container->blips = g_ptr_array_new ();
	g_ptr_array_add (container->blips, blip);
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

	for (l = container->obj_queue; l; l = g_list_next (l))
		(void) (*container->vtbl->realize_obj) (l->data, container);
}
