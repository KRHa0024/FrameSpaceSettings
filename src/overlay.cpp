#include "overlay.h"

#include "core/log.h"

#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/udmabuf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

using namespace vr;

namespace
{
constexpr float kWidthMeters = 2.2f;
constexpr uint32_t kAB24 = 0x34324241; // DRM_FORMAT_ABGR8888: bytes R, G, B, A in memory = our RGBA
constexpr uint64_t kLinear = 0;        // DRM_FORMAT_MOD_LINEAR

// A dmabuf of `len` bytes of ordinary memory (memfd + /dev/udmabuf). -1 on failure.
int MakeDmabuf( size_t len )
{
	const int dev = open( "/dev/udmabuf", O_RDWR | O_CLOEXEC );
	if ( dev < 0 )
		return -1;
	const int mem = memfd_create( "frame-space-settings-panel", MFD_CLOEXEC | MFD_ALLOW_SEALING );
	int buf = -1;
	if ( mem >= 0 && ftruncate( mem, (off_t)len ) == 0 && fcntl( mem, F_ADD_SEALS, F_SEAL_SHRINK ) == 0 )
	{
		udmabuf_create c = {};
		c.memfd = (uint32_t)mem;
		c.flags = UDMABUF_FLAGS_CLOEXEC;
		c.offset = 0;
		c.size = len;
		buf = ioctl( dev, UDMABUF_CREATE, &c );
	}
	if ( mem >= 0 )
		close( mem ); // the dmabuf keeps the memory
	close( dev );
	return buf;
}
} // namespace

bool PanelOverlay::Init( const char *key, const char *name, int w, int h, const std::vector<uint8_t> &icon, int iconSize )
{
	IVROverlay *ov = VROverlay();
	if ( !ov )
		return false;
	m_w = w;
	m_h = h;
	if ( EVROverlayError e = ov->CreateDashboardOverlay( key, name, &m_main, &m_thumb ); e != VROverlayError_None )
	{
		logging::Printf( "CreateDashboardOverlay failed: %s", ov->GetOverlayErrorNameFromEnum( e ) );
		return false;
	}
	ov->SetOverlayWidthInMeters( m_main, kWidthMeters * w / 1280.0f ); // 2.2 m per 1280 px: text keeps its size at any panel width
	ov->SetOverlayInputMethod( m_main, VROverlayInputMethod_Mouse );
	ov->SetOverlayFlag( m_main, VROverlayFlags_EnableControlBarClose, true ); // the dashboard shows a close button; VREvent_OverlayClosed
	HmdVector2_t scale = { { (float)w, (float)h } };
	ov->SetOverlayMouseScale( m_main, &scale );
	if ( !icon.empty() )
		ov->SetOverlayRaw( m_thumb, const_cast<uint8_t *>( icon.data() ), (uint32_t)iconSize, (uint32_t)iconSize, 4 );
	return true;
}

void PanelOverlay::Shutdown()
{
	IVROverlay *ov = VROverlay();
	if ( !ov || m_main == k_ulOverlayHandleInvalid )
		return;
	ov->HideOverlay( m_main );
	ReleaseBuffers();
	ov->DestroyOverlay( m_main );
	m_main = m_thumb = k_ulOverlayHandleInvalid;
}

bool PanelOverlay::InitBuffers()
{
	auto fail = [this]( const char *why ) {
		logging::Printf( "panel: double-buffered upload unavailable (%s); using SetOverlayRaw, which blinks on updates", why );
		ReleaseBuffers();
		m_buffersFailed = true;
		return false;
	};
	IVRIPCResourceManagerClient *rm = VRIPCResourceManager();
	if ( !rm )
		return fail( "no IPC resource manager" );
	uint32_t n = 0;
	if ( !rm->GetDmabufModifiers( VRApplication_Overlay, kAB24, &n, nullptr ) || n == 0 )
		return fail( "SteamVR lists no RGBA dmabuf modifiers" );
	std::vector<uint64_t> mods( n );
	rm->GetDmabufModifiers( VRApplication_Overlay, kAB24, &n, mods.data() );
	if ( std::find( mods.begin(), mods.begin() + n, kLinear ) == mods.begin() + n )
		return fail( "SteamVR does not take LINEAR RGBA dmabufs" );
	const uint32_t stride = (uint32_t)m_w * 4;
	const long page = sysconf( _SC_PAGESIZE );
	m_len = ( ( (size_t)stride * m_h + page - 1 ) / page ) * page;
	for ( int i = 0; i < 2; i++ )
	{
		m_fd[i] = MakeDmabuf( m_len );
		if ( m_fd[i] < 0 )
			return fail( "cannot create a dmabuf with /dev/udmabuf" );
		void *p = mmap( nullptr, m_len, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd[i], 0 );
		if ( p == MAP_FAILED )
			return fail( "cannot map the dmabuf" );
		m_map[i] = (uint8_t *)p;
		DmabufAttributes_t a = {};
		a.unWidth = (uint32_t)m_w;
		a.unHeight = (uint32_t)m_h;
		a.unDepth = 1;
		a.unMipLevels = 1;
		a.unArrayLayers = 1;
		a.unSampleCount = 1;
		a.unFormat = kAB24;
		a.ulModifier = kLinear;
		a.unPlaneCount = 1;
		a.plane[0].unOffset = 0;
		a.plane[0].unStride = stride;
		a.plane[0].nFd = m_fd[i]; // duplicated by SteamVR
		if ( !rm->ImportDmabuf( VRApplication_Overlay, &a, &m_shared[i] ) || !m_shared[i] )
			return fail( "SteamVR refused to import the dmabuf" );
	}
	m_buffers = true;
	m_next = 0;
	return true;
}

