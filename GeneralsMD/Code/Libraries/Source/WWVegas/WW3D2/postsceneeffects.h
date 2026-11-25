// Exact existing DX11 sun-shaft and soft-shroud programs shared by native backends.
#pragma once
#include "postshader.h"
struct PostSceneEffectConstants : PostProcessConstants {
    float SunFromWorld[16];
    float Shafts[4], ShaftSun[4], ShaftLook[4], ShaftGlare[4], ShaftColour[4];
    float ShaftWater[8][4], ShaftWaterHeight[8][4];
    float ShaftLimit[4], ShaftBand[4], ShaftTerrain[4], WorldFromSun[16];
};
static_assert(sizeof(PostSceneEffectConstants)==54*16,"scene effect c0..c53 ABI");
// The shroud's three programs (Soften_Shroud, Shroud_Memory_Scene).  First the easing, a texel a
// cell: Source is the terrain's shroud texture as logic last wrote it, Extra the level each cell
// showed last frame, and each channel moves toward the texture's by BlurDirection.x of the gap
// (one less the decay over the frame's seconds) but never by less than BlurDirection.y (the frame's
// share of a whole swing), so a change runs out in a few tenths of a second whatever the frame rate
// and lands exactly.  Sixteen bits a channel, so a slow frame rate's small steps are not lost.
static const char * const SHROUD_EASE_SHADER_BODY =
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    int3 texel = int3(input.Position.xy, 0);\n"
	"    float4 wanted = Source.Load(texel);\n"
	"    float4 held = Extra.Load(texel);\n"
	"    float4 gap = wanted - held;\n"
	"    float4 step = min(abs(gap), max(abs(gap) * BlurDirection.x, BlurDirection.y));\n"
	"    return held + sign(gap) * step;\n"
	"}\n";

// Then the eased cells up to BlurDirection.x texels a cell through a cubic B-spline over the
// sixteen round each texel, which is about a gaussian of 0.58 cells: the fog's edge is a round ramp
// with no step in its slope, where the texture's own bilinear filter drew a straight one cell ramp
// that turned a corner at every cell and showed the grid as a staircase.  The spline is centred, as
// the bilinear ramp was, so the edge sits where the logic puts it.  It never overshoots, so clear
// ground stays at one and shrouded at nought.
static const char * const SHROUD_SOFTEN_SHADER_BODY =
	"float4 spline(float f)\n"
	"{\n"
	"    float g = 1.0 - f;\n"
	"    float f2 = f * f;\n"
	"    float f3 = f2 * f;\n"
	"    return float4(g * g * g, 3.0 * f3 - 6.0 * f2 + 4.0, -3.0 * f3 + 3.0 * f2 + 3.0 * f + 1.0, f3) / 6.0;\n"
	"}\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float2 at = input.Position.xy / BlurDirection.x - 0.5;\n"
	"    float2 base = floor(at);\n"
	"    float4 across = spline(at.x - base.x);\n"
	"    float4 down = spline(at.y - base.y);\n"
	"    int2 last = int2(TexelSize.zw) - 1;\n"
	"    float4 sum = 0.0;\n"
	"    for (int y = 0; y < 4; ++y)\n"
	"    {\n"
	"        float4 row = 0.0;\n"
	"        for (int x = 0; x < 4; ++x)\n"
	"            row += across[x] * Source.Load(int3(clamp(int2(base) + int2(x - 1, y - 1), int2(0, 0), last), 0));\n"
	"        sum += down[y] * row;\n"
	"    }\n"
	"    return saturate(sum);\n"
	"}\n";

// Last the memory, blended over the scene target by its alpha at the haze's moment, before the haze:
// ground and the ghosts of buildings in the fog are what the player last saw, and they read as that
// when most of their colour has gone and they sit a little darker and cooler than the shroud's
// multiply alone leaves them.  How far into the fog a pixel lies is its light (t2, the soft picture
// the terrain multiplied by, so the two edges are one) between clear and the fog's level
// (BlurDirection.x); under the shroud it is black already and stays so.  Source is a copy of the
// scene, Extra the depth.  Sky and anything in sight take nothing, so their samples are untouched.
static const char * const SHROUD_MEMORY_SHADER_BODY =
	"cbuffer MemoryConstants : register(b1)\n"
	"{\n"
	"    row_major float4x4 WorldFromClip : packoffset(c15);\n"
	"    float4 HazeSun : packoffset(c19);\n"
	"    float4 HazeShroudRect : packoffset(c20);\n"
	"};\n"
	"Texture2D Shroud : register(t2);\n"
	"static const float MEMORY_GREY = 0.7;\n"
	"static const float MEMORY_DIM = 0.88;\n"
	"static const float3 MEMORY_TINT = float3(0.94, 0.98, 1.06);\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float raw = Extra.Load(int3(input.Position.xy, 0)).r;\n"
	"    if (raw >= 0.99999 || HazeSun.z <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);\n"
	"    float2 ndc = float2(input.Texture.x * 2.0 - 1.0, 1.0 - input.Texture.y * 2.0);\n"
	"    float4 world = mul(float4(ndc, raw, 1.0), WorldFromClip);\n"
	"    float2 here = world.xy / world.w;\n"
	// The shroud texel is its light times the map's shroud colour: the brightest channel is the light
	// whatever the colour, and the colour over it is put back on the recalled grey, so a tinted fog
	// (the Apocalypse map's red) keeps its hue.  A white shroud reads as it always did.
	"    float3 shroud = Shroud.SampleLevel(Sampler, (here - HazeShroudRect.xy) * HazeShroudRect.zw, 0.0).rgb;\n"
	"    float lit = max(shroud.r, max(shroud.g, shroud.b));\n"
	"    float memory = saturate((1.0 - lit) / max(1.0 - BlurDirection.x, 0.0001));\n"
	"    if (memory <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);\n"
	"    float3 scene = Source.Load(int3(input.Position.xy, 0)).rgb;\n"
	"    float grey = dot(scene, float3(0.299, 0.587, 0.114));\n"
	// the R5G6B5 shroud keeps a white fog's red and blue a step under its green: a hue that close to
	// white is white, so an untinted shroud draws exactly as before
	"    float3 hue = shroud / max(lit, 0.0001);\n"
	"    if (min(hue.r, min(hue.g, hue.b)) > 0.97) hue = float3(1.0, 1.0, 1.0);\n"
	"    float3 recalled = lerp(scene, grey * MEMORY_TINT, MEMORY_GREY) * MEMORY_DIM * hue;\n"
	"    return float4(recalled, memory);\n"
	"}\n";

