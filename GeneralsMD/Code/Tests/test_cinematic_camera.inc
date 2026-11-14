// The production camera and its presentation-only sampler, without a map or graphics device.
#include "W3DDevice/GameClient/W3DView.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/ScriptConditions.h"
#include "GameClient/GameText.h"

TEST(cinematic_camera_animation_clock_preserves_real_freezes_and_other_clock_modes)
{
	CinematicAnimationClockMode normal;
	normal.singlePlayer = true;
	CHECK(normal.allowsRenderTime());
	CHECK(!CinematicAnimationClockFrozen(false, true, normal));
	CHECK(CinematicAnimationClockFrozen(true, true, normal));
	CHECK(CinematicAnimationClockFrozen(true, false, normal));
	CHECK(!CinematicAnimationClockFrozen(false, false, normal));
	// Every exclusion is independently sufficient. Positive and zero picture identities follow
	// the same policy; there is no special first-frame escape from a genuine freeze.
	bool CinematicAnimationClockMode::*exclusions[] = {
		&CinematicAnimationClockMode::network, &CinematicAnimationClockMode::replay,
		&CinematicAnimationClockMode::headless, &CinematicAnimationClockMode::shell,
		&CinematicAnimationClockMode::loading, &CinematicAnimationClockMode::fast,
		&CinematicAnimationClockMode::video, &CinematicAnimationClockMode::capture,
		&CinematicAnimationClockMode::pictureClock
	};
	for (auto exclusion : exclusions)
	{
		CinematicAnimationClockMode mode = normal;
		mode.*exclusion = true;
		CHECK(!mode.allowsRenderTime());
		CHECK(CinematicAnimationClockFrozen(false, true, mode));
		CHECK(!CinematicAnimationClockFrozen(false, false, mode));
		CHECK(CinematicAnimationClockFrozen(true, false, mode));
	}
	CinematicAnimationClockMode inactive;
	CHECK(!inactive.allowsRenderTime());
	CHECK(CinematicAnimationClockFrozen(false, true, inactive));
	CHECK(!CinematicAnimationClockFrozen(false, false, inactive));
}

TEST(cinematic_camera_animation_clock_advances_repeated_client_frames_without_projection_pulses)
{
	const Int renderRates[] = {60, 120, 144};
	const Int logicRates[] = {44, 60};
	for (Int renderRate : renderRates)
		for (Int logicRate : logicRates)
			for (UnsignedInt firstFrame : {0u, 100u})
			{
				CinematicAnimationClockMode mode[2];
				mode[1].singlePlayer = true;
				UnsignedInt lastFrame = ~0u, previousMs[2] = {0, 0}, sync[2] = {0, 0};
				double remainder[2] = {0, 0};
				Int repeats = 0, legacyPulses = 0;
				for (Int picture = 0; picture <= renderRate * 3; ++picture)
				{
					const UnsignedInt frame = firstFrame + (UnsignedInt)((Int64)picture * logicRate / renderRate);
					const UnsignedInt now = (UnsignedInt)((Int64)picture * 1000 / renderRate);
					const bool repeated = lastFrame == frame;
					const double rate = (double)logicRate / 60.0;
					const UnsignedInt cameraMs = (UnsignedInt)(now * rate + 1.0e-8);
					if (repeated) ++repeats;
					for (Int enabled = 0; enabled < 2; ++enabled)
					{
						// This is the actual out-of-line guard used by W3DDisplay::draw. Keep the
						// existing accumulated wall-time algorithm on either side of its decision.
						if (!CinematicAnimationClockFrozen(false, repeated, mode[enabled]))
						{
							remainder[enabled] += (now - previousMs[enabled]) * rate;
							previousMs[enabled] = now;
							const UnsignedInt delta = (UnsignedInt)(remainder[enabled] + 1.0e-8);
							remainder[enabled] -= delta;
							sync[enabled] += delta;
						}
						// A rigid aircraft travels towards the eye, keeping pace vertically with a
						// moving camera. Its smooth projected centre is fixed. Holding only the
						// aircraft on a repeated client frame creates a backwards screen-space pulse.
						Matrix3D mesh(true), cameraInverse(true), viewMesh;
						mesh.Set_Translation(Vector3(20.0f, -0.3f * sync[enabled], 200.0f - 0.02f * sync[enabled]));
						cameraInverse.Set_Translation(Vector3(0.0f, 0.3f * cameraMs, 0.0f));
						Matrix3D::Multiply(cameraInverse, mesh, &viewMesh);
						const Vector3 centre = viewMesh.Get_Translation();
						const Real screenY = 180.0f - 180.0f * centre.Y / centre.Z;
						if (enabled)
						{
							CHECK_EQ(sync[enabled], cameraMs);
							CHECK_NEAR(screenY, 180.0f, 0.0002f);
						}
						else if (repeated && screenY < 179.0f) ++legacyPulses;
					}
					lastFrame = frame;
				}
				CHECK_EQ(repeats > 0, renderRate > logicRate);
				CHECK_EQ(legacyPulses > 0, renderRate > logicRate);
				CHECK_EQ(sync[0], sync[1]); // same elapsed duration at the final common boundary
			}
}

