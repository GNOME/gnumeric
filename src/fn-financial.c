/*
 * fn-financial.c:  Built in financial functions and functions registration
 *
 * Authors:
 *  Vladimir Vuksan (vuksan@veus.hr)
 *  Jukka-Pekka Iivonen (iivonen@iki.fi)
 *  

 */
#include <config.h>
#include <gnome.h>
#include <math.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

double calculate_pvif (double rate, double nper);
double calculate_pvifa (double rate, double nper);
double calculate_fvif (double rate, double nper);
double calculate_fvifa (double rate, double nper);
double calculate_principal (double starting_principal, double payment, double rate, double period);
double calculate_pmt (double rate, double nper, double pv, double fv, int type);

/* Some forward declarations */

/*

Below are some of the functions that are used quite often in
financial analysis.

Present value interest factor 

	              1
	 PVIF = ( ---------- ) ^ n
	            1 + k

Future value interest factor 

         FVIF = 1 / PVIF

Present value interest factor of annuities

                  1          1
	 PVIFA = --- - -----------
                  k     k*(1+k)^n

Future value interest factor of annuities

                  (1+k)^n - 1
         FVIFA = ----------------
	                k



	 PV * PVIF(k%, nper) + PMT * ( 1 + rate * type ) * FVIFA(k%, nper) + FV = 0

 */

double calculate_pvif (double rate, double nper) 
{

  return ( pow ( 1 + rate, nper) );

}

double calculate_fvif (double rate, double nper) 
{

  return ( 1.0 / calculate_pvif(rate,nper) );

}

double calculate_pvifa (double rate, double nper) 
{

  return ( ( 1.0 / rate ) - ( 1.0 / ( rate * pow(1+rate, nper) ))) ;

}

double calculate_fvifa (double rate, double nper)
{
  return (  (pow(1+rate, nper) - 1) / rate); 
}

/*

Principal for period x is calculated using this formula

PR(x) = PR(0) * ( 1 + rate ) ^ x + PMT * ( ( 1 + rate ) ^ x - 1 ) / rate )

*/

double calculate_principal (double starting_principal, double payment, double rate, double period) 
{

  return ( starting_principal * pow( 1.0 + rate, period ) + payment * ( ( pow(1+rate, period) - 1 ) / rate ));

}

double calculate_pmt (double rate, double nper, double pv, double fv, int type){

  double pvif, fvifa;
	/* Calculate the PVIF and FVIFA */

	pvif = calculate_pvif(rate,nper);
	fvifa = calculate_fvifa(rate,nper);

        return (( (-1.0) * pv * pvif  - fv ) / ( ( 1.0 + rate * type) * fvifa )); 

}


static char *help_effect = {
	N_("@FUNCTION=EFFECT\n"
	   "@SYNTAX=EFFECT(b1,b2)\n"
	   "@DESCRIPTION=Calculates the effective interest rate from "
	   "a given nominal rate.\n"
	   "Effective interest rate is calculated using this formulae:\n"
	   "\n"
	   "         r"
           "( 1 + ------ ) ^ nper - 1"
           "       nper"
	   "\n"
	   "where:\n"
	   "\n"
	   "r = nominal interest rate (stated in yearly terms)"
	   "nper = number of periods used for compounding"
	   "\n"
	   "For example credit cards will list an APR (annual percentage rate) which "
	   "is a nominal interest rate."
	   "\n"
	   "For example if you wanted to find out how much you are actually paying interest "
	   "on your credit card that states an APR of 19% that is compounded monthly "
	   "you would type in:"
	   "\n"
	   "=EFFECT(.19,12) and you would get .2075 or 20.75%. That is the effective percentage you will "
	   "pay on your loan."
	   "\n"
	   "@SEEALSO=NOMINAL")
};


static FuncReturn *
gnumeric_effect (FunctionEvalInfo *s)
{
	double rate;
	int nper;

	rate = value_get_as_float (s->a.args[0]);
	nper = value_get_as_int (s->a.args[1]);

	/* Rate or number of periods cannot be negative */
	if ( (rate < 0) || (nper <= 0) ){
		s->error_string = _("effect - domain error");
		return NULL;
	}

        return (FuncReturn *)value_new_float ( pow( (1 + rate/nper) , nper) - 1 );

}


