// java terrain density on the gpu: the same arithmetic as src/world/vanilla (float values, double coordinates, no fused multiply-add)
// four phases over one pipeline, picked by the push constant
//   phase 0  corners:  the inner graph (everything an `interpolated` node samples) at every cell corner of every chunk
//   phase 1  density:  the outer graph per block; `interpolated` nodes blend the eight corners around the block
//   phase 2  columns:  per column, the ground height from the densities, the six climate functions at its biome site, and the nearest box of Java's
//                      biome placement table (src/world/vanilla/VanillaSurface.cpp is the cpu reference for this and the next phase)
//   phase 3  surface:  per block, the block id from the column's biome materials (bedrock-samples) and the density; written over the density in place
// the graph is data, not code: see gpu/VanillaGpuProgram.hpp for the table layout
//   set 0 binding 0  tables:  header (16 offsets) then nodes, aux, noises, layers, perms, blends, order, interpolated inputs, the climate list,
//                             the biome placement table and the biome materials
//   set 0 binding 1  chunks:  chunk x, z per batch entry
//   set 0 binding 2  scratch: corners from phase 0, in to phase 1: [chunk][cx 5][cz 5][cy 49][interpolated input]; then from columnBase, per column,
//                             the ground index and the biome, written by phase 2 and read by phase 3
//   set 0 binding 3  density: out of phase 1: [chunk][x][z][y], one float per block; phase 3 turns each into its block id

struct Params
{
    uint chunkCount;
    uint phase;
    uint orderBegin;
    uint orderCount;
    uint root;
    uint interpCount;
    uint columnBase;
    uint climateBase;
    int minY;
    uint height;
    uint cellXz;
    uint cellY;
    int quartMinY;
    uint quartLayers;
    uint reserved0;
    uint reserved1;
};

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0, 0)]] StructuredBuffer<uint> tables;
[[vk::binding(1, 0)]] StructuredBuffer<int2> chunks;
// scratch holds raw 32-bit words (float corner values and integer column data alike), converted with asfloat / asuint at each use
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> corners;
[[vk::binding(3, 0)]] RWStructuredBuffer<float> density;

static const uint kNodeStride = 14;
static const uint kLayerStride = 12;
static const uint kBlendStride = 16;
#define kMinY (params.minY)
#define kHeight (params.height)
#define kCornersXz (16u / params.cellXz + 1u)
#define kCornersY (params.height / params.cellY + 1u)

// header words
static const uint H_NODES = 0;
static const uint H_AUX = 1;
static const uint H_NOISES = 2;
static const uint H_LAYERS = 3;
static const uint H_PERMS = 4;
static const uint H_BLENDS = 5;
static const uint H_ORDER = 6;
static const uint H_INTERP = 7;
static const uint H_CLIMATE = 8;
static const uint H_BIOMES = 9;
static const uint H_MATERIALS = 10;

// the value of every node for the position being evaluated; the thread's own copy
static float g_vals[512];
// where the block sits in its cell (phase 1)
static uint g_chunk;
static int g_cx, g_cy, g_cz;
static int g_fx, g_fy, g_fz;

// ---- helpers ------------------------------------------------------------------------------------------------------

double dbl(uint lo, uint hi)
{
    return asdouble(lo, hi);
}

// float division correctly rounded: vulkan allows 2.5 ulp for a plain float divide, a double divide rounded once to float is exact
// a double product that must not fuse with an add that follows it (the cpu rounds the product first)
double mulp(double a, double b)
{
    precise double r = a * b;
    return r;
}

float fdiv(float a, float b)
{
    return float(double(a) / double(b));
}

float lerpf(float t, float a, float b)
{
    precise float d = b - a;
    precise float m = t * d;
    precise float r = a + m;
    return r;
}

// java's Math.min / Math.max on floats: NaN wins, -0.0 is below 0.0
float jmin(float a, float b)
{
    if (a != a)
        return a;
    if (a == 0.0f && b == 0.0f)
        return (asuint(a) >> 31) != 0 ? a : b;
    return a <= b ? a : b;
}

float jmax(float a, float b)
{
    if (a != a)
        return a;
    if (a == 0.0f && b == 0.0f)
        return (asuint(a) >> 31) != 0 ? b : a;
    return a >= b ? a : b;
}

float clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    return hi < v ? hi : v;
}

// ---- perlin noise -------------------------------------------------------------------------------------------------

struct Layer
{
    double frequency;
    float amplitude;
    double ox, oy, oz;
    uint perm;
    double fudge;
};