struct CinematicCameraTestAccess
{
	static void clientMode(W3DView &view, const CinematicAnimationClockMode &mode)
	{ view.captureCinematicCameraEligibility(mode); }
	static Bool eligible(const W3DView &view) { return view.m_cinematicCameraSmooth; }
	static Bool pose(W3DView &view, Real phase, Bool allowed = TRUE, Bool eligible = TRUE)
	{ return view.updateCinematicCameraPose(phase, allowed, eligible); }
	static void step(W3DView &view) { view.updateCameraMovements(); }
	static Int zoomStep(const W3DView &view) { return view.m_zcInfo.curFrame; }
	static Int angleStep(const W3DView &view) { return view.m_rcInfo.curFrame; }
	static Int pitchStep(const W3DView &view) { return view.m_pcInfo.curFrame; }
	static Int count(const W3DView &view) { return view.m_zcInfo.numFrames; }
	static Real shownZoom(const W3DView &view) { return view.m_cinematicCameraZoom; }
	static Real shownAngle(const W3DView &view) { return view.m_cinematicCameraAngle; }
	static Real shownPitch(const W3DView &view) { return view.cameraPresentationPitch(); }
	static Bool valid(const W3DView &view) { return view.m_cinematicCameraPoseValid; }
	static Matrix3D matrix(W3DView &view) { Matrix3D m; view.buildCameraTransform(&m); return m; }
	static void realZoom(W3DView &view, Bool on) { view.m_useRealZoomCam = on; }
	static void tracked(W3DView &view) { view.m_rcInfo.trackObject = TRUE; }
	static void slave(W3DView &view, Bool on) { view.m_isCameraSlaved = on; }
	static void diagnostic(W3DView &view, Bool on) { view.m_diagnosticRouteCaptured = on; }
	static void freeCamera(W3DView &view, Bool on) { view.m_freeCamera = on; }
	static void installCamera(W3DView &view) { view.m_3DCamera = NEW_REF(CameraClass, ()); }
	static void constrain(W3DView &view) { view.applyCameraConstraint(); }
	static Region2D boundary(const W3DView &view) { return view.m_cameraConstraint; }
	static void position(W3DView &view, const Coord3D &position) { view.setPosition(&position); }
	static Coord3D position(const W3DView &view) { return *view.getPosition(); }
	static void extraClock(W3DView &view, UnsignedInt now, Real phase, Real rate, Bool allowed)
	{
		view.m_cinematicCameraClockTime = now;
		view.m_cinematicCameraClockPhase = phase;
		view.m_cinematicCameraClockRate = rate;
		view.m_cinematicCameraExtraPictures = allowed;
	}
	static Bool extraPicture(W3DView &view, UnsignedInt now) { return view.advanceCinematicCameraPicture(now); }
	static Matrix3D published(const W3DView &view) { return view.m_3DCamera->Get_Transform(); }
};

