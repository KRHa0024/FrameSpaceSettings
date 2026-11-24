#!/usr/bin/env bash
# Frame Space Settings installer: run on the Steam Frame while SteamVR is running.
#   curl -fsSL https://github.com/KRHa0024/FrameSpaceSettings/releases/latest/download/install.sh | bash
# Downloads the latest release, checks its SHA-256 and runs its --install (program, SteamVR registration, driver).
# Everything goes into your home directory; no root access is needed. Running it again updates to the latest release.
set -euo pipefail

# Everything is in a function that runs on the last line: with `curl | bash`, a download cut short runs nothing.
main() {
	REPO="KRHa0024/FrameSpaceSettings"
	ASSET="frame-space-settings-linux-arm64.tar.gz"
	BASE="${FSS_RELEASE_BASE:-https://github.com/${REPO}/releases/latest/download}" # override only for testing

	say() { printf '%s\n' "$*"; }
	fail() { printf 'Error / エラー: %s\n' "$*" >&2; exit 1; }

	[ "$(uname -m)" = "aarch64" ] || fail "this is for the Steam Frame (ARM64) / Steam Frame（ARM64）専用です"
	for cmd in curl sha256sum tar; do command -v "$cmd" >/dev/null || fail "$cmd is missing / $cmd が見つかりません"; done
	pgrep -x vrserver >/dev/null || fail "start SteamVR first / SteamVR を起動してから実行してください"

	tmp="$(mktemp -d)"
	trap 'rm -rf "$tmp"' EXIT

	say "Downloading the latest release / 最新版をダウンロードしています..."
	curl -fsSL -o "$tmp/$ASSET" "$BASE/$ASSET"
	curl -fsSL -o "$tmp/SHA256SUMS" "$BASE/SHA256SUMS"
	( cd "$tmp" && grep -q " ${ASSET}\$" SHA256SUMS && sha256sum -c --ignore-missing --quiet SHA256SUMS ) ||
		fail "checksum mismatch / チェックサムが一致しません"

	tar -xzf "$tmp/$ASSET" -C "$tmp"
	"$tmp/frame-space-settings/frame-space-settings" --install </dev/null

	version="$("$HOME/.local/bin/frame-space-settings" --version)"
	say ""
	say "Frame Space Settings ${version} is installed. Restart SteamVR once to load the driver."
	say "Frame Space Settings ${version} をインストールしました。ドライバを読み込むため、SteamVR を一度再起動してください。"
}

main "$@"