Layer loadLayer(uint index)
{
    uint b = tables[H_LAYERS] + index * kLayerStride;
    Layer l;
    l.frequency = dbl(tables[b + 0], tables[b + 1]);
    l.amplitude = asfloat(tables[b + 2]);
    l.ox = dbl(tables[b + 3], tables[b + 4]);
    l.oy = dbl(tables[b + 5], tables[b + 6]);
    l.oz = dbl(tables[b + 7], tables[b + 8]);
    l.perm = tables[b + 9];
    l.fudge = dbl(tables[b + 10], tables[b + 11]);
    return l;
}

int permute(uint perm, int i)
{
    return int(tables[tables[H_PERMS] + perm + uint(i & 255)]);
}

// positions beyond +-2^24 wrap so float precision holds
double wrapPos(double x)
{
    if (x >= -16777216.0 && x < 16777216.0)
        return x;
    precise double scaled = x * 2.9802322387695312e-08;
    precise double shifted = scaled + 0.5;
    precise double wrapped = x - mulp(floor(shifted), 33554432.0);
    return wrapped;
}

int javaFloor(double d)
{
    double f = floor(d);
    if (f != f)
        return 0;
    if (f >= 2147483647.0)
        return 2147483647;
    if (f <= -2147483648.0)
        return -2147483647 - 1;
    return int(f);
}

static const int3 kGrad[16] = {
    int3(1, 1, 0), int3(-1, 1, 0), int3(1, -1, 0), int3(-1, -1, 0), int3(1, 0, 1), int3(-1, 0, 1), int3(1, 0, -1), int3(-1, 0, -1),
    int3(0, 1, 1), int3(0, -1, 1), int3(0, 1, -1), int3(0, -1, -1), int3(1, 1, 0), int3(0, -1, 1), int3(-1, 1, 0), int3(0, -1, -1)
};

float gradDot(int hash, float x, float y, float z)
{
    int3 g = kGrad[hash & 15];
    precise float a = float(g.x) * x;
    precise float b = float(g.y) * y;
    precise float c = float(g.z) * z;
    precise float ab = a + b;
    precise float r = ab + c;
    return r;
}

float smoothstepf(float f)
{
    precise float f2 = f * f;
    precise float f3 = f2 * f;
    precise float t = f * 6.0f;
    precise float u = t - 15.0f;
    precise float v = f * u;
    precise float w = v + 10.0f;
    precise float r = f3 * w;
    return r;
}

float lerp2f(float d1, float d2, float v00, float v10, float v01, float v11)
{
    return lerpf(d2, lerpf(d1, v00, v10), lerpf(d1, v01, v11));
}

float sampleAndLerp(uint perm, int ix, int iy, int iz, float fx, float fy, float fz, float smoothY)
{
    int p0 = permute(perm, ix);
    int p1 = permute(perm, ix + 1);
    int i10 = permute(perm, p0 + iy);
    int i11 = permute(perm, p0 + iy + 1);
    int i12 = permute(perm, p1 + iy);
    int i13 = permute(perm, p1 + iy + 1);
    float g000 = gradDot(permute(perm, i10 + iz), fx, fy, fz);
    float g100 = gradDot(permute(perm, i12 + iz), fx - 1.0f, fy, fz);
    float g010 = gradDot(permute(perm, i11 + iz), fx, fy - 1.0f, fz);
    float g110 = gradDot(permute(perm, i13 + iz), fx - 1.0f, fy - 1.0f, fz);
    float g001 = gradDot(permute(perm, i10 + iz + 1), fx, fy, fz - 1.0f);
    float g101 = gradDot(permute(perm, i12 + iz + 1), fx - 1.0f, fy, fz - 1.0f);
    float g011 = gradDot(permute(perm, i11 + iz + 1), fx, fy - 1.0f, fz - 1.0f);
    float g111 = gradDot(permute(perm, i13 + iz + 1), fx - 1.0f, fy - 1.0f, fz - 1.0f);
    float sx = smoothstepf(fx);
    float sy = smoothstepf(smoothY);
    float sz = smoothstepf(fz);
    return lerpf(sz, lerp2f(sx, sy, g000, g100, g010, g110), lerp2f(sx, sy, g001, g101, g011, g111));
}

float perlinGet(Layer l, double x, double y, double z)
{
    double px = wrapPos(x) + l.ox;
    double py = wrapPos(y) + l.oy;
    double pz = wrapPos(z) + l.oz;
    int ix = javaFloor(px);
    int iy = javaFloor(py);
    int iz = javaFloor(pz);
    float fx = float(px - double(ix));
    float fy = float(py - double(iy));
    float fz = float(pz - double(iz));
    return sampleAndLerp(l.perm, ix, iy, iz, fx, fy, fz, fy);
}

