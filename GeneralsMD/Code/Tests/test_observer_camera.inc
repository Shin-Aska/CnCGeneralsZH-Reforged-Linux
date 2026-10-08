// The observer camera's director and its pan, included into test_gameengine.cpp.

#include "GameClient/ObserverCamera.h"

static DirectorHeat directorHit( Real x, Real y )
{
	DirectorHeat hit;
	hit.position.x = x;
	hit.position.y = y;
	hit.weight = 1.0f;
	return hit;
}

TEST(the_director_goes_to_the_crowded_fight_not_the_lone_hit)
{
	std::vector< DirectorHeat > hits;
	for( Int each = 0; each < 5; each++ )
		hits.push_back( directorHit( 1000.0f + each * 20.0f, 1000.0f ) );
	hits.push_back( directorHit( 3000.0f, 3000.0f ) );
	hits.push_back( directorHit( 3010.0f, 3000.0f ) );

	Coord2D place;
	Real heat = 0.0f;
	CHECK( ObserverCamera_hottestPlace( hits, &place, &heat ) );
	CHECK_NEAR( heat, 5.0f, 0.001f );
	CHECK_NEAR( place.x, 1040.0f, 0.001f );
	CHECK_NEAR( place.y, 1000.0f, 0.001f );

	std::vector< DirectorHeat > none;
	CHECK( !ObserverCamera_hottestPlace( none, &place, &heat ) );
}

TEST(the_director_holds_a_live_fight_and_leaves_a_dead_one)
{
	const UnsignedInt longHeld = 15 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt justArrived = LOGICFRAMES_PER_SECOND;
	CHECK( ObserverCamera_shouldMove( 0.0f, 1.0f, justArrived ) );
	CHECK( !ObserverCamera_shouldMove( 0.0f, 0.0f, longHeld ) );
	CHECK( !ObserverCamera_shouldMove( 4.0f, 20.0f, justArrived ) );
	CHECK( !ObserverCamera_shouldMove( 4.0f, 7.0f, longHeld ) );
	CHECK( ObserverCamera_shouldMove( 4.0f, 9.0f, longHeld ) );
	// ten seconds in is still inside the hold
	CHECK( !ObserverCamera_shouldMove( 4.0f, 9.0f, 10 * LOGICFRAMES_PER_SECOND ) );
}

TEST(the_director_leaves_early_for_a_much_bigger_fight_and_late_for_any_bigger_one)
{
	const UnsignedInt settled = 7 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt tired = 35 * LOGICFRAMES_PER_SECOND;
	CHECK( ObserverCamera_shouldMove( 4.0f, 17.0f, settled ) );
	CHECK( !ObserverCamera_shouldMove( 4.0f, 13.0f, settled ) );
	// not even a much bigger fight pulls it away in its first seconds
	CHECK( !ObserverCamera_shouldMove( 4.0f, 40.0f, 3 * LOGICFRAMES_PER_SECOND ) );
	CHECK( ObserverCamera_shouldMove( 4.0f, 5.4f, tired ) );
	CHECK( !ObserverCamera_shouldMove( 4.0f, 5.0f, tired ) );
}

TEST(the_director_shows_a_superweapon_leaving_its_silo_then_where_it_lands)
{
	DirectorEvent nuke;
	nuke.id = 1;
	nuke.owner = NULL;
	nuke.source.x = 500.0f;
	nuke.source.y = 500.0f;
	nuke.target.x = 3000.0f;
	nuke.target.y = 2000.0f;
	nuke.since = 1000;
	nuke.until = 2000;
	nuke.weight = 5.0f;
	nuke.superweapon = TRUE;
	CHECK_NEAR( ObserverCamera_eventPlace( nuke, 1001 ).x, 500.0f, 0.001f );
	CHECK_NEAR( ObserverCamera_eventPlace( nuke, 1000 + 10 * LOGICFRAMES_PER_SECOND ).x, 3000.0f, 0.001f );

	// a bomber's strike has nothing worth seeing where it was ordered from
	DirectorEvent strike = nuke;
	strike.superweapon = FALSE;
	CHECK_NEAR( ObserverCamera_eventPlace( strike, 1001 ).x, 3000.0f, 0.001f );
}

TEST(the_director_goes_where_more_is_at_stake_not_where_more_was_hit)
{
	// six rifles plinked at one end, two Overlords killed at the other
	std::vector< DirectorHeat > hits;
	for( Int each = 0; each < 6; each++ )
		hits.push_back( directorHit( 1000.0f + each * 10.0f, 1000.0f ) );
	for( Int each = 0; each < 2; each++ )
	{
		DirectorHeat hit = directorHit( 3000.0f + each * 10.0f, 3000.0f );
		hit.weight = ObserverCamera_hitWeight( 2000, TRUE, FALSE );
		hits.push_back( hit );
	}
	for( Int each = 0; each < 6; each++ )
		hits[ each ].weight = ObserverCamera_hitWeight( 225, FALSE, FALSE );

	Coord2D place;
	Real heat = 0.0f;
	CHECK( ObserverCamera_hottestPlace( hits, &place, &heat ) );
	CHECK_NEAR( place.y, 3000.0f, 0.001f );
	CHECK( ObserverCamera_hitWeight( 5000, FALSE, TRUE ) > ObserverCamera_hitWeight( 5000, FALSE, FALSE ) );
}

TEST(the_director_with_no_fight_goes_round_the_sights_and_never_parks)
{
	std::vector< DirectorHeat > sights;
	DirectorHeat army = directorHit( 1000.0f, 1000.0f );
	army.weight = ObserverCamera_sightWeight( 4000, FALSE, TRUE, FALSE );
	DirectorHeat base = directorHit( 3000.0f, 3000.0f );
	base.weight = ObserverCamera_sightWeight( 6000, TRUE, FALSE, FALSE );
	DirectorHeat silo = directorHit( 5000.0f, 1000.0f );
	silo.weight = ObserverCamera_sightWeight( 5000, TRUE, FALSE, TRUE );
	sights.push_back( base );
	sights.push_back( army );
	sights.push_back( silo );

	// the marching army first, then the superweapon, then the base, then nothing new
	std::vector< Coord2D > seen;
	Coord2D place;
	CHECK( ObserverCamera_nextSight( sights, seen, &place ) );
	CHECK_NEAR( place.x, 1000.0f, 0.001f );
	seen.push_back( place );
	CHECK( ObserverCamera_nextSight( sights, seen, &place ) );
	CHECK_NEAR( place.x, 5000.0f, 0.001f );
	seen.push_back( place );
	CHECK( ObserverCamera_nextSight( sights, seen, &place ) );
	CHECK_NEAR( place.x, 3000.0f, 0.001f );
	seen.push_back( place );
	CHECK( !ObserverCamera_nextSight( sights, seen, &place ) );

	// with the oldest dropped the army comes round again
	seen.erase( seen.begin() );
	CHECK( ObserverCamera_nextSight( sights, seen, &place ) );
	CHECK_NEAR( place.x, 1000.0f, 0.001f );
}

