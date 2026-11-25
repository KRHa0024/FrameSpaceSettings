#include "panel.h"

#include "core/files.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
// Neutral near-black surfaces for contrast; the accent pink only for what is on or can be pressed.
const Color kBg = Color::Hex( 0x0c0c0e );
const Color kCard = Color::Hex( 0x16161a );
const Color kRaised = Color::Hex( 0x202026 ); // controls inside cards
const Color kLine = Color::Hex( 0x2c2c33 );
const Color kInk = Color::Hex( 0xf4f4f6 );
const Color kMuted = Color::Hex( 0x8e8e98 );
const Color kDim = Color::Hex( 0x6c6c76 );        // disabled text: dimmer than kInk, still readable
const Color kOffTrack = Color::Hex( 0x3a3a43 );   // switch track when off / disabled
const Color kOffEdge = Color::Hex( 0x5a5a63 );
const Color kOffKnob = Color::Hex( 0xb4b4bc );
const Color kHover = Color::Hex( 0x2a2a32 );      // hover fill (with a kAccentText outline)
const Color kReadout = Color::Hex( 0xa8a8b2 );
const Color kAccent = Color::Hex( 0xdb2777 );     // primary
const Color kAccentText = Color::Hex( 0xf472b6 ); // secondary: accent for text and outlines on dark
const Color kAccentHover = Color::Hex( 0xe8458c );
const Color kWhite = Color::Hex( 0xffffff );
const Color kDanger = Color::Hex( 0xdc2626 ); // quit only
const Color kDangerHover = Color::Hex( 0xef4444 );

struct Strings
{
	const char *drag, *turn, *on, *off, *reset, *moved;
	const char *gainLabel, *gainUnit, *left, *right, *snapTurn, *snapAngle, *axisLock;
	const char *gravity, *gravityStrength, *earth, *moon, *zero;
	const char *haptics, *reset2, *bindings;
	const char *waiting, *noDriver, *driverBlocked, *noInput, *otherLanguage;
	const char *bindingsTitle, *unbound, *bothHands, *leftHand, *rightHand, *sep, *noArt, *more;
	const char *dragDesc, *turnDesc, *resetDesc, *gravityDesc, *autoStart, *quit;
	const char *checkUpdates, *driverInstalled, *driverUpdated, *updateAvailable, *updateNow, *updating, *updateFailed, *retry;
	const char *updated, *welcome, *ok;
};

const Strings kJa = {
	"スペースドラッグ",
	"スペースターン",
	"有効",
	"無効",
	"元の位置に戻す",
	"高さ %+.2f m・水平 %.2f m・回転 %+.0f°",
	"移動の倍率",
	" 倍",
	"左手",
	"右手",
	"スナップターン",
	"スナップ角度",
	"軸の固定",
	"グラビティ",
	"重力の強さ",
	"地球",
	"月",
	"無重力",
	"操作したときに振動する",
	"位置リセット",
	"バインド設定を開く",
	"トラッキングを待っています",
	"ドライバが読み込まれていません。SteamVR を再起動すると読み込まれます（それまでは移動できません）",
	"前回の起動でドライバの準備が完了しなかったため、停止しています。~/.local/state/frame-space-settings/driver-hooking を削除して SteamVR を再起動してください",
	"コントローラの入力を取得できません（SteamVR Input）",
	"English",
	"バインド",
	"未設定",
	"左右の",
	"左手の",
	"右手の",
	"、",
	"コントローラの絵を読み込めませんでした",
	"、ほか %d 件",
	"空間をドラッグして移動します",
	"空間をドラッグして向きを変更します",
	"移動した分と回転をリセットします",
	"移動した後に重力を有効化します",
	"SteamVR と一緒に起動",
	"終了",
	"更新を確認する",
	"ドライバをインストールしました。SteamVR を再起動すると使えるようになります",
	"新しいドライバは、SteamVR を再起動すると有効になります",
	"新しいバージョン %s があります（更新すると位置はリセットされます）",
	"更新する",
	"%s に更新しています…",
	"更新できませんでした。時間をおいてもう一度お試しください",
	"もう一度確認する",
	"%s に更新しました。再起動しています…",
	"準備ができました。操作するボタンは右のバインドで確認できます",
	"OK",
};

const Strings kEn = {
	"Space drag",
	"Space turn",
	"On",
	"Off",
	"Back to original",
	"Up %+.2f m · across %.2f m · turned %+.0f°",
	"Drag multiplier",
	" x",
	"Left",
	"Right",
	"Snap turn",
	"Snap angle",
	"Axis lock",
	"Gravity",
	"Gravity strength",
	"Earth",
	"Moon",
	"Zero-G",
	"Vibrate on actions",
	"Position reset",
	"Open bindings",
	"Waiting for tracking",
	"The driver is not loaded; restart SteamVR to load it (nothing can be moved until then)",
	"The driver stopped itself after an unfinished start; delete ~/.local/state/frame-space-settings/driver-hooking and restart SteamVR",
	"Controller input unavailable (SteamVR Input)",
	"日本語",
	"Bindings",
	"Not bound",
	"L+R ",
	"L ",
	"R ",
	", ",
	"Could not load the controller art",
	", +%d more",
	"Drag the space to move",
	"Drag the space to change your facing",
	"Resets how far you moved and turned",
	"Turns on gravity after you move",
	"Start with SteamVR",
	"Quit",
	"Check for updates",
	"The driver is installed. Restart SteamVR to start using it",
	"The new driver takes effect after a SteamVR restart",
	"Version %s is available (updating resets your position)",
	"Update",
	"Updating to %s…",
	"The update did not work. Please try again later",
	"Check again",
	"Updated to %s. Restarting…",
	"Ready. The bindings on the right show which buttons to use",
	"OK",
};