namespace
{
struct CinematicCameraFixture
{
	GlobalData *savedGlobal = TheWritableGlobalData;
	TerrainLogic *savedTerrain = TheTerrainLogic;
	BaseHeightMapRenderObjClass *savedRenderedTerrain = TheTerrainRenderObject;
	Display *savedDisplay = TheDisplay;
	GameLogic *savedLogic = TheGameLogic;
	ScriptEngine *savedScript = TheScriptEngine;
	RecorderClass *savedRecorder = TheRecorder;
	NetworkInterface *savedNetwork = TheNetwork;
	GlobalData *global;
	GameLogic *logic;
	CinematicCameraFixture()
	{
		CHECK(bootOnce());
		static Bool mathReady = FALSE;
		if (!mathReady)
		{
			WWMath::Init();
			atexit(WWMath::Shutdown); // release its default lookup table before static-list teardown
			mathReady = TRUE;
		}
		global = NEW GlobalData;
		logic = NEW GameLogic;
		logic->setGameMode(GAME_SINGLE_PLAYER);
		TheGameLogic = logic;
		TheScriptEngine = NULL;
		TheRecorder = NULL;
		TheNetwork = NULL;
		TheWritableGlobalData = global;
		global->m_cameraHeight = 300;
		global->m_cameraPitch = 37.5f;
		global->m_cameraYaw = 0;
		global->m_disableCameraMovement = FALSE;
		global->m_useCameraConstraints = FALSE;
		TheTerrainLogic = NULL;
		TheTerrainRenderObject = NULL;
	}
	~CinematicCameraFixture()
	{
		// This fixture never creates the subsystems GameLogic normally owns. Preserve any
		// surrounding test's globals while destroying its empty logic instance.
		auto ghost = TheGhostObjectManager; TheGhostObjectManager = NULL;
		auto partition = ThePartitionManager; ThePartitionManager = NULL;
		auto actions = TheScriptActions; TheScriptActions = NULL;
		auto conditions = TheScriptConditions; TheScriptConditions = NULL;
		TheTerrainLogic = NULL;
		delete logic;
		TheGhostObjectManager = ghost;
		ThePartitionManager = partition;
		TheScriptActions = actions;
		TheScriptConditions = conditions;
		TheGameLogic = savedLogic;
		TheScriptEngine = savedScript;
		TheRecorder = savedRecorder;
		TheNetwork = savedNetwork;
		TheTerrainLogic = savedTerrain;
		TheTerrainRenderObject = savedRenderedTerrain;
		TheDisplay = savedDisplay;
		delete global;
		TheWritableGlobalData = savedGlobal;
	}
	void prepare(W3DView &view, Bool normalSinglePlayer = TRUE)
	{
		view.View::init();
		view.setZoom(1.0f); // also initializes/cancels every scripted mover
		view.setAngle(0.0f);
		view.setPitch(0.0f);
		CinematicAnimationClockMode mode = CinematicCameraModeForClientUpdate();
		if (!normalSinglePlayer) mode.singlePlayer = false; // authoritative legacy control view
		CinematicCameraTestAccess::clientMode(view, mode);
	}
};

static Bool cinematicCameraMatricesDiffer(const Matrix3D &a, const Matrix3D &b)
{
	for (Int r = 0; r < 3; ++r)
		for (Int c = 0; c < 4; ++c)
			if (fabs(a[r][c] - b[r][c]) > 1.0e-5f) return TRUE;
	return FALSE;
}

class CinematicCameraTerrain : public TerrainLogic
{
public:
	void getExtent(Region3D *extent) const
	{
		extent->lo.set(0, 0, 0);
		extent->hi.set(2000, 2000, 0);
	}
};

class CinematicCameraDisplay : public Display
{
public:
	CinematicCameraDisplay() { m_width = 800; m_height = 600; }
	void doSmartAssetPurgeAndPreload(const char *) {}
#if defined(_DEBUG) || defined(_INTERNAL)
	void dumpAssetUsage(const char *) {}
	void dumpModelAssets(const char *) {}
#endif
	VideoBuffer *createVideoBuffer() { return NULL; }
	void setClipRegion(IRegion2D *) {}
	Bool isClippingEnabled() { return FALSE; }
	void enableClipping(Bool) {}
	void setTimeOfDay(TimeOfDay) {}
	void createLightPulse(const Coord3D *, const RGBColor *, Real, Real, UnsignedInt, UnsignedInt) {}
	void drawLine(Int, Int, Int, Int, Real, UnsignedInt) {}
	void drawLine(Int, Int, Int, Int, Real, UnsignedInt, UnsignedInt) {}
	void drawOpenRect(Int, Int, Int, Int, Real, UnsignedInt) {}
	void drawFillRect(Int, Int, Int, Int, UnsignedInt) {}
	void drawRectClock(Int, Int, Int, Int, Int, UnsignedInt) {}
	void drawRemainingRectClock(Int, Int, Int, Int, Int, UnsignedInt) {}
	void drawImage(const Image *, Int, Int, Int, Int, Color, DrawImageMode) {}
	void drawVideoBuffer(VideoBuffer *, Int, Int, Int, Int) {}
	void setShroudLevel(Int, Int, CellShroudStatus) {}
	void clearShroud() {}
	void setBorderShroudLevel(UnsignedByte) {}
	void preloadModelAssets(AsciiString) {}
	void preloadTextureAssets(AsciiString) {}
	void takeScreenShot() {}
	void toggleMovieCapture() {}
	void toggleLetterBox() {}
	void enableLetterBox(Bool) {}
	Bool isLetterBoxed() { return TRUE; }
	const WideChar *getRendererName() const { return u"camera test"; }
	Real getAverageFPS() { return 60.0f; }
	Int getLastFrameDrawCalls() { return 0; }
};

class CinematicCameraPlayback : public RecorderClass
{
public:
	CinematicCameraPlayback()
	{
		m_mode = RECORDERMODETYPE_PLAYBACK;
		m_originalGameMode = GAME_SINGLE_PLAYER;
	}
};

class CinematicCameraText : public GameTextInterface
{
	AsciiStringVec strings;
public:
	void init() {}
	void reset() {}
	void update() {}
	UnicodeString fetch(const Char *, Bool *exists = NULL) { if (exists) *exists = true; return UnicodeString(); }
	UnicodeString fetch(AsciiString label, Bool *exists = NULL) { return fetch(label.str(), exists); }
	AsciiStringVec &getStringsWithLabelPrefix(AsciiString) { return strings; }
	void initMapStringFile(const AsciiString &) {}
};

static void cinematicCameraTestEnvironment(const char *value)
{
#if defined(_WIN32)
	_putenv_s("ZH_CINEMATIC_CAMERA_SMOOTH", value ? value : "");
#else
	if (value) setenv("ZH_CINEMATIC_CAMERA_SMOOTH", value, 1);
	else unsetenv("ZH_CINEMATIC_CAMERA_SMOOTH");
#endif
}
}

