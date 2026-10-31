#include "test_harness.h"
#include "Platform/RenderTiming.h"

TEST(render_timing_logic_window_parser_is_strict_and_bounded)
{
    RenderTiming::LogicWindow window;
    CHECK(window.Parse("108420,109019"));CHECK_EQ(window.Count(),600u);
    CHECK(window.Parse("4294967295,4294967295"));CHECK_EQ(window.Count(),1u);
    CHECK(window.Parse("1,100000"));
    const char *bad[]={nullptr,"","1","1,","0,1","2,1","-1,2","1,2x","1,2,3",
        " 1,2","1, 2","1,100001","4294967296,4294967296","999999999999999999999999,2"};
    for(const char *text:bad)CHECK(!window.Parse(text));
}

TEST(render_timing_logic_window_requires_predecessor_and_matches_exact_frames)
{
    RenderTiming::LogicWindow window;CHECK(window.Parse("100,699"));
    RenderTiming::LogicSequence sequence;
    CHECK(!sequence.Observe(window,false,99));
    CHECK(!sequence.Observe(window,true,98));
    CHECK(!sequence.Observe(window,true,99));
    for(unsigned frame=100;frame<=699;++frame)CHECK(sequence.Observe(window,true,frame));
    CHECK(sequence.Complete(window));CHECK_EQ(sequence.accepted,600u);CHECK_EQ(sequence.rejected,0u);
    CHECK(!sequence.Observe(window,true,700));
}

TEST(render_timing_logic_window_rejects_missing_duplicate_and_unstamped_presents)
{
    RenderTiming::LogicWindow window;CHECK(window.Parse("10,12"));
    RenderTiming::LogicSequence absent;
    CHECK(!absent.Observe(window,true,10));CHECK(!absent.Complete(window));
    RenderTiming::LogicSequence duplicate;
    duplicate.Observe(window,true,9);CHECK(duplicate.Observe(window,true,10));
    CHECK(!duplicate.Observe(window,true,10));CHECK(duplicate.Observe(window,true,11));
    CHECK(duplicate.Observe(window,true,12));CHECK(!duplicate.Complete(window));
    RenderTiming::LogicSequence skipped;
    skipped.Observe(window,true,9);CHECK(skipped.Observe(window,true,10));
    CHECK(!skipped.Observe(window,true,12));CHECK(!skipped.Complete(window));
    RenderTiming::LogicSequence unstamped;
    unstamped.Observe(window,true,9);unstamped.Observe(window,false,10);
    CHECK(!unstamped.Observe(window,true,10));CHECK(!unstamped.Complete(window));
}

TEST(render_timing_logic_window_does_not_wrap_frame_predecessor)
{
    RenderTiming::LogicWindow window;CHECK(window.Parse("1,1"));
    RenderTiming::LogicSequence sequence;
    sequence.Observe(window,true,UINT32_MAX);CHECK(!sequence.Observe(window,true,1));
    CHECK(!sequence.Complete(window));
}
