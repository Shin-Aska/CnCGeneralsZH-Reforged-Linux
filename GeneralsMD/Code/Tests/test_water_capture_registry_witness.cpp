// UNREGISTERED parser-only PIN/RED. Current public APIs only; no live operation.
#include "test_harness.h"
#include "GameLogic/ScenarioDrill.h"
TEST(water_capture_registry_witness_pin_existing_named_orders)
{
    ScenarioAction action;
    CHECK_EQ((Int)ScenarioDrill_parseLine("120 watercapturemove player:PlyrCivilian WaterFixtureLow 1060 425", &action), (Int)SCENARIO_PARSE_OK);
    CHECK_STR(action.captureActorName.str(), "WaterFixtureLow");
    CHECK_EQ(action.action, SCENARIO_ACTION_MOVE);
    CHECK_EQ((Int)ScenarioDrill_parseLine("181 watercapturestop player:PlyrCivilian WaterFixtureHigh", &action), (Int)SCENARIO_PARSE_OK);
    CHECK_EQ(action.action, SCENARIO_ACTION_STOP);
    CHECK_EQ((Int)ScenarioDrill_parseLine("0 move 2 * start1:20:-30", &action), (Int)SCENARIO_PARSE_OK);
    CHECK(action.captureActorName.isEmpty());
    CHECK_EQ(action.atStart, 1);
}
TEST(water_capture_registry_witness_red_accepts_positive_lookup_ids)
{
    const char *lines[] = {
        "200 watercaptureinspect player:PlyrCivilian WaterFixtureLow 3 7",
        "201 watercaptureinspect player:PlyrCivilian a 1 1",
        "202 watercaptureinspect player:PlyrCivilian WaterFixtureLow 2147483647 2147483647",
    };
    for (const char *line : lines) {
        ScenarioAction action;
        const ScenarioParseResult result = ScenarioDrill_parseLine(line, &action);
        CHECK_EQ((Int)result, (Int)SCENARIO_PARSE_OK);
        if (result != SCENARIO_PARSE_OK) continue;
        CHECK_STR(action.capturePlayerName.str(), "PlyrCivilian");
        CHECK_STR(action.selector.str(), "WaterCaptureCarrier");
        CHECK_EQ(action.count, 1);
        CHECK_EQ(action.atStart, SCENARIO_NO_START);
    }
}
TEST(water_capture_registry_witness_red_rejects_unbounded_or_ambiguous_input)
{
    struct Bad { const char *line; ScenarioParseResult result; };
    const Bad cases[] = {
        {"0 watercaptureinspect 0 Low 3 7", SCENARIO_PARSE_BAD_SLOT},
        {"0 watercaptureinspect player:ThePlayer Low 3 7", SCENARIO_PARSE_BAD_SLOT},
        {"0 watercaptureinspect player:PlyrCivilian * 3 7", SCENARIO_PARSE_BAD_SLOT},
        {"0 watercaptureinspect player:PlyrCivilian object:3 3 7", SCENARIO_PARSE_BAD_SLOT},
        {"0 watercaptureinspect player:PlyrCivilian Low\xC3\xA9 3 7", SCENARIO_PARSE_BAD_SLOT},
        {"0 watercaptureinspect player:PlyrCivilian Low 0 7", SCENARIO_PARSE_BAD_COUNT},
        {"0 watercaptureinspect player:PlyrCivilian Low 3 0", SCENARIO_PARSE_BAD_COUNT},
        {"0 watercaptureinspect player:PlyrCivilian Low -1 7", SCENARIO_PARSE_BAD_COUNT},
        {"0 watercaptureinspect player:PlyrCivilian Low 3 +7", SCENARIO_PARSE_BAD_COUNT},
        {"0 watercaptureinspect player:PlyrCivilian Low 3 7junk", SCENARIO_PARSE_BAD_COUNT},
        {"0 watercaptureinspect player:PlyrCivilian Low 0x3 7", SCENARIO_PARSE_BAD_COUNT},
        {"0 watercaptureinspect player:PlyrCivilian Low 3 nan", SCENARIO_PARSE_BAD_COUNT},
        {"0 watercaptureinspect player:PlyrCivilian Low 2147483648 7", SCENARIO_PARSE_BAD_COUNT},
        {"0 watercaptureinspect player:PlyrCivilian Low 3 2147483648", SCENARIO_PARSE_BAD_COUNT},
        {"0 watercaptureinspect player:PlyrCivilian Low 999999999999999999999999999999999999 7", SCENARIO_PARSE_BAD_COUNT},
        {"0 watercaptureinspect player:PlyrCivilian Low 3", SCENARIO_PARSE_MISSING_ARGS},
        {"0 watercaptureinspect player:PlyrCivilian Low 3 7 extra", SCENARIO_PARSE_MISSING_ARGS},
    };
    for (const Bad &entry : cases) {
        ScenarioAction action;
        CHECK_EQ((Int)ScenarioDrill_parseLine(entry.line, &action), (Int)entry.result);
    }
}
