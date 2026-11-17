#include "GameClient/LoadScreen.h"
#include "Common/AudioHandleSpecialValues.h"
#include "../Stubs/NullAudioManager.h"

namespace {
UnsignedInt briefingTestNow = 0;
UnsignedInt briefingTestClock() { return briefingTestNow; }

class LoadingBriefingTestAudio : public NullAudioManager
{
public:
	Bool enabled = TRUE;
	Real volume = 1.0f;
	Real lengthMS = 1000.0f;
	Int plays = 0;
	Int measurements = 0;
	UnsignedInt openingCostMS = 0;
	AsciiString playedName;
	Bool isOn(AudioAffect) const override { return enabled; }
	Real getVolume(AudioAffect) override { return volume; }
	Real getAudioLengthMS(const AudioEventRTS *) override { ++measurements; return lengthMS; }
	void friend_forcePlayAudioEventRTS(const AudioEventRTS *event) override
	{
		++plays;
		playedName = event->getEventName();
		briefingTestNow += openingCostMS;
	}
};

class QueuedLoadingTestAudio : public LoadingBriefingTestAudio
{
public:
	AudioHandle handle = AHSV_FirstHandle + 10;
	AudioHandle removed = AHSV_Error;
	AudioHandle result = AHSV_FirstHandle;
	Int adds = 0, removes = 0, overlappingAdds = 0;
	Bool active = FALSE;
	Bool stopImmediately = TRUE;
	std::vector<AsciiString> order;
	AudioHandle addAudioEvent(const AudioEventRTS *event) override
	{
		++adds;
		if (active) ++overlappingAdds;
		playedName = event->getEventName();
		order.push_back(playedName);
		briefingTestNow += openingCostMS;
		if (result < AHSV_FirstHandle) return result;
		active = TRUE;
		return ++handle;
	}
	Bool isCurrentlyPlaying(AudioHandle queried) override { return active && queried == handle; }
	void removeAudioEvent(AudioHandle owned) override
	{
		removed = owned;
		++removes;
		if (stopImmediately && owned == handle) active = FALSE;
	}
};
}

TEST(loading_speech_queue_serializes_lines_while_assets_are_loading)
{
	CHECK(bootOnce());
	QueuedLoadingTestAudio audio;
	LoadingSpeechQueue queue(briefingTestClock);
	briefingTestNow = 0;
	queue.enqueue(AudioEventRTS("GeneralName"));
	queue.enqueue(AudioEventRTS("Versus"));
	queue.update(audio);
	CHECK_EQ(audio.adds, 1);
	CHECK_STR(audio.playedName.str(), "GeneralName");
	CHECK(!queue.isFinished());
	// The caller keeps loading assets. An active or backend-queued line must
	// neither block update() nor allow the next line to overlap it.
	briefingTestNow = 100000;
	queue.update(audio);
	CHECK_EQ(audio.adds, 1);
	CHECK_EQ(audio.removes, 0);
	CHECK_EQ(queue.displayProgress(40), 40);
	queue.enqueue(AudioEventRTS("Taunts_Toxin098"));
	queue.seal();
	audio.active = FALSE;
	queue.update(audio);
	CHECK_EQ(audio.adds, 2);
	CHECK_STR(audio.playedName.str(), "Versus");
	queue.markAssetsReady();
	CHECK(!queue.isFinished());
	CHECK_EQ(queue.displayProgress(100), 99);
	queue.update(audio);
	CHECK_EQ(audio.adds, 2);
	audio.active = FALSE;
	queue.update(audio);
	CHECK_EQ(audio.adds, 3);
	CHECK_STR(audio.playedName.str(), "Taunts_Toxin098");
	CHECK(!queue.isFinished());
	audio.active = FALSE;
	queue.update(audio);
	CHECK(queue.isFinished());
	CHECK(!queue.hasPending());
	CHECK_EQ(queue.displayProgress(100), 100);
	CHECK_EQ(audio.overlappingAdds, 0);
	CHECK_EQ(audio.removes, 0); // every valid line ended naturally
}

