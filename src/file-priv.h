#ifndef GNUMERIC_FILE_PRIV_H
#define GNUMERIC_FILE_PRIV_H

/*
 * GnmFileOpener
 */

#define GNM_FILE_OPENER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_GNM_FILE_OPENER, GnmFileOpenerClass))
#define IS_GNM_FILE_OPENER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_GNM_FILE_OPENER))

#define GNM_FILE_OPENER_METHOD(obj,name) \
        ((GNM_FILE_OPENER_CLASS (G_OBJECT_GET_CLASS (obj)))->name)

struct _GnmFileOpenerClass {
	GObjectClass parent_class;

	gboolean  (*probe) (GnmFileOpener const *fo,
	                    GsfInput *input,
	                    FileProbeLevel pl);
	void      (*open)  (GnmFileOpener const *fo,
			    gchar const *opt_enc,
	                    IOContext *io_context,
	                    WorkbookView *wbv,
	                    GsfInput *input);
};

struct _GnmFileOpener {
	GObject parent;

	gchar                  *id;
	gchar                  *description;
	gboolean               encoding_dependent;
	GnmFileOpenerProbeFunc probe_func;
	GnmFileOpenerOpenFunc  open_func;
};

void gnm_file_opener_setup (GnmFileOpener *fo, const gchar *id,
			    const gchar *description,
			    gboolean encoding_dependent,
			    GnmFileOpenerProbeFunc probe_func,
			    GnmFileOpenerOpenFunc open_func);

/*
 * GnmFileSaver
 */

#define GNM_FILE_SAVER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_GNM_FILE_SAVER, GnmFileSaverClass))
#define IS_GNM_FILE_SAVER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_GNM_FILE_SAVER))

#define GNM_FILE_SAVER_METHOD(obj,name) \
        ((GNM_FILE_SAVER_CLASS (G_OBJECT_GET_CLASS (obj)))->name)

struct _GnmFileSaverClass {
	GObjectClass parent_class;

	void (*save) (GnmFileSaver const *fs,
	              IOContext *io_context,
	              WorkbookView const *wbv,
	              GsfOutput *output);
};

struct _GnmFileSaver {
	GObject parent;

	gchar                *id;
	const gchar          *mime_type;
	gchar                *extension;
	gchar                *description;
	gboolean              overwrite_files;
	FileFormatLevel               format_level;
	FileSaveScope                 save_scope;
	GnmFileSaverSaveFunc         save_func;
};

void gnm_file_saver_setup (GnmFileSaver *fs,
                            const gchar *id,
                            const gchar *extension,
                            const gchar *description,
                            FileFormatLevel level,
                            GnmFileSaverSaveFunc save_func);

#endif /* GNUMERIC_FILE_PRIV_H */
