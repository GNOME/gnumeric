
/*  A Bison parser, made from lp_rlpt.y
    by GNU Bison version 1.28  */

#define YYBISON 1  /* Identify Bison output.  */

#define	VAR	257
#define	CONS	258
#define	INTCONS	259
#define	VARIABLECOLON	260
#define	INF	261
#define	FR	262
#define	SEC_INT	263
#define	SEC_SEC	264
#define	SEC_SOS	265
#define	SOSTYPE	266
#define	SIGN	267
#define	RE_OPLE	268
#define	RE_OPGE	269
#define	MINIMISE	270
#define	MAXIMISE	271
#define	SUBJECTTO	272
#define	BOUNDS	273
#define	END	274
#define	UNDEFINED	275


#include <string.h>
#include <ctype.h>

#include "lpkit.h"
#include "yacc_read.h"

static char Last_var[NAMELEN], Last_var0[NAMELEN];
static REAL f, f0;
static int x;
static int Sign;
static int isign, isign0;      /* internal_sign variable to make sure nothing goes wrong */
		/* with lookahead */
static int make_neg;   /* is true after the relational operator is seen in order */
		/* to remember if lin_term stands before or after re_op */
static int Within_gen_decl = FALSE; /* TRUE when we are within an gen declaration */
static int Within_bin_decl = FALSE; /* TRUE when we are within an bin declaration */
static int Within_sec_decl = FALSE; /* TRUE when we are within an sec declaration */
static int Within_sos_decl = FALSE; /* TRUE when we are within an sos declaration */
static short SOStype; /* SOS type */
static int SOSNr;
static int weight; /* SOS weight */
static int SOSweight = 0; /* SOS weight */

static int HadConstraint;
static int HadVar;
static int Had_lineair_sum;

#define YY_FATAL_ERROR lex_fatal_error

/* let's please C++ users */
#ifdef __cplusplus
extern "C" {
#endif

static int wrap(void)
{
  return(1);
}

#ifdef __cplusplus
};
#endif

#define lpt_yywrap wrap
#define lpt_yyerror read_error

#include "lp_rlpt.h"

#ifndef YYSTYPE
#define YYSTYPE int
#endif
#include <stdio.h>

#ifndef __cplusplus
#ifndef __STDC__
#define const
#endif
#endif



#define	YYFINAL		129
#define	YYFLAG		-32768
#define	YYNTBASE	22

#define YYTRANSLATE(x) ((unsigned)(x) <= 275 ? lpt_yytranslate[x] : 83)

static const char lpt_yytranslate[] = {     0,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     1,     3,     4,     5,     6,
     7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
    17,    18,    19,    20,    21
};

#if YYDEBUG != 0
static const short lpt_yyprhs[] = {     0,
     0,     1,     2,     9,    12,    15,    17,    18,    22,    24,
    25,    29,    31,    34,    36,    37,    41,    42,    48,    50,
    53,    54,    59,    64,    66,    68,    71,    73,    75,    78,
    80,    83,    84,    89,    90,    91,    92,    93,    94,   106,
   107,   112,   113,   114,   120,   122,   123,   128,   130,   132,
   134,   136,   138,   140,   144,   146,   149,   151,   154,   156,
   158,   159,   163,   165,   167,   169,   172,   173,   177,   179,
   181,   183,   186,   187,   191,   193,   195,   197,   200,   203,
   205,   207,   209,   212,   213,   217,   219,   221,   223
};

static const short lpt_yyrhs[] = {    -1,
     0,    24,    25,    29,    41,    60,    82,     0,    17,    26,
     0,    16,    26,     0,    28,     0,     0,     6,    27,    28,
     0,    36,     0,     0,    18,    30,    31,     0,    32,     0,
    31,    32,     0,    34,     0,     0,     6,    33,    34,     0,
     0,    36,    39,    35,    40,    57,     0,    37,     0,    36,
    37,     0,     0,    58,    81,    38,    59,     0,    58,    56,
    81,    59,     0,    14,     0,    15,     0,    58,    56,     0,
     7,     0,    22,     0,    19,    42,     0,    43,     0,    42,
    43,     0,     0,    81,    44,    59,    50,     0,     0,     0,
     0,     0,     0,    40,    45,    39,    46,    81,    47,    59,
    48,    57,    49,    54,     0,     0,    39,    51,    40,    57,
     0,     0,     0,     8,    52,    57,    53,    57,     0,    22,
     0,     0,    39,    55,    40,    57,     0,     5,     0,     4,
     0,    22,     0,    22,     0,    13,     0,    22,     0,    66,
    70,    74,     0,    63,     0,    61,    63,     0,    64,     0,
    62,    64,     0,    81,     0,    81,     0,     0,     6,    65,
     5,     0,    22,     0,    67,     0,    68,     0,    67,    68,
     0,     0,     9,    69,    61,     0,    22,     0,    71,     0,
    72,     0,    71,    72,     0,     0,    10,    73,    61,     0,
    22,     0,    75,     0,    76,     0,    75,    76,     0,    11,
    77,     0,    22,     0,    78,     0,    79,     0,    78,    79,
     0,     0,    12,    80,    62,     0,     3,     0,     8,     0,
    22,     0,    20,     0
};

#endif

#if YYDEBUG != 0
static const short lpt_yyrline[] = { 0,
    65,    68,    77,    93,    99,   106,   107,   114,   116,   149,
   154,   159,   160,   164,   165,   172,   174,   182,   194,   195,
   199,   205,   205,   211,   211,   214,   216,   240,   241,   245,
   246,   250,   256,   257,   263,   269,   274,   279,   284,   293,
   300,   310,   319,   329,   341,   342,   350,   361,   361,   364,
   375,   379,   385,   403,   408,   409,   413,   414,   418,   424,
   431,   436,   442,   444,   447,   448,   452,   457,   459,   461,
   464,   465,   469,   474,   477,   479,   482,   483,   487,   491,
   493,   496,   498,   502,   518,   526,   526,   531,   532
};
#endif


