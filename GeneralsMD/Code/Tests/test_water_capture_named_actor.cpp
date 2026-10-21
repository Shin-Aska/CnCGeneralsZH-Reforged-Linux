// Unregistered source-only parser PIN/RED suite; Native must attach to test_gameengine.
#include "test_harness.h"
#include "GameLogic/ScenarioDrill.h"

TEST(water_capture_named_actor_pin_original_capture_and_ordinary)
{
    ScenarioAction action;
    CHECK_EQ((Int)ScenarioDrill_parseLine("4140 spawn player:ThePlayer WaterCaptureCarrier 1 2500 1000", &action), (Int)SCENARIO_PARSE_OK);
    CHECK_EQ(action.action, SCENARIO_ACTION_SPAWN);
    CHECK_STR(action.capturePlayerName.str(), "ThePlayer");
    CHECK_EQ(action.count, 1);
    CHECK_EQ((Int)ScenarioDrill_parseLine("4200 move player:PlyrCivilian WaterCaptureCarrier 2600 1000", &action), (Int)SCENARIO_PARSE_OK);
    CHECK_EQ(action.action, SCENARIO_ACTION_MOVE);
    CHECK_STR(action.selector.str(), "WaterCaptureCarrier");
    CHECK_EQ(action.at.x, 2600.0f);
    CHECK_EQ((Int)ScenarioDrill_parseLine("4500 stop player:ThePlayer WaterCaptureCarrier", &action), (Int)SCENARIO_PARSE_OK);
    CHECK_EQ(action.action, SCENARIO_ACTION_STOP);
    CHECK_EQ((Int)ScenarioDrill_parseLine("0 spawn player:ThePlayer WaterCaptureCarrier 2 0 0", &action), (Int)SCENARIO_PARSE_BAD_COUNT);
    CHECK_EQ((Int)ScenarioDrill_parseLine("0 move civilian * 10 20", &action), (Int)SCENARIO_PARSE_BAD_SLOT);
    CHECK_EQ((Int)ScenarioDrill_parseLine("0 move -1 * 10 20", &action), (Int)SCENARIO_PARSE_BAD_SLOT);
    CHECK_EQ((Int)ScenarioDrill_parseLine("120 move 2 * start1:20:-30", &action), (Int)SCENARIO_PARSE_OK);
    CHECK_EQ(action.frame, 120u);
    CHECK_EQ(action.slot, 2);
    CHECK_EQ(action.atStart, 1);
    CHECK_EQ(action.at.x, 20.0f);
    CHECK_EQ(action.at.y, -30.0f);
    CHECK(action.capturePlayerName.isEmpty());
    CHECK_EQ((Int)ScenarioDrill_parseLine("900 stop 2 * # ordinary comment", &action), (Int)SCENARIO_PARSE_OK);
}

TEST(water_capture_named_actor_red_accepts_independent_move_and_stop)
{
    ScenarioAction low, high;
    const ScenarioParseResult lowResult = ScenarioDrill_parseLine(
        "120 watercapturemove player:PlyrCivilian LowCarrier 1200 425", &low);
    const ScenarioParseResult highResult = ScenarioDrill_parseLine(
        "120 watercapturemove player:PlyrCivilian HighCarrier 600 1375", &high);
    CHECK_EQ((Int)lowResult, (Int)SCENARIO_PARSE_OK);
    CHECK_EQ((Int)highResult, (Int)SCENARIO_PARSE_OK);
    // Guard field reads: old parser returns BAD_ACTION without initializing either action.
    if (lowResult == SCENARIO_PARSE_OK && highResult == SCENARIO_PARSE_OK) {
        CHECK_EQ(low.action, SCENARIO_ACTION_MOVE);
        CHECK_EQ(high.action, SCENARIO_ACTION_MOVE);
        CHECK_STR(low.capturePlayerName.str(), "PlyrCivilian");
        CHECK_STR(low.selector.str(), "WaterCaptureCarrier");
        CHECK_EQ(low.at.x, 1200.0f);
        CHECK_EQ(low.at.y, 425.0f);
        CHECK_EQ(high.at.x, 600.0f);
        CHECK_EQ(high.at.y, 1375.0f);
    }
    ScenarioAction stop;
    const ScenarioParseResult stopped = ScenarioDrill_parseLine(
        "180 watercapturestop player:PlyrCivilian LowCarrier", &stop);
    CHECK_EQ((Int)stopped, (Int)SCENARIO_PARSE_OK);
    if (stopped == SCENARIO_PARSE_OK) CHECK_EQ(stop.action, SCENARIO_ACTION_STOP);
}

TEST(water_capture_named_actor_red_rejects_wrong_owner_name_and_position)
{
    ScenarioAction action;
    const char *badOwnerOrName[] = {
        "0 watercapturemove 0 LowCarrier 10 20",
        "0 watercapturemove player:ThePlayer LowCarrier 10 20",
        "0 watercapturestop player:PlyrCivilian *",
        "0 watercapturestop player:PlyrCivilian Low*",
        "0 watercapturestop player:PlyrCivilian object:3",
    };
    for (const char *line : badOwnerOrName)
        CHECK_EQ((Int)ScenarioDrill_parseLine(line, &action), (Int)SCENARIO_PARSE_BAD_SLOT);
    const char *badPosition[] = {
        "0 watercapturemove player:PlyrCivilian LowCarrier start0",
        "0 watercapturemove player:PlyrCivilian LowCarrier nan 20",
        "0 watercapturemove player:PlyrCivilian LowCarrier 10 inf",
        "0 watercapturemove player:PlyrCivilian LowCarrier 12junk 20",
    };
    for (const char *line : badPosition)
        CHECK_EQ((Int)ScenarioDrill_parseLine(line, &action), (Int)SCENARIO_PARSE_BAD_POSITION);
    CHECK_EQ((Int)ScenarioDrill_parseLine("0 watercapturemove player:PlyrCivilian LowCarrier 10", &action), (Int)SCENARIO_PARSE_MISSING_ARGS);
    CHECK_EQ((Int)ScenarioDrill_parseLine("0 watercapturestop player:PlyrCivilian LowCarrier extra", &action), (Int)SCENARIO_PARSE_MISSING_ARGS);
    CHECK_EQ((Int)ScenarioDrill_parseLine("0 watercapturemove player:PlyrCivilian LowCarrier 10 20 extra", &action), (Int)SCENARIO_PARSE_MISSING_ARGS);
}
