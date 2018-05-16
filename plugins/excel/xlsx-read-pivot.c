/**
 * xlsx-read-pivot.c: MS Excel XLSX (OOX) import for pivot tables (tm)
 *
 * (C) 2007-2008 Jody Goldberg
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 **/

#include <gnm-data-cache-source.h>
#include <go-data-cache.h>
#include <go-data-cache-field.h>
#include <go-data-slicer.h>
#include <gnm-sheet-slicer.h>
#include <go-data-slicer-field.h>

/*
 *
 * DO * NOT * COMPILE * DIRECTLY *
 * DO * NOT * COMPILE * DIRECTLY *
 * DO * NOT * COMPILE * DIRECTLY *
 *
 * included via xlsx-read.c
 **/
static void
xlsx_CT_pivotTableDefinition (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GOString *name = NULL;
	GODataCache   *cache = NULL;
	int tmp;

	gboolean dataOnRows = 0;
	gboolean showError = FALSE;
	gboolean showMissing = TRUE;
	gboolean asteriskTotals = FALSE;
	gboolean showItems = TRUE;
	gboolean editData = FALSE;
	gboolean disableFieldList = FALSE;
	gboolean showCalcMbrs = TRUE;
	gboolean visualTotals = TRUE;
	gboolean showMultipleLabel = TRUE;
	gboolean showDataDropDown = TRUE;
	gboolean showDrill = TRUE;
	gboolean printDrill = FALSE;
	gboolean showMemberPropertyTips = TRUE;
	gboolean showDataTips = TRUE;
	gboolean enableWizard = TRUE;
	gboolean enableDrill = TRUE;
	gboolean enableFieldProperties = TRUE;
	gboolean preserveFormatting = TRUE;
	gboolean useAutoFormatting = FALSE;
	unsigned int pageWrap = 0;
	gboolean pageOverThenDown = FALSE;
	gboolean subtotalHiddenItems = FALSE;
	gboolean rowGrandTotals = TRUE;
	gboolean colGrandTotals = TRUE;
	gboolean fieldPrintTitles = FALSE;
	gboolean itemPrintTitles = FALSE;
	gboolean mergeItem = FALSE;
	gboolean showDropZones = TRUE;
	unsigned int indent = 1;
	gboolean published = FALSE;
	gboolean immersive = TRUE;
	gboolean multipleFieldFilters = TRUE;
	gboolean showEmptyRow = FALSE;
	gboolean showEmptyCol = FALSE;
	gboolean showHeaders = TRUE;
	gboolean outlineData = FALSE;
	gboolean compactData = TRUE;
	gboolean compact = TRUE;
	gboolean outline = FALSE;
	gboolean gridDropZones = FALSE;

#if 0
	unsigned int chartFormat = 0;

    <xsd:attributeGroup ref="AG_AutoFormat" />

    <xsd:attribute name="fieldListSortAscending" type="xsd:boolean" default="false">
    <xsd:attribute name="mdxSubqueries" type="xsd:boolean" default="false">
    <xsd:attribute name="customListSort" type="xsd:boolean" use="optional" default="true">
    <xsd:attribute name="rowHeaderCaption" type="ST_Xstring">
    <xsd:attribute name="colHeaderCaption" type="ST_Xstring">
    <xsd:attribute name="dataPosition" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="dataCaption" use="required" type="ST_Xstring">
    <xsd:attribute name="grandTotalCaption" type="ST_Xstring">
    <xsd:attribute name="errorCaption" type="ST_Xstring">
    <xsd:attribute name="missingCaption" type="ST_Xstring">
    <xsd:attribute name="pageStyle" type="ST_Xstring">
    <xsd:attribute name="pivotTableStyle" type="ST_Xstring">
    <xsd:attribute name="vacatedStyle" type="ST_Xstring">
    <xsd:attribute name="tag" type="ST_Xstring">
    <xsd:attribute name="createdVersion" type="xsd:unsignedByte" default="0">
    <xsd:attribute name="updatedVersion" type="xsd:unsignedByte" default="0">
    <xsd:attribute name="minRefreshableVersion" type="xsd:unsignedByte" default="0">
#endif

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (0 == strcmp (attrs[0], "cacheId"))
			cache = g_hash_table_lookup (state->pivot.cache_by_id, attrs[1]);
		else if (0 == strcmp (attrs[0], "name")) name = go_string_new (attrs[1]);
		else if (attr_bool (xin, attrs, "dataOnRows", &tmp)) dataOnRows = tmp;
		else if (attr_bool (xin, attrs, "showError", &tmp)) showError = tmp;
		else if (attr_bool (xin, attrs, "showMissing", &tmp)) showMissing = tmp;
		else if (attr_bool (xin, attrs, "asteriskTotals", &tmp)) asteriskTotals = tmp;
		else if (attr_bool (xin, attrs, "showItems", &tmp)) showItems = tmp;
		else if (attr_bool (xin, attrs, "editData", &tmp)) editData = tmp;
		else if (attr_bool (xin, attrs, "disableFieldList", &tmp)) disableFieldList = tmp;
		else if (attr_bool (xin, attrs, "showCalcMbrs", &tmp)) showCalcMbrs = tmp;
		else if (attr_bool (xin, attrs, "visualTotals", &tmp)) visualTotals = tmp;
		else if (attr_bool (xin, attrs, "showMultipleLabel", &tmp)) showMultipleLabel = tmp;
		else if (attr_bool (xin, attrs, "showDataDropDown", &tmp)) showDataDropDown = tmp;
		else if (attr_bool (xin, attrs, "showDrill", &tmp)) showDrill = tmp;
		else if (attr_bool (xin, attrs, "printDrill", &tmp)) printDrill = tmp;
		else if (attr_bool (xin, attrs, "showMemberPropertyTips", &tmp)) showMemberPropertyTips = tmp;
		else if (attr_bool (xin, attrs, "showDataTips", &tmp)) showDataTips = tmp;
		else if (attr_bool (xin, attrs, "enableWizard", &tmp)) enableWizard = tmp;
		else if (attr_bool (xin, attrs, "enableDrill", &tmp)) enableDrill = tmp;
		else if (attr_bool (xin, attrs, "enableFieldProperties", &tmp)) enableFieldProperties = tmp;
		else if (attr_bool (xin, attrs, "preserveFormatting", &tmp)) preserveFormatting = tmp;
		else if (attr_bool (xin, attrs, "useAutoFormatting", &tmp)) useAutoFormatting = tmp;
		else if (attr_int (xin, attrs, "pageWrap", &tmp)) pageWrap = tmp;
		else if (attr_bool (xin, attrs, "pageOverThenDown", &tmp)) pageOverThenDown = tmp;
		else if (attr_bool (xin, attrs, "subtotalHiddenItems", &tmp)) subtotalHiddenItems = tmp;
		else if (attr_bool (xin, attrs, "rowGrandTotals", &tmp)) rowGrandTotals = tmp;
		else if (attr_bool (xin, attrs, "colGrandTotals", &tmp)) colGrandTotals = tmp;
		else if (attr_bool (xin, attrs, "fieldPrintTitles", &tmp)) fieldPrintTitles = tmp;
		else if (attr_bool (xin, attrs, "itemPrintTitles", &tmp)) itemPrintTitles = tmp;
		else if (attr_bool (xin, attrs, "mergeItem", &tmp)) mergeItem = tmp;
		else if (attr_bool (xin, attrs, "showDropZones", &tmp)) showDropZones = tmp;
		else if (attr_int (xin, attrs, "indent", &tmp)) indent = tmp;
		else if (attr_bool (xin, attrs, "published", &tmp)) published = tmp;
		else if (attr_bool (xin, attrs, "immersive", &tmp)) immersive = tmp;
		else if (attr_bool (xin, attrs, "multipleFieldFilters", &tmp)) multipleFieldFilters = tmp;
		else if (attr_bool (xin, attrs, "showEmptyRow", &tmp)) showEmptyRow = tmp;
		else if (attr_bool (xin, attrs, "showEmptyCol", &tmp)) showEmptyCol = tmp;
		else if (attr_bool (xin, attrs, "showHeaders", &tmp)) showHeaders = tmp;
		else if (attr_bool (xin, attrs, "outlineData", &tmp)) outlineData = tmp;
		else if (attr_bool (xin, attrs, "compactData", &tmp)) compactData = tmp;
		else if (attr_bool (xin, attrs, "compact", &tmp)) compact = tmp;
		else if (attr_bool (xin, attrs, "outline", &tmp)) outline = tmp;
		else if (attr_bool (xin, attrs, "gridDropZones", &tmp)) gridDropZones = tmp;
	}

	(void)indent;
	(void)pageWrap;
	(void)dataOnRows;
	(void)showError;
	(void)showMissing;
	(void)asteriskTotals;
	(void)showItems;
	(void)editData;
	(void)disableFieldList;
	(void)showCalcMbrs;
	(void)visualTotals;
	(void)showMultipleLabel;
	(void)showDataDropDown;
	(void)showDrill;
	(void)printDrill;
	(void)showMemberPropertyTips;
	(void)showDataTips;
	(void)enableWizard;
	(void)enableDrill;
	(void)enableFieldProperties;
	(void)preserveFormatting;
	(void)useAutoFormatting;
	(void)pageOverThenDown;
	(void)subtotalHiddenItems;
	(void)rowGrandTotals;
	(void)colGrandTotals;
	(void)fieldPrintTitles;
	(void)itemPrintTitles;
	(void)mergeItem;
	(void)showDropZones;
	(void)published;
	(void)immersive;
	(void)multipleFieldFilters;
	(void)showEmptyRow;
	(void)showEmptyCol;
	(void)showHeaders;
	(void)outlineData;
	(void)compactData;
	(void)compact;
	(void)outline;
	(void)gridDropZones;

	state->pivot.field_count = 0;
	state->pivot.slicer = g_object_new (GNM_SHEET_SLICER_TYPE,
		"name",		name,
		"cache",	cache,
		NULL);
	go_string_unref (name);
}

