#pragma once
// A small RGBA canvas: anti-aliased rounded rectangles and text. Text uses FreeType with a font already installed on
// the system (Noto Sans CJK on the Frame); no font is bundled. Glyphs are rasterized once and cached.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct Color
{
	uint8_t r = 0, g = 0, b = 0, a = 255;
	static Color Hex( uint32_t rgb, uint8_t a = 255 ) { return { uint8_t( rgb >> 16 ), uint8_t( rgb >> 8 ), uint8_t( rgb ), a }; }
};

enum class Weight
{
	Regular,
	Bold
};

class Fonts
{
public:
	static Fonts &Get(); // loads the fonts on first use after Release()
	bool Ok() const { return m_ok[0]; }
	// Frees the fonts and the glyph cache (the panel is closed: ~8 MB back).
	void Release();
	struct Glyph
	{
		int w = 0, h = 0, left = 0, top = 0, advance = 0; // pixels; advance in 1/64 px
		std::vector<uint8_t> alpha;
	};
	const Glyph &Lookup( uint32_t codepoint, int px, Weight weight );
	int Ascender( int px, Weight weight );

private:
	Fonts() = default;
	void Load();
	bool m_loaded = false;
	void *m_lib = nullptr;
	void *m_face[2] = {};
	bool m_ok[2] = {};
	std::unordered_map<uint64_t, Glyph> m_cache;
};

class Canvas
{
public:
	Canvas( int w, int h );
	int W() const { return m_w; }
	int H() const { return m_h; }
	const std::vector<uint8_t> &Pixels() const { return m_px; } // RGBA, rows top to bottom

	void Clear( Color c );
	// Rounded rectangle, anti-aliased; `stroke` > 0 draws only an outline of that width.
	void RoundRect( double x, double y, double w, double h, double r, Color c, double stroke = 0 );
	void Circle( double cx, double cy, double r, Color c ) { RoundRect( cx - r, cy - r, 2 * r, 2 * r, r, c ); }
	// A stroke with round caps from (x0, y0) to (x1, y1).
	void Line( double x0, double y0, double x1, double y1, double width, Color c );
	// A filled triangle.
	void Triangle( double x0, double y0, double x1, double y1, double x2, double y2, Color c );
	// A stroke with round caps along a circle, from angle a0 to a1 (radians, screen coordinates: 0 = right, clockwise).
	void Arc( double cx, double cy, double r, double a0, double a1, double width, Color c );
	// Filled outlines (non-zero winding), anti-aliased: what SVG paths become once their curves are flattened.
	struct Pt
	{
		double x, y;
	};
	void FillPolygons( const std::vector<std::vector<Pt>> &contours, Color c );
	// Another canvas drawn over this one with its top-left at (x, y).
	void Blit( const Canvas &src, int x, int y );
	// Text with its top-left at (x, y); returns the width drawn. `px` is the font size in pixels.
	int Text( double x, double y, const std::string &utf8, int px, Color c, Weight weight = Weight::Regular );
	static int TextWidth( const std::string &utf8, int px, Weight weight = Weight::Regular );
	bool WritePng( const std::string &path ) const;

private:
	void Blend( int x, int y, Color c, double coverage );
	int m_w, m_h;
	std::vector<uint8_t> m_px;
};

std::vector<uint32_t> DecodeUtf8( const std::string &s );
