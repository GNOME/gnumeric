/* format.c - attempts to emulate excel's number formatting ability.
 * Copyright (C) 1998 Chris Lahey
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>
#include <gnome.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "gnumeric.h"
#include "format.h"
#include <glib.h>

static int append_year( GString *string, gchar *format, struct tm *time_split );
static int append_month( GString *string, gchar *format, struct tm *time_split );
static int append_hour( GString *string, gchar *format, struct tm *time_split, int timeformat );
static int append_day( GString *string, gchar *format, struct tm *time_split );
static int append_minute( GString *string, gchar *format, struct tm *time_split );
static int append_second( GString *string, gchar *format, struct tm *time_split );
static int append_half( GString *string, gchar *format, struct tm *time_split );
static void style_entry_free( gpointer data, gpointer user_data );


/*
   The returned string is newly allocated.

   Current format is an optional date specification followed by an
   optional number specification.

   A date specification is an arbitrary sequence of characters (other
   than '#', '0', '?', or '.') which is copied to the output.  The
   standard date fields are substituted for.  If it ever finds an a or
   a p it lists dates in 12 hour time, otherwise, it lists dates in 24
   hour time.

   A number specification is as described in the relavent portions of
   the excel formatting information.  Commas can currently only appear
   at the end of the number specification.  Fractions are not yet
   supported.  
*/


gchar *day_short[] =
{
  N_( "Sun" ),
  N_( "Mon" ),
  N_( "Tue" ),
  N_( "Wed" ),
  N_( "Thu" ),
  N_( "Fri" ),
  N_( "Sat" )
};

gchar *day_long[] =
{
  N_( "Sunday" ),
  N_( "Monday" ),
  N_( "Tuesday" ),
  N_( "Wednesday" ),
  N_( "Thursday" ),
  N_( "Friday" ),
  N_( "Saturday" )
};

gchar *month_short[] =
{
  N_( "Jan" ),
  N_( "Feb" ),
  N_( "Mar" ),
  N_( "Apr" ),
  N_( "May" ),
  N_( "Jun" ),
  N_( "Jul" ),
  N_( "Aug" ),
  N_( "Sep" ),
  N_( "Oct" ),
  N_( "Nov" ),
  N_( "Dec" )
};

gchar *month_long[] =
{
  N_( "January" ),
  N_( "Februrary" ),
  N_( "March" ),
  N_( "April" ),
  N_( "May" ),
  N_( "June" ),
  N_( "July" ),
  N_( "August" ),
  N_( "September" ),
  N_( "October" ),
  N_( "November" ),
  N_( "December" )
};

static void do_roundup( GString *string )
{
  int i;
  
  for ( i = string->len - 1; string->str[i] == '9'; i--)
    {
      string->str[i] = '0';
    }
  if ( string->str[i] == '.' )
    {
      /* FIXME */
    }
  else
    {
      string->str[i] ++;
    }
}

/* Parses the year field at the beginning of the format.  Returns the
   number of characters used. */
static
int append_year( GString *string, gchar *format, struct tm *time_split )
{
  gchar temp[5];
  if ( format[ 1 ] != 'y' )
    {
      g_string_append_c( string, 'y' );
      return 1;
    }
  if ( format[ 2 ] != 'y' || format[ 3 ] != 'y' )
    {
      sprintf( temp, "%02d", time_split->tm_year );
      g_string_append( string, temp );
      return 2;
    }
  sprintf( temp, "%04d", time_split->tm_year + 1900);
  g_string_append( string, temp );
  return 4;
}

/* Parses the month field at the beginning of the format.  Returns the
   number of characters used. */
