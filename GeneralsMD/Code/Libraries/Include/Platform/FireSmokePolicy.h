#pragma once

#include <cmath>

// Known combustion plumes only. Smoke textures are also used by dust, toxins,
// exhaust and water spray; neither their texture nor a broad "Smoke" name is enough.
namespace FireSmokePolicy {

static const char SteamTemplate[] = "ReforgedFireExtinguishSteam";

inline bool sameName(const char *a, const char *b)
{
	if (!a || !b) return false;
	while (*a && *b)
	{
		const char ca = *a++, cb = *b++;
		if ((ca >= 'A' && ca <= 'Z' ? ca + ('a' - 'A') : ca)
				!= (cb >= 'A' && cb <= 'Z' ? cb + ('a' - 'A') : cb))
			return false;
	}
	return *a == *b;
}

inline bool knownSource(const char *name)
{
	static const char *const names[] = {
		"SmokeFactionSmall", "SmokeFactionMedium", "SmokeFactionLarge",
		"SmokeBuildingSmall", "SmokeTreeSmall",
		"SmokeBurningBarricade01", "SmokeBurningBarricade02",
		"SmolderingSmoke", "SmolderingSmokeLarge", "SmolderingSmokeHuge", "SmolderingSmokeTower",
		"AutoSmokeSmall01", "AutoSmokeLarge01", "FirestormSmallSmoke",
		"ReforgedTankSmoke", "ReforgedStructureSmoke"
	};
	for (const char *source : names)
		if (sameName(name, source)) return true;
	return false;
}

inline bool submerged(bool hasWater, float emitterZ, float waterZ)
{
	return hasWater && std::isfinite(emitterZ) && std::isfinite(waterZ)
		&& waterZ > emitterZ + 0.1f;
}

} // namespace FireSmokePolicy