static DirectorEvent directorEvent( UnsignedInt id, Real weight, Bool superweapon )
{
	DirectorEvent event;
	event.id = id;
	event.owner = NULL;
	event.source.x = event.source.y = 500.0f;
	event.target.x = 3000.0f;
	event.target.y = 2000.0f;
	event.since = 1000;
	event.until = 2000;
	event.weight = weight;
	event.superweapon = superweapon;
	return event;
}

TEST(the_director_keeps_its_event_when_none_may_be_shown_any_more)
{
	// narrowed to a player whose things at the target all died, the event it shows is filtered out
	// and nothing is left to compare it with; this read a NULL best and crashed
	const DirectorEvent nuke = directorEvent( 1, 5.0f, TRUE );
	CHECK( ObserverCamera_stayOnEvent( &nuke, NULL, 0 ) );
	CHECK( ObserverCamera_stayOnEvent( &nuke, &nuke, 0 ) );
	CHECK( !ObserverCamera_stayOnEvent( NULL, &nuke, 0 ) );
	CHECK( !ObserverCamera_stayOnEvent( NULL, NULL, 0 ) );

	// a bomber's strike gives way to a superweapon once it has settled, not before
	const DirectorEvent strike = directorEvent( 2, 1.0f, FALSE );
	CHECK( ObserverCamera_stayOnEvent( &strike, &nuke, LOGICFRAMES_PER_SECOND ) );
	CHECK( !ObserverCamera_stayOnEvent( &strike, &nuke, 7 * LOGICFRAMES_PER_SECOND ) );
}

TEST(a_superweapon_leaving_its_silo_takes_the_camera_before_the_settle_is_up)
{
	const DirectorEvent nuke = directorEvent( 1, 5.0f, TRUE );
	const DirectorEvent strike = directorEvent( 2, 1.0f, FALSE );
	const UnsignedInt justArrived = LOGICFRAMES_PER_SECOND;
	CHECK( ObserverCamera_eventCutsIn( nuke, 1001, justArrived ) );
	CHECK( !ObserverCamera_eventCutsIn( strike, 1001, justArrived ) );
	// once the missile is out of the silo the nuke waits for the settle like anything else
	CHECK( !ObserverCamera_eventCutsIn( nuke, 1000 + 5 * LOGICFRAMES_PER_SECOND, justArrived ) );
	CHECK( ObserverCamera_eventCutsIn( strike, 1001, 7 * LOGICFRAMES_PER_SECOND ) );
}

TEST(the_director_rises_over_a_wide_fight_and_not_over_a_tight_one)
{
	std::vector< DirectorHeat > tight;
	std::vector< DirectorHeat > wide;
	for( Int each = 0; each < 4; each++ )
	{
		tight.push_back( directorHit( 1000.0f + ( each - 2 ) * 10.0f, 1000.0f ) );
		wide.push_back( directorHit( 1000.0f + ( each % 2 ? 150.0f : -150.0f ), 1000.0f ) );
	}
	Coord2D middle;
	middle.x = middle.y = 1000.0f;
	CHECK_NEAR( ObserverCamera_spreadAround( wide, middle ), 150.0f, 0.01f );
	CHECK_NEAR( ObserverCamera_fightHeight( ObserverCamera_spreadAround( tight, middle ) ), 0.0f, 0.001f );
	CHECK_NEAR( ObserverCamera_fightHeight( ObserverCamera_spreadAround( wide, middle ) ), 135.0f, 0.01f );
	CHECK( ObserverCamera_fightHeight( 10000.0f ) <= 300.0f );
	std::vector< DirectorHeat > none;
	CHECK_NEAR( ObserverCamera_spreadAround( none, middle ), 0.0f, 0.001f );
}

TEST(the_director_stops_short_of_a_place_that_shows_ground_off_the_map)
{
	// the screen's ground runs 300 to each side, 200 below the point looked at and 500 above it
	Coord2D corners[ 4 ];
	corners[ 0 ].x = -300.0f; corners[ 0 ].y = 500.0f;
	corners[ 1 ].x = 300.0f; corners[ 1 ].y = 500.0f;
	corners[ 2 ].x = -300.0f; corners[ 2 ].y = -200.0f;
	corners[ 3 ].x = 300.0f; corners[ 3 ].y = -200.0f;
	Region2D map;
	map.lo.x = map.lo.y = 0.0f;
	map.hi.x = map.hi.y = 4000.0f;

	Coord2D place;
	place.x = 2000.0f;
	place.y = 2000.0f;
	CHECK_NEAR( ObserverCamera_keepInMap( place, corners, 4, map ).x, 2000.0f, 0.001f );

	// a fight in the map's corner is shown from as near it as the screen allows
	place.x = 50.0f;
	place.y = 3900.0f;
	const Coord2D kept = ObserverCamera_keepInMap( place, corners, 4, map );
	CHECK_NEAR( kept.x, 300.0f, 0.001f );
	CHECK_NEAR( kept.y, 3500.0f, 0.001f );

	// a map narrower than the screen: the place stays between the two edges' limits
	map.hi.x = 400.0f;
	place.x = 50.0f;
	CHECK_NEAR( ObserverCamera_keepInMap( place, corners, 4, map ).x, 100.0f, 0.001f );
}

static ObserverCameraVelocity atRest( void )
{
	ObserverCameraVelocity velocity;
	velocity.x = velocity.y = velocity.z = velocity.angle = velocity.pitch = velocity.zoom = 0.0f;
	return velocity;
}

TEST(the_observer_camera_pans_a_short_way_and_cuts_a_long_one)
{
	ViewLocation from;
	from.init( 1000.0f, 1000.0f, 0.0f, 3.0f, 0.5f, 1.0f );
	// not "near" and "far": windef.h defines both away to nothing
	ViewLocation beside;
	beside.init( 1100.0f, 1000.0f, 0.0f, -3.0f, 0.5f, 1.0f );
	ViewLocation across;
	across.init( 3000.0f, 1000.0f, 0.0f, 3.0f, 0.5f, 1.0f );

	ObserverCameraVelocity velocity = atRest();
	const ViewLocation still = ObserverCamera_approach( from, beside, 0.0f, 0.2f, 900.0f, &velocity );
	CHECK_NEAR( still.getPosition().x, 1000.0f, 0.001f );

	const ViewLocation step = ObserverCamera_approach( from, beside, 0.1f, 0.2f, 900.0f, &velocity );
	CHECK( step.getPosition().x > 1000.0f && step.getPosition().x < 1100.0f );
	// 3 to -3 is the short way through the back, not six radians round the front
	CHECK( fabs( step.getAngle() ) > 3.0f );

	velocity = atRest();
	const ViewLocation cut = ObserverCamera_approach( from, across, 0.01f, 0.2f, 900.0f, &velocity );
	CHECK_NEAR( cut.getPosition().x, 3000.0f, 0.001f );
	CHECK_NEAR( velocity.x, 0.0f, 0.001f );
}

