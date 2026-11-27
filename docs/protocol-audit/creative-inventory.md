# Creative catalog and inventory presentation

This change uses the server's `CreativeContent` catalog for category membership,
group order, item order, group icons and creative entry IDs. It does not generate
a supposedly universal Bedrock order from item names. A server or its resource
packs can supply a different catalog.

## Recovered contracts

The matched 1.26.51.1 client has these anchors:

| Contract | Matched RVA / source |
| --- | --- |
| Creative packet handler calls the registry initializer | `0x1343AB0` → `0x348FF50` → `0x346B880` |
| Registry creates categories 1 through 5 | `CreativeItemRegistry::createCategories`, `0x3469B00` |
| Groups are visited in packet order; empty names make anonymous groups | `0x346BC47`, loop increment `0x346BC58`; named-group insertion `0x346BDC7` |
| Entries retain their received sequence; unassigned entries are retained | Entry loop increment `0x346BFEC`; ungrouped/out-of-range check `0x346C490` |
| A group header needs multiple members and a valid nonempty icon | `ExpandoContainerModel::_refreshContainer`, member count `0x3446F14`, icon checks `0x3446F91`–`0x3446FBF` |
| New group headers begin contracted unless retained expansion state matches | Header-state construction `0x3447212`–`0x3447228` |
| Contracted headers hide their child entries in a flat sequence | `ExpandoContainerModel::_generateCurrentItems`, `0x3447935`–`0x344798C` |

The category enum values are `All=0`, `Construction=1`, `Nature=2`,
`Equipment=3`, `Items=4`, `ItemCommandOnly=5`, `Undefined=6`. Category enum
order is different from classic inventory tab order.

Mojang's published inventory UI defines these classic tabs and resources:

| Tab | Wire category | Resource |
| --- | ---: | --- |
| Construction | 1 | `textures/ui/icon_recipe_construction` |
| Equipment | 3 | `textures/ui/icon_recipe_equipment` |
| Items | 4 | `textures/ui/icon_recipe_item` |
| Nature | 2 | `textures/ui/icon_recipe_nature` |
| Search | — | `textures/ui/magnifyingGlass` |

These are fixed composite UI sprites. In particular, the equipment and items
headers are not adequately represented by picking the first sword or item from
a server group. The separate player-inventory control uses
`textures/ui/inventory_icon`; its placement in Perlin's existing screen remains
Perlin's layout.

Primary asset references, pinned to Mojang's 26.50.4 sample release:

- [inventory_screen.json](https://github.com/Mojang/bedrock-samples/blob/46ba6ea985fb5a92d79a9419198f10dda14c199d/resource_pack/ui/inventory_screen.json)
- [ui_common.json](https://github.com/Mojang/bedrock-samples/blob/46ba6ea985fb5a92d79a9419198f10dda14c199d/resource_pack/ui/ui_common.json)
- [Published UI textures](https://github.com/Mojang/bedrock-samples/tree/46ba6ea985fb5a92d79a9419198f10dda14c199d/resource_pack/textures/ui)
- [Creative group schema](https://github.com/Mojang/bedrock-protocol-docs/blob/main/json/CreativeGroupInfoPayload.json)
- [Creative entry schema](https://github.com/Mojang/bedrock-protocol-docs/blob/main/json/CreativeItemEntryPayload.json)

The four category textures, magnifier and inventory texture were also checked
against sample release 26.60.5 (`15701fa9c13923f12662984c32c124327e40e3d3`)
and have identical bytes. No Mojang texture files are copied into this source
change. The renderer resolves the named resources through Perlin's existing
resource-pack and downloaded-samples asset loader.

## Implemented changes

- Explicit category-to-tab mapping uses Construction, Equipment, Items, Nature.
  Category presence remains based on received groups; a packet's group icon no
  longer replaces a fixed category icon.
- Category projection follows server group order and entry order within each
  group. Named multi-item groups with icons produce collapsible headers.
  Anonymous, single-item and iconless groups remain ordinary entries. Expansion
  adds a flat sequence without introducing blank grid rows.
- Group headers use their own server-supplied icon and localized name. Their
  mouse action changes expansion state and never crafts the header icon or
  destroys the cursor stack. Only item cells carry a creative entry ID.
- Creative item and group-icon conversion retains the complete item extra-data
  variant. NBT, display name, damage, enchantments and placement/destruction
  metadata now reach the same item conversion and raw inventory pathways used
  for ordinary server inventory items.
- Catalog caching takes the version from the copied snapshot. Reconnection and
  transfer resets clear the previous catalog and raw creative IDs, and a new
  session or catalog removes obsolete UI expansion indexes.

## Validation and remaining boundaries

`creative_catalog_selftest` tests display/category mapping, non-alphabetical
group order, interleaved received entries, independent expansion, anonymous and
single-item groups, and retention of original item identity.

The app's `creative_inventory` selftest drives the actual inventory screen,
scene hit testing and mouse input with a renderer that does not create a window.
It checks that header clicks send no inventory request and that selecting a
child emits its original, noncontiguous creative entry ID. It then moves that
stack into the hotbar and verifies the raw NBT and related extra data in a real
outgoing use transaction. The same data-flow cases run under protocols 944,
2193 and 2225; this is not independent native-client evidence for all three
profiles. Scene assertions also check the fixed tab texture paths and group
background/expand/contract resources.

The special search visibility of command-only, undefined and ungrouped entries
has not been established from the native controller. The existing flat search
policy is retained, with original entry order and custom-name matching. No
claim is made that this closes the complete native search/collapse preference
model. Category-tab visibility retains Perlin's existing group-derived policy;
the native runtime visibility getter has not been independently traced.
The pre-existing minimum creative-stack count of one is retained, including for
received group icons. Malformed category handling, duplicate creative ID policy,
resource-pack pixel rendering and the surrounding screen layout are also outside
the established equivalence. Windows compilation and interactive rendering
require the corresponding platform build/run.