// The SunShafts option's three programs.  Their constants sit past the occlusion's (c22 on), which no
// other program declares, and the helpers all three share follow them: the eye distance of a depth, the
// world point of a depth, the shroud's light over a world point, whether a point lies under one of the
// water areas' surfaces, the eye distance a half size pixel stands for (full size depth texel 2i+1, as
// the march reads it), how much sun reaches a point through the sun's map, the terrain's height under a
// point, and how much a surface is the ground.
//
// The ground: a surface facing up (its normal's height past SHAFT_GROUND_TILT) and, when the terrain's
// heights are bound (ShaftTerrain.w), standing within SHAFT_GROUND_RISE world units of them.  Only the
// ground takes the lift: a unit, a roof or a wall standing in a sunlit lane does not, so no body in the
// light is veiled.  Without the heights a flat roof or a tank's deck still counts as ground.
#define SUN_SHAFT_CONSTANTS \
	"cbuffer SunShaftConstants : register(b1)\n" \
	"{\n" \
	"    row_major float4x4 WorldFromClip : packoffset(c15);\n" \
	"    float4 HazeSun : packoffset(c19);\n" \
	"    float4 HazeShroudRect : packoffset(c20);\n" \
	"    float4 WorldAoProjection : packoffset(c21);\n" \
	"    row_major float4x4 SunFromWorld : packoffset(c22);\n" \
	"    float4 Shafts : packoffset(c26);\n" \
	"    float4 ShaftSun : packoffset(c27);\n" \
	"    float4 ShaftLook : packoffset(c28);\n" \
	"    float4 ShaftGlare : packoffset(c29);\n" \
	"    float4 ShaftColour : packoffset(c30);\n" \
	"    float4 ShaftWater[8] : packoffset(c31);\n" \
	"    float4 ShaftWaterHeight[8] : packoffset(c39);\n" \
	"    float4 ShaftLimit : packoffset(c47);\n" \
	"    float4 ShaftBand : packoffset(c48);\n" \
	"    float4 ShaftTerrain : packoffset(c49);\n" \
	"    row_major float4x4 WorldFromSun : packoffset(c50);\n" \
	"};\n" \
	"Texture2D ShaftShroud : register(t2);\n" \
	"Texture2D SunMap : register(t3);\n" \
	"Texture2D ShaftHeights : register(t4);\n" \
	"static const float SHAFT_BOX_EDGE = 0.04;\n" \
	"static const float SHAFT_GROUND_TILT_LOW = 0.6;\n" \
	"static const float SHAFT_GROUND_TILT_HIGH = 0.8;\n" \
	"static const float SHAFT_GROUND_RISE_LOW = 2.0;\n" \
	"static const float SHAFT_GROUND_RISE_HIGH = 4.0;\n" \
	"\n" \
	"float shaft_eye_distance(float raw)\n" \
	"{\n" \
	"    return 1.0 / (raw * WorldFromClip[2][3] + WorldFromClip[3][3]);\n" \
	"}\n" \
	"\n" \
	"float3 shaft_world(float2 ndc, float raw)\n" \
	"{\n" \
	"    float4 world = mul(float4(ndc, raw, 1.0), WorldFromClip);\n" \
	"    return world.xyz / world.w;\n" \
	"}\n" \
	"\n" \
	"float shaft_light(float2 at)\n" \
	"{\n" \
	"    float3 texel = ShaftShroud.SampleLevel(Sampler, (at - HazeShroudRect.xy) * HazeShroudRect.zw, 0.0).rgb;\n" \
	"    return lerp(1.0, max(texel.r, max(texel.g, texel.b)), HazeSun.z);\n" \
	"}\n" \
	"\n" \
	"bool shaft_under_water(float3 end)\n" \
	"{\n" \
	"    int areas = (int)ShaftGlare.z;\n" \
	"    [loop]\n" \
	"    for (int area = 0; area < areas; ++area)\n" \
	"    {\n" \
	"        float4 box = ShaftWater[area];\n" \
	"        if (end.x >= box.x && end.y >= box.y && end.x <= box.z && end.y <= box.w && end.z < ShaftWaterHeight[area].x)\n" \
	"            return true;\n" \
	"    }\n" \
	"    return false;\n" \
	"}\n" \
	"\n" \
	"float shaft_half_distance(int2 half_texel)\n" \
	"{\n" \
	"    uint width;\n" \
	"    uint height;\n" \
	"    Extra.GetDimensions(width, height);\n" \
	"    int2 texel = min(half_texel * 2 + 1, int2(width, height) - 1);\n" \
	"    return shaft_eye_distance(Extra.Load(int3(texel, 0)).r);\n" \
	"}\n" \
	"\n" \
	"float shaft_sunlit(float3 at, float2 size, float2 nudge, out float inside)\n" \
	"{\n" \
	"    inside = 0.0;\n" \
	"    float4 sun = mul(float4(at, 1.0), SunFromWorld);\n" \
	"    if (sun.w <= 0.0) return 1.0;\n" \
	"    sun.xyz /= sun.w;\n" \
	"    float2 map = float2(0.5 * sun.x + 0.5, 0.5 - 0.5 * sun.y);\n" \
	"    float2 edge = min(map, 1.0 - map);\n" \
	"    inside = saturate(min(edge.x, edge.y) / SHAFT_BOX_EDGE);\n" \
	"    if (inside <= 0.0 || sun.z < 0.0 || sun.z > 1.0) { inside = 0.0; return 1.0; }\n" \
	"    float2 spot = map * size - 0.5 + nudge;\n" \
	"    int2 base = clamp(int2(floor(spot)), int2(0, 0), int2(size) - 2);\n" \
	"    float2 part = saturate(spot - base);\n" \
	"    float bias = sun.z - ShaftSun.w;\n" \
	"    float a = (bias <= SunMap.Load(int3(base, 0)).r) ? 1.0 : 0.0;\n" \
	"    float b = (bias <= SunMap.Load(int3(base + int2(1, 0), 0)).r) ? 1.0 : 0.0;\n" \
	"    float c = (bias <= SunMap.Load(int3(base + int2(0, 1), 0)).r) ? 1.0 : 0.0;\n" \
	"    float d = (bias <= SunMap.Load(int3(base + int2(1, 1), 0)).r) ? 1.0 : 0.0;\n" \
	"    return lerp(lerp(a, b, part.x), lerp(c, d, part.x), part.y);\n" \
	"}\n" \
	"\n" \
	"float shaft_ground(float2 at)\n" \
	"{\n" \
	"    uint width;\n" \
	"    uint height;\n" \
	"    ShaftHeights.GetDimensions(width, height);\n" \
	"    float2 grid = at * ShaftTerrain.x + ShaftTerrain.y;\n" \
	"    return ShaftHeights.SampleLevel(Sampler, (grid + 0.5) / float2(width, height), 0.0).r * 255.0 * ShaftTerrain.z;\n" \
	"}\n" \
	"\n" \
	"float shaft_grounded(float3 at, float3 facing)\n" \
	"{\n" \
	"    float grounded = smoothstep(SHAFT_GROUND_TILT_LOW, SHAFT_GROUND_TILT_HIGH, facing.z);\n" \
	"    if (ShaftTerrain.w > 0.0)\n" \
	"        grounded *= 1.0 - smoothstep(SHAFT_GROUND_RISE_LOW, SHAFT_GROUND_RISE_HIGH, at.z - shaft_ground(at.xy));\n" \
	"    return grounded;\n" \
	"}\n" \
	"\n"