#if YYDEBUG != 0 || defined (YYERROR_VERBOSE)

static const char * const lpt_yytname[] = {   "$","error","$undefined.","VAR","CONS",
"INTCONS","VARIABLECOLON","INF","FR","SEC_INT","SEC_SEC","SEC_SOS","SOSTYPE",
"SIGN","RE_OPLE","RE_OPGE","MINIMISE","MAXIMISE","SUBJECTTO","BOUNDS","END",
"UNDEFINED","EMPTY","inputfile","@1","objective_function","of","@2","real_of",
"x_constraints","@3","constraints","constraint","@4","real_constraint","@5",
"lineair_sum","lineair_term","@6","RE_OP","cons_term","bounds","x_bounds","bound",
"@7","@8","@9","@10","@11","@12","bound2","@13","@14","@15","optionalbound",
"@16","REALCONS","RHS_STORE","x_SIGN","VAR_STORE","int_sec_sos_declarations",
"VARIABLES","SOSVARIABLES","ONEVARIABLE","ONESOSVARIABLE","@17","x_int_declarations",
"int_declarations","int_declaration","@18","x_sec_declarations","sec_declarations",
"sec_declaration","@19","x_sos_declarations","sos_declarations","sos_declaration",
"x_single_sos_declarations","single_sos_declarations","single_sos_declaration",
"@20","VARIABLE","end", NULL
};
#endif

static const short lpt_yyr1[] = {     0,
    22,    24,    23,    25,    25,    26,    27,    26,    28,    30,
    29,    31,    31,    32,    33,    32,    35,    34,    36,    36,
    38,    37,    37,    39,    39,    40,    40,    41,    41,    42,
    42,    44,    43,    45,    46,    47,    48,    49,    43,    51,
    50,    52,    53,    50,    54,    55,    54,    56,    56,    57,
    58,    58,    59,    60,    61,    61,    62,    62,    63,    64,
    65,    64,    66,    66,    67,    67,    69,    68,    70,    70,
    71,    71,    73,    72,    74,    74,    75,    75,    76,    77,
    77,    78,    78,    80,    79,    81,    81,    82,    82
};

static const short lpt_yyr2[] = {     0,
     0,     0,     6,     2,     2,     1,     0,     3,     1,     0,
     3,     1,     2,     1,     0,     3,     0,     5,     1,     2,
     0,     4,     4,     1,     1,     2,     1,     1,     2,     1,
     2,     0,     4,     0,     0,     0,     0,     0,    11,     0,
     4,     0,     0,     5,     1,     0,     4,     1,     1,     1,
     1,     1,     1,     3,     1,     2,     1,     2,     1,     1,
     0,     3,     1,     1,     1,     2,     0,     3,     1,     1,
     1,     2,     0,     3,     1,     1,     1,     2,     2,     1,
     1,     1,     2,     0,     3,     1,     1,     1,     1
};

static const short lpt_yydefact[] = {     2,
     0,     1,     1,     0,     7,    52,    51,     5,     6,     1,
    19,     0,     4,    10,     1,     1,    20,    86,    49,    48,
    87,     0,    21,     1,     1,    28,     1,     8,     1,     1,
    15,    11,    12,    14,     1,    27,    34,    29,    30,     0,
    32,    67,    63,     1,     1,    64,    65,    53,    23,    22,
     1,    13,    24,    25,    17,     0,    31,    26,     1,     0,
    89,    88,     3,    73,    69,     1,    70,    71,    66,    16,
     1,    35,     0,    68,    55,    59,     0,     1,    75,    54,
    76,    77,    72,     1,     0,    42,    40,    33,    56,    74,
    84,    80,    79,    81,    82,    78,    50,    18,    36,     1,
     1,     0,    83,     1,    43,     1,    61,    85,    57,    60,
    37,     1,    41,     0,    58,     1,    44,    62,    38,     1,
    45,    46,    39,     1,     1,    47,     0,     0,     0
};

static const short lpt_yydefgoto[] = {     7,
   127,     1,     4,     8,    16,     9,    15,    24,    32,    33,
    51,    34,    71,    10,    11,    30,    55,    37,    27,    38,
    39,    59,    56,    85,   104,   116,   120,    88,   101,   100,
   112,   123,   124,    22,    98,    12,    49,    44,    74,   108,
    75,   109,   114,    45,    46,    47,    60,    66,    67,    68,
    77,    80,    81,    82,    93,    94,    95,   102,    76,    63
};

static const short lpt_yypact[] = {-32768,
    83,     4,     4,   -14,-32768,-32768,-32768,-32768,-32768,    53,
-32768,    56,-32768,-32768,   -12,     9,-32768,-32768,-32768,-32768,
-32768,    64,-32768,    37,    34,-32768,    15,-32768,-32768,-32768,
-32768,    49,-32768,-32768,    80,-32768,-32768,    70,-32768,   102,
-32768,-32768,-32768,    26,    38,    15,-32768,-32768,-32768,-32768,
     9,-32768,-32768,-32768,-32768,    94,-32768,-32768,-32768,    64,
-32768,-32768,-32768,-32768,-32768,    45,    38,-32768,-32768,-32768,
    12,-32768,    25,    64,-32768,-32768,    64,    68,-32768,-32768,
    45,-32768,-32768,-32768,    64,-32768,-32768,-32768,-32768,    64,
-32768,-32768,-32768,    68,-32768,-32768,-32768,-32768,-32768,-32768,
    12,    84,-32768,-32768,-32768,-32768,-32768,    84,-32768,-32768,
-32768,-32768,-32768,    60,-32768,-32768,-32768,-32768,-32768,    94,
-32768,-32768,-32768,    12,-32768,-32768,    82,    86,-32768
};

static const short lpt_yypgoto[] = {   -15,
-32768,-32768,-32768,    65,-32768,    72,-32768,-32768,-32768,    52,
-32768,    61,-32768,   -23,     3,-32768,   -50,   -66,-32768,-32768,
    58,-32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768,
-32768,-32768,-32768,    63,   -80,   -22,   -28,-32768,    36,-32768,
   -63,     6,-32768,-32768,-32768,    69,-32768,-32768,-32768,    44,
-32768,-32768,-32768,    35,-32768,-32768,    23,-32768,    -4,-32768
};


