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

#include <glib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <gnome.h>
#include <time.h>
gchar *format_text( gchar *format, gdouble number );
gchar *format_time( gchar *format, const time_t timec );

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

void
test()
{
  int timec = 1220000000;
  printf( "%s|\n", format_text( "??0000.00?", 12.3456789 ) );
  printf( "%s|\n", format_text( "??0000.00?", 12.3 ) );
  printf( "%s|\n", format_text( "??0000.00?", 12345.6789 ) );
  printf( "%s|\n", format_text( "???????.00", 0.123456789 ) );
  printf( "%s|\n", format_text( "???0.000??#,,", 12200000 ) );
  printf( "%s|\n", format_time( "hh:mm:ss", timec ) );
  printf( "%s|\n", format_time( "mmmm dd, yyyy", timec ) );
  printf( "%s|\n", format_time( "mmm d, yy h:m:s", timec ) );
}

int
main( int argc, gchar *argv )
{
  test();
  return 0;
}

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

gchar *
format_text( gchar *format, gdouble number )
{
  gint left_req = 0, right_req = 0;
  gint left_spaces = 0, right_spaces = 0;
  gint right_allowed = 0;
  gint i = 0;
  gdouble temp;
  gboolean negative = FALSE;
  GString *string = g_string_new( "" );
#if 0
  gint length;
#endif
  gchar *returnvalue;
  gint zero_count;
  gint nine_count;

  for ( i = 0; format[i] == '#'; i++ )
    /* Empty statement */;
  for ( ; format[i] == '?'; i++ )
    left_spaces ++;
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
	right_allowed ++;
    }
  for ( ; format[i] == ','; i++ )
    number /= 1000.0;
  if ( format[i] )
    return NULL;
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

  g_string_maybe_expand( string, length );
#endif
  
  for ( temp = number; temp >= 1.0; temp /= 10.0 )
    {
      gint digit = floor( temp );
      digit %= 10;
      g_string_prepend_c( string, digit + '0' );
      if ( left_req > 0 )
	left_req --;
      if ( left_spaces > 0 )
	left_spaces --;

    }

  for ( ; left_req > 0; left_req --, left_spaces -- )
    {
      g_string_prepend_c( string, '0' );
    }

  for ( ; left_spaces > 0; left_spaces -- )
    {
      g_string_prepend_c( string, ' ' );
    }

  if( negative )
    g_string_prepend_c( string, '-' );

  g_string_append_c( string, '.' );

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
	      do_roundup( string );
	    }
	}
      g_string_append_c( string, digit + '0' );
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
	      do_roundup( string );
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
      g_string_append_c( string, digit + '0' );
    }


  g_string_truncate( string, string->len - zero_count );

  for ( ; right_spaces > 0; right_spaces -- )
    {
      g_string_append_c( string, ' ' );
    }
  
  returnvalue = g_malloc0( string->len + 1);
  strncpy( returnvalue, string->str, string->len );
  returnvalue[string->len] = 0;

  g_string_free( string, TRUE );
  
  return returnvalue;
}

/* Parses the year field at the beginning of the format.  Returns the
   number of characters used. */
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
int append_month( GString *string, gchar *format, struct tm *time_split )
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
int append_hour( GString *string, gchar *format, struct tm *time_split )
{
  gchar temp[3];
  if ( format[ 1 ] != 'h' )
    {
      sprintf( temp, "%d", time_split->tm_hour );
      g_string_append( string, temp );
      return 1;
    }
  sprintf( temp, "%02d", time_split->tm_hour );
  g_string_append( string, temp );
  return 2;
}

/* Parses the day field at the beginning of the format.  Returns the
   number of characters used. */
int append_day( GString *string, gchar *format, struct tm *time_split )
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
int append_minute( GString *string, gchar *format, struct tm *time_split )
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
int append_second( GString *string, gchar *format, struct tm *time_split )
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

gchar *format_time( gchar *format, const time_t timec )
{
  struct tm *time_split = localtime( &timec );
  GString *string = g_string_new( "" );
  int i;
  int length = strlen( format );
  gboolean minute_mode = FALSE;
  gchar *returnvalue;

  for ( i = 0; i < length; i++ )
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
	  i += append_hour( string, format + i, time_split ) - 1;
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
	default:
	  g_string_append_c( string, format[i] );
	  break;
	}
    }
  
  returnvalue = g_malloc0( string->len + 1);
  strncpy( returnvalue, string->str, string->len );
  returnvalue[string->len] = 0;

  g_string_free( string, TRUE );
  
  return returnvalue;
}
