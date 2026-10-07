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
	CHECK( !ObserverCamera_secondPlace( hits, first, &second, &heat ) );

	for( Int each = 0; each < 3; each++ )
		hits.push_back( directorHit( 3000.0f, 3000.0f + each * 20.0f ) );
	CHECK( ObserverCamera_secondPlace( hits, first, &second, &heat ) );
	CHECK_NEAR( heat, 3.0f, 0.001f );
	CHECK_NEAR( second.x, 3000.0f, 0.001f );
	CHECK_NEAR( second.y, 3020.0f, 0.001f );
}

TEST(the_recording_splits_for_a_big_second_fight_holds_it_and_rests_after)
{
	const UnsignedInt rested = 5 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt justJoined = 2 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt longHeld = 8 * LOGICFRAMES_PER_SECOND;
	const Real farApart = 2000.0f;
	// on for a second fight hot on its own and at least half the first
	CHECK( ObserverCamera_holdSplit( FALSE, 6.0f, 3.0f, farApart, rested ) );
	CHECK( !ObserverCamera_holdSplit( FALSE, 8.0f, 3.0f, farApart, rested ) );
	CHECK( !ObserverCamera_holdSplit( FALSE, 1.0f, 1.5f, farApart, rested ) );
	// not straight back on after it went off
	CHECK( !ObserverCamera_holdSplit( FALSE, 6.0f, 3.0f, farApart, 2 * LOGICFRAMES_PER_SECOND ) );
	// held a few seconds while the second fight goes on at all, then only while it keeps a quarter
	CHECK( ObserverCamera_holdSplit( TRUE, 6.0f, 0.5f, farApart, justJoined ) );
	CHECK( ObserverCamera_holdSplit( TRUE, 8.0f, 2.0f, farApart, longHeld ) );
	CHECK( !ObserverCamera_holdSplit( TRUE, 8.0f, 1.9f, farApart, longHeld ) );
}

TEST(the_recording_split_ends_at_once_when_the_two_halves_show_one_fight)
{
	const UnsignedInt justJoined = 2 * LOGICFRAMES_PER_SECOND;
	// the director moved onto the second fight: both halves would show the same ground
	CHECK( !ObserverCamera_holdSplit( TRUE, 12.0f, 8.0f, 60.0f, justJoined ) );
	CHECK( !ObserverCamera_holdSplit( FALSE, 6.0f, 6.0f, 600.0f, 5 * LOGICFRAMES_PER_SECOND ) );
	// the second fight is over, however new the split
	CHECK( !ObserverCamera_holdSplit( TRUE, 6.0f, 0.0f, 2000.0f, justJoined ) );
}

