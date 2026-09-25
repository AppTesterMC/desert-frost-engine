#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
repo_source_root="$repo_root/third_party/scummvm"
local_root="${DUNE_LOCAL_BUILD_ROOT:-/private/tmp/dune-scummvm-ios-build}"
. "$script_dir/scummvm_source.sh"
source_root="$local_root/scummvm"
ios_lib_root="$repo_root/third_party/scummvm-ios-libs"
framework_root="$ios_lib_root/frameworks"
artifact_root="$repo_root/notes/temp/dune_scummvm_engine_20260916"
project_root="$source_root/build-dune"
generator_derived="$source_root/build-tools/create_project-derived-data"
ios_objroot="$source_root/build-tools/ios-dune-obj"
ios_symroot="$source_root/build-tools/ios-dune-sym"
staging_root="$source_root/build-tools/ios-dune-ipa-staging"
module_cache="$local_root/module-cache"
module_session="$module_cache/Session.modulevalidation"
ipa_path="$artifact_root/packages/ScummVM-ios-dune-prototype.ipa"
source_marker="$repo_source_root/.dune-source-ready"

case "${DUNE_DEBUG:-0}" in
	0|1) ;;
	*) echo "DUNE_DEBUG must be 0 or 1" >&2; exit 2 ;;
esac
if [ "${DUNE_DEBUG:-0}" = 1 ]; then
	preprocessor_definitions='$(inherited) DUNE_DEBUG'
	debug_label="enabled"
else
	preprocessor_definitions='$(inherited)'
	debug_label="disabled"
fi

echo "Running desktop regression gate before IPA packaging..."
python3 "$repo_root/scripts/dune_regress.py" verify

mkdir -p "$artifact_root/commands" "$artifact_root/logs" "$artifact_root/results" "$artifact_root/packages"
mkdir -p "$module_cache"
touch "$module_session"

if [ ! -f "$source_marker" ]; then
	mkdir -p "$repo_source_root"
	stage_scummvm_source "$repo_source_root"
	touch "$source_marker"
fi

mkdir -p "$source_root"
if [ ! -f "$source_root/.dune-local-source-ready" ]; then
	stage_scummvm_source "$source_root"
	touch "$source_root/.dune-local-source-ready"
fi
mkdir -p "$source_root/engines/dune"
rsync -a "$repo_source_root/engines/dune/" "$source_root/engines/dune/"