static void
xlsx_CT_pivotTableDefinition_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (NULL != state->pivot.slicer) {
		gnm_sheet_slicer_set_sheet (state->pivot.slicer, state->sheet);
		g_object_unref (state->pivot.slicer);
		state->pivot.slicer = NULL;
	}
}

static void
xlsx_CT_Location (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmRange range;
	int tmp;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_range (xin, attrs, "ref", &range))
			gnm_sheet_slicer_set_range (state->pivot.slicer, &range);
		else if (attr_int (xin, attrs, "firstHeaderRow", &tmp)) g_object_set (state->pivot.slicer, "first-header-row", tmp, NULL);
		else if (attr_int (xin, attrs, "firstDataRow", &tmp))   g_object_set (state->pivot.slicer, "first-data-row", tmp, NULL);
		else if (attr_int (xin, attrs, "firstDataCol", &tmp))   g_object_set (state->pivot.slicer, "first-data-col", tmp, NULL);
		else if (attr_int (xin, attrs, "rowPageCount", &tmp))   g_object_set (state->pivot.slicer, "row-page-count", tmp, NULL);
		else if (attr_int (xin, attrs, "colPageCount", &tmp))   g_object_set (state->pivot.slicer, "col-page-count", tmp, NULL);
}

static void
xlsx_CT_Members (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_Member (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_Field (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int indx = -1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, "x", &indx))
			/* Nothing */ ;

	if (indx >= 0)
		go_data_slicer_field_set_field_type_pos (
			go_data_slicer_get_field ((GODataSlicer *)state->pivot.slicer, indx),
			xin->node->user_data.v_int, G_MAXINT);
}

static void
xlsx_CT_DataField (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const aggregations[] = {
		{ "min",		(1 << GO_AGGREGATE_BY_MIN) },
		{ "max",		(1 << GO_AGGREGATE_BY_MAX) },
		{ "sum",		(1 << GO_AGGREGATE_BY_SUM) },
		{ "product",		(1 << GO_AGGREGATE_BY_PRODUCT) },

		/* TODO : Check this */
		{ "count",		(1 << GO_AGGREGATE_BY_COUNTA) },
		{ "countNums",		(1 << GO_AGGREGATE_BY_COUNT) },

		{ "average",		(1 << GO_AGGREGATE_BY_AVERAGE) },
		{ "stdDev",		(1 << GO_AGGREGATE_BY_STDDEV) },
		{ "stdDevP",		(1 << GO_AGGREGATE_BY_STDDEVP) },
		{ "var",		(1 << GO_AGGREGATE_BY_VAR) },
		{ "varP",		(1 << GO_AGGREGATE_BY_VARP) },
		{ NULL,			0},
	};

	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int indx = -1;
	int aggregate_by = (1 << GO_AGGREGATE_BY_SUM);	/* default */

	/* Why "fld" in data vs "x" in col/row ? */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, "fld", &indx)) ;
		else if (attr_enum (xin, attrs, "subtotal", aggregations, &aggregate_by))
			/* Nothing */ ;

	if (indx >= 0) {
		GODataSlicerField *dsf = go_data_slicer_get_field ((GODataSlicer *)state->pivot.slicer, indx);
		go_data_slicer_field_set_field_type_pos (dsf, GDS_FIELD_TYPE_DATA, G_MAXINT);
		g_object_set (G_OBJECT (dsf), "aggregations", aggregate_by, NULL);
	}
}