static char *help_nominal = {
	N_("@FUNCTION=NOMINAL\n"
	   "@SYNTAX=NOMINAL(b1,b2)\n"
	   "@DESCRIPTION=Calculates the nominal interest rate from "
	   "a given effective rate.\n"
	   "Nominal interest rate is given by a formula:\n"
	   "\n"
           "nper * (( 1 + r ) ^ (1 / nper) - 1 )"
	   "\n"
	   "where:\n"
	   "\n"
	   "r = effective interest rate"
	   "nper = number of periods used for compounding"
	   "\n"
	   "@SEEALSO=EFFECT")
};

static FuncReturn *
gnumeric_nominal (FunctionEvalInfo *s)
{
	double rate;
	int nper;

	rate = value_get_as_float (s->a.args[0]);
	nper = value_get_as_int (s->a.args[1]);

	/* Rate or number of periods cannot be negative */
	if ( (rate < 0) || (nper <= 0) ){
		s->error_string = _("nominal - domain error");
		return NULL;
	}

        return (FuncReturn *)value_new_float ( nper * ( pow( 1 + rate, 1.0/nper ) - 1 ) );

}




static char *help_sln = {
	N_("@FUNCTION=SLN\n"
	   "@SYNTAX=SLN(cost,salvage value,life)\n"

	   "@DESCRIPTION=Calculates the straight line depriciation for an"
	   "asset based on its cost, salvage value and anticipated life."
	   "\n"
	   "Formula for straight line depriciation is:"
	   "\n"
	   "Depriciation expense = ( cost - salvage value ) / life"
	   "\n"	
	   "\tcost = cost of an asset when acquired (market value)"
	   "\tsalvage_value = amount you get when asset sold at the end of life"
	   "\tlife = anticipated life of an asset"
	   "@SEEALSO=SYD")
};


static FuncReturn *
gnumeric_sln (FunctionEvalInfo *s)
{
	double cost,salvage_value,life;

	cost = value_get_as_float (s->a.args[0]);
	salvage_value = value_get_as_int (s->a.args[1]);
	life = value_get_as_float (s->a.args[2]);

	/* Life of an asset cannot be negative */

	if ( life < 0 ){
		s->error_string = _("sln - domain error");
		return NULL;
	}

        return (FuncReturn *)value_new_float ( (cost - salvage_value) / life ) ;

}

static char *help_syd = {
	N_("@FUNCTION=SYD\n"
	   "@SYNTAX=SYD(cost,salvage value,life,period)\n"

	   "@DESCRIPTION=Calculates the sum-of-years digits depriciation for an"
	   "asset based on its cost, salvage value, anticipated life and a"
	   "particular period."
	   "\n"
	   "Formula for sum-of-years digits depriciation is:"
	   "\n"
	   "Depriciation expense = ( cost - salvage value ) * (life-period+1) * 2 / life * (life + 1)"
	   "\n"
	   "\tcost = cost of an asset when acquired (market value)"
	   "\tsalvage_value = amount you get when asset sold at the end of life"
	   "\tlife = anticipated life of an asset"
	   "\tperiod = period for which we need the expense"
	   "@SEEALSO=SLN")
};

static FuncReturn *
gnumeric_syd (FunctionEvalInfo *s)
{
	double cost,salvage_value,life,period;

	cost = value_get_as_float (s->a.args[0]);
	salvage_value = value_get_as_int (s->a.args[1]);
	life = value_get_as_float (s->a.args[2]);
	period = value_get_as_float (s->a.args[3]);

        return (FuncReturn *)value_new_float ( ( (cost - salvage_value) * (life-period+1) * 2 ) / ( life * (life + 1.0) )) ;

}

static char *help_dollarde = {
	N_("@FUNCTION=DOLLARDE\n"
	   "@SYNTAX=DOLLARDE(fractional_dollar,fraction)\n"
	   "@DESCRIPTION=DOLLARDE converts a dollar price expressed as a "
	   "fraction into a dollar price expressed as a decimal number. "
	   "\n"
	   "If fraction is non-integer it is truncated. "
	   "If fraction<=0 DOLLARDE returns #NUM! error. "
	   "@SEEALSO=DOLLARFR")
};


static FuncReturn *
gnumeric_dollarde (FunctionEvalInfo *s)
{
        float_t fractional_dollar;
	int     fraction, n, tmp;
	float_t floored, rest;

	fractional_dollar = value_get_as_float (s->a.args[0]) ;
	fraction = value_get_as_int (s->a.args[1]) ;

	if (fraction <= 0) {
                s->error_string = _("#NUM!") ;
                return NULL;
	}

	tmp = fraction;
	/* Count digits in fraction */
	for (n=0; tmp; n++)
	        tmp /= 10;

	floored = floor (fractional_dollar);
	rest = fractional_dollar - floored;
	tmp = (int) (rest * pow(10, n));

	return (FuncReturn *)value_new_float (floored + ((float_t) tmp / fraction)) ;
}

