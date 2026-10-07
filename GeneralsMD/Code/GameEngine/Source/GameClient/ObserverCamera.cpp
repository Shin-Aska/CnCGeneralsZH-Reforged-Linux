/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// ObserverCamera.cpp /////////////////////////////////////////////////////////////////////////////
// The watcher's camera driven for him, and the watched player's fog.  ObserverCamera.h says which
// is which.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/ObserverCamera.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GhostObject.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"

#include <math.h>

ObserverCamera TheObserverCamera;

/// how far round a hit the others count as the same fight: about half a screen at the default zoom
static const Real DIRECTOR_GATHER_RADIUS = 220.0f;
/// how long a hit keeps a place hot
static const UnsignedInt DIRECTOR_HEAT_FRAMES = 5 * LOGICFRAMES_PER_SECOND;
/// how often the hits are counted again
static const UnsignedInt DIRECTOR_SCAN_FRAMES = LOGICFRAMES_PER_SECOND / 2;
/// how long the director stays with a fight that is still going before it looks for a better one
static const UnsignedInt DIRECTOR_HOLD_FRAMES = 12 * LOGICFRAMES_PER_SECOND;
/// how much hotter somewhere else has to be to be worth leaving a fight that is still going
static const Real DIRECTOR_SWITCH_MARGIN = 2.0f;
/// no move sooner than this after the last one, however big the other fight
static const UnsignedInt DIRECTOR_SETTLE_FRAMES = 6 * LOGICFRAMES_PER_SECOND;
/// this much hotter elsewhere and the director goes before its hold is up
static const Real DIRECTOR_BIG_MARGIN = 4.0f;
/// after this long on one fight any clearly hotter one elsewhere will do
static const UnsignedInt DIRECTOR_TIRED_FRAMES = 30 * LOGICFRAMES_PER_SECOND;
static const Real DIRECTOR_TIRED_MARGIN = 1.3f;
/// a fight's middle drifts as units die and arrive; the camera follows it only once it has gone this far
static const Real DIRECTOR_FOLLOW_SLACK = 80.0f;
/// what a special power counts for against another one; any of them outranks every fight
static const Real EVENT_WEIGHT = 1.0f;
static const Real EVENT_SUPERWEAPON_WEIGHT = 5.0f;
/// how long a special power is watched with nothing hitting the ground there yet: a superweapon's
/// missiles take a while to arrive, a bomber longer to fly in
static const UnsignedInt EVENT_FRAMES = 12 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt EVENT_SUPERWEAPON_FRAMES = 20 * LOGICFRAMES_PER_SECOND;
/// how long a superweapon is shown at its silo before the camera goes to where it will land
static const UnsignedInt EVENT_LAUNCH_FRAMES = 4 * LOGICFRAMES_PER_SECOND;
/// the camera stays this long after the last hit at an event's target, for the cloud and the fires
static const UnsignedInt EVENT_AFTERMATH_FRAMES = 5 * LOGICFRAMES_PER_SECOND;
/// a hit on a thing that cost this much counts twice what a free one does
static const Real DIRECTOR_COST_PER_WEIGHT = 500.0f;
/// a kill counts this many times a hit, and anything on a superweapon this many times again
static const Real DIRECTOR_KILL_FACTOR = 2.0f;
static const Real DIRECTOR_SUPERWEAPON_FACTOR = 3.0f;
/// with no fight on, how long the director looks at one army, base or building site
static const UnsignedInt DIRECTOR_SIGHT_FRAMES = 14 * LOGICFRAMES_PER_SECOND;
/// how many of the last sights the director will not go back to while there is another
static const size_t DIRECTOR_SEEN_COUNT = 3;
/// further apart than this the camera cuts rather than glides: a glide across most of a map is too
/// long to sit through and shows nothing
static const Real CUT_DISTANCE = 1600.0f;
/// the camera found further than this from where it was put was moved by something else, a click on
/// the production strip or a jump to a group; the edge of the map pulls it back by less.  The radar
/// hands the camera over itself, since a click near the camera and a drag both move it by less
static const Real HAND_JUMP_DISTANCE = 400.0f;
/// roughly how long the director's glide takes to arrive, easing in and out
static const Real DIRECTOR_PAN_SECONDS = 1.4f;
/// the director's glide never crosses the ground faster than this, about two screens a second
static const Real DIRECTOR_TOP_SPEED = 900.0f;
/// a player's camera comes a few times a second, and this smooths the steps between
static const Real PLAYER_PAN_SECONDS = 0.15f;
static const Real PLAYER_TOP_SPEED = 4000.0f;
/// a frame longer than this is a hitch, and is not allowed to throw the camera across the map
static const UnsignedInt LONGEST_STEP_MILLISECONDS = 100;
static const Real MILLISECONDS_PER_SECOND = 1000.0f;