static int append_month( GString *string, gchar *format, struct tm *time_split )
{
  gchar temp[3];
  if ( format[ 1 ] != 'm' )
    {
      sprintf( temp, "%d", time_split->tm_mon );
      g_string_append( string, temp );
      return 1;
    }
  if ( format[ 2 ] != 'm' )
  {
    sprintf( temp, "%02d", time_split->tm_mon );
    g_string_append( string, temp );
    return 2;
  }
  if ( format[ 3 ] != 'm' )
  {
    g_string_append( string, _( month_short[time_split->tm_mon] ) );
    return 3;
  }
  g_string_append( string, _( month_long[time_split->tm_mon] ) );
  return 4;
}

/* Parses the hour field at the beginning of the format.  Returns the
   number of characters used. */
static int append_hour( GString *string, gchar *format, struct tm *time_split, int timeformat )
{
  gchar temp[3];
  if ( format[ 1 ] != 'h' )
    {
      sprintf( temp, "%d", timeformat ? ( time_split->tm_hour % 12 ) : time_split->tm_hour );
      g_string_append( string, temp );
      return 1;
    }
  sprintf( temp, "%02d", timeformat ? ( time_split->tm_hour % 12 ) : time_split->tm_hour );
  g_string_append( string, temp );
  return 2;
}

/* Parses the day field at the beginning of the format.  Returns the
   number of characters used. */
static int append_day( GString *string, gchar *format, struct tm *time_split )
{
  gchar temp[3];
  if ( format[ 1 ] != 'd' )
    {
      sprintf( temp, "%d", time_split->tm_mday );
      g_string_append( string, temp );
      return 1;
    }
  if ( format[ 2 ] != 'd' )
  {
    sprintf( temp, "%02d", time_split->tm_mday );
    g_string_append( string, temp );
    return 2;
  }
  if ( format[ 3 ] != 'd' )
  {
    g_string_append( string, _( day_short[time_split->tm_wday] ) );
    return 3;
  }
  g_string_append( string, _( day_long[time_split->tm_wday] ) );
  return 4;
}

/* Parses the minute field at the beginning of the format.  Returns the
   number of characters used. */
static int append_minute( GString *string, gchar *format, struct tm *time_split )
{
  gchar temp[3];
  if ( format[ 1 ] != 'm' )
    {
      sprintf( temp, "%d", time_split->tm_min );
      g_string_append( string, temp );
      return 1;
    }
  sprintf( temp, "%02d", time_split->tm_min );
  g_string_append( string, temp );
  return 2;
}

/* Parses the second field at the beginning of the format.  Returns the
   number of characters used. */
static int append_second( GString *string, gchar *format, struct tm *time_split )
{
  gchar temp[3];
  if ( format[ 1 ] != 's' )
    {
      sprintf( temp, "%d", time_split->tm_sec );
      g_string_append( string, temp );
      return 1;
    }
  sprintf( temp, "%02d", time_split->tm_sec );
  g_string_append( string, temp );
  return 2;
}

/* Parses the day part field at the beginning of the format.  Returns
   the number of characters used. */
static int append_half( GString *string, gchar *format, struct tm *time_split )
{
  if ( time_split->tm_hour <= 11 )
    {
      if ( format[ 0 ] == 'a' || format[ 0 ] == 'p' )
	g_string_append_c( string, 'a' );
      else
	g_string_append_c( string, 'A' );
    }
  else
    {
      if ( format[ 0 ] == 'a' || format[ 0 ] == 'p' )
	g_string_append_c( string, 'p' );
      else
	g_string_append_c( string, 'P' );
    }
  
  if ( format[ 1 ] == 'm' || format[ 1 ] == 'M' )
    {
      g_string_append_c( string, format[ 1 ] );
      return 2;
    }
  else return 1;
}

typedef struct
{
  int decimal;
  int timeformat;
  int hasnumbers;
} format_info;

/* This routine should always return, it cant fail, in the worst
 * case it should just downgrade to stupid formatting
 */