TEST(loading_speech_queue_joins_asset_readiness_with_a_closed_empty_queue_once)
{
	CHECK(bootOnce());
	QueuedLoadingTestAudio audio;
	LoadingSpeechQueue queue(briefingTestClock);
	queue.enqueue(AudioEventRTS("LoadingVoice"));
	queue.update(audio);
	audio.active = FALSE;
	queue.update(audio);
	CHECK(!queue.hasPending());
	CHECK(!queue.isFinished()); // speech ended before the assets were ready
	queue.markAssetsReady();
	CHECK(!queue.isFinished()); // another cue could still be enqueued
	queue.seal();
	CHECK(queue.isFinished());
	queue.enqueue(AudioEventRTS("MustNotReplay"));
	queue.seal();
	queue.markAssetsReady();
	queue.update(audio);
	CHECK_EQ(audio.adds, 1);
	CHECK(queue.isFinished());
	queue.reset(audio);
	CHECK(!queue.isFinished());
	queue.enqueue(AudioEventRTS("NextMission"));
	queue.update(audio);
	CHECK_EQ(audio.adds, 2);
	queue.cancel(audio);
	CHECK_EQ(audio.removed, audio.handle);
	CHECK(!queue.hasPending());
	queue.cancel(audio);
	CHECK_EQ(audio.removes, 1);
}

TEST(loading_speech_queue_retires_stalled_requests_only_after_assets_and_stop_complete)
{
	CHECK(bootOnce());
	QueuedLoadingTestAudio audio;
	audio.stopImmediately = FALSE;
	LoadingSpeechQueue queue(briefingTestClock);
	briefingTestNow = 0;
	queue.enqueue(AudioEventRTS("StalledVoice"));
	queue.enqueue(AudioEventRTS("NextVoice"));
	queue.seal();
	queue.update(audio);
	briefingTestNow = 0xfffffff0u;
	queue.update(audio);
	CHECK_EQ(audio.removes, 0); // long loader work cannot expire a pending request
	queue.markAssetsReady();
	queue.update(audio); // start the final gate's budget now
	briefingTestNow = 1983;
	queue.update(audio);
	CHECK_EQ(audio.removes, 0);
	briefingTestNow = 1984; // 1000ms voice + 1000ms startup, across clock wrap
	queue.update(audio);
	CHECK_EQ(audio.removes, 1);
	CHECK_EQ(audio.removed, audio.handle);
	CHECK_EQ(audio.adds, 1); // the queued stop has not taken effect yet
	CHECK(!queue.isFinished());
	queue.update(audio);
	CHECK_EQ(audio.removes, 1);
	audio.active = FALSE;
	queue.update(audio);
	CHECK_EQ(audio.adds, 2);
	CHECK_EQ(audio.overlappingAdds, 0);
	audio.active = FALSE;
	queue.update(audio);
	CHECK(queue.isFinished());
}

TEST(loading_speech_queue_skips_missing_muted_and_failed_cues)
{
	CHECK(bootOnce());
	QueuedLoadingTestAudio audio;
	LoadingSpeechQueue queue(briefingTestClock);
	queue.enqueue(AudioEventRTS());
	queue.enqueue(AudioEventRTS("Missing"), 0);
	queue.enqueue(AudioEventRTS("Invalid"), std::numeric_limits<Real>::quiet_NaN());
	queue.update(audio);
	CHECK_EQ(audio.adds, 0);
	audio.result = AHSV_NoSound;
	queue.enqueue(AudioEventRTS("Failed"));
	queue.seal();
	queue.markAssetsReady();
	queue.update(audio);
	CHECK_EQ(audio.adds, 1);
	CHECK(queue.isFinished());
	CHECK_EQ(audio.removes, 0);
	for (Int muted = 0; muted < 2; ++muted)
	{
		queue.reset(audio);
		audio.enabled = muted != 0;
		audio.volume = muted == 0 ? 1.0f : 0.0f;
		queue.enqueue(AudioEventRTS("Muted"));
		queue.seal();
		queue.markAssetsReady();
		queue.update(audio);
		CHECK_EQ(audio.adds, 1);
		CHECK(queue.isFinished());
	}
}