TEST(the_recording_splits_only_beside_a_real_fight)
{
	// bases going up are no fight: the split's hits are only one side's on another, so the director
	// looking at a sight has no heat of its own and a second fight does not split the picture
	CHECK( !ObserverCamera_holdSplit( FALSE, 0.0f, 10.0f, 2000.0f, 5 * LOGICFRAMES_PER_SECOND ) );
	// and with no fight anywhere there is no second place to show
	std::vector< DirectorHeat > none;
	Coord2D first;
	first.x = first.y = 1000.0f;
	Coord2D second;
	Real heat = 0.0f;
	CHECK( !ObserverCamera_secondPlace( none, first, &second, &heat ) );
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
	ObserverCamera_paneCircles( rays, 2, 1280, 720, noRadar, centres, radii );
	CHECK_NEAR( radii[ 0 ], 292.9f, 8.0f );
	CHECK_NEAR( centres[ 0 ].x, 292.9f, 15.0f );
	CHECK_NEAR( centres[ 0 ].y, 292.9f, 15.0f );
	CHECK_NEAR( radii[ 1 ], radii[ 0 ], 8.0f );
	CHECK_NEAR( centres[ 0 ].x + centres[ 1 ].x, 1280.0f, 20.0f );

	// an X: the top wedge is a right triangle on a 720 wide side, its circle 149 in radius; the left
	// one runs past the top and bottom corners, and its circle touching the left edge and both rays has
	// 640 / (1 + root 2)
	ObserverCamera_paneLayout( 4, rays );
	ObserverCamera_paneCircles( rays, 4, 1280, 720, noRadar, centres, radii );
	CHECK_NEAR( radii[ 0 ], 149.1f, 8.0f );
	CHECK_NEAR( centres[ 0 ].x, 640.0f, 15.0f );
	CHECK_NEAR( radii[ 1 ], 265.1f, 8.0f );
	CHECK_NEAR( centres[ 1 ].y, 360.0f, 15.0f );
	CHECK_NEAR( radii[ 2 ], radii[ 0 ], 8.0f );
	CHECK_NEAR( radii[ 3 ], radii[ 1 ], 8.0f );

	// a Y: three circles, every one inside the screen
	ObserverCamera_paneLayout( 3, rays );
	ObserverCamera_paneCircles( rays, 3, 1280, 720, noRadar, centres, radii );
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
	ObserverCamera_paneCircles( rays, 2, 1280, 720, radar, centres, radii );
	CHECK( radii[ 0 ] < 285.0f );
	CHECK( radii[ 0 ] > 150.0f );
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
	ObserverCameraGround ground;
	const Coord2D atOne[ 4 ] = { { -400.0f, 400.0f }, { 400.0f, 400.0f }, { 300.0f, -200.0f }, { -300.0f, -200.0f } };
	for( Int corner = 0; corner < 4; corner++ )
	{
		ground.perZoom[ corner ] = atOne[ corner ];
		ground.shift[ corner ].x = ground.shift[ corner ].y = 0.0f;
	}
	ground.zoomAtPivot = 0.0f;
	const Coord2D centre = { 640.0f, 360.0f };
	Coord2D corners[ 4 ];
	ObserverCamera_cornersAtZoom( ground, 1.0f, corners );
	const Real reachAtOne = ObserverCamera_groundRadius( centre, 200.0f, 1280, 720, corners );
	// a subject that fits at the director's own zoom is not brought closer
	CHECK_NEAR( ObserverCamera_fitZoom( reachAtOne * 0.5f, centre, 200.0f, 1280, 720, ground, 1.0f, 2.0f ), 1.0f, 0.0001f );
	// one 1.5 times the circle's reach, with its 15 percent margin, needs 1.725 times as far out
	CHECK_NEAR( ObserverCamera_fitZoom( reachAtOne * 1.5f, centre, 200.0f, 1280, 720, ground, 1.0f, 3.0f ), 1.725f, 0.001f );
	CHECK_NEAR( ObserverCamera_fitZoom( reachAtOne * 5.0f, centre, 200.0f, 1280, 720, ground, 1.0f, 2.0f ), 2.0f, 0.001f );
	// a smaller circle needs the camera further out
	CHECK( ObserverCamera_fitZoom( reachAtOne, centre, 150.0f, 1280, 720, ground, 1.0f, 3.0f )
		> ObserverCamera_fitZoom( reachAtOne, centre, 200.0f, 1280, 720, ground, 1.0f, 3.0f ) );
}

/// the game's view as buildCameraTransform makes it: a pinhole looking down at 50 degrees on a target
/// TARGET_HEIGHT above zero, its eye (zoom - ZOOM_AT_PIVOT) times 400 back from it
static const Real TARGET_HEIGHT = 10.0f;
static const Real ZOOM_AT_PIVOT = 0.3f;

