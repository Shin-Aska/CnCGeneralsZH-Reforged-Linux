#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "Common/GameState.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/ScriptConditions.h"
#include "../Stubs/NullAudioManager.h"

namespace
{
class CountdownTestWorld : public GameLogic
{
public:
	void setFrame(UnsignedInt frame)
	{
		// The world frame is private. Use its real snapshot entry point to set only that first
		// field, then stop before loading a world; no production setter or layout assumptions.
		struct FrameLoaded {};
		class FrameLoad : public XferLoad
		{
		public:
			explicit FrameLoad(UnsignedInt value) : frame(value) {}
			void xferVersion(XferVersion *, XferVersion) override {}
			void xferUnsignedInt(UnsignedInt *value) override { *value = frame; throw FrameLoaded(); }
			UnsignedInt frame;
		} load(frame);
		try { GameLogic::xfer(&load); }
		catch (const FrameLoaded &) {}
		CHECK_EQ(getFrame(), frame);
	}
};

struct CountdownTestContext
{
	GameLogic *savedLogic = TheGameLogic;
	SidesList *savedSides = TheSidesList;
	ScriptActionsInterface *savedActions = TheScriptActions;
	ScriptConditionsInterface *savedConditions = TheScriptConditions;
	GameEngine *savedEngine = TheGameEngine;
	Int savedSpeed = GAME_SPEED_PERCENT;
	SidesList sides;
	CountdownTestWorld *world;
	CountdownTestContext()
	{
		// Like the other empty-world fixtures, retain GameLogic: its destructor owns shared globals.
		static CountdownTestWorld *emptyWorld = NEW CountdownTestWorld;
		world = emptyWorld;
		TheGameLogic = world;
		TheSidesList = &sides;
		TheScriptActions = NULL;
		TheScriptConditions = NULL;
		TheGameEngine = NULL;
		GameSpeed_set(100);
	}
	~CountdownTestContext()
	{
		TheGameLogic = savedLogic;
		TheSidesList = savedSides;
		TheScriptActions = savedActions;
		TheScriptConditions = savedConditions;
		TheGameEngine = savedEngine;
		GameSpeed_set(savedSpeed);
	}
};

class CountdownTestEngine : public ScriptEngine
{
public:
	CountdownTestEngine() { reset(); }
	void tick() { updateCountdownTimers(); }
	void endingPass()
	{
		ScriptEngine *saved = TheScriptEngine;
		TheScriptEngine = this;
		m_firstUpdate = FALSE;
		m_endGameTimer = 0;
		m_fade = FADE_NONE;
		update();
		m_endGameTimer = -1;
		TheScriptEngine = saved;
	}
	void transfer(Xfer *xfer) { ScriptEngine::xfer(xfer); }
	void seed(Int value)
	{
		m_numCounters = 4;
		m_counters[1].value = value;
		m_counters[1].isCountdownTimer = TRUE;
		m_counters[2].value = 123;
		m_counters[2].isCountdownTimer = FALSE;
		m_counters[3].value = -5;
		m_counters[3].isCountdownTimer = TRUE;
	}
    Bool equal(Int value)
    {
        ConditionTemplate &type = m_conditionTemplates[Condition::COUNTER];
        type.m_numParameters = 3;
        type.m_parameters[0] = Parameter::COUNTER;
        type.m_parameters[1] = Parameter::COMPARISON;
        type.m_parameters[2] = Parameter::INT;
        ScriptEngine *saved = TheScriptEngine;
        TheScriptEngine = this;
        Condition *condition = newInstance(Condition)(Condition::COUNTER);
        condition->getParameter(0)->friend_setInt(1);
        condition->getParameter(1)->friend_setInt(Parameter::EQUAL);
        condition->getParameter(2)->friend_setInt(value);
        const Bool result = evaluateCounter(condition);
        condition->deleteInstance();
        TheScriptEngine = saved;
        return result;
    }
	Int remaining() const { return m_counters[1].value; }
	void checkOtherCounters() const
	{
		CHECK_EQ(m_counters[2].value, 123);
		CHECK_EQ(m_counters[3].value, -5);
	}
};

class CinematicPacingTestEngine : public GameEngine
{
	LocalFileSystem *createLocalFileSystem() override { return NULL; }
	ArchiveFileSystem *createArchiveFileSystem() override { return NULL; }
	GameLogic *createGameLogic() override { return NULL; }
	GameClient *createGameClient() override { return NULL; }
	ModuleFactory *createModuleFactory() override { return NULL; }
	ThingFactory *createThingFactory() override { return NULL; }
	FunctionLexicon *createFunctionLexicon() override { return NULL; }
	Radar *createRadar() override { return NULL; }
	WebBrowser *createWebBrowser() override { return NULL; }
	ParticleSystemManager *createParticleSystemManager() override { return NULL; }
	AudioManager *createAudioManager() override { return NULL; }
};
}