TEST(loading_speech_queue_waits_for_a_muted_active_cue_to_stop_before_completing)
{
	CHECK(bootOnce());
	QueuedLoadingTestAudio audio;
	audio.stopImmediately = FALSE;
	LoadingSpeechQueue queue(briefingTestClock);
	queue.enqueue(AudioEventRTS("ActiveVoice"));
	queue.enqueue(AudioEventRTS("PendingVoice"));
	queue.update(audio);
	queue.seal();
	queue.markAssetsReady();
	audio.volume = 0;
	queue.update(audio);
	CHECK_EQ(audio.removes, 1);
	CHECK(!queue.isFinished());
	CHECK_EQ(audio.adds, 1);
	audio.active = FALSE;
	queue.update(audio);
	CHECK(queue.isFinished());
	CHECK_EQ(audio.adds, 1);
}

TEST(campaign_loading_briefing_uses_the_shared_queue_and_actual_completion)
{
	CHECK(bootOnce());
	QueuedLoadingTestAudio audio;
	LoadingSpeechQueue queue(briefingTestClock);
	CampaignBriefingPlayback briefing(briefingTestClock);
	briefing.setQueue(queue);
	queue.enqueue(AudioEventRTS("EarlierLoadingCue"));
	queue.update(audio);
	briefing.update(audio, AudioEventRTS("BriefingGLA01"), 26, 255);
	CHECK_EQ(audio.plays, 0);
	CHECK_EQ(audio.adds, 1);
	briefing.update(audio, AudioEventRTS("BriefingGLA01"), 26, 300);
	audio.active = FALSE;
	queue.update(audio);
	CHECK_EQ(audio.adds, 2);
	CHECK_STR(audio.playedName.str(), "BriefingGLA01");
	queue.seal();
	queue.markAssetsReady();
	queue.update(audio);
	CHECK(!queue.isFinished());
	audio.active = FALSE;
	queue.update(audio);
	CHECK(queue.isFinished()); // no fixed metadata wait after playback actually ends
	CHECK_EQ(audio.overlappingAdds, 0);
}

TEST(campaign_loading_briefing_requests_its_configured_voice_once_at_or_after_frame_255)
{
	CHECK(bootOnce());
	LoadingBriefingTestAudio audio;
	audio.openingCostMS = 400;
	AudioEventRTS voice("BriefingUSA01");
	CampaignBriefingPlayback briefing(briefingTestClock);
	briefingTestNow = 8400;
	briefing.update(audio, voice, 2, 254);
	CHECK_EQ(audio.plays, 0);
	CHECK_EQ(audio.measurements, 0);
	CHECK_EQ(briefing.remainingMS(), 0u);
	briefingTestNow = 8500;
	briefing.update(audio, voice, 2, 255);
	CHECK_EQ(audio.plays, 1);
	CHECK_STR(audio.playedName.str(), "BriefingUSA01");
	CHECK_EQ(briefing.remainingMS(), 2000u); // opening time is not subtracted from the spoken budget
	briefing.update(audio, voice, 2, 255);
	briefing.update(audio, voice, 2, 270);
	CHECK_EQ(audio.plays, 1);
	CHECK_EQ(audio.measurements, 1);
	briefingTestNow = 10899;
	CHECK_EQ(briefing.remainingMS(), 1u);
	briefingTestNow = 10900;
	CHECK_EQ(briefing.remainingMS(), 0u);
	briefing.reset();
	briefing.update(audio, voice, 2, 254);
	briefing.update(audio, voice, 2, 267); // a decoder that skips frame 255 still cues once
	CHECK_EQ(audio.plays, 2);
}