TEST(the_observer_camera_eases_in_holds_its_top_speed_and_settles_without_overshoot)
{
	ViewLocation from;
	from.init( 1000.0f, 1000.0f, 0.0f, 0.0f, 0.5f, 1.0f );
	ViewLocation to;
	to.init( 2200.0f, 1000.0f, 0.0f, 0.0f, 0.5f, 1.0f );

	const Real tick = 1.0f / 30.0f;
	const Real topSpeed = 900.0f;
	ObserverCameraVelocity velocity = atRest();
	ViewLocation at = from;
	Real lastStep = 0.0f;
	Real firstStep = 0.0f;
	Bool overshot = FALSE;
	Bool tooFast = FALSE;
	for( Int each = 0; each < 30 * 10; each++ )
	{
		const ViewLocation next = ObserverCamera_approach( at, to, tick, 1.4f, topSpeed, &velocity );
		const Real moved = next.getPosition().x - at.getPosition().x;
		if( each == 0 )
			firstStep = moved;
		tooFast = tooFast || moved > topSpeed * tick;
		overshot = overshot || next.getPosition().x > 2200.0f + 0.01f;
		lastStep = moved;
		at = next;
	}
	// from rest the first step is a small fraction of the speed it reaches
	CHECK( firstStep < 0.25f * topSpeed * tick );
	CHECK( !tooFast );
	CHECK( !overshot );
	CHECK_NEAR( at.getPosition().x, 2200.0f, 1.0f );
	CHECK( lastStep < 1.0f );
}

TEST(the_recording_splits_for_a_second_fight_far_from_the_first_and_never_for_the_same_one)
{
	Coord2D first;
	first.x = first.y = 1000.0f;
	std::vector< DirectorHeat > hits;
	for( Int each = 0; each < 6; each++ )
		hits.push_back( directorHit( 1000.0f + each * 20.0f, 1000.0f ) );
	// a hit 500 away is near enough that the two halves would show the same ground
	hits.push_back( directorHit( 1500.0f, 1000.0f ) );

	Coord2D second;
	Real heat = 0.0f;
	const Real needed = 660.0f;
	CHECK( !ObserverCamera_secondPlace( hits, first, needed, &second, &heat ) );

	for( Int each = 0; each < 3; each++ )
		hits.push_back( directorHit( 3000.0f, 3000.0f + each * 20.0f ) );
	CHECK( ObserverCamera_secondPlace( hits, first, needed, &second, &heat ) );
	CHECK_NEAR( heat, 3.0f, 0.001f );
	CHECK_NEAR( second.x, 3000.0f, 0.001f );
	CHECK_NEAR( second.y, 3020.0f, 0.001f );
	// a picture that spans more ground wants the second fight further off
	CHECK( !ObserverCamera_secondPlace( hits, first, 3000.0f, &second, &heat ) );
}

TEST(the_recording_splits_for_a_big_second_fight_holds_it_and_rests_after)
{
	const UnsignedInt rested = 5 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt justJoined = 2 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt longHeld = 8 * LOGICFRAMES_PER_SECOND;
	const Real farApart = 2000.0f;
	const Real needed = 660.0f;
	// on for a second fight hot on its own and at least half the first
	CHECK( ObserverCamera_holdSplit( FALSE, 6.0f, 3.0f, farApart, needed, rested ) );
	CHECK( !ObserverCamera_holdSplit( FALSE, 8.0f, 3.0f, farApart, needed, rested ) );
	CHECK( !ObserverCamera_holdSplit( FALSE, 1.0f, 1.5f, farApart, needed, rested ) );
	// not straight back on after it went off
	CHECK( !ObserverCamera_holdSplit( FALSE, 6.0f, 3.0f, farApart, needed, 2 * LOGICFRAMES_PER_SECOND ) );
	// held a few seconds while the second fight goes on at all, then only while it keeps a quarter
	CHECK( ObserverCamera_holdSplit( TRUE, 6.0f, 0.5f, farApart, needed, justJoined ) );
	CHECK( ObserverCamera_holdSplit( TRUE, 8.0f, 2.0f, farApart, needed, longHeld ) );
	CHECK( !ObserverCamera_holdSplit( TRUE, 8.0f, 1.9f, farApart, needed, longHeld ) );
}

TEST(the_recording_split_ends_at_once_when_the_two_halves_show_one_fight)
{
	const UnsignedInt justJoined = 2 * LOGICFRAMES_PER_SECOND;
	const Real needed = 660.0f;
	// the director moved onto the second fight: both halves would show the same ground
	CHECK( !ObserverCamera_holdSplit( TRUE, 12.0f, 8.0f, 60.0f, needed, justJoined ) );
	CHECK( !ObserverCamera_holdSplit( FALSE, 6.0f, 6.0f, 600.0f, needed, 5 * LOGICFRAMES_PER_SECOND ) );
}

TEST(a_split_once_on_stays_until_its_panes_are_in_and_held_six_seconds)
{
	const Real needed = 660.0f;
	// in through the radar's slide and the panes' and held six seconds, whatever the second fight does:
	// one that went off on the next scan slid a pane in and straight back out
	const UnsignedInt least = 12 + 15 + 6 * LOGICFRAMES_PER_SECOND;
	CHECK( ObserverCamera_holdSplit( TRUE, 6.0f, 0.0f, 2000.0f, needed, 15 ) );
	CHECK( ObserverCamera_holdSplit( TRUE, 6.0f, 0.0f, 2000.0f, needed, least - 1 ) );
	CHECK( !ObserverCamera_holdSplit( TRUE, 6.0f, 0.0f, 2000.0f, needed, least ) );
}

TEST(a_split_wants_its_fights_further_apart_the_more_ground_a_pane_shows)
{
	const UnsignedInt rested = 5 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt longHeld = 8 * LOGICFRAMES_PER_SECOND;
	// panes 1650 across: a split opens past 1650, and a held one stays past 1100
	const Real needed = 1650.0f;
	CHECK( !ObserverCamera_holdSplit( FALSE, 6.0f, 6.0f, 1600.0f, needed, rested ) );
	CHECK( ObserverCamera_holdSplit( FALSE, 6.0f, 6.0f, 1700.0f, needed, rested ) );
	CHECK( ObserverCamera_holdSplit( TRUE, 6.0f, 6.0f, 1200.0f, needed, longHeld ) );
	CHECK( !ObserverCamera_holdSplit( TRUE, 6.0f, 6.0f, 1050.0f, needed, 15 ) );
	// a picture 460 across whose pane circle reaches 100: a fight fitted into 172 zooms the pane out
	// 1.72 times, a smaller one leaves it at the director's zoom, a huge one stops at three times
	CHECK_NEAR( ObserverCamera_paneGround( 460.0f, 100.0f, 172.0f ), 791.2f, 0.01f );
	CHECK_NEAR( ObserverCamera_paneGround( 460.0f, 100.0f, 60.0f ), 460.0f, 0.01f );
	CHECK_NEAR( ObserverCamera_paneGround( 460.0f, 100.0f, 1000.0f ), 1380.0f, 0.01f );
}

