#include "version.h"

#include <cctype>
#include <cstdlib>

bool ParseVersion( const std::string &text, int out[3] )
{
	size_t i = !text.empty() && ( text[0] == 'v' || text[0] == 'V' ) ? 1 : 0;
	for ( int k = 0; k < 3; k++ )
	{
		if ( i >= text.size() || !isdigit( (unsigned char)text[i] ) )
			return false;
		out[k] = 0;
		while ( i < text.size() && isdigit( (unsigned char)text[i] ) )
			out[k] = out[k] * 10 + ( text[i++] - '0' );
		if ( k < 2 )
		{
			if ( i >= text.size() || text[i] != '.' )
				return false;
			i++;
		}
	}
	return true;
}

bool IsNewerVersion( const std::string &candidate, const std::string &current )
{
	int a[3], b[3];
	if ( !ParseVersion( candidate, a ) || !ParseVersion( current, b ) )
		return false;
	for ( int k = 0; k < 3; k++ )
		if ( a[k] != b[k] )
			return a[k] > b[k];
	return false;
}

std::string ReleaseTag( const std::string &json )
{
	const size_t k = json.find( "\"tag_name\"" );
	if ( k == std::string::npos )
		return "";
	const size_t a = json.find( '"', json.find( ':', k ) );
	const size_t b = a == std::string::npos ? a : json.find( '"', a + 1 );
	return b == std::string::npos ? "" : json.substr( a + 1, b - a - 1 );
}