// the old terrain noise: y snaps to steps of the fudge scale while it is within the first step
float smearedGet(Layer l, double x, double y, double z)
{
    double px = wrapPos(x) + l.ox;
    double py = wrapPos(y) + l.oy;
    double pz = wrapPos(z) + l.oz;
    int ix = javaFloor(px);
    int iy = javaFloor(py);
    int iz = javaFloor(pz);
    float fx = float(px - double(ix));
    double fyd = py - double(iy);
    float fz = float(pz - double(iz));
    double d = (y >= 0.0 && y < fyd) ? y : fyd;
    // 1.0000000116860974E-7 as bits, so no compiler rounds the literal
    double eps = asdouble(0xa0000000u, 0x3e7ad7f2u);
    double fudge = mulp(double(javaFloor(d / l.fudge + eps)), l.fudge);
    return sampleAndLerp(l.perm, ix, iy, iz, fx, float(fyd - fudge), fz, float(fyd));
}

float normalNoiseGet(uint index, double x, double y, double z)
{
    uint start = tables[tables[H_NOISES] + index * 2];
    uint count = tables[tables[H_NOISES] + index * 2 + 1];
    float sum = 0.0f;
    for (uint i = 0; i < count; i++)
    {
        Layer l = loadLayer(start + i);
        float v = perlinGet(l, mulp(x, l.frequency), mulp(y, l.frequency), mulp(z, l.frequency));
        precise float p = l.amplitude * v;
        precise float s = sum + p;
        sum = s;
    }
    return sum;
}

float blendStack(uint start, uint count, double x, double y, double z)
{
    float sum = 0.0f;
    for (uint i = 0; i < count; i++)
    {
        Layer l = loadLayer(start + i);
        float v = smearedGet(l, mulp(x, l.frequency), mulp(y, l.frequency), mulp(z, l.frequency));
        precise float p = l.amplitude * v;
        precise float s = sum + p;
        sum = s;
    }
    return sum;
}

float blendedGet(uint index, int x, int y, int z)
{
    uint b = tables[H_BLENDS] + index * kBlendStride;
    double xzM = dbl(tables[b + 0], tables[b + 1]);
    double yM = dbl(tables[b + 2], tables[b + 3]);
    double xzS = dbl(tables[b + 4], tables[b + 5]);
    double yS = dbl(tables[b + 6], tables[b + 7]);
    double dx = double(x), dy = double(y), dz = double(z);
    // pass 0 blends (main), pass 1 the lower limit, pass 2 the upper; the limits are skipped when alpha is 0 or 1
    float alpha = 0.0f;
    float lower = 0.0f;
    float upper = 0.0f;
    [loop]
    for (uint pass = 0; pass < 3; pass++)
    {
        if (pass == 1 && alpha == 1.0f)
            continue;
        if (pass == 2 && alpha == 0.0f)
            continue;
        uint w = pass == 0 ? 12 : (pass == 1 ? 8 : 10);
        uint start = tables[b + w];
        uint count = tables[b + w + 1];
        double mx = pass == 0 ? xzS : xzM;
        double my = pass == 0 ? yS : yM;
        float v = blendStack(start, count, mulp(dx, mx), mulp(dy, my), mulp(dz, mx));
        if (pass == 0)
        {
            precise float m = v + 0.5f;
            alpha = m < 0.0f ? 0.0f : (1.0f < m ? 1.0f : m);
        }
        else if (pass == 1)
            lower = v;
        else
            upper = v;
    }
    if (alpha == 0.0f)
        return lower;
    if (alpha == 1.0f)
        return upper;
    precise float diff = upper - lower;
    precise float scaled = alpha * diff;
    precise float r = lower + scaled;
    return r;
}

// ---- simplex and the end islands -------------------------------------------------------------------------------

static const int2 kGrad2[12] = {
    int2(1, 1), int2(-1, 1), int2(1, -1), int2(-1, -1), int2(1, 0), int2(-1, 0), int2(1, 0), int2(-1, 0),
    int2(0, 1), int2(0, -1), int2(0, 1), int2(0, -1)
};

double simplexCorner(int g, double x, double y)
{
    precise double xx = x * x;
    precise double yy = y * y;
    precise double t0 = 0.5 - xx;
    precise double t = t0 - yy;
    if (t < 0.0)
        return 0.0;
    precise double t2 = t * t;
    precise double t4 = t2 * t2;
    precise double gx = double(kGrad2[g].x) * x;
    precise double gy = double(kGrad2[g].y) * y;
    precise double dot = gx + gy;
    precise double r = t4 * dot;
    return r;
}