TEST(the_recording_splits_only_beside_a_real_fight)
{
	// bases going up are no fight: the split's hits are only one side's on another, so the director
	// looking at a sight has no heat of its own and a second fight does not split the picture
	CHECK( !ObserverCamera_holdSplit( FALSE, 0.0f, 10.0f, 2000.0f, 660.0f, 5 * LOGICFRAMES_PER_SECOND ) );
	// and with no fight anywhere there is no second place to show
	std::vector< DirectorHeat > none;
	Coord2D first;
	first.x = first.y = 1000.0f;
	Coord2D second;
	Real heat = 0.0f;
	CHECK( !ObserverCamera_secondPlace( none, first, 660.0f, &second, &heat ) );
}

TEST(every_pane_layout_cuts_on_45_degree_rays)
{
	CHECK_EQ( ObserverCamera_paneLayout( 1, NULL ), 0 );
	for( Int count = 2; count <= OBSERVER_MOST_PANES; count++ )
	{
		Real rays[ OBSERVER_MOST_PANES ];
		CHECK_EQ( ObserverCamera_paneLayout( count, rays ), count );
		for( Int ray = 0; ray < count; ray++ )
		{
			CHECK_EQ( (Int)rays[ ray ] % 45, 0 );
			CHECK( ray == 0 || rays[ ray ] > rays[ ray - 1 ] );
		}
	}
	// more players than panes share the eight
	Real rays[ OBSERVER_MOST_PANES ];
	CHECK_EQ( ObserverCamera_paneLayout( 12, rays ), (Int)OBSERVER_MOST_PANES );
}

TEST(a_pixel_lies_in_the_wedge_counter_clockwise_of_its_ray)
{
	Real rays[ OBSERVER_MOST_PANES ];
	ObserverCamera_paneLayout( 2, rays );
	// the diagonal through the middle of 1280x720: pane 0 above it on the left, pane 1 below it
	CHECK_EQ( ObserverCamera_paneOf( 740.0f, 258.0f, 640.0f, 360.0f, rays, 2 ), 0 );
	CHECK_EQ( ObserverCamera_paneOf( 740.0f, 262.0f, 640.0f, 360.0f, rays, 2 ), 1 );
	CHECK_EQ( ObserverCamera_paneOf( 100.0f, 100.0f, 640.0f, 360.0f, rays, 2 ), 0 );
	CHECK_EQ( ObserverCamera_paneOf( 1200.0f, 700.0f, 640.0f, 360.0f, rays, 2 ), 1 );

	// an X: top, left, bottom, right
	ObserverCamera_paneLayout( 4, rays );
	CHECK_EQ( ObserverCamera_paneOf( 640.0f, 100.0f, 640.0f, 360.0f, rays, 4 ), 0 );
	CHECK_EQ( ObserverCamera_paneOf( 100.0f, 360.0f, 640.0f, 360.0f, rays, 4 ), 1 );
	CHECK_EQ( ObserverCamera_paneOf( 640.0f, 700.0f, 640.0f, 360.0f, rays, 4 ), 2 );
	CHECK_EQ( ObserverCamera_paneOf( 1200.0f, 360.0f, 640.0f, 360.0f, rays, 4 ), 3 );
}

TEST(pane_zero_fills_the_screen_once_the_meeting_point_is_out_far_enough)
{
	Real rays[ OBSERVER_MOST_PANES ];
	ObserverCamera_paneLayout( 2, rays );
	// the diagonal has to clear the bottom right corner: (639.5 + 359.5) * cos 45 degrees
	CHECK_NEAR( ObserverCamera_paneExit( rays, 2, 1280, 720 ), 706.4f, 2.0f );
	const Coord2D away = ObserverCamera_paneExitDirection( rays );
	CHECK( away.x > 0.0f && away.y > 0.0f );
}

TEST(a_pane_circle_is_the_largest_its_wedge_holds)
{
	Real rays[ OBSERVER_MOST_PANES ];
	Coord2D centres[ OBSERVER_MOST_PANES ];
	Real radii[ OBSERVER_MOST_PANES ];
	const Coord2D noRadar = { 0.0f, 0.0f };

	// two panes: the circle touching the top, the left and the diagonal x + y = 1000 has a radius of
	// 1000 / (2 + root 2), and pane 1's is its mirror through the middle
	ObserverCamera_paneLayout( 2, rays );
	ObserverCamera_paneCircles( rays, 2, 1280, 720, noRadar, 0.0f, centres, radii );
	CHECK_NEAR( radii[ 0 ], 292.9f, 8.0f );
	CHECK_NEAR( centres[ 0 ].x, 292.9f, 15.0f );
	CHECK_NEAR( centres[ 0 ].y, 292.9f, 15.0f );
	CHECK_NEAR( radii[ 1 ], radii[ 0 ], 8.0f );
	CHECK_NEAR( centres[ 0 ].x + centres[ 1 ].x, 1280.0f, 20.0f );

	// an X: the top wedge is a right triangle on a 720 wide side, its circle 149 in radius; the left
	// one runs past the top and bottom corners, and its circle touching the left edge and both rays has
	// 640 / (1 + root 2)
	ObserverCamera_paneLayout( 4, rays );
	ObserverCamera_paneCircles( rays, 4, 1280, 720, noRadar, 0.0f, centres, radii );
	CHECK_NEAR( radii[ 0 ], 149.1f, 8.0f );
	CHECK_NEAR( centres[ 0 ].x, 640.0f, 15.0f );
	CHECK_NEAR( radii[ 1 ], 265.1f, 8.0f );
	CHECK_NEAR( centres[ 1 ].y, 360.0f, 15.0f );
	CHECK_NEAR( radii[ 2 ], radii[ 0 ], 8.0f );
	CHECK_NEAR( radii[ 3 ], radii[ 1 ], 8.0f );

	// a Y: three circles, every one inside the screen
	ObserverCamera_paneLayout( 3, rays );
	ObserverCamera_paneCircles( rays, 3, 1280, 720, noRadar, 0.0f, centres, radii );
	for( Int pane = 0; pane < 3; pane++ )
	{
		CHECK( radii[ pane ] > 100.0f );
		CHECK( centres[ pane ].x - radii[ pane ] >= 0.0f && centres[ pane ].x + radii[ pane ] <= 1280.0f );
		CHECK( centres[ pane ].y - radii[ pane ] >= 0.0f && centres[ pane ].y + radii[ pane ] <= 720.0f );
		CHECK_EQ( ObserverCamera_paneOf( centres[ pane ].x, centres[ pane ].y, 640.0f, 360.0f, rays, 3 ), pane );
	}

	// the framed radar in the middle takes room from the two pane circle, which reached its corner
	ObserverCamera_paneLayout( 2, rays );
	const Coord2D radar = { 150.0f, 100.0f };
	ObserverCamera_paneCircles( rays, 2, 1280, 720, radar, 0.0f, centres, radii );
	CHECK( radii[ 0 ] < 285.0f );
	CHECK( radii[ 0 ] > 150.0f );

	// the score bar's rows at the top are no pane's: the upper circle stays below them
	ObserverCamera_paneCircles( rays, 2, 1280, 720, noRadar, 90.0f, centres, radii );
	CHECK( centres[ 0 ].y - radii[ 0 ] >= 90.0f - 6.0f );
	CHECK( radii[ 0 ] < 285.0f );
}

