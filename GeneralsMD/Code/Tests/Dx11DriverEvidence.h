#pragma once

#include "test_harness.h"

#include "dx11device.h"

#include <cstdio>
#include <cstdlib>
#include <string.h>

static void verify_dx11_test_driver(const DX11DeviceClass & device)
{
	const char * expected = getenv("ZH_TEST_DX11_EXPECT_DRIVER");
	if (expected != NULL && _stricmp(expected, "warp") == 0) CHECK(device.Is_Software());
	if (expected != NULL && _stricmp(expected, "hardware") == 0) CHECK(!device.Is_Software());
	std::printf("  adapter=%s debug=%u feature=0x%x\n", device.Is_Software() ? "warp" : "hardware",
		device.Has_Debug_Layer() ? 1u : 0u, static_cast<unsigned>(device.Get_Feature_Level()));
}
