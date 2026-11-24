#!/usr/bin/env bash
# Builds a release on the Steam Frame and publishes it on GitHub (needs gh, logged in).
#   scripts/make-release.sh            build and package into dist/ only
#   scripts/make-release.sh --publish  also create the GitHub release v<version> with the files from dist/
# The version comes from project(VERSION) in CMakeLists.txt. Building on the Frame itself keeps the binaries in step
# with SteamOS's libraries. The release holds the tarball (program, driver, data), SHA256SUMS and install.sh; the app's
# updater and install.sh both use these exact file names.
set -euo pipefail

cd "$(dirname "$0")/.."
version="$(sed -n 's/^project(frame-space-settings VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ -n "$version" ] || { echo "no version in CMakeLists.txt" >&2; exit 1; }
tag="v$version"
asset="frame-space-settings-linux-arm64.tar.gz"

if [ "${1:-}" = "--publish" ]; then
	[ -z "$(git status --porcelain)" ] || { echo "commit your changes first" >&2; exit 1; }
	git fetch -q origin
	[ "$(git rev-parse HEAD)" = "$(git rev-parse '@{u}')" ] || { echo "push your commits first" >&2; exit 1; }
	! gh release view "$tag" >/dev/null 2>&1 || { echo "$tag already exists; raise the version first" >&2; exit 1; }
fi

build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=MinSizeRel >/dev/null
cmake --build "$build" -j"$(nproc)"
"$build/fss-tests"

stage="$build/stage/frame-space-settings"
mkdir -p "$stage"
cp "$build/frame-space-settings" "$build/driver_framespacesettings.so" "$stage/"
cp -r "$build/data" "$stage/"
cp LICENSE README.md README.en.md "$stage/"

rm -rf dist && mkdir dist
tar --owner=0 --group=0 --numeric-owner -czf "dist/$asset" -C "$build/stage" frame-space-settings
cp install.sh dist/
( cd dist && sha256sum "$asset" install.sh > SHA256SUMS )
echo "dist/ for $tag:"
ls -l dist

if [ "${1:-}" = "--publish" ]; then
	# --target: tag the commit that was just built and checked, not whatever the default branch points at
	gh release create "$tag" dist/* --target "$(git rev-parse HEAD)" --title "Frame Space Settings $tag" --generate-notes
fi