// The march, half size into the occlusion's first target: R the lift, G the dimming, two amounts that
// travel apart through the blur and the composite, so neither can cancel the other.  Each half size pixel
// takes full size depth texel 2i+1 back to the world as the occlusion does.
//
// The lift is decided on the surface the pixel shows, in the sun's map's own space.  A surface in the
// sun's shadow, turned from the sun, outside the map's box, under water or over fog and shroud, or not the
// ground (shaft_grounded), takes none at all, and says so with R below nought (SHAFT_NEVER), which the
// blur keeps out of its neighbours' sums; the composite decides the same gate again per full size pixel.
// A sunlit patch of ground looks along the line across the sun's ground bearing (ShaftSun.xy turned a
// quarter), both ways, for the nearest shade at its own height in the sun's map: half of Shafts.w taps
// out to ShaftBand.x world units, then a bisection of the bracket the first shaded tap closes, so the
// distance moves smoothly as the camera and the map's box move and the band has no contour a tap wide.
// Shade here is a neighbour whose stored depth stands nearer the sun than the surface by more than the
// bias and ShaftLook.w world units and, with the terrain's heights, whose caster stands that far over the
// ground (shaft_shade), so rising ground across the bearing is no caster.  Each side's nearness is one less a smoothstep of
// its distance over the radius, so the band is whole at the shadow's edge and reaches nought at the
// radius with no slope: shade close on both sides, a gap between two casters, gives the nearer of the two
// whole (so a narrow gap glows down its middle), shade on one side only ShaftGlare.w of its square, and no
// shade within the radius exactly nought, open ground untouched to the bit.  A soft value noise in world
// space, ShaftBand.y across the bearing by ShaftBand.z along it, breaks the band into streaks pointing
// along the sun's way (shaft_streak); no clock moves it, so a capture is the frame a player saw.
// ShaftLook.y is its most, of the sun's colour.
//
// The dimming walks the view ray through the layer, from the map's lowest ground (ShaftLimit.z) to
// Shafts.x + Shafts.y, one step on each of Shafts.w fixed heights (with a fixed offset a pixel, the
// occlusion's interleaved gradient noise, and no clock), so a step slides along the ground as the camera
// moves instead of jumping with the stretch it was a share of.  The dust is at its thickest up to the
// median ground (Shafts.x) and thins above it.  A step reads the sun's map through a two by two filter;
// one near the box's edge fades out of the sum.  The shaded dust (ShaftLook.z, held under ShaftLimit.y
// rays, each step weighing its density, exp(-h / Shafts.z), its stretch of ray and the shroud's light
// cubed there, over a full ray's dust ShaftLook.x) is taken off a sunlit surface seen through it; a
// surface in shadow, or one turned from the sun, took its shade already.  The surface's own sun is read
// a little off it along its normal (ShaftLimit.w), as the receivers move their lookup, so a slope does not
// shade itself in speckles.  Both amounts take the shroud's light at the ray's end, cubed.
static const char * const SUN_SHAFTS_SHADER_BODY =
	SUN_SHAFT_CONSTANTS
	"static const int SHAFT_MOST_STEPS = 32;\n"
	"static const int SHAFT_REFINE = 5;\n"
	"static const float SHAFT_NEVER = -1.0;\n"
	"static const float SHAFT_STREAK_FLOOR = 0.2;\n"
	"static const float SHAFT_STREAK_PLAIN = 0.5;\n"
	"static const float SHAFT_BEARING_STEP = 0.08726646;\n"
	"\n"
	"float shaft_shroud(float2 at)\n"
	"{\n"
	"    float lit = shaft_light(at);\n"
	"    return lit * lit * lit;\n"
	"}\n"
	"\n"
	"float3 shaft_world_of(int2 texel)\n"
	"{\n"
	"    float2 ndc = float2((texel.x + 0.5) * TexelSize.x * 2.0 - 1.0, 1.0 - (texel.y + 0.5) * TexelSize.y * 2.0);\n"
	"    return shaft_world(ndc, Extra.Load(int3(texel, 0)).r);\n"
	"}\n"
	"\n"
	"float3 shaft_shorter(float3 a, float3 b)\n"
	"{\n"
	"    return dot(a, a) < dot(b, b) ? a : b;\n"
	"}\n"
	"\n"
	// The share of a two by two of the sun's map round a texel spot whose stored depth stands nearer the sun
	// than limit: the shade there at the surface's height, filtered so it moves smoothly with the spot.  Past
	// the map's edge it says nothing, and counts as lit.  With the terrain's heights bound, the nearest of
	// the four is taken back to the world (WorldFromSun) and counts only by how far that caster stands over
	// the ground under it, nothing below half ShaftLook.w, all of it from ShaftLook.w up: so a valley wall,
	// a ridge or a road cut, which is the terrain itself in the map, is no caster, and neither on purpose
	// is anything lower than the slack (infantry, fences, low walls), whose gaps are too small to read.
	"float shaft_shade(float2 spot, float limit, float2 size)\n"
	"{\n"
	"    if (spot.x < 0.0 || spot.y < 0.0 || spot.x > size.x - 1.0 || spot.y > size.y - 1.0) return 0.0;\n"
	"    int2 base = min(int2(spot), int2(size) - 2);\n"
	"    float2 part = saturate(spot - base);\n"
	"    float4 stored = float4(SunMap.Load(int3(base, 0)).r, SunMap.Load(int3(base + int2(1, 0), 0)).r,\n"
	"        SunMap.Load(int3(base + int2(0, 1), 0)).r, SunMap.Load(int3(base + int2(1, 1), 0)).r);\n"
	"    float4 shade = float4(stored < limit);\n"
	"    float share = lerp(lerp(shade.x, shade.y, part.x), lerp(shade.z, shade.w, part.x), part.y);\n"
	"    if (share > 0.0 && ShaftTerrain.w > 0.0)\n"
	"    {\n"
	"        float nearest = min(min(stored.x, stored.y), min(stored.z, stored.w));\n"
	"        float2 map = (spot + 0.5) / size;\n"
	"        float4 caster = mul(float4(map.x * 2.0 - 1.0, 1.0 - map.y * 2.0, nearest, 1.0), WorldFromSun);\n"
	"        caster.xyz /= caster.w;\n"
	"        float slack = max(ShaftLook.w, 0.001);\n"
	"        share *= smoothstep(0.5 * slack, slack, caster.z - shaft_ground(caster.xy));\n"
	"    }\n"
	"    return share;\n"
	"}\n"
	"\n"
	// World units from a spot to the first shade along a line (per_unit texels a world unit), out to the
	// radius: steps taps, then SHAFT_REFINE halvings of the bracket the first shaded tap closes.  The radius
	// itself when there is none.  A shadow thinner than a tap's stride can fall between two taps.
	"float shaft_edge(float2 spot, float2 per_unit, float limit, float2 size, int steps)\n"
	"{\n"
	"    float reach = ShaftBand.x;\n"
	"    float stride = reach / steps;\n"
	"    float near_d = 0.0;\n"
	"    float far_d = -1.0;\n"
	"    [loop]\n"
	"    for (int k = 1; k <= steps; ++k)\n"
	"    {\n"
	"        float d = stride * k;\n"
	"        if (shaft_shade(spot + per_unit * d, limit, size) >= 0.5) { far_d = d; near_d = d - stride; break; }\n"
	"    }\n"
	"    if (far_d < 0.0) return reach;\n"
	"    [unroll]\n"
	"    for (int i = 0; i < SHAFT_REFINE; ++i)\n"
	"    {\n"
	"        float middle = 0.5 * (near_d + far_d);\n"
	"        if (shaft_shade(spot + per_unit * middle, limit, size) >= 0.5) far_d = middle; else near_d = middle;\n"
	"    }\n"
	"    return 0.5 * (near_d + far_d);\n"
	"}\n"
	"\n"
	"float shaft_hash(int2 cell)\n"
	"{\n"
	"    uint h = ((uint)cell.x * 0x8da6b343u) ^ ((uint)cell.y * 0xd8163841u);\n"
	"    h = (h ^ (h >> 13)) * 0x5bd1e995u;\n"
	"    h ^= h >> 15;\n"
	"    return (float)(h & 0xffffu) / 65535.0;\n"
	"}\n"
	"\n"
	"float shaft_noise(float2 at)\n"
	"{\n"
	"    float2 cell = floor(at);\n"
	"    float2 f = at - cell;\n"
	"    float2 u = f * f * (3.0 - 2.0 * f);\n"
	"    int2 c = int2(cell);\n"
	"    return lerp(lerp(shaft_hash(c), shaft_hash(c + int2(1, 0)), u.x),\n"
	"        lerp(shaft_hash(c + int2(0, 1)), shaft_hash(c + int2(1, 1)), u.x), u.y);\n"
	"}\n"
	"\n"
	// The streaks' noise in a frame turned to a bearing angle about the world's origin.
	"float shaft_streak_noise(float2 at, float angle)\n"
	"{\n"
	"    float2 way = float2(cos(angle), sin(angle));\n"
	"    float2 cell = float2(dot(at, float2(-way.y, way.x)) / ShaftBand.y, dot(at, way) / max(ShaftBand.z, ShaftBand.y));\n"
	"    return shaft_noise(cell);\n"
	"}\n"
	"\n"
	// The streaks at a world point.  Their frame is turned to the sun's bearing in fixed steps of
	// SHAFT_BEARING_STEP (five degrees), faded between the two steps either side of it: a frame turned with
	// the bearing itself swept a point a thousand units out across a cell or two at each half degree the day
	// and night cycle moves the sun, and every streak re-rolled, where now a pattern holds and the next one
	// fades in over the five degrees.  Nought width is a flat SHAFT_STREAK_PLAIN, about their mean, so
	// switching them off does not brighten the band.
	"float shaft_streak(float2 at)\n"
	"{\n"
	"    if (ShaftBand.y <= 0.0) return SHAFT_STREAK_PLAIN;\n"
	"    float steps = atan2(ShaftSun.y, ShaftSun.x) / SHAFT_BEARING_STEP;\n"
	"    float below = floor(steps);\n"
	"    float n = lerp(shaft_streak_noise(at, below * SHAFT_BEARING_STEP),\n"
	"        shaft_streak_noise(at, (below + 1.0) * SHAFT_BEARING_STEP), steps - below);\n"
	"    return SHAFT_STREAK_FLOOR + (1.0 - SHAFT_STREAK_FLOOR) * smoothstep(0.35, 0.7, n);\n"
	"}\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    int2 texel = min(int2(input.Position.xy) * 2 + 1, int2(TexelSize.zw) - 1);\n"
	"    float raw = Extra.Load(int3(texel, 0)).r;\n"
	"    if (raw >= 0.99999) return float4(0.0, 0.0, 0.0, 0.0);\n"
	"    float2 ndc = float2((texel.x + 0.5) * TexelSize.x * 2.0 - 1.0, 1.0 - (texel.y + 0.5) * TexelSize.y * 2.0);\n"
	"    float3 end = shaft_world(ndc, raw);\n"
	"    float3 eye = shaft_world(ndc, 0.0);\n"
	"    float3 ray = end - eye;\n"
	"    float3 look = ray / max(length(ray), 0.0001);\n"
	"    float light_end = shaft_light(end.xy);\n"
	"    float shroud_end = light_end * light_end * light_end;\n"
	// black shroud, and water: its surface draws over this pixel, so neither the bed nor the air over it is
	// what the pixel shows
	"    if (shroud_end <= 0.0 || shaft_under_water(end)) return float4(SHAFT_NEVER, 0.0, 0.0, 0.0);\n"
	"\n"
	"    uint width;\n"
	"    uint height;\n"
	"    SunMap.GetDimensions(width, height);\n"
	"    float2 size = float2(width, height);\n"
	// The surface comes from the depth as the occlusion's does, from whichever neighbour on each axis lies
	// nearer.  Its own sun is read four times a texel apart, a tent about three texels wide like the
	// receivers' soft edge: a two by two read alone stepped the dimming texel by texel along every shadow's
	// edge, a sawtooth fixed on the ground.
	"    float3 across_end = shaft_shorter(end - shaft_world_of(texel - int2(1, 0)), shaft_world_of(texel + int2(1, 0)) - end);\n"
	"    float3 down_end = shaft_shorter(end - shaft_world_of(texel - int2(0, 1)), shaft_world_of(texel + int2(0, 1)) - end);\n"
	"    float3 facing_end = cross(across_end, down_end);\n"
	"    facing_end *= rsqrt(max(dot(facing_end, facing_end), 1.0e-12));\n"
	"    if (dot(facing_end, eye - end) < 0.0) facing_end = -facing_end;\n"
	"    float3 depth_way = float3(SunFromWorld[0][2], SunFromWorld[1][2], SunFromWorld[2][2]);\n"
	"    float depth_unit = length(depth_way);\n"
	"    float3 toward_sun = -depth_way / max(depth_unit, 1.0e-6);\n"
	"    float3 surface = end + facing_end * ShaftLimit.w;\n"
	"    float sun_on = 0.0;\n"
	"    float end_inside = 0.0;\n"
	"    if (dot(facing_end, toward_sun) > 0.1)\n"
	"    {\n"
	"        sun_on = 0.25 * (shaft_sunlit(surface, size, float2(-1.0, -1.0), end_inside)\n"
	"            + shaft_sunlit(surface, size, float2(1.0, -1.0), end_inside)\n"
	"            + shaft_sunlit(surface, size, float2(-1.0, 1.0), end_inside)\n"
	"            + shaft_sunlit(surface, size, float2(1.0, 1.0), end_inside));\n"
	"    }\n"
	// the dimming's view of the surface: past the box's edge it counts lit, as it always did
	"    float seen_lit = (dot(facing_end, toward_sun) > 0.1) ? lerp(1.0, sun_on, end_inside) : 0.0;\n"
	// the lift's: the ground, in full sun, inside the box, under clear sky; anything less takes none
	"    float gate = end_inside * smoothstep(0.75, 1.0, sun_on) * smoothstep(0.9, 1.0, light_end);\n"
	"    if (gate > 0.0) gate *= shaft_grounded(end, facing_end);\n"
	"\n"
	"    float lift = SHAFT_NEVER;\n"
	"    if (gate > 0.0)\n"
	"    {\n"
	"        float2 across = float2(-ShaftSun.y, ShaftSun.x);\n"
	"        float4 sun = mul(float4(surface, 1.0), SunFromWorld);\n"
	"        float4 aside = mul(float4(surface + float3(across, 0.0), 1.0), SunFromWorld);\n"
	"        sun.xyz /= sun.w;\n"
	"        aside.xy /= aside.w;\n"
	"        float2 spot = float2(0.5 * sun.x + 0.5, 0.5 - 0.5 * sun.y) * size - 0.5;\n"
	"        float2 per_unit = float2(0.5 * (aside.x - sun.x), -0.5 * (aside.y - sun.y)) * size;\n"
	"        float limit = sun.z - ShaftSun.w - ShaftLook.w * depth_unit;\n"
	// half the layer's steps a side, at least three: the bisection gives the distance its precision, the taps
	// only have to find the bracket
	"        int steps = clamp((int)(Shafts.w * 0.5), 3, SHAFT_MOST_STEPS);\n"
	"        float near_one = 1.0 - smoothstep(0.0, ShaftBand.x, shaft_edge(spot, per_unit, limit, size, steps));\n"
	"        float near_two = 1.0 - smoothstep(0.0, ShaftBand.x, shaft_edge(spot, -per_unit, limit, size, steps));\n"
	"        float one = max(near_one, near_two);\n"
	"        float edge = max(min(near_one, near_two), ShaftGlare.w * one * one);\n"
	"        lift = (edge > 0.0) ? ShaftLook.y * edge * shaft_streak(end.xy) * gate * shroud_end : 0.0;\n"
	"    }\n"
	"\n"
	"    float shaded = 0.0;\n"
	"    float lowest = max(end.z, ShaftLimit.z);\n"
	"    float highest = min(eye.z, Shafts.x + Shafts.y);\n"
	"    if (seen_lit > 0.0 && highest > lowest && look.z < -0.0001)\n"
	"    {\n"
	"        float start = frac(52.9829189 * frac(dot(input.Position.xy, float2(0.06711056, 0.00583715))));\n"
	"        int steps = min((int)Shafts.w, SHAFT_MOST_STEPS);\n"
	"        float rise = max(Shafts.x + Shafts.y - ShaftLimit.z, 0.0001) / Shafts.w;\n"
	"        float stretch = rise / max(-look.z, 0.1);\n"
	"        [loop]\n"
	"        for (int index = 0; index < steps; ++index)\n"
	"        {\n"
	"            float z = Shafts.x + Shafts.y - ((float)index + start) * rise;\n"
	"            if (z < highest && z > lowest)\n"
	"            {\n"
	"                float3 at = eye + ray * ((z - eye.z) / ray.z);\n"
	"                float inside;\n"
	"                float lit = shaft_sunlit(at, size, float2(0.0, 0.0), inside);\n"
	"                shaded += stretch * exp(-max(z - Shafts.x, 0.0) / Shafts.z) * inside * shaft_shroud(at.xy) * ShaftLook.x * (1.0 - lit);\n"
	"            }\n"
	"        }\n"
	"    }\n"
	"    float dim = ShaftLook.z * min(shaded, ShaftLimit.y) * seen_lit * shroud_end;\n"
	"    return float4(lift, dim, 0.0, 0.0);\n"
	"}\n";

