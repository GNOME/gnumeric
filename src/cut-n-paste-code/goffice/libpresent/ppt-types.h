/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * ppt-types.h - 
 * Copyright (C) 2003, Christopher James Lahey
 *
 * Authors:
 *   Christopher James Lahey <clahey@ximian.com>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU Library General Public
 * License as published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this file; if not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 **/

#ifndef GOFFICE_PRESENT_PPT_TYPES_H
#define GOFFICE_PRESENT_PPT_TYPES_H

typedef enum {
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
} PPTypecode;

#endif
