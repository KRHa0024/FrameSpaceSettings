#include "canvas.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
// Installed fonts to look for, in order. The .ttc files hold several faces; the Japanese one is picked by family name.
const char *kRegular[] = { "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Medium.ttc",
	"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto/NotoSans-Regular.ttf",
	"/usr/share/fonts/TTF/DejaVuSans.ttf" };
const char *kBold[] = { "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc", "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
	"/usr/share/fonts/noto/NotoSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf" };

FT_Face OpenJapanese( FT_Library lib, const char *path )
{
	FT_Face face = nullptr;
	if ( FT_New_Face( lib, path, -1, &face ) != 0 )
		return nullptr;
	const long count = face->num_faces;
	FT_Done_Face( face );
	for ( long i = 0; i < count; i++ )
	{
		if ( FT_New_Face( lib, path, i, &face ) != 0 )
			continue;
		if ( count == 1 || ( face->family_name && strstr( face->family_name, " JP" ) ) )
			return face;
		FT_Done_Face( face );
	}
	return FT_New_Face( lib, path, 0, &face ) == 0 ? face : nullptr;
}
} // namespace

Fonts &Fonts::Get()
{
	static Fonts f;
	if ( !f.m_loaded )
		f.Load();
	return f;
}

void Fonts::Release()
{
	if ( !m_loaded )
		return;
	if ( m_face[1] && m_face[1] != m_face[0] )
		FT_Done_Face( (FT_Face)m_face[1] );
	if ( m_face[0] )
		FT_Done_Face( (FT_Face)m_face[0] );
	if ( m_lib )
		FT_Done_FreeType( (FT_Library)m_lib );
	m_face[0] = m_face[1] = m_lib = nullptr;
	m_ok[0] = m_ok[1] = false;
	std::unordered_map<uint64_t, Glyph>().swap( m_cache );
	m_loaded = false;
}

void Fonts::Load()
{
	m_loaded = true;
	FT_Library lib;
	if ( FT_Init_FreeType( &lib ) != 0 )
		return;
	m_lib = lib;
	for ( const char *p : kRegular )
		if ( ( m_face[0] = OpenJapanese( lib, p ) ) )
			break;
	for ( const char *p : kBold )
		if ( ( m_face[1] = OpenJapanese( lib, p ) ) )
			break;
	m_ok[0] = m_face[0] != nullptr;
	m_ok[1] = m_face[1] != nullptr;
	if ( !m_ok[1] )
		m_face[1] = m_face[0];
}

const Fonts::Glyph &Fonts::Lookup( uint32_t cp, int px, Weight weight )
{
	const int wi = weight == Weight::Bold ? 1 : 0;
	const uint64_t key = ( uint64_t )cp << 16 | ( uint64_t )px << 1 | (uint64_t)wi;
	auto it = m_cache.find( key );
	if ( it != m_cache.end() )
		return it->second;
	Glyph &g = m_cache[key];
	FT_Face face = (FT_Face)m_face[wi];
	if ( !face || FT_Set_Pixel_Sizes( face, 0, (FT_UInt)px ) != 0 || FT_Load_Char( face, cp, FT_LOAD_RENDER ) != 0 )
		return g;
	const FT_GlyphSlot s = face->glyph;
	g.w = (int)s->bitmap.width;
	g.h = (int)s->bitmap.rows;
	g.left = s->bitmap_left;
	g.top = s->bitmap_top;
	g.advance = (int)s->advance.x;
	g.alpha.resize( (size_t)g.w * g.h );
	for ( int y = 0; y < g.h; y++ )
		memcpy( &g.alpha[(size_t)y * g.w], s->bitmap.buffer + y * s->bitmap.pitch, (size_t)g.w );
	return g;
}

int Fonts::Ascender( int px, Weight weight )
{
	FT_Face face = (FT_Face)m_face[weight == Weight::Bold ? 1 : 0];
	if ( !face || FT_Set_Pixel_Sizes( face, 0, (FT_UInt)px ) != 0 )
		return px;
	return (int)( face->size->metrics.ascender >> 6 );
}

std::vector<uint32_t> DecodeUtf8( const std::string &s )
{
	std::vector<uint32_t> out;
	for ( size_t i = 0; i < s.size(); )
	{
		const unsigned char c = (unsigned char)s[i];
		uint32_t cp = c;
		int extra = 0;
		if ( c >= 0xf0 )
			cp = c & 0x07, extra = 3;
		else if ( c >= 0xe0 )
			cp = c & 0x0f, extra = 2;
		else if ( c >= 0xc0 )
			cp = c & 0x1f, extra = 1;
		i++;
		for ( int k = 0; k < extra && i < s.size(); k++, i++ )
			cp = cp << 6 | ( (unsigned char)s[i] & 0x3f );
		out.push_back( cp );
	}
	return out;
}

