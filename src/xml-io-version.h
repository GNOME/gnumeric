#ifndef _GNM_XML_IO_VERSION_H_
# define _GNM_XML_IO_VERSION_H_

G_BEGIN_DECLS

typedef enum
{
	GNM_XML_UNKNOWN = -1,
	GNM_XML_V1,
	GNM_XML_V2,
	GNM_XML_V3,	/* >= 0.52 */
	GNM_XML_V4,	/* >= 0.57 */
	GNM_XML_V5,	/* >= 0.58 */
	GNM_XML_V6,	/* >= 0.62 */
	GNM_XML_V7,	/* >= 0.66 */
	GNM_XML_V8,	/* >= 0.71 */
	GNM_XML_V9,	/* >= 0.73 add print scaling */
	GNM_XML_V10,	/* >= 1.0.3 remove useless Content node in cells */
	GNM_XML_V11,	/* >= 1.7.0 jump to sax exporter */
	GNM_XML_V12,	/* >= 1.7.3 Fix swapping of Value and ValueType in sax exporter */
	GNM_XML_V13,	/* >= 1.7.7 Deprecate ObjectAnchorType */
	GNM_XML_V14,	/* >= 1.12.21 Various */

	/* NOTE : Keep this up to date (and in sync with the schema) */
	GNM_XML_LATEST = GNM_XML_V14
} GnumericXMLVersion;

G_END_DECLS

#endif /* _GNM_XML_IO_VERSION_H_ */
