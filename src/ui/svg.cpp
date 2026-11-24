#include "svg.h"

#include "core/files.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>

namespace
{
// The value of attribute `name` inside one tag, or "".
std::string Attr( const std::string &tag, const char *name )
{
	const std::string key = std::string( " " ) + name + "=\"";
	const size_t a = tag.find( key );
	if ( a == std::string::npos )
		return "";
	const size_t b = tag.find( '"', a + key.size() );
	return b == std::string::npos ? "" : tag.substr( a + key.size(), b - a - key.size() );
}

// #rgb / #rrggbb -> lightness 0..1; -1 when it is not a colour we understand (none, url(...)).
double Lightness( std::string v )
{
	while ( !v.empty() && isspace( (unsigned char)v.back() ) )
		v.pop_back();
	if ( v.size() == 4 && v[0] == '#' )
		v = std::string( "#" ) + v[1] + v[1] + v[2] + v[2] + v[3] + v[3];
	if ( v.size() != 7 || v[0] != '#' )
		return v == "black" ? 0 : v == "white" ? 1 : -1;
	const unsigned long rgb = strtoul( v.c_str() + 1, nullptr, 16 );
	return ( 0.299 * ( ( rgb >> 16 ) & 255 ) + 0.587 * ( ( rgb >> 8 ) & 255 ) + 0.114 * ( rgb & 255 ) ) / 255.0;
}

// .name { fill: #xxx; } rules of a <style> block.
std::map<std::string, double> ParseStyle( const std::string &css )
{
	std::map<std::string, double> out;
	size_t i = 0;
	while ( ( i = css.find( '.', i ) ) != std::string::npos )
	{
		const size_t open = css.find( '{', i ), close = open == std::string::npos ? open : css.find( '}', open );
		if ( close == std::string::npos )
			break;
		std::string name = css.substr( i + 1, open - i - 1 );
		while ( !name.empty() && isspace( (unsigned char)name.back() ) )
			name.pop_back();
		const std::string body = css.substr( open + 1, close - open - 1 );
		const size_t f = body.find( "fill:" );
		if ( f != std::string::npos )
		{
			const size_t end = body.find( ';', f );
			std::string v = body.substr( f + 5, end == std::string::npos ? std::string::npos : end - f - 5 );
			v.erase( 0, v.find_first_not_of( " \t" ) );
			out[name] = Lightness( v );
		}
		i = close + 1;
	}
	return out;
}

// Path data -> flattened closed contours.
class PathParser
{
public:
	explicit PathParser( const std::string &d ) : m_d( d ) {}

	std::vector<std::vector<Canvas::Pt>> Run()
	{
		char cmd = 0;
		while ( SkipSpace(), m_i < m_d.size() )
		{
			if ( isalpha( (unsigned char)m_d[m_i] ) )
				cmd = m_d[m_i++];
			else if ( !cmd )
				return {}; // numbers before any command: not a path we can read
			const bool rel = islower( (unsigned char)cmd );
			const double ox = rel ? m_x : 0, oy = rel ? m_y : 0;
			switch ( tolower( (unsigned char)cmd ) )
			{
			case 'm':
			{
				double x, y;
				if ( !Num( x ) || !Num( y ) )
					return Done();
				Close();
				m_x = ox + x, m_y = oy + y;
				m_startX = m_x, m_startY = m_y;
				m_cur.push_back( { m_x, m_y } );
				cmd = rel ? 'l' : 'L'; // further pairs are line-tos
				break;
			}
			case 'l':
			{
				double x, y;
				if ( !Num( x ) || !Num( y ) )
					return Done();
				LineTo( ox + x, oy + y );
				break;
			}
			case 'h':
			{
				double x;
				if ( !Num( x ) )
					return Done();
				LineTo( ox + x, m_y );
				break;
			}
			case 'v':
			{
				double y;
				if ( !Num( y ) )
					return Done();
				LineTo( m_x, oy + y );
				break;
			}
			case 'c':
			{
				double x1, y1, x2, y2, x, y;
				if ( !Num( x1 ) || !Num( y1 ) || !Num( x2 ) || !Num( y2 ) || !Num( x ) || !Num( y ) )
					return Done();
				Cubic( ox + x1, oy + y1, ox + x2, oy + y2, ox + x, oy + y );
				break;
			}
			case 's':
			{
				double x2, y2, x, y;
				if ( !Num( x2 ) || !Num( y2 ) || !Num( x ) || !Num( y ) )
					return Done();
				// The first control point mirrors the previous curve's second one (or is the current point).
				const double x1 = m_hasCtrl ? 2 * m_x - m_cx : m_x, y1 = m_hasCtrl ? 2 * m_y - m_cy : m_y;
				Cubic( x1, y1, ox + x2, oy + y2, ox + x, oy + y );
				break;
			}
			case 'z':
				Close();
				m_x = m_startX, m_y = m_startY;
				m_hasCtrl = false;
				break;
			default:
				return Done(); // a command this reader does not know: keep what was read
			}
			if ( tolower( (unsigned char)cmd ) != 'c' && tolower( (unsigned char)cmd ) != 's' )
				m_hasCtrl = false;
		}
		return Done();
	}

private:
	void SkipSpace()
	{
		while ( m_i < m_d.size() && ( isspace( (unsigned char)m_d[m_i] ) || m_d[m_i] == ',' ) )
			m_i++;
	}
	bool Num( double &v )
	{
		SkipSpace();
		if ( m_i >= m_d.size() )
			return false;
		// strtod covers SVG's compact number forms: "1.5.5" reads as 1.5 then .5, "1-2" as 1 then -2, exponents too.
		const char *start = m_d.c_str() + m_i;
		char *end = nullptr;
		v = strtod( start, &end );
		if ( end == start )
			return false;
		m_i += end - start;
		return true;
	}
	void LineTo( double x, double y )
	{
		m_x = x, m_y = y;
		m_cur.push_back( { x, y } );
	}
	void Cubic( double x1, double y1, double x2, double y2, double x, double y )
	{
		const double len = std::hypot( x1 - m_x, y1 - m_y ) + std::hypot( x2 - x1, y2 - y1 ) + std::hypot( x - x2, y - y2 );
		const int n = std::clamp( (int)std::ceil( len / 1.5 ), 2, 64 ); // ~1.5 art units per segment
		const double x0 = m_x, y0 = m_y;
		for ( int i = 1; i <= n; i++ )
		{
			const double t = (double)i / n, u = 1 - t;
			m_cur.push_back( { u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x,
				u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y } );
		}
		m_x = x, m_y = y;
		m_cx = x2, m_cy = y2;
		m_hasCtrl = true;
	}
	void Close()
	{
		if ( m_cur.size() >= 3 )
			m_out.push_back( std::move( m_cur ) );
		m_cur.clear();
	}
	std::vector<std::vector<Canvas::Pt>> Done()
	{
		Close();
		return std::move( m_out );
	}

