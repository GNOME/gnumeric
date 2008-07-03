/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * xlsx-read-pivot.c: MS Excel XLS import for pivot tables (tm)
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
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="name" use="required" type="ST_Xstring">
    <xsd:attribute name="cacheId" use="required" type="xsd:unsignedInt">
    <xsd:attribute name="dataOnRows" type="xsd:boolean" default="false">
    <xsd:attribute name="dataPosition" type="xsd:unsignedInt" use="optional">
    <xsd:attributeGroup ref="AG_AutoFormat" />
    <xsd:attribute name="dataCaption" use="required" type="ST_Xstring">
    <xsd:attribute name="grandTotalCaption" type="ST_Xstring">
    <xsd:attribute name="errorCaption" type="ST_Xstring">
    <xsd:attribute name="showError" type="xsd:boolean" default="false">
    <xsd:attribute name="missingCaption" type="ST_Xstring">
    <xsd:attribute name="showMissing" type="xsd:boolean" default="true">
    <xsd:attribute name="pageStyle" type="ST_Xstring">
    <xsd:attribute name="pivotTableStyle" type="ST_Xstring">
    <xsd:attribute name="vacatedStyle" type="ST_Xstring">
    <xsd:attribute name="tag" type="ST_Xstring">
    <xsd:attribute name="updatedVersion" type="xsd:unsignedByte" default="0">
    <xsd:attribute name="minRefreshableVersion" type="xsd:unsignedByte" default="0">
    <xsd:attribute name="asteriskTotals" type="xsd:boolean" default="false">
    <xsd:attribute name="showItems" type="xsd:boolean" default="true">
    <xsd:attribute name="editData" type="xsd:boolean" default="false">
    <xsd:attribute name="disableFieldList" type="xsd:boolean" default="false">
    <xsd:attribute name="showCalcMbrs" type="xsd:boolean" default="true">
    <xsd:attribute name="visualTotals" type="xsd:boolean" default="true">
    <xsd:attribute name="showMultipleLabel" type="xsd:boolean" default="true">
    <xsd:attribute name="showDataDropDown" type="xsd:boolean" default="true">
    <xsd:attribute name="showDrill" type="xsd:boolean" default="true">
    <xsd:attribute name="printDrill" type="xsd:boolean" default="false">
    <xsd:attribute name="showMemberPropertyTips" type="xsd:boolean" default="true">
    <xsd:attribute name="showDataTips" type="xsd:boolean" default="true">
    <xsd:attribute name="enableWizard" type="xsd:boolean" default="true">
    <xsd:attribute name="enableDrill" type="xsd:boolean" default="true">
    <xsd:attribute name="enableFieldProperties" type="xsd:boolean" default="true">
    <xsd:attribute name="preserveFormatting" type="xsd:boolean" default="true">
    <xsd:attribute name="useAutoFormatting" type="xsd:boolean" default="false">
    <xsd:attribute name="pageWrap" type="xsd:unsignedInt" default="0">
    <xsd:attribute name="pageOverThenDown" type="xsd:boolean" default="false">
    <xsd:attribute name="subtotalHiddenItems" type="xsd:boolean" default="false">
    <xsd:attribute name="rowGrandTotals" type="xsd:boolean" default="true">
    <xsd:attribute name="colGrandTotals" type="xsd:boolean" default="true">
    <xsd:attribute name="fieldPrintTitles" type="xsd:boolean" default="false">
    <xsd:attribute name="itemPrintTitles" type="xsd:boolean" default="false">
    <xsd:attribute name="mergeItem" type="xsd:boolean" default="false">
    <xsd:attribute name="showDropZones" type="xsd:boolean" default="true">
    <xsd:attribute name="createdVersion" type="xsd:unsignedByte" default="0">
    <xsd:attribute name="indent" type="xsd:unsignedInt" default="1">
    <xsd:attribute name="showEmptyRow" type="xsd:boolean" default="false">
    <xsd:attribute name="showEmptyCol" type="xsd:boolean" default="false">
    <xsd:attribute name="showHeaders" type="xsd:boolean" default="true">
    <xsd:attribute name="compact" type="xsd:boolean" default="true">
    <xsd:attribute name="outline" type="xsd:boolean" default="false">
    <xsd:attribute name="outlineData" type="xsd:boolean" default="false">
    <xsd:attribute name="compactData" type="xsd:boolean" default="true">
    <xsd:attribute name="published" type="xsd:boolean" default="false">
    <xsd:attribute name="gridDropZones" type="xsd:boolean" default="false">
    <xsd:attribute name="immersive" type="xsd:boolean" default="true">
    <xsd:attribute name="multipleFieldFilters" type="xsd:boolean" default="true">
    <xsd:attribute name="chartFormat" type="xsd:unsignedInt" default="0">
    <xsd:attribute name="rowHeaderCaption" type="ST_Xstring">
    <xsd:attribute name="colHeaderCaption" type="ST_Xstring">
    <xsd:attribute name="fieldListSortAscending" type="xsd:boolean" default="false">
    <xsd:attribute name="mdxSubqueries" type="xsd:boolean" default="false">
    <xsd:attribute name="customListSort" type="xsd:boolean" use="optional" default="true">
