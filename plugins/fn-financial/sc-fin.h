/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef __SC_FIN_H__
#define __SC_FIN_H__

Value *    get_amordegrc (gnum_float fCost, GDate *nDate, GDate *nFirstPer, 
			  gnum_float fRestVal, gint nPer, gnum_float fRate,
			  gint nBase);
Value *    get_amorlinc  (gnum_float fCost, GDate *nDate, GDate *nFirstPer, 
			  gnum_float fRestVal, gint nPer, gnum_float fRate,
			  gint nBase);
Value *    get_yielddisc (GDate *nSettle, GDate *nMat, gnum_float fPrice,
			  gnum_float fRedemp, gint nBase);
Value *	   get_yieldmat  (GDate *nSettle, GDate *nMat, GDate *nIssue,
			  gnum_float fRate, gnum_float fPrice, gint nBase);
Value *    get_oddlprice (GDate *nSettle, GDate *nMat, GDate *nLastCoup,
			  gnum_float fRate, gnum_float fYield,
			  gnum_float fRedemp, gint nFreq, gint nBase);
Value *    get_oddlyield (GDate *nSettle, GDate *nMat, GDate *nLastCoup,
			  gnum_float fRate, gnum_float fYield,
			  gnum_float fRedemp, gint nFreq, gint nBase);
Value *    get_duration  (GDate *nSettle, GDate *nMat, gnum_float fCoup,
			  gnum_float fYield, gint nFreq, gint nBase,
			  gnum_float fNumOfCoups);
Value *    get_cumprinc  (gnum_float fRate, gint nNumPeriods, gnum_float fVal,
			  gint nStartPer, gint nEndPer, gint nPayType);


#endif

