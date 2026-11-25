// Shared copied sun-shaft tuning. Client rendering only.
#pragma once
namespace RenderPost {
enum { SunShaftWaterAreas = 8 };
struct SunShaftSettings
{
	float Colour[3];				// the light the band gives back, the sun's colour (scaled so no channel passes one)
	float Strength;					// the whole effect's weight; zero is no pass
	float Ground;					// the world height the layer's floor and its density are measured from
	float Top;						// how far above Ground the layer reaches
	float Falloff;					// the height over which the density falls to a third
	float Samples;					// steps through the layer, and taps out to Radius on each side of a surface
	float Scale;					// what a world unit of dust at full density weighs (one over a full ray's)
	float Band;						// the light a sunlit gap between two shadows gives back, of the sun's colour
	float Dark;						// the light shaded dust takes off what is seen through it
	float Slack;					// world units nearer the sun a neighbour's caster must stand to count as shade
	float Side;						// the share of the band a sunlit border of one shadow only gives back
	float Radius;					// world units across the bearing a shadow still lights a surface by it
	float StreakWidth;				// world units across the bearing a streak's noise cell spans; nought, a flat half (their mean)
	float StreakLength;				// world units along the bearing it spans
	float DarkCap;					// the most the dimming takes, as a share of what the receivers' shadow takes
	float ShadedMost;				// the most shaded dust a ray counts, in full rays
	float Floor;					// the world height the layer starts at, the map's lowest ground
	float NormalOffset;				// world units off a surface along its normal its own sun is read at
	float TowardSun[2];				// the sun's bearing on the ground, of length one
	float Glare;					// the glare toward that bearing at a shallow ray
	float ShallowTop;				// how steep a ray (its share of straight down) still takes any glare
	float ShallowSpan;				// how much steeper than full glare that is
	// The water areas the layer stops at: a ray that ends under one of these surfaces inside its box
	// (min x, min y, max x, max y) ends on the surface, since water writes no depth.
	float WaterCount;
	float WaterBoxes[SunShaftWaterAreas][4];
	float WaterHeights[SunShaftWaterAreas];
};
}