TEST(turbo_cinematic_pacing_preserves_dialogue_and_restores_match_speed)
{
	CHECK(bootOnce());
	CountdownTestContext context;
	GlobalData *savedData = TheWritableGlobalData;
	TheWritableGlobalData = NEW GlobalData;
	// Like the retained empty world, do not destroy an unbooted engine: its
	// destructor shuts down the shared game globals owned by this test binary.
	static CinematicPacingTestEngine *pacer = new CinematicPacingTestEngine;
	CountdownTestEngine script;
	ScriptEngine *savedScript = TheScriptEngine;
	TheScriptEngine = &script;
	TheGameEngine = pacer;
	context.world->setGameMode(GAME_SINGLE_PLAYER);
	pacer->setFramesPerSecondLimit(LOGICFRAMES_PER_SECOND * 2);
	CHECK_EQ(pacer->getEffectiveFramesPerSecondLimit(), LOGICFRAMES_PER_SECOND * 2);

	// Thrax holds input, but never asks for a cinematic frame rate. His second
	// cue at twelve game seconds must follow the 11.48-second first line.
	script.setInputHeldByScript(TRUE);
	CHECK_EQ(pacer->getFramesPerSecondLimit(), LOGICFRAMES_PER_SECOND * 2);
	CHECK_EQ(pacer->getEffectiveFramesPerSecondLimit(), LOGICFRAMES_PER_SECOND);
	CHECK_NEAR(GameEngine_missionTimeRate(), 1.0f, 0.001f);
	Real debt = 0.0f;
	Int ticks = 0, elapsedMs = 0;
	while (ticks < 12 * LOGICFRAMES_PER_SECOND && elapsedMs < 13000)
	{
		++elapsedMs;
		if (GameEngine_isLogicFrameDue(debt, 1.0f, pacer->getEffectiveFramesPerSecondLimit()))
			++ticks;
	}
	CHECK(elapsedMs >= 11999 && elapsedMs <= 12001);

	// Authored slow cinematics retain their own rate, even when an option or
	// SET_FPS_LIMIT changes the requested rate while input is still held.
	pacer->setFramesPerSecondLimit(ConvertRetailFrames(23));
	CHECK_EQ(pacer->getEffectiveFramesPerSecondLimit(), ConvertRetailFrames(23));
	CHECK_NEAR(GameEngine_missionTimeRate(), 23.0f / RETAIL_LOGICFRAMES_PER_SECOND, 0.001f);
	pacer->setFramesPerSecondLimit(LOGICFRAMES_PER_SECOND * 2);
	CHECK_EQ(pacer->getEffectiveFramesPerSecondLimit(), LOGICFRAMES_PER_SECOND);
	script.setInputHeldByScript(FALSE);
	CHECK_EQ(pacer->getEffectiveFramesPerSecondLimit(), LOGICFRAMES_PER_SECOND * 2);
	CHECK_NEAR(GameEngine_missionTimeRate(), 2.0f, 0.001f);

	// The same input flag in other game modes must not change their clocks.
	script.setInputHeldByScript(TRUE);
	const Int otherModes[] = {GAME_SKIRMISH, GAME_LAN, GAME_INTERNET, GAME_REPLAY, GAME_SHELL, GAME_NONE};
	for (Int mode : otherModes)
	{
		context.world->setGameMode(mode);
		CHECK_EQ(pacer->getEffectiveFramesPerSecondLimit(), LOGICFRAMES_PER_SECOND * 2);
	}
	context.world->setGameMode(GAME_SINGLE_PLAYER);
	// The sync branch scales the game time of a tick at each selected speed.
	// Both the effective rate and animation clock must use that same retail pace.
	for (Int speed : PLAYER_GAME_SPEED_PERCENTS)
	{
		GameSpeed_set(speed);
		const Int retailPace = GameEngine_framesPerSecondForSpeed(100);
		pacer->setFramesPerSecondLimit(LOGICFRAMES_PER_SECOND * 2);
		CHECK_EQ(pacer->getEffectiveFramesPerSecondLimit(), retailPace);
		CHECK_EQ(pacer->getFramesPerSecondLimit(), LOGICFRAMES_PER_SECOND * 2);
		CHECK_NEAR(GameEngine_missionTimeRate(), (Real)retailPace / LOGICFRAMES_PER_SECOND, 0.001f);
		pacer->setFramesPerSecondLimit(ConvertRetailFrames(23));
		CHECK_EQ(pacer->getEffectiveFramesPerSecondLimit(), ConvertRetailFrames(23));
		script.setInputHeldByScript(FALSE);
		pacer->setFramesPerSecondLimit(LOGICFRAMES_PER_SECOND * 2);
		CHECK_EQ(pacer->getEffectiveFramesPerSecondLimit(), LOGICFRAMES_PER_SECOND * 2);
		script.setInputHeldByScript(TRUE);
	}
	GameSpeed_set(100);
	pacer->setFramesPerSecondLimit(LOGICFRAMES_PER_SECOND);
	CHECK_EQ(pacer->getEffectiveFramesPerSecondLimit(), LOGICFRAMES_PER_SECOND);
	TheScriptEngine = NULL;
	pacer->setFramesPerSecondLimit(LOGICFRAMES_PER_SECOND * 2);
	CHECK_EQ(pacer->getEffectiveFramesPerSecondLimit(), LOGICFRAMES_PER_SECOND * 2);
	context.world->setGameMode(GAME_NONE);
	TheScriptEngine = savedScript;
	delete TheWritableGlobalData;
	TheWritableGlobalData = savedData;
}