Canvas::Canvas( int w, int h ) : m_w( w ), m_h( h ), m_px( (size_t)w * h * 4, 0 ) {}

void Canvas::Clear( Color c )
{
	for ( size_t i = 0; i < m_px.size(); i += 4 )
	{
		m_px[i] = c.r;
		m_px[i + 1] = c.g;
		m_px[i + 2] = c.b;
		m_px[i + 3] = c.a;
	}
}

void Canvas::Blend( int x, int y, Color c, double coverage )
{
	if ( x < 0 || y < 0 || x >= m_w || y >= m_h || coverage <= 0 )
		return;
	uint8_t *p = &m_px[( (size_t)y * m_w + x ) * 4];
	const double a = std::min( 1.0, coverage ) * c.a / 255.0;
	// Straight (non-premultiplied) alpha "over".
	const double da = p[3] / 255.0, oa = a + da * ( 1 - a );
	if ( oa <= 0 )
		return;
	for ( int k = 0; k < 3; k++ )
	{
		const double src = ( k == 0 ? c.r : k == 1 ? c.g : c.b );
		p[k] = (uint8_t)std::lround( ( src * a + p[k] * da * ( 1 - a ) ) / oa );
	}
	p[3] = (uint8_t)std::lround( oa * 255 );
}

void Canvas::RoundRect( double x, double y, double w, double h, double r, Color c, double stroke )
{
	r = std::min( r, std::min( w, h ) / 2 );
	const int x0 = std::max( 0, (int)std::floor( x ) ), x1 = std::min( m_w, (int)std::ceil( x + w ) );
	const int y0 = std::max( 0, (int)std::floor( y ) ), y1 = std::min( m_h, (int)std::ceil( y + h ) );
	const double cx = x + w / 2, cy = y + h / 2, hx = w / 2 - r, hy = h / 2 - r;
	for ( int py = y0; py < y1; py++ )
		for ( int px = x0; px < x1; px++ )
		{
			// Signed distance to the rounded rectangle at the pixel centre.
			const double qx = std::fabs( px + 0.5 - cx ) - hx, qy = std::fabs( py + 0.5 - cy ) - hy;
			const double outside = std::hypot( std::max( qx, 0.0 ), std::max( qy, 0.0 ) ) + std::min( std::max( qx, qy ), 0.0 ) - r;
			double cov = std::clamp( 0.5 - outside, 0.0, 1.0 );
			if ( stroke > 0 )
				cov = std::min( cov, std::clamp( 0.5 + outside + stroke, 0.0, 1.0 ) );
			Blend( px, py, c, cov );
		}
}

void Canvas::Line( double x0, double y0, double x1, double y1, double width, Color c )
{
	const double h = width / 2;
	const int xa = std::max( 0, (int)std::floor( std::min( x0, x1 ) - h - 1 ) ), xb = std::min( m_w, (int)std::ceil( std::max( x0, x1 ) + h + 1 ) );
	const int ya = std::max( 0, (int)std::floor( std::min( y0, y1 ) - h - 1 ) ), yb = std::min( m_h, (int)std::ceil( std::max( y0, y1 ) + h + 1 ) );
	const double dx = x1 - x0, dy = y1 - y0, len2 = std::max( 1e-9, dx * dx + dy * dy );
	for ( int py = ya; py < yb; py++ )
		for ( int px = xa; px < xb; px++ )
		{
			const double qx = px + 0.5 - x0, qy = py + 0.5 - y0;
			const double t = std::clamp( ( qx * dx + qy * dy ) / len2, 0.0, 1.0 );
			const double d = std::hypot( qx - t * dx, qy - t * dy ) - h;
			Blend( px, py, c, std::clamp( 0.5 - d, 0.0, 1.0 ) );
		}
}

void Canvas::Triangle( double x0, double y0, double x1, double y1, double x2, double y2, Color c )
{
	const double xs[3] = { x0, x1, x2 }, ys[3] = { y0, y1, y2 };
	const double area = ( x1 - x0 ) * ( y2 - y0 ) - ( x2 - x0 ) * ( y1 - y0 );
	const double sgn = area < 0 ? -1 : 1;
	const int xa = std::max( 0, (int)std::floor( std::min( { x0, x1, x2 } ) - 1 ) ), xb = std::min( m_w, (int)std::ceil( std::max( { x0, x1, x2 } ) + 1 ) );
	const int ya = std::max( 0, (int)std::floor( std::min( { y0, y1, y2 } ) - 1 ) ), yb = std::min( m_h, (int)std::ceil( std::max( { y0, y1, y2 } ) + 1 ) );
	for ( int py = ya; py < yb; py++ )
		for ( int px = xa; px < xb; px++ )
		{
			double inside = 1e9; // distance to the nearest edge, positive inside
			for ( int i = 0; i < 3; i++ )
			{
				const int j = ( i + 1 ) % 3;
				const double ex = xs[j] - xs[i], ey = ys[j] - ys[i], len = std::hypot( ex, ey );
				inside = std::min( inside, sgn * ( ex * ( py + 0.5 - ys[i] ) - ey * ( px + 0.5 - xs[i] ) ) / len );
			}
			Blend( px, py, c, std::clamp( inside + 0.5, 0.0, 1.0 ) );
		}
}