	const std::string &m_d;
	size_t m_i = 0;
	double m_x = 0, m_y = 0, m_startX = 0, m_startY = 0, m_cx = 0, m_cy = 0;
	bool m_hasCtrl = false;
	std::vector<Canvas::Pt> m_cur;
	std::vector<std::vector<Canvas::Pt>> m_out;
};
} // namespace

bool SvgImage::Load( const std::string &path )
{
	m_shapes.clear();
	std::string text;
	if ( !files::ReadFile( path, text ) )
		return false;
	std::map<std::string, double> classes;
	if ( const size_t a = text.find( "<style" ); a != std::string::npos )
		if ( const size_t b = text.find( "</style>", a ); b != std::string::npos )
			classes = ParseStyle( text.substr( a, b - a ) );
	size_t i = 0;
	while ( ( i = text.find( '<', i ) ) != std::string::npos )
	{
		const size_t end = text.find( '>', i );
		if ( end == std::string::npos )
			break;
		const std::string tag = text.substr( i, end - i );
		i = end + 1;
		if ( tag.rfind( "<svg", 0 ) == 0 )
		{
			const std::string vb = Attr( tag, "viewBox" );
			double v[4] = {};
			const char *p = vb.c_str();
			for ( double &x : v )
			{
				char *e = nullptr;
				x = strtod( p, &e );
				p = e;
				while ( *p == ',' || isspace( (unsigned char)*p ) )
					p++;
			}
			m_w = v[2], m_h = v[3];
			continue;
		}
		if ( tag.rfind( "<path", 0 ) != 0 )
			continue;
		double light = -1;
		if ( const std::string fill = Attr( tag, "fill" ); !fill.empty() )
			light = Lightness( fill );
		else if ( const auto it = classes.find( Attr( tag, "class" ) ); it != classes.end() )
			light = it->second;
		else
			light = 0; // SVG's default fill is black
		if ( light < 0 )
			continue;
		const std::string d = Attr( tag, "d" );
		PathParser parser( d );
		Shape s{ light, parser.Run() };
		if ( !s.contours.empty() )
			m_shapes.push_back( std::move( s ) );
	}
	return Ok();
}

void SvgImage::Draw( Canvas &c, double x, double y, double scale, Color tint ) const
{
	double top = 0;
	for ( const Shape &s : m_shapes )
		top = std::max( top, s.lightness );
	for ( const Shape &s : m_shapes )
	{
		std::vector<std::vector<Canvas::Pt>> moved = s.contours;
		for ( auto &poly : moved )
			for ( Canvas::Pt &p : poly )
				p = { x + p.x * scale, y + p.y * scale };
		Color col = tint;
		col.a = (uint8_t)std::lround( tint.a * ( top > 0 ? 0.55 + 0.45 * s.lightness / top : 1.0 ) );
		c.FillPolygons( moved, col );
	}
}