TEST(a_pane_label_hangs_as_high_in_its_circle_as_the_circle_holds_it)
{
	Coord2D centre;
	centre.x = 300.0f;
	centre.y = 400.0f;
	// 160 wide in a circle of 100: its top corners meet the circle 60 above the centre
	const Real top = ObserverCamera_paneLabelTop( centre, 100.0f, 160.0f, 20.0f );
	CHECK_NEAR( top, 340.0f, 0.01f );
	// its top corners, the first of it to leave the circle, are on it
	const Real dx = 80.0f;
	const Real dy = top - centre.y;
	CHECK( dx * dx + dy * dy <= 100.0f * 100.0f + 0.5f );
	// too wide to fit at all: across the middle
	CHECK_NEAR( ObserverCamera_paneLabelTop( centre, 100.0f, 240.0f, 20.0f ), 390.0f, 0.01f );
	// a chord shorter than the label is tall: across the middle as well
	CHECK_NEAR( ObserverCamera_paneLabelTop( centre, 100.0f, 198.0f, 40.0f ), 380.0f, 0.01f );
}

TEST(a_subject_spreads_as_far_as_its_farthest_thing_within_reach)
{
	std::vector< DirectorHeat > things;
	const Coord2D around = { 1000.0f, 1000.0f };
	CHECK_NEAR( ObserverCamera_extentAround( things, around, 500.0f, 150.0f ), 150.0f, 0.01f );
	const Real distances[] = { 100.0f, 300.0f, 600.0f };
	for( Int index = 0; index < 3; index++ )
	{
		DirectorHeat thing;
		thing.position.x = around.x + distances[ index ];
		thing.position.y = around.y;
		thing.weight = 1.0f;
		things.push_back( thing );
	}
	CHECK_NEAR( ObserverCamera_extentAround( things, around, 500.0f, 150.0f ), 300.0f, 0.01f );
}

TEST(a_pane_zooms_out_to_fit_its_subject_and_no_further_than_the_cap)
{
	// the view measured a circle reaching 180 at zoom 1.2 and 420 at 2.4: the eye stands back in a
	// straight line with the zoom from a pivot at 0.3, as W3D's camera does
	const Real nearest = 1.2f;
	const Real farthest = 2.4f;
	// a subject that fits at the director's own zoom is not brought closer
	CHECK_NEAR( ObserverCamera_zoomForReach( nearest, 180.0f, farthest, 420.0f, 120.0f, nearest, farthest ), 1.2f, 0.0001f );
	// 300 is half way along the line
	CHECK_NEAR( ObserverCamera_zoomForReach( nearest, 180.0f, farthest, 420.0f, 300.0f, nearest, farthest ), 1.8f, 0.0001f );
	// and one too wide for the cap gets the cap
	CHECK_NEAR( ObserverCamera_zoomForReach( nearest, 180.0f, farthest, 420.0f, 900.0f, nearest, farthest ), 2.4f, 0.0001f );
}

TEST(a_pane_whose_top_leaves_the_map_is_moved_in_by_the_top_alone)
{
	// the ground under a tilted pane's corners: the top reaches three times as far as the bottom, so
	// a look point 400 from the far edge shows 200 past it at the top while its middle is on the map
	const Coord2D corners[ 4 ] = { { -900.0f, 600.0f }, { 900.0f, 600.0f }, { 300.0f, -200.0f }, { -300.0f, -200.0f } };
	Region2D map;
	map.lo.x = map.lo.y = 0.0f;
	map.hi.x = map.hi.y = 3000.0f;
	const Coord2D nearTop = { 1500.0f, 2600.0f };
	const Coord2D kept = ObserverCamera_keepInMap( nearTop, corners, 4, map );
	CHECK_NEAR( kept.x, 1500.0f, 0.001f );
	CHECK_NEAR( kept.y, 2400.0f, 0.001f );
	const Coord2D middle = { 1500.0f, 1500.0f };
	const Coord2D free = ObserverCamera_keepInMap( middle, corners, 4, map );
	CHECK_NEAR( free.y, 1500.0f, 0.001f );
}

TEST(a_pane_shows_its_wedge_cut_by_the_screen)
{
	Real rays[ OBSERVER_MOST_PANES ];
	ObserverCamera_paneLayout( 2, rays );
	const Coord2D middle = { 640.0f, 360.0f };
	Coord2D vertices[ 8 ];
	// the upper left half: the meeting point, the two left corners and where the diagonal leaves the
	// top and the bottom
	CHECK_EQ( ObserverCamera_panePolygon( rays, 2, 0, middle, 1280, 720, vertices ), 5 );
	Bool top = FALSE;
	Bool bottom = FALSE;
	for( Int vertex = 0; vertex < 5; vertex++ )
	{
		top = top || ( fabs( vertices[ vertex ].x - 1000.0f ) < 0.5f && fabs( vertices[ vertex ].y ) < 0.5f );
		bottom = bottom || ( fabs( vertices[ vertex ].x - 280.0f ) < 0.5f && fabs( vertices[ vertex ].y - 720.0f ) < 0.5f );
	}
	CHECK( top );
	CHECK( bottom );
	// with no panes it is the whole screen
	CHECK_EQ( ObserverCamera_panePolygon( rays, 1, 0, middle, 1280, 720, vertices ), 4 );
}

TEST(pane_lines_grow_with_the_picture_and_the_join_keeps_all_of_them)
{
	CHECK_EQ( ObserverCamera_paneLineWidth( 720 ), 6 );
	CHECK_EQ( ObserverCamera_paneLineWidth( 1080 ), 9 );
	CHECK_EQ( ObserverCamera_paneLineWidth( 2160 ), 18 );
	// the gold and both edges, crossed by a row at 45 degrees: 13 * 0.707 is 9.2 at 1080
	CHECK( ObserverCamera_paneSeamBand( 1080 ) >= 10 );
	CHECK( ObserverCamera_paneSeamBand( 720 ) >= 8 );
}

static DirectorFightHit fightHit( Real x, Real y, UnsignedInt sides )
{
	DirectorFightHit hit;
	hit.position.x = x;
	hit.position.y = y;
	hit.weight = 1.0f;
	hit.sides = sides;
	return hit;
}

