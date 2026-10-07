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