TEST(cinematic_camera_defaults_to_single_player_and_ignores_the_retired_environment_flag)
{
	CinematicCameraFixture fixture;
	const char *old = getenv("ZH_CINEMATIC_CAMERA_SMOOTH");
	const Bool hadOld = old != NULL;
	const std::string saved = old ? old : "";
	const char *values[] = {NULL, "0", "1", "off"};
	for (const char *value : values)
	{
		cinematicCameraTestEnvironment(value);
		W3DView view;
		CHECK(!CinematicCameraTestAccess::eligible(view)); // constructor does not query the session
		fixture.prepare(view); // actual production mode snapshot; no test-only enable bit
		CHECK(CinematicCameraModeForClientUpdate().allowsRenderTime());
		CHECK(CinematicCameraTestAccess::eligible(view));
		CHECK(!CinematicAnimationClockFrozen(false, true, CinematicCameraModeForClientUpdate()));
		view.zoomCamera(2.0f, 330, 0, 0);
		CinematicCameraTestAccess::pose(view, 0.0f);
		CinematicCameraTestAccess::pose(view, 0.5f);
		CHECK(CinematicCameraTestAccess::valid(view));
		CHECK(CinematicCameraTestAccess::shownZoom(view) > view.getZoom());
		CHECK_EQ(CinematicCameraTestAccess::zoomStep(view), 0);
	}
	cinematicCameraTestEnvironment(hadOld ? saved.c_str() : NULL);
}

TEST(cinematic_camera_session_snapshot_excludes_other_modes_and_clears_cached_extra_pictures)
{
	CinematicCameraFixture fixture;
	W3DView view;
	const Int excludedModes[] = {GAME_NONE, GAME_LAN, GAME_SKIRMISH, GAME_REPLAY, GAME_SHELL, GAME_INTERNET};
	for (Int gameMode : excludedModes)
	{
		fixture.logic->setGameMode(GAME_SINGLE_PLAYER);
		fixture.prepare(view);
		view.zoomCamera(2.0f, 330, 0, 0);
		CinematicCameraTestAccess::pose(view, 0.0f);
		CinematicCameraTestAccess::pose(view, 0.2f);
		CinematicCameraTestAccess::extraClock(view, 100, 0.2f, 1.0f, TRUE);
		CHECK(CinematicCameraTestAccess::valid(view));
		fixture.logic->setGameMode(gameMode);
		const CinematicAnimationClockMode mode = CinematicCameraModeForClientUpdate();
		CHECK(!mode.allowsRenderTime());
		CinematicCameraTestAccess::clientMode(view, mode);
		CHECK(!CinematicCameraTestAccess::eligible(view));
		CHECK(!CinematicCameraTestAccess::valid(view));
		CHECK(!CinematicCameraTestAccess::extraPicture(view, 110));
		CHECK_EQ(view.getZoom(), 1.0f);
		CHECK_EQ(CinematicCameraTestAccess::zoomStep(view), 0);
	}
	fixture.logic->setGameMode(GAME_SINGLE_PLAYER);
	CinematicCameraText text;
	GameTextInterface *savedText = TheGameText;
	TheGameText = &text; // Recorder's empty slot list needs labels, but no string assets.
	CinematicCameraPlayback playback;
	TheGameText = savedText;
	TheRecorder = &playback; // playback must be excluded independently of the current game mode
	CHECK(!CinematicCameraModeForClientUpdate().allowsRenderTime());
	TheRecorder = NULL;
	fixture.logic->setLoadingMap(TRUE);
	CHECK(!CinematicCameraModeForClientUpdate().allowsRenderTime());
	fixture.logic->setLoadingMap(FALSE);
	fixture.logic->setLoadingSave(TRUE);
	CHECK(!CinematicCameraModeForClientUpdate().allowsRenderTime());
	fixture.logic->setLoadingSave(FALSE);
	Bool GlobalData::*excludedSettings[] = {&GlobalData::m_headless, &GlobalData::m_loadScreenRender, &GlobalData::m_turbo};
	for (auto setting : excludedSettings)
	{
		fixture.global->*setting = TRUE;
		CHECK(!CinematicCameraModeForClientUpdate().allowsRenderTime());
		fixture.global->*setting = FALSE;
	}
	fixture.global->m_videoEndFrame = 100;
	CHECK(!CinematicCameraModeForClientUpdate().allowsRenderTime());
	fixture.global->m_videoEndFrame = 0;
	fixture.global->m_screenShotFrame = 100;
	CHECK(!CinematicCameraModeForClientUpdate().allowsRenderTime());
	fixture.global->m_screenShotFrame = 0;
	CHECK(CinematicCameraModeForClientUpdate().allowsRenderTime());
}