static DirectorMoment scoutedFight( UnsignedInt start, UnsignedInt last, Real x, Real y, Real peak )
{
	DirectorMoment fight;
	fight.start = start;
	fight.last = last;
	fight.place.x = fight.target.x = x;
	fight.place.y = fight.target.y = y;
	fight.peak = peak;
	fight.sides = 3;
	fight.power = FALSE;
	fight.superweapon = FALSE;
	return fight;
}

static DirectorMoment scoutedPower( UnsignedInt frame, Real x, Real y, Bool superweapon )
{
	DirectorMoment power = scoutedFight( frame, frame, x, y, superweapon ? 5.0f : 1.0f );
	power.target.x = x + 1500.0f;
	power.power = TRUE;
	power.superweapon = superweapon;
	return power;
}

TEST(the_scout_follows_a_fight_from_its_first_scan_to_its_last_and_keeps_two_apart)
{
	std::vector< DirectorMoment > fights;
	std::vector< DirectorFightHit > hits;
	for( Int each = 0; each < 3; each++ )
		hits.push_back( fightHit( 1000.0f + each * 10.0f, 1000.0f, 1 | 2 ) );
	hits.push_back( fightHit( 4000.0f, 4000.0f, 4 | 8 ) );
	hits.push_back( fightHit( 4010.0f, 4000.0f, 4 | 8 ) );
	ObserverCamera_trackFights( fights, hits, 15 );
	CHECK_EQ( (Int)fights.size(), 2 );
	CHECK_EQ( fights[ 0 ].start, 15u );
	CHECK_NEAR( fights[ 0 ].peak, 3.0f, 0.001f );
	CHECK_EQ( fights[ 0 ].sides, 3u );
	CHECK_EQ( fights[ 1 ].sides, 12u );

	// the first fight moves 200 on and a third player joins it: the same fight, seen later
	hits.clear();
	for( Int each = 0; each < 4; each++ )
		hits.push_back( fightHit( 1200.0f + each * 10.0f, 1000.0f, 1 | 4 ) );
	ObserverCamera_trackFights( fights, hits, 30 );
	CHECK_EQ( (Int)fights.size(), 2 );
	CHECK_EQ( fights[ 0 ].start, 15u );
	CHECK_EQ( fights[ 0 ].last, 30u );
	CHECK_NEAR( fights[ 0 ].place.x, 1010.0f, 0.001f );
	CHECK_NEAR( fights[ 0 ].target.x, 1215.0f, 0.001f );
	CHECK_NEAR( fights[ 0 ].peak, 4.0f, 0.001f );
	CHECK_EQ( fights[ 0 ].sides, 7u );
	CHECK_EQ( fights[ 1 ].last, 15u );

	// quiet for longer than the gap, the place starts a new fight
	ObserverCamera_trackFights( fights, hits, 30 + 4 * LOGICFRAMES_PER_SECOND );
	CHECK_EQ( (Int)fights.size(), 3 );
	CHECK_EQ( fights[ 2 ].start, 30u + 4 * LOGICFRAMES_PER_SECOND );
}

TEST(the_scout_counts_a_fight_worth_filming_only_when_it_got_hot_and_lasted)
{
	CHECK( ObserverCamera_worthFilming( scoutedFight( 100, 100 + 8 * LOGICFRAMES_PER_SECOND, 0.0f, 0.0f, 4.0f ) ) );
	CHECK( !ObserverCamera_worthFilming( scoutedFight( 100, 100 + 8 * LOGICFRAMES_PER_SECOND, 0.0f, 0.0f, 3.9f ) ) );
	CHECK( !ObserverCamera_worthFilming( scoutedFight( 100, 100 + 3 * LOGICFRAMES_PER_SECOND, 0.0f, 0.0f, 20.0f ) ) );
	CHECK( ObserverCamera_worthFilming( scoutedPower( 100, 0.0f, 0.0f, FALSE ) ) );
}

TEST(a_special_power_is_worth_filming_only_once_it_hurts_somebody_where_it_was_aimed)
{
	// a spy satellite and a carpet bomb fired the same frame; only the bomb's target is hit
	std::vector< DirectorMoment > moments;
	moments.push_back( scoutedPower( 1800, 500.0f, 500.0f, FALSE ) );
	moments.push_back( scoutedPower( 1800, 500.0f, 3000.0f, FALSE ) );
	moments.push_back( scoutedPower( 1800, 500.0f, 5000.0f, TRUE ) );
	moments[ 0 ].peak = moments[ 1 ].peak = 0.0f;
	// the superweapon is filmed from its launch, hit or not
	CHECK( ObserverCamera_worthFilming( moments[ 2 ] ) );
	CHECK( !ObserverCamera_worthFilming( moments[ 1 ] ) );
	std::vector< DirectorFightHit > hits;
	hits.push_back( fightHit( 2100.0f, 3000.0f, 3 ) );
	ObserverCamera_markLandings( moments, hits, 1950 );
	CHECK( !ObserverCamera_worthFilming( moments[ 0 ] ) );
	CHECK( ObserverCamera_worthFilming( moments[ 1 ] ) );
	// a hit long after the launch is somebody else's
	hits.push_back( fightHit( 2000.0f, 500.0f, 3 ) );
	ObserverCamera_markLandings( moments, hits, 1800 + 13 * LOGICFRAMES_PER_SECOND );
	CHECK( !ObserverCamera_worthFilming( moments[ 0 ] ) );

	// the filming pass finds the power by its frame and its target, written without decimals
	Coord2D at;
	at.x = 2000.4f;
	at.y = 3000.3f;
	CHECK( ObserverCamera_powerLands( moments, 1800, at ) );
	at.y = 500.0f;
	CHECK( !ObserverCamera_powerLands( moments, 1800, at ) );
	at.y = 3000.0f;
	CHECK( !ObserverCamera_powerLands( moments, 1801, at ) );
}

TEST(a_hot_fight_the_end_of_the_scouting_run_cut_short_is_still_worth_filming)
{
	std::vector< DirectorMoment > timeline;
	timeline.push_back( scoutedFight( 5865, 6000, 1218.0f, 1689.0f, 15.2f ) );
	timeline.push_back( scoutedFight( 5000, 5100, 3000.0f, 3000.0f, 9.0f ) );
	timeline.push_back( scoutedFight( 5950, 5940 + 2 * LOGICFRAMES_PER_SECOND, 4000.0f, 4000.0f, 2.0f ) );
	timeline.push_back( scoutedPower( 5990, 500.0f, 500.0f, FALSE ) );
	CHECK( !ObserverCamera_worthFilming( timeline[ 0 ] ) );
	ObserverCamera_closeTimeline( timeline, 6000 );
	CHECK( ObserverCamera_worthFilming( timeline[ 0 ] ) );
	// one that was over before the end keeps its own end
	CHECK_EQ( timeline[ 1 ].last, 5100u );
	CHECK( !ObserverCamera_worthFilming( timeline[ 1 ] ) );
	// still small at the end is still a fizzle
	CHECK( !ObserverCamera_worthFilming( timeline[ 2 ] ) );
	CHECK_EQ( timeline[ 3 ].last, 5990u );
}