void PanelOverlay::ReleaseBuffers()
{
	if ( m_main != k_ulOverlayHandleInvalid && ( m_shared[0] || m_shared[1] ) )
		VROverlay()->ClearOverlayTexture( m_main ); // nothing of ours referenced before the handles go
	IVRIPCResourceManagerClient *rm = VRIPCResourceManager();
	for ( int i = 0; i < 2; i++ )
	{
		if ( m_shared[i] && rm )
			rm->UnrefResource( m_shared[i] );
		m_shared[i] = 0;
		if ( m_map[i] )
			munmap( m_map[i], m_len );
		m_map[i] = nullptr;
		if ( m_fd[i] >= 0 )
			close( m_fd[i] );
		m_fd[i] = -1;
	}
	m_buffers = false;
}

void PanelOverlay::Show( const std::vector<uint8_t> &rgba )
{
	if ( rgba.size() != (size_t)m_w * m_h * 4 || m_main == k_ulOverlayHandleInvalid )
		return;
	IVROverlay *ov = VROverlay();
	if ( !m_buffers && !m_buffersFailed )
		InitBuffers();
	if ( m_buffers )
	{
		const int i = m_next;
		dma_buf_sync s = { DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE };
		ioctl( m_fd[i], DMA_BUF_IOCTL_SYNC, &s );
		memcpy( m_map[i], rgba.data(), rgba.size() );
		s.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
		ioctl( m_fd[i], DMA_BUF_IOCTL_SYNC, &s );
		Texture_t tex = { reinterpret_cast<void *>( &m_shared[i] ), TextureType_SharedTextureHandle, ColorSpace_Gamma };
		if ( const EVROverlayError e = ov->SetOverlayTexture( m_main, &tex ); e == VROverlayError_None )
		{
			m_next ^= 1;
			return;
		}
		else
		{
			logging::Printf( "panel: SetOverlayTexture(dmabuf) failed (%s); using SetOverlayRaw", ov->GetOverlayErrorNameFromEnum( e ) );
			ReleaseBuffers();
			m_buffersFailed = true;
		}
	}
	ov->SetOverlayRaw( m_main, const_cast<uint8_t *>( rgba.data() ), (uint32_t)m_w, (uint32_t)m_h, 4 );
}

bool PanelOverlay::PollEvent( VREvent_t &ev )
{
	IVROverlay *ov = VROverlay();
	if ( m_thumb != k_ulOverlayHandleInvalid && ov->PollNextOverlayEvent( m_thumb, &ev, sizeof ev ) )
		return true;
	if ( m_main == k_ulOverlayHandleInvalid || !ov->PollNextOverlayEvent( m_main, &ev, sizeof ev ) )
		return false;
	if ( ev.eventType == VREvent_OverlayShown )
		m_visible = true;
	else if ( ev.eventType == VREvent_OverlayHidden )
	{
		m_visible = false;
		ReleaseBuffers(); // 2 x ~7 MB back while the tab is closed
	}
	return true;
}

bool PanelOverlay::ShowKeyboard( const char *description, const char *text, uint64_t userValue )
{
	if ( m_main == k_ulOverlayHandleInvalid )
		return false;
	return VROverlay()->ShowKeyboardForOverlay( m_main, k_EGamepadTextInputModeNormal, k_EGamepadTextInputLineModeSingleLine,
			   KeyboardFlag_Modal, description, 8, text, userValue ) == VROverlayError_None;
}

std::string PanelOverlay::KeyboardText()
{
	char buf[32] = {};
	VROverlay()->GetKeyboardText( buf, sizeof buf );
	return buf;
}