float simplex2(uint perm, double xin, double yin)
{
    double kF2 = asdouble(0xd0b09954u, 0x3fd76cf5u);
    double kG2 = asdouble(0x74df99c8u, 0x3fcb0cb1u);
    precise double xy = xin + yin;
    precise double s = xy * kF2;
    int i = javaFloor(xin + s);
    int j = javaFloor(yin + s);
    precise double t = double(i + j) * kG2;
    precise double X0 = double(i) - t;
    precise double Y0 = double(j) - t;
    precise double x0 = xin - X0;
    precise double y0 = yin - Y0;
    int i1 = x0 > y0 ? 1 : 0;
    int j1 = x0 > y0 ? 0 : 1;
    precise double x1a = x0 - double(i1);
    precise double x1 = x1a + kG2;
    precise double y1a = y0 - double(j1);
    precise double y1 = y1a + kG2;
    precise double g2x2 = 2.0 * kG2;
    precise double x2a = x0 - 1.0;
    precise double x2 = x2a + g2x2;
    precise double y2a = y0 - 1.0;
    precise double y2 = y2a + g2x2;
    int ii = i & 255;
    int jj = j & 255;
    int g0 = permute(perm, ii + permute(perm, jj)) % 12;
    int g1 = permute(perm, ii + i1 + permute(perm, jj + j1)) % 12;
    int g2 = permute(perm, ii + 1 + permute(perm, jj + 1)) % 12;
    precise double n01 = simplexCorner(g0, x0, y0) + simplexCorner(g1, x1, y1);
    precise double n = n01 + simplexCorner(g2, x2, y2);
    precise double r = 70.0 * n;
    return float(r);
}

float fmodExact(float a, float b)
{
    double q = trunc(double(a) / double(b));
    precise double prod = q * double(b);
    precise double rem = double(a) - prod;
    return float(rem);
}

float endIslands(uint perm, int bx, int bz)
{
    int sectionX = bx / 8;
    int sectionZ = bz / 8;
    int chunkX = sectionX / 2;
    int chunkZ = sectionZ / 2;
    int subX = sectionX % 2;
    int subZ = sectionZ % 2;
    float offs = -100.0f;
    [loop]
    for (int xo = -12; xo <= 12; xo++)
    {
        [loop]
        for (int zo = -12; zo <= 12; zo++)
        {
            double tx = double(chunkX + xo);
            double tz = double(chunkZ + zo);
            precise double txx = tx * tx;
            precise double tzz = tz * tz;
            precise double d2 = txx + tzz;
            if (d2 <= 4096.0)
                continue;
            if (!(simplex2(perm, tx, tz) < -0.9f))
                continue;
            precise float ax = abs(float(tx)) * 3439.0f;
            precise float az = abs(float(tz)) * 147.0f;
            precise float sum = ax + az;
            precise float size = fmodExact(sum, 13.0f) + 9.0f;
            float xd = float(subX - xo * 2);
            float zd = float(subZ - zo * 2);
            precise float xx = xd * xd;
            precise float zz = zd * zd;
            precise float dd = xx + zz;
            float root = float(sqrt(double(dd)));
            precise float scaled = root * size;
            precise float v = 100.0f - scaled;
            v = clampf(v, -100.0f, 80.0f);
            offs = max(offs, v);
        }
    }
    precise float shifted = offs - 8.0f;
    return fdiv(shifted, 128.0f);
}

// ---- splines ------------------------------------------------------------------------------------------------------
// a spline point's value is a constant or another spline, and shaders cannot recurse: one function per nesting level.
// aux layout: [coordinate node, point count] then per point [location, derivative, nested?, constant bits or nested spline offset]

