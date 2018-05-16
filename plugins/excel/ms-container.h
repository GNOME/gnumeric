#ifndef GNM_EXCEL_CONTAINER_H
#define GNM_EXCEL_CONTAINER_H

/**
 * ms-container.h: A meta container to handle object import for charts,
 *		workbooks and sheets.
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 2000-2005 Jody Goldberg
 **/

#include "excel.h"
#include "ms-biff.h"
#include <glib.h>
#include <pango/pango-attributes.h>

typedef struct _MSContainer	MSContainer;
typedef struct _GnmXLImporter	GnmXLImporter;
typedef struct _MSEscherBlip	MSEscherBlip;
typedef struct _MSObj		MSObj;

typedef struct {
	gboolean        (*realize_obj)	(MSContainer *c, MSObj *obj);
	SheetObject   * (*create_obj)	(MSContainer *c, MSObj *obj);
	GnmExprTop const* (*parse_expr) (MSContainer *c,
					 guint8 const *expr, int length);
	Sheet	      * (*sheet)	(MSContainer const *c);
	GOFormat     * (*get_fmt)	(MSContainer const *c, unsigned indx);
	PangoAttrList * (*get_markup)	(MSContainer const *c, unsigned indx);
} MSContainerClass;

struct _MSContainer {
	MSContainerClass const *vtbl;

	GnmXLImporter	*importer;
	gboolean	 free_blips;
	GPtrArray	*blips;
	GSList		*obj_queue;

	struct {
		GPtrArray	*externsheets;
		GPtrArray	*externnames;
	} v7;	/* biff7 does this at the container level */

	/* This is the container containing this container */
	MSContainer	*parent;
};

void ms_container_init (MSContainer *container, MSContainerClass const *vtbl,
			MSContainer *parent, GnmXLImporter *importer);
void ms_container_finalize (MSContainer *container);

void           ms_container_add_blip	 (MSContainer *c, MSEscherBlip *blip);
MSEscherBlip  *ms_container_get_blip	 (MSContainer *c, int blip_id);
void	       ms_container_set_blips    (MSContainer *c, GPtrArray *blips);
void	       ms_container_add_obj	 (MSContainer *c, MSObj *obj);
MSObj	      *ms_container_get_obj	 (MSContainer *c, int obj_id);
void	       ms_container_realize_objs (MSContainer *c);
GnmExprTop const *ms_container_parse_expr (MSContainer *c,
					  guint8 const *data, int length);

Sheet		*ms_container_sheet	  (MSContainer const *c);
GOFormat	*ms_container_get_fmt	  (MSContainer const *c, unsigned indx);
PangoAttrList	*ms_container_get_markup  (MSContainer const *c, unsigned indx);
PangoAttrList	*ms_container_read_markup (MSContainer const *c,
					   guint8 const *data, size_t txo_len,
					   char const *str);

#endif /* GNM_EXCEL_CONTAINER_H */