// One axis of the shafts' blur, the occlusion's weights and stop, the stop's distance read from the depth.
// The lift and the dimming are blurred apart.  A neighbour that must never be lifted (R below nought)
// stays out of the lift's sum, and such a pixel stays so itself, so the band does not bleed off a lit
// surface onto the shade, the water or the shroud beside it.
static const char * const SUN_SHAFTS_BLUR_SHADER_BODY =
	SUN_SHAFT_CONSTANTS
	"static const float SHAFT_WEIGHTS[5] = { 0.2, 0.18, 0.14, 0.09, 0.05 };\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    int2 texel = int2(input.Position.xy);\n"
	"    int2 step = int2(BlurDirection.xy);\n"
	"    int2 last = int2(TexelSize.zw) - 1;\n"
	"    float2 centre = Source.Load(int3(texel, 0)).rg;\n"
	"    float centre_distance = shaft_half_distance(texel);\n"
	"    float allowed = 40.0 * centre_distance / (WorldAoProjection.y * TexelSize.w);\n"
	"    float lift = max(centre.r, 0.0) * SHAFT_WEIGHTS[0];\n"
	"    float lift_total = SHAFT_WEIGHTS[0];\n"
	"    float dim = centre.g * SHAFT_WEIGHTS[0];\n"
	"    float dim_total = SHAFT_WEIGHTS[0];\n"
	"    [unroll]\n"
	"    for (int index = 1; index < 5; ++index)\n"
	"    {\n"
	"        [unroll]\n"
	"        for (int side = -1; side <= 1; side += 2)\n"
	"        {\n"
	"            int2 at = clamp(texel + step * index * side, int2(0, 0), last);\n"
	"            float2 tap = Source.Load(int3(at, 0)).rg;\n"
	"            float weight = SHAFT_WEIGHTS[index] * saturate(1.0 - abs(shaft_half_distance(at) - centre_distance) / allowed);\n"
	"            if (tap.r >= 0.0)\n"
	"            {\n"
	"                lift += tap.r * weight;\n"
	"                lift_total += weight;\n"
	"            }\n"
	"            dim += tap.g * weight;\n"
	"            dim_total += weight;\n"
	"        }\n"
	"    }\n"
	"    return float4((centre.r < 0.0) ? centre.r : lift / lift_total, dim / dim_total, 0.0, 0.0);\n"
	"}\n";

