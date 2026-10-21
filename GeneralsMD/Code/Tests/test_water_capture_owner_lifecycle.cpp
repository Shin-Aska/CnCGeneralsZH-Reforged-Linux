// UNREGISTERED parser-only PIN/RED suite. No live lifecycle outcome is asserted here.
#include "test_harness.h"
#include "Platform/WaterCaptureFixture.h"
#include "GameLogic/ScenarioDrill.h"

TEST(water_capture_owner_lifecycle_pin_existing_grammar)
{
    ScenarioAction action;
    CHECK_EQ((Int)ScenarioDrill_parseLine("120 watercapturemove player:PlyrCivilian WaterFixtureLow 1060 425", &action), (Int)SCENARIO_PARSE_OK);
    CHECK_EQ(action.action, SCENARIO_ACTION_MOVE);
    CHECK_STR(action.captureActorName.str(), "WaterFixtureLow");
    CHECK_STR(action.capturePlayerName.str(), "PlyrCivilian");
    CHECK_EQ(action.at.x, 1060.0f);
    CHECK_EQ((Int)ScenarioDrill_parseLine("181 watercapturestop player:PlyrCivilian WaterFixtureHigh", &action), (Int)SCENARIO_PARSE_OK);
    CHECK_EQ(action.action, SCENARIO_ACTION_STOP);
    CHECK_EQ((Int)ScenarioDrill_parseLine("0 spawn player:ThePlayer WaterCaptureCarrier 1 2500 1000", &action), (Int)SCENARIO_PARSE_OK);
    CHECK_EQ(action.count, 1);
    CHECK(action.captureActorName.isEmpty());
    CHECK_EQ((Int)ScenarioDrill_parseLine("0 spawn player:ThePlayer WaterCaptureCarrier 2 0 0", &action), (Int)SCENARIO_PARSE_BAD_COUNT);
    CHECK_EQ((Int)ScenarioDrill_parseLine("0 move civilian * 10 20", &action), (Int)SCENARIO_PARSE_BAD_SLOT);
    CHECK_EQ((Int)ScenarioDrill_parseLine("120 move 2 * start1:20:-30", &action), (Int)SCENARIO_PARSE_OK);
    CHECK_EQ(action.slot, 2);
    CHECK_EQ(action.atStart, 1);
    CHECK_EQ(action.at.y, -30.0f);
    CHECK(action.capturePlayerName.isEmpty());
    CHECK(action.captureActorName.isEmpty());
}

TEST(water_capture_owner_lifecycle_pin_default_off_gate)
{
    // Existing pure policy only: these checks do not prove runtime dispatch consults it.
    CHECK(!WaterCaptureFixture::Enabled(NULL, 45000, true, false, false));
    CHECK(!WaterCaptureFixture::Enabled("off", 45000, true, false, false));
    CHECK(!WaterCaptureFixture::Enabled("carrier", 0, true, false, false));
    CHECK(!WaterCaptureFixture::Enabled("carrier", 45000, false, false, false));
    CHECK(!WaterCaptureFixture::Enabled("carrier", 45000, true, true, false));
    CHECK(!WaterCaptureFixture::Enabled("carrier", 45000, true, false, true));
    CHECK(WaterCaptureFixture::Enabled("carrier", 45000, true, false, false));
}

TEST(water_capture_owner_lifecycle_red_accepts_targeted_operations)
{
    const char *lines[] = {
        "200 watercaptureteleport player:PlyrCivilian WaterFixtureLow 1300 425",
        "201 watercapturehidden player:PlyrCivilian WaterFixtureLow 1",
        "202 watercapturehidden player:PlyrCivilian WaterFixtureLow 0",
        "203 watercaptureshrouded player:PlyrCivilian WaterFixtureLow 1",
        "204 watercaptureshrouded player:PlyrCivilian WaterFixtureLow 0",
        "205 watercapturedestroy player:PlyrCivilian WaterFixtureLow",
        "210 watercapturespawn player:PlyrCivilian WaterFixtureLow 900 425",
    };
    for (const char *line : lines) {
        ScenarioAction action;
        const ScenarioParseResult result = ScenarioDrill_parseLine(line, &action);
        CHECK_EQ((Int)result, (Int)SCENARIO_PARSE_OK);
        // Current parser returns BAD_ACTION; never read its uninitialized scalar fields.
        if (result != SCENARIO_PARSE_OK) continue;
        CHECK_STR(action.captureActorName.str(), "WaterFixtureLow");
        CHECK_STR(action.capturePlayerName.str(), "PlyrCivilian");
        CHECK_STR(action.selector.str(), "WaterCaptureCarrier");
        CHECK_EQ(action.count, 1);
        CHECK_EQ(action.atStart, SCENARIO_NO_START);
    }
    char name[64];
    memset(name, 'a', sizeof(name)-1);
    name[sizeof(name)-1] = 0;
    char boundary[160];
    snprintf(boundary, sizeof(boundary), "0 watercapturedestroy player:PlyrCivilian %s", name);
    ScenarioAction action;
    CHECK_EQ((Int)ScenarioDrill_parseLine(boundary, &action), (Int)SCENARIO_PARSE_OK);
    CHECK_EQ((Int)ScenarioDrill_parseLine("0 watercapturedestroy player:PlyrCivilian a", &action), (Int)SCENARIO_PARSE_OK);
    const ScenarioParseResult result = ScenarioDrill_parseLine(lines[0], &action);
    if (result == SCENARIO_PARSE_OK) {
        CHECK_EQ(action.at.x, 1300.0f);
        CHECK_EQ(action.at.y, 425.0f);
    }
}

