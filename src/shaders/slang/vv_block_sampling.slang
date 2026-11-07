#pragma once
float atlasLod(vec2 du, vec2 dv, vec2 pixelSize)
{
    vec2 sx = du / pixelSize;
    vec2 sy = dv / pixelSize;
    float rho = max(length(sx), length(sy));
    return max(0.0, log2(max(rho, 1.0e-8)));
}

vec4 sampleNearest(vec2 uv, float layer, vec2 pixelSize, vec2 du, vec2 dv, vec2 texelScreenSize, float lod)
{
    vec2 texelCoords = uv / pixelSize;
    vec2 texelCenter = floor(texelCoords + 0.5) - 0.5;
    vec2 texelOffset = texelCoords - texelCenter;
    texelOffset = (texelOffset - 0.5) * pixelSize / max(texelScreenSize, vec2_splat(1.0e-8)) + 0.5;
    texelOffset = clamp(texelOffset, vec2_splat(0.0), vec2_splat(1.0));
    vec2 sampleUv = (texelCenter + texelOffset) * pixelSize;
    return perlinAtlasLod(vec3(sampleUv, layer), lod);
}

vec4 sampleBlock(vec2 uv, float layer)
{
    vec2 pixelSize = u_textureInfo.xx;
    vec2 du = dFdx(uv);
    vec2 dv = dFdy(uv);
    vec2 texelScreenSize = sqrt(du * du + dv * dv);
    float lod = atlasLod(du, dv, pixelSize);
    vec4 nearest = sampleNearest(uv, layer, pixelSize, du, dv, texelScreenSize, lod);
    vec4 result = nearest;
    if (u_textureInfo.y >= 0.5)
    {

    float maxTexelSize = max(texelScreenSize.x, texelScreenSize.y);
    float minPixelSize = min(pixelSize.x, pixelSize.y);
    float blendFactor = smoothstep(minPixelSize, minPixelSize * 2.0, maxTexelSize);

    float duLength = length(du);
    float dvLength = length(dv);
    float effectiveDerivative = sqrt(min(duLength, dvLength) * max(duLength, dvLength));
    float mipExact = max(0.0, log2(max(effectiveDerivative, 1.0e-8) / minPixelSize));
    float mipLow = floor(mipExact);
    float mipHigh = mipLow + 1.0;
    float mipBlend = fract(mipExact);

    vec4 low = vec4_splat(0.0);
    vec4 high = vec4_splat(0.0);
    vec2 offsets[4];
    offsets[0] = vec2(0.125, 0.375);
    offsets[1] = vec2(-0.125, -0.375);
    offsets[2] = vec2(0.375, -0.125);
    offsets[3] = vec2(-0.375, 0.125);
    for (int i = 0; i < 4; i++)
    {
        vec3 coord = vec3(uv + offsets[i] * pixelSize, layer);
        low += perlinAtlasLod(coord, mipLow);
        high += perlinAtlasLod(coord, mipHigh);
    }
    vec4 rgss = mix(low * 0.25, high * 0.25, mipBlend);
    result = mix(nearest, rgss, blendFactor);
    }
    return result;
}
