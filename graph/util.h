#ifndef GRAPH_UTIL_H
#define GRAPH_UTIL_H

#define DEFINE_TYPE(name,strname,parent,prefix) \
	GtkType prefix##_get_type (void)	\
	{					\
		static GtkType type = 0;	\
						\
		if (!type){			\
			GtkTypeInfo info = {	\
				strname,	\
				sizeof (name),	\
				sizeof (name##Class),	\
				(GtkClassInitFunc) prefix##_class_init,	\
				(GtkObjectInitFunc) prefix##_init,	\
				NULL, NULL, NULL \
			}; 	\
				\
			type = gtk_type_unique (parent##_get_type (), &info);\
		}	\
			\
		return type;\
	}
			
#endif /* GRAPH_UTIL_H */