TEST(script_countdown_timers_preserve_game_speed_and_frozen_world_phase)
{
    CHECK(bootOnce());
    CountdownTestContext context;
    for (Int speed : PLAYER_GAME_SPEED_PERCENTS) {
        GameSpeed_set(speed);
        for (UnsignedInt frozenFrame = 0; frozenFrame < 3; ++frozenFrame) {
            context.world->setFrame(frozenFrame);
            CountdownTestEngine engine;
            engine.seed(1000);
            for (Int pass = 0; pass < LOGICFRAMES_PER_SECOND; ++pass) {
                if (frozenFrame == 2) context.world->setFrame(pass);
                engine.tick();
            }
            CHECK_EQ(engine.remaining(), 1000 - (RETAIL_LOGICFRAMES_PER_SECOND * speed + 99) / 100);
            engine.checkOtherCounters();
        }
        CountdownTestEngine engine;
        engine.seed(1000);
        for (UnsignedInt pass = 0; pass < 64; ++pass) {
            const UnsignedInt frame = pass < 8 ? pass : pass < 32 ? 8 : pass < 56 ? 9 : pass - 46;
            context.world->setFrame(frame);
            engine.tick();
            CHECK_EQ(engine.remaining(), 1000 - (Int)(((pass + 1) * speed + 99) / 100));
        }
        engine.reset(); engine.seed(0); engine.tick();
        CHECK_EQ(engine.remaining(), -1);
        for (Int pass = 0; pass < 8; ++pass) engine.tick();
        CHECK_EQ(engine.remaining(), -1);
        engine.checkOtherCounters();
        for (Int map = 0; map < 2; ++map) {
            if (map) engine.newMap(); else engine.reset();
            engine.seed(10); engine.tick();
            CHECK_EQ(engine.remaining(), 10 - (speed + 99) / 100);
        }
        engine.reset(); engine.seed(10); engine.endingPass();
        CHECK_EQ(engine.remaining(), 10);
        engine.tick();
        CHECK_EQ(engine.remaining(), 10 - (speed + 99) / 100);
    }
}

