#ifndef GNUMERIC_XML_SAX_H
#define GNUMERIC_XML_SAX_H

#include <gnumeric.h>
#include <gsf/gsf.h>
#include <gsf/gsf-output-memory.h>

void	gnm_xml_file_open	(GOFileOpener const *fo, IOContext *io_context,
				 WorkbookView *wb_view, GsfInput *input);

void	gnm_xml_file_save	(GOFileSaver const *fs, IOContext *io_context,
				 WorkbookView const *wb_view, GsfOutput *output);
GsfOutputMemory *
	gnm_cellregion_to_xml	(GnmCellRegion const *cr);

#endif /* GNUMERIC_XML_SAX_H */