static void
xlsx_CT_PivotField (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const ST_Axis_types[] = {
		{ "axisPage",	GDS_FIELD_TYPE_PAGE },
		{ "axisRow",	GDS_FIELD_TYPE_ROW },
		{ "axisCol",	GDS_FIELD_TYPE_COL },
		{ "axisValues",	GDS_FIELD_TYPE_DATA },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean defaultAttributeDrillState = FALSE;
	gboolean showPropAsCaption = FALSE;
	gboolean showPropTip = FALSE;
	gboolean showPropCell = FALSE;

	unsigned int aggregations = 0;
	gboolean defaultSubtotal = TRUE;
	gboolean nonAutoSortDefault = FALSE;
	gboolean dataSourceSort = FALSE;
	gboolean includeNewItemsInFilter = FALSE;
	gboolean measureFilter = FALSE;
	gboolean hideNewItems = FALSE;
	gboolean topAutoShow = TRUE;
	gboolean autoShow = FALSE;
	gboolean insertPageBreak = FALSE;
	gboolean serverField = FALSE;
	gboolean insertBlankRow = FALSE;
	gboolean showAll = TRUE;
	gboolean dragOff = TRUE;
	gboolean dragToData = TRUE;
	gboolean dragToPage = TRUE;
	gboolean multipleItemSelectionAllowed = FALSE;
	gboolean dragToCol = TRUE;
	gboolean dragToRow = TRUE;
	gboolean subtotalTop = TRUE;
	gboolean outline = TRUE;
	gboolean allDrilled = FALSE;
	gboolean compact = TRUE;
	gboolean hiddenLevel = FALSE;
	gboolean showDropDowns = TRUE;
	GOString *name = NULL;
	int tmp;

	state->pivot.slicer_field = g_object_new (GO_DATA_SLICER_FIELD_TYPE,
		"data-cache-field-index", state->pivot.field_count++,
		NULL);
	go_data_slicer_add_field (GO_DATA_SLICER (state->pivot.slicer),
		state->pivot.slicer_field);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (0 == strcmp (attrs[0], "name"))	name = go_string_new (attrs[1]);
		else if (attr_enum (xin, attrs, "axis", ST_Axis_types, &tmp))
			go_data_slicer_field_set_field_type_pos (state->pivot.slicer_field, tmp, G_MAXINT);
		else if (attr_bool (xin, attrs, "dataField", &tmp) && tmp)
			go_data_slicer_field_set_field_type_pos (state->pivot.slicer_field, GDS_FIELD_TYPE_DATA, G_MAXINT);
		else if (attr_bool (xin, attrs, "showDropDowns", &tmp))	showDropDowns = tmp;
		else if (attr_bool (xin, attrs, "hiddenLevel", &tmp))	hiddenLevel = tmp;
		else if (attr_bool (xin, attrs, "compact", &tmp))	compact = tmp;
		else if (attr_bool (xin, attrs, "allDrilled", &tmp))	allDrilled = tmp;
		else if (attr_bool (xin, attrs, "outline", &tmp))	outline = tmp;
		else if (attr_bool (xin, attrs, "subtotalTop", &tmp))	subtotalTop = tmp;
		else if (attr_bool (xin, attrs, "dragToRow", &tmp))	dragToRow = tmp;
		else if (attr_bool (xin, attrs, "dragToCol", &tmp))	dragToCol = tmp;
		else if (attr_bool (xin, attrs, "multipleItemSelectionAllowed", &tmp))	multipleItemSelectionAllowed = tmp;
		else if (attr_bool (xin, attrs, "dragToPage", &tmp))	dragToPage = tmp;
		else if (attr_bool (xin, attrs, "dragToData", &tmp))	dragToData = tmp;
		else if (attr_bool (xin, attrs, "dragOff", &tmp))	dragOff = tmp;
		else if (attr_bool (xin, attrs, "showAll", &tmp))	showAll = tmp;
		else if (attr_bool (xin, attrs, "insertBlankRow", &tmp))	insertBlankRow = tmp;
		else if (attr_bool (xin, attrs, "serverField", &tmp))	serverField = tmp;
		else if (attr_bool (xin, attrs, "insertPageBreak", &tmp))	insertPageBreak = tmp;
		else if (attr_bool (xin, attrs, "autoShow", &tmp))	autoShow = tmp;
		else if (attr_bool (xin, attrs, "topAutoShow", &tmp))	topAutoShow = tmp;
		else if (attr_bool (xin, attrs, "hideNewItems", &tmp))	hideNewItems = tmp;
		else if (attr_bool (xin, attrs, "measureFilter", &tmp))	measureFilter = tmp;
		else if (attr_bool (xin, attrs, "includeNewItemsInFilter", &tmp))	includeNewItemsInFilter = tmp;
		else if (attr_bool (xin, attrs, "dataSourceSort", &tmp))	dataSourceSort = tmp;
		else if (attr_bool (xin, attrs, "nonAutoSortDefault", &tmp))	nonAutoSortDefault = tmp;
		else if (attr_bool (xin, attrs, "defaultSubtotal", &tmp))	defaultSubtotal = tmp;

		else if (attr_bool (xin, attrs, "minSubtotal", &tmp) && tmp)	aggregations |= (1 << GO_AGGREGATE_BY_MIN);
		else if (attr_bool (xin, attrs, "maxSubtotal", &tmp) && tmp)	aggregations |= (1 << GO_AGGREGATE_BY_MAX);
		else if (attr_bool (xin, attrs, "sumSubtotal", &tmp) && tmp)	aggregations |= (1 << GO_AGGREGATE_BY_SUM);
		else if (attr_bool (xin, attrs, "productSubtotal", &tmp) && tmp)aggregations |= (1 << GO_AGGREGATE_BY_PRODUCT);
		else if (attr_bool (xin, attrs, "countSubtotal", &tmp) && tmp)	aggregations |= (1 << GO_AGGREGATE_BY_COUNT);
		else if (attr_bool (xin, attrs, "countASubtotal", &tmp) && tmp)	aggregations |= (1 << GO_AGGREGATE_BY_COUNTA);
		else if (attr_bool (xin, attrs, "avgSubtotal", &tmp) && tmp)	aggregations |= (1 << GO_AGGREGATE_BY_AVERAGE);
		else if (attr_bool (xin, attrs, "stdDevSubtotal", &tmp) && tmp)	aggregations |= (1 << GO_AGGREGATE_BY_STDDEV);
		else if (attr_bool (xin, attrs, "stdDevPSubtotal", &tmp) && tmp)	aggregations |= (1 << GO_AGGREGATE_BY_STDDEVP);
		else if (attr_bool (xin, attrs, "varSubtotal", &tmp) && tmp)	aggregations |= (1 << GO_AGGREGATE_BY_VAR);
		else if (attr_bool (xin, attrs, "varPSubtotal", &tmp) && tmp)	aggregations |= (1 << GO_AGGREGATE_BY_VARP);

		else if (attr_bool (xin, attrs, "showPropCell", &tmp))	showPropCell = tmp;
		else if (attr_bool (xin, attrs, "showPropTip", &tmp))	showPropTip = tmp;
		else if (attr_bool (xin, attrs, "showPropAsCaption", &tmp))	showPropAsCaption = tmp;
		else if (attr_bool (xin, attrs, "defaultAttributeDrillState", &tmp))	defaultAttributeDrillState = tmp;
	}

	(void)defaultAttributeDrillState;
	(void)showPropAsCaption;
	(void)showPropTip;
	(void)showPropCell;
	(void)defaultSubtotal;
	(void)nonAutoSortDefault;
	(void)dataSourceSort;
	(void)includeNewItemsInFilter;
	(void)measureFilter;
	(void)hideNewItems;
	(void)topAutoShow;
	(void)autoShow;
	(void)insertPageBreak;
	(void)serverField;
	(void)insertBlankRow;
	(void)showAll;
	(void)dragOff;
	(void)dragToData;
	(void)dragToPage;
	(void)multipleItemSelectionAllowed;
	(void)dragToCol;
	(void)dragToRow;
	(void)subtotalTop;
	(void)outline;
	(void)allDrilled;
	(void)compact;
	(void)hiddenLevel;
	(void)showDropDowns;

#if 0
	"rankBy" type="xsd:unsignedInt"
	"numFmtId" type="ST_NumFmtId"
	"itemPageCount" type="xsd:unsignedInt" default="10">
	"sortType" type="ST_FieldSortType" default="manual">
	"subtotalCaption" type="ST_Xstring">
	"uniqueMemberProperty" type="ST_Xstring">
#endif

	g_object_set (G_OBJECT (state->pivot.slicer_field),
		      "name",	name,
		      "aggregations",	    aggregations,
		      NULL);
	go_string_unref (name);
}

static void
xlsx_CT_PivotField_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->pivot.slicer_field = NULL;
}

static void
xlsx_CT_PageField (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="fld" use="required" type="xsd:int">
    <xsd:attribute name="item" use="optional" type="xsd:unsignedInt">
    <xsd:attribute name="hier" type="xsd:int">
    <xsd:attribute name="name" type="ST_Xstring">
    <xsd:attribute name="cap" type="ST_Xstring">
#endif
}
static void
xlsx_CT_Items (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="count" type="xsd:unsignedInt">
#endif
}

static void
xlsx_CT_Item (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
    <xsd:attribute name="n" type="ST_Xstring">
    <xsd:attribute name="t" type="ST_ItemType" default="data">
    <xsd:attribute name="h" type="xsd:boolean" default="false">
    <xsd:attribute name="s" type="xsd:boolean" default="false">
    <xsd:attribute name="sd" type="xsd:boolean" default="true">
    <xsd:attribute name="f" type="xsd:boolean" default="false">
    <xsd:attribute name="m" type="xsd:boolean" default="false">
    <xsd:attribute name="c" type="xsd:boolean" default="false">
    <xsd:attribute name="x" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="d" type="xsd:boolean" default="false">
    <xsd:attribute name="e" type="xsd:boolean" default="true">
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	guint    number;
	gboolean hidden;
	guint	 type = GO_DATA;
	static EnumVal const item_types[] = {
		{ "default",		GO_DEFAULT },
		{ "sum",		GDS_FIELD_AGGREGATE_SUM },
		{ "countA",		GDS_FIELD_AGGREGATE_COUNTA },
		{ "avg",		GDS_FIELD_AGGREGATE_AVG },
		{ "max",		GDS_FIELD_AGGREGATE_MAX },
		{ "min",		GDS_FIELD_AGGREGATE_MIN },
		{ "product",		GDS_FIELD_AGGREGATE_PRODUCT },
		{ "count",		GDS_FIELD_AGGREGATE_COUNT },
		{ "stdDev",		GDS_FIELD_AGGREGATE_STDDEV },
		{ "stdDevP",		GDS_FIELD_AGGREGATE_STDDEVP },
		{ "var",		GDS_FIELD_AGGREGATE_VAR },
		{ "varP",		GDS_FIELD_AGGREGATE_VARP },
		{ "grand",		GO_GRAND },
		{ "blank",		GO_BLANK },
		{ "data",		GO_DATA },
		{ NULL,			0},
	};

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, "t", item_types, &type)) ;
		else if (attr_int (xin, attrs, "x", &number)) ;
		else if (attr_bool(xin,attrs,"h",&hidden)) ;