void
format_compile (StyleFormat *format)
{
  GString *string = g_string_new( "" );
  int i;
  int which = 0;
  int length = strlen( format->format );
  StyleFormatEntry standard_entries[4];
  StyleFormatEntry *temp;

  g_list_free( format->format_list );
  format->format_list = 0;

  /* g_string_maybe_expand( string, length ); */
  
  for ( i = 0; i < length; i++ )
    {
      switch( format->format[i] )
	{
	case ';':
	  if ( which < 4 )
	    {
	      standard_entries[which].format = g_malloc0( string->len + 1 );
	      strncpy( standard_entries[which].format, string->str, string->len );
	      standard_entries[which].format[string->len] = 0;
	      standard_entries[which].restriction_type = '*';
	      which++;
	    }
	  string = g_string_truncate( string, 0 );
	  break;
	default:
	  string = g_string_append_c( string, format->format[i] );
	  break;
	}
    }
  if ( which < 4 )
    {
      standard_entries[which].format = g_malloc0( string->len + 1 );
      strncpy( standard_entries[which].format, string->str, string->len );
      standard_entries[which].format[string->len] = 0;
      standard_entries[which].restriction_type = '*';
      which++;
    }
  
  /* Set up restriction types. */
  standard_entries[1].restriction_type = '<';
  standard_entries[1].restriction_value = 0;
  switch( which )
    {
    case 4:
      standard_entries[3].restriction_type = '@';
      /* Fall through. */
    case 3:
      standard_entries[2].restriction_type = '=';
      standard_entries[2].restriction_value = 0;
      standard_entries[0].restriction_type = '>';
      standard_entries[0].restriction_value = 0;
      break;
    case 2:
      standard_entries[0].restriction_type = '.';  /* . >= */
      standard_entries[0].restriction_value = 0;
      break;
    }
  for( i = 0; i < which; i++ )
    {
      temp = g_new( StyleFormatEntry, 1 );
      *temp = standard_entries[i];
      format->format_list = g_list_append( format->format_list, temp );
    }
  g_string_free( string, TRUE );
}

static void
style_entry_free(gpointer data, gpointer user_data)
{
  StyleFormatEntry *entry = data;
  g_free( entry->format );
  g_free( entry );
}

void
format_destroy (StyleFormat *format)
{
  /* This routine is invoked when the last user of the
   * format is gone (ie, refcount has reached zero) just
   * before the StyleFormat structure is actually released.
   *
   * resources allocated in format_compile should be disposed here
   */

  g_list_foreach( format->format_list, style_entry_free, NULL );
  g_list_free( format->format_list );
  format->format_list = NULL;  
}

