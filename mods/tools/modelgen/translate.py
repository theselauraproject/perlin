"""Bedrock block state -> Java block (name, properties).

translate() returns (java_name, fixed_props, search) or None when the block keeps the legacy path.
`search` maps Java property -> candidate values; the generator picks the candidate whose model best
matches the Bedrock collision shape (see modelgen.fit) -- this resolves handedness conventions
(door hinges, stair corners, trapdoor facing...) from real Bedrock data instead of guesses.
"""

FD = {0: "down", 1: "up", 2: "north", 3: "south", 4: "west", 5: "east"}
DIRN = {0: "south", 1: "west", 2: "north", 3: "east"}
WEIRDO = {0: "east", 1: "west", 2: "south", 3: "north"}
HORIZ = ["north", "east", "south", "west"]

def b(v):
    return "true" if v else "false"

ALIASES = {
    "brick_block": "bricks", "beetroot": "beetroots", "deadbush": "dead_bush", "end_brick_stairs": "end_stone_brick_stairs",
    "fence_gate": "oak_fence_gate", "frog_spawn": "frogspawn", "golden_rail": "powered_rail", "grass_path": "dirt_path",
    "melon_block": "melon", "mob_spawner": "spawner", "nether_brick": "nether_bricks", "noteblock": "note_block",
    "prismarine_bricks_stairs": "prismarine_brick_stairs", "reeds": "sugar_cane", "slime": "slime_block",
    "stonecutter_block": "stonecutter", "trapdoor": "oak_trapdoor", "waterlily": "lily_pad", "web": "cobweb",
    "wooden_button": "oak_button", "wooden_door": "oak_door", "wooden_pressure_plate": "oak_pressure_plate",
    "normal_stone_stairs": "stone_stairs", "normal_stone_slab": "stone_slab", "red_nether_brick": "red_nether_bricks",
    "waxed_copper": "waxed_copper_block", "daylight_detector_inverted": "daylight_detector",
    "small_dripleaf_block": "small_dripleaf", "azalea_leaves_flowered": "flowering_azalea_leaves",
    "lit_pumpkin": "jack_o_lantern", "trip_wire": "tripwire", "undyed_shulker_box": "shulker_box",
    "dirt_with_roots": "rooted_dirt", "hardened_clay": "terracotta", "glowingobsidian": "crying_obsidian",
    "powered_comparator": "comparator", "unpowered_comparator": "comparator",
    "powered_repeater": "repeater", "unpowered_repeater": "repeater", "snow_layer": "snow", "snow": "snow_block",
    "quartz_ore": "nether_quartz_ore", "silver_glazed_terracotta": "light_gray_glazed_terracotta",
    "lit_furnace": "furnace", "lit_blast_furnace": "blast_furnace", "lit_smoker": "smoker",
    "lit_redstone_lamp": "redstone_lamp", "lit_redstone_ore": "redstone_ore",
    "lit_deepslate_redstone_ore": "deepslate_redstone_ore", "unlit_redstone_torch": "redstone_torch",
    "pearlescent_froglight": "pearlescent_froglight", "magma": "magma_block", "stone_slab": "stone_slab",
}

def java_name(bname):
    n = bname.replace("minecraft:", "")
    return ALIASES.get(n, n)

def _facing4(st):
    """Horizontal facing for the several Bedrock encodings."""
    if "minecraft:cardinal_direction" in st:
        return st["minecraft:cardinal_direction"]
    if "direction" in st:
        return DIRN[int(st["direction"])]
    if "facing_direction" in st:
        return FD.get(int(st["facing_direction"]), "north")
    return "north"