//-------------------------------------------------------------------------------------------------
static Bool sameFight( const Coord2D &a, const Coord2D &b )
{
	const Real dx = a.x - b.x;
	const Real dy = a.y - b.y;
	return dx * dx + dy * dy <= DIRECTOR_GATHER_RADIUS * DIRECTOR_GATHER_RADIUS;
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_hitWeight( Int cost, Bool killed, Bool superweapon )
{
	Real weight = 1.0f + cost / DIRECTOR_COST_PER_WEIGHT;
	if( killed )
		weight *= DIRECTOR_KILL_FACTOR;
	if( superweapon )
		weight *= DIRECTOR_SUPERWEAPON_FACTOR;
	return weight;
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_sightWeight( Int cost, Bool structure, Bool busy, Bool superweapon )
{
	Real weight = cost / DIRECTOR_COST_PER_WEIGHT;
	if( busy )
		weight *= 2.0f;
	else if( structure )
		weight *= 0.5f;
	if( superweapon )
		weight *= DIRECTOR_SUPERWEAPON_FACTOR;
	return weight;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_nextSight( const std::vector< DirectorHeat > &sights, const std::vector< Coord2D > &seen, Coord2D *place )
{
	std::vector< DirectorHeat > fresh;
	for( size_t index = 0; index < sights.size(); index++ )
	{
		Bool wasSeen = FALSE;
		for( size_t look = 0; look < seen.size() && !wasSeen; look++ )
			wasSeen = sameFight( sights[ index ].position, seen[ look ] );
		if( !wasSeen )
			fresh.push_back( sights[ index ] );
	}
	Real heat = 0.0f;
	return ObserverCamera_hottestPlace( fresh, place, &heat );
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_heatAround( const std::vector< DirectorHeat > &hits, const Coord2D &around, Coord2D *middle )
{
	Real heat = 0.0f;
	Coord2D sum = { 0.0f, 0.0f };
	for( size_t index = 0; index < hits.size(); index++ )
	{
		const DirectorHeat &hit = hits[ index ];
		if( !sameFight( hit.position, around ) )
			continue;

		heat += hit.weight;
		sum.x += hit.position.x * hit.weight;
		sum.y += hit.position.y * hit.weight;
	}

	*middle = around;
	if( heat > 0.0f )
	{
		middle->x = sum.x / heat;
		middle->y = sum.y / heat;
	}
	return heat;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_hottestPlace( const std::vector< DirectorHeat > &hits, Coord2D *place, Real *heat )
{
	// ponytail: every hit against every other, fine for the few hundred a big fight makes in five
	// seconds; a grid of cells if a match ever gets to thousands
	*heat = 0.0f;
	for( size_t index = 0; index < hits.size(); index++ )
	{
		Coord2D middle;
		const Real around = ObserverCamera_heatAround( hits, hits[ index ].position, &middle );
		if( around > *heat )
		{
			*heat = around;
			*place = middle;
		}
	}
	return *heat > 0.0f;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_shouldMove( Real heatHere, Real heatThere, UnsignedInt framesHere )
{
	if( heatHere <= 0.0f )
		return heatThere > 0.0f;
	if( framesHere < DIRECTOR_SETTLE_FRAMES )
		return FALSE;
	if( heatThere > heatHere * DIRECTOR_BIG_MARGIN )
		return TRUE;
	if( framesHere < DIRECTOR_HOLD_FRAMES )
		return FALSE;
	const Real margin = framesHere >= DIRECTOR_TIRED_FRAMES ? DIRECTOR_TIRED_MARGIN : DIRECTOR_SWITCH_MARGIN;
	return heatThere > heatHere * margin;
}

//-------------------------------------------------------------------------------------------------
Coord2D ObserverCamera_eventPlace( const DirectorEvent &event, UnsignedInt frame )
{
	if( event.superweapon && frame < event.since + EVENT_LAUNCH_FRAMES && !sameFight( event.source, event.target ) )
		return event.source;
	return event.target;
}

//-------------------------------------------------------------------------------------------------
/** An angle's shortest way round to another, so a camera facing just west of north turns a few
	* degrees to just east of it rather than all the way back round. */
//-------------------------------------------------------------------------------------------------
static Real shortestTurn( Real from, Real to )
{
	Real turn = to - from;
	while( turn > PI )
		turn -= 2.0f * PI;
	while( turn < -PI )
		turn += 2.0f * PI;
	return turn;
}

//-------------------------------------------------------------------------------------------------
/** One axis of a critically damped spring, in the closed form game cameras use (Game Programming
	* Gems 4, 1.10): from rest it gathers speed, then slows into the target without passing it. */
//-------------------------------------------------------------------------------------------------
static Real springTowards( Real from, Real to, Real *velocity, Real smoothSeconds, Real elapsedSeconds )
{
	const Real omega = 2.0f / smoothSeconds;
	const Real x = omega * elapsedSeconds;
	const Real decay = 1.0f / ( 1.0f + x + 0.48f * x * x + 0.235f * x * x * x );
	const Real change = from - to;
	const Real pull = ( *velocity + omega * change ) * elapsedSeconds;
	*velocity = ( *velocity - omega * pull ) * decay;
	Real result = to + ( change + pull ) * decay;
	if( ( to - from > 0.0f ) == ( result > to ) )
	{
		result = to;
		*velocity = 0.0f;
	}
	return result;
}

//-------------------------------------------------------------------------------------------------
ViewLocation ObserverCamera_approach( const ViewLocation &from, const ViewLocation &to, Real elapsedSeconds, Real smoothSeconds,
	Real topSpeed, ObserverCameraVelocity *velocity )
{
	const Coord3D &start = from.getPosition();
	const Coord3D &end = to.getPosition();
	Real dx = end.x - start.x;
	Real dy = end.y - start.y;
	const Real distance = sqrtf( dx * dx + dy * dy );
	if( distance > CUT_DISTANCE )
	{
		velocity->x = velocity->y = velocity->z = velocity->angle = velocity->pitch = velocity->zoom = 0.0f;
		return to;
	}

	// a long way is closed on a point that runs at most this far ahead, which holds the middle of the
	// glide to a steady speed instead of a lunge
	const Real reach = topSpeed * smoothSeconds;
	if( distance > reach )
	{
		dx *= reach / distance;
		dy *= reach / distance;
	}

	const Real angle = from.getAngle();
	ViewLocation step;
	step.init( springTowards( start.x, start.x + dx, &velocity->x, smoothSeconds, elapsedSeconds ),
						 springTowards( start.y, start.y + dy, &velocity->y, smoothSeconds, elapsedSeconds ),
						 springTowards( start.z, end.z, &velocity->z, smoothSeconds, elapsedSeconds ),
						 springTowards( angle, angle + shortestTurn( angle, to.getAngle() ), &velocity->angle, smoothSeconds, elapsedSeconds ),
						 springTowards( from.getPitch(), to.getPitch(), &velocity->pitch, smoothSeconds, elapsedSeconds ),
						 springTowards( from.getZoom(), to.getZoom(), &velocity->zoom, smoothSeconds, elapsedSeconds ) );
	return step;
}

//-------------------------------------------------------------------------------------------------
ObserverCamera::ObserverCamera()
{
	reset();
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::reset( void )
{
	m_mode = OBSERVER_CAMERA_FREE;
	m_followed = NO_PLAYER;
	m_fog = FALSE;
	m_shroudViewer = NO_PLAYER;
	m_driving = FALSE;
	m_holdingHeight = FALSE;
	m_drivenTo.zero();
	m_lastUpdate = 0;
	m_velocity.x = m_velocity.y = m_velocity.z = m_velocity.angle = m_velocity.pitch = m_velocity.zoom = 0.0f;
	for( Int index = 0; index < MAX_PLAYER_COUNT; index++ )
		m_playerViews[ index ] = ViewLocation();
	m_placeValid = FALSE;
	m_place.x = m_place.y = 0.0f;
	m_placeSince = 0;
	m_placeScanned = 0;
	m_placeFor = NULL;
	m_placeKind = PLACE_SIGHT;
	m_placeEvent = 0;
	m_seen.clear();
	m_events.clear();
	m_nextEventId = 1;
}

//-------------------------------------------------------------------------------------------------
/** Called from the logic on every machine, players' included, so it only ever adds to a list the
	* director reads; nothing the logic does depends on it. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::noteSpecialPower( const Player *owner, const Coord3D *from, const Coord3D *at, Bool superweapon )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	dropOldEvents( frame );

	DirectorEvent event;
	event.id = m_nextEventId++;
	event.owner = owner;
	event.source.x = from->x;
	event.source.y = from->y;
	event.target.x = at->x;
	event.target.y = at->y;
	event.since = frame;
	event.until = frame + ( superweapon ? EVENT_SUPERWEAPON_FRAMES : EVENT_FRAMES );
	event.weight = superweapon ? EVENT_SUPERWEAPON_WEIGHT : EVENT_WEIGHT;
	event.superweapon = superweapon;
	m_events.push_back( event );
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::noteSuperweaponHit( const Player *owner, const Coord3D *at, Bool follow )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	Coord2D where;
	where.x = at->x;
	where.y = at->y;
	for( size_t index = 0; index < m_events.size(); index++ )
	{
		DirectorEvent &event = m_events[ index ];
		if( event.owner != owner || !sameFight( event.target, where ) || frame >= event.until )
			continue;

		if( follow )
			event.target = where;
		event.until = max( event.until, frame + EVENT_AFTERMATH_FRAMES );
		return;
	}

	// a warhead nobody's special power sent, a script's or a map's, is still worth seeing land
	noteSpecialPower( owner, at, at, TRUE );
	m_events.back().until = frame + EVENT_AFTERMATH_FRAMES;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::dropOldEvents( UnsignedInt frame )
{
	size_t kept = 0;
	for( size_t index = 0; index < m_events.size(); index++ )
	{
		if( frame < m_events[ index ].until )
			m_events[ kept++ ] = m_events[ index ];
	}
	m_events.resize( kept );
}

//-------------------------------------------------------------------------------------------------
DirectorEvent *ObserverCamera::findEvent( UnsignedInt id )
{
	for( size_t index = 0; index < m_events.size(); index++ )
	{
		if( m_events[ index ].id == id )
			return &m_events[ index ];
	}
	return NULL;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::notePlayerView( Int playerIndex, const ViewLocation &view )
{
	if( !m_playerViews[ playerIndex ].isValid() )
		DEBUG_LOG(( "OBSCAM frame %u first camera from player %d\n", TheGameLogic->getFrame(), playerIndex ));
	m_playerViews[ playerIndex ] = view;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::setMode( ObserverCameraMode mode )
{
	m_mode = mode;
	m_driving = FALSE;
	m_placeValid = FALSE;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::followPlayer( Int playerIndex )
{
	m_followed = playerIndex;
	m_driving = FALSE;
	m_placeValid = FALSE;
}

//-------------------------------------------------------------------------------------------------
Int ObserverCamera::getShroudPlayerIndex( void ) const
{
	return m_shroudViewer != NO_PLAYER ? m_shroudViewer : ThePlayerList->getLocalPlayer()->getPlayerIndex();
}

//-------------------------------------------------------------------------------------------------
/** The fog is the followed player's while it is on.  Swapping it is what the debug key that makes
	* you another player does to the fog: his ghosts of what he last saw in, and the ground redrawn
	* in his shroud.  A knocked-out player's machine kept only his own fog memory until now, so it
	* starts keeping everybody's the first time he asks for somebody else's; what the new viewer saw
	* before that has no snapshot and is not drawn in his fog, shadow included.
	*
	* The frame each drawable was last seen clear belongs to the old viewer and is cleared, or a unit
	* he saw stays drawn two seconds into the new viewer's fog. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::updateShroudViewer( void )
{
	const Int viewer = m_fog ? m_followed : NO_PLAYER;
	if( viewer == m_shroudViewer )
		return;

	m_shroudViewer = viewer;
	TheGhostObjectManager->setTrackAllPlayers( TRUE );
	TheGhostObjectManager->setLocalPlayerIndex( getShroudPlayerIndex() );
	ThePartitionManager->refreshShroudForLocalPlayer();
	for( Drawable *draw = TheGameClient->firstDrawable(); draw != NULL; draw = draw->getNextDrawable() )
		draw->setShroudClearFrame( 0 );
}

//-------------------------------------------------------------------------------------------------
/** While a player's camera is shown, his zoom is: the view otherwise eases its height back towards
	* the watcher's own every frame and the two meet two thirds of the way.  Only ever let go when
	* this took it, so the cinema's hold on the height is not undone. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::holdHeight( Bool hold )
{
	if( hold == m_holdingHeight )
		return;

	m_holdingHeight = hold;
	TheTacticalView->setOkToAdjustHeight( !hold );
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::takenByHand( const ViewLocation &current ) const
{
	if( TheLookAtTranslator->isMovingCamera() )
		return TRUE;

	const Real dx = current.getPosition().x - m_drivenTo.x;
	const Real dy = current.getPosition().y - m_drivenTo.y;
	return dx * dx + dy * dy > HAND_JUMP_DISTANCE * HAND_JUMP_DISTANCE;
}

//-------------------------------------------------------------------------------------------------
/** Where the director looks: the fight with the most at stake, held for a while, followed as it
	* moves, and left for a clearly bigger one.  With no fight anywhere it goes round what is worth
	* seeing instead, an army on the move, a base going up, a superweapon, a few seconds each and not
	* straight back to one it has just shown.  A special power beats all of it and is held until
	* nothing has hit the ground there for a few seconds.  Narrowed to one player it counts only the
	* hits on his things, the hits his things made, his own sights and the special powers he used or
	* had used on him.  It only reads the logic. */
//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::directorPlace( const Player *narrowTo, Coord2D *place )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( narrowTo != m_placeFor )
	{
		m_placeFor = narrowTo;
		m_placeValid = FALSE;
	}
	if( m_placeValid && frame >= m_placeScanned && frame < m_placeScanned + DIRECTOR_SCAN_FRAMES )
	{
		*place = m_place;
		return TRUE;
	}
	m_placeScanned = frame;

	std::vector< DirectorHeat > hits;
	std::vector< DirectorHeat > sights;
	for( Object *obj = TheGameLogic->getFirstObject(); obj != NULL; obj = obj->getNextObject() )
	{
		const Int cost = obj->getTemplate()->friend_getBuildCost();
		const Bool superweapon = obj->isKindOf( KINDOF_FS_SUPERWEAPON );
		const Bool mine = narrowTo == NULL || obj->getControllingPlayer() == narrowTo;
		DirectorHeat heat;
		heat.position.x = obj->getPosition()->x;
		heat.position.y = obj->getPosition()->y;

		if( mine && cost > 0 && !obj->isEffectivelyDead() )
		{
			const AIUpdateInterface *ai = obj->getAIUpdateInterface();
			const Bool marching = ai != NULL && ai->isMoving() && obj->isAbleToAttack();
			const Bool busy = marching || obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION );
			heat.weight = ObserverCamera_sightWeight( cost, obj->isKindOf( KINDOF_STRUCTURE ), busy, superweapon );
			sights.push_back( heat );
		}

		const BodyModuleInterface *body = obj->getBodyModule();
		const UnsignedInt hitAt = body->getLastDamageTimestamp();
		if( hitAt == 0 || frame >= hitAt + DIRECTOR_HEAT_FRAMES )
			continue;
		if( !mine && ( body->getLastDamageInfo()->in.m_sourcePlayerMask & narrowTo->getPlayerMask() ) == 0 )
			continue;

		heat.weight = ObserverCamera_hitWeight( cost, obj->isEffectivelyDead(), superweapon );
		hits.push_back( heat );
	}

	Coord2D hottest;
	Real hottestHeat = 0.0f;
	ObserverCamera_hottestPlace( hits, &hottest, &hottestHeat );

	const UnsignedInt held = frame - m_placeSince;
	Coord2D followed = m_place;

	// a special power outranks any fight.  Narrowed to one player, only his own and the ones that
	// land on his things count, the way only his fights do
	dropOldEvents( frame );
	DirectorEvent *best = NULL;
	for( size_t index = 0; index < m_events.size(); index++ )
	{
		DirectorEvent &event = m_events[ index ];
		Coord2D middle;
		if( event.owner != narrowTo && narrowTo != NULL && ObserverCamera_heatAround( sights, event.target, &middle ) <= 0.0f )
			continue;
		// hits at the target keep it going: the missiles arriving, the bombs, the fires after
		if( frame >= event.since + EVENT_LAUNCH_FRAMES && ObserverCamera_heatAround( hits, event.target, &middle ) > 0.0f )
			event.until = max( event.until, frame + EVENT_AFTERMATH_FRAMES );
		if( best == NULL || event.weight >= best->weight )
			best = &event;
	}
	DirectorEvent *current = m_placeValid && m_placeKind == PLACE_EVENT ? findEvent( m_placeEvent ) : NULL;
	if( current != NULL && ( best == current || !ObserverCamera_shouldMove( current->weight, best->weight, held ) ) )
	{
		m_place = ObserverCamera_eventPlace( *current, frame );
		*place = m_place;
		return TRUE;
	}
	// an event that just ended hands straight over to the next; anything else is given its settle first
	if( best != NULL && ( !m_placeValid || m_placeKind == PLACE_EVENT || held >= DIRECTOR_SETTLE_FRAMES ) )
	{
		DEBUG_LOG(( "OBSCAM frame %u director to special power %u at (%.0f,%.0f)%s\n", frame, best->id,
			best->target.x, best->target.y, best->superweapon ? " superweapon" : "" ));
		m_place = ObserverCamera_eventPlace( *best, frame );
		m_placeKind = PLACE_EVENT;
		m_placeEvent = best->id;
		m_placeSince = frame;
		m_placeValid = TRUE;
		*place = m_place;
		return TRUE;
	}

	if( m_placeValid && m_placeKind == PLACE_FIGHT )
	{
		const Real heatHere = ObserverCamera_heatAround( hits, m_place, &followed );
		if( heatHere > 0.0f && ( sameFight( hottest, followed ) || !ObserverCamera_shouldMove( heatHere, hottestHeat, held ) ) )
		{
			const Real dx = followed.x - m_place.x;
			const Real dy = followed.y - m_place.y;
			if( dx * dx + dy * dy > DIRECTOR_FOLLOW_SLACK * DIRECTOR_FOLLOW_SLACK )
				m_place = followed;
			*place = m_place;
			return TRUE;
		}
	}
	else if( m_placeValid && m_placeKind == PLACE_SIGHT && hottestHeat <= 0.0f && held < DIRECTOR_SIGHT_FRAMES )
	{
		if( ObserverCamera_heatAround( sights, m_place, &followed ) > 0.0f )
			m_place = followed;
		*place = m_place;
		return TRUE;
	}

	if( hottestHeat > 0.0f )
	{
		DEBUG_LOG(( "OBSCAM frame %u director to fight (%.0f,%.0f) heat %.1f\n", frame, hottest.x, hottest.y, hottestHeat ));
		m_place = hottest;
		m_placeKind = PLACE_FIGHT;
	}
	else
	{
		Coord2D sight;
		if( !ObserverCamera_nextSight( sights, m_seen, &sight ) )
		{
			m_seen.clear();
			if( !ObserverCamera_nextSight( sights, m_seen, &sight ) )
			{
				m_placeValid = FALSE;
				return FALSE;
			}
		}
		DEBUG_LOG(( "OBSCAM frame %u director to sight (%.0f,%.0f)\n", frame, sight.x, sight.y ));
		m_place = sight;
		m_placeKind = PLACE_SIGHT;
		m_seen.push_back( sight );
		if( m_seen.size() > DIRECTOR_SEEN_COUNT )
			m_seen.erase( m_seen.begin() );
	}
	m_placeSince = frame;
	m_placeValid = TRUE;
	*place = m_place;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::isShowingPlayerView( void ) const
{
	return m_mode == OBSERVER_CAMERA_PLAYER && m_followed != NO_PLAYER && m_playerViews[ m_followed ].isValid();
}

//-------------------------------------------------------------------------------------------------
/** The director with a player picked keeps to his fights; a player's camera with nobody picked has
	* nothing to show and leaves the camera where it is. */
//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::chooseTarget( const ViewLocation &current, ViewLocation *target )
{
	if( m_mode == OBSERVER_CAMERA_PLAYER && m_followed == NO_PLAYER )
		return FALSE;

	if( isShowingPlayerView() )
	{
		*target = m_playerViews[ m_followed ];
		return TRUE;
	}

	const Player *narrowTo = m_followed == NO_PLAYER ? NULL : ThePlayerList->getNthPlayer( m_followed );
	const Coord3D &at = current.getPosition();
	Coord2D place;
	if( !directorPlace( narrowTo, &place ) )
		return FALSE;
	target->init( place.x, place.y, at.z, current.getAngle(), current.getPitch(), current.getZoom() );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::update( UnsignedInt nowMilliseconds )
{
	const UnsignedInt elapsed = m_lastUpdate == 0 ? 0 : min( nowMilliseconds - m_lastUpdate, LONGEST_STEP_MILLISECONDS );
	m_lastUpdate = nowMilliseconds;

	updateShroudViewer();

	if( m_mode == OBSERVER_CAMERA_FREE )
	{
		m_driving = FALSE;
		holdHeight( FALSE );
		return;
	}

	ViewLocation current;
	TheTacticalView->getLocation( &current );
	// the followed player stays followed, for his fog, with the camera back in the watcher's hands
	if( m_driving && takenByHand( current ) )
	{
		DEBUG_LOG(( "OBSCAM frame %u the watcher took the camera\n", TheGameLogic->getFrame() ));
		m_mode = OBSERVER_CAMERA_FREE;
		m_driving = FALSE;
		holdHeight( FALSE );
		return;
	}

	ViewLocation target;
	if( !chooseTarget( current, &target ) )
	{
		m_driving = FALSE;
		holdHeight( FALSE );
		return;
	}

	holdHeight( isShowingPlayerView() );
	if( !m_driving )
		m_velocity.x = m_velocity.y = m_velocity.z = m_velocity.angle = m_velocity.pitch = m_velocity.zoom = 0.0f;
	const Bool player = m_mode == OBSERVER_CAMERA_PLAYER;
	const ViewLocation step = ObserverCamera_approach( current, target, elapsed / MILLISECONDS_PER_SECOND,
		player ? PLAYER_PAN_SECONDS : DIRECTOR_PAN_SECONDS, player ? PLAYER_TOP_SPEED : DIRECTOR_TOP_SPEED, &m_velocity );
	TheTacticalView->setLocation( &step );
	m_drivenTo = step.getPosition();
	m_driving = TRUE;
}