static char *help_dollarfr = {
	N_("@FUNCTION=DOLLARFR\n"
	   "@SYNTAX=DOLLARFR(decimal_dollar,fraction)\n"
	   "@DESCRIPTION=DOLLARFR converts a decimal dollar price into "
	   "a dollar price expressed as a fraction. "
	   "\n"
	   "If fraction is non-integer it is truncated. "
	   "If fraction<=0 DOLLARDE returns #NUM! error. "
	   "@SEEALSO=DOLLARDE")
};


static FuncReturn *
gnumeric_dollarfr (FunctionEvalInfo *s)
{
        float_t fractional_dollar;
	int     fraction, n, tmp;
	float_t floored, rest;

	fractional_dollar = value_get_as_float (s->a.args[0]) ;
	fraction = value_get_as_int (s->a.args[1]) ;

	if (fraction <= 0) {
                s->error_string = _("#NUM!") ;
                return NULL;
	}

	tmp = fraction;
	/* Count digits in fraction */
	for (n=0; tmp; n++)
	        tmp /= 10;

	floored = floor (fractional_dollar);
	rest = fractional_dollar - floored;
	tmp = (int) (rest * fraction);

	return (FuncReturn *)value_new_float (floored + ((float_t) tmp / pow(10, n))) ;
}


static char *help_pv = {
	N_("@FUNCTION=PV\n"
	   "@SYNTAX=PV(rate,nper,pmt,fv,type)\n"
	   "@DESCRIPTION=Calculates the present value of an investment."
	   "@SEEALSO=FV")
};


static FuncReturn *
gnumeric_pv (FunctionEvalInfo *s)
{
	double rate,nper,pmt,fv;
	int type;
	
	double pvif,fvifa;

	rate = value_get_as_float (s->a.args[0]);
	nper = value_get_as_float (s->a.args[1]);
	pmt = value_get_as_float (s->a.args[2]);
	fv = value_get_as_float (s->a.args[3]);
	type = value_get_as_int (s->a.args[4]);

	/* Calculate the PVIF and FVIFA */

	pvif = calculate_pvif(rate,nper);

	fvifa = calculate_fvifa(rate,nper);

        return (FuncReturn *)value_new_float ( ( (-1.0) * fv - pmt * ( 1.0 + rate * type ) * fvifa ) / pvif );

}

static char *help_npv = {
	N_("@FUNCTION=NPV\n"
	   "@SYNTAX=NPV(rate,v1,v2,...)\n"
	   "@DESCRIPTION=Calculates the net present value of an investment."
	   "@SEEALSO=PV")
};

typedef struct {
        int first ;
        guint32 num ;
        float_t rate ;
        float_t sum ;
} financial_npv_t;

static int
callback_function_npv (Sheet *sheet, Value *value, char **error_string, void *closure)
{
        financial_npv_t *mm = closure;
 
        switch (value->type){
        case VALUE_INTEGER:
	        if (mm->first == TRUE)
		        mm->rate = value->v.v_int;
		else
		        mm->sum += value->v.v_int / pow(1+mm->rate, mm->num);
		mm->num++ ;
                break;
        case VALUE_FLOAT:
	        if (mm->first == TRUE)
		        mm->rate = value->v.v_float;
		else
		        mm->sum += value->v.v_float / pow(1+mm->rate, mm->num);
		mm->num++ ;
                break ;
        default:
                /* ignore strings */
                break;
        }
        mm->first = FALSE ;
        return TRUE;
}

static FuncReturn *
gnumeric_npv (FunctionEvalInfo *s)
{
        financial_npv_t p;

	p.first = TRUE;
	p.sum   = 0.0;
	p.num   = 0;

	function_iterate_argument_values (&s->pos, callback_function_npv,
                                          &p, s->a.nodes,
					  &s->error_string);
	return (FuncReturn *)value_new_float (p.sum);
}


static char *help_fv = {
	N_("@FUNCTION=FV\n"
	   "@SYNTAX=FV(rate,nper,pmt,pv,type)\n"
	   "@DESCRIPTION=Calculates the future value of an investment."
	   "@SEEALSO=PV,PMT,PPMT")
};


