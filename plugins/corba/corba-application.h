#ifndef GNUMERIC_CORBA_APPLICATION_H
#define GNUMERIC_CORBA_APPLICATION_H

#define CORBA_TYPE_APPLICATION        (corba_application_get_type ())
#define CORBA_APPLICATION(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CORBA_TYPE_APPLICATION, CorbaApplication))
#define CORBA_APPLICATION_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), CORBA_TYPE_APPLICATION, CorbaApplicationClass))
#define CORBA_IS_APPLICATION(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), CORBA_TYPE_APPLICATION))
#define CORBA_IS_APPLICATION_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), CORBA_TYPE_APPLICATION))

struct _CorbaApplication {
	BonoboObject base;

	/* No need for any data */
};

typedef struct {
	BonoboObjectClass      parent_class;

	POA_GNOME_Gnumeric_Application__epv epv;
} CorbaApplicationClass;

GType corba_application_get_type (void);

#endif /* GNUMERIC_CORBA_APPLICATION_H */