#endif
}

static void
xlsx_CT_Location (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="ref" use="required" type="ST_Ref">
    <xsd:attribute name="firstHeaderRow" use="required" type="xsd:unsignedInt">
    <xsd:attribute name="firstDataRow" use="required" type="xsd:unsignedInt">
    <xsd:attribute name="firstDataCol" use="required" type="xsd:unsignedInt">
    <xsd:attribute name="rowPageCount" type="xsd:unsignedInt" default="0">
    <xsd:attribute name="colPageCount" type="xsd:unsignedInt" default="0">
#endif
}

static void
xlsx_CT_Fields (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="count" type="xsd:unsignedInt">
#endif
}

static void
xlsx_CT_Field (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_PivotField (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="name" type="ST_Xstring">
    <xsd:attribute name="axis" use="optional" type="ST_Axis">
    <xsd:attribute name="dataField" type="xsd:boolean" default="false">
    <xsd:attribute name="subtotalCaption" type="ST_Xstring">
    <xsd:attribute name="showDropDowns" type="xsd:boolean" default="true">
    <xsd:attribute name="hiddenLevel" type="xsd:boolean" default="false">
    <xsd:attribute name="uniqueMemberProperty" type="ST_Xstring">
    <xsd:attribute name="compact" type="xsd:boolean" default="true">
    <xsd:attribute name="allDrilled" type="xsd:boolean" default="false">
    <xsd:attribute name="numFmtId" type="ST_NumFmtId" use="optional">
    <xsd:attribute name="outline" type="xsd:boolean" default="true">
    <xsd:attribute name="subtotalTop" type="xsd:boolean" default="true">
    <xsd:attribute name="dragToRow" type="xsd:boolean" default="true">
    <xsd:attribute name="dragToCol" type="xsd:boolean" default="true">
    <xsd:attribute name="multipleItemSelectionAllowed" type="xsd:boolean" default="false">
    <xsd:attribute name="dragToPage" type="xsd:boolean" default="true">
    <xsd:attribute name="dragToData" type="xsd:boolean" default="true">
    <xsd:attribute name="dragOff" type="xsd:boolean" default="true">
    <xsd:attribute name="showAll" type="xsd:boolean" default="true">
    <xsd:attribute name="insertBlankRow" type="xsd:boolean" default="false">
    <xsd:attribute name="serverField" type="xsd:boolean" default="false">
    <xsd:attribute name="insertPageBreak" type="xsd:boolean" default="false">
    <xsd:attribute name="autoShow" type="xsd:boolean" default="false">
    <xsd:attribute name="topAutoShow" type="xsd:boolean" default="true">
    <xsd:attribute name="hideNewItems" type="xsd:boolean" default="false">
    <xsd:attribute name="measureFilter" type="xsd:boolean" default="false">
    <xsd:attribute name="includeNewItemsInFilter" type="xsd:boolean" default="false">
    <xsd:attribute name="itemPageCount" type="xsd:unsignedInt" default="10">
    <xsd:attribute name="sortType" type="ST_FieldSortType" default="manual">
    <xsd:attribute name="dataSourceSort" type="xsd:boolean" use="optional">
    <xsd:attribute name="nonAutoSortDefault" type="xsd:boolean" default="false">
    <xsd:attribute name="rankBy" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="defaultSubtotal" type="xsd:boolean" default="true">
    <xsd:attribute name="sumSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="countASubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="avgSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="maxSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="minSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="productSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="countSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="stdDevSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="stdDevPSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="varSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="varPSubtotal" type="xsd:boolean" default="false">
    <xsd:attribute name="showPropCell" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="showPropTip" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="showPropAsCaption" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="defaultAttributeDrillState" type="xsd:boolean" use="optional" default="false">
#endif
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
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
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
xlsx_CT_X (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static GsfXMLInNode const xlsx_pivot_table_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, PT_CACHE_DEF, XL_NS_SS, "pivotTableDefinition", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_CT_pivotTableDefinition, NULL, 0),

  GSF_XML_IN_NODE (PT_CACHE_DEF, LOCATION, XL_NS_SS, "location", GSF_XML_NO_CONTENT, &xlsx_CT_Location, NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, PT_FIELDS, XL_NS_SS, "pivotFields", GSF_XML_NO_CONTENT, &xlsx_CT_Fields, NULL),
    GSF_XML_IN_NODE (PT_FIELDS, PT_FIELD, XL_NS_SS, "pivotField", GSF_XML_NO_CONTENT, &xlsx_CT_PivotField, NULL),
      GSF_XML_IN_NODE (PT_FIELD, PT_FIELD_ITEMS, XL_NS_SS, "items", GSF_XML_NO_CONTENT, &xlsx_CT_Items, NULL),
        GSF_XML_IN_NODE (PT_FIELD_ITEMS, PT_FIELD_ITEM, XL_NS_SS, "item", GSF_XML_NO_CONTENT, &xlsx_CT_Item, NULL),
      GSF_XML_IN_NODE (PT_FIELD, PT_FIELD_SORT_SCOPE, XL_NS_SS, "autoSortScope", GSF_XML_NO_CONTENT,
		       &xlsx_CT_AutoSortScope, NULL),
      GSF_XML_IN_NODE (PT_FIELD, EXTLIST, XL_NS_SS, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, PAGE_FIELDS, XL_NS_SS, "pageFields", GSF_XML_NO_CONTENT, &xlsx_CT_Fields, NULL),
    GSF_XML_IN_NODE (PAGE_FIELDS,   PAGE_FIELD, XL_NS_SS, "pageField", GSF_XML_NO_CONTENT, &xlsx_CT_PageField, NULL),
      GSF_XML_IN_NODE (PAGE_FIELD, EXTLIST, XL_NS_SS, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, COL_FIELDS, XL_NS_SS, "colFields", GSF_XML_NO_CONTENT, &xlsx_CT_Fields, NULL),
    GSF_XML_IN_NODE (COL_FIELDS,    FIELD, XL_NS_SS, "field", GSF_XML_NO_CONTENT, &xlsx_CT_Field, NULL),
  GSF_XML_IN_NODE (PT_CACHE_DEF, ROW_FIELDS, XL_NS_SS, "rowFields", GSF_XML_NO_CONTENT, &xlsx_CT_Fields, NULL),
    GSF_XML_IN_NODE (ROW_FIELDS,    FIELD, XL_NS_SS, "field", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (PT_CACHE_DEF, DATA_FIELDS, XL_NS_SS, "dataFields", GSF_XML_NO_CONTENT, &xlsx_CT_Fields, NULL),
    GSF_XML_IN_NODE (DATA_FIELDS,   DATA_FIELD, XL_NS_SS, "dataField", GSF_XML_NO_CONTENT, &xlsx_CT_Field, NULL),

  GSF_XML_IN_NODE (PT_CACHE_DEF, ROW_ITEMS, XL_NS_SS, "rowItems", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (ROW_ITEMS, ITEM, XL_NS_SS, "i", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (ITEM, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_NO_CONTENT, &xlsx_CT_X, NULL),
  GSF_XML_IN_NODE (PT_CACHE_DEF, COL_ITEMS, XL_NS_SS, "colItems", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (COL_ITEMS, ITEM, XL_NS_SS, "i", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */

  GSF_XML_IN_NODE (PT_CACHE_DEF, PC_FORMATS, XL_NS_SS, "formats", GSF_XML_NO_CONTENT, NULL, NULL), /* CT_Formats" minOccurs="0 */
  GSF_XML_IN_NODE (PT_CACHE_DEF, PC_CONDITIONALFORMATS, XL_NS_SS, "conditionalFormats", GSF_XML_NO_CONTENT, NULL, NULL), /* CT_ConditionalFormats" minOccurs="0 */
  GSF_XML_IN_NODE (PT_CACHE_DEF, PC_CHARTFORMATS, XL_NS_SS, "chartFormats", GSF_XML_NO_CONTENT, NULL, NULL), /* CT_ChartFormats" minOccurs="0 */
  GSF_XML_IN_NODE (PT_CACHE_DEF, PC_PIVOTHIERARCHIES, XL_NS_SS, "pivotHierarchies", GSF_XML_NO_CONTENT, NULL, NULL), /* CT_PivotHierarchies" minOccurs="0 */
  GSF_XML_IN_NODE (PT_CACHE_DEF, PC_PIVOTTABLESTYLEINFO, XL_NS_SS, "pivotTableStyleInfo", GSF_XML_NO_CONTENT, NULL, NULL), /* 0" maxOccurs="1" type="CT_PivotTableStyle */
  GSF_XML_IN_NODE (PT_CACHE_DEF, PC_FILTERS, XL_NS_SS, "filters", GSF_XML_NO_CONTENT, NULL, NULL), /* 0" maxOccurs="1" type="CT_PivotFilters */
  GSF_XML_IN_NODE (PT_CACHE_DEF, PC_ROWHIERARCHIESUSAGE, XL_NS_SS, "rowHierarchiesUsage", GSF_XML_NO_CONTENT, NULL, NULL), /* CT_RowHierarchiesUsage" minOccurs="0" maxOccurs="1 */
  GSF_XML_IN_NODE (PT_CACHE_DEF, PC_COLHIERARCHIESUSAGE, XL_NS_SS, "colHierarchiesUsage", GSF_XML_NO_CONTENT, NULL, NULL), /* CT_ColHierarchiesUsage" minOccurs="0" maxOccurs="1 */
  GSF_XML_IN_NODE (PT_CACHE_DEF, EXT_LIST, XL_NS_SS, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE_END
};

/*****************************************************************************/

static void
xlsx_CT_Missing (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="u" type="xsd:boolean">
    <xsd:attribute name="f" type="xsd:boolean">
    <xsd:attribute name="c" type="ST_Xstring">
    <xsd:attribute name="cp" type="xsd:unsignedInt">
    <xsd:attribute name="in" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="bc" type="ST_UnsignedIntHex" use="optional">
    <xsd:attribute name="fc" type="ST_UnsignedIntHex" use="optional">
    <xsd:attribute name="i" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="un" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="st" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="b" type="xsd:boolean" use="optional" default="false">
#endif
}

static void
xlsx_CT_Number (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="v" use="required" type="xsd:double">
    <xsd:attribute name="u" type="xsd:boolean">
    <xsd:attribute name="f" type="xsd:boolean">
    <xsd:attribute name="c" type="ST_Xstring">
    <xsd:attribute name="cp" type="xsd:unsignedInt">
    <xsd:attribute name="in" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="bc" type="ST_UnsignedIntHex" use="optional">
    <xsd:attribute name="fc" type="ST_UnsignedIntHex" use="optional">
    <xsd:attribute name="i" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="un" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="st" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="b" type="xsd:boolean" use="optional" default="false">
#endif
}
static void
xlsx_CT_Boolean (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="v" use="required" type="xsd:boolean">
    <xsd:attribute name="u" type="xsd:boolean">
    <xsd:attribute name="f" type="xsd:boolean">
    <xsd:attribute name="c" type="ST_Xstring">
    <xsd:attribute name="cp" type="xsd:unsignedInt">
#endif
}

static void
xlsx_CT_Error (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="v" use="required" type="ST_Xstring">
    <xsd:attribute name="u" type="xsd:boolean">
    <xsd:attribute name="f" type="xsd:boolean">
    <xsd:attribute name="c" type="ST_Xstring">
    <xsd:attribute name="cp" type="xsd:unsignedInt">
    <xsd:attribute name="in" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="bc" type="ST_UnsignedIntHex" use="optional">
    <xsd:attribute name="fc" type="ST_UnsignedIntHex" use="optional">
    <xsd:attribute name="i" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="un" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="st" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="b" type="xsd:boolean" use="optional" default="false">
#endif
}

static void
xlsx_CT_String (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="v" use="required" type="ST_Xstring">
    <xsd:attribute name="u" type="xsd:boolean">
    <xsd:attribute name="f" type="xsd:boolean">
    <xsd:attribute name="c" type="ST_Xstring">
    <xsd:attribute name="cp" type="xsd:unsignedInt">
    <xsd:attribute name="in" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="bc" type="ST_UnsignedIntHex" use="optional">
    <xsd:attribute name="fc" type="ST_UnsignedIntHex" use="optional">
    <xsd:attribute name="i" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="un" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="st" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="b" type="xsd:boolean" use="optional" default="false">
#endif
}
static void
xlsx_CT_DateTime (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="v" use="required" type="xsd:dateTime">
    <xsd:attribute name="u" type="xsd:boolean">
    <xsd:attribute name="f" type="xsd:boolean">
    <xsd:attribute name="c" type="ST_Xstring">
    <xsd:attribute name="cp" type="xsd:unsignedInt">
#endif
}

static void
xlsx_CT_Tuples (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_Index (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	<xsd:attribute name="v" use="required" type="xsd:unsignedInt">
#endif
}

/*****************************************************************************/

static void
xlsx_CT_Record (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_pivotCacheRecords (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static GsfXMLInNode const xlsx_pivot_cache_records_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, CACHE_RECORDS, XL_NS_SS, "pivotCacheRecords", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_CT_pivotCacheRecords, NULL, 0),
  GSF_XML_IN_NODE_FULL (CACHE_RECORDS, CACHE_RECORD, XL_NS_SS, "r", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_CT_Record, NULL, 0),
    GSF_XML_IN_NODE (CACHE_RECORD, ITEM_MISSING, XL_NS_SS,	"m",	GSF_XML_NO_CONTENT, &xlsx_CT_Missing, NULL),
      GSF_XML_IN_NODE (ITEM_MISSING, ITEM_TPLS, XL_NS_SS,	"tpls",	GSF_XML_NO_CONTENT, &xlsx_CT_Tuples, NULL),
      GSF_XML_IN_NODE (ITEM_MISSING, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_NO_CONTENT, &xlsx_CT_X, NULL),
    GSF_XML_IN_NODE (CACHE_RECORD, ITEM_NUMBER, XL_NS_SS,	"n",	GSF_XML_NO_CONTENT, &xlsx_CT_Number, NULL),
      GSF_XML_IN_NODE (ITEM_NUMBER, ITEM_TPLS, XL_NS_SS,	"tpls",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
      GSF_XML_IN_NODE (ITEM_NUMBER, ITEM_X,  XL_NS_SS,		"x",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
    GSF_XML_IN_NODE (CACHE_RECORD, ITEM_BOOLEAN, XL_NS_SS,	"b",	GSF_XML_NO_CONTENT, &xlsx_CT_Boolean, NULL),
      GSF_XML_IN_NODE (ITEM_BOOLEAN, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
    GSF_XML_IN_NODE (CACHE_RECORD, ITEM_ERROR, XL_NS_SS,	"e",	GSF_XML_NO_CONTENT, &xlsx_CT_Error, NULL),
      GSF_XML_IN_NODE (ITEM_ERROR, ITEM_TPLS, XL_NS_SS,		"tpls",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
      GSF_XML_IN_NODE (ITEM_ERROR, ITEM_X,    XL_NS_SS,		"x",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
    GSF_XML_IN_NODE (CACHE_RECORD, ITEM_STRING, XL_NS_SS,	"s",	GSF_XML_NO_CONTENT, &xlsx_CT_String, NULL),
      GSF_XML_IN_NODE (ITEM_STRING, ITEM_TPLS, XL_NS_SS,	"tpls",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
      GSF_XML_IN_NODE (ITEM_STRING, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
    GSF_XML_IN_NODE (CACHE_RECORD, ITEM_DATE, XL_NS_SS,		"d",	GSF_XML_NO_CONTENT, &xlsx_CT_DateTime, NULL),
      GSF_XML_IN_NODE (ITEM_DATE, ITEM_TPLS, XL_NS_SS,		"tpls",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
      GSF_XML_IN_NODE (ITEM_DATE, ITEM_X,    XL_NS_SS,		"x",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
    GSF_XML_IN_NODE (CACHE_RECORD, INDEX,  XL_NS_SS, "x", GSF_XML_NO_CONTENT, &xlsx_CT_Index, NULL),
GSF_XML_IN_NODE_END
};

/*****************************************************************************/

static void
xlsx_CT_pivotCacheDefinition (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "id"))
			xlsx_parse_rel_by_id (xin, attrs[1],
				xlsx_pivot_cache_records_dtd, xlsx_ns);

#if 0
    <xsd:attribute name="invalid" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="saveData" type="xsd:boolean" use="optional" default="true">
    <xsd:attribute name="refreshOnLoad" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="optimizeMemory" type="xsd:boolean" use="optional" default="false">
    <xsd:attribute name="enableRefresh" type="xsd:boolean" use="optional" default="true">
    <xsd:attribute name="refreshedBy" type="ST_Xstring" use="optional">
    <xsd:attribute name="refreshedDate" type="xsd:double" use="optional">
    <xsd:attribute name="backgroundQuery" type="xsd:boolean" default="false">
    <xsd:attribute name="missingItemsLimit" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="createdVersion" type="xsd:unsignedByte" use="optional" default="0">
    <xsd:attribute name="refreshedVersion" type="xsd:unsignedByte" use="optional" default="0">
    <xsd:attribute name="minRefreshableVersion" type="xsd:unsignedByte" use="optional" default="0">
    <xsd:attribute name="recordCount" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="upgradeOnRefresh" type="xsd:boolean" use="optional" default="false">
#endif
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
xlsx_CT_WorksheetSource (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="ref" type="ST_Ref" use="optional">
    <xsd:attribute name="name" type="ST_Xstring" use="optional">
    <xsd:attribute name="sheet" type="ST_Xstring" use="optional">
    <xsd:attribute ref="r:id" use="optional">
#endif
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
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="name" type="ST_Xstring" use="required">
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
xlsx_CT_SharedItems (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
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
    <xsd:attribute name="count" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="longText" type="xsd:boolean" use="optional" default="false">
#endif
}

static void
xlsx_CT_GroupItems (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
#endif
}

static void
xlsx_CT_FieldGroup (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
    <xsd:attribute name="par" type="xsd:unsignedInt" use="optional">
    <xsd:attribute name="base" type="xsd:unsignedInt" use="optional">
#endif
}

static void
xlsx_CT_RangePr (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
  <xsd:simpleType name="ST_GroupBy">
      <xsd:enumeration value="range">
      <xsd:enumeration value="seconds">
      <xsd:enumeration value="minutes">
      <xsd:enumeration value="hours">
      <xsd:enumeration value="days">
      <xsd:enumeration value="months">
      <xsd:enumeration value="quarters">
      <xsd:enumeration value="years">

    <xsd:attribute name="autoStart" type="xsd:boolean" default="true">
    <xsd:attribute name="autoEnd" type="xsd:boolean" default="true">
    <xsd:attribute name="groupBy" type="ST_GroupBy" default="range">
    <xsd:attribute name="startNum" type="xsd:double">
    <xsd:attribute name="endNum" type="xsd:double">
    <xsd:attribute name="startDate" type="xsd:dateTime">
    <xsd:attribute name="endDate" type="xsd:dateTime">
    <xsd:attribute name="groupInterval" type="xsd:double" default="1">
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
GSF_XML_IN_NODE_FULL (START, CACHE_DEF, XL_NS_SS, "pivotCacheDefinition", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_CT_pivotCacheDefinition, NULL, 0),
  GSF_XML_IN_NODE (CACHE_DEF, CACHE_SRC, XL_NS_SS,	"cacheSource", GSF_XML_NO_CONTENT, &xlsx_CT_CacheSource, NULL),
    GSF_XML_IN_NODE (CACHE_SRC, SHEET_SRC, XL_NS_SS,	"worksheetSource", GSF_XML_NO_CONTENT, &xlsx_CT_WorksheetSource, NULL),
    GSF_XML_IN_NODE (CACHE_SRC, CONSOLIDATION, XL_NS_SS, "consolidation", GSF_XML_NO_CONTENT, &xlsx_CT_Consolidation, NULL),
      GSF_XML_IN_NODE (CONSOLIDATION, PAGES, XL_NS_SS,	"pages", GSF_XML_NO_CONTENT, &xlsx_CT_Pages, NULL),
	GSF_XML_IN_NODE (PAGES, PAGE, XL_NS_SS,		"page", GSF_XML_NO_CONTENT, &xlsx_CT_PCDSCPage, NULL),
	  GSF_XML_IN_NODE (PAGE, PAGE_ITEM, XL_NS_SS,	"pageItem", GSF_XML_NO_CONTENT, &xlsx_CT_PageItem, NULL),
      GSF_XML_IN_NODE (CONSOLIDATION, RANGE_SETS, XL_NS_SS, "rangeSets", GSF_XML_NO_CONTENT, &xlsx_CT_RangeSets, NULL),
	GSF_XML_IN_NODE (RANGE_SETS, RANGE_SET, XL_NS_SS, "rangeSet", GSF_XML_NO_CONTENT, &xlsx_CT_RangeSet, NULL),

    GSF_XML_IN_NODE (CACHE_SRC, EXTLST, XL_NS_SS, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (CACHE_DEF, CACHE_FIELDS,	XL_NS_SS, "cacheFields", GSF_XML_NO_CONTENT, &xlsx_CT_CacheFields, NULL),
    GSF_XML_IN_NODE (CACHE_FIELDS, CACHE_FIELD, XL_NS_SS, "cacheField",  GSF_XML_NO_CONTENT, &xlsx_CT_CacheField, NULL),
      GSF_XML_IN_NODE (CACHE_FIELD, SHARED_ITEMS,    XL_NS_SS, "sharedItems", GSF_XML_NO_CONTENT, &xlsx_CT_SharedItems, NULL),
	GSF_XML_IN_NODE (SHARED_ITEMS, ITEM_MISSING, XL_NS_SS,	"m",	GSF_XML_NO_CONTENT, &xlsx_CT_Missing, NULL),
	  GSF_XML_IN_NODE (ITEM_MISSING, ITEM_TPLS, XL_NS_SS,	"tpls",	GSF_XML_NO_CONTENT, &xlsx_CT_Tuples, NULL),
	  GSF_XML_IN_NODE (ITEM_MISSING, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_NO_CONTENT, &xlsx_CT_X, NULL),
	GSF_XML_IN_NODE (SHARED_ITEMS, ITEM_NUMBER, XL_NS_SS,	"n",	GSF_XML_NO_CONTENT, &xlsx_CT_Number, NULL),
	  GSF_XML_IN_NODE (ITEM_NUMBER, ITEM_TPLS, XL_NS_SS,	"tpls",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	  GSF_XML_IN_NODE (ITEM_NUMBER, ITEM_X,  XL_NS_SS,	"x",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	GSF_XML_IN_NODE (SHARED_ITEMS, ITEM_BOOLEAN, XL_NS_SS,	"b",	GSF_XML_NO_CONTENT, &xlsx_CT_Boolean, NULL),
	  GSF_XML_IN_NODE (ITEM_BOOLEAN, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	GSF_XML_IN_NODE (SHARED_ITEMS, ITEM_ERROR, XL_NS_SS,	"e", GSF_XML_NO_CONTENT, &xlsx_CT_Error, NULL),
	  GSF_XML_IN_NODE (ITEM_ERROR, ITEM_TPLS, XL_NS_SS,	"tpls",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	  GSF_XML_IN_NODE (ITEM_ERROR, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	GSF_XML_IN_NODE (SHARED_ITEMS, ITEM_STRING, XL_NS_SS,	"s", GSF_XML_NO_CONTENT, &xlsx_CT_String, NULL),
	  GSF_XML_IN_NODE (ITEM_STRING, ITEM_TPLS, XL_NS_SS,	"tpls",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	  GSF_XML_IN_NODE (ITEM_STRING, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	GSF_XML_IN_NODE (SHARED_ITEMS, ITEM_DATE, XL_NS_SS,	"d", GSF_XML_NO_CONTENT, &xlsx_CT_DateTime, NULL),
	  GSF_XML_IN_NODE (ITEM_DATE, ITEM_TPLS, XL_NS_SS,	"tpls",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	  GSF_XML_IN_NODE (ITEM_DATE, ITEM_X,    XL_NS_SS,	"x",	GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */

      GSF_XML_IN_NODE (CACHE_FIELD, FIELD_GROUP,	XL_NS_SS, "fieldGroup", GSF_XML_NO_CONTENT, &xlsx_CT_FieldGroup, NULL),
        GSF_XML_IN_NODE (FIELD_GROUP, RANGE_PR,    XL_NS_SS, "rangePr", GSF_XML_NO_CONTENT, &xlsx_CT_RangePr, NULL),
	GSF_XML_IN_NODE (FIELD_GROUP, DISCRETE_PR, XL_NS_SS, "discretePr", GSF_XML_NO_CONTENT, &xlsx_CT_DiscretePr, NULL),
	  GSF_XML_IN_NODE (DISCRETE_PR, DISCRETE_INDEX,  XL_NS_SS, "x", GSF_XML_NO_CONTENT, &xlsx_CT_Index, NULL),

	GSF_XML_IN_NODE (FIELD_GROUP, GROUP_ITEMS, XL_NS_SS, "groupItems", GSF_XML_NO_CONTENT, &xlsx_CT_GroupItems, NULL),
	  GSF_XML_IN_NODE (GROUP_ITEMS, ITEM_MISSING, XL_NS_SS,	"m", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	  GSF_XML_IN_NODE (GROUP_ITEMS, ITEM_NUMBER, XL_NS_SS,	"n", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	  GSF_XML_IN_NODE (GROUP_ITEMS, ITEM_BOOLEAN, XL_NS_SS,	"b", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	  GSF_XML_IN_NODE (GROUP_ITEMS, ITEM_ERROR, XL_NS_SS,	"e", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	  GSF_XML_IN_NODE (GROUP_ITEMS, ITEM_STRING, XL_NS_SS,	"s", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	  GSF_XML_IN_NODE (GROUP_ITEMS, ITEM_DATE, XL_NS_SS,	"d", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */

      GSF_XML_IN_NODE (CACHE_FIELD, MP_MAP,   	XL_NS_SS, "mpMap", GSF_XML_NO_CONTENT, &xlsx_CT_X, NULL),
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