#define	YYLAST		117


static const short lpt_yytable[] = {    26,
    35,    50,    40,    14,    84,    72,    25,    23,    35,     5,
    89,    43,    17,    48,    48,    40,     6,    29,    36,   105,
    41,     6,    87,    42,     6,   113,    89,    35,    62,    65,
    73,   117,    86,    41,   106,   119,    18,    17,    53,    54,
    36,    21,    31,    48,   126,    61,     6,    64,    40,     6,
    79,    -1,    -1,    -1,    31,    78,    -1,   125,    18,    19,
    20,     6,    92,    21,   118,     6,    18,    13,    97,   122,
    -9,    21,    18,    -1,    -1,   111,    36,    21,    40,    91,
    99,   128,     6,    52,    97,   129,    18,    28,    48,   107,
    97,    21,     6,    53,    54,    57,    97,   110,     2,     3,
    97,    40,    58,   110,   121,    19,    20,    53,    54,    97,
    83,    70,    90,   115,    69,    96,   103
};

static const short lpt_yycheck[] = {    15,
    24,    30,    25,    18,    71,    56,    19,    12,    32,     6,
    74,    27,    10,    29,    30,    38,    13,    22,     7,   100,
    25,    13,    73,     9,    13,   106,    90,    51,    44,    45,
    59,   112,     8,    38,   101,   116,     3,    35,    14,    15,
     7,     8,     6,    59,   125,    20,    13,    10,    71,    13,
    66,     3,     4,     5,     6,    11,     8,   124,     3,     4,
     5,    13,    78,     8,     5,    13,     3,     3,    84,   120,
    18,     8,     3,     4,     5,   104,     7,     8,   101,    12,
    85,     0,    13,    32,   100,     0,     3,    16,   104,     6,
   106,     8,    13,    14,    15,    38,   112,   102,    16,    17,
   116,   124,    40,   108,   120,     4,     5,    14,    15,   125,
    67,    51,    77,   108,    46,    81,    94
};
/* -*-C-*-  Note some compilers choke on comments on `#line' lines.  */

/* This file comes from bison-1.28.  */

/* Skeleton output parser for bison,
   Copyright (C) 1984, 1989, 1990 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* As a special exception, when this file is copied by Bison into a
   Bison output file, you may use that output file without restriction.
   This special exception was added by the Free Software Foundation
   in version 1.24 of Bison.  */

/* This is the parser code that is written into each bison parser
  when the %semantic_parser declaration is not specified in the grammar.
  It was written by Richard Stallman by simplifying the hairy parser
  used when %semantic_parser is specified.  */

#ifndef YYSTACK_USE_ALLOCA
#ifdef alloca
#define YYSTACK_USE_ALLOCA
#else /* alloca not defined */
#ifdef __GNUC__
#define YYSTACK_USE_ALLOCA
#define alloca __builtin_alloca
#else /* not GNU C.  */
#if (!defined (__STDC__) && defined (sparc)) || defined (__sparc__) || defined (__sparc) || defined (__sgi) || (defined (__sun) && defined (__i386))
#define YYSTACK_USE_ALLOCA
#include <alloca.h>
#else /* not sparc */
/* We think this test detects Watcom and Microsoft C.  */
/* This used to test MSDOS, but that is a bad idea
   since that symbol is in the user namespace.  */
#if (defined (_MSDOS) || defined (_MSDOS_)) && !defined (__TURBOC__)
#if 0 /* No need for malloc.h, which pollutes the namespace;
	 instead, just don't use alloca.  */
#include <malloc.h>
#endif
#else /* not MSDOS, or __TURBOC__ */
#if defined(_AIX)
/* I don't know what this was needed for, but it pollutes the namespace.
   So I turned it off.   rms, 2 May 1997.  */
/* #include <malloc.h>  */
 #pragma alloca
#define YYSTACK_USE_ALLOCA
#else /* not MSDOS, or __TURBOC__, or _AIX */
#if 0
#ifdef __hpux /* haible@ilog.fr says this works for HPUX 9.05 and up,
		 and on HPUX 10.  Eventually we can turn this on.  */
#define YYSTACK_USE_ALLOCA
#define alloca __builtin_alloca
#endif /* __hpux */
#endif
#endif /* not _AIX */
#endif /* not MSDOS, or __TURBOC__ */
#endif /* not sparc */
#endif /* not GNU C */
#endif /* alloca not defined */
#endif /* YYSTACK_USE_ALLOCA not defined */

#ifdef YYSTACK_USE_ALLOCA
#define YYSTACK_ALLOC alloca
#else
#define YYSTACK_ALLOC malloc
#endif

/* Note: there must be only one dollar sign in this file.
   It is replaced by the list of actions, each action
   as one case of the switch.  */

#define lpt_yyerrok		(lpt_yyerrstatus = 0)
#define lpt_yyclearin	(lpt_yychar = YYEMPTY)
#define YYEMPTY		-2
#define YYEOF		0
#define YYACCEPT	goto lpt_yyacceptlab
#define YYABORT 	goto lpt_yyabortlab
#define YYERROR		goto lpt_yyerrlab1
/* Like YYERROR except do call lpt_yyerror.
   This remains here temporarily to ease the
   transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */
#define YYFAIL		goto lpt_yyerrlab
#define YYRECOVERING()  (!!lpt_yyerrstatus)
#define YYBACKUP(token, value) \
do								\
  if (lpt_yychar == YYEMPTY && lpt_yylen == 1)				\
    { lpt_yychar = (token), lpt_yylval = (value);			\
      lpt_yychar1 = YYTRANSLATE (lpt_yychar);				\
      YYPOPSTACK;						\
      goto lpt_yybackup;						\
    }								\
  else								\
    { lpt_yyerror ("syntax error: cannot back up"); YYERROR; }	\
