/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * test-msole1.c: test program to dump biff streams
 *
 * Copyright (C) 2002-2003	Jody Goldberg (jody@gnome.org)
 * 			Michael Meeks (michael@ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2.1 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 * Parts of this code are taken from libole2/test/test-ole.c
 */

#include <ms-compat/go-ms-parser.h>

#include <gsf/gsf-input-stdio.h>
#include <gsf/gsf-input-memory.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-msole.h>
#include <gsf/gsf-msole-utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#define INDENT { int i; for (i = 0; i < recursion_depth; i++) { g_print ("\t"); } }
static int recursion_depth;

#define STACK_TOP GO_MS_PARSER_STACK_TOP(stack)

enum {
	Unknown = 0,
	Document = 1000,
	DocumentAtom = 1001,
	EndDocument = 1002,
	Slide = 1006,
	SlideAtom = 1007,
	Notes = 1008,
	NotesAtom = 1009,
	Environment = 1010,
	SlidePersistAtom = 1011,
	SSlideLayoutAtom = 1015,
	MainMaster = 1016,
	SSSlideInfoAtom = 1017,
	SlideViewInfo = 1018,
	GuideAtom = 1019,
	ViewInfo = 1020,
	ViewInfoAtom = 1021,
	SlideViewInfoAtom = 1022,
	VBAInfo = 1023,
	VBAInfoAtom = 1024,
	SSDocInfoAtom = 1025,
	Summary = 1026,
	DocRoutingSlip = 1030,
	OutlineViewInfo = 1031,
	SorterViewInfo = 1032,
	ExObjList = 1033,
	ExObjListAtom = 1034,
	PPDrawingGroup = 1035, //FIXME: Office Art File Format Docu
	PPDrawing = 1036, //FIXME: Office Art File Format Docu
	NamedShows = 1040, // don't know if container
	NamedShow = 1041,
	NamedShowSlides = 1042, // don't know if container
	List = 2000,
	FontCollection = 2005,
	BookmarkCollection = 2019,
	SoundCollAtom = 2021,
	Sound = 2022,
	SoundData = 2023,
	BookmarkSeedAtom = 2025,
	ColorSchemeAtom = 2032,
	ExObjRefAtom = 3009,
	OEShapeAtom = 3009,
	OEPlaceholderAtom = 3011,
	GPointAtom = 3024,
	GRatioAtom = 3031,
	OutlineTextRefAtom = 3998,
	TextHeaderAtom = 3999,
	TextCharsAtom = 4000,
	StyleTextPropAtom = 4001,
	BaseTextPropAtom = 4002,
	TxMasterStyleAtom = 4003,
	TxCFStyleAtom = 4004,
	TxPFStyleAtom = 4005,
	TextRulerAtom = 4006,
	TextBookmarkAtom = 4007,
	TextBytesAtom = 4008,
	TxSIStyleAtom = 4009,
	TextSpecInfoAtom = 4010,
	DefaultRulerAtom = 4011,
	FontEntityAtom = 4023,
	FontEmbeddedData = 4024,
	CString = 4026,
	MetaFile = 4033,
	ExOleObjAtom = 4035,
	SrKinsoku = 4040,
	HandOut = 4041,
	ExEmbed = 4044,
	ExEmbedAtom = 4045,
	ExLink = 4046,
	BookmarkEntityAtom = 4048,
	ExLinkAtom = 4049,
	SrKinsokuAtom = 4050,
	ExHyperlinkAtom = 4051,
	ExHyperlink = 4055,
	SlideNumberMCAtom = 4056,
	HeadersFooters = 4057,
	HeadersFootersAtom = 4058,
	TxInteractiveInfoAtom = 4063,
	CharFormatAtom = 4066,
	ParaFormatAtom = 4067,
	RecolorInfoAtom = 4071,
	ExQuickTimeMovie = 4074,
	ExQuickTimeMovieData = 4075,
	ExControl = 4078,
	SlideListWithText = 4080,
	InteractiveInfo = 4082,
	InteractiveInfoAtom = 4083,
	UserEditAtom = 4085,
	CurrentUserAtom = 4086,
	DateTimeMCAtom = 4087,
	GenericDateMCAtom = 4088,
	FooterMCAtom = 4090,
	ExControlAtom = 4091,
	ExMediaAtom = 4100,
	ExVideo = 4101,
	ExAviMovie = 4102,
	ExMCIMovie = 4103,
	ExMIDIAudio = 4109,
	ExCDAudio = 4110,
	ExWAVAudioEmbedded = 4111,
	ExWAVAudioLink = 4112,
	ExOleObjStg = 4113,
	ExCDAudioAtom = 4114,
	ExWAVAudioEmbeddedAtom = 4115,
	AnimationInfoAtom = 4116,
	RTFDateTimeMCAtom = 4117,
	ProgTags = 5000, // don't know if container
	ProgStringTag = 5001,
	ProgBinaryTag = 5002,
	BinaryTagData = 5003,
	PrintOptions = 6000,
	PersistPtrFullBlock = 6001, // don't know if container
	PersistPtrIncrementalBlock = 6002, // don't know if container
	GScalingAtom = 10001,
	GRColorAtom = 10002,
	EscherDggContainer		= 0xf000, /* Drawing Group Container */
	EscherDgg			= 0xf006,
	EscherCLSID			= 0xf016,
	EscherOPT			= 0xf00b,
	EscherBStoreContainer		= 0xf001,
	EscherBSE			= 0xf007,
	EscherBlip_START		= 0xf018, /* Blip types are between */
	EscherBlip_END			= 0xf117, /* these two values */
	EscherDgContainer		= 0xf002, /* Drawing Container */
	EscherDg			= 0xf008,
	EscherRegroupItems		= 0xf118,
	EscherColorScheme		= 0xf120, /* bug in docs */
	EscherSpgrContainer		= 0xf003,
	EscherSpContainer		= 0xf004,
	EscherSpgr			= 0xf009,
	EscherSp			= 0xf00a,
	EscherTextbox			= 0xf00c,
	EscherClientTextbox		= 0xf00d,
	EscherAnchor			= 0xf00e,
	EscherChildAnchor		= 0xf00f,
	EscherClientAnchor		= 0xf010,
	EscherClientData		= 0xf011,
	EscherSolverContainer		= 0xf005,
	EscherConnectorRule		= 0xf012, /* bug in docs */
	EscherAlignRule			= 0xf013,
	EscherArcRule			= 0xf014,
	EscherClientRule		= 0xf015,
	EscherCalloutRule		= 0xf017,
	EscherSelection			= 0xf119,
	EscherColorMRU			= 0xf11a,
	EscherDeletedPspl		= 0xf11d, /* bug in docs */
	EscherSplitMenuColors		= 0xf11e,
	EscherOleObject			= 0xf11f,
	EscherUserDefined		= 0xf122,
};

static const GOMSParserRecordType types[] =
{
	{	Unknown,			"Unknown",			FALSE,	TRUE,	-1,	-1	},
	{	Document,			"Document",			TRUE,	TRUE,	-1,	-1	},
	{	DocumentAtom,			"DocumentAtom",			FALSE,	TRUE,	-1,	-1	},
	{	EndDocument,			"EndDocument",			FALSE,	TRUE,	-1,	-1	},
	{	Slide,				"Slide",			TRUE,	TRUE,	-1,	-1	},
	{	SlideAtom,			"SlideAtom",			FALSE,	TRUE,	-1,	-1	},
	{	Notes,				"Notes",			TRUE,	TRUE,	-1,	-1	},
	{	NotesAtom,			"NotesAtom",			FALSE,	TRUE,	-1,	-1	},
	{	Environment,			"Environment",			TRUE,	TRUE,	-1,	-1	},
	{	SlidePersistAtom,		"SlidePersistAtom",		FALSE,	TRUE,	-1,	-1	},
	{	SSlideLayoutAtom,		"SSlideLayoutAtom",		FALSE,	TRUE,	-1,	-1	},
	{	MainMaster,			"MainMaster",			TRUE,	TRUE,	-1,	-1	},
	{	SSSlideInfoAtom,		"SSSlideInfoAtom",		FALSE,	TRUE,	-1,	-1	},
	{	SlideViewInfo,			"SlideViewInfo",		TRUE,	TRUE,	-1,	-1	},
	{	GuideAtom,			"GuideAtom",			FALSE,	TRUE,	-1,	-1	},
	{	ViewInfo,			"ViewInfo",			TRUE,	TRUE,	-1,	-1	},
	{	ViewInfoAtom,			"ViewInfoAtom",			FALSE,	TRUE,	-1,	-1	},
	{	SlideViewInfoAtom,		"SlideViewInfoAtom",		FALSE,	TRUE,	-1,	-1	},
	{	VBAInfo,			"VBAInfo",			TRUE,	TRUE,	-1,	-1	},
	{	VBAInfoAtom,			"VBAInfoAtom",			FALSE,	TRUE,	-1,	-1	},
	{	SSDocInfoAtom,			"SSDocInfoAtom",		FALSE,	TRUE,	-1,	-1	},
	{	Summary,			"Summary",			TRUE,	TRUE,	-1,	-1	},
	{	DocRoutingSlip,			"DocRoutingSlip",		FALSE,	TRUE,	-1,	-1	},
	{	OutlineViewInfo,		"OutlineViewInfo",		TRUE,	TRUE,	-1,	-1	},
	{	SorterViewInfo,			"SorterViewInfo",		TRUE,	TRUE,	-1,	-1	},
	{	ExObjList,			"ExObjList",			TRUE,	TRUE,	-1,	-1	},
	{	ExObjListAtom,			"ExObjListAtom",		FALSE,	TRUE,	-1,	-1	},
	{	PPDrawingGroup,			"PPDrawingGroup",		TRUE,	TRUE,	-1,	-1	}, //FIXME: Office Art File Format Docu
	{	PPDrawing,			"PPDrawing",			TRUE,	TRUE,	-1,	-1	}, //FIXME: Office Art File Format Docu
	{	NamedShows,			"NamedShows",			FALSE,	TRUE,	-1,	-1	}, // don't know if container
	{	NamedShow,			"NamedShow",			TRUE,	TRUE,	-1,	-1	},
	{	NamedShowSlides,		"NamedShowSlides",		FALSE,	TRUE,	-1,	-1	}, // don't know if container
	{	List,				"List",				TRUE,	TRUE,	-1,	-1	},
	{	FontCollection,			"FontCollection",		TRUE,	TRUE,	-1,	-1	},
	{	BookmarkCollection,		"BookmarkCollection",		TRUE,	TRUE,	-1,	-1	},
	{	SoundCollAtom,			"SoundCollAtom",		FALSE,	TRUE,	-1,	-1	},
	{	Sound,				"Sound",			TRUE,	TRUE,	-1,	-1	},
	{	SoundData,			"SoundData",			FALSE,	TRUE,	-1,	-1	},
	{	BookmarkSeedAtom,		"BookmarkSeedAtom",		FALSE,	TRUE,	-1,	-1	},
	{	ColorSchemeAtom,		"ColorSchemeAtom",		FALSE,	TRUE,	-1,	-1	},
	{	ExObjRefAtom,			"ExObjRefAtom",			FALSE,	TRUE,	-1,	-1	},
	{	OEShapeAtom,			"OEShapeAtom",			FALSE,	TRUE,	-1,	-1	},
	{	OEPlaceholderAtom,		"OEPlaceholderAtom",		FALSE,	TRUE,	-1,	-1	},
	{	GPointAtom,			"GPointAtom",			FALSE,	TRUE,	-1,	-1	},
	{	GRatioAtom,			"GRatioAtom",			FALSE,	TRUE,	-1,	-1	},
	{	OutlineTextRefAtom,		"OutlineTextRefAtom",		FALSE,	TRUE,	-1,	-1	},
	{	TextHeaderAtom,			"TextHeaderAtom",		FALSE,	TRUE,	-1,	-1	},
	{	TextCharsAtom,			"TextCharsAtom",		FALSE,	TRUE,	-1,	-1	},
	{	StyleTextPropAtom,		"StyleTextPropAtom",		FALSE,	TRUE,	-1,	-1	},
	{	BaseTextPropAtom,		"BaseTextPropAtom",		FALSE,	TRUE,	-1,	-1	},
	{	TxMasterStyleAtom,		"TxMasterStyleAtom",		FALSE,	TRUE,	-1,	-1	},
	{	TxCFStyleAtom,			"TxCFStyleAtom",		FALSE,	TRUE,	-1,	-1	},
	{	TxPFStyleAtom,			"TxPFStyleAtom",		FALSE,	TRUE,	-1,	-1	},
	{	TextRulerAtom,			"TextRulerAtom",		FALSE,	TRUE,	-1,	-1	},
	{	TextBookmarkAtom,		"TextBookmarkAtom",		FALSE,	TRUE,	-1,	-1	},
	{	TextBytesAtom,			"TextBytesAtom",		FALSE,	TRUE,	-1,	-1	},
	{	TxSIStyleAtom,			"TxSIStyleAtom",		FALSE,	TRUE,	-1,	-1	},
	{	TextSpecInfoAtom,		"TextSpecInfoAtom",		FALSE,	TRUE,	-1,	-1	},
	{	DefaultRulerAtom,		"DefaultRulerAtom",		FALSE,	TRUE,	-1,	-1	},
	{	FontEntityAtom,			"FontEntityAtom",		FALSE,	TRUE,	-1,	-1	},
	{	FontEmbeddedData,		"FontEmbeddedData",		FALSE,	TRUE,	-1,	-1	},
	{	CString,			"CString",			FALSE,	TRUE,	-1,	-1	},
	{	MetaFile,			"MetaFile",			FALSE,	TRUE,	-1,	-1	},
	{	ExOleObjAtom,			"ExOleObjAtom",			FALSE,	TRUE,	-1,	-1	},
	{	SrKinsoku,			"SrKinsoku",			TRUE,	TRUE,	-1,	-1	},
	{	HandOut,			"HandOut",			TRUE,	TRUE,	-1,	-1	},
	{	ExEmbed,			"ExEmbed",			TRUE,	TRUE,	-1,	-1	},
	{	ExEmbedAtom,			"ExEmbedAtom",			FALSE,	TRUE,	-1,	-1	},
	{	ExLink,				"ExLink",			TRUE,	TRUE,	-1,	-1	},
	{	BookmarkEntityAtom,		"BookmarkEntityAtom",		FALSE,	TRUE,	-1,	-1	},
	{	ExLinkAtom,			"ExLinkAtom",			FALSE,	TRUE,	-1,	-1	},
	{	SrKinsokuAtom,			"SrKinsokuAtom",		FALSE,	TRUE,	-1,	-1	},
	{	ExHyperlinkAtom,		"ExHyperlinkAtom",		FALSE,	TRUE,	-1,	-1	},
	{	ExHyperlink,			"ExHyperlink",			TRUE,	TRUE,	-1,	-1	},
	{	SlideNumberMCAtom,		"SlideNumberMCAtom",		FALSE,	TRUE,	-1,	-1	},
	{	HeadersFooters,			"HeadersFooters",		TRUE,	TRUE,	-1,	-1	},
	{	HeadersFootersAtom,		"HeadersFootersAtom",		FALSE,	TRUE,	-1,	-1	},
	{	TxInteractiveInfoAtom,		"TxInteractiveInfoAtom",	FALSE,	TRUE,	-1,	-1	},
	{	CharFormatAtom,			"CharFormatAtom",		FALSE,	TRUE,	-1,	-1	},
	{	ParaFormatAtom,			"ParaFormatAtom",		FALSE,	TRUE,	-1,	-1	},
	{	RecolorInfoAtom,		"RecolorInfoAtom",		FALSE,	TRUE,	-1,	-1	},
	{	ExQuickTimeMovie,		"ExQuickTimeMovie",		TRUE,	TRUE,	-1,	-1	},
	{	ExQuickTimeMovieData,		"ExQuickTimeMovieData",		FALSE,	TRUE,	-1,	-1	},
	{	ExControl,			"ExControl",			TRUE,	TRUE,	-1,	-1	},
	{	SlideListWithText,		"SlideListWithText",		TRUE,	TRUE,	-1,	-1	},
	{	InteractiveInfo,		"InteractiveInfo",		TRUE,	TRUE,	-1,	-1	},
	{	InteractiveInfoAtom,		"InteractiveInfoAtom",		FALSE,	TRUE,	-1,	-1	},
	{	UserEditAtom,			"UserEditAtom",			FALSE,	TRUE,	-1,	-1	},
	{	CurrentUserAtom,		"CurrentUserAtom",		FALSE,	TRUE,	-1,	-1	},
	{	DateTimeMCAtom,			"DateTimeMCAtom",		FALSE,	TRUE,	-1,	-1	},
	{	GenericDateMCAtom,		"GenericDateMCAtom",		FALSE,	TRUE,	-1,	-1	},
	{	FooterMCAtom,			"FooterMCAtom",			FALSE,	TRUE,	-1,	-1	},
	{	ExControlAtom,			"ExControlAtom",		FALSE,	TRUE,	-1,	-1	},
	{	ExMediaAtom,			"ExMediaAtom",			FALSE,	TRUE,	-1,	-1	},
	{	ExVideo,			"ExVideo",			TRUE,	TRUE,	-1,	-1	},
	{	ExAviMovie,			"ExAviMovie",			TRUE,	TRUE,	-1,	-1	},
	{	ExMCIMovie,			"ExMCIMovie",			TRUE,	TRUE,	-1,	-1	},
	{	ExMIDIAudio,			"ExMIDIAudio",			TRUE,	TRUE,	-1,	-1	},
	{	ExCDAudio,			"ExCDAudio",			TRUE,	TRUE,	-1,	-1	},
	{	ExWAVAudioEmbedded,		"ExWAVAudioEmbedded",		TRUE,	TRUE,	-1,	-1	},
	{	ExWAVAudioLink,			"ExWAVAudioLink",		TRUE,	TRUE,	-1,	-1	},
	{	ExOleObjStg,			"ExOleObjStg",			FALSE,	TRUE,	-1,	-1	},
	{	ExCDAudioAtom,			"ExCDAudioAtom",		FALSE,	TRUE,	-1,	-1	},
	{	ExWAVAudioEmbeddedAtom,		"ExWAVAudioEmbeddedAtom",	FALSE,	TRUE,	-1,	-1	},
	{	AnimationInfoAtom,		"AnimationInfoAtom",		FALSE,	TRUE,	-1,	-1	},
	{	RTFDateTimeMCAtom,		"RTFDateTimeMCAtom",		FALSE,	TRUE,	-1,	-1	},
	{	ProgTags,			"ProgTags",			FALSE,	TRUE,	-1,	-1	}, // don't know if container
	{	ProgStringTag,			"ProgStringTag",		TRUE,	TRUE,	-1,	-1	},
	{	ProgBinaryTag,			"ProgBinaryTag",		TRUE,	TRUE,	-1,	-1	},
	{	BinaryTagData,			"BinaryTagData",		FALSE,	TRUE,	-1,	-1	},
	{	PrintOptions,			"PrintOptions",			FALSE,	TRUE,	-1,	-1	},
	{	PersistPtrFullBlock,		"PersistPtrFullBlock",		FALSE,	TRUE,	-1,	-1	}, // don't know if container
	{	PersistPtrIncrementalBlock,	"PersistPtrIncrementalBlock",	FALSE,	TRUE,	-1,	-1	},
	{	GScalingAtom,			"GScalingAtom",			FALSE,	TRUE,	-1,	-1	},
	{	GRColorAtom,			"GRColorAtom",			FALSE,	TRUE,	-1,	-1	},

	{	EscherDggContainer,		"EscherDggContainer",		TRUE,	TRUE,	-1,	-1	},
	{	EscherDgg,			"EscherDgg",			FALSE,	TRUE,	-1,	-1	},
	{	EscherCLSID,			"EscherCLSID",			FALSE,	TRUE,	-1,	-1	},
	{	EscherOPT,			"EscherOPT",			FALSE,	TRUE,	-1,	-1	},
	{	EscherBStoreContainer,		"EscherBStoreContainer",	TRUE,	TRUE,	-1,	-1	},
	{	EscherBSE,			"EscherBSE",			FALSE,	TRUE,	-1,	-1	},
	{	EscherBlip_START,		"EscherBlip_START",		FALSE,	TRUE,	-1,	-1	},
	{	EscherBlip_END,			"EscherBlip_END",		FALSE,	TRUE,	-1,	-1	},
	{	EscherDgContainer,		"EscherDgContainer",		TRUE,	TRUE,	-1,	-1	},
	{	EscherDg,			"EscherDg",			FALSE,	TRUE,	-1,	-1	},
	{	EscherRegroupItems,		"EscherRegroupItems",		FALSE,	TRUE,	-1,	-1	},
	{	EscherColorScheme,		"EscherColorScheme",		FALSE,	TRUE,	-1,	-1	},
	{	EscherSpgrContainer,		"EscherSpgrContainer",		TRUE,	TRUE,	-1,	-1	},
	{	EscherSpContainer,		"EscherSpContainer",		TRUE,	TRUE,	-1,	-1	},
	{	EscherSpgr,			"EscherSpgr",			FALSE,	TRUE,	-1,	-1	},
	{	EscherSp,			"EscherSp",			FALSE,	TRUE,	-1,	-1	},
	{	EscherTextbox,			"EscherTextbox",		FALSE,	TRUE,	-1,	-1	},
	{	EscherClientTextbox,		"EscherClientTextbox",		TRUE,	TRUE,	-1,	-1	},
	{	EscherAnchor,			"EscherAnchor",			FALSE,	TRUE,	-1,	-1	},
	{	EscherChildAnchor,		"EscherChildAnchor",		FALSE,	TRUE,	-1,	-1	},
	{	EscherClientAnchor,		"EscherClientAnchor",		FALSE,	TRUE,	-1,	-1	},
	{	EscherClientData,		"EscherClientData",		TRUE,	TRUE,	-1,	-1	},
	{	EscherSolverContainer,		"EscherSolverContainer",	TRUE,	TRUE,	-1,	-1	},
	{	EscherConnectorRule,		"EscherConnectorRule",		FALSE,	TRUE,	-1,	-1	},
	{	EscherAlignRule,		"EscherAlignRule",		FALSE,	TRUE,	-1,	-1	},
	{	EscherArcRule,			"EscherArcRule",		FALSE,	TRUE,	-1,	-1	},
	{	EscherClientRule,		"EscherClientRule",		FALSE,	TRUE,	-1,	-1	},
	{	EscherCalloutRule,		"EscherCalloutRule",		FALSE,	TRUE,	-1,	-1	},
	{	EscherSelection,		"EscherSelection",		FALSE,	TRUE,	-1,	-1	},
	{	EscherColorMRU,			"EscherColorMRU",		FALSE,	TRUE,	-1,	-1	},
	{	EscherDeletedPspl,		"EscherDeletedPspl",		FALSE,	TRUE,	-1,	-1	},
	{	EscherSplitMenuColors,		"EscherSplitMenuColors",	FALSE,	TRUE,	-1,	-1	},
	{	EscherOleObject,		"EscherOleObject",		FALSE,	TRUE,	-1,	-1	},
	{	EscherUserDefined,		"EscherUserDefined",		FALSE,	TRUE,	-1,	-1	},
};

enum {
	TEXT_FIELD_PROPERTY_EXISTS_BOLD = 0x00000001,
	TEXT_FIELD_PROPERTY_EXISTS_ITALIC = 0x00000002,
	TEXT_FIELD_PROPERTY_EXISTS_UNDERLINE = 0x00000004,
	TEXT_FIELD_PROPERTY_EXISTS_SHADOW = 0x00000010,
	TEXT_FIELD_PROPERTY_EXISTS_RELIEF = 0x00000200,
	TEXT_FIELD_PROPERTY_EXISTS_FONT = 0x00010000,
	TEXT_FIELD_PROPERTY_EXISTS_FONT_SIZE = 0x00020000,
	TEXT_FIELD_PROPERTY_EXISTS_COLOR = 0x00040000,
	TEXT_FIELD_PROPERTY_EXISTS_OFFSET = 0x00080000,
} TextFieldPropExists;


enum {
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_FLAGS     = 0x0000000f,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_CHARACTER = 0x00000080,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_FAMILY    = 0x00000010,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_SIZE      = 0x00000040,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_COLOR     = 0x00000020,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_ALIGNMENT        = 0x00000800,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_1        = 0x00000400,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_2        = 0x00000200,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_3        = 0x00000100,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_LINE_FEED        = 0x00001000,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_SPACING_ABOVE    = 0x00002000,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_SPACING_BELOW    = 0x00004000,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_4        = 0x00008000,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_5        = 0x00010000,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_ASIAN_UNKNOWN    = 0x000e0000,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BIDI             = 0x00200000,
} TextFieldParagraphPropExists;

enum {
	PARAGRAPH_ALIGNMENT_LEFT = 0,
	PARAGRAPH_ALIGNMENT_CENTER = 1,
	PARAGRAPH_ALIGNMENT_RIGHT = 2,
	PARAGRAPH_ALIGNMENT_JUSTIFY = 3,
} ParagraphAlignment;

static const char *font[0x10000];
static int text_length;

static void
handle_atom (GOMSParserRecord *record, GSList *stack, const guint8 *data, GsfInput *input, GError **err, gpointer user_data)
{
	guint pos = gsf_input_tell (input);
	int i;
	INDENT;
	printf ("Atom %5d: %s, length 0x%hx (=%hd) @ pos = 0x%x (=%d) (vers=0x%x, inst=0x%x)\n",
		record->opcode, record->type->name,
		record->length, record->length, pos, pos, record->vers, record->inst);
	//		}
	if (record->length > 0) {
		int remain;
		switch (record->opcode) {
		case EscherClientAnchor:
			g_print ("%d %d %d %d\n", (int) GSF_LE_GET_GUINT16 (data), (int) GSF_LE_GET_GUINT16 (data + 2), (int) GSF_LE_GET_GUINT16 (data + 4), (int) GSF_LE_GET_GUINT16 (data + 6));
			break;
		case EscherOPT:
			{
				int i;
				guint complex_offset;

				complex_offset = 6 * record->inst;
				for (i = 0; i < record->inst; i++) {
					int id = GSF_LE_GET_GUINT16 (data + i * 6);
					gboolean is_bid = id & 0x4000;
					gboolean is_complex = id & 0x8000;
					guint32 opt_data = GSF_LE_GET_GUINT32 (data + i * 6 + 2);

					id &= 0x3fff;
					g_print ("Opt  Id: %d, is_bid: %s, is_complex: %s, data: %d = 0x%x\n", id, is_bid ? "true" : "false", is_complex ? "true" : "false", opt_data, opt_data);
					if (is_complex) {
						gsf_mem_dump (data + complex_offset, opt_data);
					}
					if (is_complex) {
						complex_offset += opt_data;
					}
				}
			}
			break;
		case TextCharsAtom:
			data = g_utf16_to_utf8 ((gunichar2 *) data, record->length / 2, NULL, NULL, NULL);
			g_print (data);
			g_print ("\n");
			text_length = g_utf8_strlen (data, -1);
			g_free (data);
			break;
		case TextBytesAtom:
			data = g_convert (data, record->length, "utf8", "latin1", NULL, NULL, NULL);
			g_print (data);
			g_print ("\n");
			text_length = g_utf8_strlen (data, -1);
			g_free (data);
			break;
		case BaseTextPropAtom:
			gsf_mem_dump (data, record->length);
			remain = text_length + 1;
			i = 0;
			while (remain > 0) {
				int section_length = GSF_LE_GET_GUINT32 (data + i);
				printf ("length: %d\n", section_length);
				remain -= section_length;
				i += 4;
				printf ("depth: %d\n", (guint32) GSF_LE_GET_GUINT16 (data + i));
				i += 2;
			}
			break;
		case TxMasterStyleAtom:
			{
				int indentation_levels;


				int indentation_level;
				guint i = 0;
				guint lasti;
				gboolean first = TRUE;

				gsf_mem_dump (data, record->length);

				indentation_levels = GSF_LE_GET_GUINT16 (data);
				i += 2;
				lasti = i;
				if (record->inst >= 5) {
					i += 2;
					first = FALSE;
				}
				for (indentation_level = 0; indentation_level < indentation_levels; indentation_level ++) {
					guint32 fields;

					/* Paragraph Attributes */
					fields = GSF_LE_GET_GUINT32 (data + i);
					i += 4;

					printf ("level: %d  para fields: 0x%04x\n", indentation_level, fields);

					if (fields & 0x000f)
						i += 2; /* Bullet Flags */
					if (fields & 0x0080) {
						printf ("Bullet Char: %d\n", (int) GSF_LE_GET_GUINT16 (data + i));
						i += 2; /* Bullet Char */
					}
					if (fields & 0x0010) {
						printf ("Bullet Font: %d=%s\n", (int) GSF_LE_GET_GUINT16 (data + i), font [GSF_LE_GET_GUINT16 (data + i)]);
						i += 2; /* Bullet Font */
					}
					if (fields & 0x0040) {
						printf ("Bullet Height: %d\n", (int) GSF_LE_GET_GUINT16 (data + i));
						i += 2; /* Bullet Height */
					}
					if (fields & 0x0020) {
						i += 4; /* Bullet Color */
					}
					if (first) {
						if (fields & 0x0f00) {
							printf ("Justification: %d\n", (int) (GSF_LE_GET_GUINT16 (data + i) & 3));
							i += 2; /* Justification last 2 bits */
						}
					} else {
						if (fields & 0x0800) {
							printf ("Justification: %d\n", (int) (GSF_LE_GET_GUINT16 (data + i) & 3));
							i += 2; /* Justification last 2 bits */
						}
					}
					if (fields & 0x1000)
						i += 2; /* line feed */
					if (fields & 0x2000)
						i += 2; /* upper dist */
					if (fields & 0x4000)
						i += 2; /* lower dist */
					if (first) {
						if (fields & 0x8000) {
							printf ("Text Offset: %d\n", (int) GSF_LE_GET_GUINT16 (data + i));
							i += 2; /* Text offset */
						}
						if (fields & 0x00010000) {
							printf ("Bullet Offset: %d\n", (int) GSF_LE_GET_GUINT16 (data + i));
							i += 2; /* Bullet offset */
						}
						if (fields & 0x00020000) 
							i += 2; /* Default tab */
						if (fields & 0x00200000) {
							guint tab_count = GSF_LE_GET_GUINT16 (data + i);
							i += 2 + tab_count * 4; /* Tabs */
						}
						if (fields & 0x00040000)
							i += 2; /* Unknown */
						if (fields & 0x00080000)
							i += 2; /* Asian Line Break */
						if (fields & 0x00100000)
							i += 2; /* bidi */
					} else {
						if (fields & 0x8000)
							i += 2; /* Unknown */
						if (fields & 0x0100) {
							printf ("Text Offset: %d\n", (int) GSF_LE_GET_GUINT16 (data + i));
							i += 2; /* Text offset */
						}
						if (fields & 0x0200)
							i += 2; /* Unknown */
						if (fields & 0x0400) {
							printf ("Bullet Offset: %d\n", (int) GSF_LE_GET_GUINT16 (data + i));
							i += 2; /* Bullet offset */
						}
						if (fields & 0x00010000)
							i += 2; /* Unknown */
						if (fields & 0x000e0000)
							i += 2; /* Asian Line Break some bits. */
						if (fields & 0x00100000) {
							guint tab_count = GSF_LE_GET_GUINT16 (data + i);
							i += 2 + tab_count * 4; /* Tabs */
						}
						if (fields & 0x00200000) {
							i += 2;
						}
					}

					/* Character Attributes */
					fields = GSF_LE_GET_GUINT32 (data + i);
					printf ("level: %d  char fields: 0x%04x\n", indentation_level, fields);
					i += 4;
					if (fields & 0x0000ffff)
						i += 2; /* Bit Field */
					if (fields & 0x00010000) {
						printf ("Font: %d=%s\n", (int) GSF_LE_GET_GUINT16 (data + i), font [GSF_LE_GET_GUINT16 (data + i)]);
						i += 2; /* Font */
					}
					if (fields & 0x00200000)
						i += 2; /* Asian or Complex Font */
					if (fields & 0x00400000)
						i += 2; /* Unknown */
					if (fields & 0x00800000)
						i += 2; /* Symbol */
					if (fields & 0x00020000) {
						printf ("Font size: %d\n", (int) GSF_LE_GET_GUINT16 (data + i));
						i += 2;
					}
					if (fields & 0x00040000)
						i += 4; /* Font Color */
					if (fields & 0x00080000)
						i += 2; /* Escapement */
					if (fields & 0x00100000)
						i += 2; /* Unknown */
					if (i != lasti &&
					    i <= record->length) {
						gsf_mem_dump (data + lasti, i - lasti);
						lasti = i;
					}

					first = FALSE;
				}

			}
			break;
		case StyleTextPropAtom:
			gsf_mem_dump (data, record->length);
			remain = text_length + 1;
			i = 0;
			while (remain > 0) {
				int sublen = 0;
				guint fields;
				int section_length = GSF_LE_GET_GUINT32 (data + i);
				printf ("length: %d\n", section_length);
				remain -= section_length;
				sublen += 4;
				printf ("indent level: %d\n", (int) GSF_LE_GET_GUINT16 (data + i + sublen));
				sublen += 2;
				fields = GSF_LE_GET_GUINT32 (data + i + sublen);
				printf ("para fields: 0x%04x\n", fields);
				sublen += 4;
				if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_FLAGS) {
					sublen += 2;
				}
				if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_CHARACTER) {
					sublen += 2;
				}
				if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_FAMILY) {
					sublen += 2;
				}
				if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_SIZE) {
					sublen += 2;
				}
				if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_COLOR) {
					sublen += 4;
				}
				if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_ALIGNMENT) {
					printf ("Alignment: ");
					switch (GSF_LE_GET_GUINT16 (data + i + sublen)) {
					case PARAGRAPH_ALIGNMENT_LEFT:
						printf ("Left");
						break;
					case PARAGRAPH_ALIGNMENT_CENTER:
						printf ("Center");
						break;
					case PARAGRAPH_ALIGNMENT_RIGHT:
						printf ("Right");
						break;
					case PARAGRAPH_ALIGNMENT_JUSTIFY:
						printf ("Justify");
						break;
					}
					printf ("\n");
					sublen += 2;
				}
				if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_1) {
					sublen += 2;
				}
				if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_2) {
					sublen += 2;
				}
				if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_3) {
					sublen += 2;
				}
				if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_LINE_FEED) {
					sublen += 2;
				}

				if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_SPACING_ABOVE) {
					int space;
					space = GSF_LE_GET_GUINT16 (data + i + sublen);
					if (space & 0x8000) {
						space = 0x10000 - space;
					}
					printf ("space_before: %.3f\"\n", space / 576.0);
					sublen += 2;
				}
				if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_SPACING_BELOW) {
					int space;
					space = GSF_LE_GET_GUINT16 (data + i + sublen);
					if (space & 0x8000) {
						space = 0x10000 - space;
					}
					printf ("space_after: %.3f\"\n", space / 576.0);
					sublen += 2;
				}
				if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_4)
					sublen += 2;
				if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_5)
					sublen += 2;
				if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_ASIAN_UNKNOWN)
					sublen += 2;
				if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BIDI)
					sublen += 2;
				gsf_mem_dump (data + i, sublen);
				i += sublen;
			}
			remain = text_length + 1;
			while (remain > 0) {
				int sublen = 0;
				guint fields;
				int section_length = GSF_LE_GET_GUINT32 (data + i);
				printf ("length: %d\n", section_length);
				remain -= section_length;
				sublen += 4;
				fields = GSF_LE_GET_GUINT32 (data + i + sublen);
				printf ("char fields: 0x%04x\n", fields);
				sublen += 4;
				if (fields & (TEXT_FIELD_PROPERTY_EXISTS_BOLD |
					      TEXT_FIELD_PROPERTY_EXISTS_ITALIC |
					      TEXT_FIELD_PROPERTY_EXISTS_UNDERLINE |
					      TEXT_FIELD_PROPERTY_EXISTS_SHADOW |
					      TEXT_FIELD_PROPERTY_EXISTS_RELIEF)) {
					guint text_fields = GSF_LE_GET_GUINT16 (data + i + sublen);
					if (text_fields & 0x1)
						printf ("bold\n");
					if (text_fields & 0x2)
						printf ("italic\n");
					if (text_fields & 0x4)
						printf ("underline\n");
					if (text_fields & 0x10)
						printf ("shadow\n");
					if (text_fields & 0x200)
						printf ("relief\n");
					sublen += 2;
				}
				if (fields & TEXT_FIELD_PROPERTY_EXISTS_FONT) {
					printf ("Font: %d=%s\n", (int) GSF_LE_GET_GUINT16 (data + i + sublen), font [GSF_LE_GET_GUINT16 (data + i + sublen)]);
					sublen += 2;
				}
				if (fields & TEXT_FIELD_PROPERTY_EXISTS_FONT_SIZE) {
					printf ("Font size: %d\n", GSF_LE_GET_GUINT16 (data + i + sublen));
					sublen += 2;
				}
				if (fields & (TEXT_FIELD_PROPERTY_EXISTS_COLOR)) {
					printf ("color: #");
					printf ("%02X", data[i + sublen++]);
					printf ("%02X", data[i + sublen++]);
					printf ("%02X", data[i + sublen++]);
					printf ("\n");
					sublen ++;
				}
				if (fields & TEXT_FIELD_PROPERTY_EXISTS_OFFSET) {
					int offset = GSF_LE_GET_GUINT16 (data + i + sublen - 2);
					if (offset & 0x8000)
						offset -= 0x10000;
					printf ("offset: %d\n", offset);
					sublen += 2;
				}
				gsf_mem_dump (data + i, sublen);
				i += sublen;
			}
		case FontEntityAtom:
			font[record->inst] = g_utf16_to_utf8 ((gunichar2 *) data, record->length / 2, NULL, NULL, NULL);
			printf ("Font %d = %s\n", record->inst, font[record->inst]);
			break;
		default:
			if (record->length > 0) {
				if (data == NULL) {
					data = gsf_input_read (input, record->length, NULL);
				}
				gsf_mem_dump (data, record->length);
			}
			break;
		}
	}
}

