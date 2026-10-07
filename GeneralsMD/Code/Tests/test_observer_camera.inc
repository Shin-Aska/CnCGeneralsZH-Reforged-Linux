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

TEST(the_split_line_leans_twelve_degrees_through_the_middle)
{
	CHECK_EQ( ObserverCamera_splitBoundary( 360, 1280, 720 ), 640 );
	// 360 rows from the middle at tan 12 degrees is 76.5 columns
	CHECK_EQ( ObserverCamera_splitBoundary( 0, 1280, 720 ), 717 );
	CHECK_EQ( ObserverCamera_splitBoundary( 720, 1280, 720 ), 563 );
}