# Our changes to ScummVM itself live as patches in scripts/patches/ so they
# stay visible and removable. Each is applied once to the staged tree.
for patch_file in "$repo_root"/scripts/patches/*.patch; do
	[ -f "$patch_file" ] || continue
	marker="$source_root/.applied-$(basename -- "$patch_file")"
	if [ ! -f "$marker" ]; then
		patch -d "$source_root" -p1 -N < "$patch_file"
		touch "$marker"
	fi
done

if [ ! -d "$framework_root" ]; then
	archive="$ios_lib_root/scummvm-ios7-libs-v4.zip"
	if [ ! -f "$archive" ]; then
		# ScummVM's prebuilt iOS dependency bundle, as used by its buildbot.
		mkdir -p "$ios_lib_root"
		curl -fL -o "$archive" \
			"${SCUMMVM_IOS_LIBS_URL:-https://downloads.scummvm.org/frs/build/scummvm-ios7-libs-v4.zip}"
	fi
	unzip -q -o "$archive" -d "$ios_lib_root"
fi

xcodebuild \
	-project "$source_root/devtools/create_project/xcode/create_project.xcodeproj" \
	-scheme create_project \
	-configuration Release \
	-sdk macosx \
	-derivedDataPath "$generator_derived" \
	CODE_SIGNING_ALLOWED=NO \
	> "$artifact_root/logs/create-project-build.log" 2>&1

generator="$generator_derived/Build/Products/Release/create_project"
[ -x "$generator" ] || {
	echo "create_project was not built: $generator" >&2
	exit 1
}

rm -rf "$project_root"
mkdir -p "$project_root"
"$generator" "$source_root" \
	--xcode \
	--ios \
	--use-xcframework \
	--disable-all-engines \
	--enable-engine=dune \
	--enable-faad \
	--enable-gif \
	--enable-mikmod \
	--enable-vpx \
	--enable-mpc \
	--enable-a52 \
	--disable-taskbar \
	--output-dir "$project_root" \
	> "$artifact_root/logs/create-project.log" 2>&1

mkdir -p "$project_root/frameworks"
for framework in "$framework_root"/*.xcframework; do
	name=$(basename -- "$framework")
	ln -sfn "$framework" "$project_root/frameworks/$name"
done

cp "$project_root/scummvm.xcodeproj/project.pbxproj" "$artifact_root/commands/scummvm-dune-project.pbxproj"

fallback_project="$artifact_root/commands/scummvm-dune-no-ui-assets.xcodeproj"
mkdir -p "$fallback_project"
cp "$project_root/scummvm.xcodeproj/project.pbxproj" "$fallback_project/project.pbxproj"
mkdir -p "$fallback_project/xcshareddata/xcschemes"
cp "$repo_root/scripts/ScummVM-iOS.xcscheme" \
	"$fallback_project/xcshareddata/xcschemes/ScummVM-iOS.xcscheme"

# Xcode 26.6 on this host cannot compile the iOS storyboard and asset catalog
# because the iOS 26.5 platform is unavailable to ibtool/actool. Remove only
# those iOS build-phase entries; the tvOS entries remain untouched.
fallback_pbx="$fallback_project/project.pbxproj"
perl -pi -e 's/^.*LaunchScreen_ios\.storyboard in Resources.*\n//; s/^.*LaunchScreen_ios\.storyboard in Sources.*\n//; s/^.*Image Asset Catalog.*\n//' "$fallback_pbx"

info_plist="$artifact_root/commands/Info-no-launch-storyboard.plist"
if [ ! -f "$info_plist" ]; then
	cp "$source_root/dists/ios7/Info.plist" "$info_plist"
	perl -0pi -e 's/\n\t<key>UILaunchStoryboardName<\/key>\n\t<string>[^<]*<\/string>//' "$info_plist"
fi

rm -rf "$ios_objroot" "$ios_symroot"
xcodebuild \
	-project "$fallback_project" \
	-target ScummVM-iOS \
	-configuration Release \
	-sdk "${IOS_SDK:-iphoneos26.5}" \
	CODE_SIGNING_ALLOWED=NO \
	IPHONEOS_DEPLOYMENT_TARGET=12.0 \
	ONLY_ACTIVE_ARCH=YES \
	INFOPLIST_FILE="$info_plist" \
	CLANG_MODULE_CACHE_PATH="$module_cache" \
	CLANG_MODULES_BUILD_SESSION_FILE="$module_session" \
	CLANG_ENABLE_MODULES=NO \
	CLANG_ENABLE_EXPLICIT_MODULES=NO \
	SWIFT_ENABLE_EXPLICIT_MODULES=NO \
	GCC_PREPROCESSOR_DEFINITIONS="$preprocessor_definitions" \
	LIBRARY_SEARCH_PATHS="$ios_lib_root/frameworks/bz2.xcframework/ios-arm64-armv7-armv7s" \
	OTHER_LDFLAGS="-lbz2" \
	OBJROOT="$ios_objroot" \
	SYMROOT="$ios_symroot" \
	> "$artifact_root/logs/ios-dune-build.log" 2>&1

app="$ios_symroot/Release-iphoneos/scummvm.app"
[ -x "$app/scummvm" ] || {
	echo "iOS app was not built: $app" >&2
	exit 1
}

rm -rf "$staging_root"
mkdir -p "$staging_root/Payload"
cp -R "$app" "$staging_root/Payload/ScummVM.app"
codesign --force --deep --sign - --timestamp=none "$staging_root/Payload/ScummVM.app"
codesign --verify --deep --strict "$staging_root/Payload/ScummVM.app"
ditto -c -k --sequesterRsrc --keepParent "$staging_root/Payload" "$ipa_path"
(cd "$(dirname -- "$ipa_path")" && shasum -a 256 "$(basename -- "$ipa_path")" > "$(basename -- "$ipa_path").sha256")

echo "Built: $ipa_path"
echo "DUNE_DEBUG: $debug_label"
echo "Checksum: $(awk '{print $1}' "$ipa_path.sha256")"
