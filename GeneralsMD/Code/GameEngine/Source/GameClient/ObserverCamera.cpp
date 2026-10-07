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
#include "GameClient/Display.h"
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
#include "GameLogic/TerrainLogic.h"

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
/// the production strip or a jump to a group.  Where it was put is taken after the view's own
/// constraint, so the edge of the map pulling it back does not count.  The radar
/// hands the camera over itself, since a click near the camera and a drag both move it by less
static const Real HAND_JUMP_DISTANCE = 400.0f;
/// roughly how long the director's glide takes to arrive, easing in and out
static const Real DIRECTOR_PAN_SECONDS = 1.4f;
/// the director's glide never crosses the ground faster than this, about two screens a second
static const Real DIRECTOR_TOP_SPEED = 900.0f;
/// a fight this tight is watched from the watcher's own height; wider, the camera rises this much
/// for every unit further, up to the most.  A fight's hits lie within DIRECTOR_GATHER_RADIUS of one
/// another, so the most is reached only by the widest
static const Real DIRECTOR_TIGHT_SPREAD = 60.0f;
static const Real DIRECTOR_HEIGHT_PER_SPREAD = 1.5f;
static const Real DIRECTOR_MOST_EXTRA_HEIGHT = 300.0f;
/// a player's camera comes a few times a second, and this smooths the steps between
static const Real PLAYER_PAN_SECONDS = 0.15f;
static const Real PLAYER_TOP_SPEED = 4000.0f;
/// a frame longer than this is a hitch, and is not allowed to throw the camera across the map
static const UnsignedInt LONGEST_STEP_MILLISECONDS = 100;
static const Real MILLISECONDS_PER_SECOND = 1000.0f;
/// -directorrecord: a second fight closer than this to the first shares its ground, and the split
/// would show one fight twice
static const Real SPLIT_APART = 3.0f * DIRECTOR_GATHER_RADIUS;
/// the picture splits for a second fight at least this hot, and at least this share of the first
static const Real SPLIT_ENTER_HEAT = 2.0f;
static const Real SPLIT_ENTER_SHARE = 0.5f;
/// a split stays this long at least, and after that while the second fight keeps this share
static const UnsignedInt SPLIT_HOLD_FRAMES = 6 * LOGICFRAMES_PER_SECOND;
static const Real SPLIT_STAY_SHARE = 0.25f;
/// once the picture is whole again it stays whole this long, so it does not flicker between the two
static const UnsignedInt SPLIT_REST_FRAMES = 4 * LOGICFRAMES_PER_SECOND;
/// the corner radar takes this long to slide out to the left before the panes come, and back after
static const UnsignedInt PANE_RADAR_FRAMES = 12;
/// the panes take this long to slide in along the rays, and out again
static const UnsignedInt PANE_SLIDE_FRAMES = 15;
/// the match opens on every player's base, one pane each, for this long
static const UnsignedInt PANE_INTRO_FRAMES = 7 * LOGICFRAMES_PER_SECOND;
/// how much higher the cameras stand with the panes all in, by how many panes there are
static const Real PANE_ZOOM_TWO = 1.3f;
static const Real PANE_ZOOM_THREE = 1.4f;
static const Real PANE_ZOOM_FOUR_OR_MORE = 1.5f;
/// a pane's middle is measured on a grid this coarse, which is plenty for where to put a subject
static const Int PANE_MIDDLE_COLUMNS = 64;
static const Int PANE_MIDDLE_ROWS = 36;

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
Bool ObserverCamera_stayOnEvent( const DirectorEvent *current, const DirectorEvent *best, UnsignedInt held )
{
	if( current == NULL )
		return FALSE;
	return best == NULL || best == current || !ObserverCamera_shouldMove( current->weight, best->weight, held );
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_eventCutsIn( const DirectorEvent &event, UnsignedInt frame, UnsignedInt held )
{
	return held >= DIRECTOR_SETTLE_FRAMES || ( event.superweapon && frame < event.since + EVENT_LAUNCH_FRAMES );
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_spreadAround( const std::vector< DirectorHeat > &hits, const Coord2D &around )
{
	Real weight = 0.0f;
	Real squares = 0.0f;
	for( size_t index = 0; index < hits.size(); index++ )
	{
		const DirectorHeat &hit = hits[ index ];
		if( !sameFight( hit.position, around ) )
			continue;

		const Real dx = hit.position.x - around.x;
		const Real dy = hit.position.y - around.y;
		weight += hit.weight;
		squares += ( dx * dx + dy * dy ) * hit.weight;
	}
	return weight > 0.0f ? sqrtf( squares / weight ) : 0.0f;
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_fightHeight( Real spread )
{
	const Real extra = ( spread - DIRECTOR_TIGHT_SPREAD ) * DIRECTOR_HEIGHT_PER_SPREAD;
	return min( max( extra, 0.0f ), DIRECTOR_MOST_EXTRA_HEIGHT );
}

//-------------------------------------------------------------------------------------------------
/** Each axis on its own: the lowest corner may not go below the map's low edge, nor the highest
	* past its high one.  When the screen is wider than the map the two bounds cross, and anywhere
	* between them shows as little off the map as the screen can. */
//-------------------------------------------------------------------------------------------------
static Real keepAxisInMap( Real place, Real lowestCorner, Real highestCorner, Real mapLow, Real mapHigh )
{
	const Real low = mapLow - lowestCorner;
	const Real high = mapHigh - highestCorner;
	return min( max( place, min( low, high ) ), max( low, high ) );
}

//-------------------------------------------------------------------------------------------------
Coord2D ObserverCamera_keepInMap( const Coord2D &place, const Coord2D *corners, Int count, const Region2D &map )
{
	Coord2D lowest = corners[ 0 ];
	Coord2D highest = corners[ 0 ];
	for( Int index = 1; index < count; index++ )
	{
		lowest.x = min( lowest.x, corners[ index ].x );
		lowest.y = min( lowest.y, corners[ index ].y );
		highest.x = max( highest.x, corners[ index ].x );
		highest.y = max( highest.y, corners[ index ].y );
	}

	Coord2D kept;
	kept.x = keepAxisInMap( place.x, lowest.x, highest.x, map.lo.x, map.hi.x );
	kept.y = keepAxisInMap( place.y, lowest.y, highest.y, map.lo.y, map.hi.y );
	return kept;
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
Bool ObserverCamera_secondPlace( const std::vector< DirectorHeat > &hits, const Coord2D &first, Coord2D *place, Real *heat )
{
	std::vector< DirectorHeat > apart;
	for( size_t index = 0; index < hits.size(); index++ )
	{
		const Real dx = hits[ index ].position.x - first.x;
		const Real dy = hits[ index ].position.y - first.y;
		if( dx * dx + dy * dy > SPLIT_APART * SPLIT_APART )
			apart.push_back( hits[ index ] );
	}
	return ObserverCamera_hottestPlace( apart, place, heat );
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_holdSplit( Bool split, Real firstHeat, Real secondHeat, Real apart, UnsignedInt framesSince )
{
	// two halves of one fight, or a half with no fight left in it, is no split at all
	if( secondHeat <= 0.0f || apart <= SPLIT_APART )
		return FALSE;
	if( split )
		return framesSince < SPLIT_HOLD_FRAMES || secondHeat >= firstHeat * SPLIT_STAY_SHARE;
	return framesSince >= SPLIT_REST_FRAMES && firstHeat > 0.0f && secondHeat >= SPLIT_ENTER_HEAT
		&& secondHeat >= firstHeat * SPLIT_ENTER_SHARE;
}

//-------------------------------------------------------------------------------------------------
/** Two panes are one diagonal, its upper left half the radar's; four are an X.  The others add rays to
	* the X, the bottom first: three is a Y, the top wedge and two halves below; five is the X with the
	* bottom wedge halved; six halves the top wedge as well; seven adds the right half of the level
	* line, and eight is every 45 degrees. */
//-------------------------------------------------------------------------------------------------
Int ObserverCamera_paneLayout( Int count, Real *rays )
{
	static const Real two[] = { 45, 225 };
	static const Real three[] = { 45, 135, 270 };
	static const Real four[] = { 45, 135, 225, 315 };
	static const Real five[] = { 45, 135, 225, 270, 315 };
	static const Real six[] = { 45, 90, 135, 225, 270, 315 };
	static const Real seven[] = { 0, 45, 90, 135, 225, 270, 315 };
	static const Real eight[] = { 0, 45, 90, 135, 180, 225, 270, 315 };
	static const Real *layouts[] = { two, three, four, five, six, seven, eight };
	if( count < 2 )
		return 0;
	count = min( count, (Int)OBSERVER_MOST_PANES );
	for( Int ray = 0; ray < count; ray++ )
		rays[ ray ] = layouts[ count - 2 ][ ray ];
	return count;
}

//-------------------------------------------------------------------------------------------------
Int ObserverCamera_paneOf( Real x, Real y, Real originX, Real originY, const Real *rays, Int count )
{
	Real angle = atan2f( originY - y, x - originX ) * 180.0f / PI;
	if( angle < 0.0f )
		angle += 360.0f;
	// below the first ray is the far side of the last pane, which wraps round through 0
	Int pane = count - 1;
	for( Int ray = 0; ray < count; ray++ )
	{
		if( angle >= rays[ ray ] )
			pane = ray;
	}
	return pane;
}

//-------------------------------------------------------------------------------------------------
Coord2D ObserverCamera_paneExitDirection( const Real *rays )
{
	const Real away = ( ( rays[ 0 ] + rays[ 1 ] ) * 0.5f + 180.0f ) * PI / 180.0f;
	Coord2D direction;
	direction.x = cosf( away );
	direction.y = -sinf( away );
	return direction;
}

//-------------------------------------------------------------------------------------------------
/** Pane 0 is a wedge of 180 degrees or less, so it holds the whole picture once it holds the four
	* corners. */
//-------------------------------------------------------------------------------------------------
Real ObserverCamera_paneExit( const Real *rays, Int count, Int width, Int height )
{
	const Coord2D direction = ObserverCamera_paneExitDirection( rays );
	const Real cornerX[ 4 ] = { 0.5f, width - 0.5f, 0.5f, width - 0.5f };
	const Real cornerY[ 4 ] = { 0.5f, 0.5f, height - 0.5f, height - 0.5f };
	Real inside = 2.0f * ( width + height );
	Real outside = 0.0f;
	for( Int step = 0; step < 32; step++ )
	{
		const Real tried = ( inside + outside ) * 0.5f;
		const Real originX = width * 0.5f + direction.x * tried;
		const Real originY = height * 0.5f + direction.y * tried;
		Bool whole = TRUE;
		for( Int corner = 0; corner < 4 && whole; corner++ )
			whole = ObserverCamera_paneOf( cornerX[ corner ], cornerY[ corner ], originX, originY, rays, count ) == 0;
		if( whole )
			inside = tried;
		else
			outside = tried;
	}
	return inside;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera_paneMiddles( const Real *rays, Int count, Int width, Int height, Coord2D *middles )
{
	Int samples[ OBSERVER_MOST_PANES ];
	for( Int pane = 0; pane < count; pane++ )
	{
		samples[ pane ] = 0;
		middles[ pane ].x = middles[ pane ].y = 0.0f;
	}
	for( Int row = 0; row < PANE_MIDDLE_ROWS; row++ )
	{
		for( Int column = 0; column < PANE_MIDDLE_COLUMNS; column++ )
		{
			const Real x = ( column + 0.5f ) * width / PANE_MIDDLE_COLUMNS;
			const Real y = ( row + 0.5f ) * height / PANE_MIDDLE_ROWS;
			const Int pane = ObserverCamera_paneOf( x, y, width * 0.5f, height * 0.5f, rays, count );
			middles[ pane ].x += x;
			middles[ pane ].y += y;
			samples[ pane ]++;
		}
	}
	for( Int pane = 0; pane < count; pane++ )
	{
		middles[ pane ].x /= samples[ pane ];
		middles[ pane ].y /= samples[ pane ];
	}
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_easeFrames( UnsignedInt frame, UnsignedInt start, UnsignedInt length )
{
	if( frame <= start )
		return 0.0f;
	const Real t = min( (Real)( frame - start ) / length, 1.0f );
	return t * t * ( 3.0f - 2.0f * t );
}

//-------------------------------------------------------------------------------------------------
/** A wedge is a slice of the screen, so its subject wants more ground round it than the whole
	* screen gave; more panes are narrower slices and stand higher still. */
//-------------------------------------------------------------------------------------------------
Real ObserverCamera_paneZoom( Int count, Real progress )
{
	if( count < 2 )
		return 1.0f;
	const Real highest = count == 2 ? PANE_ZOOM_TWO : count == 3 ? PANE_ZOOM_THREE : PANE_ZOOM_FOUR_OR_MORE;
	return 1.0f + ( highest - 1.0f ) * progress;
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
	m_heightDriven = FALSE;
	m_handHeight = 0.0f;
	m_drivenHeight = 0.0f;
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
	m_placeHeight = 0.0f;
	m_placeEvent = 0;
	m_seen.clear();
	m_events.clear();
	m_nextEventId = 1;
	m_fights.clear();
	m_split = FALSE;
	m_splitChanged = 0;
	m_secondPlace.x = m_secondPlace.y = 0.0f;
	m_panePhase = PANES_NONE;
	m_panePhaseStart = 0;
	m_intro = FALSE;
	m_introDone = FALSE;
	m_introGlide = FALSE;
	m_panesLeftPlace.x = m_panesLeftPlace.y = 0.0f;
	m_paneCount = 0;
	m_paneProgress = 0.0f;
	m_paneExit = 0.0f;
	m_paneBaseZoom = 1.0f;
	m_paneOrigin.x = m_paneOrigin.y = 0.0f;
	m_cornerRadarSlide = 0.0f;
	m_radarFrame.lo.x = m_radarFrame.lo.y = m_radarFrame.hi.x = m_radarFrame.hi.y = 0;
	for( Int pane = 0; pane < OBSERVER_MOST_PANES; pane++ )
	{
		m_paneRays[ pane ] = 0.0f;
		m_panePlayers[ pane ] = NULL;
		m_paneSubject[ pane ].x = m_paneSubject[ pane ].y = 0.0f;
		m_paneGlide[ pane ] = ViewLocation();
		m_paneVelocity[ pane ].x = m_paneVelocity[ pane ].y = m_paneVelocity[ pane ].z = 0.0f;
		m_paneVelocity[ pane ].angle = m_paneVelocity[ pane ].pitch = m_paneVelocity[ pane ].zoom = 0.0f;
		m_paneView[ pane ] = ViewLocation();
	}
	m_mainOffset.x = m_mainOffset.y = 0.0f;
	m_firstView = ViewLocation();
	m_drawingPane = 0;
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
/** The director's height over a fight is the watcher's own plus extra.  It is set as the height
	* the view wants, so the view's own easing carries the camera there rather than a jump.  A turn
	* of the wheel while it drives moves the height the view wants, and that turn is the watcher's:
	* it is added to his own height, so the director never takes it back. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::driveHeight( Real extra )
{
	const Real now = TheTacticalView->getHeightAboveGround();
	if( m_heightDriven )
		m_handHeight += now - m_drivenHeight;
	else
		m_handHeight = now;
	m_heightDriven = TRUE;

	const Real wanted = m_handHeight + extra;
	if( wanted != now )
		TheTacticalView->setHeightAboveGround( wanted );
	m_drivenHeight = TheTacticalView->getHeightAboveGround();
}

//-------------------------------------------------------------------------------------------------
/** The watcher's own height back, with any turn of the wheel since the last frame kept. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::releaseHeight( void )
{
	if( !m_heightDriven )
		return;

	m_heightDriven = FALSE;
	TheTacticalView->setHeightAboveGround( m_handHeight + TheTacticalView->getHeightAboveGround() - m_drivenHeight );
}

//-------------------------------------------------------------------------------------------------
/** The screen's corners on the ground round the point looked at now, so the place keeps the whole
	* screen over the map.  They are measured at the current zoom: while the camera rises the corners
	* spread and the place is pulled further in, frame by frame. */
//-------------------------------------------------------------------------------------------------
Coord2D ObserverCamera::keepInMap( const Coord2D &place, const ViewLocation &current ) const
{
	const Coord3D &at = current.getPosition();
	Coord3D world[ 4 ];
	TheTacticalView->getScreenCornerWorldPointsAtZ( &world[ 0 ], &world[ 1 ], &world[ 2 ], &world[ 3 ],
		TheTerrainLogic->getGroundHeight( at.x, at.y ) );
	Coord2D corners[ 4 ];
	for( Int index = 0; index < 4; index++ )
	{
		corners[ index ].x = world[ index ].x - at.x;
		corners[ index ].y = world[ index ].y - at.y;
	}

	Region3D extent;
	TheTerrainLogic->getExtent( &extent );
	Region2D map;
	map.lo.x = extent.lo.x;
	map.lo.y = extent.lo.y;
	map.hi.x = extent.hi.x;
	map.hi.y = extent.hi.y;
	return ObserverCamera_keepInMap( place, corners, 4, map );
}

//-------------------------------------------------------------------------------------------------
/** A step across the screen in pixels, from its middle, as a step on the ground: moving the look point
	* back by it puts what was in the middle of the screen at that many pixels from it. */
//-------------------------------------------------------------------------------------------------
Coord2D ObserverCamera::screenToGround( const ViewLocation &current, const Coord2D &pixels ) const
{
	const Coord3D &at = current.getPosition();
	Coord3D world[ 4 ];
	TheTacticalView->getScreenCornerWorldPointsAtZ( &world[ 0 ], &world[ 1 ], &world[ 2 ], &world[ 3 ],
		TheTerrainLogic->getGroundHeight( at.x, at.y ) );
	// the corners come top left, top right, bottom right, bottom left; opposite edges are averaged
	const Real acrossX = ( world[ 1 ].x - world[ 0 ].x + world[ 2 ].x - world[ 3 ].x ) * 0.5f;
	const Real acrossY = ( world[ 1 ].y - world[ 0 ].y + world[ 2 ].y - world[ 3 ].y ) * 0.5f;
	const Real downX = ( world[ 3 ].x - world[ 0 ].x + world[ 2 ].x - world[ 1 ].x ) * 0.5f;
	const Real downY = ( world[ 3 ].y - world[ 0 ].y + world[ 2 ].y - world[ 1 ].y ) * 0.5f;
	const Real right = pixels.x / TheDisplay->getWidth();
	const Real down = pixels.y / TheDisplay->getHeight();
	Coord2D ground;
	ground.x = acrossX * right + downX * down;
	ground.y = acrossY * right + downY * down;
	return ground;
}

//-------------------------------------------------------------------------------------------------
/** -directorrecord: whether the picture is split, and the second fight it shows.  Asked again only
	* when the hits were counted again.  Only real fighting counts, on both sides of the line: a base
	* going up, or a dozer clearing trees, is no reason to split.  A special power the director is
	* showing counts as a fight beside it. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::updateSplit( void )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	Coord2D middle;
	Real firstHeat = ObserverCamera_heatAround( m_fights, m_place, &middle );
	if( m_placeKind == PLACE_EVENT )
		firstHeat = max( firstHeat, SPLIT_ENTER_HEAT );
	Coord2D second = { 0.0f, 0.0f };
	Real secondHeat = 0.0f;
	ObserverCamera_secondPlace( m_fights, m_place, &second, &secondHeat );

	// the second fight is followed once it has moved a little, the way the director follows its own
	Coord2D shown = m_secondPlace;
	const Real dx = second.x - m_secondPlace.x;
	const Real dy = second.y - m_secondPlace.y;
	if( !m_split || dx * dx + dy * dy > DIRECTOR_FOLLOW_SLACK * DIRECTOR_FOLLOW_SLACK )
		shown = second;
	const Real ax = shown.x - m_place.x;
	const Real ay = shown.y - m_place.y;
	const Real apart = sqrtf( ax * ax + ay * ay );

	const UnsignedInt since = frame >= m_splitChanged ? frame - m_splitChanged : 0;
	const Bool split = ObserverCamera_holdSplit( m_split, firstHeat, secondHeat, apart, since );
	if( split != m_split )
	{
		DEBUG_LOG(( "OBSCAM frame %u split %s, first heat %.1f, second (%.0f,%.0f) heat %.1f, %.0f apart\n", frame,
			split ? "on" : "off", firstHeat, shown.x, shown.y, secondHeat, apart ));
		m_split = split;
		m_splitChanged = frame;
	}
	if( m_split )
		m_secondPlace = shown;
}

//-------------------------------------------------------------------------------------------------
/** The panes' timeline, on logic frames, which is what each recorded picture is.  A split: the corner
	* radar slides out, the second pane slides in along the diagonal with the framed radar on its
	* corner, and it all goes back the same way when the split ends.  The match opens with a pane a
	* player, held a few seconds and then slid away to leave pane 0. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::advancePanes( UnsignedInt frame )
{
	static const char *const phaseNames[] = { "none", "radar out", "in", "held", "out", "radar in" };
	PanePhase next = m_panePhase;
	const UnsignedInt elapsed = frame >= m_panePhaseStart ? frame - m_panePhaseStart : 0;
	switch( m_panePhase )
	{
		case PANES_NONE:
			if( !m_introDone )
			{
				m_introDone = TRUE;
				Int players = 0;
				for( Int index = 0; index < ThePlayerList->getPlayerCount() && players < OBSERVER_MOST_PANES; index++ )
				{
					const Player *player = ThePlayerList->getNthPlayer( index );
					if( player->isPlayableSide() && !player->isPlayerObserver() && player->isPlayerActive() )
						m_panePlayers[ players++ ] = player;
				}
				m_paneCount = ObserverCamera_paneLayout( players, m_paneRays );
				if( m_paneCount >= 2 )
				{
					m_intro = TRUE;
					next = PANES_HELD;
				}
			}
			else if( m_split )
			{
				m_paneCount = ObserverCamera_paneLayout( 2, m_paneRays );
				next = PANES_RADAR_OUT;
			}
			if( next != PANES_NONE )
			{
				m_paneExit = ObserverCamera_paneExit( m_paneRays, m_paneCount, TheDisplay->getWidth(), TheDisplay->getHeight() );
				m_paneBaseZoom = TheTacticalView->getZoom();
				for( Int pane = 0; pane < OBSERVER_MOST_PANES; pane++ )
					m_paneGlide[ pane ] = ViewLocation();
			}
			break;
		case PANES_RADAR_OUT:
			if( elapsed >= PANE_RADAR_FRAMES )
				next = PANES_IN;
			break;
		case PANES_IN:
			if( elapsed >= PANE_SLIDE_FRAMES )
				next = PANES_HELD;
			break;
		case PANES_HELD:
			if( m_intro ? elapsed >= PANE_INTRO_FRAMES : !m_split )
				next = PANES_OUT;
			break;
		case PANES_OUT:
			if( elapsed >= PANE_SLIDE_FRAMES )
			{
				next = PANES_RADAR_IN;
				m_introGlide = m_intro;
			}
			break;
		case PANES_RADAR_IN:
			if( elapsed >= PANE_RADAR_FRAMES )
			{
				next = PANES_NONE;
				m_paneCount = 0;
				m_intro = FALSE;
			}
			break;
	}
	if( next != m_panePhase )
	{
		DEBUG_LOG(( "OBSCAM frame %u panes %s, %d of them%s\n", frame, phaseNames[ next ], m_paneCount, m_intro ? ", the opening" : "" ));
		m_panePhase = next;
		m_panePhaseStart = frame;
	}

	switch( m_panePhase )
	{
		case PANES_NONE:
			m_paneProgress = 0.0f;
			m_cornerRadarSlide = 0.0f;
			break;
		case PANES_RADAR_OUT:
			m_paneProgress = 0.0f;
			m_cornerRadarSlide = ObserverCamera_easeFrames( frame, m_panePhaseStart, PANE_RADAR_FRAMES );
			break;
		case PANES_IN:
			m_paneProgress = ObserverCamera_easeFrames( frame, m_panePhaseStart, PANE_SLIDE_FRAMES );
			m_cornerRadarSlide = 1.0f;
			break;
		case PANES_HELD:
			m_paneProgress = 1.0f;
			m_cornerRadarSlide = 1.0f;
			break;
		case PANES_OUT:
			m_paneProgress = 1.0f - ObserverCamera_easeFrames( frame, m_panePhaseStart, PANE_SLIDE_FRAMES );
			m_cornerRadarSlide = 1.0f;
			break;
		case PANES_RADAR_IN:
			m_paneProgress = 0.0f;
			m_cornerRadarSlide = 1.0f - ObserverCamera_easeFrames( frame, m_panePhaseStart, PANE_RADAR_FRAMES );
			break;
	}

	m_paneOrigin.x = TheDisplay->getWidth() * 0.5f;
	m_paneOrigin.y = TheDisplay->getHeight() * 0.5f;
	if( m_paneCount >= 2 )
	{
		const Coord2D away = ObserverCamera_paneExitDirection( m_paneRays );
		m_paneOrigin.x += away.x * ( 1.0f - m_paneProgress ) * m_paneExit;
		m_paneOrigin.y += away.y * ( 1.0f - m_paneProgress ) * m_paneExit;
	}
}

//-------------------------------------------------------------------------------------------------
/** The opening's panes: where each player's things crowd, his base at the start and his army once it
	* is bigger than the base. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::updateIntroPlaces( void )
{
	std::vector< DirectorHeat > sights[ OBSERVER_MOST_PANES ];
	for( Object *obj = TheGameLogic->getFirstObject(); obj != NULL; obj = obj->getNextObject() )
	{
		const Int cost = obj->getTemplate()->friend_getBuildCost();
		if( cost <= 0 || obj->isEffectivelyDead() )
			continue;
		for( Int pane = 0; pane < m_paneCount; pane++ )
		{
			if( obj->getControllingPlayer() != m_panePlayers[ pane ] )
				continue;
			DirectorHeat heat;
			heat.position.x = obj->getPosition()->x;
			heat.position.y = obj->getPosition()->y;
			heat.weight = ObserverCamera_sightWeight( cost, obj->isKindOf( KINDOF_STRUCTURE ), FALSE, FALSE ) + 1.0f;
			sights[ pane ].push_back( heat );
		}
	}
	for( Int pane = 0; pane < m_paneCount; pane++ )
	{
		Real heat = 0.0f;
		ObserverCamera_hottestPlace( sights[ pane ], &m_paneSubject[ pane ], &heat );
	}
}

//-------------------------------------------------------------------------------------------------
/** Each pane past the first glides to its subject on its own, and its camera sits back from the
	* subject by the pane's middle, so the subject is in the middle of its pane.  While the rays' meeting
	* point slides, the camera sits back by that much more, so the pane's picture slides with it and
	* is the world on the whole screen, with no edge of a moved picture to show.  Angle, pitch and zoom
	* are the main camera's. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::stepPaneCameras( const ViewLocation &step, Real elapsedSeconds )
{
	if( m_paneCount < 2 )
		return;
	if( !m_intro )
		m_paneSubject[ 1 ] = m_secondPlace;

	Coord2D middles[ OBSERVER_MOST_PANES ];
	ObserverCamera_paneMiddles( m_paneRays, m_paneCount, TheDisplay->getWidth(), TheDisplay->getHeight(), middles );
	const Coord3D &at = step.getPosition();
	for( Int pane = 1; pane < m_paneCount; pane++ )
	{
		ViewLocation subject;
		subject.init( m_paneSubject[ pane ].x, m_paneSubject[ pane ].y, at.z, step.getAngle(), step.getPitch(), step.getZoom() );
		if( !m_paneGlide[ pane ].isValid() )
		{
			m_paneVelocity[ pane ].x = m_paneVelocity[ pane ].y = m_paneVelocity[ pane ].z = 0.0f;
			m_paneVelocity[ pane ].angle = m_paneVelocity[ pane ].pitch = m_paneVelocity[ pane ].zoom = 0.0f;
			m_paneGlide[ pane ] = subject;
		}
		else
		{
			const ViewLocation glide = ObserverCamera_approach( m_paneGlide[ pane ], subject, elapsedSeconds,
				DIRECTOR_PAN_SECONDS, DIRECTOR_TOP_SPEED, &m_paneVelocity[ pane ] );
			m_paneGlide[ pane ].init( glide.getPosition().x, glide.getPosition().y, at.z, step.getAngle(), step.getPitch(), step.getZoom() );
		}

		Coord2D fromMiddle;
		fromMiddle.x = middles[ pane ].x + m_paneOrigin.x - TheDisplay->getWidth();
		fromMiddle.y = middles[ pane ].y + m_paneOrigin.y - TheDisplay->getHeight();
		const Coord2D offset = screenToGround( step, fromMiddle );
		Coord2D camera;
		camera.x = m_paneGlide[ pane ].getPosition().x - offset.x;
		camera.y = m_paneGlide[ pane ].getPosition().y - offset.y;
		camera = keepInMap( camera, step );
		m_paneView[ pane ].init( camera.x, camera.y, at.z, step.getAngle(), step.getPitch(), step.getZoom() );
	}
}

//-------------------------------------------------------------------------------------------------
/** On the meeting point when the panes are all in, and far enough out along its way off the screen,
	* the radar's own size further, that none of it shows when they have gone. */
//-------------------------------------------------------------------------------------------------
Coord2D ObserverCamera::getFramedRadarMiddle( Real radarDiagonal ) const
{
	const Coord2D away = ObserverCamera_paneExitDirection( m_paneRays );
	const Real out = ( 1.0f - m_paneProgress ) * ( m_paneExit + radarDiagonal );
	Coord2D middle;
	middle.x = TheDisplay->getWidth() * 0.5f + away.x * out;
	middle.y = TheDisplay->getHeight() * 0.5f + away.y * out;
	return middle;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::beginPanePass( Int pane )
{
	TheTacticalView->getLocation( &m_firstView );
	TheTacticalView->setLocation( &m_paneView[ pane ] );
	m_drawingPane = pane;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::endPanePass( void )
{
	TheTacticalView->setLocation( &m_firstView );
	m_drawingPane = 0;
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
	std::vector< DirectorHeat > fights;
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

		// a thing with no body module takes no damage, so it has no hits to count
		const BodyModuleInterface *body = obj->getBodyModule();
		if( body == NULL )
			continue;
		const UnsignedInt hitAt = body->getLastDamageTimestamp();
		if( hitAt == 0 || frame >= hitAt + DIRECTOR_HEAT_FRAMES )
			continue;
		if( !mine && ( body->getLastDamageInfo()->in.m_sourcePlayerMask & narrowTo->getPlayerMask() ) == 0 )
			continue;

		heat.weight = ObserverCamera_hitWeight( cost, obj->isEffectivelyDead(), superweapon );
		hits.push_back( heat );

		// -directorrecord's split counts a hit only when one player dealt it to another he is at war
		// with: a tree a dozer cleared, or a building's own wear, is no fight
		const PlayerMaskType sourceMask = body->getLastDamageInfo()->in.m_sourcePlayerMask;
		const Player *victim = obj->getControllingPlayer();
		if( sourceMask == 0 || victim == NULL || ( sourceMask & victim->getPlayerMask() ) != 0 )
			continue;
		const Player *source = ThePlayerList->getPlayerFromMask( sourceMask );
		if( source != NULL && victim->getRelationship( source->getDefaultTeam() ) == ENEMIES )
			fights.push_back( heat );
	}
	m_fights = fights;

	Coord2D hottest;
	Real hottestHeat = 0.0f;
	ObserverCamera_hottestPlace( hits, &hottest, &hottestHeat );

	const UnsignedInt held = frame >= m_placeSince ? frame - m_placeSince : 0;
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
	if( ObserverCamera_stayOnEvent( current, best, held ) )
	{
		m_place = ObserverCamera_eventPlace( *current, frame );
		*place = m_place;
		return TRUE;
	}
	// an event that just ended hands straight over to the next; anything else is given its settle
	// first, except a superweapon still leaving its silo
	if( best != NULL && ( !m_placeValid || m_placeKind == PLACE_EVENT || ObserverCamera_eventCutsIn( *best, frame, held ) ) )
	{
		DEBUG_LOG(( "OBSCAM frame %u director to special power %u at (%.0f,%.0f)%s\n", frame, best->id,
			best->target.x, best->target.y, best->superweapon ? " superweapon" : "" ));
		m_place = ObserverCamera_eventPlace( *best, frame );
		m_placeKind = PLACE_EVENT;
		m_placeHeight = 0.0f;
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
			m_placeHeight = ObserverCamera_fightHeight( ObserverCamera_spreadAround( hits, followed ) );
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
		m_placeHeight = ObserverCamera_fightHeight( ObserverCamera_spreadAround( hits, hottest ) );
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
		m_placeHeight = 0.0f;
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
		m_split = FALSE;
		*target = m_playerViews[ m_followed ];
		return TRUE;
	}

	const Player *narrowTo = m_followed == NO_PLAYER ? NULL : ThePlayerList->getNthPlayer( m_followed );
	const Coord3D &at = current.getPosition();
	Coord2D place;
	if( !directorPlace( narrowTo, &place ) )
		return FALSE;
	if( TheGlobalData->m_directorRecord && m_placeScanned == TheGameLogic->getFrame() )
		updateSplit();
	// the opening shows the first player's base in pane 0.  Pane 0 keeps its place until the panes
	// have gone: a split ends when the director moves onto the second fight, and pane 0 going there at
	// once showed that fight twice while pane 1 slid out
	if( m_intro && m_panePhase == PANES_HELD )
		place = m_paneSubject[ 0 ];
	const Bool panesUp = m_panePhase == PANES_IN || m_panePhase == PANES_HELD || m_panePhase == PANES_OUT;
	if( panesUp && ( m_panePhase == PANES_OUT || ( !m_split && !m_intro ) ) )
		place = m_panesLeftPlace;
	else
		m_panesLeftPlace = place;
	place = keepInMap( place, current );
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
		m_split = FALSE;
		holdHeight( FALSE );
		releaseHeight();
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
		m_split = FALSE;
		holdHeight( FALSE );
		releaseHeight();
		return;
	}

	// -directorrecord's panes run on the logic clock, on the split the last scan decided
	if( TheGlobalData->m_directorRecord )
	{
		const UnsignedInt frame = TheGameLogic->getFrame();
		const Bool introStarting = !m_introDone;
		advancePanes( frame );
		if( m_intro && ( introStarting || frame % DIRECTOR_SCAN_FRAMES == 0 ) )
			updateIntroPlaces();
	}

	ViewLocation target;
	if( !chooseTarget( current, &target ) )
	{
		m_driving = FALSE;
		m_split = FALSE;
		holdHeight( FALSE );
		releaseHeight();
		return;
	}

	// a player's screen brings its own zoom; the director's height is the watcher's own plus what
	// the fight's width asks for
	if( isShowingPlayerView() )
		releaseHeight();
	else
		driveHeight( m_placeHeight );
	// while there are panes the zoom is set outright from how far in they are, with the view's own
	// settling held off: it eases on every draw, and a frame has a draw a pane
	const Bool panesZoom = m_paneCount >= 2 && !isShowingPlayerView();
	holdHeight( isShowingPlayerView() || panesZoom );
	if( !m_driving )
	{
		m_velocity.x = m_velocity.y = m_velocity.z = m_velocity.angle = m_velocity.pitch = m_velocity.zoom = 0.0f;
		m_mainOffset.x = m_mainOffset.y = 0.0f;
	}

	// the glide is of the subject; the camera sits back from it so the subject is in the middle of
	// pane 0, moving there as the panes come in and back to the middle of the screen as they go
	Coord2D fromMiddle = { 0.0f, 0.0f };
	if( m_paneCount >= 2 && !isShowingPlayerView() )
	{
		Coord2D middles[ OBSERVER_MOST_PANES ];
		ObserverCamera_paneMiddles( m_paneRays, m_paneCount, TheDisplay->getWidth(), TheDisplay->getHeight(), middles );
		fromMiddle.x = ( middles[ 0 ].x - TheDisplay->getWidth() * 0.5f ) * m_paneProgress;
		fromMiddle.y = ( middles[ 0 ].y - TheDisplay->getHeight() * 0.5f ) * m_paneProgress;
	}
	const Coord3D &camera = current.getPosition();
	ViewLocation subject;
	subject.init( camera.x + m_mainOffset.x, camera.y + m_mainOffset.y, camera.z, current.getAngle(), current.getPitch(), current.getZoom() );
	// from the opening's last player to the director's first place is a glide whatever the distance,
	// on a target kept just inside the cut distance until the real one is
	if( m_introGlide )
	{
		const Real dx = target.getPosition().x - subject.getPosition().x;
		const Real dy = target.getPosition().y - subject.getPosition().y;
		const Real distance = sqrtf( dx * dx + dy * dy );
		const Real glideReach = CUT_DISTANCE * 0.9f;
		if( distance > glideReach )
		{
			const Real share = glideReach / distance;
			target.init( subject.getPosition().x + dx * share, subject.getPosition().y + dy * share, target.getPosition().z,
				target.getAngle(), target.getPitch(), target.getZoom() );
		}
		else
			m_introGlide = FALSE;
	}
	const Bool player = m_mode == OBSERVER_CAMERA_PLAYER;
	ViewLocation step = ObserverCamera_approach( subject, target, elapsed / MILLISECONDS_PER_SECOND,
		player ? PLAYER_PAN_SECONDS : DIRECTOR_PAN_SECONDS, player ? PLAYER_TOP_SPEED : DIRECTOR_TOP_SPEED, &m_velocity );
	if( panesZoom )
	{
		const Coord3D &stepAt = step.getPosition();
		step.init( stepAt.x, stepAt.y, stepAt.z, step.getAngle(), step.getPitch(),
			m_paneBaseZoom * ObserverCamera_paneZoom( m_paneCount, m_paneProgress ) );
	}
	const Coord2D offset = screenToGround( step, fromMiddle );
	Coord2D sitBack;
	sitBack.x = step.getPosition().x - offset.x;
	sitBack.y = step.getPosition().y - offset.y;
	if( fromMiddle.x != 0.0f || fromMiddle.y != 0.0f )
		sitBack = keepInMap( sitBack, step );
	m_mainOffset.x = step.getPosition().x - sitBack.x;
	m_mainOffset.y = step.getPosition().y - sitBack.y;
	ViewLocation placed;
	placed.init( sitBack.x, sitBack.y, step.getPosition().z, step.getAngle(), step.getPitch(), step.getZoom() );
	TheTacticalView->setLocation( &placed );
	// the view keeps its look point inside its constraint when it draws; held there now, a cut to a
	// place past the constraint is not mistaken next frame for the watcher moving the camera
	TheTacticalView->applyCameraConstraint();
	TheTacticalView->getPosition( &m_drivenTo );
	m_driving = TRUE;

	stepPaneCameras( step, elapsed / MILLISECONDS_PER_SECOND );
}
