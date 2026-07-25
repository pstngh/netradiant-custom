#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
install_dir="${INSTALL_DIR:-$repo_root/install}"
target_dir="${TARGET_DIR:-$script_dir/target}"
app="$target_dir/NetRadiant-Custom.app"
contents="$app/Contents"
macos_dir="$contents/MacOS"
frameworks_dir="$contents/Frameworks"
resources_dir="$contents/Resources"
arch="$(uname -m)"
archive="$target_dir/NetRadiant-Custom-macos-$arch.zip"
qt_prefix="${QT_PREFIX:-$(brew --prefix qt@5)}"

main_binary="$install_dir/radiant.$arch"
if [[ ! -f "$main_binary" ]]; then
	echo "Missing $main_binary; build NetRadiant Custom before packaging." >&2
	exit 1
fi

if [[ ! -x "$qt_prefix/bin/macdeployqt" ]]; then
	echo "Required packaging tool '$qt_prefix/bin/macdeployqt' was not found." >&2
	exit 1
fi

for tool in dylibbundler otool lipo codesign ditto plutil file; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "Required packaging tool '$tool' was not found." >&2
		exit 1
	fi
done

rm -rf "$app"
rm -f "$archive"
mkdir -p "$macos_dir" "$resources_dir"

ditto "$install_dir" "$macos_dir"
rm -f "$macos_dir/radiant"
mv "$macos_dir/radiant.$arch" "$macos_dir/radiant"
cp "$script_dir/NetRadiant.app/Contents/Info.plist" "$contents/Info.plist"
cp "$script_dir/NetRadiant.app/Contents/Resources/radiant.icns" "$resources_dir/radiant.icns"
chmod +x "$macos_dir/radiant"

is_macho() {
	file -b "$1" | grep -q 'Mach-O'
}

qt_args=( "$app" -verbose=1 )
while IFS= read -r -d '' candidate; do
	if [[ "$candidate" != "$macos_dir/radiant" ]] && is_macho "$candidate"; then
		qt_args+=( "-executable=$candidate" )
	fi
done < <(find "$macos_dir" -type f -print0)
"$qt_prefix/bin/macdeployqt" "${qt_args[@]}"

# macdeployqt deploys and rewrites the Qt frameworks and plugins. Seed
# dylibbundler with NetRadiant's executables and plug-ins; it recursively walks
# their remaining non-system dependencies without reprocessing deployed Qt.
dylib_args=(
	-b
	-ns
	-cd
	-of
	-i /System/Library
	-i @executable_path
	-i @loader_path
	-i @rpath
	-d "$frameworks_dir"
	-p @executable_path/../Frameworks
)
while IFS= read -r -d '' candidate; do
	if is_macho "$candidate"; then
		dylib_args+=( -x "$candidate" )
	fi
done < <(find "$macos_dir" -type f -print0)
dylibbundler "${dylib_args[@]}"

plutil -lint "$contents/Info.plist"

dependency_error=0
architecture_error=0
while IFS= read -r -d '' candidate; do
	if ! is_macho "$candidate"; then
		continue
	fi

	# otool -L includes a dylib's LC_ID_DYLIB as its first entry. Qt plug-ins
	# retain their build-time ID, but that ID is not a load dependency.
	install_id="$(otool -D "$candidate" 2>/dev/null | sed -n '2p' || true)"
	homebrew_dependencies="$(
		otool -L "$candidate" |
			sed -n '2,$p' |
			awk '{ print $1 }' |
			grep -E '^/(opt/homebrew|usr/local)/' |
			grep -Fvx "$install_id" || true
	)"
	if [[ -n "$homebrew_dependencies" ]]; then
		echo "Unbundled Homebrew dependency in $candidate:" >&2
		echo "$homebrew_dependencies" >&2
		dependency_error=1
	fi

	if ! lipo -archs "$candidate" | tr ' ' '\n' | grep -Fx "$arch" >/dev/null; then
		echo "$candidate does not contain the expected $arch architecture." >&2
		architecture_error=1
	fi
done < <(find "$app" -type f -print0)

if (( dependency_error || architecture_error )); then
	exit 1
fi

# Files with an executable bit under Contents/MacOS are treated as nested code.
# The install tree contains executable-marked SVG/XML assets, so normalize all
# non-Mach-O resources before creating the application signature.
while IFS= read -r -d '' candidate; do
	if ! is_macho "$candidate"; then
		chmod a-x "$candidate"
	fi
done < <(find "$app" -type f -print0)

# Sign nested code explicitly instead of using --deep. NetRadiant gamepack
# directories end in ".game", which codesign --deep mistakes for Apple bundles.
while IFS= read -r -d '' candidate; do
	if is_macho "$candidate"; then
		codesign --force --sign - "$candidate"
	fi
done < <(find "$app" -type f -print0)

while IFS= read -r -d '' framework; do
	codesign --force --sign - "$framework"
done < <(find "$frameworks_dir" -type d -name '*.framework' -prune -print0)

codesign --force --sign - "$app"

while IFS= read -r -d '' candidate; do
	if is_macho "$candidate"; then
		codesign --verify --strict --verbose=2 "$candidate"
	fi
done < <(find "$app" -type f -print0)

while IFS= read -r -d '' framework; do
	codesign --verify --strict --verbose=2 "$framework"
done < <(find "$frameworks_dir" -type d -name '*.framework' -prune -print0)

codesign --verify --strict --verbose=2 "$app"

ditto -c -k --sequesterRsrc --keepParent "$app" "$archive"
echo "Created $archive"