static void
start_container (GSList *stack, GsfInput *input, GError **err, gpointer user_data)
{
	guint pos = gsf_input_tell (input);
	INDENT;
	printf ("Cont %5d: %s, length 0x%hx (=%hd) @ pos = 0x%x (=%d) (vers=0x%x, inst=0x%x)\n",
		STACK_TOP->opcode, STACK_TOP->type->name,
		STACK_TOP->length, STACK_TOP->length, pos, pos, STACK_TOP->vers, STACK_TOP->inst);
	recursion_depth++;
}

static void
end_container (GSList *stack, GsfInput *input, GError **err, gpointer user_data)
{
	recursion_depth--;
}

static GOMSParserCallbacks callbacks = { handle_atom,
					 start_container,
					 end_container };

static int
test (unsigned argc, char *argv[])
{
	GsfInput  *input, *stream;
	GsfInfile *infile;
	GError    *err = NULL;
	unsigned i;

	for (i = 0; i < 0x10000; i++) {
		font[i] = "Unknown";
	}

	for (i = 1 ; i < argc ; i++) {
		g_print ("%s\n",argv[i]);

		input = gsf_input_mmap_new (argv[i], &err);
		if (input == NULL) {
			g_return_val_if_fail (err != NULL, 1);
			g_warning ("'%s' error: %s", argv[i], err->message);
			g_error_free (err);
			continue;
		}

		input = gsf_input_uncompress (input);
		infile = gsf_infile_msole_new (input, &err);
#if 0
		stream = gsf_infile_child_by_name (infile, "\01CompObj");
		if (stream != NULL) {
			gsf_off_t len = gsf_input_size (stream);
			guint8 const *data = gsf_input_read (stream, len, NULL);
			if (data != NULL)
				gsf_mem_dump (data, len);
			g_object_unref (G_OBJECT (stream));
		}
		return 0;
#endif

		stream = gsf_infile_child_by_name (infile, "\05SummaryInformation");
		if (stream != NULL) {
			puts ( "SummaryInfo");
			gsf_msole_metadata_read (stream, &err);
			if (err != NULL)
				g_warning ("'%s' error: %s", argv[i], err->message);
			g_object_unref (G_OBJECT (stream));
		}

		stream = gsf_infile_child_by_name (infile, "\05DocumentSummaryInformation");
		if (stream != NULL) {
			puts ( "DocSummaryInfo");
			gsf_msole_metadata_read (stream, &err);
			if (err != NULL)
				g_warning ("'%s' error: %s", argv[i], err->message);
			g_object_unref (G_OBJECT (stream));
		}

		stream = gsf_infile_child_by_name (infile, "PowerPoint Document");

		if (stream != NULL) {
			g_print ("PowerPoint Document\n");

			go_ms_parser_read (stream,
					   gsf_input_remaining (stream),
					   types,
					   (sizeof (types) / sizeof (types[0])),
					   &callbacks,
					   NULL,
					   NULL);
			g_object_unref (G_OBJECT (stream));
		}

		stream = gsf_infile_child_by_name (infile, "Pictures");

		if (stream != NULL) {
			g_print ("Pictures\n");
			go_ms_parser_read (stream,
					   gsf_input_remaining (stream),
					   types,
					   (sizeof (types) / sizeof (types[0])),
					   &callbacks,
					   NULL,
					   NULL);

			g_object_unref (G_OBJECT (stream));
		}

		g_object_unref (G_OBJECT (infile));
		g_object_unref (G_OBJECT (input));
	}

	return 0;
}

int
main (int argc, char *argv[])
{
	int res;

	gsf_init ();
	res = test (argc, argv);
	gsf_shutdown ();

	return res;
}