#endif
}

static void
xlsx_CT_X (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_AutoSortScope (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_GroupMembers (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_GroupMember (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_Group (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_Groups (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_ColItems (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
    <xsd:attribute name="count" type="xsd:unsignedInt">
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_RowItems (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
    <xsd:attribute name="count" type="xsd:unsignedInt">
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_Format (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}
static void
xlsx_CT_Formats (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_ConditionalFormat (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_ConditionalFormats (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_ChartFormat (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}
static void
xlsx_CT_ChartFormats (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_PivotTableStyle (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean showColHeaders = TRUE;
	gboolean showRowHeaders = TRUE;
	gboolean showColStripes = TRUE;
	gboolean showRowStripes = TRUE;
	gboolean showLastColumn = TRUE;
	gboolean showLastRow = TRUE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_bool (xin, attrs, "showColHeaders", &showColHeaders))
			;
		else if (attr_bool (xin, attrs, "showRowHeaders", &showRowHeaders))
			;
		else if (attr_bool (xin, attrs, "showColStripes", &showColStripes))
			;
		else if (attr_bool (xin, attrs, "showRowStripes", &showRowStripes))
			;
		else if (attr_bool (xin, attrs, "showLastColumn", &showLastColumn))
			;
		else if (attr_bool (xin, attrs, "showLastRow", &showLastRow))
			;
	}

	g_object_set (G_OBJECT (state->pivot.slicer),
		"show-headers-col", showColHeaders,
		"show-headers-row", showRowHeaders,
		"show-stripes-col", showColStripes,
		"show-stripes-row", showRowStripes,
		"show-last-col", showLastColumn,
		"show-last-row", showLastRow,
		NULL);
}

static GsfXMLInNode const xlsx_pivot_table_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, PT_CACHE_DEF, XL_NS_SS, "pivotTableDefinition", GSF_XML_NO_CONTENT, FALSE, TRUE,
		      &xlsx_CT_pivotTableDefinition, &xlsx_CT_pivotTableDefinition_end, 0),

  GSF_XML_IN_NODE (PT_CACHE_DEF, LOCATION, XL_NS_SS, "location", GSF_XML_NO_CONTENT, &xlsx_CT_Location, NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, PT_FIELDS, XL_NS_SS, "pivotFields", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PT_FIELDS, PT_FIELD, XL_NS_SS, "pivotField", GSF_XML_NO_CONTENT,
		     &xlsx_CT_PivotField, &xlsx_CT_PivotField_end),
      GSF_XML_IN_NODE (PT_FIELD, PT_FIELD_ITEMS, XL_NS_SS, "items", GSF_XML_NO_CONTENT, &xlsx_CT_Items, NULL),
        GSF_XML_IN_NODE (PT_FIELD_ITEMS, PT_FIELD_ITEM, XL_NS_SS, "item", GSF_XML_NO_CONTENT, &xlsx_CT_Item, NULL),
      GSF_XML_IN_NODE (PT_FIELD, PT_FIELD_SORT_SCOPE, XL_NS_SS, "autoSortScope", GSF_XML_NO_CONTENT,
		       &xlsx_CT_AutoSortScope, NULL),
      GSF_XML_IN_NODE (PT_FIELD, EXTLIST, XL_NS_SS, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, PAGE_FIELDS, XL_NS_SS, "pageFields", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PAGE_FIELDS,   PAGE_FIELD, XL_NS_SS, "pageField", GSF_XML_NO_CONTENT, &xlsx_CT_PageField, NULL),
      GSF_XML_IN_NODE (PAGE_FIELD, EXTLIST, XL_NS_SS, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, COL_FIELDS, XL_NS_SS, "colFields", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE_FULL (COL_FIELDS,    COL_FIELD, XL_NS_SS, "field", GSF_XML_NO_CONTENT, FALSE, FALSE,
			  &xlsx_CT_Field, NULL, GDS_FIELD_TYPE_COL),
  GSF_XML_IN_NODE (PT_CACHE_DEF, ROW_FIELDS, XL_NS_SS, "rowFields", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE_FULL (ROW_FIELDS,    ROW_FIELD, XL_NS_SS, "field", GSF_XML_NO_CONTENT, FALSE, FALSE,
			  &xlsx_CT_Field, NULL, GDS_FIELD_TYPE_ROW),
  GSF_XML_IN_NODE (PT_CACHE_DEF, DATA_FIELDS, XL_NS_SS, "dataFields", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DATA_FIELDS,   DATA_FIELD, XL_NS_SS, "dataField", GSF_XML_NO_CONTENT, &xlsx_CT_DataField, NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, ROW_ITEMS, XL_NS_SS, "rowItems", GSF_XML_NO_CONTENT, &xlsx_CT_RowItems, NULL),
    GSF_XML_IN_NODE (ROW_ITEMS, ITEM, XL_NS_SS, "i", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (ITEM, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_NO_CONTENT, &xlsx_CT_X, NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, COL_ITEMS, XL_NS_SS, "colItems", GSF_XML_NO_CONTENT, &xlsx_CT_ColItems, NULL),
    GSF_XML_IN_NODE (COL_ITEMS, ITEM, XL_NS_SS, "i", GSF_XML_2ND, NULL, NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, PC_FORMATS, XL_NS_SS, "formats", GSF_XML_NO_CONTENT, &xlsx_CT_Formats, NULL),
    GSF_XML_IN_NODE (PC_FORMATS, PC_FORMAT, XL_NS_SS, "format", GSF_XML_NO_CONTENT, &xlsx_CT_Format, NULL),
      GSF_XML_IN_NODE (PC_FORMAT, EXTLST, XL_NS_SS, "extLst", GSF_XML_NO_CONTENT, NULL,	NULL),
      GSF_XML_IN_NODE (PC_FORMAT, PIVOT_AREA, XL_NS_SS, "pivotArea", GSF_XML_2ND, NULL, NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, PC_CONDITIONALFORMATS, XL_NS_SS, "conditionalFormats", GSF_XML_NO_CONTENT, &xlsx_CT_ConditionalFormats, NULL),
    GSF_XML_IN_NODE (PC_CONDITIONALFORMATS, PC_CONDITIONALFORMAT, XL_NS_SS, "conditionalFormat", GSF_XML_NO_CONTENT, &xlsx_CT_ConditionalFormat, NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, PC_CHARTFORMATS, XL_NS_SS, "chartFormats", GSF_XML_NO_CONTENT, &xlsx_CT_ChartFormats, NULL),
   GSF_XML_IN_NODE (PC_CHARTFORMATS, PC_CHARTFORMAT, XL_NS_SS, "chartsFormat", GSF_XML_NO_CONTENT, &xlsx_CT_ChartFormat, NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, PC_PIVOTHIERARCHIES, XL_NS_SS, "pivotHierarchies", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PC_PIVOTHIERARCHIES, PC_PIVOTHIERARCHY, XL_NS_SS, "pivotHierarchy", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (PC_PIVOTHIERARCHY, MEMBERS, XL_NS_SS, "members", GSF_XML_NO_CONTENT, &xlsx_CT_Members, NULL),
        GSF_XML_IN_NODE (MEMBERS, MEMBER, XL_NS_SS, "member", GSF_XML_NO_CONTENT, &xlsx_CT_Member, NULL),
    GSF_XML_IN_NODE (PC_PIVOTHIERARCHIES, MPS, XL_NS_SS, "mps", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (MPS,MP, XL_NS_SS, "mp", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, PC_PIVOTTABLESTYLEINFO, XL_NS_SS, "pivotTableStyleInfo", GSF_XML_NO_CONTENT, &xlsx_CT_PivotTableStyle, NULL),
  GSF_XML_IN_NODE (PT_CACHE_DEF, PC_FILTERS, XL_NS_SS, "filters", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PC_FILTERS, PC_FILTER, XL_NS_SS, "filter", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (PC_FILTER, AUTO_FILTER, XL_NS_SS, "autoFilter", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PC_FILTERS, EXTLST, XL_NS_SS, "extLst", GSF_XML_NO_CONTENT, NULL,	NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, PC_ROWHIERARCHIESUSAGE, XL_NS_SS, "rowHierarchiesUsage", GSF_XML_NO_CONTENT, NULL, NULL),
   GSF_XML_IN_NODE (PC_ROWHIERARCHIESUSAGE, PC_ROWHIERARCHYUSAGE, XL_NS_SS, "rowHierarchyUsage", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (PT_CACHE_DEF, PC_COLHIERARCHIESUSAGE, XL_NS_SS, "colHierarchiesUsage", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PC_COLHIERARCHIESUSAGE, PC_COLHIERARCHYUSAGE, XL_NS_SS, "colHierarchyUsage", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, GROUPS, XL_NS_SS, "groups", GSF_XML_NO_CONTENT,&xlsx_CT_Groups, NULL),
    GSF_XML_IN_NODE (GROUPS, GROUP, XL_NS_SS, "group", GSF_XML_NO_CONTENT, &xlsx_CT_Group, NULL),
      GSF_XML_IN_NODE (GROUP, G_MEMBERS, XL_NS_SS, "groupMembers", GSF_XML_NO_CONTENT, &xlsx_CT_GroupMembers, NULL),
	GSF_XML_IN_NODE (G_MEMBERS, G_MEMBER, XL_NS_SS, "groupMember", GSF_XML_NO_CONTENT, &xlsx_CT_GroupMember, NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, EXT_LIST, XL_NS_SS, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE_END
};

/*****************************************************************************/
/* Some common, and ignored attributes for the various value types
 * "u"  : unused
 * "f"  : is calculated
 * "c"  : caption
 * "cp" : # member properties
 * "bc" : OLAP background colour
 * "fc" : OLAP foreground colour
 * "in" : OLAP index
 * "b"  : OLAP bold
 * "i"  : OLAP italics
 * "un" : OLAP italics
 * "st" : OLAP strikethrough
 */

/* Utiity routine to insert a value in the various states they appear
 * Absorbs the ref to @val */
static void
xlsx_pivot_insert_value (XLSXReadState *state, GnmValue *v)
{
	if (NULL == state->pivot.cache_field)	/* inline value in a record */
		go_data_cache_set_val (state->pivot.cache,
			state->pivot.field_count++, state->pivot.record_count, v);
	else {
		GPtrArray *a = state->pivot.cache_field_values;
		unsigned i = state->pivot.record_count++;
		if (i < a->len)
			g_ptr_array_index (a, i) = v;
		else if (i == a->len)
			g_ptr_array_add (a, v);
		else {
			g_warning ("index out of whack");
		}
	}
}

static void
xlsx_CT_Missing (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xlsx_pivot_insert_value (state, go_val_new_empty ());
}

static void
xlsx_CT_Number (GsfXMLIn *xin, xmlChar const **attrs)
{
	gnm_float v;
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_float (xin, attrs, "v", &v))
			xlsx_pivot_insert_value (state, go_val_new_float (v));
}
static void
xlsx_CT_Boolean (GsfXMLIn *xin, xmlChar const **attrs)
{
	int v;
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_bool (xin, attrs, "v", &v))
			xlsx_pivot_insert_value (state, go_val_new_bool (v));
}

static void
xlsx_CT_Error (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "v"))
			xlsx_pivot_insert_value (state, value_new_error (NULL, attrs[1]));
}

static void
xlsx_CT_String (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "v"))
			xlsx_pivot_insert_value (state, go_val_new_str (attrs[1]));
}
static void
xlsx_CT_DateTime (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmValue *v;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (NULL != (v = attr_datetime (xin, attrs, "v")))
			xlsx_pivot_insert_value (state, v);
}

/****************************************************************************/

static void
xlsx_CT_Tuples (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

/*****************************************************************************/

static void
xlsx_CT_Index (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int i;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, "v", &i))
			go_data_cache_set_index (state->pivot.cache,
				state->pivot.field_count++, state->pivot.record_count, i);
}

static void
xlsx_CT_Record (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->pivot.field_count = 0;
#ifdef GO_DEBUG_SLICERS
	g_print ("%d)", state->pivot.record_count + 1);
#endif
}
static void
xlsx_CT_Record_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->pivot.record_count++;
#ifdef GO_DEBUG_SLICERS
	g_print ("\n");
#endif
}

static void
xlsx_CT_pivotCacheRecords_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	go_data_cache_import_done (state->pivot.cache, state->pivot.record_count);
#ifdef GO_DEBUG_SLICERS
	go_data_cache_dump (state->pivot.cache, NULL, NULL);
#endif
	state->pivot.record_count = 0;
}
static void
xlsx_CT_pivotCacheRecords (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	unsigned int n = 0;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_uint (xin, attrs, "count", &n))
			;
	}

	state->pivot.record_count = 0;
	go_data_cache_import_start (state->pivot.cache, MIN (n, 10000u));
}

static GsfXMLInNode const xlsx_pivot_cache_records_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, CACHE_RECORDS, XL_NS_SS, "pivotCacheRecords", GSF_XML_NO_CONTENT, FALSE, TRUE,
		      &xlsx_CT_pivotCacheRecords, &xlsx_CT_pivotCacheRecords_end, 0),
  GSF_XML_IN_NODE_FULL (CACHE_RECORDS, CACHE_RECORD, XL_NS_SS, "r", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_CT_Record, &xlsx_CT_Record_end, 0),
    GSF_XML_IN_NODE (CACHE_RECORD, ITEM_MISSING, XL_NS_SS,	"m",	GSF_XML_NO_CONTENT, &xlsx_CT_Missing, NULL),
      GSF_XML_IN_NODE (ITEM_MISSING, ITEM_TPLS, XL_NS_SS,	"tpls",	GSF_XML_NO_CONTENT, &xlsx_CT_Tuples, NULL),
      GSF_XML_IN_NODE (ITEM_MISSING, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_NO_CONTENT, &xlsx_CT_X, NULL),
    GSF_XML_IN_NODE (CACHE_RECORD, ITEM_NUMBER, XL_NS_SS,	"n",	GSF_XML_NO_CONTENT, &xlsx_CT_Number, NULL),
      GSF_XML_IN_NODE (ITEM_NUMBER, ITEM_TPLS, XL_NS_SS,	"tpls",	GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (ITEM_NUMBER, ITEM_X,  XL_NS_SS,		"x",	GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (CACHE_RECORD, ITEM_BOOLEAN, XL_NS_SS,	"b",	GSF_XML_NO_CONTENT, &xlsx_CT_Boolean, NULL),
      GSF_XML_IN_NODE (ITEM_BOOLEAN, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (CACHE_RECORD, ITEM_ERROR, XL_NS_SS,	"e",	GSF_XML_NO_CONTENT, &xlsx_CT_Error, NULL),
      GSF_XML_IN_NODE (ITEM_ERROR, ITEM_TPLS, XL_NS_SS,		"tpls",	GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (ITEM_ERROR, ITEM_X,    XL_NS_SS,		"x",	GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (CACHE_RECORD, ITEM_STRING, XL_NS_SS,	"s",	GSF_XML_NO_CONTENT, &xlsx_CT_String, NULL),
      GSF_XML_IN_NODE (ITEM_STRING, ITEM_TPLS, XL_NS_SS,	"tpls",	GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (ITEM_STRING, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (CACHE_RECORD, ITEM_DATE, XL_NS_SS,		"d",	GSF_XML_NO_CONTENT, &xlsx_CT_DateTime, NULL),
      GSF_XML_IN_NODE (ITEM_DATE, ITEM_TPLS, XL_NS_SS,		"tpls",	GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (ITEM_DATE, ITEM_X,    XL_NS_SS,		"x",	GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (CACHE_RECORD, INDEX,  XL_NS_SS, "x", GSF_XML_NO_CONTENT, &xlsx_CT_Index, NULL),
GSF_XML_IN_NODE_END
};

/*****************************************************************************/

static void
xlsx_CT_pivotCacheDefinition (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	char const *refreshedBy = NULL;
	GOVal *refreshedDate = NULL;
	GOVal *date = NULL;
	unsigned int createdVersion = 0;
	unsigned int refreshedVersion = 0;
	gboolean upgradeOnRefresh = FALSE;
	gnm_float v;

	state->pivot.cache_record_part_id = NULL;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "id"))
			state->pivot.cache_record_part_id = g_strdup (attrs[1]);
		else if (0 == strcmp (attrs[0], "refreshedBy")) refreshedBy = attrs[1];
		else if (attr_float (xin, attrs, "refreshedDate", &v)) {
			 /* idiots : why not use an actual date ?
			  * Assume that this is in the same date convention as the workbook */
			if (refreshedDate == NULL) {
				refreshedDate = value_new_float (v);
				value_set_fmt (refreshedDate, state->date_fmt);
			} else
				xlsx_warning (xin, _("Encountered both the  \"refreshedDate\" and "
						     "the \"refreshedDateIso\" attributes!"));
		} else if ((date = attr_datetime (xin, attrs, "refreshedDateIso")) != NULL) {
			if (refreshedDate)
				go_val_free (refreshedDate);
			refreshedDate = date;
			state->version = ECMA_376_2008;
		} else if (attr_int (xin, attrs, "createdVersion", &createdVersion))
			;
		else if (attr_int (xin, attrs, "refreshedVersion", &refreshedVersion))
			;
		else if (attr_bool (xin, attrs, "upgradeOnRefresh", &upgradeOnRefresh))
			;
	}

#if 0
    <xsd:attribute name="invalid" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="saveData" type="xsd:boolean" use="optional" default="true">
    <xsd:attribute name="refreshOnLoad" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="optimizeMemory" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="enableRefresh" type="xsd:boolean" use="optional" default="true">
    <xsd:attribute name="backgroundQuery" type="xsd:boolean" default="false">
    <xsd:attribute name="missingItemsLimit" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="minRefreshableVersion" type="xsd:unsignedByte" use="optional" default="0">
    <xsd:attribute name="recordCount" type="xsd:unsignedInt" use="optional">
#endif

	state->pivot.field_count = 0;
	state->pivot.cache = g_object_new (GO_DATA_CACHE_TYPE,
					   "refreshed-by", refreshedBy,
					   "refreshed-on", refreshedDate,
					   "refresh-upgrades", upgradeOnRefresh,
					   "refresh-version", refreshedVersion,
					   "created-version", createdVersion,
					   NULL);
	go_val_free (refreshedDate);
}

static void
xlsx_CT_pivotCacheDefinition_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	if (state->pivot.cache_record_part_id) {
		xlsx_parse_rel_by_id (xin, state->pivot.cache_record_part_id,
			xlsx_pivot_cache_records_dtd, xlsx_ns);
		g_free (state->pivot.cache_record_part_id);
	}
	/* leave the object in the state for xlsx_CT_PivotCache to store */
}

static void
xlsx_CT_CacheSource (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="type" type="ST_SourceType" use="required">
      <xsd:enumeration value="worksheet">
      <xsd:enumeration value="external">
      <xsd:enumeration value="consolidation">
      <xsd:enumeration value="scenario">
    <xsd:attribute name="connectionId" type="xsd:unsignedInt" default="0" use="optional">
#endif
}

static void
xlsx_CT_CacheSource_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	if (state->pivot.cache_src) {
		go_data_cache_set_source (state->pivot.cache,
			state->pivot.cache_src);
		state->pivot.cache_src = NULL;
	}
}

static void
xlsx_CT_WorksheetSource (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xmlChar const *sheetName = NULL;
	xmlChar const *sheetID = NULL;
	xmlChar const *targetName = NULL;
	GnmRange range;
	Sheet *sheet;

	range_init_invalid (&range);
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_range (xin, attrs, "ref", &range)) ;
		else if (0 == strcmp (attrs[0], "sheet"))
			sheetName = attrs[1];
		else if (0 == strcmp (attrs[0], "name"))
			targetName = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "id"))
			sheetID = attrs[1];
	}

	(void)sheetID;

	if (NULL != sheetName &&
	    NULL != (sheet = workbook_sheet_by_name (state->wb, sheetName)))
		go_data_cache_set_source (state->pivot.cache,
			gnm_data_cache_source_new (sheet, &range, targetName));
}

static void
xlsx_CT_Consolidation (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="autoPage" type="xsd:boolean" default="true" use="optional">
#endif
}
static void
xlsx_CT_Pages (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="count" type="xsd:unsignedInt" use="optional">
#endif
}

static void
xlsx_CT_PCDSCPage (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="count" type="xsd:unsignedInt" use="optional">
#endif
}

static void
xlsx_CT_RangeSets (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="count" type="xsd:unsignedInt" use="optional">
#endif
}

static void
xlsx_CT_PageItem (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="name" type="ST_Xstring" use="required">
#endif
}

static void
xlsx_CT_RangeSet (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
  <xsd:complexType name="CT_RangeSet">
    <xsd:attribute name="i1" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="i2" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="i3" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="i4" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="ref" type="ST_Ref" use="optional">
    <xsd:attribute name="name" type="ST_Xstring" use="optional">
    <xsd:attribute name="sheet" type="ST_Xstring" use="optional">
    <xsd:attribute ref="r:id" use="optional">
#endif
}

static void
xlsx_CT_CacheFields (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="count" type="xsd:unsignedInt">
#endif
}

static void
xlsx_CT_CacheField (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GOString *name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "name"))
			name = go_string_new (attrs[1]);

	state->pivot.cache_field = g_object_new (GO_DATA_CACHE_FIELD_TYPE,
						 "name", name,
						 NULL);
	go_data_cache_add_field (state->pivot.cache, state->pivot.cache_field);
	state->pivot.field_count++;
	go_string_unref (name);
#if 0
    <xsd:attribute name="caption" type="ST_Xstring" use="optional">
    <xsd:attribute name="propertyName" type="ST_Xstring" use="optional">
    <xsd:attribute name="serverField" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="uniqueList" type="xsd:boolean" use="optional" default="true">
    <xsd:attribute name="numFmtId" type="ST_NumFmtId" use="optional">
    <xsd:attribute name="formula" type="ST_Xstring" use="optional">
    <xsd:attribute name="sqlType" type="xsd:int" use="optional" default="0">
    <xsd:attribute name="hierarchy" type="xsd:int" use="optional" default="0">
    <xsd:attribute name="level" type="xsd:unsignedInt" use="optional" default="0">
    <xsd:attribute name="databaseField" type="xsd:boolean" default="true">
    <xsd:attribute name="mappingCount" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="memberPropertyField" type="xsd:boolean" use="optional" default="false">
#endif
}

static void
xlsx_CT_CacheField_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->pivot.cache_field = NULL;
}