/// where a pixel of a 1280x720 picture falls on a plane at planeHeight for that view at zoom
static Coord2D castPixel( Real x, Real y, Real zoom, Real planeHeight )
{
	const Real distance = ( zoom - ZOOM_AT_PIVOT ) * 400.0f;
	const Real pitch = 50.0f * PI / 180.0f;
	const Real halfWide = 0.6f;
	const Real halfHigh = 0.6f * 720.0f / 1280.0f;
	const Real forwardY = cosf( pitch ), forwardZ = -sinf( pitch );
	const Real upY = sinf( pitch ), upZ = cosf( pitch );
	const Real right = ( 2.0f * x / 1280.0f - 1.0f ) * halfWide;
	const Real up = ( 1.0f - 2.0f * y / 720.0f ) * halfHigh;
	const Real eyeY = -distance * forwardY, eyeZ = TARGET_HEIGHT - distance * forwardZ;
	const Real rayX = right, rayY = forwardY + up * upY, rayZ = forwardZ + up * upZ;
	const Real along = ( planeHeight - eyeZ ) / rayZ;
	Coord2D ground;
	ground.x = rayX * along;
	ground.y = eyeY + rayY * along;
	return ground;
}

static void castCorners( Real zoom, Real planeHeight, Coord2D *corners )
{
	corners[ 0 ] = castPixel( 0.0f, 0.0f, zoom, planeHeight );
	corners[ 1 ] = castPixel( 1280.0f, 0.0f, zoom, planeHeight );
	corners[ 2 ] = castPixel( 1280.0f, 720.0f, zoom, planeHeight );
	corners[ 3 ] = castPixel( 0.0f, 720.0f, zoom, planeHeight );
}

/// what measureCorners takes from the view at zoom over ground at groundHeight
static ObserverCameraGround measureCast( Real zoom, Real groundHeight )
{
	ObserverCameraGround ground;
	Coord2D onPivot[ 4 ];
	Coord2D onGround[ 4 ];
	castCorners( zoom, TARGET_HEIGHT, onPivot );
	castCorners( zoom, groundHeight, onGround );
	for( Int corner = 0; corner < 4; corner++ )
	{
		ground.perZoom[ corner ].x = onPivot[ corner ].x / ( zoom - ZOOM_AT_PIVOT );
		ground.perZoom[ corner ].y = onPivot[ corner ].y / ( zoom - ZOOM_AT_PIVOT );
		ground.shift[ corner ].x = onGround[ corner ].x - onPivot[ corner ].x;
		ground.shift[ corner ].y = onGround[ corner ].y - onPivot[ corner ].y;
	}
	ground.zoomAtPivot = ZOOM_AT_PIVOT;
	return ground;
}

TEST(corners_measured_at_one_zoom_say_where_they_fall_at_another)
{
	// ground 110 above the camera's target, which is how the old scaling about the look point went
	// wrong: the top corners, nearly level, moved furthest
	const ObserverCameraGround ground = measureCast( 1.0f, 120.0f );
	const Real zooms[] = { 1.0f, 1.4f, 2.0f };
	for( Int index = 0; index < 3; index++ )
	{
		Coord2D cast[ 4 ];
		castCorners( zooms[ index ], 120.0f, cast );
		Coord2D predicted[ 4 ];
		ObserverCamera_cornersAtZoom( ground, zooms[ index ], predicted );
		for( Int corner = 0; corner < 4; corner++ )
		{
			CHECK_NEAR( predicted[ corner ].x, cast[ corner ].x, 0.5f );
			CHECK_NEAR( predicted[ corner ].y, cast[ corner ].y, 0.5f );
		}
	}
}

TEST(the_four_corners_project_every_pixel_onto_flat_ground_exactly)
{
	Coord2D corners[ 4 ];
	castCorners( 1.3f, 120.0f, corners );
	const Coord2D pixels[] = { { 900.0f, 200.0f }, { 150.0f, 650.0f }, { 640.0f, 360.0f }, { 1200.0f, 40.0f } };
	for( Int index = 0; index < 4; index++ )
	{
		const Coord2D cast = castPixel( pixels[ index ].x, pixels[ index ].y, 1.3f, 120.0f );
		const Coord2D projected = ObserverCamera_pixelToGround( pixels[ index ], 1280, 720, corners );
		CHECK_NEAR( projected.x, cast.x, 0.5f );
		CHECK_NEAR( projected.y, cast.y, 0.5f );
		const Coord2D back = ObserverCamera_groundToPixel( projected, 1280, 720, corners );
		CHECK_NEAR( back.x, pixels[ index ].x, 0.05f );
		CHECK_NEAR( back.y, pixels[ index ].y, 0.05f );
	}
}