TEST(the_director_goes_to_wait_for_a_fight_four_seconds_before_it_starts_superweapons_first)
{
	std::vector< DirectorMoment > timeline;
	timeline.push_back( scoutedFight( 1000, 1600, 1000.0f, 1000.0f, 6.0f ) );
	timeline.push_back( scoutedFight( 1050, 1600, 3000.0f, 3000.0f, 9.0f ) );
	timeline.push_back( scoutedFight( 1010, 1030, 5000.0f, 5000.0f, 2.0f ) );
	timeline.push_back( scoutedPower( 1020, 6000.0f, 1000.0f, FALSE ) );
	timeline.push_back( scoutedPower( 1100, 6000.0f, 3000.0f, TRUE ) );
	const UnsignedInt preroll = 4 * LOGICFRAMES_PER_SECOND;
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1000 - preroll - 1, NULL, 660.0f ), -1 );
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1000 - preroll, NULL, 660.0f ), 0 );
	// two coming up: the hotter
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1050 - preroll, NULL, 660.0f ), 1 );
	// a superweapon over any fight
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1100 - preroll, NULL, 660.0f ), 4 );
	// one that has started is no longer waited for
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1100, NULL, 660.0f ), -1 );
}

TEST(the_director_does_not_wait_for_the_fight_the_other_pane_already_shows)
{
	std::vector< DirectorMoment > timeline;
	timeline.push_back( scoutedFight( 1000, 1600, 1000.0f, 1000.0f, 6.0f ) );
	timeline.push_back( scoutedFight( 1050, 1600, 3000.0f, 3000.0f, 9.0f ) );
	const UnsignedInt preroll = 4 * LOGICFRAMES_PER_SECOND;
	// pane 1 waits at the hotter one: pane 0 takes the other
	Coord2D taken;
	taken.x = 3100.0f;
	taken.y = 3000.0f;
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1050 - preroll, &taken, 660.0f ), 0 );
	// and with nothing else coming up, none
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1001, &taken, 660.0f ), -1 );
}

TEST(the_director_skips_only_a_fight_the_scout_saw_fizzle)
{
	std::vector< DirectorMoment > timeline;
	timeline.push_back( scoutedFight( 500, 560, 2000.0f, 2000.0f, 2.0f ) );
	timeline.push_back( scoutedFight( 500, 900, 5000.0f, 5000.0f, 8.0f ) );
	Coord2D place;
	place.x = 2050.0f;
	place.y = 2000.0f;
	CHECK( ObserverCamera_fizzles( timeline, place, 520 ) );
	// over and past its gap, whatever is there now the scout did not see
	CHECK( !ObserverCamera_fizzles( timeline, place, 560 + 4 * LOGICFRAMES_PER_SECOND ) );
	place.x = place.y = 5000.0f;
	CHECK( !ObserverCamera_fizzles( timeline, place, 600 ) );
	place.x = place.y = 8000.0f;
	CHECK( !ObserverCamera_fizzles( timeline, place, 520 ) );
	// a fight worth filming in the same place beats the fizzle beside it
	timeline.push_back( scoutedFight( 500, 900, 2100.0f, 2000.0f, 8.0f ) );
	place.x = 2050.0f;
	place.y = 2000.0f;
	CHECK( !ObserverCamera_fizzles( timeline, place, 520 ) );
}

TEST(the_recording_splits_before_a_known_second_fight_far_from_the_first)
{
	std::vector< DirectorMoment > timeline;
	timeline.push_back( scoutedFight( 1000, 1500, 4000.0f, 4000.0f, 8.0f ) );
	timeline.push_back( scoutedFight( 900, 1500, 1200.0f, 1000.0f, 20.0f ) );
	timeline.push_back( scoutedFight( 900, 1500, 6000.0f, 6000.0f, 2.0f ) );
	Coord2D first;
	first.x = first.y = 1000.0f;
	// three seconds ahead: at four pane 1 sat on an empty bridge
	const UnsignedInt lead = 3 * LOGICFRAMES_PER_SECOND;
	CHECK_EQ( ObserverCamera_plannedSecond( timeline, first, 1000 - lead - 1, 660.0f, FALSE ), -1 );
	CHECK_EQ( ObserverCamera_plannedSecond( timeline, first, 1000 - lead, 660.0f, FALSE ), 0 );
	CHECK_EQ( ObserverCamera_plannedSecond( timeline, first, 1500, 660.0f, TRUE ), 0 );
	CHECK_EQ( ObserverCamera_plannedSecond( timeline, first, 1501, 660.0f, TRUE ), -1 );
	// the fight 4243 away is too near once the picture is so wide a split wants 4300
	CHECK_EQ( ObserverCamera_plannedSecond( timeline, first, 1000 - lead, 4300.0f, FALSE ), -1 );
}

TEST(a_split_is_planned_only_beside_a_fight_that_outlasts_its_least_hold)
{
	std::vector< DirectorMoment > timeline;
	timeline.push_back( scoutedFight( 1000, 1300, 1000.0f, 1000.0f, 8.0f ) );
	timeline[ 0 ].target.x = 1400.0f;
	timeline.push_back( scoutedFight( 1000, 1300, 3000.0f, 3000.0f, 2.0f ) );
	Coord2D place;
	place.x = 1050.0f;
	place.y = 1000.0f;
	CHECK( ObserverCamera_fightLasts( timeline, place, 1100, 1300, NULL ) );
	CHECK( !ObserverCamera_fightLasts( timeline, place, 1100, 1301, NULL ) );
	// near where it ended up as well as where it began
	place.x = 1600.0f;
	CHECK( ObserverCamera_fightLasts( timeline, place, 1100, 1300, NULL ) );
	// but not for a second pane while the first shows where it began: that is the first pane's fight
	Coord2D first;
	first.x = first.y = 1000.0f;
	CHECK( !ObserverCamera_fightLasts( timeline, place, 1100, 1300, &first ) );
	first.x = first.y = 3000.0f;
	CHECK( ObserverCamera_fightLasts( timeline, place, 1100, 1300, &first ) );
	// one about to begin counts; a fizzle never does
	CHECK( ObserverCamera_fightLasts( timeline, place, 1000 - 4 * LOGICFRAMES_PER_SECOND, 1300, NULL ) );
	place.x = place.y = 3000.0f;
	CHECK( !ObserverCamera_fightLasts( timeline, place, 1100, 1200, NULL ) );
}