while (0)

#define YYTERROR	1
#define YYERRCODE	256

#ifndef YYPURE
#define YYLEX		lpt_yylex()
#endif

#ifdef YYPURE
#ifdef YYLSP_NEEDED
#ifdef YYLEX_PARAM
#define YYLEX		lpt_yylex(&lpt_yylval, &lpt_yylloc, YYLEX_PARAM)
#else
#define YYLEX		lpt_yylex(&lpt_yylval, &lpt_yylloc)
#endif
#else /* not YYLSP_NEEDED */
#ifdef YYLEX_PARAM
#define YYLEX		lpt_yylex(&lpt_yylval, YYLEX_PARAM)
#else
#define YYLEX		lpt_yylex(&lpt_yylval)
#endif
#endif /* not YYLSP_NEEDED */
#endif

/* If nonreentrant, generate the variables here */

#ifndef YYPURE

int	lpt_yychar;			/*  the lookahead symbol		*/
YYSTYPE	lpt_yylval;			/*  the semantic value of the		*/
				/*  lookahead symbol			*/

#ifdef YYLSP_NEEDED
YYLTYPE lpt_yylloc;			/*  location data for the lookahead	*/
				/*  symbol				*/
#endif

int lpt_yynerrs;			/*  number of parse errors so far       */
#endif  /* not YYPURE */

#if YYDEBUG != 0
int lpt_yydebug;			/*  nonzero means print parse trace	*/
/* Since this is uninitialized, it does not stop multiple parsers
   from coexisting.  */
#endif

/*  YYINITDEPTH indicates the initial size of the parser's stacks	*/

#ifndef	YYINITDEPTH
#define YYINITDEPTH 200
#endif

/*  YYMAXDEPTH is the maximum size the stacks can grow to
    (effective only if the built-in stack extension method is used).  */

#if YYMAXDEPTH == 0
#undef YYMAXDEPTH
#endif

#ifndef YYMAXDEPTH
#define YYMAXDEPTH 10000
#endif

/* Define __lpt_yy_memcpy.  Note that the size argument
   should be passed with type unsigned int, because that is what the non-GCC
   definitions require.  With GCC, __builtin_memcpy takes an arg
   of type size_t, but it can handle unsigned int.  */

#if __GNUC__ > 1		/* GNU C and GNU C++ define this.  */
#define __lpt_yy_memcpy(TO,FROM,COUNT)	__builtin_memcpy(TO,FROM,COUNT)
#else				/* not GNU C or C++ */
#ifndef __cplusplus

/* This is the most reliable way to avoid incompatibilities
   in available built-in functions on various systems.  */
static void
__lpt_yy_memcpy (to, from, count)
     char *to;
     char *from;
     unsigned int count;
{
  register char *f = from;
  register char *t = to;
  register int i = count;

  while (i-- > 0)
    *t++ = *f++;
}

#else /* __cplusplus */

/* This is the most reliable way to avoid incompatibilities
   in available built-in functions on various systems.  */
static void
__lpt_yy_memcpy (char *to, char *from, unsigned int count)
{
  register char *t = to;
  register char *f = from;
  register int i = count;

  while (i-- > 0)
    *t++ = *f++;
}

#endif
#endif



/* The user can define YYPARSE_PARAM as the name of an argument to be passed
   into lpt_yyparse.  The argument should have type void *.
   It should actually point to an object.
   Grammar actions can access the variable by casting it
   to the proper pointer type.  */

#ifdef YYPARSE_PARAM
#ifdef __cplusplus
#define YYPARSE_PARAM_ARG void *YYPARSE_PARAM
#define YYPARSE_PARAM_DECL
#else /* not __cplusplus */
#define YYPARSE_PARAM_ARG YYPARSE_PARAM
#define YYPARSE_PARAM_DECL void *YYPARSE_PARAM;
#endif /* not __cplusplus */
#else /* not YYPARSE_PARAM */
#define YYPARSE_PARAM_ARG
#define YYPARSE_PARAM_DECL
#endif /* not YYPARSE_PARAM */

/* Prevent warning if -Wstrict-prototypes.  */
#ifdef __GNUC__
#ifdef YYPARSE_PARAM
int lpt_yyparse (void *);
#else
int lpt_yyparse (void);
#endif
#endif