static void
xlsx_field_items_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GPtrArray *a = state->pivot.cache_field_values;

	if (state->pivot.record_count < a->len)
		g_ptr_array_set_size (a, state->pivot.record_count);
	go_data_cache_field_set_vals (state->pivot.cache_field,
		xin->node->user_data.v_int, a);
	state->pivot.cache_field_values = NULL;
}

static void
xlsx_CT_SharedItems (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int n = 0;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_int (xin, attrs, "count", &n))
			;
	}

	state->pivot.record_count = 0;
	state->pivot.cache_field_values = g_ptr_array_sized_new (n);

#if 0
    <xsd:attribute name="containsSemiMixedTypes" type="xsd:boolean" use="optional" default="true">
    <xsd:attribute name="containsNonDate" type="xsd:boolean" use="optional" default="true">
    <xsd:attribute name="containsDate" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="containsString" type="xsd:boolean" use="optional" default="true">
    <xsd:attribute name="containsBlank" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="containsMixedTypes" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="containsNumber" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="containsInteger" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="minValue" type="xsd:double" use="optional">
    <xsd:attribute name="maxValue" type="xsd:double" use="optional">
    <xsd:attribute name="minDate" type="xsd:dateTime" use="optional">
    <xsd:attribute name="maxDate" type="xsd:dateTime" use="optional">
    <xsd:attribute name="longText" type="xsd:boolean" use="optional" default="false">
