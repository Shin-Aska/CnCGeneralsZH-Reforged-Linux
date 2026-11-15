#pragma once

#include <cmath>

namespace FireWaterEffects {

// Client-only crossing history. The first observation establishes the current
// state, so a loaded flooded map never bursts into steam. A small dry-side
// hysteresis keeps waves hovering at the source from repeatedly rearming it.
struct Contact
{
	bool initialized = false, wet = false;
	unsigned seen = 0;

	bool observe(bool hasWater, float emitterZ, float waterZ, unsigned frame)
	{
		if (!std::isfinite(emitterZ) || (hasWater && !std::isfinite(waterZ)))
			return false;
		const bool submerged = hasWater && waterZ > emitterZ + 0.1f;
		if (!initialized || frame < seen)
		{
			initialized = true;
			wet = submerged;
			seen = frame;
			return false;
		}
		const bool crossing = submerged && !wet;
		if (submerged) wet = true;
		else if (!hasWater || waterZ < emitterZ - 0.5f) wet = false;
		seen = frame;
		return crossing;
	}
};

// A convex billboard cut by one plane has at most five vertices. Keep the
// existing quad sorting path: a pentagon is one quad and a degenerate quad
// holding its final triangle. The caller supplies interpolation for its vertex
// payload, preserving texture coordinates, colour, glow and fire age/seed.
// quad has room for eight vertices; plane is in the same space as its positions.
template<class Vertex, class Blend>
int clipQuad(Vertex *quad, const float plane[4], Blend blend)
{
	float distance[4];
	int above = 0;
	for (int i = 0; i < 4; ++i)
	{
		distance[i] = plane[0] * quad[i].x + plane[1] * quad[i].y
			+ plane[2] * quad[i].z + plane[3];
		if (!std::isfinite(distance[i])) return 1;
		if (distance[i] >= 0.0f) ++above;
	}
	if (above == 4) return 1;
	if (above == 0) return 0;
	Vertex original[4] = {quad[0], quad[1], quad[2], quad[3]};
	Vertex polygon[5];
	int count = 0;
	for (int i = 0, previous = 3; i < 4; previous = i++)
	{
		const float a = distance[previous], b = distance[i];
		// A corner already on the plane is emitted once, avoiding duplicate
		// intersections and zero-area triangles at exact waterline contact.
		if ((a < 0.0f && b > 0.0f) || (a > 0.0f && b < 0.0f))
			polygon[count++] = blend(original[previous], original[i], a / (a - b));
		if (b >= 0.0f) polygon[count++] = original[i];
	}
	if (count < 3) return 0;
	for (int i = 0; i < 4; ++i) quad[i] = polygon[i < count ? i : count - 1];
	if (count < 5) return 1;
	quad[4] = polygon[0];
	quad[5] = polygon[3];
	quad[6] = quad[7] = polygon[4];
	return 2;
}

} // namespace FireWaterEffects
