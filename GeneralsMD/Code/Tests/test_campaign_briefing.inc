#include "GameClient/LoadScreen.h"
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
