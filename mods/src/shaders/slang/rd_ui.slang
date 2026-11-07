#pragma once
#include "rd_common.slang"

#define OverlayColor u_uiOverlay
#define DiscardValue u_uiParams
#define MatColor1 vec4(v_color0.rgb, 1.0)
#define TileLightColor vec4_splat(1.0)
#define GlintColor (vec4(0.38, 0.19, 0.608, 1.0) * sqrt(max(u_uiParams.z, 0.0)))
#define TintColor vec4_splat(1.0)
#define HudOpacity vec4_splat(1.0)
#define OcclusionHeightOffset vec4_splat(0.0)

vec4 rdBase(vec2 uv) { return texture2D(s_uiTex, uv); }
vec4 rdGlint(vec2 uv) { return texture2D(s_uiGlint, uv); }