#endif
}

static void
xlsx_CT_GroupItems (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int n = 0;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_int (xin, attrs, "count", &n))
			;
	}

	state->pivot.record_count = 0;
	state->pivot.cache_field_values = g_ptr_array_sized_new (n);
}

static void
xlsx_CT_FieldGroup (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int tmp;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, "base", &tmp))
			g_object_set (G_OBJECT (state->pivot.cache_field),
				      "group-parent", tmp, NULL);
#if 0 /* not clear on the utility of this */
		else if (attr_int (xin, attrs, "par", &tmp))
			g_object_set (G_OBJECT (state->pivot.cache_field),
				      "child", tmp, NULL);
#endif
}

static void
xlsx_CT_RangePr (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	static EnumVal const bucket_types[] = {
		{ "range",		GO_VAL_BUCKET_SERIES_LINEAR },
		{ "seconds",		GO_VAL_BUCKET_SECOND },
		{ "minutes",		GO_VAL_BUCKET_MINUTE },
		{ "hours",		GO_VAL_BUCKET_HOUR },
		{ "days",		GO_VAL_BUCKET_DAY_OF_YEAR },
		{ "months",		GO_VAL_BUCKET_MONTH },
		{ "quarters",		GO_VAL_BUCKET_CALENDAR_QUARTER },
		{ "years",		GO_VAL_BUCKET_YEAR },
		{ NULL,			0},
	};
	int type;
	gnm_float v;
	GnmValue *dt;
	GError *valid;
	GOValBucketer	bucketer;

	go_val_bucketer_init (&bucketer);
	bucketer.type   		= GO_VAL_BUCKET_SERIES_LINEAR;
	bucketer.details.series.step	= 1.;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, "groupBy", bucket_types, &type))
			bucketer.type = type;
		else if (bucketer.type >= GO_VAL_BUCKET_SERIES_LINEAR) {
			if (attr_float (xin, attrs, "startNum", &v))		bucketer.details.series.minimum	= v;
			else if (attr_float (xin, attrs, "endNum", &v))		bucketer.details.series.maximum	= v;
			else if (attr_float (xin, attrs, "groupInterval", &v))	bucketer.details.series.step	= v;
		} else if (bucketer.type != GO_VAL_BUCKET_NONE) {
			if (NULL != (dt = attr_datetime (xin, attrs, "startDate"))) {
				bucketer.details.dates.minimum = value_get_as_float (dt);
				value_release (dt);
			} else if (NULL != (dt = attr_datetime (xin, attrs, "endDate"))) {
				bucketer.details.dates.maximum = value_get_as_float (dt);
				value_release (dt);
			}
		}

	if (NULL == (valid = go_val_bucketer_validate (&bucketer)))
		g_object_set (G_OBJECT (state->pivot.cache_field), "bucketer", &bucketer, NULL);
	else {
		xlsx_warning (xin, _("Skipping invalid pivot field group for field '%s' because : %s"),
			      go_data_cache_field_get_name (state->pivot.cache_field)->str,
			      valid->message);
		g_error_free (valid);
	}
