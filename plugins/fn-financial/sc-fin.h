/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef __SC_FIN_H__
#define __SC_FIN_H__

Value *    get_amordegrc (gnm_float fCost, GDate *nDate, GDate *nFirstPer, 
			  gnm_float fRestVal, gint nPer, gnm_float fRate,
			  gint nBase);
Value *    get_amorlinc  (gnm_float fCost, GDate *nDate, GDate *nFirstPer, 
			  gnm_float fRestVal, gint nPer, gnm_float fRate,
			  gint nBase);
Value *    get_yielddisc (GDate *nSettle, GDate *nMat, gnm_float fPrice,
			  gnm_float fRedemp, gint nBase);
Value *	   get_yieldmat  (GDate *nSettle, GDate *nMat, GDate *nIssue,
			  gnm_float fRate, gnm_float fPrice, gint nBase);
Value *    get_oddlprice (GDate *nSettle, GDate *nMat, GDate *nLastCoup,
			  gnm_float fRate, gnm_float fYield,
			  gnm_float fRedemp, gint nFreq, gint nBase);
Value *    get_oddlyield (GDate *nSettle, GDate *nMat, GDate *nLastCoup,
			  gnm_float fRate, gnm_float fYield,
			  gnm_float fRedemp, gint nFreq, gint nBase);
Value *    get_duration  (GDate *nSettle, GDate *nMat, gnm_float fCoup,
			  gnm_float fYield, gint nFreq, gint nBase,
			  gnm_float fNumOfCoups);
Value *    get_mduration (GDate *nSettle, GDate *nMat, gnm_float fCoup,
			  gnm_float fYield, gint nFreq, gint nBase,
			  gnm_float fNumOfCoups);
Value *    get_cumprinc  (gnm_float fRate, gint nNumPeriods, gnm_float fVal,
			  gint nStartPer, gint nEndPer, gint nPayType);
Value *    get_cumipmt   (gnm_float fRate, gint nNumPeriods, gnm_float fVal,
			  gint nStartPer, gint nEndPer, gint nPayType);
Value *    get_vdb       (gnm_float cost, gnm_float salvage, gnm_float life,
			  gnm_float start_period, gnm_float end_period,
			  gnm_float factor, gboolean flag);

#endif

