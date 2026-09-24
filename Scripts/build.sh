#!/bin/bash
#
#  build.sh - configure and build FX Analyzer in Release, then verify it.
#
#  It exists so the exact configuration used to produce a build is written down
#  rather than living in somebody's shell history - and so that "it builds" and
#  "it measures correctly" are the same command. A build that compiles and reads
#  3 dB high is not a build.
#
#  One build tree. There were two - the second existed only because JUCE offers
#  the AUv3 format under the Xcode generator alone - and it went with the AUv3.
#  See CMakeLists.txt for why the AUv3 went.
#
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

cd "$PROJECT_DIR"

cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"

echo
echo "Measuring the analysers before anything is believed:"
echo
"$BUILD_DIR/AnalyzerCheck_artefacts/Release/AnalyzerCheck"

echo
echo "Checking that the Resolution setting is honoured, or says why not:"
"$BUILD_DIR/EditorShot_artefacts/Release/EditorShot" "$BUILD_DIR/shots" --resolution

echo
echo "Checking that a saved session comes back, and that an open panel shows it:"
"$BUILD_DIR/EditorShot_artefacts/Release/EditorShot" "$BUILD_DIR/shots" --state

echo
echo "Checking that every pixel of the tab strip reaches the tab drawn on it:"
"$BUILD_DIR/EditorShot_artefacts/Release/EditorShot" "$BUILD_DIR/shots" --hitmap

echo
echo "Surviving a host re-preparing and reopening the editor under load:"
"$BUILD_DIR/EditorShot_artefacts/Release/EditorShot" "$BUILD_DIR/shots" --lifecycle

echo
echo "Rendering the pages so the layout can be checked without a DAW:"
"$BUILD_DIR/EditorShot_artefacts/Release/EditorShot" "$BUILD_DIR/shots" --demo

echo
echo "Built:"
ls -d "$BUILD_DIR/FXAnalyzer_artefacts/Release/"*/*.{vst3,component,app} 2>/dev/null || true
