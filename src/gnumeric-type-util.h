#ifndef GNUMERIC_GNUMERIC_TYPE_UTIL_H
#define GNUMERIC_GNUMERIC_TYPE_UTIL_H

/*
 * Useful macros for reducing the ammount of boiler plate
 * to be typed every time we create a GtkObject
 */
 
#define GNUMERIC_MAKE_TYPE_WITH_PARENT(l,str,t,pt,ci,i,parent) \
GtkType l##_get_type(void)\
{\
	static GtkType type = 0;\
	if (!type){\
		GtkTypeInfo info = {\
			str,\
			sizeof (t),\
			sizeof (pt),\
			(GtkClassInitFunc) ci,\
			(GtkObjectInitFunc) i,\
			NULL, /* reserved 1 */\
			NULL, /* reserved 2 */\
			(GtkClassInitFunc) NULL\
		};\
                type = gtk_type_unique (parent, &info);\
	}\
	return type;\
}

#define GNUMERIC_MAKE_TYPE(l,str,t,ci,i,parent) \
	GNUMERIC_MAKE_TYPE_WITH_PARENT(l,str,t,t##Class,ci,i,parent)

#endif /* GNUMERIC_GNUMERIC_TYPE_UTIL_H */