static FuncReturn *
gnumeric_fv (FunctionEvalInfo *s)
{
	double rate,nper,pv,pmt;
	int type;
	
	double pvif,fvifa;

	rate = value_get_as_float (s->a.args[0]);
	nper = value_get_as_float (s->a.args[1]);
	pmt = value_get_as_float (s->a.args[2]);
	pv = value_get_as_float (s->a.args[3]);
	type = value_get_as_int (s->a.args[4]);

	pvif = calculate_pvif(rate,nper);
	fvifa = calculate_fvifa(rate,nper);

        return (FuncReturn *)value_new_float ( -1.0 * ( ( pv * pvif ) + pmt * ( 1.0 + rate * type ) * fvifa )  );

}



static char *help_pmt = {
	N_("@FUNCTION=PMT\n"
	   "@SYNTAX=PMT(rate,nper,pv,fv,type)\n"
	   "@DESCRIPTION=Calculates the present value of an investment."
	   "@SEEALSO=PPMT,PV,FV")
};

static FuncReturn *
gnumeric_pmt (FunctionEvalInfo *s)
{
	double rate,pv,fv,nper;
	int type;
	
	rate = value_get_as_float (s->a.args[0]);
	nper = value_get_as_float (s->a.args[1]);
	pv = value_get_as_float (s->a.args[2]);
	fv = value_get_as_float (s->a.args[3]);
	type = value_get_as_int (s->a.args[4]);

        return (FuncReturn *)value_new_float ( calculate_pmt(rate,nper,pv,fv,type) );

}


static char *help_ipmt = {
	N_("@FUNCTION=IPMT\n"
	   "@SYNTAX=IPMT(rate,per,nper,pv,fv,type)\n"
	   "@DESCRIPTION=Calculates the amount of a payment of an annuity going "
	   "towards interest."
	   "\n"
	   "Formula for IPMT is:\n"
	   "\n"	
	   "IPMT(PER) = PMT - PRINCIPAL(PER-1) * INTEREST_RATE" 
	   "\n"
	   "where:"
	   "\n"
	   "PMT = Payment received on annuity"
	   "PRINCIPA(per-1) = amount of the remaining principal from last period"
	   "\n"
	   "@SEEALSO=PPMT,PV,FV")
};

static FuncReturn *
gnumeric_ipmt (FunctionEvalInfo *s)
{
	double rate,nper,per,pv,fv;
	double pmt;
	int type;
	
	rate = value_get_as_float (s->a.args[0]);
	nper = value_get_as_float (s->a.args[1]);
	per = value_get_as_float (s->a.args[2]);
	pv = value_get_as_float (s->a.args[3]);
	fv = value_get_as_float (s->a.args[4]);
	type = value_get_as_int (s->a.args[5]);

	/* First calculate the payment */

        pmt = calculate_pmt(rate,nper,pv,fv,type);

	/* Now we need to calculate the amount of money going towards the 
	   principal */

	return (FuncReturn *)value_new_float ( calculate_principal(pv,pmt,rate,per-1) * rate * (-1.0)  );

}


static char *help_ppmt = {
	N_("@FUNCTION=PPMT\n"
	   "@SYNTAX=PPMT(rate,per,nper,pv,fv,type)\n"
	   "@DESCRIPTION=Calculates the amount of a payment of an annuity going "
	   "towards principal."
	   "\n"
	   "Formula for it is:"
	   "\n"
	   "PPMT(per) = PMT - IPMT(per)"
	   "\n"
	   "where:"
	   "\n"
	   "PMT = Payment received on annuity"
	   "IPMT(per) = amount of interest for period per"
	   "\n"
	   "@SEEALSO=IPMT,PV,FV")
};

static FuncReturn *
gnumeric_ppmt (FunctionEvalInfo *s)
{
	double rate,nper,per,pv,fv;
	double ipmt,pmt;
	int type;
	
	rate = value_get_as_float (s->a.args[0]);
	nper = value_get_as_float (s->a.args[1]);
	per = value_get_as_float (s->a.args[2]);
	pv = value_get_as_float (s->a.args[3]);
	fv = value_get_as_float (s->a.args[4]);
	type = value_get_as_int (s->a.args[5]);

	/* First calculate the payment */

        pmt = calculate_pmt(rate,nper,pv,fv,type);

	/* This piece of code was copied from gnumeric_ppmt */

	/* Now we need to calculate the amount of money going towards the 
	   principal */

	ipmt = ( calculate_principal(pv,pmt,rate,per-1) * rate * (-1.0)  );

	return (FuncReturn *)value_new_float ( pmt - ipmt );

}