#if 0
    <xsd:attribute name="autoStart" type="xsd:boolean" default="true">
    <xsd:attribute name="autoEnd" type="xsd:boolean" default="true">
#endif
}
static void
xlsx_CT_DiscretePr (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="count" type="xsd:unsignedInt">
#endif
}

static void
xlsx_CT_CacheHierarchies (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="count" type="xsd:unsignedInt">
#endif
}

static void
xlsx_CT_CacheHierarchy (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="uniqueName" use="required" type="ST_Xstring">
    <xsd:attribute name="caption" use="optional" type="ST_Xstring">
    <xsd:attribute name="measure" type="xsd:boolean" default="false">
    <xsd:attribute name="set" type="xsd:boolean" default="false">
    <xsd:attribute name="parentSet" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="iconSet" type="xsd:int" default="0">
    <xsd:attribute name="attribute" type="xsd:boolean" default="false">
    <xsd:attribute name="time" type="xsd:boolean" default="false">
    <xsd:attribute name="keyAttribute" type="xsd:boolean" default="false">
    <xsd:attribute name="defaultMemberUniqueName" type="ST_Xstring">
    <xsd:attribute name="allUniqueName" type="ST_Xstring">
    <xsd:attribute name="allCaption" type="ST_Xstring">
    <xsd:attribute name="dimensionUniqueName" type="ST_Xstring">
    <xsd:attribute name="displayFolder" type="ST_Xstring">
    <xsd:attribute name="measureGroup" type="ST_Xstring">
    <xsd:attribute name="measures" type="xsd:boolean" default="false">
    <xsd:attribute name="count" use="required" type="xsd:unsignedInt">
    <xsd:attribute name="oneField" type="xsd:boolean" default="false">
    <xsd:attribute name="memberValueDatatype" use="optional" type="xsd:unsignedShort">
    <xsd:attribute name="unbalanced" use="optional" type="xsd:boolean">
    <xsd:attribute name="unbalancedGroup" use="optional" type="xsd:boolean">
    <xsd:attribute name="hidden" type="xsd:boolean" default="false">
#endif
}