void Canvas::FillPolygons( const std::vector<std::vector<Pt>> &contours, Color c )
{
	double minY = 1e18, maxY = -1e18, minX = 1e18, maxX = -1e18;
	for ( const auto &poly : contours )
		for ( const Pt &p : poly )
		{
			minY = std::min( minY, p.y ), maxY = std::max( maxY, p.y );
			minX = std::min( minX, p.x ), maxX = std::max( maxX, p.x );
		}
	if ( minY > maxY )
		return;
	const int y0 = std::max( 0, (int)std::floor( minY ) ), y1 = std::min( m_h, (int)std::ceil( maxY ) );
	const int x0 = std::max( 0, (int)std::floor( minX ) ), x1 = std::min( m_w, (int)std::ceil( maxX ) + 1 );
	if ( y0 >= y1 || x0 >= x1 )
		return;
	// 5 sub-scanlines per pixel row; along a sub-scanline, span ends count with their exact fraction.
	constexpr int kSub = 5;
	std::vector<double> cov( x1 - x0 + 1 );
	struct Cross
	{
		double x;
		int dir;
	};
	std::vector<Cross> xs;
	for ( int py = y0; py < y1; py++ )
	{
		std::fill( cov.begin(), cov.end(), 0.0 );
		for ( int s = 0; s < kSub; s++ )
		{
			const double sy = py + ( s + 0.5 ) / kSub;
			xs.clear();
			for ( const auto &poly : contours )
				for ( size_t i = 0, n = poly.size(); i < n; i++ )
				{
					const Pt &a = poly[i], &b = poly[( i + 1 ) % n];
					if ( ( a.y <= sy ) == ( b.y <= sy ) )
						continue;
					xs.push_back( { a.x + ( sy - a.y ) / ( b.y - a.y ) * ( b.x - a.x ), b.y > a.y ? 1 : -1 } );
				}
			std::sort( xs.begin(), xs.end(), []( const Cross &l, const Cross &r ) { return l.x < r.x; } );
			int wind = 0;
			for ( size_t i = 0; i + 1 < xs.size(); i++ )
			{
				wind += xs[i].dir;
				if ( wind == 0 )
					continue;
				const double a = std::clamp( xs[i].x, (double)x0, (double)x1 ), b = std::clamp( xs[i + 1].x, (double)x0, (double)x1 );
				if ( b <= a )
					continue;
				const int ia = (int)std::floor( a ), ib = (int)std::floor( b );
				if ( ia == ib )
					cov[ia - x0] += ( b - a ) / kSub;
				else
				{
					cov[ia - x0] += ( ia + 1 - a ) / kSub;
					for ( int k = ia + 1; k < ib; k++ )
						cov[k - x0] += 1.0 / kSub;
					if ( ib < x1 )
						cov[ib - x0] += ( b - ib ) / kSub;
				}
			}
		}
		for ( int px = x0; px < x1; px++ )
			Blend( px, py, c, cov[px - x0] );
	}
}

void Canvas::Blit( const Canvas &src, int x, int y )
{
	for ( int j = 0; j < src.m_h; j++ )
		for ( int i = 0; i < src.m_w; i++ )
		{
			const uint8_t *p = &src.m_px[( (size_t)j * src.m_w + i ) * 4];
			if ( p[3] )
				Blend( x + i, y + j, { p[0], p[1], p[2], 255 }, p[3] / 255.0 );
		}
}

void Canvas::Arc( double cx, double cy, double r, double a0, double a1, double width, Color c )
{
	const double h = width / 2, sweep = a1 - a0;
	const double ex[2] = { cx + r * std::cos( a0 ), cx + r * std::cos( a1 ) }, ey[2] = { cy + r * std::sin( a0 ), cy + r * std::sin( a1 ) };
	const int xa = std::max( 0, (int)std::floor( cx - r - h - 1 ) ), xb = std::min( m_w, (int)std::ceil( cx + r + h + 1 ) );
	const int ya = std::max( 0, (int)std::floor( cy - r - h - 1 ) ), yb = std::min( m_h, (int)std::ceil( cy + r + h + 1 ) );
	for ( int py = ya; py < yb; py++ )
		for ( int px = xa; px < xb; px++ )
		{
			const double qx = px + 0.5 - cx, qy = py + 0.5 - cy;
			double a = std::atan2( qy, qx ) - a0;
			a = std::fmod( std::fmod( a, 2 * M_PI ) + 2 * M_PI, 2 * M_PI );
			double d;
			if ( a <= sweep )
				d = std::fabs( std::hypot( qx, qy ) - r ) - h;
			else // past either end: the round caps
				d = std::min( std::hypot( px + 0.5 - ex[0], py + 0.5 - ey[0] ), std::hypot( px + 0.5 - ex[1], py + 0.5 - ey[1] ) ) - h;
			Blend( px, py, c, std::clamp( 0.5 - d, 0.0, 1.0 ) );
		}
}