static char *help_nper = {
	N_("@FUNCTION=NPER\n"
	   "@SYNTAX=NPER(rate,pmt,pv,fv,type)\n"
	   "@DESCRIPTION=Calculates number of periods of an investment."
	   "@SEEALSO=PPMT,PV,FV")
};

static FuncReturn *
gnumeric_nper (FunctionEvalInfo *s)
{
	double rate,pmt,pv,fv;
	int type;
	
	rate = value_get_as_float (s->a.args[0]);
	pmt = value_get_as_float (s->a.args[1]);
	pv = value_get_as_float (s->a.args[2]);
	fv = value_get_as_float (s->a.args[3]);
	type = value_get_as_int (s->a.args[4]);

        return (FuncReturn *)value_new_float ( log (( pmt * ( 1.0 + rate * type ) - fv * rate ) / ( pv * rate + pmt * ( 1.0 + rate * type ) ) ) / log ( 1.0 + rate ));

}



static char *help_duration = {
	N_("@FUNCTION=DURATION\n"
	   "@SYNTAX=DURATION(rate,pv,fv)\n"
	   "@DESCRIPTION=Calculates number of periods needed for an investment to "
	   "attain a desired value. This function is similar to FV and PV with a "
	   "difference that we do not need give the direction of cash flows e.g. "
	   "-100 for a cash outflow and +100 for a cash inflow."
	   "\n"
	   "@SEEALSO=PPMT,PV,FV")
};

static FuncReturn *
gnumeric_duration (FunctionEvalInfo *s)
{
	double rate,pv,fv;
	
	rate = value_get_as_float (s->a.args[0]);
	pv = value_get_as_float (s->a.args[1]);
	fv = value_get_as_float (s->a.args[2]);

	if ( rate < 0 ){
		s->error_string = _("duration - domain error");
		return NULL;
	}

        return (FuncReturn *)value_new_float (  log( fv / pv ) / log(1.0 + rate) );

}

void finance_functions_init()
{
	FunctionCategory *cat = function_get_category (_("Financial"));

	function_new (cat, "dollarde", "ff", "fractional_dollar,fraction", &help_dollarde, FUNCTION_ARGS, gnumeric_dollarde);
	function_new (cat, "dollarfr", "ff", "decimal_dollar,fraction", &help_dollarfr, FUNCTION_ARGS, gnumeric_dollarfr);
	function_new (cat, "effect", "ff",    "rate,nper",    &help_effect,   FUNCTION_ARGS, gnumeric_effect);
	function_new (cat, "nominal", "ff",    "rate,nper",    &help_nominal,   FUNCTION_ARGS, gnumeric_nominal);
        function_new (cat, "npv",      0,      "",             &help_npv,      FUNCTION_NODES, gnumeric_npv);
	function_new (cat, "sln", "fff", "cost,salvagevalue,life", &help_sln, FUNCTION_ARGS, gnumeric_sln);
	function_new (cat, "syd", "ffff", "cost,salvagevalue,life,period", &help_syd, FUNCTION_ARGS, gnumeric_syd);
	function_new (cat, "pv", "ffffi", "rate,nper,pmt,fv,type", &help_pv, FUNCTION_ARGS, gnumeric_pv);	
	function_new (cat, "fv", "ffffi", "rate,nper,pmt,pv,type", &help_fv, FUNCTION_ARGS, gnumeric_fv);	
	function_new (cat, "pmt", "ffffi", "rate,nper,pv,fv,type", &help_pmt, FUNCTION_ARGS, gnumeric_pmt);
	function_new (cat, "ipmt", "fffffi", "rate,per,nper,pv,fv,type", &help_ipmt, FUNCTION_ARGS, gnumeric_ipmt);
	function_new (cat, "ppmt", "fffffi", "rate,per,nper,pv,fv,type", &help_ppmt, FUNCTION_ARGS, gnumeric_ppmt);
	function_new (cat, "nper", "ffffi", "rate,pmt,pv,fv,type", &help_nper, FUNCTION_ARGS, gnumeric_nper);
	function_new (cat, "duration", "fff", "rate,pv,fv", &help_duration, FUNCTION_ARGS, gnumeric_duration);
}
