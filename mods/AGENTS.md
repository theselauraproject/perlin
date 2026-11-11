# AGENTS.md

How to write code comments in Perlin. This applies to everyone, people and coding agents alike.

## The rule

A comment says **why** the code does something: a game quirk, a protocol detail, or a choice that isn't obvious from the code. What the code does, the code already says.

If removing the comment loses nothing, don't write it.

## Density

- Comment where a reader would stop and wonder. Roughly one comment every 50 lines is normal; whole files with none are fine too.
- No doc block on every function. A function name and its parameters should explain themselves; add one line only when the contract is surprising (units, thread, ownership, what an empty result means).
- No section banners (`// ===== Rendering =====`) and no comments that repeat a heading or a name.

## Tone

- Fragments, not sentences. Lowercase, no capital at the start, no period at the end.
- Cut the filler words (`so`, `which`, `that`, `in order to`, `must not`); join the parts with `:` or `;`.
- One line when possible, two at most. A longer note only for a real trap (a binary layout, a protocol ordering, a race).
- Write for someone who knows C++ and Minecraft but not this file.

## What to put in a comment

- The reason behind a value or an order: `// air first: the server indexes its palette from it`.
- What the game or the protocol does that the code copies: `// the game rounds box UV sizes down to whole pixels`.
- A trap for the next change: `// read before SetBlock, which overwrites it`.
- References to the game's own functions when the code mirrors them: `// ClientInstance::_tickBuildAction`.
- Binary and wire layouts, field by field.

## What not to put in a comment

- Narration of the code: `// increment the counter`, `// loop over entities`, `// call the function`.
- History: `// fixed bug`, `// changed from 3 to 4`, `// new version`. That belongs in the commit message.
- Commented-out code. Delete it; git keeps it.
- **Names of specific servers, networks or other clients** (no server brands, no other client or mod-loader names). Describe the behaviour instead: `// some proxies follow the real registry with an empty one`, not the name of the server where it was seen. Names go in the PR description if they matter.
- Who wrote it, dates, ticket numbers, or anything addressed to a reviewer.
- Apologies and hedging: `// sorry`, `// not sure why this works`, `// hopefully`.

## Keeping them true

- Change the comment in the same commit as the code it describes. A wrong comment is worse than none.
- Keep a comment right above the line it explains, not at the top of a long function.
- When code is deleted, delete its comments too.

## TODO

`TODO` is allowed for real known gaps, one line saying what is missing:

```cpp
// TODO: catmullrom keyframes, linear for now
```

## Examples

Written as fragments, short and lowercase. Each one says something the code alone can't.

```cpp
// undefined "$name" after substitution: the game can't parse it, keeps the default (false)
case Json::Type::String: return !v.string.empty() && v.string != "false" && v.string != "0" && v.string[0] != '$';
```

```cpp
// bitmap glyphs: scale snapped to whole device pixels per texel, layout widths keep the exact one
```

```cpp
// hover/default layers get display from the stylesheet: no inline display
" style=\"position:absolute;" + (cls == "hv" || cls == "df" ? "" : "display:block;") + ...
```

```cpp
// server ui still loading: wait for its layout, don't flash ours first
if (!formEngine && formUi.Building() && now - formOpenedAt < std::chrono::seconds(3))
    return;
```

```cpp
// server hitbox (BOUNDINGBOX_WIDTH/HEIGHT), 0 if never sent
```

```cpp
// some proxies send an empty registry after the real one: keep the names
if (p.itemstates.empty())
```

Not like this:

```cpp
// Hover/default layers take display from the stylesheet, so an inline display must not override it.   <- a full sentence
// check if the item registry is empty                                                                    <- says what the code says
// <server name> sends blocks without properties                                                          <- names a server
```