// Button and press names for the bindings list.
std::string InputName( const std::string &input, const std::string &slot, bool ja )
{
	struct N
	{
		const char *path, *ja, *en;
	};
	static const N names[] = { { "/input/bumper", "バンパー", "bumper" }, { "/input/trigger", "トリガー", "trigger" },
		{ "/input/grip", "グリップ", "grip" }, { "/input/thumbstick", "スティック", "stick" }, { "/input/a", "A ボタン", "A" },
		{ "/input/b", "B ボタン", "B" }, { "/input/x", "X ボタン", "X" }, { "/input/y", "Y ボタン", "Y" },
		{ "/input/view", "View ボタン", "View" }, { "/input/menu", "メニューボタン", "Menu" }, { "/input/system", "Steam ボタン", "Steam" },
		{ "/input/dpad_up", "十字キー上", "D-pad up" }, { "/input/dpad_down", "十字キー下", "D-pad down" },
		{ "/input/dpad_left", "十字キー左", "D-pad left" }, { "/input/dpad_right", "十字キー右", "D-pad right" },
		{ "/input/thumbrest", "サムレスト", "thumbrest" } };
	if ( input == "/input/thumbstick" && slot != "touch" && slot != "position" )
		return ja ? "スティック押し込み" : "stick click"; // any press of the stick (click, double, long...): SlotName adds which one
	for ( const N &n : names )
		if ( input == n.path )
			return ja ? n.ja : n.en;
	const size_t k = input.rfind( '/' );
	return k == std::string::npos ? input : input.substr( k + 1 );
}

std::string SlotName( const std::string &slot, bool ja )
{
	if ( slot == "double" )
		return ja ? "・2回押し" : " (double)";
	if ( slot == "long" )
		return ja ? "・長押し" : " (long)";
	if ( slot == "held" )
		return ja ? "・ホールド" : " (held)";
	if ( slot == "touch" )
		return ja ? "・タッチ" : " (touch)";
	if ( slot == "single" )
		return ja ? "・1回押し" : " (single)";
	return ""; // click, or a mode-specific slot: the button name says enough
}

// Gravity presets, m/s^2.
constexpr double kEarth = 9.8, kMoon = 1.62;

std::string Fmt( const char *f, ... ) __attribute__( ( format( printf, 1, 2 ) ) );
std::string Fmt( const char *f, ... )
{
	char buf[256];
	va_list ap;
	va_start( ap, f );
	vsnprintf( buf, sizeof buf, f, ap );
	va_end( ap );
	return buf;
}

std::string Trim( double v )
{
	std::string s = Fmt( "%.2f", v );
	while ( s.size() > 1 && s.back() == '0' )
		s.pop_back();
	if ( s.back() == '.' )
		s.pop_back();
	return s;
}
// Text top for text of `px` vertically centred in a box.
double TextY( double top, double h, int px )
{
	return top + ( h - px * 1.45 ) / 2.0;
}
} // namespace

std::string ResolveLanguage( const std::string &setting )
{
	if ( setting == "ja" || setting == "en" )
		return setting;
	for ( const std::string &p : { files::Home() + "/.steam/registry.vdf", files::Home() + "/.local/share/Steam/registry.vdf" } )
	{
		std::string text;
		if ( !files::ReadFile( p, text ) )
			continue;
		const size_t k = text.find( "\"language\"" );
		if ( k != std::string::npos )
		{
			const size_t a = text.find( '"', k + 10 ), b = a == std::string::npos ? a : text.find( '"', a + 1 );
			if ( b != std::string::npos )
				return text.substr( a + 1, b - a - 1 ) == "japanese" ? "ja" : "en";
		}
	}
	const char *lang = getenv( "LC_ALL" );
	if ( !lang || !*lang )
		lang = getenv( "LANG" );
	return lang && !strncmp( lang, "ja", 2 ) ? "ja" : "en";
}

bool ApplySettingsAct( Act a, Settings &s )
{
	auto step = []( double v, double d, double lo, double hi ) { return std::clamp( std::round( ( v + d ) * 100 ) / 100, lo, hi ); };
	switch ( a )
	{
	case Act::Drag: s.dragEnabled = !s.dragEnabled; return true;
	case Act::Turn: s.turnEnabled = !s.turnEnabled; return true;
	case Act::GainMinus: s.dragGain = step( s.dragGain, -0.25, 0.25, 4.0 ); return true;
	case Act::GainPlus: s.dragGain = step( s.dragGain, 0.25, 0.25, 4.0 ); return true;
	case Act::DragLeft: s.dragLeft = !s.dragLeft; return true;
	case Act::DragRight: s.dragRight = !s.dragRight; return true;
	case Act::LockX: s.lockX = !s.lockX; return true;
	case Act::LockY: s.lockY = !s.lockY; return true;
	case Act::LockZ: s.lockZ = !s.lockZ; return true;
	case Act::SnapTurn: s.snapTurn = !s.snapTurn; return true;
	case Act::SnapMinus: s.snapDegrees = step( s.snapDegrees, -5, 5, 90 ); return true;
	case Act::SnapPlus: s.snapDegrees = step( s.snapDegrees, 5, 5, 90 ); return true;
	case Act::TurnLeft: s.turnLeft = !s.turnLeft; return true;
	case Act::TurnRight: s.turnRight = !s.turnRight; return true;
	case Act::Gravity: s.gravity = !s.gravity; return true;
	case Act::GravityEarth: s.gravityStrength = kEarth; return true;
	case Act::GravityMoon: s.gravityStrength = kMoon; return true;
	case Act::GravityZero: s.gravityStrength = 0; return true;
	case Act::GravityMinus: s.gravityStrength = step( s.gravityStrength, -0.1, 0, kGravityMax ); return true;
	case Act::GravityPlus: s.gravityStrength = step( s.gravityStrength, 0.1, 0, kGravityMax ); return true;
	case Act::Haptics: s.haptics = !s.haptics; return true;
	case Act::CheckUpdates: s.checkUpdates = !s.checkUpdates; return true;
	case Act::WelcomeOk: s.welcomeDone = true; return true;
	case Act::ResetEnabled: s.resetEnabled = !s.resetEnabled; return true;
	default: return false;
	}
}

