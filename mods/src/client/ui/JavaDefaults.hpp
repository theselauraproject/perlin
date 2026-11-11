#pragma once
namespace perlin::ui {
    inline constexpr const char* kJavaHudOverrides = R"JSON({
  "namespace": "hud",

  "centered_gui_elements_at_bottom_middle": {
    "modifications": [],
    "offset": [ 0, 2 ]
  },

  "item_name_text@hud.item_name_text_root": {
    "modifications": [],
    "$show_survival_padding": true,
    "$show_text_background": false,
    "offset": [ 0, -11 ]
  },

  "hud_actionbar_text": {
    "modifications": [],
    "alpha": 0
  },

  "hud_title_text": {
    "type": "panel",
    "size": [ "100%", "100%" ],
    "layer": 1,
    "alpha": "@hud.anim_title_text_alpha_in",
    "propagate_alpha": true,
    "controls": [
      {
        "title_frame": {
          "type": "panel",
          "size": [ "100%", "100%cm" ],
          "anchor_from": "center",
          "anchor_to": "center",
          "offset": [ 0, -22 ],
          "controls": [
            {
              "title": {
                "type": "label",
                "anchor_from": "center",
                "anchor_to": "center",
                "color": [ 1, 1, 1 ],
                "text": "#text",
                "layer": 31,
                "localize": false,
                "font_scale_factor": 4.0,
                "shadow": true,
                "text_alignment": "center",
                "bindings": [
                  {
                    "binding_name": "#hud_title_text_string",
                    "binding_name_override": "#text",
                    "binding_type": "global"
                  }
                ]
              }
            }
          ]
        }
      },
      {
        "subtitle_frame": {
          "type": "panel",
          "size": [ "100%", "100%cm" ],
          "anchor_from": "center",
          "anchor_to": "center",
          "offset": [ 0, 19 ],
          "controls": [
            {
              "subtitle": {
                "type": "label",
                "anchor_from": "center",
                "anchor_to": "center",
                "color": [ 1, 1, 1 ],
                "text": "#text",
                "layer": 31,
                "localize": false,
                "font_scale_factor": 2.0,
                "shadow": true,
                "text_alignment": "center",
                "bindings": [
                  {
                    "binding_name": "#hud_subtitle_text_string",
                    "binding_name_override": "#text",
                    "binding_type": "global"
                  }
                ]
              }
            }
          ]
        }
      }
    ]
  },

  "hud_content": {
    "modifications": [
      {
        "array_name": "controls",
        "operation": "insert_back",
        "value": [
          {
            "perlin_debug_overlay": {
              "type": "custom",
              "renderer": "debug_renderer",
              "size": [ 0, 0 ],
              "layer": 40
            }
          },
          {
            "perlin_fire": {
              "type": "custom",
              "renderer": "fire_renderer",
              "size": [ "100%", "100%" ],
              "layer": 0
            }
          },
          {
            "perlin_subtitles": {
              "type": "custom",
              "renderer": "subtitle_renderer",
              "size": [ 0, 0 ],
              "layer": 30
            }
          }
        ]
      }
    ]
  },

  "chat_panel": {
    "modifications": [
      {
        "array_name": "controls",
        "operation": "remove",
        "control_name": "stack_panel"
      }
    ],
    "type": "custom",
    "renderer": "chat_renderer",
    "size": [ 0, 0 ],
    "max_size": [ 0, 0 ],
    "controls": []
  },

  "progress_text_label": {
    "modifications": [],
    "anchor_from": "center",
    "anchor_to": "bottom_middle",
    "shadow": false,
    "perlin_text_outline": true
  }
}
)JSON";
}