#define SPLINE_LEVEL(NAME, NESTED_EVAL, NESTED_ALLOWED)                                                                      \
    float NAME(uint off)                                                                                                     \
    {                                                                                                                        \
        uint aux = tables[H_AUX];                                                                                            \
        float c = g_vals[tables[aux + off]];                                                                                 \
        int n = int(tables[aux + off + 1]);                                                                                  \
        uint pts = aux + off + 2;                                                                                            \
        int lo = 0;                                                                                                          \
        int hi = n;                                                                                                          \
        while (lo < hi)                                                                                                      \
        {                                                                                                                    \
            int mid = (lo + hi) / 2;                                                                                         \
            if (c < asfloat(tables[pts + uint(mid) * 4]))                                                                    \
                hi = mid;                                                                                                    \
            else                                                                                                             \
                lo = mid + 1;                                                                                                \
        }                                                                                                                    \
        int i = lo - 1;                                                                                                      \
        bool edge = i < 0 || i == n - 1;                                                                                     \
        int first = i < 0 ? 0 : (i == n - 1 ? n - 1 : i);                                                                    \
        uint wanted = edge ? 1 : 2;                                                                                          \
        float yv[2] = { 0.0f, 0.0f };                                                                                        \
        [loop]                                                                                                               \
        for (uint j = 0; j < wanted; j++)                                                                                    \
        {                                                                                                                    \
            uint pj = pts + uint(first + int(j)) * 4;                                                                        \
            yv[j] = (NESTED_ALLOWED && tables[pj + 2] != 0) ? NESTED_EVAL(tables[pj + 3]) : asfloat(tables[pj + 3]);         \
        }                                                                                                                    \
        if (edge)                                                                                                            \
        {                                                                                                                    \
            uint p = pts + uint(first) * 4;                                                                                  \
            float d = asfloat(tables[p + 1]);                                                                                \
            if (d == 0.0f)                                                                                                   \
                return yv[0];                                                                                                \
            precise float dc = c - asfloat(tables[p]);                                                                       \
            precise float prod = d * dc;                                                                                     \
            precise float sum = yv[0] + prod;                                                                                \
            return sum;                                                                                                      \
        }                                                                                                                    \
        uint p1 = pts + uint(i) * 4;                                                                                         \
        uint p2 = pts + uint(i + 1) * 4;                                                                                     \
        float x1 = asfloat(tables[p1]);                                                                                      \
        float x2 = asfloat(tables[p2]);                                                                                      \
        precise float dx = x2 - x1;                                                                                          \
        precise float cx = c - x1;                                                                                           \
        float t = fdiv(cx, dx);                                                                                              \
        float y1 = yv[0];                                                                                                    \
        float y2 = yv[1];                                                                                                    \
        float d1 = asfloat(tables[p1 + 1]);                                                                                  \
        float d2 = asfloat(tables[p2 + 1]);                                                                                  \
        precise float dy = y2 - y1;                                                                                          \
        precise float d1dx = d1 * dx;                                                                                        \
        precise float a = d1dx - dy;                                                                                         \
        precise float nd2 = -d2;                                                                                             \
        precise float nd2dx = nd2 * dx;                                                                                      \
        precise float b = nd2dx + dy;                                                                                        \
        precise float omt = 1.0f - t;                                                                                        \
        precise float tomt = t * omt;                                                                                        \
        precise float tail = tomt * lerpf(t, a, b);                                                                          \
        precise float result = lerpf(t, y1, y2) + tail;                                                                      \
        return result;                                                                                                       \
    }

float noNested(uint off)
{
    return 0.0f;
}

SPLINE_LEVEL(splineLevel0, noNested, false)
SPLINE_LEVEL(splineLevel1, splineLevel0, true)
SPLINE_LEVEL(splineLevel2, splineLevel1, true)
SPLINE_LEVEL(splineLevel3, splineLevel2, true)
SPLINE_LEVEL(splineLevel4, splineLevel3, true)
SPLINE_LEVEL(splineLevel5, splineLevel4, true)
SPLINE_LEVEL(splineLevel6, splineLevel5, true)

// ---- the graph ----------------------------------------------------------------------------------------------------

float cornerAt(uint k, int cx, int cy, int cz)
{
    uint at = (((g_chunk * kCornersXz + uint(cx)) * kCornersXz + uint(cz)) * kCornersY + uint(cy)) * params.interpCount + k;
    return asfloat(corners[at]);
}

float interpolated(uint k)
{
    if (g_fx == 0 && g_fy == 0 && g_fz == 0)
        return cornerAt(k, g_cx, g_cy, g_cz);
    float tx = fdiv(float(g_fx), float(params.cellXz));
    float ty = fdiv(float(g_fy), float(params.cellY));
    float tz = fdiv(float(g_fz), float(params.cellXz));
    float v000 = cornerAt(k, g_cx, g_cy, g_cz);
    float v100 = cornerAt(k, g_cx + 1, g_cy, g_cz);
    float v010 = cornerAt(k, g_cx, g_cy + 1, g_cz);
    float v110 = cornerAt(k, g_cx + 1, g_cy + 1, g_cz);
    float v001 = cornerAt(k, g_cx, g_cy, g_cz + 1);
    float v101 = cornerAt(k, g_cx + 1, g_cy, g_cz + 1);
    float v011 = cornerAt(k, g_cx, g_cy + 1, g_cz + 1);
    float v111 = cornerAt(k, g_cx + 1, g_cy + 1, g_cz + 1);
    float a = lerpf(ty, lerpf(tx, v000, v100), lerpf(tx, v010, v110));
    float b = lerpf(ty, lerpf(tx, v001, v101), lerpf(tx, v011, v111));
    return lerpf(tz, a, b);
}

