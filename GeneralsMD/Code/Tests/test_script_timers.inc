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
	}
	~CountdownTestContext()
	{
		TheGameLogic = savedLogic;
		TheSidesList = savedSides;
		TheScriptActions = savedActions;
		TheScriptConditions = savedConditions;
		TheGameEngine = savedEngine;
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
	Int remaining() const { return m_counters[1].value; }
	void checkOtherCounters() const
	{
		CHECK_EQ(m_counters[2].value, 123);
		CHECK_EQ(m_counters[3].value, -5);
	}
};
}

TEST(script_countdown_timers_keep_half_cadence_while_world_time_is_frozen)
{
	CHECK(bootOnce());
	CountdownTestContext context;
	// USA01 freezes world time for its briefing. Even frames used to count twice as fast;
	// odd frames never counted at all. Both must keep the same cadence as a moving world.
	for (UnsignedInt frozenFrame = 0; frozenFrame < 3; ++frozenFrame)
	{
		context.world->setFrame(frozenFrame);
		CountdownTestEngine engine;
		engine.seed(100);
		for (Int pass = 0; pass < LOGICFRAMES_PER_SECOND; ++pass)
		{
			if (frozenFrame == 2) context.world->setFrame(pass);
			engine.tick();
		}
		CHECK_EQ(engine.remaining(), 100 - RETAIL_LOGICFRAMES_PER_SECOND);
		engine.checkOtherCounters();
	}
	CountdownTestEngine engine;
	engine.seed(100);
	for (UnsignedInt pass = 0; pass < 64; ++pass)
	{
		// Advance, freeze on even, freeze on odd, then advance again. The timer phase must
		// continue through every transition instead of resynchronizing to the world frame.
		const UnsignedInt frame = pass < 8 ? pass : pass < 32 ? 8 : pass < 56 ? 9 : pass - 46;
		context.world->setFrame(frame);
		engine.tick();
		CHECK_EQ(engine.remaining(), 100 - (Int)((pass + 2) / 2));
	}
	engine.reset();
	engine.seed(0);
	engine.tick();
	CHECK_EQ(engine.remaining(), -1);
	for (Int pass = 0; pass < 8; ++pass) engine.tick();
	CHECK_EQ(engine.remaining(), -1);
	engine.checkOtherCounters();
	// Reset and newMap both discard a half-used timer frame.
	for (Int map = 0; map < 2; ++map)
	{
		if (map) engine.newMap(); else engine.reset();
		engine.seed(10);
		engine.tick();
		CHECK_EQ(engine.remaining(), 9);
	}
	engine.reset();
	engine.seed(10);
	engine.endingPass();
	CHECK_EQ(engine.remaining(), 10);
	engine.tick();
	CHECK_EQ(engine.remaining(), 9);
}

TEST(script_countdown_timer_snapshot_preserves_phase_and_loads_older_saves)
{
	CHECK(bootOnce());
	CountdownTestContext context;
	context.world->setFrame(0);
	const char *path = "test_script_timer_save_tmp.bin";
	for (Int phase = 0; phase < LOGICFRAMES_PER_RETAIL_FRAME; ++phase)
	{
		CountdownTestEngine original;
		original.seed(100);
		if (phase) original.tick();
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
	// v6 appends only the subframe byte to the v5 snapshot. A v5 save deterministically starts
	// with phase zero; an invalid v6 phase is rejected rather than stopping every timer forever.
	FILE *file = fopen(path, "rb");
	CHECK(file != NULL);
	if (!file) return;
	fseek(file, 0, SEEK_END);
	const long size = ftell(file);
	rewind(file);
	std::vector<UnsignedByte> bytes(size);
	CHECK_EQ(fread(bytes.data(), 1, bytes.size(), file), bytes.size());
	fclose(file);
	CHECK(bytes.size() > 2 && bytes.front() == 6);
	for (Int legacy = 0; legacy < 2; ++legacy)
	{
		bytes.front() = legacy ? 5 : 6;
		bytes.back() = LOGICFRAMES_PER_RETAIL_FRAME;
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
			CHECK_EQ(restored.remaining(), before - 1);
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
	context.world->setFrame(20);
	CHECK(!engine.isSpeechComplete(other, FALSE));
	context.world->setFrame(30);
	const Int measured = audio.measurements;
	const UnsignedInt seed = GetGameLogicRandomSeedCRC();
	engine.notifySpeechRequested(line);
	engine.notifySpeechRequested(AsciiString("NoExistingWait"));
	CHECK_EQ(audio.measurements, measured);
	CHECK_EQ(GetGameLogicRandomSeedCRC(), seed);
	context.world->setFrame(31);
	CHECK(!engine.isSpeechComplete(line, FALSE)); // the new deadline is 91, not the old 60
	context.world->setFrame(60);
	CHECK(!engine.isSpeechComplete(line, FALSE));
	context.world->setFrame(80);
	CHECK(engine.isSpeechComplete(other, FALSE)); // exact-name invalidation leaves this deadline alone
	context.world->setFrame(90);
	CHECK(!engine.isSpeechComplete(line, FALSE));
	context.world->setFrame(91);
	CHECK(engine.isSpeechComplete(line, FALSE));
	engine.notifySpeechRequested(line); // a repeated request also replaces an already completed test
	context.world->setFrame(100);
	CHECK(!engine.isSpeechComplete(line, FALSE));
	context.world->setFrame(159);
	CHECK(!engine.isSpeechComplete(line, FALSE));
	context.world->setFrame(160);
	CHECK(engine.isSpeechComplete(line, FALSE));
	// Retail scripts sometimes wait on a name they never play. Preserve that lazy duration fallback.
	context.world->setFrame(200);
	CHECK(!engine.isSpeechComplete(neverPlayed, TRUE));
	context.world->setFrame(259);
	CHECK(!engine.isSpeechComplete(neverPlayed, TRUE));
	context.world->setFrame(260);
	CHECK(engine.isSpeechComplete(neverPlayed, TRUE));
	// Preserve the existing consuming-query contract when two scripts observe the same name.
	context.world->setFrame(400);
	CHECK(!engine.isSpeechComplete(shared, TRUE));
	context.world->setFrame(460);
	CHECK(engine.isSpeechComplete(shared, TRUE));
	CHECK(!engine.isSpeechComplete(shared, TRUE));
	context.world->setFrame(519);
	CHECK(!engine.isSpeechComplete(shared, TRUE));
	context.world->setFrame(520);
	CHECK(engine.isSpeechComplete(shared, TRUE));
	TheAudio = savedAudio;
}