static void
xlsx_CT_FieldsUsage (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}
static void
xlsx_CT_GroupLevels (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_PCDKPIs (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_TupleCache (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_CalculatedItems (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_CalculatedMembers (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_Dimensions (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_MeasureGroups (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_MeasureDimensionMaps (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static GsfXMLInNode const xlsx_pivot_cache_def_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, CACHE_DEF, XL_NS_SS, "pivotCacheDefinition", GSF_XML_NO_CONTENT, FALSE, TRUE,
		      &xlsx_CT_pivotCacheDefinition, &xlsx_CT_pivotCacheDefinition_end, 0),
  GSF_XML_IN_NODE (CACHE_DEF, CACHE_SRC, XL_NS_SS,	"cacheSource", GSF_XML_NO_CONTENT,
		   &xlsx_CT_CacheSource, &xlsx_CT_CacheSource_end),
    GSF_XML_IN_NODE (CACHE_SRC, SHEET_SRC, XL_NS_SS,	"worksheetSource", GSF_XML_NO_CONTENT, &xlsx_CT_WorksheetSource, NULL),
    GSF_XML_IN_NODE (CACHE_SRC, CONSOLIDATION, XL_NS_SS, "consolidation", GSF_XML_NO_CONTENT, &xlsx_CT_Consolidation, NULL),
      GSF_XML_IN_NODE (CONSOLIDATION, PAGES, XL_NS_SS,	"pages", GSF_XML_NO_CONTENT, &xlsx_CT_Pages, NULL),
	GSF_XML_IN_NODE (PAGES, PAGE, XL_NS_SS,		"page", GSF_XML_NO_CONTENT, &xlsx_CT_PCDSCPage, NULL),
	  GSF_XML_IN_NODE (PAGE, PAGE_ITEM, XL_NS_SS,	"pageItem", GSF_XML_NO_CONTENT, &xlsx_CT_PageItem, NULL),
      GSF_XML_IN_NODE (CONSOLIDATION, RANGE_SETS, XL_NS_SS, "rangeSets", GSF_XML_NO_CONTENT, &xlsx_CT_RangeSets, NULL),
	GSF_XML_IN_NODE (RANGE_SETS, RANGE_SET, XL_NS_SS, "rangeSet", GSF_XML_NO_CONTENT, &xlsx_CT_RangeSet, NULL),

    GSF_XML_IN_NODE (CACHE_SRC, EXTLST, XL_NS_SS, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (CACHE_DEF, CACHE_FIELDS,	XL_NS_SS, "cacheFields", GSF_XML_NO_CONTENT, &xlsx_CT_CacheFields, NULL),
    GSF_XML_IN_NODE (CACHE_FIELDS, CACHE_FIELD, XL_NS_SS, "cacheField",  GSF_XML_NO_CONTENT,
		     &xlsx_CT_CacheField, &xlsx_CT_CacheField_end),
      GSF_XML_IN_NODE_FULL (CACHE_FIELD, SHARED_ITEMS,    XL_NS_SS, "sharedItems", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_CT_SharedItems, &xlsx_field_items_end, FALSE),
	GSF_XML_IN_NODE (SHARED_ITEMS, ITEM_MISSING, XL_NS_SS,	"m",	GSF_XML_NO_CONTENT, &xlsx_CT_Missing, NULL),
	  GSF_XML_IN_NODE (ITEM_MISSING, ITEM_TPLS, XL_NS_SS,	"tpls",	GSF_XML_NO_CONTENT, &xlsx_CT_Tuples, NULL),
	  GSF_XML_IN_NODE (ITEM_MISSING, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_NO_CONTENT, &xlsx_CT_X, NULL),
	GSF_XML_IN_NODE (SHARED_ITEMS, ITEM_NUMBER, XL_NS_SS,	"n",	GSF_XML_NO_CONTENT, &xlsx_CT_Number, NULL),
	  GSF_XML_IN_NODE (ITEM_NUMBER, ITEM_TPLS, XL_NS_SS,	"tpls",	GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (ITEM_NUMBER, ITEM_X,  XL_NS_SS,	"x",	GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (SHARED_ITEMS, ITEM_BOOLEAN, XL_NS_SS,	"b",	GSF_XML_NO_CONTENT, &xlsx_CT_Boolean, NULL),
	  GSF_XML_IN_NODE (ITEM_BOOLEAN, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (SHARED_ITEMS, ITEM_ERROR, XL_NS_SS,	"e", GSF_XML_NO_CONTENT, &xlsx_CT_Error, NULL),
	  GSF_XML_IN_NODE (ITEM_ERROR, ITEM_TPLS, XL_NS_SS,	"tpls",	GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (ITEM_ERROR, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (SHARED_ITEMS, ITEM_STRING, XL_NS_SS,	"s", GSF_XML_NO_CONTENT, &xlsx_CT_String, NULL),
	  GSF_XML_IN_NODE (ITEM_STRING, ITEM_TPLS, XL_NS_SS,	"tpls",	GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (ITEM_STRING, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (SHARED_ITEMS, ITEM_DATE, XL_NS_SS,	"d", GSF_XML_NO_CONTENT, &xlsx_CT_DateTime, NULL),
	  GSF_XML_IN_NODE (ITEM_DATE, ITEM_TPLS, XL_NS_SS,	"tpls",	GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (ITEM_DATE, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_2ND, NULL, NULL),

      GSF_XML_IN_NODE (CACHE_FIELD, FIELD_GROUP,	XL_NS_SS, "fieldGroup", GSF_XML_NO_CONTENT, &xlsx_CT_FieldGroup, NULL),
        GSF_XML_IN_NODE (FIELD_GROUP, RANGE_PR,    XL_NS_SS, "rangePr", GSF_XML_NO_CONTENT, &xlsx_CT_RangePr, NULL),
	GSF_XML_IN_NODE (FIELD_GROUP, DISCRETE_PR, XL_NS_SS, "discretePr", GSF_XML_NO_CONTENT, &xlsx_CT_DiscretePr, NULL),
	  GSF_XML_IN_NODE (DISCRETE_PR, DISCRETE_INDEX,  XL_NS_SS, "x", GSF_XML_NO_CONTENT, NULL, NULL),

	GSF_XML_IN_NODE_FULL (FIELD_GROUP, GROUP_ITEMS, XL_NS_SS, "groupItems", GSF_XML_NO_CONTENT, FALSE, FALSE,
			      &xlsx_CT_GroupItems, &xlsx_field_items_end, TRUE),
	  GSF_XML_IN_NODE (GROUP_ITEMS, ITEM_MISSING, XL_NS_SS,	"m", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (GROUP_ITEMS, ITEM_NUMBER, XL_NS_SS,	"n", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (GROUP_ITEMS, ITEM_BOOLEAN, XL_NS_SS,	"b", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (GROUP_ITEMS, ITEM_ERROR, XL_NS_SS,	"e", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (GROUP_ITEMS, ITEM_STRING, XL_NS_SS,	"s", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (GROUP_ITEMS, ITEM_DATE, XL_NS_SS,	"d", GSF_XML_2ND, NULL, NULL),

      GSF_XML_IN_NODE (CACHE_FIELD, MP_MAP,	XL_NS_SS, "mpMap", GSF_XML_NO_CONTENT, &xlsx_CT_X, NULL),
      GSF_XML_IN_NODE (CACHE_FIELD, EXTLST,	XL_NS_SS, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (CACHE_DEF, CACHEHIERARCHIES, XL_NS_SS, "cacheHierarchies", GSF_XML_NO_CONTENT, &xlsx_CT_CacheHierarchies, NULL),
    GSF_XML_IN_NODE (CACHEHIERARCHIES, CACHE_HIERARCHY, XL_NS_SS, "cacheHierarchy", GSF_XML_NO_CONTENT, &xlsx_CT_CacheHierarchy, NULL),
      GSF_XML_IN_NODE (CACHE_HIERARCHY, FIELDS_USAGE, XL_NS_SS, "fieldsUsage", GSF_XML_NO_CONTENT, &xlsx_CT_FieldsUsage, NULL),
      GSF_XML_IN_NODE (CACHE_HIERARCHY, GROUP_LEVELS, XL_NS_SS, "groupLevels", GSF_XML_NO_CONTENT, &xlsx_CT_GroupLevels, NULL),

  GSF_XML_IN_NODE (CACHE_DEF, KPIS, XL_NS_SS, "kpis", GSF_XML_NO_CONTENT, &xlsx_CT_PCDKPIs, NULL),
  GSF_XML_IN_NODE (CACHE_DEF, TUPLECACHE, XL_NS_SS, "tupleCache", GSF_XML_NO_CONTENT, &xlsx_CT_TupleCache, NULL),
  GSF_XML_IN_NODE (CACHE_DEF, CALCULATEDITEMS, XL_NS_SS, "calculatedItems", GSF_XML_NO_CONTENT, &xlsx_CT_CalculatedItems, NULL),
  GSF_XML_IN_NODE (CACHE_DEF, CALCULATEDMEMBERS, XL_NS_SS, "calculatedMembers", GSF_XML_NO_CONTENT, &xlsx_CT_CalculatedMembers, NULL),
  GSF_XML_IN_NODE (CACHE_DEF, DIMENSIONS, XL_NS_SS, "dimensions", GSF_XML_NO_CONTENT, &xlsx_CT_Dimensions, NULL),
  GSF_XML_IN_NODE (CACHE_DEF, MEASUREGROUPS, XL_NS_SS, "measureGroups", GSF_XML_NO_CONTENT, &xlsx_CT_MeasureGroups, NULL),
  GSF_XML_IN_NODE (CACHE_DEF, MAPS, XL_NS_SS, "maps", GSF_XML_NO_CONTENT, &xlsx_CT_MeasureDimensionMaps, NULL),
  GSF_XML_IN_NODE (CACHE_DEF, EXTLST, XL_NS_SS, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE_END
};