static gchar *
format_number(gdouble number, StyleFormatEntry *style_format_entry, char **color_name )
{
  gint left_req = 0, right_req = 0;
  gint left_spaces = 0, right_spaces = 0;
  gint right_allowed = 0;
  gint i = 0;
  gdouble temp;
  gboolean negative = FALSE;
  GString *string = g_string_new( "" );
  GString *number_string = g_string_new( "" );
  gchar *format = style_format_entry->format;
  gint length = strlen(format);
  gchar *returnvalue;
  gint zero_count;
  gint nine_count;
  format_info info;
  gboolean minute_mode = FALSE;
  gboolean done = FALSE;
  gboolean any = FALSE;
  gdouble date;
  time_t timec;
  struct tm *time_split;

  if (color_name)
    *color_name = NULL;
  
  date = number;
  date -= 25569.0;
  date *= 86400.0;

  timec = date;

  time_split = localtime( &timec );

  info.decimal = -1;
  info.timeformat = 0;
  info.hasnumbers = FALSE;

  if (strcmp (format, "General") == 0){
      char buffer [40];

      snprintf (buffer, sizeof (buffer), "%g", number);
      return g_strdup (buffer);
  }
  
  for ( i = 0; i < length; i++ )
    {
      switch ( format[i] )
	{
	case '.':
	  info.decimal = i;
	  break;
	case 'a': /* Fall through */
	case 'A': /* Fall through */
	case 'p': /* Fall through */
	case 'P':
	  info.timeformat = 1;
	  break;
	case '0': /* Fall through */
	case '#': /* Fall through */
	case '?':
	  info.hasnumbers = TRUE;
	  break;
	}
    }

  for ( i = 0; i < length && !done; i++ )
    {
      switch ( format[i] )
	{
	case 'm':
	  if ( minute_mode )
	    i += append_minute( string, format + i, time_split ) - 1;
	  else
	    i += append_month( string, format + i, time_split ) - 1;
	  minute_mode = FALSE;
	  break;
	case 'y':
	  i += append_year( string, format + i, time_split ) - 1;
	  minute_mode = FALSE;
	  break;
	case 'h':
	  i += append_hour( string, format + i, time_split, info.timeformat ) - 1;
	  minute_mode = TRUE;
	  break;
	case 'd':
	  i += append_day( string, format + i, time_split ) - 1;
	  minute_mode = FALSE;
	  break;
	case 's':
	  i += append_second( string, format + i, time_split ) - 1;
	  minute_mode = FALSE;
	  break;
	case 'a':
	case 'A':
	case 'p':
	case 'P':
	  i += append_half( string, format + i, time_split ) - 1;
	  minute_mode = FALSE;
	  break;
	case '0':
	case '?':
	case '#':
	case '.':
	  done = any = TRUE;
	  break;
	default:
	  g_string_append_c( string, format[i] );
	  break;
	}
    }

  if(any)
    {
      for ( ; format[i] == '#'; i++ )
	{
	}
      for ( ; format[i] == '?'; i++ )
	{
	  left_spaces ++;
	}
      for ( ; format[i] == '0'; i++ )
	{
	  left_req ++;
	  left_spaces ++;
	}
      if ( format[i] == '.' )
	{
	  i++;
	  for ( ; format[i] == '0'; i++ )
	    {
	      right_req ++;
	      right_allowed ++;
	      right_spaces ++;
	    }
	  for ( ; format[i] == '?'; i++ )
	    {
	      right_spaces ++;
	      right_allowed ++;
	    }
	  for ( ; format[i] == '#'; i++ )
	    {
	      right_allowed ++;
	    }
	}
      for ( ; format[i] == ','; i++ )
	{
	  number /= 1000.0;
	}

      if ( number < 0.0 )
	{
	  number = - number;
	  negative = TRUE;
	}
#if 0
      length = ceil( log10( number ) );
      if ( log10( number ) == ceil( log10( number ) ) )
	length ++;
      length = length > left_req ? length : left_req;
      length += 1;
      length += right_allowed;
      
      g_string_maybe_expand( number_string, length );
#endif
  
      for ( temp = number; temp >= 1.0; temp /= 10.0 )
	{
	  gint digit = floor( temp );
	  digit %= 10;
	  g_string_prepend_c( number_string, digit + '0' );
	  if ( left_req > 0 )
	    left_req --;
	  if ( left_spaces > 0 )
	    left_spaces --;
	  
	}
      
      for ( ; left_req > 0; left_req --, left_spaces -- )
	{
	  g_string_prepend_c( number_string, '0' );
	}

      for ( ; left_spaces > 0; left_spaces -- )
	{
	  g_string_prepend_c( number_string, ' ' );
	}

      if( negative )
	g_string_prepend_c( number_string, '-' );

      if( info.decimal >= 0 )
	g_string_append_c( number_string, '.' );

      temp = number - floor( number );

      for ( ; right_req > 0; right_req --, right_allowed --, right_spaces -- )
	{
	  gint digit;
	  temp *= 10.0;
	  digit = floor( temp );
	  temp -= floor( temp );
	  if ( right_allowed == 1 && floor( temp * 10.0 ) >= 5 )
	    {
	      if ( digit < 9 )
		digit ++;
	      else
		{
		  digit = 0;
		  do_roundup( number_string );
		}
	    }
	  g_string_append_c( number_string, digit + '0' );
	}

      zero_count = 0;
      nine_count = 0;
  
      for ( ; right_allowed > 0; right_allowed -- )
	{
	  gint digit;
	  temp *= 10.0;
	  digit = floor( temp );
	  temp -= floor( temp );
	  if ( right_allowed == 1 && floor( temp * 10.0 ) >= 5 )
	    {
	      if ( digit < 9 )
		digit ++;
	      else
		{
		  digit = 0;
		  right_spaces -= zero_count;
		  zero_count = nine_count;
		  right_spaces += zero_count;
		  do_roundup( number_string );
		}
	    }
	  if ( digit == 0 )
	    zero_count ++;
	  else
	    {
	      right_spaces -= zero_count + 1;
	      zero_count = 0;
	    }
	  if ( digit == 9 )
	    nine_count ++;
	  else
	    nine_count = 0;
	  g_string_append_c( number_string, digit + '0' );
	}

      g_string_truncate( number_string, number_string->len - zero_count );

      for ( ; right_spaces > 0; right_spaces -- )
	{
	  g_string_append_c( number_string, ' ' );
	}
      g_string_append( string, number_string->str );
    }
  if (format [i])
    g_string_append ( string, &format [i] );
  
  returnvalue = g_malloc0( string->len + 1);
  strncpy( returnvalue, string->str, string->len );
  returnvalue[string->len] = 0;

  g_string_free( string, TRUE );
  g_string_free( number_string, TRUE );
  
  return returnvalue;
}