TEST(cinematic_camera_world_freeze_keeps_authored_motion_while_pause_and_debug_hold)
{
	CinematicCameraFixture fixture;
	W3DView view;
	fixture.prepare(view);
	view.zoomCamera(2.0f, 330, 0, 0);
	const Bool worldFreezeMoves = CinematicCameraMovesAllowed(false, false, true);
	CHECK(worldFreezeMoves); // the same mapping drives both steppers and presentation in update()
	CinematicCameraTestAccess::pose(view, 0.0f, worldFreezeMoves);
	CinematicCameraTestAccess::pose(view, 0.2f, worldFreezeMoves);
	const Real shown = CinematicCameraTestAccess::shownZoom(view);
	if (worldFreezeMoves) CinematicCameraTestAccess::step(view);
	CinematicCameraTestAccess::pose(view, 0.4f, worldFreezeMoves);
	CHECK_EQ(CinematicCameraTestAccess::zoomStep(view), 1);
	CHECK(view.getZoom() > 1.0f);
	CHECK(CinematicCameraTestAccess::shownZoom(view) > shown);
	CHECK(CinematicAnimationClockFrozen(true, true, CinematicCameraModeForClientUpdate()));
	for (Int freeze = 0; freeze < 2; ++freeze)
	{
		const Real held = CinematicCameraTestAccess::shownZoom(view);
		const Bool moves = CinematicCameraMovesAllowed(freeze == 0, freeze == 1, false);
		CHECK(!moves);
		// Session eligibility remains true while the movement sampler freezes its fractional pose.
		CinematicCameraTestAccess::clientMode(view, CinematicCameraModeForClientUpdate());
		CHECK(CinematicCameraTestAccess::eligible(view));
		CinematicCameraTestAccess::pose(view, 0.9f, moves);
		CHECK_EQ(CinematicCameraTestAccess::shownZoom(view), held);
		CHECK_EQ(CinematicCameraTestAccess::zoomStep(view), 1);
	}
}

TEST(cinematic_camera_samples_real_w3d_movers_without_changing_authoritative_steps)
{
	CinematicCameraFixture fixture;
	const Int renderRates[] = {60, 120, 144};
	const Int cameraRates[] = {22, 30};
	for (Int renderRate : renderRates)
		for (Int cameraRate : cameraRates)
		{
			W3DView view, control;
			fixture.prepare(view);
			fixture.prepare(control, FALSE);
			for (W3DView *v : {&view, &control})
			{
				v->zoomCamera(2.0f, 3300, 0, 0);
				v->pitchCamera(-0.4f, 3300, 0, 0);
				v->rotateCamera(0.25f, 3300, 0, 0);
			}
			const Int count = CinematicCameraTestAccess::count(view);
			Int step = 0, shown = 0, changedBetweenSteps = 0;
			Real previousZoom = -1.0f;
			Matrix3D previousMatrix(true);
			for (Int picture = 0; picture <= (count * renderRate + cameraRate - 1) / cameraRate; ++picture)
			{
				const double clock = (double)picture * cameraRate / renderRate;
				const Int due = (Int)floor(clock + 1.0e-9);
				const Int oldStep = step;
				while (step < due && step < count)
				{
					CinematicCameraTestAccess::step(view);
					CinematicCameraTestAccess::step(control);
					++step;
				}
				const Real phase = (Real)(clock - due);
				CinematicCameraTestAccess::pose(view, phase);
				CinematicCameraTestAccess::pose(control, phase);
				// These are the actual fields/getters consumed by script queries, saves and later actions.
				CHECK_EQ(view.getZoom(), control.getZoom());
				CHECK_EQ(view.getAngle(), control.getAngle());
				CHECK_EQ(view.getFXPitch(), control.getFXPitch());
				CHECK_EQ(view.isCameraMoving(), control.isCameraMoving());
				CHECK_EQ(CinematicCameraTestAccess::zoomStep(view), CinematicCameraTestAccess::zoomStep(control));
				CHECK_EQ(CinematicCameraTestAccess::angleStep(view), CinematicCameraTestAccess::angleStep(control));
				CHECK_EQ(CinematicCameraTestAccess::pitchStep(view), CinematicCameraTestAccess::pitchStep(control));
				const Matrix3D matrix = CinematicCameraTestAccess::matrix(view);
				CHECK_EQ(view.getZoom(), control.getZoom());
				CHECK_EQ(view.getAngle(), control.getAngle());
				CHECK_EQ(view.getFXPitch(), control.getFXPitch());
				if (step < count)
				{
					const Real zoom = CinematicCameraTestAccess::shownZoom(view);
					CHECK_NEAR(zoom, 1.0f + (Real)(clock / count), 2.0e-5f);
					if (picture > 0)
					{
						CHECK(zoom > previousZoom);
						CHECK(cinematicCameraMatricesDiffer(matrix, previousMatrix));
						if (oldStep == step) ++changedBetweenSteps;
					}
					previousZoom = zoom;
					++shown;
				}
				previousMatrix = matrix;
			}
			CHECK(shown > count);
			CHECK(changedBetweenSteps > 0); // fails if buildCameraTransform stops consuming the displayed pose
			CHECK(!view.isCameraMoving());
			CHECK_EQ(view.getZoom(), 2.0f);
			CHECK_EQ(view.getFXPitch(), -0.4f);
			CHECK_EQ(CinematicCameraTestAccess::zoomStep(view), count);
		}
}

