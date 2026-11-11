# JSON UI reference checks

Reference: the local symbol-rich `china-1.16.201.i64` database. IDA reports its
original input as `Windows Client 1.19`; addresses below belong to that client,
not to the current server or the installed modern client.

`TextComponent::getFontScale`, `0x140c03eb0`, multiplies
`mFontScaleFactor` by 0.5, 1, 2, or 4 for small, normal, large, or extra-large.
Both intrinsic measurement and RML emission use these factors. The
`ui_font_*` fixtures check the resulting label dimensions.

A bound `#alpha` supplies the alpha property. Multiplying it into an already
resolved `alpha: "#alpha"` applied the value twice: a 0.3 background became
0.09. `ui_bound_alpha` checks the emitted opacity.

Level exit schedules resource disposal for the next frame boundary. It joins
UI builders and mesh workers, clears HUD/form/chest/screen trees and animation
state, releases RML textures and level atlas/sky resources, and invalidates
pending form-image downloads. The GPU regression checks that old geometry is
gone and a new atlas and mesh render after repeated unloads.

These are bounded checks. Full UI parity, binding scheduling and every server
screen are not established by these fixtures.

Repeated names in a `controls` array identify separate sibling instances, not
updates to one sibling. Collapsing them dropped conditional form routes and
exposed the fallback layout, including protocol titles and placeholder rows.
`ui_repeated_routes` checks a selected route between two inactive siblings
with the same name, including its collection-driven factory.

MinecraftTen baseline: RmlUi's `FontEngineInterface::GenerateString` position
is already the glyph baseline. The bitmap bearings from stb_truetype are
relative to that same baseline. Adding ascent a second time moved the ink
downward. Rendering now applies the bearings directly, with ascent/descent
from the installed font's `hhea` table and x-height from its `x` glyph. There
are no screen-specific adjustments. The metrics regression reads `head` and
`hhea` independently across several scales; offline captured menus check the
rendered placement.

Native bitmap glyph sizing was checked in the symbol-bearing client database:
`BitmapFont::_scanUnicodeCharacterSize` (`0x140c37db0`) and
`BitmapFont::_buildChar` (`0x140c38180`). Pages E0 through F8 retain the
source cell width as their GUI quad width and height, unlike ordinary Unicode
pages, which normalize to 8 GUI pixels. Advance is the scanned ink span plus
one GUI pixel; left offset is `(1 - firstInkColumn) * unit`, with unit 1 for
private-use icons or `8 / cellWidth` otherwise. Private-use quads are vertically
centered around the normal 8-pixel font and preserve the sheet colors. The
previous full-cell 8-pixel sizing and fixed advance were incorrect. These rules
now apply to UI, name tags and signs. Color sequences remain formatting; they
are not converted into invented private-use characters.

Fractional regular text now retains continuous pen positions inside the run.
The native `Font::drawTransformed` path scales the entire cached run, rather
than rounding every character origin separately. At a 0.8 font scale and GUI
scale 2, per-character snapping changes the sampling phase of adjacent digits.
The before/after offline price probe uses the same glyph, requested size and
advance widths; colored icon placement remains unchanged.