Act Panel::HitTest( int x, int y ) const
{
	for ( const Button &b : m_buttons )
		if ( x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h )
			return b.act;
	return Act::None;
}

void Panel::DrawIcon( Canvas &c )
{
	// 128-unit design, scaled to the canvas. Keep in step with data/icon.svg.
	const double k = c.W() / 128.0;
	c.Clear( { 0, 0, 0, 0 } );
	c.RoundRect( 6 * k, 6 * k, 116 * k, 116 * k, 28 * k, kCard );
	c.RoundRect( 6 * k, 6 * k, 116 * k, 116 * k, 28 * k, kLine, 2 * k );
	// Turn: a 270-degree arc, open at the top, ending in a solid arrow head.
	const double cx = 64 * k, cy = 64 * k, r = 39 * k, a0 = -40 * M_PI / 180, a1 = 205 * M_PI / 180;
	c.Arc( cx, cy, r, a0, a1, 8 * k, kAccent );
	{
		const double px = cx + r * std::cos( a1 ), py = cy + r * std::sin( a1 );
		const double tx = -std::sin( a1 ), ty = std::cos( a1 ); // direction of travel (clockwise on screen)
		const double nx = -ty, ny = tx;                         // across the stroke
		const double len = 15 * k, half = 11 * k;
		c.Triangle( px + tx * len, py + ty * len, px + nx * half, py + ny * half, px - nx * half, py - ny * half, kAccent );
	}
	// Move: a cross with four solid arrow heads.
	const double arm = 15 * k, tip = 25 * k, half = 7.5 * k;
	c.Line( cx - arm, cy, cx + arm, cy, 5 * k, kInk );
	c.Line( cx, cy - arm, cx, cy + arm, 5 * k, kInk );
	const double dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
	for ( int i = 0; i < 4; i++ )
	{
		const double bx = cx + dx[i] * ( arm - 1 * k ), by = cy + dy[i] * ( arm - 1 * k );
		c.Triangle( cx + dx[i] * tip, cy + dy[i] * tip, bx - dy[i] * half, by + dx[i] * half, bx + dy[i] * half, by - dx[i] * half, kInk );
	}
}

