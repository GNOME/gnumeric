/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNUMERIC_VECTOR_H_
#define GNUMERIC_VECTOR_H_

#include <libgnome/gnome-defs.h>
#include <bonobo/gnome-object.h>
#include "src/Gnumeric.h"

BEGIN_GNOME_DECLS

#define VECTOR_TYPE        (vector_get_type ())
#define VECTOR(o)          (GTK_CHECK_CAST ((o), VECTOR_TYPE, Vector))
#define VECTOR_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), VECTOR_TYPE, VectorClass))
#define IS_VECTOR(o)       (GTK_CHECK_TYPE ((o), VECTOR_TYPE))
#define IS_VECTOR_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), VECTOR_TYPE))

typedef GNOME_Gnumeric_DoubleVec   *(*VectorGetNumFn)(CORBA_short low, CORBA_short high, void *data);
typedef GNOME_Gnumeric_VecValueVec *(*VectorGetValFn)(CORBA_short low, CORBA_short high, void *data);
typedef void (*VectorSetFn) (CORBA_short pos, double val, CORBA_Environment *ev, void *data);
typedef CORBA_short (*VectorLenFn) (void *data);
typedef CORBA_boolean (*VectorTypeFn)(void *data);

typedef struct {
	GnomeObject base;

	VectorTypeFn    type;
	VectorGetNumFn  get_numbers;
	VectorGetValFn  get_values;
	VectorSetFn     set;
	VectorLenFn     len;

	GNOME_Gnumeric_VectorNotify notify;
	
	void *user_data;
} Vector;

typedef struct {
	GnomeObjectClass parent_class;
} VectorClass;

GtkType      vector_get_type      (void);
Vector      *vector_new           (VectorGetNumFn get, VectorGetValFn,
				   VectorSetFn set, VectorLenFn len,
				   GNOME_Gnumeric_VectorNotify notify,
				   void *data);
void         vector_changed       (Vector *vector, int low, int high);

END_GNOME_DECLS

#endif /* GRAH_VECTOR_H_ */