def translate(bname, st, jb):
    """jb: dict of Java blocks.  Returns (jname, props, search) or None."""
    n = bname.replace("minecraft:", "")
    if n.startswith("cinnabar:") or n.endswith("_double_slab") or "double_cut_copper_slab" in n:
        return None
    jn = java_name(bname)
    props = {}
    search = {}

    if "weirdo_direction" in st:
        if jn not in jb:
            return None
        props["facing"] = WEIRDO[int(st["weirdo_direction"])]
        props["half"] = "top" if int(st.get("upside_down_bit", 0)) else "bottom"
        c = st.get("minecraft:corner", "none")
        if c == "none":
            props["shape"] = "straight"
        else:
            kind = c.split("_")[0]
            search["shape"] = [kind + "_left", kind + "_right"]
        return jn, props, search

    if "minecraft:vertical_half" in st and jn.endswith("slab"):
        props["type"] = st["minecraft:vertical_half"]
        return jn, props, search

    if "pillar_axis" in st and not ("creaking_heart_state" in st):
        props["axis"] = st["pillar_axis"]
        return jn, props, search
    if jn.endswith("_chain") or jn == "chain" or jn == "iron_chain":
        pass

    if "minecraft:connection_north" in st and "trip" not in n:
        for d in ("north", "east", "south", "west"):
            props[d] = b(int(st["minecraft:connection_" + d]))
        return jn, props, search
    if "wall_connection_type_east" in st:
        for d in ("north", "east", "south", "west"):
            v = st["wall_connection_type_" + d]
            props[d] = {"none": "none", "short": "low", "tall": "tall"}[v]
        props["up"] = b(int(st.get("wall_post_bit", 0)))
        return jn, props, search

    if "door_hinge_bit" in st:
        props["half"] = "upper" if int(st["upper_block_bit"]) else "lower"
        props["open"] = b(int(st["open_bit"]))
        props["facing"] = {"south": "east", "west": "south", "north": "west", "east": "north"}[st["minecraft:cardinal_direction"]]
        props["hinge"] = "left" if int(st["door_hinge_bit"]) == 0 else "right"
        return jn, props, search

    if "upside_down_bit" in st and "open_bit" in st and "direction" in st:
        props["half"] = "top" if int(st["upside_down_bit"]) else "bottom"
        props["open"] = b(int(st["open_bit"]))
        props["facing"] = {0: "east", 1: "west", 2: "south", 3: "north"}[int(st["direction"])]
        return jn, props, search

    if "in_wall_bit" in st:
        props["open"] = b(int(st["open_bit"]))
        props["in_wall"] = b(int(st["in_wall_bit"]))
        props["facing"] = st["minecraft:cardinal_direction"]
        search["facing"] = [st["minecraft:cardinal_direction"]] + [x for x in HORIZ if x != st["minecraft:cardinal_direction"]]
        return jn, props, search

    if jn.endswith("_button"):
        d = FD[int(st["facing_direction"])]
        props["powered"] = b(int(st["button_pressed_bit"]))
        if d == "up":
            props["face"], props["facing"] = "floor", "north"
        elif d == "down":
            props["face"], props["facing"] = "ceiling", "north"
        else:
            props["face"], props["facing"] = "wall", d
        return jn, props, search

    if jn.endswith("_pressure_plate"):
        sig = int(st.get("redstone_signal", 0))
        if "weighted" in jn:
            props["power"] = str(sig)
        else:
            props["powered"] = b(sig > 0)
        return jn, props, search

    if jn == "ladder":
        props["facing"] = FD.get(int(st["facing_direction"]), "north")
        return jn, props, search

    if "torch_facing_direction" in st:
        d = st["torch_facing_direction"]
        base = {"torch": "torch", "soul_torch": "soul_torch", "redstone_torch": "redstone_torch",
                "unlit_redstone_torch": "redstone_torch", "copper_torch": "copper_torch"}.get(n)
        if base is None:
            return None
        if d in ("top", "unknown"):
            jnm = base
        else:
            jnm = base.replace("torch", "wall_torch")
            props["facing"] = d
        if base == "redstone_torch":
            props["lit"] = b(n != "unlit_redstone_torch")
        return jnm, props, search

    if jn.endswith("lantern") and "hanging" in st:
        props["hanging"] = b(int(st["hanging"]))
        return jn, props, search

    if jn == "lever":
        d = st["lever_direction"]
        props["powered"] = b(int(st["open_bit"]))
        if d in ("north", "south", "east", "west"):
            props["face"], props["facing"] = "wall", d
        else:
            props["face"] = "floor" if d.startswith("up") else "ceiling"
            props["facing"] = "north" if d.endswith("north_south") else "east"
        return jn, props, search

    if "rail_direction" in st:
        shapes = ["north_south", "east_west", "ascending_east", "ascending_west", "ascending_north",
                  "ascending_south", "south_east", "south_west", "north_west", "north_east"]
        props["shape"] = shapes[int(st["rail_direction"])]
        if "rail_data_bit" in st:
            props["powered"] = b(int(st["rail_data_bit"]))
        return jn, props, search

    if n == "portal":
        props["axis"] = "z" if st.get("portal_axis") == "z" else "x"
        return "nether_portal", props, search

    if n == "bed":
        return None
        jn = "red_bed"
        props["part"] = "head" if int(st["head_piece_bit"]) else "foot"
        props["facing"] = DIRN[int(st["direction"])]
        return jn, props, search

    if "candles" in st:
        props["candles"] = str(int(st["candles"]) + 1)
        props["lit"] = b(int(st["lit"]))
        return jn, props, search
    if n.endswith("candle_cake"):
        props["lit"] = b(int(st["lit"]))
        return jn, props, search
    if n == "cake":
        props["bites"] = str(int(st["bite_counter"]))
        return jn, props, search

    if "powered_shelf_type" in st:
        props["facing"] = st["minecraft:cardinal_direction"]
        props["powered"] = b(int(st["powered_bit"]))
        props["side_chain"] = ["unconnected", "right", "center", "left"][int(st["powered_shelf_type"])]
        return jn, props, search

    if "dripstone_thickness" in st:
        t = st["dripstone_thickness"]
        props["thickness"] = "tip_merge" if t == "merge" else t
        props["vertical_direction"] = "down" if int(st["hanging"]) else "up"
        return jn, props, search

    if "minecraft:block_face" in st:
        props["facing"] = st["minecraft:block_face"]
        return jn, props, search

    if n == "hopper":
        d = FD[int(st["facing_direction"])]
        props["facing"] = d if d != "up" else "down"
        props["enabled"] = b(not int(st["toggle_bit"]))
        return jn, props, search
    if n in ("piston", "sticky_piston"):
        props["facing"] = FD[int(st["facing_direction"])]
        props["extended"] = "false"
        return jn, props, search
    if n == "observer":
        props["facing"] = st["minecraft:facing_direction"]
        props["powered"] = b(int(st["powered_bit"]))
        return jn, props, search
    if n in ("dispenser", "dropper"):
        props["facing"] = FD[int(st["facing_direction"])]
        props["triggered"] = b(int(st["triggered_bit"]))
        return jn, props, search
    if n == "barrel":
        props["facing"] = FD[int(st["facing_direction"])]
        props["open"] = b(int(st["open_bit"]))
        return jn, props, search
    if n.endswith("command_block"):
        props["facing"] = FD[int(st["facing_direction"])]
        props["conditional"] = b(int(st["conditional_bit"]))
        return jn, props, search
    if n.endswith("lightning_rod"):
        props["facing"] = FD[int(st["facing_direction"])]
        props["powered"] = b(int(st["powered_bit"]))
        return jn, props, search
    if jn.endswith("glazed_terracotta"):
        props["facing"] = FD.get(int(st["facing_direction"]), "north")
        return jn, props, search
    if n in ("furnace", "blast_furnace", "smoker", "lit_furnace", "lit_blast_furnace", "lit_smoker"):
        props["facing"] = st["minecraft:cardinal_direction"]
        props["lit"] = b(n.startswith("lit_"))
        return jn, props, search

    if "copper_bulb" in n:
        props["lit"] = b(int(st["lit"]))
        props["powered"] = b(int(st["powered_bit"]))
        return jn, props, search

    if n in ("anvil", "chipped_anvil", "damaged_anvil", "deprecated_anvil"):
        jn = {"deprecated_anvil": "anvil"}.get(n, n)
        props["facing"] = st["minecraft:cardinal_direction"]
        search["facing"] = [st["minecraft:cardinal_direction"]] + [x for x in HORIZ if x != st["minecraft:cardinal_direction"]]
        return jn, props, search
    if n in ("campfire", "soul_campfire"):
        props["facing"] = st["minecraft:cardinal_direction"]
        props["lit"] = b(not int(st["extinguished"]))
        return jn, props, search
    if n == "lectern":
        props["facing"] = st["minecraft:cardinal_direction"]
        props["powered"] = b(int(st["powered_bit"]))
        return jn, props, search
    if n == "end_portal_frame":
        props["facing"] = st["minecraft:cardinal_direction"]
        props["eye"] = b(int(st["end_portal_eye_bit"]))
        return jn, props, search
    if n == "bell":
        props["attachment"] = {"standing": "floor", "hanging": "ceiling", "side": "single_wall",
                               "multiple": "double_wall"}[st["attachment"]]
        props["facing"] = DIRN[int(st["direction"])]
        return jn, props, search
    if n == "grindstone":
        a = st["attachment"]
        props["face"] = {"standing": "floor", "hanging": "ceiling", "side": "wall", "multiple": "wall"}[a]
        props["facing"] = DIRN[int(st["direction"])]
        return jn, props, search
    if n in ("loom", "decorated_pot"):
        props["facing"] = DIRN[int(st["direction"])]
        return jn, props, search
    if n in ("beehive", "bee_nest"):
        props["facing"] = DIRN[int(st["direction"])]
        props["honey_level"] = str(int(st["honey_level"]))
        return jn, props, search
    if n == "composter":
        props["level"] = str(int(st["composter_fill_level"]))
        return jn, props, search
    if n == "brewing_stand":
        for i, k in enumerate("abc"):
            props["has_bottle_%d" % i] = b(int(st["brewing_stand_slot_%s_bit" % k]))
        return jn, props, search
    if n == "cauldron":
        lvl = int(st["fill_level"])
        liq = st["cauldron_liquid"]
        if lvl == 0:
            return "cauldron", {}, search
        if liq == "lava":
            return "lava_cauldron", {}, search
        return ("water_cauldron" if liq == "water" else "powder_snow_cauldron"), {"level": str(max(1, min(3, (lvl + 1) // 2)))}, search
    if n == "cocoa":
        props["age"] = str(int(st["age"]))
        props["facing"] = DIRN[int(st["direction"])]
        search["facing"] = [DIRN[int(st["direction"])]] + [x for x in HORIZ if x != DIRN[int(st["direction"])]]
        return jn, props, search
    if n == "tripwire_hook":
        props["facing"] = DIRN[int(st["direction"])]
        props["attached"] = b(int(st["attached_bit"]))
        props["powered"] = b(int(st["powered_bit"]))
        return jn, props, search
    if n == "tripwire" or n == "trip_wire":
        for d in ("north", "east", "south", "west"):
            props[d] = b(int(st["minecraft:connection_" + d]))
        props["attached"] = b(int(st["attached_bit"]))
        props["powered"] = b(int(st["powered_bit"]))
        props["disarmed"] = b(int(st["disarmed_bit"]))
        return "tripwire", props, search
    if n in ("unpowered_repeater", "powered_repeater"):
        props["facing"] = st["minecraft:cardinal_direction"]
        props["delay"] = str(int(st["repeater_delay"]) + 1)
        props["powered"] = b(n == "powered_repeater")
        return "repeater", props, search
    if n in ("unpowered_comparator", "powered_comparator"):
        props["facing"] = st["minecraft:cardinal_direction"]
        props["mode"] = "subtract" if int(st["output_subtract_bit"]) else "compare"
        props["powered"] = b(int(st["output_lit_bit"]))
        return "comparator", props, search
    if n == "small_dripleaf_block":
        props["facing"] = st["minecraft:cardinal_direction"]
        props["half"] = "upper" if int(st["upper_block_bit"]) else "lower"
        return "small_dripleaf", props, search
    if n == "big_dripleaf":
        tilt = {"none": "none", "unstable": "unstable", "partial_tilt": "partial", "full_tilt": "full"}[st["big_dripleaf_tilt"]]
        props["facing"] = st["minecraft:cardinal_direction"]
        if int(st["big_dripleaf_head"]):
            props["tilt"] = tilt
            return "big_dripleaf", props, search
        return "big_dripleaf_stem", props, search
    if n == "bamboo":
        props["age"] = "1" if st["bamboo_stalk_thickness"] == "thick" else "0"
        props["leaves"] = {"no_leaves": "none", "small_leaves": "small", "large_leaves": "large"}[st["bamboo_leaf_size"]]
        return jn, props, search

    if n in ("wheat", "carrots", "potatoes") and "growth" in st:
        props["age"] = str(int(st["growth"]))
        return jn, props, search
    if n in ("beetroot", "torchflower_crop") and "growth" in st:
        g = int(st["growth"])
        props["age"] = str(g * 3 // 7) if n == "beetroot" else str(min(1, g // 4))
        return jn, props, search
    if n == "nether_wart":
        props["age"] = str(int(st["age"]))
        return jn, props, search
    if n == "sweet_berry_bush":
        props["age"] = str(min(3, int(st["growth"]) // 2))
        return jn, props, search
    if n in ("melon_stem", "pumpkin_stem"):
        d = int(st["facing_direction"])
        if d >= 2:
            return "attached_" + n, {"facing": FD[d]}, search
        props["age"] = str(int(st["growth"]))
        return jn, props, search
    if n == "pitcher_crop":
        props["age"] = str(min(4, int(st["growth"])))
        props["half"] = "upper" if int(st["upper_block_bit"]) else "lower"
        return jn, props, search
    if "upper_block_bit" in st and n not in ("pitcher_crop", "small_dripleaf_block", "pale_moss_carpet") and "door" not in n:
        props["half"] = "upper" if int(st["upper_block_bit"]) else "lower"
        return jn, props, search
    if n == "seagrass":
        t = st["sea_grass_type"]
        if t == "default":
            return "seagrass", {}, search
        return "tall_seagrass", {"half": "lower" if t == "double_bot" else "upper"}, search
    if n == "chorus_flower":
        props["age"] = str(int(st["age"]))
        return jn, props, search
    if n == "farmland":
        props["moisture"] = str(int(st["moisturized_amount"]))
        return jn, props, search
    if n == "snow_layer":
        props["layers"] = str(int(st["height"]) + 1)
        return "snow", props, search
    if n in ("kelp",):
        return "kelp", {"age": str(min(25, int(st["kelp_age"])))}, search
    if n in ("weeping_vines", "twisting_vines"):
        return n, {"age": str(min(25, int(st[n + "_age"])))}, search
    if n == "cave_vines" or n.startswith("cave_vines"):
        berries = "berries" in n
        head = "head" in n
        return ("cave_vines" if head or n == "cave_vines" else "cave_vines_plant"), {"berries": b(berries)}, search
    if n == "vine":
        bits = int(st["vine_direction_bits"])
        props["south"] = b(bits & 1)
        props["west"] = b(bits & 2)
        props["north"] = b(bits & 4)
        props["east"] = b(bits & 8)
        props["up"] = "false"
        return jn, props, search
    if "multi_face_direction_bits" in st:
        bits = int(st["multi_face_direction_bits"])
        for i, d in enumerate(("down", "up", "north", "south", "west", "east")):
            props[d] = b(bits & (1 << i))
        return jn, props, search
    if "huge_mushroom_bits" in st:
        bits = int(st["huge_mushroom_bits"])
        faces = {"down": 0, "up": 0, "north": 0, "south": 0, "west": 0, "east": 0}
        table = {
            1: ("up", "west", "north"), 2: ("up", "north"), 3: ("up", "east", "north"), 4: ("up", "west"),
            5: ("up",), 6: ("up", "east"), 7: ("up", "west", "south"), 8: ("up", "south"),
            9: ("up", "east", "south"), 10: ("north", "east", "south", "west"),
            14: ("down", "up", "north", "south", "west", "east"), 15: ("down", "up", "north", "south", "west", "east"),
        }
        for d in table.get(bits, ()):
            faces[d] = 1
        if n == "mushroom_stem":
            faces = {d: 1 for d in faces}
        for d, v in faces.items():
            props[d] = b(v)
        return jn, props, search
    if n in ("pink_petals", "wildflowers"):
        props["facing"] = st["minecraft:cardinal_direction"]
        props["flower_amount"] = str((int(st["growth"]) & 3) + 1)
        return jn, props, search
    if n == "leaf_litter":
        props["facing"] = st["minecraft:cardinal_direction"]
        props["segment_amount"] = str((int(st["growth"]) & 3) + 1)
        return jn, props, search
    if n == "turtle_egg":
        props["eggs"] = {"one_egg": "1", "two_egg": "2", "three_egg": "3", "four_egg": "4"}[st["turtle_egg_count"]]
        props["hatch"] = {"no_cracks": "0", "cracked": "1", "max_cracked": "2"}[st["cracked_state"]]
        return jn, props, search
    if n == "sniffer_egg":
        props["hatch"] = {"no_cracks": "0", "cracked": "1", "max_cracked": "2"}[st["cracked_state"]]
        return jn, props, search
    if n == "sea_pickle":
        props["pickles"] = str(int(st["cluster_count"]) + 1)
        props["waterlogged"] = b(not int(st["dead_bit"]))
        return jn, props, search
    if n == "pale_hanging_moss":
        props["tip"] = b(int(st["tip"]))
        return jn, props, search
    if n == "pale_moss_carpet":
        props["bottom"] = b(not int(st["upper_block_bit"]))
        for d in ("north", "east", "south", "west"):
            props[d] = {"none": "none", "short": "low", "tall": "tall"}[st["pale_moss_carpet_side_" + d]]
        return jn, props, search
    if n == "scaffolding":
        props["bottom"] = b(int(st["stability"]) > 0)
        return jn, props, search
    if n == "respawn_anchor":
        props["charges"] = str(int(st["respawn_anchor_charge"]))
        return jn, props, search
    if n == "sculk_catalyst":
        props["bloom"] = b(int(st["bloom"]))
        return jn, props, search
    if n == "sculk_sensor":
        props["sculk_sensor_phase"] = ["inactive", "active", "cooldown"][int(st["sculk_sensor_phase"])]
        return jn, props, search
    if n == "calibrated_sculk_sensor":
        props["facing"] = st["minecraft:cardinal_direction"]
        props["sculk_sensor_phase"] = ["inactive", "active", "cooldown"][int(st["sculk_sensor_phase"])]
        return jn, props, search
    if n == "sculk_shrieker":
        props["can_summon"] = b(int(st["can_summon"]))
        return jn, props, search
    if n == "dried_ghast":
        props["facing"] = st["minecraft:cardinal_direction"]
        props["hydration"] = str(int(st["rehydration_level"]))
        return jn, props, search
    if n.endswith("_hanging_sign") or n.endswith("_sign"):
        return None
    if "coral_fan" in n and "wall" not in n:
        return jn, props, search
    if "coral_wall_fan" in n:
        props["facing"] = DIRN.get(int(st["coral_direction"]), "north")
        return jn, props, search
    if n == "creaking_heart":
        props["axis"] = st["pillar_axis"]
        props["creaking_heart_state"] = {"uprooted": "uprooted", "dormant": "dormant", "awake": "awake"}[st["creaking_heart_state"]]
        return jn, props, search
    if n == "vault":
        props["facing"] = st["minecraft:cardinal_direction"]
        props["ominous"] = b(int(st["ominous"]))
        props["vault_state"] = st["vault_state"]
        return jn, props, search
    if n == "trial_spawner":
        props["ominous"] = b(int(st["ominous"]))
        return jn, props, search
    if n == "cactus" or n == "cactus_flower":
        return jn, props, search

    if not st or all(k in IGNORE_KEYS for k in st):
        return jn, props, search
    return jn, props, search

IGNORE_KEYS = {"age", "update_bit", "persistent_bit", "infiniburn_bit", "deprecated", "brushed_progress", "hanging",
               "explode_bit", "age_bit"}