// evaluates the nodes whose ids are listed at tables[listBase ...], in that order, for the block position p
void evalNodes(int3 p0, uint listBase, uint count)
{
    uint nodes = tables[H_NODES];
    uint orderBase = listBase;
    for (uint s = 0; s < count; s++)
    {
        uint n = tables[orderBase + s];
        uint b = nodes + n * kNodeStride;
        uint op = tables[b];
        int3 p = p0;
        uint ov = tables[b + 12];
        if ((ov & 1u) != 0)
            p.x = asint(tables[b + 13]);
        if ((ov & 2u) != 0)
            p.y = asint(tables[b + 13]);
        if ((ov & 4u) != 0)
            p.z = asint(tables[b + 13]);
        uint c1 = tables[b + 1], c2 = tables[b + 2], c3 = tables[b + 3];
        float r = 0.0f;
        // noise nodes (15 to 18) are handled before the switch, in one place: a single lookup call site keeps the compiled shader small, and
        // keeping their working values out of the switch avoids a slang c++ emitter bug that clobbered loop counters in other cases
        if (op >= 15 && op <= 18)
        {
            double nx, ny, nz;
            if (op == 15)
            {
                // scaled position, with the optional shifts added after scaling
                double xzS = dbl(tables[b + 8], tables[b + 9]);
                double yS = dbl(tables[b + 10], tables[b + 11]);
                precise double sx = mulp(double(p.x), xzS);
                precise double sy = mulp(double(p.y), yS);
                precise double sz = mulp(double(p.z), xzS);
                if (tables[b + 7] != 0)
                {
                    sx += double(g_vals[c1]);
                    sz += double(g_vals[c3]);
                    if (c2 != 0xFFFFFFFFu)
                        sy += double(g_vals[c2]);
                }
                nx = sx;
                ny = sy;
                nz = sz;
            }
            else if (op == 16)
            {
                nx = mulp(double(p.x), 0.25);
                ny = mulp(double(p.y), 0.0);
                nz = mulp(double(p.z), 0.25);
            }
            else if (op == 17)
            {
                nx = mulp(double(p.z), 0.25);
                ny = mulp(double(p.x), 0.25);
                nz = mulp(double(p.y), 0.0);
            }
            else
            {
                nx = mulp(double(p.x), 0.25);
                ny = mulp(double(p.y), 0.25);
                nz = mulp(double(p.z), 0.25);
            }
            float v = normalNoiseGet(tables[b + 6], nx, ny, nz);
            if (op != 15)
            {
                precise float q = v * 4.0f;
                v = q;
            }
            g_vals[n] = v;
            continue;
        }
        switch (op)
        {
        case 0: // constant
            r = asfloat(tables[b + 4]);
            break;
        case 1: // add
        {
            precise float v = g_vals[c1] + g_vals[c2];
            r = v;
            break;
        }
        case 2: // sub
        {
            precise float v = g_vals[c1] - g_vals[c2];
            r = v;
            break;
        }
        case 3: // mul
        {
            precise float v = g_vals[c1] * g_vals[c2];
            r = v;
            break;
        }
        case 4: // div
        {
            float l = g_vals[c1];
            r = l == 0.0f ? 0.0f : fdiv(l, g_vals[c2]);
            break;
        }
        case 5:
            r = jmin(g_vals[c1], g_vals[c2]);
            break;
        case 6:
            r = jmax(g_vals[c1], g_vals[c2]);
            break;
        case 7:
            r = abs(g_vals[c1]);
            break;
        case 8: // square
        {
            float v = g_vals[c1];
            precise float q = v * v;
            r = q;
            break;
        }
        case 9: // cube
        {
            float v = g_vals[c1];
            precise float q = v * v;
            precise float q3 = q * v;
            r = q3;
            break;
        }
        case 10: // half negative
        {
            float v = g_vals[c1];
            precise float h = v * 0.5f;
            r = v > 0.0f ? v : h;
            break;
        }
        case 11: // quarter negative
        {
            float v = g_vals[c1];
            precise float h = v * 0.25f;
            r = v > 0.0f ? v : h;
            break;
        }
        case 12: // squeeze
        {
            float c = clampf(g_vals[c1], -1.0f, 1.0f);
            precise float halfC = c * 0.5f;
            precise float c2x = c * c;
            precise float c3x = c2x * c;
            precise float tail = fdiv(c3x, 24.0f);
            precise float v = halfC - tail;
            r = v;
            break;
        }
        case 13:
            r = -g_vals[c1];
            break;
        case 14: // clamp
            r = clampf(g_vals[c1], asfloat(tables[b + 4]), asfloat(tables[b + 5]));
            break;
        case 19: // y / x / z clamped gradient
        {
            uint axis = tables[b + 6];
            int cval = axis == 0 ? p.x : (axis == 1 ? p.y : p.z);
            int clamped = clamp(cval, asint(c1), asint(c2));
            precise float steps = float(clamped - asint(tables[b + 7])) * asfloat(tables[b + 5]);
            precise float v = asfloat(tables[b + 4]) + steps;
            r = v;
            break;
        }
        case 20: // lerp
        {
            float alpha = g_vals[c1];
            if (alpha == 0.0f)
                r = g_vals[c2];
            else if (alpha == 1.0f)
                r = g_vals[c3];
            else
                r = lerpf(alpha, g_vals[c2], g_vals[c3]);
            break;
        }
        case 21: // range choice
        {
            float v = g_vals[c1];
            r = (v >= asfloat(tables[b + 4]) && v < asfloat(tables[b + 5])) ? g_vals[c2] : g_vals[c3];
            break;
        }
        case 22: // interval select
        {
            float v = g_vals[c1];
            uint aux = tables[H_AUX] + tables[b + 6];
            uint thresholds = tables[aux];
            uint i = 0;
            while (i < thresholds && v >= asfloat(tables[aux + 1 + i]))
                i++;
            r = g_vals[tables[aux + 1 + thresholds + i]];
            break;
        }
        case 23: // spline
            r = splineLevel6(tables[b + 6]);
            break;
        case 24: // pass
            r = g_vals[c1];
            break;
        case 25: // interpolated
            r = interpolated(tables[b + 6]);
            break;
        case 26: // old blended noise
            r = blendedGet(tables[b + 6], p.x, p.y, p.z);
            break;
        case 28: // slice: the child was evaluated at the sliced position
            r = g_vals[c1];
            break;
        case 29: // distance to point
        {
            precise float dx = float(asint(tables[b + 4]) - p.x);
            precise float dy = float(asint(tables[b + 5]) - p.y);
            precise float dz = float(asint(tables[b + 6]) - p.z);
            uint metric = tables[b + 7];
            if (metric == 1)
            {
                precise float a = dx * dx;
                precise float bb = dy * dy;
                precise float c = dz * dz;
                precise float ab = a + bb;
                precise float sum = ab + c;
                r = sum;
            }
            else if (metric == 2)
            {
                precise float sum = abs(dx) + abs(dy);
                precise float sum2 = sum + abs(dz);
                r = sum2;
            }
            else if (metric == 3)
                r = max(max(abs(dx), abs(dy)), abs(dz));
            else
            {
                precise float a = dx * dx;
                precise float bb = dy * dy;
                precise float c = dz * dz;
                precise float ab = a + bb;
                precise float sum = ab + c;
                r = float(sqrt(double(sum)));
            }
            break;
        }
        case 30: // end islands
            r = endIslands(tables[b + 6], p.x, p.z);
            break;
        default:
            break;
        }
        g_vals[n] = r;
    }
}