int
lpt_yyparse(YYPARSE_PARAM_ARG)
     YYPARSE_PARAM_DECL
{
  register int lpt_yystate;
  register int lpt_yyn;
  register short *lpt_yyssp;
  register YYSTYPE *lpt_yyvsp;
  int lpt_yyerrstatus;	/*  number of tokens to shift before error messages enabled */
  int lpt_yychar1 = 0;		/*  lookahead token as an internal (translated) token number */

  short	lpt_yyssa[YYINITDEPTH];	/*  the state stack			*/
  YYSTYPE lpt_yyvsa[YYINITDEPTH];	/*  the semantic value stack		*/

  short *lpt_yyss = lpt_yyssa;		/*  refer to the stacks thru separate pointers */
  YYSTYPE *lpt_yyvs = lpt_yyvsa;	/*  to allow lpt_yyoverflow to reallocate them elsewhere */

#ifdef YYLSP_NEEDED
  YYLTYPE lpt_yylsa[YYINITDEPTH];	/*  the location stack			*/
  YYLTYPE *lpt_yyls = lpt_yylsa;
  YYLTYPE *lpt_yylsp;

#define YYPOPSTACK   (lpt_yyvsp--, lpt_yyssp--, lpt_yylsp--)
#else
#define YYPOPSTACK   (lpt_yyvsp--, lpt_yyssp--)
#endif

  int lpt_yystacksize = YYINITDEPTH;
  int lpt_yyfree_stacks = 0;

#ifdef YYPURE
  int lpt_yychar;
  YYSTYPE lpt_yylval;
  int lpt_yynerrs;
#ifdef YYLSP_NEEDED
  YYLTYPE lpt_yylloc;
#endif
#endif

  YYSTYPE lpt_yyval = 0;		/*  the variable used to return		*/
				/*  semantic values from the action	*/
				/*  routines				*/

  int lpt_yylen;

#if YYDEBUG != 0
  if (lpt_yydebug)
    fprintf(stderr, "Starting parse\n");
#endif

  lpt_yystate = 0;
  lpt_yyerrstatus = 0;
  lpt_yynerrs = 0;
  lpt_yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  lpt_yyssp = lpt_yyss - 1;
  lpt_yyvsp = lpt_yyvs;
#ifdef YYLSP_NEEDED
  lpt_yylsp = lpt_yyls;
#endif

/* Push a new state, which is found in  lpt_yystate  .  */
/* In all cases, when you get here, the value and location stacks
   have just been pushed. so pushing a state here evens the stacks.  */
lpt_yynewstate:

  *++lpt_yyssp = lpt_yystate;

  if (lpt_yyssp >= lpt_yyss + lpt_yystacksize - 1)
    {
      /* Give user a chance to reallocate the stack */
      /* Use copies of these so that the &'s don't force the real ones into memory. */
      YYSTYPE *lpt_yyvs1 = lpt_yyvs;
      short *lpt_yyss1 = lpt_yyss;
#ifdef YYLSP_NEEDED
      YYLTYPE *lpt_yyls1 = lpt_yyls;
#endif

      /* Get the current used size of the three stacks, in elements.  */
      int size = lpt_yyssp - lpt_yyss + 1;

#ifdef lpt_yyoverflow
      /* Each stack pointer address is followed by the size of
	 the data in use in that stack, in bytes.  */
#ifdef YYLSP_NEEDED
      /* This used to be a conditional around just the two extra args,
	 but that might be undefined if lpt_yyoverflow is a macro.  */
      lpt_yyoverflow("parser stack overflow",
		 &lpt_yyss1, size * sizeof (*lpt_yyssp),
		 &lpt_yyvs1, size * sizeof (*lpt_yyvsp),
		 &lpt_yyls1, size * sizeof (*lpt_yylsp),
		 &lpt_yystacksize);
#else
      lpt_yyoverflow("parser stack overflow",
		 &lpt_yyss1, size * sizeof (*lpt_yyssp),
		 &lpt_yyvs1, size * sizeof (*lpt_yyvsp),
		 &lpt_yystacksize);
#endif

      lpt_yyss = lpt_yyss1; lpt_yyvs = lpt_yyvs1;
#ifdef YYLSP_NEEDED
      lpt_yyls = lpt_yyls1;
#endif
#else /* no lpt_yyoverflow */
      /* Extend the stack our own way.  */
      if (lpt_yystacksize >= YYMAXDEPTH)
	{
	  lpt_yyerror("parser stack overflow");
	  if (lpt_yyfree_stacks)
	    {
	      free (lpt_yyss);
	      free (lpt_yyvs);
#ifdef YYLSP_NEEDED
	      free (lpt_yyls);
#endif
	    }
	  return 2;
	}
      lpt_yystacksize *= 2;
      if (lpt_yystacksize > YYMAXDEPTH)
	lpt_yystacksize = YYMAXDEPTH;
#ifndef YYSTACK_USE_ALLOCA
      lpt_yyfree_stacks = 1;
#endif
      lpt_yyss = (short *) YYSTACK_ALLOC (lpt_yystacksize * sizeof (*lpt_yyssp));
      __lpt_yy_memcpy ((char *)lpt_yyss, (char *)lpt_yyss1,
		   size * (unsigned int) sizeof (*lpt_yyssp));
      lpt_yyvs = (YYSTYPE *) YYSTACK_ALLOC (lpt_yystacksize * sizeof (*lpt_yyvsp));
      __lpt_yy_memcpy ((char *)lpt_yyvs, (char *)lpt_yyvs1,
		   size * (unsigned int) sizeof (*lpt_yyvsp));
#ifdef YYLSP_NEEDED
      lpt_yyls = (YYLTYPE *) YYSTACK_ALLOC (lpt_yystacksize * sizeof (*lpt_yylsp));
      __lpt_yy_memcpy ((char *)lpt_yyls, (char *)lpt_yyls1,
		   size * (unsigned int) sizeof (*lpt_yylsp));
#endif
#endif /* no lpt_yyoverflow */

      lpt_yyssp = lpt_yyss + size - 1;
      lpt_yyvsp = lpt_yyvs + size - 1;
#ifdef YYLSP_NEEDED
      lpt_yylsp = lpt_yyls + size - 1;
#endif

#if YYDEBUG != 0
      if (lpt_yydebug)
	fprintf(stderr, "Stack size increased to %d\n", lpt_yystacksize);
#endif

      if (lpt_yyssp >= lpt_yyss + lpt_yystacksize - 1)
	YYABORT;
    }

#if YYDEBUG != 0
  if (lpt_yydebug)
    fprintf(stderr, "Entering state %d\n", lpt_yystate);
#endif

  goto lpt_yybackup;
 lpt_yybackup:

/* Do appropriate processing given the current state.  */
/* Read a lookahead token if we need one and don't already have one.  */
/* lpt_yyresume: */

  /* First try to decide what to do without reference to lookahead token.  */

  lpt_yyn = lpt_yypact[lpt_yystate];
  if (lpt_yyn == YYFLAG)
    goto lpt_yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* lpt_yychar is either YYEMPTY or YYEOF
     or a valid token in external form.  */

  if (lpt_yychar == YYEMPTY)
    {
#if YYDEBUG != 0
      if (lpt_yydebug)
	fprintf(stderr, "Reading a token: ");
#endif
      lpt_yychar = YYLEX;
    }

  /* Convert token to internal form (in lpt_yychar1) for indexing tables with */

  if (lpt_yychar <= 0)		/* This means end of input. */
    {
      lpt_yychar1 = 0;
      lpt_yychar = YYEOF;		/* Don't call YYLEX any more */

#if YYDEBUG != 0
      if (lpt_yydebug)
	fprintf(stderr, "Now at end of input.\n");
#endif
    }
  else
    {
      lpt_yychar1 = YYTRANSLATE(lpt_yychar);

#if YYDEBUG != 0
      if (lpt_yydebug)
	{
	  fprintf (stderr, "Next token is %d (%s", lpt_yychar, lpt_yytname[lpt_yychar1]);
	  /* Give the individual parser a way to print the precise meaning
	     of a token, for further debugging info.  */
#ifdef YYPRINT
	  YYPRINT (stderr, lpt_yychar, lpt_yylval);
#endif
	  fprintf (stderr, ")\n");
	}
#endif
    }

  lpt_yyn += lpt_yychar1;
  if (lpt_yyn < 0 || lpt_yyn > YYLAST || lpt_yycheck[lpt_yyn] != lpt_yychar1)
    goto lpt_yydefault;

  lpt_yyn = lpt_yytable[lpt_yyn];

  /* lpt_yyn is what to do for this token type in this state.
     Negative => reduce, -lpt_yyn is rule number.
     Positive => shift, lpt_yyn is new state.
       New state is final state => don't bother to shift,
       just return success.
     0, or most negative number => error.  */

  if (lpt_yyn < 0)
    {
      if (lpt_yyn == YYFLAG)
	goto lpt_yyerrlab;
      lpt_yyn = -lpt_yyn;
      goto lpt_yyreduce;
    }
  else if (lpt_yyn == 0)
    goto lpt_yyerrlab;

  if (lpt_yyn == YYFINAL)
    YYACCEPT;

  /* Shift the lookahead token.  */

#if YYDEBUG != 0
  if (lpt_yydebug)
    fprintf(stderr, "Shifting token %d (%s), ", lpt_yychar, lpt_yytname[lpt_yychar1]);
#endif

  /* Discard the token being shifted unless it is eof.  */
  if (lpt_yychar != YYEOF)
    lpt_yychar = YYEMPTY;

  *++lpt_yyvsp = lpt_yylval;
#ifdef YYLSP_NEEDED
  *++lpt_yylsp = lpt_yylloc;
#endif

  /* count tokens shifted since error; after three, turn off error status.  */
  if (lpt_yyerrstatus) lpt_yyerrstatus--;

  lpt_yystate = lpt_yyn;
  goto lpt_yynewstate;

/* Do the default action for the current state.  */
lpt_yydefault:

  lpt_yyn = lpt_yydefact[lpt_yystate];
  if (lpt_yyn == 0)
    goto lpt_yyerrlab;

/* Do a reduction.  lpt_yyn is the number of a rule to reduce with.  */
lpt_yyreduce:
  lpt_yylen = lpt_yyr2[lpt_yyn];
  if (lpt_yylen > 0)
    lpt_yyval = lpt_yyvsp[1-lpt_yylen]; /* implement default value of the action */

#if YYDEBUG != 0
  if (lpt_yydebug)
    {
      int i;

      fprintf (stderr, "Reducing via rule %d (line %d), ",
	       lpt_yyn, lpt_yyrline[lpt_yyn]);

      /* Print the symbols being reduced, and their result.  */
      for (i = lpt_yyprhs[lpt_yyn]; lpt_yyrhs[i] > 0; i++)
	fprintf (stderr, "%s ", lpt_yytname[lpt_yyrhs[i]]);
      fprintf (stderr, " -> %s\n", lpt_yytname[lpt_yyr1[lpt_yyn]]);
    }
#endif


  switch (lpt_yyn) {

case 2:
{
  isign = 0;
  make_neg = 0;
  Sign = 0;
  HadConstraint = FALSE;
  HadVar = FALSE;
;
    break;}
case 4:
{
  set_obj_dir(TRUE);
;
    break;}
case 5:
{
  set_obj_dir(FALSE);
;
    break;}
case 7:
{
  if(!add_constraint_name(Last_var))
    YYABORT;
  /* HadConstraint = TRUE; */
;
    break;}
case 9:
{
  add_row();
  /* HadConstraint = FALSE; */
  HadVar = FALSE;
  isign = 0;
  make_neg = 0;
;
    break;}
case 10:
{
  HadConstraint = TRUE;
;
    break;}
case 11:
{
  HadConstraint = FALSE;
;
    break;}
case 15:
{
  if(!add_constraint_name(Last_var))
    YYABORT;
  /* HadConstraint = TRUE; */
;
    break;}
case 17:
{
  if(!store_re_op((char *) lpt_yytext, HadConstraint, HadVar, Had_lineair_sum))
    YYABORT;
  make_neg = 1;
;
    break;}
case 18:
{
  Had_lineair_sum = TRUE;
  add_row();
  /* HadConstraint = FALSE; */
  HadVar = FALSE;
  isign = 0;
  make_neg = 0;
  null_tmp_store(TRUE);
;
    break;}
case 21:
{
  f = 1.0;
;
    break;}
case 27:
{
  isign = Sign;
;
    break;}
case 32:
{
  f = 1.0;
  isign = 0;
;
    break;}
case 34:
{
  f0 = f;
  isign0 = isign;
;
    break;}
case 35:
{
  if(!store_re_op((char *) lpt_yytext, HadConstraint, HadVar, Had_lineair_sum))
    YYABORT;
  make_neg = 0;
;
    break;}
case 36:
{
  isign = 0;
  f = -1.0;
;
    break;}
case 37:
{
  isign = isign0;
  f = f0;
;
    break;}
case 38:
{
  if(!store_bounds(TRUE))
    YYABORT;
;
    break;}
case 39:
{
  /* HadConstraint = FALSE; */
  HadVar = FALSE;
  isign = 0;
  make_neg = 0;
  null_tmp_store(TRUE);
;
    break;}
case 40:
{
  if(!store_re_op((char *) lpt_yytext, HadConstraint, HadVar, Had_lineair_sum))
    YYABORT;
  make_neg = 1;
;
    break;}
case 41:
{
  if(!store_bounds(TRUE))
    YYABORT;
  /* HadConstraint = FALSE; */
  HadVar = FALSE;
  isign = 0;
  make_neg = 0;
  null_tmp_store(TRUE);
;
    break;}
case 42:
{
  if(!store_re_op(">", HadConstraint, HadVar, Had_lineair_sum))
    YYABORT;
  make_neg = 1;
  isign = 0;
  f = -DEF_INFINITE;
;
    break;}
case 43:
{
  if(!store_bounds(FALSE))
    YYABORT;

  if(!store_re_op("<", HadConstraint, HadVar, Had_lineair_sum))
    YYABORT;
  f = DEF_INFINITE;
  isign = 0;
;
    break;}
case 44:
{
  if(!store_bounds(FALSE))
    YYABORT;
  /* HadConstraint = FALSE; */
  HadVar = FALSE;
  isign = 0;
  make_neg = 0;
  null_tmp_store(TRUE);
;
    break;}
case 46:
{
  if(!store_re_op((*lpt_yytext == '<') ? ">" : (*lpt_yytext == '>') ? "<" : (char *) lpt_yytext, HadConstraint, HadVar, Had_lineair_sum))
    YYABORT;
  make_neg = 0;
  isign = 0;
;
    break;}
case 47:
{
  if(!store_bounds(TRUE))
    YYABORT;
;
    break;}
case 50:
{
  if (    (isign || !make_neg)
      && !(isign && !make_neg)) /* but not both! */
    f = -f;
  if(!rhs_store(f, HadConstraint, HadVar, Had_lineair_sum))
    YYABORT;
  isign = 0;
;
    break;}
case 51:
{
  isign = 0;
;
    break;}
case 52:
{
  isign = Sign;
;
    break;}
case 53:
{
  if (    (isign || make_neg)
      && !(isign && make_neg)) /* but not both! */
    f = -f;
  if(!var_store(Last_var, f, HadConstraint, HadVar, Had_lineair_sum)) {
    lpt_yyerror("var_store failed");
    YYABORT;
  }
  /* HadConstraint |= HadVar; */
  HadVar = TRUE;
  isign = 0;
;
    break;}
case 59:
{
  storevarandweight(Last_var);
;
    break;}
case 60:
{
  SOSNr++;
  weight = SOSNr;
  storevarandweight(Last_var);
  set_sos_weight(weight, 2);
;
    break;}
case 61:
{
  storevarandweight(Last_var);
;
    break;}
case 62:
{
  weight = (int) (f + .1);
  set_sos_weight(weight, 2);
;
    break;}
case 67:
{
  check_int_sec_sos_decl(Within_gen_decl ? 1 : Within_bin_decl ? 2 : 0, 0, 0);
;
    break;}
case 73:
{
  check_int_sec_sos_decl(0, 1, 0);
;
    break;}
case 84:
{
  char buf[16];

  check_int_sec_sos_decl(0, 0, 1);
  SOSweight++;
  sprintf(buf, "SOS%d", SOSweight);
  storevarandweight(buf);
  SOStype = Last_var[1] - '0';
  set_sos_type(SOStype);
  check_int_sec_sos_decl(0, 0, 2);
  weight = 0;
  SOSNr = 0;
;
    break;}
case 85:
{
  set_sos_weight(SOSweight, 1);
;
    break;}
}
   /* the action file gets copied in in place of this dollarsign */


  lpt_yyvsp -= lpt_yylen;
  lpt_yyssp -= lpt_yylen;
#ifdef YYLSP_NEEDED
  lpt_yylsp -= lpt_yylen;
#endif

#if YYDEBUG != 0
  if (lpt_yydebug)
    {
      short *ssp1 = lpt_yyss - 1;
      fprintf (stderr, "state stack now");
      while (ssp1 != lpt_yyssp)
	fprintf (stderr, " %d", *++ssp1);
      fprintf (stderr, "\n");
    }
#endif

  *++lpt_yyvsp = lpt_yyval;

#ifdef YYLSP_NEEDED
  lpt_yylsp++;
  if (lpt_yylen == 0)
    {
      lpt_yylsp->first_line = lpt_yylloc.first_line;
      lpt_yylsp->first_column = lpt_yylloc.first_column;
      lpt_yylsp->last_line = (lpt_yylsp-1)->last_line;
      lpt_yylsp->last_column = (lpt_yylsp-1)->last_column;
      lpt_yylsp->text = 0;
    }
  else
    {
      lpt_yylsp->last_line = (lpt_yylsp+lpt_yylen-1)->last_line;
      lpt_yylsp->last_column = (lpt_yylsp+lpt_yylen-1)->last_column;
    }
#endif

  /* Now "shift" the result of the reduction.
     Determine what state that goes to,
     based on the state we popped back to
     and the rule number reduced by.  */

  lpt_yyn = lpt_yyr1[lpt_yyn];

  lpt_yystate = lpt_yypgoto[lpt_yyn - YYNTBASE] + *lpt_yyssp;
  if (lpt_yystate >= 0 && lpt_yystate <= YYLAST && lpt_yycheck[lpt_yystate] == *lpt_yyssp)
    lpt_yystate = lpt_yytable[lpt_yystate];
  else
    lpt_yystate = lpt_yydefgoto[lpt_yyn - YYNTBASE];

  goto lpt_yynewstate;

lpt_yyerrlab:   /* here on detecting error */

  if (! lpt_yyerrstatus)
    /* If not already recovering from an error, report this error.  */
    {
      ++lpt_yynerrs;

#ifdef YYERROR_VERBOSE
      lpt_yyn = lpt_yypact[lpt_yystate];

      if (lpt_yyn > YYFLAG && lpt_yyn < YYLAST)
	{
	  int size = 0;
	  char *msg;
	  int x, count;

	  count = 0;
	  /* Start X at -lpt_yyn if nec to avoid negative indexes in lpt_yycheck.  */
	  for (x = (lpt_yyn < 0 ? -lpt_yyn : 0);
	       x < (sizeof(lpt_yytname) / sizeof(char *)); x++)
	    if (lpt_yycheck[x + lpt_yyn] == x)
	      size += strlen(lpt_yytname[x]) + 15, count++;
	  msg = (char *) malloc(size + 15);
	  if (msg != 0)
	    {
	      strcpy(msg, "parse error");

	      if (count < 5)
		{
		  count = 0;
		  for (x = (lpt_yyn < 0 ? -lpt_yyn : 0);
		       x < (sizeof(lpt_yytname) / sizeof(char *)); x++)
		    if (lpt_yycheck[x + lpt_yyn] == x)
		      {
			strcat(msg, count == 0 ? ", expecting `" : " or `");
			strcat(msg, lpt_yytname[x]);
			strcat(msg, "'");
			count++;
		      }
		}
	      lpt_yyerror(msg);
	      free(msg);
	    }
	  else
	    lpt_yyerror ("parse error; also virtual memory exceeded");
	}
      else
#endif /* YYERROR_VERBOSE */
	lpt_yyerror("parse error");
    }

  goto lpt_yyerrlab1;
lpt_yyerrlab1:   /* here on error raised explicitly by an action */

  if (lpt_yyerrstatus == 3)
    {
      /* if just tried and failed to reuse lookahead token after an error, discard it.  */

      /* return failure if at end of input */
      if (lpt_yychar == YYEOF)
	YYABORT;

#if YYDEBUG != 0
      if (lpt_yydebug)
	fprintf(stderr, "Discarding token %d (%s).\n", lpt_yychar, lpt_yytname[lpt_yychar1]);
#endif

      lpt_yychar = YYEMPTY;
    }

  /* Else will try to reuse lookahead token
     after shifting the error token.  */

  lpt_yyerrstatus = 3;		/* Each real token shifted decrements this */

  goto lpt_yyerrhandle;

lpt_yyerrdefault:  /* current state does not do anything special for the error token. */

#if 0
  /* This is wrong; only states that explicitly want error tokens
     should shift them.  */
  lpt_yyn = lpt_yydefact[lpt_yystate];  /* If its default is to accept any token, ok.  Otherwise pop it.*/
  if (lpt_yyn) goto lpt_yydefault;
#endif

lpt_yyerrpop:   /* pop the current state because it cannot handle the error token */

  if (lpt_yyssp == lpt_yyss) YYABORT;
  lpt_yyvsp--;
  lpt_yystate = *--lpt_yyssp;
#ifdef YYLSP_NEEDED
  lpt_yylsp--;
#endif

#if YYDEBUG != 0
  if (lpt_yydebug)
    {
      short *ssp1 = lpt_yyss - 1;
      fprintf (stderr, "Error: state stack now");
      while (ssp1 != lpt_yyssp)
	fprintf (stderr, " %d", *++ssp1);
      fprintf (stderr, "\n");
    }
#endif

lpt_yyerrhandle:

  lpt_yyn = lpt_yypact[lpt_yystate];
  if (lpt_yyn == YYFLAG)
    goto lpt_yyerrdefault;

  lpt_yyn += YYTERROR;
  if (lpt_yyn < 0 || lpt_yyn > YYLAST || lpt_yycheck[lpt_yyn] != YYTERROR)
    goto lpt_yyerrdefault;

  lpt_yyn = lpt_yytable[lpt_yyn];
  if (lpt_yyn < 0)
    {
      if (lpt_yyn == YYFLAG)
	goto lpt_yyerrpop;
      lpt_yyn = -lpt_yyn;
      goto lpt_yyreduce;
    }
  else if (lpt_yyn == 0)
    goto lpt_yyerrpop;

  if (lpt_yyn == YYFINAL)
    YYACCEPT;

#if YYDEBUG != 0
  if (lpt_yydebug)
    fprintf(stderr, "Shifting error token, ");
#endif

  *++lpt_yyvsp = lpt_yylval;
#ifdef YYLSP_NEEDED
  *++lpt_yylsp = lpt_yylloc;
#endif

  lpt_yystate = lpt_yyn;
  goto lpt_yynewstate;

 lpt_yyacceptlab:
  /* YYACCEPT comes here.  */
  if (lpt_yyfree_stacks)
    {
      free (lpt_yyss);
      free (lpt_yyvs);
#ifdef YYLSP_NEEDED
      free (lpt_yyls);
#endif
    }
  return 0;

 lpt_yyabortlab:
  /* YYABORT comes here.  */
  if (lpt_yyfree_stacks)
    {
      free (lpt_yyss);
      free (lpt_yyvs);
#ifdef YYLSP_NEEDED
      free (lpt_yyls);
#endif
    }
  return 1;
}