void Panel::Draw( Canvas &c, const PanelModel &m, Act hover, const std::string &lang )
{
	const Strings &S = lang == "ja" ? kJa : kEn;
	const Settings &st = m.settings;
	m_buttons.clear();
	c.Clear( kBg );

	// Grid: cards are padded 24, wells inside them 16, so text and controls line up 40 from each card edge.
	// One well module (64 tall, 12 apart), one small switch (60x32), one stepper button (56x52), one hover style.
	const int wellH = 64, pitch = 76, headH = 120;

	auto centered = [&]( double x, double w, double y, const std::string &t, int px, Color col, Weight wt ) {
		c.Text( x + ( w - Canvas::TextWidth( t, px, wt ) ) / 2.0, y, t, px, col, wt );
	};
	auto textY = []( double top, double h, int px ) { return top + ( h - px * 1.45 ) / 2.0; }; // vertically centred
	auto hoverBox = [&]( double x, double y, double w, double h, double r ) {
		c.RoundRect( x, y, w, h, r, kHover );
		c.RoundRect( x, y, w, h, r, kAccentText, 2 );
	};
	auto card = [&]( int x, int y, int w, int h, bool on ) {
		c.RoundRect( x, y, w, h, 24, kCard );
		c.RoundRect( x, y, w, h, 24, on ? kAccent : kLine, on ? 2 : 1.5 );
	};
	// A switch. Disabled shows no accent at all.
	auto knob = [&]( double x, double y, double w, double h, bool on, bool live = true ) {
		const double r = h / 2 - 4;
		if ( on && live )
		{
			c.RoundRect( x, y, w, h, h / 2, kAccent );
			c.Circle( x + w - h / 2, y + h / 2, r, kWhite );
			return;
		}
		c.RoundRect( x, y, w, h, h / 2, kOffTrack );
		if ( !on )
			c.RoundRect( x, y, w, h, h / 2, kOffEdge, 1.5 );
		c.Circle( on ? x + w - h / 2 : x + h / 2, y + h / 2, r, on ? kMuted : kOffKnob );
	};
	// A pill: an action or a choice. `selected` = the chosen preset (or the reset while there is something to undo).
	auto pill = [&]( int x, int y, int w, int h, const std::string &label, Act act, int px, bool selected = false, bool live = true,
					bool ghost = false ) {
		const bool hot = live && hover == act; // a disabled control takes no hover and no clicks
		const double r = h / 2.0;
		Color ink = live ? kInk : kDim;
		if ( selected && live )
		{
			c.RoundRect( x, y, w, h, r, hot ? kAccentHover : kAccent );
			ink = kWhite;
		}
		else if ( selected )
		{
			c.RoundRect( x, y, w, h, r, kLine );
			c.RoundRect( x, y, w, h, r, kMuted, 2 );
			ink = Color::Hex( 0xa0a0aa );
		}
		else if ( hot )
			hoverBox( x, y, w, h, r );
		else
		{
			if ( !ghost )
				c.RoundRect( x, y, w, h, r, kRaised );
			c.RoundRect( x, y, w, h, r, kLine, 1.5 );
			if ( ghost )
				ink = kMuted;
		}
		centered( x, w, textY( y, h, px ), label, px, ink, Weight::Bold );
		if ( live )
			m_buttons.push_back( { x, y, w, h, act } );
	};
	// A labelled toggle: the label left, a 60x32 switch 16 from the right edge. The whole thing is the target.
	auto toggle = [&]( int x, int y, int w, int h, const char *label, bool on, Act act, bool live, Color fill ) {
		if ( live && hover == act )
			hoverBox( x, y, w, h, 16 );
		else
			c.RoundRect( x, y, w, h, 16, fill );
		c.Text( x + 16, textY( y, h, 22 ), label, 22, live ? kInk : kDim );
		knob( x + w - 16 - 60, y + ( h - 32 ) / 2, 60, 32, on, live );
		if ( live )
			m_buttons.push_back( { x, y, w, h, act } );
	};
	// A check box with a short label after it, as one target; `right` is its right edge, returns its left edge.
	auto checkbox = [&]( int right, int y, int h, const char *label, bool on, Act act, bool live ) {
		const int box = 32, pad = 14, lw = Canvas::TextWidth( label, 24, Weight::Bold ), w = pad + box + 10 + lw + pad, x = right - w;
		if ( live && hover == act )
			hoverBox( x, y, w, h, 12 );
		const double bx = x + pad, by = y + ( h - box ) / 2.0;
		if ( on && live )
		{
			c.RoundRect( bx, by, box, box, 8, kAccent );
			c.Line( bx + 8, by + 17, bx + 14, by + 23, 4, kWhite );
			c.Line( bx + 14, by + 23, bx + 25, by + 10, 4, kWhite );
		}
		else
		{
			c.RoundRect( bx, by, box, box, 8, kOffTrack );
			c.RoundRect( bx, by, box, box, 8, on ? kMuted : kOffEdge, 1.5 );
			if ( on ) // checked but disabled: the mark without the accent
			{
				c.Line( bx + 8, by + 17, bx + 14, by + 23, 4, kMuted );
				c.Line( bx + 14, by + 23, bx + 25, by + 10, 4, kMuted );
			}
		}
		c.Text( bx + box + 10, textY( y, h, 24 ), label, 24, live ? kInk : kDim, Weight::Bold );
		if ( live )
			m_buttons.push_back( { x, y, w, h, act } );
		return x;
	};
	auto well = [&]( int x, int y, int w, const char *label, bool live ) {
		c.RoundRect( x, y, w, wellH, 16, kBg );
		c.Text( x + 16, textY( y, wellH, 22 ), label, 22, live ? kInk : kDim );
	};
	auto stepper = [&]( int right, int y, int h, int valueW, const std::string &value, Act minus, Act plus, bool live ) {
		const int bw = 56, x = right - 2 * bw - valueW;
		pill( x, y, bw, h, "−", minus, 26, false, live );
		pill( right - bw, y, bw, h, "+", plus, 26, false, live );
		centered( x + bw, valueW, textY( y, h, 24 ), value, 24, live ? kInk : kDim, Weight::Bold );
	};
	// A typed number: − [value] + ; the value box opens SteamVR's keyboard. `right` is the + button's right edge.
	auto numberBox = [&]( int right, int y, const std::string &value, Act minus, Act plus, Act typed, bool live ) {
		const int bw = 56, bh = 52, vw = 180, plusX = right - bw, vx = plusX - 8 - vw, minusX = vx - 8 - bw;
		pill( minusX, y, bw, bh, "−", minus, 26, false, live );
		pill( plusX, y, bw, bh, "+", plus, 26, false, live );
		if ( live && hover == typed )
			hoverBox( vx, y, vw, bh, 12 );
		else
		{
			c.RoundRect( vx, y, vw, bh, 12, kBg );
			c.RoundRect( vx, y, vw, bh, 12, live ? kAccentText : kOffEdge, 1.5 );
		}
		centered( vx, vw - 24, textY( y, bh, 24 ), value, 24, live ? kInk : kDim, Weight::Bold );
		{
			// A small pencil: the value can be typed.
			const double px = vx + vw - 30, py = y + bh / 2.0, k = 7;
			const Color pc = live ? kAccentText : kDim;
			c.Line( px - k + 2, py + k - 2, px + k, py - k, 3, pc );
			c.Triangle( px - k - 1, py + k + 1, px - k + 4, py + k - 1, px - k + 1, py + k - 4, pc );
		}
		if ( live )
			m_buttons.push_back( { vx, y, vw, bh, typed } );
		return minusX;
	};
	// Card head: title, On / Off and the big switch; the whole head toggles.
	// With `desc`: title and On / Off on one line, a one-line explanation under them, the switch beside the title.
	auto head = [&]( int x, int y, int w, int h, const char *title, bool on, Act act, const char *desc = nullptr ) {
		if ( hover == act )
			hoverBox( x + 8, y + 8, w - 16, h - 16, 16 ); // 8 from the card edge on every side: radius 24 - 8
		if ( desc )
		{
			const int ty = y + 18;
			c.Text( x + 40, ty, title, 32, kInk, Weight::Bold );
			c.Text( x + 40 + Canvas::TextWidth( title, 32, Weight::Bold ) + 16, ty + 11, on ? S.on : S.off, 22, on ? kAccentText : kMuted,
				Weight::Bold );
			c.Text( x + 40, ty + 50, desc, 19, kMuted ); // ends ~10 px above the hover outline (8 px inside the head)
			knob( x + w - 40 - 88, ty + 2, 88, 44, on );
		}
		else
		{
			const int ty = y + std::max( 24, ( h - 98 ) / 2 ); // title 32 px + status 22 px; 24 from the top at least
			c.Text( x + 40, ty, title, 32, kInk, Weight::Bold );
			c.Text( x + 40, ty + 54, on ? S.on : S.off, 22, on ? kAccentText : kMuted, Weight::Bold );
			knob( x + w - 40 - 88, y + ( h - 44 ) / 2, 88, 44, on );
		}
		m_buttons.push_back( { x, y, w, h, act } );
	};

	// Header: name; start with SteamVR, language.
	c.Text( 40, 30, "Frame Space Settings", 40, kInk, Weight::Bold );
	if ( !m.version.empty() )
		c.Text( 40 + Canvas::TextWidth( "Frame Space Settings", 40, Weight::Bold ) + 16, 47, "v" + m.version, 20, kMuted );
	{
		const int lw = Canvas::TextWidth( S.otherLanguage, 20, Weight::Bold ) + 56, lx = kPanelW - 40 - lw;
		pill( lx, 32, lw, 52, S.otherLanguage, Act::Language, 20, false, true, true );
		const int aw = Canvas::TextWidth( S.autoStart, 22 ) + 16 + 24 + 60 + 16, ax = lx - 12 - aw;
		toggle( ax, 32, aw, 52, S.autoStart, m.autoStart, Act::AutoStart, true, kRaised );
		const int uw = Canvas::TextWidth( S.checkUpdates, 22 ) + 16 + 24 + 60 + 16;
		toggle( ax - 12 - uw, 32, uw, 52, S.checkUpdates, st.checkUpdates, Act::CheckUpdates, true, kRaised );
	}

	// Space drag / space turn: one card each.
	const int fy = 108, fh = 364, fw = 590, descHeadH = 112;
	const int r1 = fy + descHeadH + 12, r2 = r1 + pitch, r3 = r2 + pitch;
	auto hands = [&]( int x, int y, int w, bool left, bool right, Act la, Act ra, bool live ) {
		const int cw = ( w - 12 ) / 2;
		toggle( x, y, cw, wellH, S.left, left, la, live, kBg );
		toggle( x + w - cw, y, cw, wellH, S.right, right, ra, live, kBg );
	};
	{
		const int x = 40, wx = x + 24, ww = fw - 48;
		const bool live = st.dragEnabled;
		card( x, fy, fw, fh, live );
		head( x, fy, fw, descHeadH, S.drag, live, Act::Drag, S.dragDesc );
		well( wx, r1, ww, S.gainLabel, live );
		stepper( wx + ww - 16, r1 + 6, 52, 112, Trim( st.dragGain ) + S.gainUnit, Act::GainMinus, Act::GainPlus, live );
		well( wx, r2, ww, S.axisLock, live );
		{
			int right = wx + ww - 8;
			right = checkbox( right, r2 + 6, 52, "Z", st.lockZ, Act::LockZ, live ) - 4;
			right = checkbox( right, r2 + 6, 52, "Y", st.lockY, Act::LockY, live ) - 4;
			checkbox( right, r2 + 6, 52, "X", st.lockX, Act::LockX, live );
		}
		hands( wx, r3, ww, st.dragLeft, st.dragRight, Act::DragLeft, Act::DragRight, live ); // same line as space turn's
	}
	{
		const int x = 650, wx = x + 24, ww = fw - 48;
		const bool live = st.turnEnabled;
		card( x, fy, fw, fh, live );
		head( x, fy, fw, descHeadH, S.turn, live, Act::Turn, S.turnDesc );
		toggle( wx, r1, ww, wellH, S.snapTurn, st.snapTurn, Act::SnapTurn, live, kBg );
		well( wx, r2, ww, S.snapAngle, live && st.snapTurn );
		stepper( wx + ww - 16, r2 + 6, 52, 112, Trim( st.snapDegrees ) + "°", Act::SnapMinus, Act::SnapPlus, live && st.snapTurn );
		hands( wx, r3, ww, st.turnLeft, st.turnRight, Act::TurnLeft, Act::TurnRight, live );
	}

	// Gravity: the switch on the left, the strength (presets, fine steps, typed value) on the right.
	const int gy = fy + fh + 20, gh = descHeadH + 12 + wellH + 24;
	{
		const int x = 40, w = 1200, wx = x + 24, ww = w - 48, ry = gy + descHeadH + 12;
		const bool live = st.gravity;
		card( x, gy, w, gh, live );
		head( x, gy, w, descHeadH, S.gravity, live, Act::Gravity, S.gravityDesc );
		well( wx, ry, ww, S.gravityStrength, live );
		const int by = ry + 6, bh = 52, right = wx + ww - 16;
		const int minusX = right - 56 - 8 - 180 - 8 - 56;
		const int sx = wx + 16 + Canvas::TextWidth( S.gravityStrength, 22 ) + 32;
		struct P
		{
			const char *name;
			double g;
			Act act;
		};
		const P presets[] = { { S.earth, kEarth, Act::GravityEarth }, { S.moon, kMoon, Act::GravityMoon }, { S.zero, 0, Act::GravityZero } };
		auto label = [&]( const P &p, bool withValue ) { return withValue ? std::string( p.name ) + "  " + Trim( p.g ) : std::string( p.name ); };
		auto rowWidth = [&]( bool withValue ) {
			int total = 0;
			for ( const P &p : presets )
				total += Canvas::TextWidth( label( p, withValue ), 21, Weight::Bold ) + 48 + 12;
			return total - 12;
		};
		const bool withValue = sx + rowWidth( true ) + 24 <= minusX; // too tight (long words): the value box shows the number
		int px = sx;
		for ( const P &p : presets )
		{
			const std::string l = label( p, withValue );
			const int pw = Canvas::TextWidth( l, 21, Weight::Bold ) + 48;
			pill( px, by, pw, bh, l, p.act, 21, std::fabs( st.gravityStrength - p.g ) < 0.005, live );
			px += pw + 12;
		}
		numberBox( right, by, Trim( st.gravityStrength ) + " m/s²", Act::GravityMinus, Act::GravityPlus, Act::GravityValue, live );
	}

	// Position reset: the controller action's switch, and the way back from the panel with how far the world is moved.
	{
		const int x = 40, y = gy + gh + 20, w = 1200, h = descHeadH, split = 480;
		const bool live = st.resetEnabled;
		card( x, y, w, h, live );
		head( x, y, split, h, S.reset2, live, Act::ResetEnabled, S.resetDesc );
		c.Line( x + split, y + 24, x + split, y + h - 24, 1.5, kLine );
		double yaw = WrapAngle( m.off.yaw ) * 180 / M_PI;
		if ( std::fabs( yaw ) < 0.5 )
			yaw = 0;
		double up = -m.off.t.y;
		if ( std::fabs( up ) < 0.005 )
			up = 0;
		const double across = std::hypot( m.off.t.x, m.off.t.z );
		const bool moved = up != 0 || yaw != 0 || across >= 0.005;
		const int ph = 52, py = y + ( h - ph ) / 2, rw = Canvas::TextWidth( S.reset, 22, Weight::Bold ) + 64, rx = x + w - 40 - rw;
		pill( rx, py, rw, ph, S.reset, Act::Reset, 22, moved ); // the accent only while there is something to undo
		const std::string readout = Fmt( S.moved, up, across, yaw );
		int px = 20;
		while ( px > 15 && x + split + 32 + Canvas::TextWidth( readout, px ) > rx - 24 )
			px--;
		c.Text( rx - 24 - Canvas::TextWidth( readout, px ), textY( py, ph, px ), readout, px, kReadout );
	}

	// Bindings, with the vibration switch (it belongs to the controller) and the binding editor.
	{
		const int x = 1260, y = 108, w = kPanelW - 40 - x, h = gy + gh + 20 + descHeadH - y;
		DrawBindings( c, m, lang, x, y, w, h );
		const int bw = Canvas::TextWidth( S.bindings, 20, Weight::Bold ) + 48;
		pill( x + w - 40 - bw, y + 26, bw, 48, S.bindings, Act::EditBindings, 20 );
		toggle( x + 24, y + h - 24 - wellH, w - 48, wellH, S.haptics, st.haptics, Act::Haptics, true, kBg );
	}

	// Quit: bottom right, the one red thing on the panel.
	const int qh = 52, qy = kPanelH - 40 - qh, qw = Canvas::TextWidth( S.quit, 22, Weight::Bold ) + 72, qx = kPanelW - 40 - qw;
	{
		const bool hot = hover == Act::Quit;
		c.RoundRect( qx, qy, qw, qh, qh / 2.0, hot ? kDangerHover : kDanger );
		centered( qx, qw, textY( qy, qh, 22 ), S.quit, 22, kWhite, Weight::Bold );
		m_buttons.push_back( { qx, qy, qw, qh, Act::Quit } );
	}

	// One notice at a time, most important first: problems (accent outline), then updates and hints (plain outline). Some
	// carry a button at the strip's right end.
	std::string note;
	bool warn = true;
	const char *button = nullptr;
	Act act = Act::None;
	const std::string tag = m.updateTag;
	// An update can also be what fixes a driver problem, so it is offered in those states too.
	std::string updateNote;
	const char *updateButton = nullptr;
	Act updateAct = Act::None;
	switch ( st.checkUpdates || m.update == 4 || m.update == 5 ? m.update : 0 ) // Updater::State
	{
	case 3: updateNote = Fmt( S.updateAvailable, tag.c_str() ), updateButton = S.updateNow, updateAct = Act::UpdateNow; break;
	case 4: updateNote = Fmt( S.updating, tag.c_str() ); break;
	case 5: updateNote = Fmt( S.updated, tag.c_str() ); break;
	case 6: updateNote = S.updateFailed, updateButton = S.retry, updateAct = Act::UpdateRetry; break;
	default: break;
	}
	if ( !m.inputOk )
		note = S.noInput;
	else if ( m.waiting )
		note = S.waiting;
	else if ( !updateNote.empty() )
		note = updateNote, button = updateButton, act = updateAct, warn = m.update == 6;
	else if ( m.driverBlocked )
		note = S.driverBlocked;
	else if ( !m.driverMode )
		note = m.driverRestart ? S.driverInstalled : S.noDriver;
	else
	{
		warn = false;
		if ( m.driverRestart )
			note = S.driverUpdated;
		else if ( !st.welcomeDone )
			note = S.welcome, button = S.ok, act = Act::WelcomeOk;
	}
	if ( !note.empty() )
	{
		const int h = 52, y = qy + ( qh - h ) / 2, sw = qx - 16 - 40;
		c.RoundRect( 40, y, sw, h, 14, kRaised );
		c.RoundRect( 40, y, sw, h, 14, warn ? kAccentText : kLine, 1.5 );
		c.Circle( 64, y + h / 2, 6, warn ? kAccentText : kMuted );
		int right = 40 + sw - 8;
		if ( button )
		{
			const int bw = Canvas::TextWidth( button, 20, Weight::Bold ) + 48;
			pill( right - bw, y + 7, bw, h - 14, button, act, 20, act == Act::UpdateNow );
			right -= bw + 16;
		}
		const int px = Canvas::TextWidth( note, 20 ) <= right - 82 ? 20 : 18;
		c.Text( 82, textY( y, h, px ), note, px, kInk );
	}
}