// ---- biomes and surface ------------------------------------------------------------------------------------------

static const int kSeaLevel = 63;
static const int kMidLayers = 3;
static const uint kMaxClimateDistance = 20000;
static const uint kBiomeStride = 14;
static const uint kMaterialStride = 8;

static int g_target[6];

// climate * 10000 truncated toward zero, NaN as 0 (Climate.quantizeCoord)
int quantizeClimate(float v)
{
    precise float scaled = v * 10000.0f;
    if (scaled != scaled)
        return 0;
    return int(clamp(scaled, -2.0e9f, 2.0e9f));
}

// the nearest box of Java's placement table, the first on a tie: a plain search, as VanillaSurface.cpp does it
uint findBiome()
{
    uint table = tables[H_BIOMES];
    uint count = tables[table];
    uint best = 0xFFFFFFFFu;
    uint found = 0;
    for (uint i = 0; i < count; i++)
    {
        uint p = table + 1 + i * kBiomeStride;
        uint fit = 0;
        for (uint d = 0; d < 6; d++)
        {
            if (fit >= best)
                break;
            int lo = asint(tables[p + d * 2]);
            int hi = asint(tables[p + d * 2 + 1]);
            int over = g_target[d] - hi;
            int under = lo - g_target[d];
            uint dist = over > 0 ? uint(over) : (under > 0 ? uint(under) : 0u);
            dist = min(dist, kMaxClimateDistance);
            fit += dist * dist;
        }
        if (fit < best)
        {
            uint offset = uint(abs(asint(tables[p + 12])));
            fit += offset * offset;
            if (fit < best)
            {
                best = fit;
                found = tables[p + 13];
            }
        }
    }
    return found;
}

// the block at index idx (from the floor) of a column whose ground is at index top; the same rules as VanillaSurface::BlockAt
uint blockAt(uint biome, int idx, int top, bool solid)
{
    uint mats = tables[H_MATERIALS];
    uint m = mats + 8 + biome * kMaterialStride;
    uint topMat = tables[m + 0];
    uint midMat = tables[m + 1];
    uint foundation = tables[m + 2];
    uint seaFloor = tables[m + 3];
    uint sea = tables[m + 4];
    int seaFloorDepth = int(tables[m + 5]);
    uint stone = tables[mats + 0];
    uint deepslate = tables[mats + 1];
    uint bedrock = tables[mats + 2];
    uint dirt = tables[mats + 3];
    uint grass = tables[mats + 4];
    int wy = kMinY + idx;
    if (idx == 0)
        return bedrock;
    if (!solid)
        return wy <= kSeaLevel ? sea : 0u;
    int depth = top - idx;
    int topWy = kMinY + top;
    uint block = depth == 0 ? topMat : (depth <= kMidLayers ? midMat : foundation);
    if (depth > kMidLayers && block == stone && wy < 0)
        block = deepslate;
    if (depth <= kMidLayers && topWy < kSeaLevel)
    {
        if (kSeaLevel - topWy > seaFloorDepth)
            block = seaFloor;
        else if (block == grass)
            block = dirt;
    }
    return block;
}