static gboolean
check_valid (StyleFormatEntry *entry, Value *value)
{
  switch (value->type)
    {
    case VALUE_STRING:
      return entry->restriction_type == '@';
    case VALUE_FLOAT:
      switch( entry->restriction_type )
	{
	case '*': 
	  return TRUE;
	case '<':
	  return value->v.v_float < entry->restriction_value;
	case '>':
	  return value->v.v_float > entry->restriction_value;
	case '=':
	  return value->v.v_float == entry->restriction_value;
	case ',':
	  return value->v.v_float <= entry->restriction_value;
	case '.':
	  return value->v.v_float >= entry->restriction_value;
	case '+':
	  return value->v.v_float != entry->restriction_value;
	default:
	  return FALSE;
	}
    case VALUE_INTEGER:
      switch( entry->restriction_type )
	{
	case '*': 
	  return TRUE;
	case '<':
	  return value->v.v_int < entry->restriction_value;
	case '>':
	  return value->v.v_int > entry->restriction_value;
	case '=':
	  return value->v.v_int == entry->restriction_value;
	case ',':
	  return value->v.v_int <= entry->restriction_value;
	case '.':
	  return value->v.v_int >= entry->restriction_value;
	case '+':
	  return value->v.v_int != entry->restriction_value;
	default:
	  return FALSE;
	}      
    default:
      return FALSE;
    }
}

gchar *
format_value (StyleFormat *format, Value *value, char **color_name)
{
  char *v = NULL;
  StyleFormatEntry entry;

  GList *list = format->format_list;
  
  /* get format */
  for ( ; list; list = g_list_next( list ) )
    {
      if ( check_valid( list->data, value ) )
	break;
    }
  
  if( list )
    {
      entry = *(StyleFormatEntry *) (list->data);
    }
  else
    entry.format = format->format;

  switch (value->type){
  case VALUE_FLOAT:
    v = format_number (value->v.v_float, &entry, color_name);
    break;
    
  case VALUE_INTEGER:
    v = format_number (value->v.v_int, &entry, color_name);
    break;
    
  case VALUE_STRING:
    return g_strdup (value->v.str->str);
    
  default:
    return g_strdup ("Internal error");
  }
  
  /* Format error, return a default value */
  if (v == NULL)
    return value_string (value);
  
  return v;
}