TEST(cinematic_camera_matrix_consumes_each_display_scalar_without_changing_its_getter)
{
	CinematicCameraFixture fixture;
	for (Int channel = 0; channel < 3; ++channel)
	{
		W3DView view;
		fixture.prepare(view);
		if (channel == 0) view.zoomCamera(2.0f, 330, 0, 0);
		if (channel == 1) view.pitchCamera(-0.4f, 330, 0, 0);
		if (channel == 2) view.rotateCamera(0.25f, 330, 0, 0);
		CinematicCameraTestAccess::pose(view, 0.0f);
		const Matrix3D start = CinematicCameraTestAccess::matrix(view);
		const Real zoom = view.getZoom(), angle = view.getAngle(), pitch = view.getFXPitch();
		CinematicCameraTestAccess::pose(view, 0.5f);
		CHECK(cinematicCameraMatricesDiffer(start, CinematicCameraTestAccess::matrix(view)));
		CHECK_EQ(view.getZoom(), zoom);
		CHECK_EQ(view.getAngle(), angle);
		CHECK_EQ(view.getFXPitch(), pitch);
		CHECK(view.isCameraMoving());
	}
}

TEST(cinematic_camera_normalizes_a_new_actions_first_partial_interval_and_freezes_on_pause)
{
	CinematicCameraFixture fixture;
	W3DView view;
	fixture.prepare(view);
	view.zoomCamera(2.0f, 330, 0, 0);
	CinematicCameraTestAccess::pose(view, 0.6f);
	CHECK_EQ(CinematicCameraTestAccess::shownZoom(view), 1.0f);
	CinematicCameraTestAccess::pose(view, 0.8f);
	CHECK_NEAR(CinematicCameraTestAccess::shownZoom(view), 1.05f, 1.0e-6f);
	const Matrix3D beforePause = CinematicCameraTestAccess::matrix(view);
	for (Real phase : {0.95f, 0.1f, 0.8f, 0.3f}) CinematicCameraTestAccess::pose(view, phase, FALSE);
	CHECK(!cinematicCameraMatricesDiffer(beforePause, CinematicCameraTestAccess::matrix(view)));
	CHECK_EQ(view.getZoom(), 1.0f);
	CHECK_EQ(CinematicCameraTestAccess::zoomStep(view), 0);
	CinematicCameraTestAccess::pose(view, 0.8f); // resume without an authoritative step
	CHECK(!cinematicCameraMatricesDiffer(beforePause, CinematicCameraTestAccess::matrix(view)));
	CinematicCameraTestAccess::pose(view, 0.9f);
	CHECK_NEAR(CinematicCameraTestAccess::shownZoom(view), 1.075f, 1.0e-6f);
	CinematicCameraTestAccess::step(view);
	CinematicCameraTestAccess::pose(view, 0.0f);
	CHECK_NEAR(CinematicCameraTestAccess::shownZoom(view), 1.1f, 1.0e-6f);
	// A replacement starts from authoritative state and cannot inherit the old curve/phase.
	view.zoomCamera(0.5f, 330, 0, 0);
	CHECK(!CinematicCameraTestAccess::valid(view));
	CinematicCameraTestAccess::pose(view, 0.3f);
	CHECK_EQ(CinematicCameraTestAccess::shownZoom(view), view.getZoom());
	view.setZoom(3.0f);
	CHECK(!CinematicCameraTestAccess::valid(view));
	CHECK(!view.isCameraMoving());
	CHECK_EQ(view.getZoom(), 3.0f);
}

TEST(cinematic_camera_uses_easing_and_preserves_authored_multi_turn_arcs)
{
	CinematicCameraFixture fixture;
	W3DView view;
	fixture.prepare(view);
	view.zoomCamera(2.0f, 3300, 825, 825);
	view.rotateCamera(2.0f, 3300, 0, 0);
	CinematicCameraTestAccess::pose(view, 0.0f);
	for (Int step = 0; step < 12; ++step) CinematicCameraTestAccess::step(view);
	CinematicCameraTestAccess::pose(view, 0.5f);
	ParabolicEase ease(0.25f, 0.25f);
	CHECK_NEAR(CinematicCameraTestAccess::shownZoom(view), 1.0f + ease(0.125f), 1.0e-6f);
	CHECK_NEAR(CinematicCameraTestAccess::shownAngle(view), PI * 0.5f, 1.0e-5f);
	CHECK_NEAR(view.getAngle(), 4 * PI * 0.12f, 1.0e-5f);
	// Cross the +-pi seam while following the same authored two-turn sweep.
	for (Int step = 12; step < 25; ++step) CinematicCameraTestAccess::step(view);
	CinematicCameraTestAccess::pose(view, 0.5f);
	CHECK_NEAR(CinematicCameraTestAccess::shownAngle(view), -PI + 0.02f * PI, 1.0e-5f);
}