void Panel::LoadArt()
{
	m_artTried = true;
	const std::string base = m_root + "/drivers/frame_controller/resources";
	SvgImage svg[2];
	if ( !svg[0].Load( base + "/icons/frame_controller_left.svg" ) || !svg[1].Load( base + "/icons/frame_controller_right.svg" ) )
		return;
	// Button points: input_source."/input/x".binding_image_point = [x, y], in units a quarter of the art's (SteamVR's
	// binding editor scales them by 4), measured from the right controller's left edge; mirrored for the left one.
	std::string json;
	if ( files::ReadFile( base + "/input/frame_controller_profile.json", json ) )
	{
		size_t i = json.find( "\"input_source\"" );
		while ( i != std::string::npos && ( i = json.find( "\"/input/", i ) ) != std::string::npos )
		{
			const size_t e = json.find( '"', i + 1 );
			const std::string key = json.substr( i + 1, e - i - 1 );
			const size_t p = json.find( "\"binding_image_point\"", e ), next = json.find( "\"/input/", e );
			i = e;
			if ( p == std::string::npos || ( next != std::string::npos && p > next ) )
				continue;
			const size_t b = json.find( '[', p );
			if ( b == std::string::npos )
				continue;
			char *end = nullptr;
			const double px = strtod( json.c_str() + b + 1, &end );
			while ( end && ( *end == ',' || *end == ' ' ) )
				end++;
			const double py = strtod( end, nullptr );
			m_points[key] = { px * 4, py * 4 };
		}
	}
	m_artW = svg[1].W();
	const int w = 180;
	m_artScale = w / m_artW;
	for ( int k = 0; k < 2; k++ )
	{
		m_art[k] = std::make_unique<Canvas>( w, (int)std::ceil( svg[k].H() * m_artScale ) );
		m_art[k]->Clear( { 0, 0, 0, 0 } );
		svg[k].Draw( *m_art[k], 0, 0, m_artScale, Color::Hex( 0xc8c8d0 ) );
	}
}

