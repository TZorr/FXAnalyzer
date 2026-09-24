#!/bin/bash
#
#  install.sh - put the built plugins where the hosts look, and make sure they
#  actually pick up the new build.
#
#  The cache flush is the part that matters. macOS keeps a registry of audio
#  units, and Logic keeps a scan cache on top of that; replacing the bundle on
#  disk does not by itself make either of them let go of the old one. Skip this
#  and the next hour is spent testing a binary from before the fix - a failure
#  mode that looks exactly like the fix not working.
#
#  One migration step matters and will matter only once: an AUv3 was shipped
#  briefly, and its registration claims the same aufx/Fxan/Tzor the AU does. A
#  registered extension wins that claim, so the AU would never appear while the
#  old container app is still in /Applications. This script deregisters and
#  removes it.
#
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARTEFACTS="$PROJECT_DIR/build/FXAnalyzer_artefacts/Release"

AU_SOURCE="$ARTEFACTS/AU/FX Analyzer.component"
VST3_SOURCE="$ARTEFACTS/VST3/FX Analyzer.vst3"
APP_SOURCE="$ARTEFACTS/Standalone/FX Analyzer.app"

AU_DEST="$HOME/Library/Audio/Plug-Ins/Components"
VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3"
APP_DEST="/Applications"

if [[ ! -d "$VST3_SOURCE" ]]; then
    echo "No build found at $ARTEFACTS - run Scripts/build.sh first." >&2
    exit 1
fi

# A .component is only an Audio Unit if it is a well-formed bundle, and the one
# thing that makes it well-formed is the Info.plist carrying the AudioComponents
# entry. Without it macOS does not register the plugin, does not complain, and
# does not leave a trace anywhere a person would look - it simply never appears
# in the host.
#
# This check exists because that happened. Deleting the AU artefact and
# rebuilding only the AU target relinks the binary without re-copying the
# generated plist, so the bundle comes out with an executable and nothing else -
# and this script cheerfully installed it over a working copy. Worse, auval
# still passed, because the AUv3 shares the same aufx/Fxan/Tzor codes and
# answered in its place.
check_bundle() {
    local bundle="$1" name="$2"

    if [[ ! -f "$bundle/Contents/Info.plist" ]]; then
        echo "$name at $bundle has no Contents/Info.plist." >&2
        echo "The bundle is incomplete - most likely something deleted the artefact and" >&2
        echo "only the link step ran. Rebuild from scratch:" >&2
        echo "    rm -rf build/FXAnalyzer_artefacts && Scripts/build.sh" >&2
        exit 1
    fi
}

check_bundle "$AU_SOURCE"   "The AU"
check_bundle "$VST3_SOURCE" "The VST3"

if ! /usr/libexec/PlistBuddy -c "Print :AudioComponents:0:subtype" \
        "$AU_SOURCE/Contents/Info.plist" >/dev/null 2>&1; then
    echo "The AU's Info.plist has no AudioComponents entry - macOS will not register it." >&2
    exit 1
fi

# Refuse to install a build older than the sources it was built from.
#
# This exists because it happened, and it cost a round trip to find. The AUv3
# comes out of the Xcode tree, and build.sh --no-auv3 skips that tree entirely -
# so a run of build.sh --no-auv3 followed by install.sh installs a current VST3
# next to an AUv3 from an hour earlier. Nothing complains. The plugin loads, the
# panel opens, and the feature that was just added is simply not there, which
# looks exactly like the feature not working.
# Source/ and the CMakeLists only. Verification/ is deliberately not here: the
# harness sources do not go into the plugin, and including them would make this
# guard cry wolf every time a test was edited - which is the fastest way to
# teach somebody to ignore it.
newest_source() {
    find "$PROJECT_DIR/Source" "$PROJECT_DIR/CMakeLists.txt" \
        -type f -newer "$1" -print -quit 2>/dev/null
}

check_fresh() {
    local binary="$1" name="$2" advice="$3"

    [[ -f "$binary" ]] || return 0

    local stale
    stale="$(newest_source "$binary")"

    if [[ -n "$stale" ]]; then
        echo "$name is older than the sources it was built from." >&2
        echo "  newer: ${stale#"$PROJECT_DIR"/}" >&2
        echo "Rebuild it first: $advice" >&2
        exit 1
    fi
}

check_fresh "$VST3_SOURCE/Contents/MacOS/FX Analyzer" \
            "The VST3" "Scripts/build.sh"

check_fresh "$AU_SOURCE/Contents/MacOS/FX Analyzer" "The AU" "Scripts/build.sh"

if pgrep -xq "Logic Pro"; then
    echo "Logic Pro is running. Quit it first: it holds the old plugin open and" >&2
    echo "will not rescan while it is." >&2
    exit 1
fi

mkdir -p "$AU_DEST" "$VST3_DEST"

# ---------------------------------------------------------------------------
# Migration: get rid of the AUv3 that was briefly shipped.
#
# It claims the same aufx/Fxan/Tzor the AU does, and a registered extension wins
# that claim - so while the old container app is still in /Applications, the AU
# installed below would simply never appear in a host. Deregistering before
# deleting matters too: pluginkit's record outlives the bundle, and a record
# pointing at a bundle that is gone answers auval with "Cannot open component".
# ---------------------------------------------------------------------------
OLD_APPEX="$APP_DEST/FX Analyzer.app/Contents/PlugIns/FX Analyzer.appex"

if [[ -d "$OLD_APPEX" ]]; then
    echo "Removing the old AUv3 - this project ships the AU again (see CMakeLists.txt)."
    pluginkit -r "$OLD_APPEX" 2>/dev/null || true
    rm -rf "$APP_DEST/FX Analyzer.app"
fi

rm -rf "$AU_DEST/FX Analyzer.component" "$VST3_DEST/FX Analyzer.vst3"
cp -R "$AU_SOURCE"   "$AU_DEST/"
cp -R "$VST3_SOURCE" "$VST3_DEST/"

echo "Installed AU and VST3 to $AU_DEST and $VST3_DEST"

# The standalone is a development tool, not a deliverable, but it is the only
# way to look at the panel without a host, so it goes to /Applications too.
if [[ -d "$APP_SOURCE" ]]; then
    rm -rf "$APP_DEST/FX Analyzer.app"
    cp -R "$APP_SOURCE" "$APP_DEST/"
    echo "Installed the standalone app to $APP_DEST"
fi

# Make the system forget the previous registration - both the AU's and any
# lingering extension record.
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 1

echo
echo "Validating the Audio Unit:"
auval -v aufx Fxan Tzor

echo
echo "What a host will find:"
auval -a 2>/dev/null | grep -i "Fxan" || echo "  (nothing - the AU did not register)"

# The null test, against the component that was just installed rather than
# against our own objects. auval proves the unit renders; this proves it renders
# the input back unchanged, which is the one promise this plugin makes.
NULL_TEST="$PROJECT_DIR/build/HostNullTest_artefacts/Release/HostNullTest"

if [[ -x "$NULL_TEST" ]]; then
    "$NULL_TEST"
else
    echo
    echo "  (HostNullTest not built - run Scripts/build.sh to include it)"
fi