TEST(campaign_loading_briefing_keeps_long_localized_voices_past_movie_end)
{
	CHECK(bootOnce());
	LoadingBriefingTestAudio audio;
	audio.lengthMS = 25406.25f;
	AudioEventRTS voice("BriefingGLA01");
	CampaignBriefingPlayback briefing(briefingTestClock);
	briefingTestNow = 8500;
	briefing.update(audio, voice, 26, 255);
	CHECK_EQ(briefing.remainingMS(), 26000u);
	briefingTestNow = 26667; // the 800-frame movie ends before this narration budget does
	CHECK_EQ(briefing.remainingMS(), 7833u);
	briefingTestNow = 34499;
	CHECK_EQ(briefing.remainingMS(), 1u);
	briefingTestNow = 34500;
	CHECK_EQ(briefing.remainingMS(), 0u);
	briefing.reset();
	briefing.update(audio, voice, 17, 300);
	CHECK_EQ(briefing.remainingMS(), 25407u); // the resolved localized WAV can exceed metadata
	briefing.reset();
	briefingTestNow = 0xfffffff0U;
	audio.lengthMS = 1000;
	briefing.update(audio, voice, 1, 255);
	briefingTestNow = 983;
	CHECK_EQ(briefing.remainingMS(), 1u);
	briefingTestNow = 984;
	CHECK_EQ(briefing.remainingMS(), 0u);
}

TEST(campaign_loading_briefing_handles_min_spec_and_video_failure_without_an_extra_movie_cue)
{
	CHECK(bootOnce());
	LoadingBriefingTestAudio audio;
	AudioEventRTS voice("BriefingUSA01");
	CampaignBriefingPlayback briefing(briefingTestClock);
	briefingTestNow = 0;
	briefing.update(audio, voice, 17, 0, TRUE); // the common min-spec/missing-video fallback
	CHECK_EQ(audio.plays, 1);
	CHECK_EQ(briefing.remainingMS(), 17000u);
	briefing.update(audio, voice, 17, 255);
	CHECK_EQ(audio.plays, 1);
	briefing.cancel(); // inactive or quitting: do not hold the loading screen or start another cue
	CHECK_EQ(briefing.remainingMS(), 0u);
	briefing.update(audio, voice, 17, 300);
	CHECK_EQ(audio.plays, 1);
	briefing.reset();
	briefing.cancel();
	briefing.update(audio, voice, 17, 0, TRUE);
	CHECK_EQ(audio.plays, 1);
	CHECK_EQ(briefing.remainingMS(), 0u);
}

TEST(campaign_loading_briefing_adds_no_voice_or_wait_for_empty_or_muted_missions)
{
	CHECK(bootOnce());
	LoadingBriefingTestAudio audio;
	AudioEventRTS emptyVoice, voice("BriefingUSA01");
	CampaignBriefingPlayback briefing(briefingTestClock);
	briefingTestNow = 0;
	briefing.update(audio, emptyVoice, 26, 255);
	briefing.update(audio, emptyVoice, 26, 0, TRUE);
	CHECK_EQ(audio.plays, 0);
	CHECK_EQ(audio.measurements, 0);
	CHECK_EQ(briefing.remainingMS(), 0u);
	audio.enabled = FALSE;
	briefing.update(audio, voice, 26, 255);
	CHECK_EQ(audio.plays, 0);
	CHECK_EQ(briefing.remainingMS(), 0u);
	briefing.reset();
	audio.enabled = TRUE;
	audio.volume = 0;
	briefing.update(audio, voice, 26, 0, TRUE);
	CHECK_EQ(audio.plays, 0);
	CHECK_EQ(audio.measurements, 0);
	CHECK_EQ(briefing.remainingMS(), 0u);
}
