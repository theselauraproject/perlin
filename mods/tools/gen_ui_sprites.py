import sys, zipfile, io
from PIL import Image

jar, out = sys.argv[1], sys.argv[2]
z = zipfile.ZipFile(jar)
base = "assets/minecraft/textures/gui/"

def load(path):
    return Image.open(io.BytesIO(z.read(base + path))).convert("RGBA")

def save(name, img):
    img.save(out + "/" + name)

save("ui_inventory.png", load("container/inventory.png").crop((0, 0, 176, 166)))
for src, dst in (("tab_items", "ui_creative_items"), ("tab_item_search", "ui_creative_search"), ("tab_inventory", "ui_creative_survival")):
    save(dst + ".png", load("container/creative_inventory/" + src + ".png").crop((0, 0, 195, 136)))

tabs = Image.new("RGBA", (26 * 7, 32 * 4), (0, 0, 0, 0))
for row, name in enumerate(("tab_top_unselected", "tab_top_selected", "tab_bottom_unselected", "tab_bottom_selected")):
    for col in range(7):
        tabs.paste(load("sprites/container/creative_inventory/%s_%d.png" % (name, col + 1)), (26 * col, 32 * row))
save("ui_creative_tabs.png", tabs)

misc = Image.new("RGBA", (24, 15), (0, 0, 0, 0))
misc.paste(load("sprites/container/creative_inventory/scroller.png"), (0, 0))
misc.paste(load("sprites/container/creative_inventory/scroller_disabled.png"), (12, 0))
save("ui_creative_misc.png", misc)

icons = Image.new("RGBA", (16 * 5, 16), (0, 0, 0, 0))
for i, name in enumerate(("helmet", "chestplate", "leggings", "boots", "shield")):
    icons.paste(load("sprites/container/slot/%s.png" % name), (16 * i, 0))
save("ui_slot_icons.png", icons)

save("ui_crafting_table.png", load("container/crafting_table.png").crop((0, 0, 176, 166)))
book = load("recipe_book.png")
save("ui_recipe_book.png", book.crop((1, 1, 148, 167)))
btn = Image.new("RGBA", (20, 36), (0, 0, 0, 0))
btn.paste(load("sprites/recipe_book/button.png"), (0, 0))
btn.paste(load("sprites/recipe_book/button_highlighted.png"), (0, 18))
save("ui_recipe_button.png", btn)
tabs = Image.new("RGBA", (70, 27), (0, 0, 0, 0))
tabs.paste(load("sprites/recipe_book/tab.png"), (0, 0))
tabs.paste(load("sprites/recipe_book/tab_selected.png"), (35, 0))
save("ui_recipe_tabs.png", tabs)
slots = Image.new("RGBA", (50, 25), (0, 0, 0, 0))
slots.paste(load("sprites/recipe_book/slot_craftable.png"), (0, 0))
slots.paste(load("sprites/recipe_book/slot_uncraftable.png"), (25, 0))
save("ui_recipe_slots.png", slots)
for name in ("effect_background", "effect_background_ambient"):
    save("ui_inv_" + name + ".png", load("sprites/container/inventory/" + name + ".png"))
import os
for f in sorted(z.namelist()):
    if f.startswith("assets/minecraft/textures/mob_effect/") and f.endswith(".png"):
        save("effect_" + os.path.basename(f), Image.open(io.BytesIO(z.read(f))).convert("RGBA"))

pages = Image.new("RGBA", (48, 17), (0, 0, 0, 0))
for i, name in enumerate(("page_backward", "page_backward_highlighted", "page_forward", "page_forward_highlighted")):
    pages.paste(load("sprites/recipe_book/%s.png" % name), (12 * i, 0))
save("ui_recipe_pages.png", pages)
hud = Image.new("RGBA", (48, 24), (0, 0, 0, 0))
hud.paste(load("sprites/hud/effect_background.png"), (0, 0))
hud.paste(load("sprites/hud/effect_background_ambient.png"), (24, 0))
save("ui_hud_effect.png", hud)