TEST(a_subject_put_on_its_circle_centre_stays_there_at_every_zoom)
{
	// the look point the pane cameras use, subject less the pixel's ground at that zoom, drawn back
	// through the real camera of that zoom, lands on the pixel it was put on
	const ObserverCameraGround ground = measureCast( 1.0f, 120.0f );
	const Coord2D subject = { 3000.0f, 2000.0f };
	const Coord2D centre = { 293.0f, 293.0f };
	const Real zooms[] = { 1.0f, 1.3f, 1.7f, 2.0f };
	for( Int index = 0; index < 4; index++ )
	{
		Coord2D corners[ 4 ];
		ObserverCamera_cornersAtZoom( ground, zooms[ index ], corners );
		const Coord2D look = ObserverCamera_lookPoint( subject, centre, 1280, 720, corners );
		Coord2D cast[ 4 ];
		castCorners( zooms[ index ], 120.0f, cast );
		Coord2D fromLook;
		fromLook.x = subject.x - look.x;
		fromLook.y = subject.y - look.y;
		const Coord2D drawn = ObserverCamera_groundToPixel( fromLook, 1280, 720, cast );
		CHECK_NEAR( drawn.x, centre.x, 0.5f );
		CHECK_NEAR( drawn.y, centre.y, 0.5f );
	}
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

TEST(a_pane_comes_down_to_the_zoom_that_shows_no_ground_past_the_map)
{
	const ObserverCameraGround ground = measureCast( 1.0f, 120.0f );
	Region2D map;
	map.lo.x = map.lo.y = 0.0f;
	map.hi.x = map.hi.y = 3000.0f;
	const Coord2D middlePixel = { 640.0f, 360.0f };
	Real rays[ OBSERVER_MOST_PANES ];
	Coord2D screen[ 8 ];
	const Int screenCount = ObserverCamera_panePolygon( rays, 1, 0, middlePixel, 1280, 720, screen );

	// in the middle of a big map the cap is free
	const Coord2D middle = { 1500.0f, 1500.0f };
	CHECK_NEAR( ObserverCamera_zoomInMap( middle, middlePixel, screen, screenCount, 1280, 720, ground, map, 1.0f, 1.5f ), 1.5f, 0.0001f );

	// near the far edge only the top of the screen, which reaches furthest, leaves the map: the
	// middle stays on it at any zoom, and the zoom comes down to where the top just touches the edge
	const Coord2D nearTop = { 1500.0f, 2550.0f };
	const Real kept = ObserverCamera_zoomInMap( nearTop, middlePixel, screen, screenCount, 1280, 720, ground, map, 1.0f, 3.0f );
	CHECK( kept > 1.0f && kept < 3.0f );
	Coord2D cast[ 4 ];
	castCorners( kept, 120.0f, cast );
	Coord2D corners[ 4 ];
	ObserverCamera_cornersAtZoom( ground, kept, corners );
	const Coord2D look = ObserverCamera_lookPoint( nearTop, middlePixel, 1280, 720, corners );
	const Real topEdge = look.y + max( cast[ 0 ].y, cast[ 1 ].y );
	CHECK_NEAR( topEdge, 3000.0f, 3.0f );
	CHECK( nearTop.y < 3000.0f );

	// and a subject that cannot be shown without the edge at any zoom gets the nearest
	const Coord2D pastEdge = { 10.0f, 1500.0f };
	CHECK_NEAR( ObserverCamera_zoomInMap( pastEdge, middlePixel, screen, screenCount, 1280, 720, ground, map, 1.0f, 2.0f ), 1.0f, 0.0001f );
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
