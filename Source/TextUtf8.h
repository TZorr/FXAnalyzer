//
//  TextUtf8.h
//  FX Analyzer
//
//  One helper, for one trap.
//
//  juce::String's constructor from a plain `const char*` runs the bytes through
//  CharPointer_ASCII, which maps each byte to one character. Source files are
//  UTF-8, so every character outside ASCII is two or three bytes, and handing
//  those bytes to that constructor renders each one separately: the arrow in
//  "44.1 -> 48 kHz" comes out as "\u00e2\u0086\u2019", and the separator dot as
//  "\u00c2\u00b7". It compiles, it runs, and it is wrong in a way that only shows
//  up once somebody looks at the panel - which is how it was found here, in the
//  first render EditorShot produced.
//
//  The interface is English, so most literals are plain ASCII and need nothing.
//  The typographic characters still do: the arrow, the middle dot, and the em
//  dash. Rather than remember which literals are safe, every user-facing string
//  containing one goes through this.
//

#pragma once

#include <juce_core/juce_core.h>

/** A UTF-8 source literal, decoded as UTF-8 rather than as bytes. */
inline juce::String utf8 (const char* literal)
{
    return juce::String (juce::CharPointer_UTF8 (literal));
}