void Panel::DrawBindings( Canvas &c, const PanelModel &m, const std::string &lang, int x, int y, int w, int h )
{
	const Strings &S = lang == "ja" ? kJa : kEn;
	const bool ja = lang == "ja";
	const Settings &st = m.settings;
	if ( !m_artTried )
		LoadArt();
	c.RoundRect( x, y, w, h, 24, kCard );
	c.RoundRect( x, y, w, h, 24, kLine, 1.5 );
	c.Text( x + 40, y + 24, S.bindingsTitle, 32, kInk, Weight::Bold );

	const char *names[3] = { S.drag, S.turn, S.reset2 };
	const bool actionOn[3] = { st.dragEnabled, st.turnEnabled, st.resetEnabled };
	auto handOn = [&]( int action, int hand ) {
		if ( !actionOn[action] )
			return false;
		if ( action == 0 && hand >= 0 )
			return hand == 0 ? st.dragLeft : st.dragRight;
		if ( action == 1 && hand >= 0 )
			return hand == 0 ? st.turnLeft : st.turnRight;
		return true;
	};
	auto badge = [&]( double cx, double cy, int action, bool on ) {
		c.Circle( cx, cy, 16, kCard );
		c.Circle( cx, cy, 13, on ? kAccent : kOffTrack );
		const std::string n = std::to_string( action + 1 );
		const int tw = Canvas::TextWidth( n, 17, Weight::Bold );
		c.Text( cx - tw / 2.0, cy - 17 * 1.45 / 2, n, 17, on ? kWhite : kMuted, Weight::Bold );
	};

	// The two controllers, with a numbered badge on every bound button.
	const int artTop = y + 92;
	int listTop = y + 120;
	if ( m_art[0] && m_art[1] )
	{
		const int gap = w - 80 - 2 * m_art[0]->W();
		const int ax[2] = { x + 40, x + 40 + m_art[0]->W() + gap };
		for ( int k = 0; k < 2; k++ )
			c.Blit( *m_art[k], ax[k], artTop );
		// One badge per action on a button (several presses of one button for one action still show one badge), centred
		// as a row on the button so a crowded button does not run off the art.
		std::map<std::string, std::vector<int>> onButton; // hand + input -> actions, in action order
		for ( const BindingInfo &b : m.bindings )
		{
			if ( b.hand < 0 || !m_points.count( b.input ) )
				continue;
			auto &v = onButton[std::to_string( b.hand ) + b.input];
			if ( std::find( v.begin(), v.end(), b.action ) == v.end() )
				v.push_back( b.action );
		}
		// Each badge sits a little outside its button, away from the middle of that controller, with a thin line back to
		// a dot on the button. Badges that would overlap (buttons close together, several actions on one button) are
		// pushed apart, so every number stays readable.
		struct Tag
		{
			double px, py; // the button
			double x, y;   // the badge
			int action;
			bool on;
		};
		std::vector<Tag> tags;
		for ( auto &[key, actions] : onButton )
		{
			std::sort( actions.begin(), actions.end() );
			const int hand = key[0] - '0';
			const Canvas::Pt pt = m_points[key.substr( 1 )];
			const double bx = ax[hand] + ( hand == 1 ? pt.x : m_artW - pt.x ) * m_artScale, by = artTop + pt.y * m_artScale;
			const double cx = ax[hand] + m_art[hand]->W() / 2.0, cy = artTop + m_art[hand]->H() * 0.3; // the face's middle
			double dx = bx - cx, dy = by - cy;
			const double len = std::hypot( dx, dy );
			dx = len > 1 ? dx / len : ( hand == 1 ? 1 : -1 ), dy = len > 1 ? dy / len : 0;
			for ( size_t i = 0; i < actions.size(); i++ )
				tags.push_back( { bx, by, bx + dx * 30 + ( i - ( actions.size() - 1 ) / 2.0 ) * 34, by + dy * 30, actions[i],
					handOn( actions[i], hand ) } );
		}
		const double minGap = 34; // badge diameter (32) plus a little air
		for ( int iter = 0; iter < 60; iter++ )
		{
			bool moved = false;
			for ( size_t i = 0; i < tags.size(); i++ )
				for ( size_t j = i + 1; j < tags.size(); j++ )
				{
					double dx = tags[j].x - tags[i].x, dy = tags[j].y - tags[i].y;
					const double d = std::hypot( dx, dy );
					if ( d >= minGap )
						continue;
					if ( d < 0.01 )
						dx = 0, dy = 1;
					else
						dx /= d, dy /= d;
					const double push = ( minGap - d ) / 2 + 0.5;
					tags[i].x -= dx * push, tags[i].y -= dy * push;
					tags[j].x += dx * push, tags[j].y += dy * push;
					moved = true;
				}
			for ( Tag &t : tags ) // stay inside the card
			{
				t.x = std::clamp( t.x, x + 24.0, x + w - 24.0 );
				t.y = std::clamp( t.y, artTop + 4.0, artTop + m_art[0]->H() - 4.0 );
			}
			if ( !moved )
				break;
		}
		for ( const Tag &t : tags )
		{
			const Color lc = t.on ? kAccentText : kOffEdge;
			c.Line( t.px, t.py, t.x, t.y, 2, lc );
			c.Circle( t.px, t.py, 4, lc );
		}
		for ( const Tag &t : tags )
			badge( t.x, t.y, t.action, t.on );
		listTop = artTop + std::max( m_art[0]->H(), m_art[1]->H() ) + 14;
	}
	else
		c.Text( x + 40, y + 88, S.noArt, 20, kMuted );

	// One row per action: badge and name, and under them what it is bound to.
	const int rowH = 60, right = x + w - 40, textX = x + 82;
	for ( int a = 0; a < 3; a++ )
	{
		const int ry = listTop + a * rowH;
		badge( x + 54, ry + 16, a, actionOn[a] );
		c.Text( textX, TextY( ry, 32, 22 ), names[a], 22, actionOn[a] ? kInk : kDim, Weight::Bold );
		// Same button and press on both hands -> "左右の…".
		std::vector<std::pair<std::string, int>> groups; // text, hand mask
		for ( const BindingInfo &b : m.bindings )
		{
			if ( b.action != a )
				continue;
			const std::string what = InputName( b.input, b.slot, ja ) + SlotName( b.slot, ja );
			auto g = std::find_if( groups.begin(), groups.end(), [&]( const auto &e ) { return e.first == what; } );
			const int mask = b.hand == 0 ? 1 : b.hand == 1 ? 2 : 4;
			if ( g == groups.end() )
				groups.push_back( { what, mask } );
			else
				g->second |= mask;
		}
		auto join = [&]( size_t count ) {
			std::string text;
			for ( size_t g = 0; g < count; g++ )
			{
				const auto &[what, mask] = groups[g];
				if ( !text.empty() )
					text += S.sep;
				const char *who = ( mask & 3 ) == 3 ? S.bothHands : mask & 1 ? S.leftHand : mask & 2 ? S.rightHand : "";
				text += who;
				if ( ja && *who && isascii( (unsigned char)what[0] ) )
					text += ' '; // 左手の View ボタン
				text += what;
			}
			if ( count < groups.size() )
				text += Fmt( S.more, (int)( groups.size() - count ) );
			return text;
		};
		// Fewer bindings with "ほか N 件" until it fits on its line; a single one that still does not fit is cut with "…".
		const int room = right - textX, px = 18;
		std::string text;
		for ( size_t count = groups.size();; count-- )
		{
			text = join( count );
			if ( Canvas::TextWidth( text, px ) <= room || count <= 1 )
				break;
		}
		if ( Canvas::TextWidth( text, px ) > room )
		{
			std::vector<uint32_t> cps = DecodeUtf8( text );
			auto encode = []( const std::vector<uint32_t> &v ) {
				std::string out;
				for ( uint32_t c : v )
				{
					if ( c < 0x80 )
						out += (char)c;
					else if ( c < 0x800 )
						out += (char)( 0xC0 | c >> 6 ), out += (char)( 0x80 | ( c & 63 ) );
					else
						out += (char)( 0xE0 | c >> 12 ), out += (char)( 0x80 | ( ( c >> 6 ) & 63 ) ), out += (char)( 0x80 | ( c & 63 ) );
				}
				return out;
			};
			while ( !cps.empty() && Canvas::TextWidth( encode( cps ) + "…", px ) > room )
				cps.pop_back();
			text = encode( cps ) + "…";
		}
		if ( !m.bindingsKnown )
			text = "—";
		else if ( groups.empty() )
			text = S.unbound;
		c.Text( textX, ry + 30, text, px, actionOn[a] ? kReadout : kDim );
	}
}