TEST(script_countdown_timer_equality_covers_every_crossing_at_every_speed)
{
    CHECK(bootOnce());
    CountdownTestContext context;
    for (Int speed : PLAYER_GAME_SPEED_PERCENTS) {
        GameSpeed_set(speed);
        CountdownTestEngine engine;
        engine.seed(100);
        Bool seen[100] = {};
        for (UnsignedInt pass = 0; engine.remaining() >= 0; ++pass) {
            context.world->setFrame(pass < 4 ? pass : 4); // equality also holds while world time is frozen
            engine.tick();
            for (Int target = 0; target < 100; ++target)
                seen[target] = seen[target] || engine.equal(target);
        }
        for (Int target = 0; target < 100; ++target) CHECK(seen[target]);
        engine.seed(10); engine.tick();
        const Int crossed = engine.remaining() + 1;
        if (speed > 100) {
            // A newly assigned value must invalidate a previous tick's skipped-value comparison.
            engine.seed(20);
            CHECK(!engine.equal(crossed));
        }
    }
}

TEST(script_countdown_timer_snapshot_preserves_phase_and_loads_older_saves)
{
	CHECK(bootOnce());
	CountdownTestContext context;
	context.world->setFrame(0);
	const char *path = "test_script_timer_save_tmp.bin";
	for (Int speed : PLAYER_GAME_SPEED_PERCENTS)
	for (Int phase = 0; phase < 4; ++phase)
	{
		GameSpeed_set(speed);
		CountdownTestEngine original;
		original.seed(100);
		for (Int pass = 0; pass < phase; ++pass) original.tick();
		XferSave save;
		save.open(AsciiString(path));
		original.transfer(&save);
		save.close();
		CountdownTestEngine restored;
		XferLoad load;
		load.setOptions(XO_NO_POST_PROCESSING);
		load.open(AsciiString(path));
		restored.transfer(&load);
		load.close();
		for (Int pass = 0; pass < 9; ++pass)
		{
			original.tick();
			restored.tick();
			CHECK_EQ(restored.remaining(), original.remaining());
		}
	}
	GameSpeed_set(125);
	// v7 appends only the subframe byte to the v6 snapshot. A v6 save deterministically starts
	// with phase zero; an invalid v7 phase is rejected rather than stopping every timer forever.
	FILE *file = fopen(path, "rb");
	CHECK(file != NULL);
	if (!file) return;
	fseek(file, 0, SEEK_END);
	const long size = ftell(file);
	rewind(file);
	std::vector<UnsignedByte> bytes(size);
	CHECK_EQ(fread(bytes.data(), 1, bytes.size(), file), bytes.size());
	fclose(file);
	CHECK(bytes.size() > 2 && bytes.front() == 7);
	for (Int legacy = 0; legacy < 2; ++legacy)
	{
		bytes.front() = legacy ? 6 : 7;
		bytes.back() = 100 * LOGICFRAMES_PER_RETAIL_FRAME;
		file = fopen(path, "wb");
		CHECK(file != NULL);
		if (!file) return;
		const size_t length = bytes.size() - (legacy ? 1 : 0);
		CHECK_EQ(fwrite(bytes.data(), 1, length, file), length);
		fclose(file);
		CountdownTestEngine restored;
		XferLoad load;
		load.setOptions(XO_NO_POST_PROCESSING);
		load.open(AsciiString(path));
		Bool rejected = FALSE;
		try { restored.transfer(&load); }
		catch (SaveCode error) { rejected = error == SC_INVALID_DATA; CHECK(rejected); }
		catch (...) { CHECK(FALSE); }
		load.close();
		CHECK_EQ(rejected, !legacy);
		if (legacy && !rejected)
		{
			const Int before = restored.remaining();
			restored.tick();
			CHECK_EQ(restored.remaining(), before - 2);
		}
	}
	remove(path);
}

