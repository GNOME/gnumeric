#ifndef MS_OFFICE_CONTAINER_H
#define MS_OFFICE_CONTAINER_H

#include "excel.h"

typedef struct _MSContainer MSContainer;
typedef struct _MSEscherBlip MSEscherBlip;
typedef struct _MSObj MSObj;

typedef struct
{
	gboolean (*realize_obj) (MSObj *obj, MSContainer *container);
} MSContainerClass;

struct _MSContainer
{
	MSContainerClass const *vtbl;

	MsBiffVersion	 ver;
	GPtrArray	*blips;
	GList		*obj_queue;
};

void ms_container_init (MSContainer *container,
			MSContainerClass const *vtbl);
void ms_container_finalize (MSContainer *container);

void ms_container_add_blip     (MSContainer *container, MSEscherBlip *blip);
void ms_container_add_obj      (MSContainer *container, MSObj *obj);
void ms_container_realize_objs (MSContainer *container);

#endif /* MS_OFFICE_CONTAINER_H */