[numthreads(64, 1, 1)]
void csMain(uint3 id : SV_DispatchThreadID)
{
    uint t = id.x;
    if (params.phase == 0)
    {
        uint total = params.chunkCount * kCornersXz * kCornersXz * kCornersY;
        if (t >= total)
            return;
        uint cy = t % kCornersY;
        uint q = t / kCornersY;
        uint cz = q % kCornersXz;
        q /= kCornersXz;
        uint cx = q % kCornersXz;
        uint chunk = q / kCornersXz;
        int2 origin = chunks[chunk] * 16;
        int3 p = int3(origin.x + int(cx * params.cellXz), kMinY + int(cy * params.cellY), origin.y + int(cz * params.cellXz));
        evalNodes(p, tables[H_ORDER] + params.orderBegin, params.orderCount);
        uint at = (((chunk * kCornersXz + cx) * kCornersXz + cz) * kCornersY + cy) * params.interpCount;
        uint inputs = tables[H_INTERP];
        for (uint k = 0; k < params.interpCount; k++)
            corners[at + k] = asuint(g_vals[tables[inputs + k]]);
    }
    else if (params.phase == 1)
    {
        uint total = params.chunkCount * 256 * kHeight;
        if (t >= total)
            return;
        uint y = t % kHeight;
        uint q = t / kHeight;
        uint z = q % 16;
        q /= 16;
        uint x = q % 16;
        uint chunk = q / 16;
        g_chunk = chunk;
        g_cx = int(x / params.cellXz);
        g_cy = int(y / params.cellY);
        g_cz = int(z / params.cellXz);
        g_fx = int(x % params.cellXz);
        g_fy = int(y % params.cellY);
        g_fz = int(z % params.cellXz);
        int2 origin = chunks[chunk] * 16;
        int3 p = int3(origin.x + int(x), kMinY + int(y), origin.y + int(z));
        evalNodes(p, tables[H_ORDER] + params.orderBegin, params.orderCount);
        density[t] = g_vals[params.root];
    }
    else if (params.phase == 2)
    {
        uint total = params.chunkCount * 256;
        if (t >= total)
            return;
        uint chunk = t / 256;
        uint col = t % 256;
        int x = int(col / 16);
        int z = int(col % 16);
        // the ground: the highest block with positive density
        int top = -1;
        uint base = t * kHeight;
        for (int y = int(kHeight) - 1; y >= 0; y--)
        {
            if (density[base + uint(y)] > 0.0f)
            {
                top = y;
                break;
            }
        }
        int2 origin = chunks[chunk] * 16;
        int worldTop = kMinY + (top < 0 ? kSeaLevel - kMinY : top);
        // the biome is read at the column's quart cell, at the height of the ground
        int3 site = int3(((origin.x + x) >> 2) << 2, (worldTop >> 2) << 2, ((origin.y + z) >> 2) << 2);
        uint climate = tables[H_CLIMATE];
        evalNodes(site, climate + 1, tables[climate]);
        for (uint i = 0; i < 6; i++)
            g_target[i] = quantizeClimate(g_vals[tables[climate + 1 + tables[climate] + i]]);
        uint biome = findBiome();
        corners[params.columnBase + t * 2] = asuint(top);
        corners[params.columnBase + t * 2 + 1] = biome;
    }
    else if (params.phase == 4)
    {
        uint total = params.chunkCount * 16 * params.quartLayers;
        if (t >= total)
            return;
        uint qy = t % params.quartLayers;
        uint q = t / params.quartLayers;
        uint column = q % 16;
        uint chunk = q / 16;
        int2 origin = chunks[chunk] * 16;
        int3 site = int3(origin.x + int(column / 4) * 4, (params.quartMinY + int(qy)) * 4, origin.y + int(column % 4) * 4);
        uint climate = tables[H_CLIMATE];
        evalNodes(site, climate + 1, tables[climate]);
        uint at = params.climateBase + t * 6;
        for (uint i = 0; i < 6; i++)
            corners[at + i] = asuint(quantizeClimate(g_vals[tables[climate + 1 + tables[climate] + i]]));
    }
    else
    {
        uint total = params.chunkCount * 256 * kHeight;
        if (t >= total)
            return;
        uint y = t % kHeight;
        uint column = t / kHeight;
        int top = asint(corners[params.columnBase + column * 2]);
        uint biome = corners[params.columnBase + column * 2 + 1];
        bool solid = density[t] > 0.0f;
        density[t] = asfloat(blockAt(biome, int(y), top, solid));
    }
}
