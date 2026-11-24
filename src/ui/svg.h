#pragma once
// Just enough SVG for SteamVR's controller line art (frame_controller_left/right.svg): filled <path>s with M/L/H/V/C/S/Z
// (absolute and relative), colours from fill="" or a class in <style>. No transforms, strokes or other shapes; anything
// else is skipped. The art is read from the SteamVR install at run time, never bundled.

#include "canvas.h"

#include <string>
#include <vector>

class SvgImage
{
public:
	bool Load( const std::string &path );
	bool Ok() const { return !m_shapes.empty() && m_w > 0 && m_h > 0; }
	double W() const { return m_w; }
	double H() const { return m_h; }
	// Draws it scaled by `scale` with its top-left at (x, y). The art's own colours are greys meant for a light
	// background; every shape is drawn in `tint` instead, its opacity following its original lightness (the lightest
	// shape gets tint's full alpha, the darkest 55% of it).
	void Draw( Canvas &c, double x, double y, double scale, Color tint ) const;

private:
	struct Shape
	{
		double lightness; // 0..1 of the original fill
		std::vector<std::vector<Canvas::Pt>> contours;
	};
	std::vector<Shape> m_shapes;
	double m_w = 0, m_h = 0;
};
