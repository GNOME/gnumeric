#ifndef GNUMERIC_FILE_PRIV_H
#define GNUMERIC_FILE_PRIV_H

/*
 * GnumFileOpener
 */
 
#define GNUM_FILE_OPENER_METHOD(obj,name) \
        ((GNUM_FILE_OPENER_CLASS (GTK_OBJECT (obj)->klass))->name)

struct _GnumFileOpenerClass {
	GtkObjectClass parent_class;

	gboolean  (*probe) (GnumFileOpener const *fo,
	                    const gchar *file_name);
	void      (*open)  (GnumFileOpener const *fo,
	                    IOContext *io_context,
	                    WorkbookView *wbv,
	                    const gchar *file_name);
};

struct _GnumFileOpener {
	GtkObject parent;

	gchar                  *id;
	gchar                  *description;
	GnumFileOpenerProbeFunc probe_func;
	GnumFileOpenerOpenFunc  open_func;
};

/*
 * GnumFileSaver
 */

#define GNUM_FILE_SAVER_METHOD(obj,name) \
        ((GNUM_FILE_SAVER_CLASS (GTK_OBJECT (obj)->klass))->name)

struct _GnumFileSaverClass {
	GtkObjectClass parent_class;

	void  (*save)  (GnumFileSaver const *fs,
	                IOContext *io_context,
	                WorkbookView *wbv,
	                const gchar *file_name);
};

struct _GnumFileSaver {
	GtkObject parent;

	gchar                *id;
	gchar                *extension;
	gchar                *description;
	FileFormatLevel       format_level;
	GnumFileSaverSaveFunc save_func;
};

#endif /* GNUMERIC_FILE_PRIV_H */