// Back to full size over the scene, the occlusion's upsample carrying the two amounts apart: the lift
// is the sun's colour added, the dimming takes that share of the scene away (the blend is ONE,
// INV_SRC_ALPHA), never more than ShaftLimit.x, a share of what the receivers' shadow takes, so a dimmed
// sunlit pixel never ends darker than the shadow beside it.  Nought in both draws the scene as it was.  A
// half size pixel that must never be lifted counts as nought lift with its full weight, and the lift's
// gate (the ground, in full sun, dry, under clear sky) is decided again for the full size pixel, so a
// shaded, wet or shrouded pixel takes exactly none whatever its half size neighbours hold.  Its distance
// stop is the occlusion's own: three times as wide let the lift run over the silhouettes of units and
// houses standing in it, which then read as ghosts.
//
// The glare is worked here, per full size pixel, and does not take the lift's gate: a shallow ray
// looking along the sun's bearing (ShaftSun.xy) takes ShaftSun.z of the colour, eased by the cube of how
// near the bearing it looks, all of it at a ray shallower than ShaftGlare.x - ShaftGlare.y of straight
// down and none past ShaftGlare.x, none over the sky, water or ground the fog of war dims.
//
// The light it adds never passes SHAFT_MOST_LIFT of the sun's colour, whatever the strength, the band and
// the glare say: a hard ceiling under any tuning, about 36 levels at full colour.
static const char * const SUN_SHAFTS_COMPOSITE_SHADER_BODY =
	SUN_SHAFT_CONSTANTS
	"static const float SHAFT_MOST_LIFT = 0.14;\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float raw = Extra.Load(int3(int2(input.Position.xy), 0)).r;\n"
	"    float wanted = shaft_eye_distance(raw);\n"
	"    float allowed = 20.0 * wanted / (WorldAoProjection.y * TexelSize.w);\n"
	"    float2 at = input.Position.xy * 0.5 - 0.75;\n"
	"    int2 last = int2(TexelSize.zw) - 1;\n"
	"    int2 corner = clamp(int2(floor(at)), int2(0, 0), last);\n"
	"    float2 part = saturate(at - corner);\n"
	"    float lift = 0.0;\n"
	"    float dim = 0.0;\n"
	"    float total = 0.0;\n"
	"    for (int y = 0; y < 2; ++y)\n"
	"    {\n"
	"        for (int x = 0; x < 2; ++x)\n"
	"        {\n"
	"            int2 half_texel = min(corner + int2(x, y), last);\n"
	"            float2 tap = Source.Load(int3(half_texel, 0)).rg;\n"
	"            float bilinear = (x == 1 ? part.x : 1.0 - part.x) * (y == 1 ? part.y : 1.0 - part.y);\n"
	"            float weight = bilinear * (0.001 + saturate(1.0 - abs(shaft_half_distance(half_texel) - wanted) / allowed));\n"
	"            lift += max(tap.r, 0.0) * weight;\n"
	"            dim += tap.g * weight;\n"
	"            total += weight;\n"
	"        }\n"
	"    }\n"
	"    lift /= total;\n"
	"    dim /= total;\n"
	"\n"
	// This pixel's own surface, its normal from the screen derivatives of its world point (taken before
	// any branch, where they are defined), for the lift's gate at full size: the half size gate let a pixel
	// on the shaded side of an edge, a shore or the shroud's border take up to half its lit neighbour's lift.
	"    float2 ndc = float2(input.Texture.x * 2.0 - 1.0, 1.0 - input.Texture.y * 2.0);\n"
	"    float3 end = shaft_world(ndc, raw);\n"
	"    float3 eye = shaft_world(ndc, 0.0);\n"
	"    float3 facing = cross(ddx(end), ddy(end));\n"
	"    facing *= rsqrt(max(dot(facing, facing), 1.0e-12));\n"
	"    if (dot(facing, eye - end) < 0.0) facing = -facing;\n"
	"    bool ground = raw < 0.99999;\n"
	"    float light_end = ground ? shaft_light(end.xy) : 0.0;\n"
	"    bool dry = ground && !shaft_under_water(end);\n"
	"    if (lift > 0.0)\n"
	"    {\n"
	"        float full_gate = 0.0;\n"
	"        float3 toward_sun = -float3(SunFromWorld[0][2], SunFromWorld[1][2], SunFromWorld[2][2]);\n"
	"        if (dry && dot(facing, toward_sun) > 0.0)\n"
	"        {\n"
	"            uint width;\n"
	"            uint height;\n"
	"            SunMap.GetDimensions(width, height);\n"
	"            float inside;\n"
	"            float sun_on = shaft_sunlit(end + facing * ShaftLimit.w, float2(width, height), float2(0.0, 0.0), inside);\n"
	"            full_gate = inside * smoothstep(0.75, 1.0, sun_on) * smoothstep(0.9, 1.0, light_end) * shaft_grounded(end, facing);\n"
	"        }\n"
	"        lift *= full_gate;\n"
	"    }\n"
	"\n"
	// The glare is light in the air between the eye and whatever the pixel shows, so it lies over units and
	// walls as well as the ground; it stays under about 8 levels, too faint to veil a body.
	"    float glare = 0.0;\n"
	"    if (dry && ShaftSun.z > 0.0)\n"
	"    {\n"
	"        float3 ray = end - eye;\n"
	"        float3 look = ray / max(length(ray), 0.0001);\n"
	"        float across = dot(look.xy, look.xy);\n"
	"        float toward = (across > 0.000001) ? saturate(dot(look.xy * rsqrt(max(across, 0.000001)), ShaftSun.xy)) : 0.0;\n"
	"        float shallow = saturate((ShaftGlare.x - abs(look.z)) / max(ShaftGlare.y, 0.0001));\n"
	"        glare = ShaftSun.z * toward * toward * toward * shallow * smoothstep(0.9, 1.0, light_end)\n"
	"            * light_end * light_end * light_end;\n"
	"    }\n"
	"    float add = min(ShaftColour.a * (lift + glare), SHAFT_MOST_LIFT);\n"
	"    return float4(ShaftColour.rgb * add, min(saturate(ShaftColour.a * dim), ShaftLimit.x));\n"
	"}\n";


#undef SUN_SHAFT_CONSTANTS
