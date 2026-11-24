#pragma once
// The dashboard tab, drawn on the CPU. SetOverlayRaw makes the compositor build a new texture on every call and on the
// Steam Frame the panel shows an empty frame meanwhile (it blinks on every change). Instead, the picture goes into two
// LINEAR RGBA dmabufs that SteamVR imports once (IVRIPCResourceManagerClient::ImportDmabuf), shown
// alternately with SetOverlayTexture, so a swap is always between two finished textures.
//
// The dmabufs come from /dev/udmabuf (plain memory turned into a dmabuf by the kernel), not GBM or Vulkan: those load
// the Mesa driver (+22-30 MB of memory), and a GPU client was behind a gamescope crash loop on the Frame. They exist only
// while the tab is open. If anything is missing (no udmabuf, runtime refuses LINEAR), SetOverlayRaw is used instead.

#include <openvr.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class PanelOverlay
{
public:
	bool Init( const char *key, const char *name, int w, int h, const std::vector<uint8_t> &iconRgba, int iconSize );
	void Shutdown();
	// A new picture (RGBA, w*h).
	void Show( const std::vector<uint8_t> &rgba );
	// Events of the dashboard overlay (mouse, shown / hidden). Returns false when there are no more.
	bool PollEvent( vr::VREvent_t &ev );
	bool Visible() const { return m_visible; }
	// SteamVR's keyboard for the panel; VREvent_KeyboardDone (with `userValue`) arrives through PollEvent.
	bool ShowKeyboard( const char *description, const char *text, uint64_t userValue );
	static std::string KeyboardText();

private:
	bool InitBuffers();
	void ReleaseBuffers();

	vr::VROverlayHandle_t m_main = vr::k_ulOverlayHandleInvalid, m_thumb = vr::k_ulOverlayHandleInvalid;
	int m_w = 0, m_h = 0;
	bool m_visible = false;

	// double buffer
	bool m_buffers = false, m_buffersFailed = false;
	int m_fd[2] = { -1, -1 };
	uint8_t *m_map[2] = {};
	size_t m_len = 0;
	vr::SharedTextureHandle_t m_shared[2] = {};
	int m_next = 0;
};