TEST(cinematic_camera_keeps_cuts_tracking_real_zoom_and_other_camera_owners_legacy)
{
	CinematicCameraFixture fixture;
	W3DView view;
	fixture.prepare(view);
	view.zoomCamera(2.0f, 0, 0, 0);
	CinematicCameraTestAccess::pose(view, 0.9f);
	CHECK(!CinematicCameraTestAccess::valid(view));
	CHECK_EQ(view.getZoom(), 1.0f);
	CinematicCameraTestAccess::step(view);
	CHECK_EQ(view.getZoom(), 2.0f);
	view.rotateCamera(0.25f, 330, 0, 0);
	CinematicCameraTestAccess::tracked(view);
	CinematicCameraTestAccess::pose(view, 0.5f);
	CHECK(!CinematicCameraTestAccess::valid(view));
	view.zoomCamera(3.0f, 330, 0, 0);
	CinematicCameraTestAccess::realZoom(view, TRUE);
	CinematicCameraTestAccess::pose(view, 0.5f);
	CHECK(!CinematicCameraTestAccess::valid(view));
	CinematicCameraTestAccess::realZoom(view, FALSE);
	CinematicCameraTestAccess::pose(view, 0.5f, TRUE, FALSE); // frozen-time inner loop
	CHECK(!CinematicCameraTestAccess::valid(view));
	CinematicCameraTestAccess::slave(view, TRUE);
	CinematicCameraTestAccess::pose(view, 0.5f);
	CHECK(!CinematicCameraTestAccess::valid(view));
	CinematicCameraTestAccess::slave(view, FALSE);
	CinematicCameraTestAccess::diagnostic(view, TRUE);
	CinematicCameraTestAccess::pose(view, 0.5f);
	CHECK(!CinematicCameraTestAccess::valid(view));
	CinematicCameraTestAccess::diagnostic(view, FALSE);
	CinematicCameraTestAccess::freeCamera(view, TRUE);
	CinematicCameraTestAccess::pose(view, 0.5f);
	CHECK(!CinematicCameraTestAccess::valid(view));
	CinematicCameraTestAccess::freeCamera(view, FALSE);
	view.setCameraLockDrawable((Drawable *)1); // a drawable lock owns its camera matrix directly
	CinematicCameraTestAccess::pose(view, 0.5f);
	CHECK(!CinematicCameraTestAccess::valid(view));
}

TEST(cinematic_camera_picture_phase_agrees_with_the_existing_last_picture_gate)
{
	// -recordfps 120 supplies two pictures per 60Hz logic frame. The existing camera gate advances
	// on the last picture of each even logic frame: f=0 there and .25/.5/.75 before the next gate.
	CinematicCameraProgress progress;
	Int step = 0;
	CHECK_EQ(progress.sample(0, 10, 0, false), 0.0f);
	for (Int picture = 1; picture <= 12; ++picture)
	{
		const double logicPicture = picture * 0.5;
		const double retailPicture = logicPicture / 2.0;
		if (picture % 4 == 0) ++step;
		const float phase = CinematicCameraProgress::picturePhase((float)logicPicture, (unsigned)step, 2);
		CHECK_NEAR(progress.sample(step, 10, phase, false), (float)retailPicture, 1.0e-6f);
	}
	CHECK_EQ(CinematicCameraProgress::picturePhase(9.0f, 2, 2), 1.0f); // stale gate is not hidden by modulo
	CHECK_EQ(CinematicCameraProgress::picturePhase(9.0f, ~0u, 2), 0.0f); // no gate consumed yet
}

TEST(cinematic_camera_constraint_rebuild_uses_only_the_authoritative_camera)
{
	CinematicCameraFixture fixture;
	CinematicCameraTerrain terrain;
	TheTerrainLogic = &terrain;
	fixture.global->m_useCameraConstraints = TRUE;
	fixture.global->m_cameraBoundaryMargin = 0;
	W3DView view, control;
	fixture.prepare(view);
	fixture.prepare(control, FALSE);
	for (W3DView *v : {&view, &control})
	{
		CinematicCameraTestAccess::installCamera(*v);
		v->View::setWidth(800);
		v->View::setHeight(600);
		const Coord3D outside = {-1000, -1000, 0};
		CinematicCameraTestAccess::position(*v, outside);
		v->zoomCamera(2.0f, 330, 0, 0);
		v->pitchCamera(0.2f, 330, 0, 0);
	}
	CinematicCameraTestAccess::pose(view, 0.0f);
	CinematicCameraTestAccess::pose(view, 0.95f);
	CinematicCameraTestAccess::constrain(view);
	CinematicCameraTestAccess::constrain(control);
	const Region2D a = CinematicCameraTestAccess::boundary(view), b = CinematicCameraTestAccess::boundary(control);
	CHECK_EQ(a.lo.x, b.lo.x);
	CHECK_EQ(a.lo.y, b.lo.y);
	CHECK_EQ(a.hi.x, b.hi.x);
	CHECK_EQ(a.hi.y, b.hi.y);
	const Coord3D viewPosition = CinematicCameraTestAccess::position(view);
	const Coord3D controlPosition = CinematicCameraTestAccess::position(control);
	CHECK_EQ(viewPosition.x, controlPosition.x);
	CHECK_EQ(viewPosition.y, controlPosition.y);
	CHECK_EQ(viewPosition.z, controlPosition.z);
	CHECK_EQ(view.getZoom(), control.getZoom());
	CHECK_EQ(view.getFXPitch(), control.getFXPitch());
}