int Canvas::TextWidth( const std::string &utf8, int px, Weight weight )
{
	Fonts &f = Fonts::Get();
	long w = 0;
	for ( uint32_t cp : DecodeUtf8( utf8 ) )
		w += f.Lookup( cp, px, weight ).advance;
	return (int)( w >> 6 );
}

int Canvas::Text( double x, double y, const std::string &utf8, int px, Color c, Weight weight )
{
	Fonts &f = Fonts::Get();
	const int base = (int)std::lround( y ) + f.Ascender( px, weight );
	long pen = (long)std::lround( x * 64 );
	for ( uint32_t cp : DecodeUtf8( utf8 ) )
	{
		const Fonts::Glyph &g = f.Lookup( cp, px, weight );
		const int gx = (int)( pen >> 6 ) + g.left, gy = base - g.top;
		for ( int j = 0; j < g.h; j++ )
			for ( int i = 0; i < g.w; i++ )
				if ( const uint8_t a = g.alpha[(size_t)j * g.w + i] )
					Blend( gx + i, gy + j, c, a / 255.0 );
		pen += g.advance;
	}
	return (int)( ( pen >> 6 ) - (long)x );
}

// Uncompressed PNG (stored deflate blocks): only for --screenshot, so size does not matter.
namespace
{
uint32_t Crc( const uint8_t *d, size_t n, uint32_t c = 0xffffffffu )
{
	static uint32_t table[256];
	if ( !table[1] )
		for ( uint32_t i = 0; i < 256; i++ )
		{
			uint32_t v = i;
			for ( int k = 0; k < 8; k++ )
				v = v & 1 ? 0xedb88320u ^ ( v >> 1 ) : v >> 1;
			table[i] = v;
		}
	for ( size_t i = 0; i < n; i++ )
		c = table[( c ^ d[i] ) & 0xff] ^ ( c >> 8 );
	return c;
}

void Be32( std::vector<uint8_t> &o, uint32_t v )
{
	for ( int s = 24; s >= 0; s -= 8 )
		o.push_back( uint8_t( v >> s ) );
}

void Chunk( std::vector<uint8_t> &o, const char *type, const std::vector<uint8_t> &data )
{
	Be32( o, (uint32_t)data.size() );
	const size_t start = o.size();
	o.insert( o.end(), type, type + 4 );
	o.insert( o.end(), data.begin(), data.end() );
	Be32( o, Crc( &o[start], o.size() - start ) ^ 0xffffffffu );
}
} // namespace

bool Canvas::WritePng( const std::string &path ) const
{
	std::vector<uint8_t> raw;
	for ( int y = 0; y < m_h; y++ )
	{
		raw.push_back( 0 );
		raw.insert( raw.end(), m_px.begin() + (long)y * m_w * 4, m_px.begin() + (long)( y + 1 ) * m_w * 4 );
	}
	std::vector<uint8_t> z = { 0x78, 0x01 };
	uint32_t a = 1, b = 0;
	for ( uint8_t v : raw )
		a = ( a + v ) % 65521, b = ( b + a ) % 65521;
	for ( size_t i = 0; i < raw.size(); i += 65535 )
	{
		const size_t n = std::min<size_t>( 65535, raw.size() - i );
		z.push_back( i + n == raw.size() ? 1 : 0 );
		z.push_back( uint8_t( n ) ), z.push_back( uint8_t( n >> 8 ) ), z.push_back( uint8_t( ~n ) ), z.push_back( uint8_t( ~n >> 8 ) );
		z.insert( z.end(), raw.begin() + (long)i, raw.begin() + (long)( i + n ) );
	}
	Be32( z, b << 16 | a );
	std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	std::vector<uint8_t> ihdr;
	Be32( ihdr, (uint32_t)m_w );
	Be32( ihdr, (uint32_t)m_h );
	ihdr.insert( ihdr.end(), { 8, 6, 0, 0, 0 } );
	Chunk( png, "IHDR", ihdr );
	Chunk( png, "IDAT", z );
	Chunk( png, "IEND", {} );
	FILE *f = fopen( path.c_str(), "wb" );
	if ( !f )
		return false;
	const bool ok = fwrite( png.data(), 1, png.size(), f ) == png.size();
	return fclose( f ) == 0 && ok;
}
