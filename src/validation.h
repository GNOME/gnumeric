#ifndef GNUMERIC_VALIDATION_H
#define GNUMERIC_VALIDATION_H

#include "gnumeric.h"
#include "style-condition.h"
#include "str.h"

typedef enum {
	VALIDATION_STYLE_NONE,
	VALIDATION_STYLE_STOP,
	VALIDATION_STYLE_WARNING,
	VALIDATION_STYLE_INFO
} ValidationStyle;

struct _Validation {
	int              ref_count;
	
	ValidationStyle  vs;
	String          *title;
	String          *msg;
	
	StyleCondition  *sc;
};

Validation *validation_new   (ValidationStyle vs, char const *title,
			      char const *msg, StyleCondition *sc);
void        validation_ref    (Validation *v);
void        validation_unref  (Validation *v);
void        validation_link   (Validation *v, Sheet *sheet);
void        validation_unlink (Validation *v);

#endif /* GNUMERIC_VALIDATION_H */