TEST(cinematic_camera_extra_picture_advances_cached_phase_without_advancing_a_step)
{
	CinematicCameraFixture fixture;
	CinematicCameraDisplay display;
	TheDisplay = &display;
	W3DView view;
	fixture.prepare(view);
	CinematicCameraTestAccess::installCamera(view);
	view.View::setWidth(800);
	view.View::setHeight(600);
	view.zoomCamera(2.0f, 330, 0, 0);
	CinematicCameraTestAccess::pose(view, 0.0f);
	CinematicCameraTestAccess::pose(view, 0.2f);
	const Matrix3D before = CinematicCameraTestAccess::matrix(view);
	CinematicCameraTestAccess::extraClock(view, 100, 0.2f, 22.0f / 30.0f, TRUE);
	CHECK(CinematicCameraTestAccess::extraPicture(view, 110));
	CHECK(cinematicCameraMatricesDiffer(before, CinematicCameraTestAccess::matrix(view)));
	CHECK_EQ(view.getZoom(), 1.0f);
	CHECK_EQ(CinematicCameraTestAccess::zoomStep(view), 0);
	CinematicCameraTestAccess::extraPicture(view, 1000);
	CHECK_NEAR(CinematicCameraTestAccess::shownZoom(view), 1.1f, 1.0e-6f); // stop at next unchanged gate
	CinematicCameraTestAccess::extraClock(view, 1000, 0.0f, 1.0f, FALSE); // picture-clock/pause/frozen controls
	CHECK(!CinematicCameraTestAccess::extraPicture(view, 2000));
	CHECK_EQ(CinematicCameraTestAccess::zoomStep(view), 0);
	// Exercise the actual phase-4 entry point and its camera publication, not just the sampler.
	view.zoomCamera(3.0f, 330, 0, 0);
	CinematicCameraTestAccess::pose(view, 0.0f);
	CinematicCameraTestAccess::pose(view, 0.2f);
	view.aimCamera();
	const Matrix3D published = CinematicCameraTestAccess::published(view);
	CinematicCameraTestAccess::extraClock(view, Clock_Milliseconds() - 10, 0.2f, 22.0f / 30.0f, TRUE);
	view.updateCameraBesideTick();
	CHECK(cinematicCameraMatricesDiffer(published, CinematicCameraTestAccess::published(view)));
	CHECK_EQ(view.getZoom(), 1.0f);
	CHECK_EQ(CinematicCameraTestAccess::zoomStep(view), 0);
}

TEST(cinematic_camera_pause_resume_handles_reversed_phase_new_steps_and_completion)
{
	CinematicCameraProgress progress;
	progress.sample(0, 3, 0, false);
	CHECK_NEAR(progress.sample(0, 3, 0.8f, false), 0.8f, 1.0e-6f);
	progress.sample(0, 3, 0.1f, true);
	CHECK_NEAR(progress.sample(0, 3, 0.2f, false), 0.8f, 1.0e-6f);
	CHECK_NEAR(progress.sample(0, 3, 0.6f, false), 0.9f, 1.0e-6f);
	progress.sample(0, 3, 0.7f, true);
	CHECK_NEAR(progress.sample(1, 3, 0.8f, false), 0.9f, 1.0e-6f); // gate advanced on resume
	CHECK_NEAR(progress.sample(1, 3, 0.9f, false), 1.45f, 1.0e-6f);
	CHECK_NEAR(progress.sample(2, 3, 0.0f, false), 2.0f, 1.0e-6f);
	CinematicCameraFixture fixture;
	W3DView view;
	fixture.prepare(view);
	view.zoomCamera(2.0f, 66, 0, 0); // two authoritative steps
	CinematicCameraTestAccess::pose(view, 0.0f);
	CinematicCameraTestAccess::pose(view, 0.2f, FALSE);
	CinematicCameraTestAccess::step(view);
	CinematicCameraTestAccess::step(view);
	CinematicCameraTestAccess::pose(view, 0.8f);
	CHECK(!view.isCameraMoving());
	CHECK(!CinematicCameraTestAccess::valid(view));
	CHECK_EQ(view.getZoom(), 2.0f);
}