TEST(the_recording_splits_ahead_for_a_fight_still_to_begin_and_not_anew_for_one_under_way)
{
	std::vector< DirectorMoment > timeline;
	timeline.push_back( scoutedFight( 5865, 6870, 1218.0f, 1689.0f, 15.2f ) );
	Coord2D first;
	first.x = 2346.0f;
	first.y = 1668.0f;
	// a split already up keeps the fight while it lasts
	CHECK_EQ( ObserverCamera_plannedSecond( timeline, first, 6451, 660.0f, TRUE ), 0 );
	// none up: where it began twenty seconds ago is no place to send a pane
	CHECK_EQ( ObserverCamera_plannedSecond( timeline, first, 6451, 660.0f, FALSE ), -1 );
	CHECK_EQ( ObserverCamera_plannedSecond( timeline, first, 5864, 660.0f, FALSE ), 0 );
}

TEST(a_timeline_line_reads_back_as_the_moment_it_was_written_from)
{
	DirectorMoment fight = scoutedFight( 450, 1230, 1234.0f, 567.0f, 7.25f );
	fight.target.x = 1400.0f;
	fight.sides = 0x30;
	DirectorMoment read;
	CHECK( ObserverCamera_parseMoment( ObserverCamera_formatMoment( fight ).str(), &read ) );
	CHECK_EQ( read.start, 450u );
	CHECK_EQ( read.last, 1230u );
	CHECK_NEAR( read.place.x, 1234.0f, 0.001f );
	CHECK_NEAR( read.place.y, 567.0f, 0.001f );
	CHECK_NEAR( read.target.x, 1400.0f, 0.001f );
	CHECK_NEAR( read.peak, 7.25f, 0.001f );
	CHECK_EQ( read.sides, 0x30u );
	CHECK( !read.power );
	CHECK( ObserverCamera_parseMoment( ObserverCamera_formatMoment( scoutedPower( 90, 10.0f, 20.0f, TRUE ) ).str(), &read ) );
	CHECK( read.power );
	CHECK( read.superweapon );
	CHECK( !ObserverCamera_parseMoment( "crc 900 305419896", &read ) );
	CHECK( !ObserverCamera_parseMoment( "", &read ) );
}

TEST(pane_animations_ease_over_logic_frames)
{
	CHECK_NEAR( ObserverCamera_easeFrames( 100, 100, 15 ), 0.0f, 0.0001f );
	CHECK_NEAR( ObserverCamera_easeFrames( 90, 100, 15 ), 0.0f, 0.0001f );
	CHECK_NEAR( ObserverCamera_easeFrames( 110, 100, 20 ), 0.5f, 0.0001f );
	CHECK_NEAR( ObserverCamera_easeFrames( 115, 100, 15 ), 1.0f, 0.0001f );
	CHECK_NEAR( ObserverCamera_easeFrames( 400, 100, 15 ), 1.0f, 0.0001f );
	Real last = 0.0f;
	for( UnsignedInt frame = 100; frame <= 115; frame++ )
	{
		const Real now = ObserverCamera_easeFrames( frame, 100, 15 );
		CHECK( now >= last );
		last = now;
	}
}

TEST(the_score_bar_keeps_each_team_together_and_balances_its_two_columns)
{
	// two sides face each other whatever their sizes: 4v1
	std::vector< Int > sizes;
	sizes.push_back( 4 );
	sizes.push_back( 1 );
	std::vector< Int > columns = ObserverCamera_blockColumns( sizes );
	CHECK_EQ( columns[ 0 ], 0 );
	CHECK_EQ( columns[ 1 ], 1 );
	// eight alone alternate, four a column
	sizes.assign( 8, 1 );
	columns = ObserverCamera_blockColumns( sizes );
	Int left = 0;
	for( size_t block = 0; block < columns.size(); block++ )
		left += columns[ block ] == 0 ? 1 : 0;
	CHECK_EQ( left, 4 );
	// four alone and a pair: the pair's two rows and its total land where there is room
	sizes.assign( 4, 1 );
	sizes.push_back( 2 );
	columns = ObserverCamera_blockColumns( sizes );
	Int rows[ 2 ] = { 0, 0 };
	for( size_t block = 0; block < columns.size(); block++ )
		rows[ columns[ block ] ] += sizes[ block ] + ( sizes[ block ] >= 2 ? 1 : 0 );
	CHECK_EQ( rows[ 0 ], 4 );
	CHECK_EQ( rows[ 1 ], 3 );

	// teammates are taken together in the order their teams first appear
	std::vector< Int > teams;
	teams.push_back( 0 );
	teams.push_back( 1 );
	teams.push_back( 0 );
	teams.push_back( 2 );
	teams.push_back( 1 );
	const std::vector< Int > order = ObserverCamera_teamOrder( teams );
	const Int expected[] = { 0, 2, 1, 4, 3 };
	CHECK_EQ( (Int)order.size(), 5 );
	for( Int index = 0; index < 5; index++ )
		CHECK_EQ( order[ index ], expected[ index ] );
}

TEST(the_army_bar_cuts_its_whole_width_among_the_sides)
{
	std::vector< Int > armies;
	armies.push_back( 1 );
	armies.push_back( 1 );
	armies.push_back( 1 );
	const std::vector< Int > pixels = ObserverCamera_barShares( armies, 100 );
	CHECK_EQ( pixels[ 0 ] + pixels[ 1 ] + pixels[ 2 ], 100 );
	CHECK( pixels[ 0 ] >= 33 && pixels[ 0 ] <= 34 );
	armies[ 0 ] = 3000;
	armies[ 1 ] = 1000;
	armies[ 2 ] = 0;
	const std::vector< Int > uneven = ObserverCamera_barShares( armies, 100 );
	CHECK_EQ( uneven[ 0 ], 75 );
	CHECK_EQ( uneven[ 1 ], 25 );
	CHECK_EQ( uneven[ 2 ], 0 );
	armies.assign( 3, 0 );
	const std::vector< Int > none = ObserverCamera_barShares( armies, 100 );
	CHECK_EQ( none[ 0 ] + none[ 1 ] + none[ 2 ], 0 );
}

TEST(a_dark_player_colour_is_lifted_to_read_and_a_gold_one_is_told_from_the_lines)
{
	// purple, #9600c8, is lifted; white stays white
	const Color purple = GameMakeColor( 0x96, 0x00, 0xc8, 255 );
	const Color lifted = ObserverCamera_readableColor( purple );
	CHECK_NE( lifted, purple );
	UnsignedByte red, green, blue, alpha;
	GameGetColorComponents( lifted, &red, &green, &blue, &alpha );
	CHECK( red > 0x96 && green > 0x00 && blue > 0xc8 );
	const Color white = GameMakeColor( 255, 255, 255, 255 );
	CHECK_NE( lifted, white );
	CHECK_EQ( ObserverCamera_readableColor( white ), white );
	// gold, #dde20d, is near the lines' gold; blue and red are not
	CHECK( ObserverCamera_nearBrandGold( GameMakeColor( 0xdd, 0xe2, 0x0d, 255 ) ) );
	CHECK( !ObserverCamera_nearBrandGold( GameMakeColor( 0x00, 0x00, 0xff, 255 ) ) );
	CHECK( !ObserverCamera_nearBrandGold( GameMakeColor( 0xff, 0x00, 0x00, 255 ) ) );
}