TEST(water_capture_owner_lifecycle_red_rejects_malformed_boundary)
{
    struct BadLine { const char *line; ScenarioParseResult result; };
    const BadLine bad[] = {
        {"0 watercapturedestroy 0 WaterFixtureLow", SCENARIO_PARSE_BAD_SLOT},
        {"0 watercapturedestroy player:ThePlayer WaterFixtureLow", SCENARIO_PARSE_BAD_SLOT},
        {"0 watercapturedestroy player:PlyrCivilian *", SCENARIO_PARSE_BAD_SLOT},
        {"0 watercapturedestroy player:PlyrCivilian Low*", SCENARIO_PARSE_BAD_SLOT},
        {"0 watercapturedestroy player:PlyrCivilian object:3", SCENARIO_PARSE_BAD_SLOT},
        {"0 watercapturedestroy player:PlyrCivilian Low\xC3\xA9", SCENARIO_PARSE_BAD_SLOT},
        {"0 watercaptureteleport player:PlyrCivilian Low nan 425", SCENARIO_PARSE_BAD_POSITION},
        {"0 watercaptureteleport player:PlyrCivilian Low 10 inf", SCENARIO_PARSE_BAD_POSITION},
        {"0 watercaptureteleport player:PlyrCivilian Low 10junk 20", SCENARIO_PARSE_BAD_POSITION},
        {"0 watercaptureteleport player:PlyrCivilian Low start0 20", SCENARIO_PARSE_BAD_POSITION},
        {"0 watercapturespawn player:PlyrCivilian Low 1e999 20", SCENARIO_PARSE_BAD_POSITION},
        {"0 watercapturespawn player:PlyrCivilian Low 1e12 20", SCENARIO_PARSE_BAD_POSITION},
        {"0 watercapturehidden player:PlyrCivilian Low true", SCENARIO_PARSE_BAD_ACTION},
        {"0 watercapturehidden player:PlyrCivilian Low 2", SCENARIO_PARSE_BAD_ACTION},
        {"0 watercaptureshrouded player:PlyrCivilian Low -1", SCENARIO_PARSE_BAD_ACTION},
        {"0 watercaptureshrouded player:PlyrCivilian Low 01", SCENARIO_PARSE_BAD_ACTION},
        {"0 watercaptureteleport player:PlyrCivilian Low 10", SCENARIO_PARSE_MISSING_ARGS},
        {"0 watercapturespawn player:PlyrCivilian Low 10 20 2", SCENARIO_PARSE_MISSING_ARGS},
        {"0 watercapturedestroy player:PlyrCivilian Low extra", SCENARIO_PARSE_MISSING_ARGS},
        {"0 watercapturehidden player:PlyrCivilian Low", SCENARIO_PARSE_MISSING_ARGS},
    };
    for (const BadLine &entry : bad) {
        ScenarioAction action;
        CHECK_EQ((Int)ScenarioDrill_parseLine(entry.line, &action), (Int)entry.result);
    }
    char line[160];
    char longName[65];
    memset(longName, 'a', sizeof(longName)-1);
    longName[sizeof(longName)-1] = 0;
    snprintf(line, sizeof(line), "0 watercapturedestroy player:PlyrCivilian %s", longName);
    ScenarioAction action;
    CHECK_EQ((Int)ScenarioDrill_parseLine(line, &action), (Int)SCENARIO_PARSE_BAD_SLOT);
}