static void lpt_yy_delete_allocated_memory(void)
{
  /* free memory allocated by flex. Otherwise some memory is not freed.
     This is a bit tricky. There is not much documentation about this, but a lot of
     reports of memory that keeps allocated */

  /* If you get errors on this function call, just comment it. This will only result
     in some memory that is not being freed. */

# if defined YY_CURRENT_BUFFER
    /* flex defines the macro YY_CURRENT_BUFFER, so you should only get here if lp_rlpt.h is
       generated by flex */
    /* lex doesn't define this macro and thus should not come here, but lex doesn't has
       this memory leak also ...*/

    lpt_yy_delete_buffer(YY_CURRENT_BUFFER); /* comment this line if you have problems with it */
    lpt_yy_init = 1; /* make sure that the next time memory is allocated again */
    lpt_yy_start = 0;
# endif
}

static int parse(void)
{
  return(lpt_yyparse());
}

lprec * __WINAPI read_lpt(FILE *filename, int verbose, char *lp_name)
{
  lpt_yyin = filename;
  return(yacc_read(verbose, lp_name, &lpt_yylineno, parse, lpt_yy_delete_allocated_memory));
}

lprec * __WINAPI read_LPT(char *filename, int verbose, char *lp_name)
{
  FILE *fpin;
  lprec *lp = NULL;

  if((fpin = fopen(filename, "r")) != NULL) {
    lp = read_lpt(fpin, verbose, lp_name);
    fclose(fpin);
  }
  return(lp);
}