namespace {
class SpeechCompletionTestAudio : public NullAudioManager
{
public:
	Int measurements = 0;
	Real getAudioLengthMS(const AudioEventRTS *) override { ++measurements; return 1000.0f; }
};
}

TEST(script_speech_completion_restarts_a_preexisting_wait_on_a_logical_request)
{
	CHECK(bootOnce());
	CountdownTestContext context;
	SpeechCompletionTestAudio audio;
	AudioManager *savedAudio = TheAudio;
	TheAudio = &audio;
	CountdownTestEngine engine;
	const AsciiString line("BriefingLine"), other("BriefingLineAlternative"), neverPlayed("UnmatchedName"), shared("SharedLine");
	context.world->setFrame(0);
	CHECK(!engine.isSpeechComplete(line, FALSE));
	context.world->setFrame(10);
	CHECK(!engine.isSpeechComplete(other, FALSE));
	context.world->setFrame(15);
	const Int measured = audio.measurements;
	const UnsignedInt seed = GetGameLogicRandomSeedCRC();
	engine.notifySpeechRequested(line);
	engine.notifySpeechRequested(AsciiString("NoExistingWait"));
	CHECK_EQ(audio.measurements, measured);
	CHECK_EQ(GetGameLogicRandomSeedCRC(), seed);
	context.world->setFrame(16);
	CHECK(!engine.isSpeechComplete(line, FALSE)); // the new deadline is 46, not the old 30
	context.world->setFrame(30);
	CHECK(!engine.isSpeechComplete(line, FALSE));
	context.world->setFrame(40);
	CHECK(engine.isSpeechComplete(other, FALSE)); // exact-name invalidation leaves this deadline alone
	context.world->setFrame(45);
	CHECK(!engine.isSpeechComplete(line, FALSE));
	context.world->setFrame(46);
	CHECK(engine.isSpeechComplete(line, FALSE));
	engine.notifySpeechRequested(line); // a repeated request also replaces an already completed test
	context.world->setFrame(50);
	CHECK(!engine.isSpeechComplete(line, FALSE));
	context.world->setFrame(79);
	CHECK(!engine.isSpeechComplete(line, FALSE));
	context.world->setFrame(80);
	CHECK(engine.isSpeechComplete(line, FALSE));
	// Retail scripts sometimes wait on a name they never play. Preserve that lazy duration fallback.
	context.world->setFrame(100);
	CHECK(!engine.isSpeechComplete(neverPlayed, TRUE));
	context.world->setFrame(129);
	CHECK(!engine.isSpeechComplete(neverPlayed, TRUE));
	context.world->setFrame(130);
	CHECK(engine.isSpeechComplete(neverPlayed, TRUE));
	// Preserve the existing consuming-query contract when two scripts observe the same name.
	context.world->setFrame(200);
	CHECK(!engine.isSpeechComplete(shared, TRUE));
	context.world->setFrame(230);
	CHECK(engine.isSpeechComplete(shared, TRUE));
	CHECK(!engine.isSpeechComplete(shared, TRUE));
	context.world->setFrame(259);
	CHECK(!engine.isSpeechComplete(shared, TRUE));
	context.world->setFrame(260);
	CHECK(engine.isSpeechComplete(shared, TRUE));
	TheAudio = savedAudio;
}
