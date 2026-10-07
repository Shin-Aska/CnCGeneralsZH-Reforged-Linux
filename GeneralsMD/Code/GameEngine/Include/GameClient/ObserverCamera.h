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

// ObserverCamera.h ///////////////////////////////////////////////////////////////////////////////
// Who drives a watcher's camera, and whose fog his screen is drawn in.
//
// The mode and the followed player are picked apart, from two lists.  Free is the camera in the
// watcher's own hands.  Director goes to the fight of the last few seconds with the most at stake,
// each hit counted by what the thing hit cost and more for a kill or a superweapon, and stays there
// a while before it looks for a bigger fight.  A special power used anywhere outranks any fight: a
// superweapon is shown leaving its silo, then where it lands until the dust settles, a laser
// followed along its sweep.  The camera glides between places on a spring and only cuts across
// most of a map, and stops short of a place that would put ground off the map on the screen.  Over
// a wide fight it rises above the watcher's own height, which the wheel still moves, and comes back
// down to it when the camera goes elsewhere or is taken back.  With no fight on it goes round the armies on the
// move, the bases going up and the superweapons, a few seconds each, so it never sits still; with a
// player picked it counts only that player's fights and things.  Player shows what the followed player's own screen
// shows: a player's camera comes over the network a few times a second (MSG_SET_REPLAY_CAMERA), an
// AI, which has no camera, gets the narrowed director, and with nobody picked it does nothing.
// Scrolling with the keys or a drag, turning the camera or clicking the radar hands it back to the
// watcher, who keeps the player he picked.  The screen's edge does not scroll while this drives:
// the page's panel stands on the right one, and reaching for it used to scroll the map and take the
// camera from the director with nobody asking.
//
// The followed player is picked from his own list, not the selection: clicking a unit makes its owner
// the watched player, and the camera jumping to an enemy's screen on a click would be no use.  Fog
// on draws the followed player's fog, what he has seen and what he has not, and hides what he
// cannot see, stealthed units he has not detected included; following nobody it changes nothing.
// Nothing here is logic: the camera and the fog are this machine's picture only.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _OBSERVER_CAMERA_H_
#define _OBSERVER_CAMERA_H_

#include "Common/GameCommon.h"
#include "GameClient/View.h"

#include <vector>

enum ObserverCameraMode
{
	OBSERVER_CAMERA_FREE,
	OBSERVER_CAMERA_DIRECTOR,
	OBSERVER_CAMERA_PLAYER
};

/// one thing hit lately, where it stands and how much it counts for
struct DirectorHeat
{
	Coord2D position;
	Real weight;
};

class Player;

/// a special power used lately: who used it, where it was fired from, where it lands, and until which
/// logic frame it is worth watching.  A superweapon is shown leaving its silo before its target
struct DirectorEvent
{
	UnsignedInt id;
	const Player *owner;
	Coord2D source;
	Coord2D target;
	UnsignedInt since;
	UnsignedInt until;
	Real weight;
	Bool superweapon;
};

/// how fast the camera is going on each of its six axes, carried from one frame's step to the next
struct ObserverCameraVelocity
{
	Real x, y, z, angle, pitch, zoom;
};

/// the place the hits crowd most: each hit's weight summed over those within DIRECTOR_GATHER_RADIUS
/// of it, and the best one's neighbours averaged by weight.  FALSE when nothing was hit
Bool ObserverCamera_hottestPlace( const std::vector< DirectorHeat > &hits, Coord2D *place, Real *heat );
/// the weight of the hits within DIRECTOR_GATHER_RADIUS of a place, and their weighted middle
Real ObserverCamera_heatAround( const std::vector< DirectorHeat > &hits, const Coord2D &around, Coord2D *middle );
/// whether a director holding a place with heatHere for framesHere should cut to one with heatThere
Bool ObserverCamera_shouldMove( Real heatHere, Real heatThere, UnsignedInt framesHere );
/// what one recent hit counts for: more the dearer the thing hit, more again if it died or is a
/// superweapon
Real ObserverCamera_hitWeight( Int cost, Bool killed, Bool superweapon );
/// what one thing is worth looking at with no fight on: its cost, doubled while it marches or is
/// being built, halved for a building that is only standing there, tripled for a superweapon
Real ObserverCamera_sightWeight( Int cost, Bool structure, Bool busy, Bool superweapon );
/// the best place among the sights away from the ones in seen; FALSE when every sight was seen
Bool ObserverCamera_nextSight( const std::vector< DirectorHeat > &sights, const std::vector< Coord2D > &seen, Coord2D *place );
/// where an event is best watched on a frame: a superweapon's silo for its first few seconds, then
/// where it lands
Coord2D ObserverCamera_eventPlace( const DirectorEvent &event, UnsignedInt frame );
/// whether the director stays on the event it is showing, current, rather than go to best, the
/// biggest one it may show now.  best is NULL when none may be shown: narrowed to a player whose
/// things round the target are all gone, the current one is filtered out and is still kept
Bool ObserverCamera_stayOnEvent( const DirectorEvent *current, const DirectorEvent *best, UnsignedInt held );
/// whether an event takes the camera from a fight or a sight held for held frames.  A superweapon
/// still on its way out of the silo goes at once, or the launch is over before the settle is
Bool ObserverCamera_eventCutsIn( const DirectorEvent &event, UnsignedInt frame, UnsignedInt held );
/// how widely the hits within DIRECTOR_GATHER_RADIUS of a place lie round it, by weight
Real ObserverCamera_spreadAround( const std::vector< DirectorHeat > &hits, const Coord2D &around );
/// how much higher than the watcher's own the director takes the camera over a fight this spread
Real ObserverCamera_fightHeight( Real spread );
/// the place moved the least that keeps the screen's ground over the map: corners are where the
/// screen's corners fall on the ground, taken from the point looked at.  Where the screen is wider
/// than the map, the place stays between the two edges
Coord2D ObserverCamera_keepInMap( const Coord2D &place, const Coord2D *corners, Int count, const Region2D &map );
/// a step of the camera towards where it is going on a critically damped spring: it gathers speed,
/// never goes faster than topSpeed across the ground, and slows into place without overshooting.
/// smoothSeconds is roughly how long it takes to arrive.  Further apart than a glide should cross,
/// it jumps there and stops
ViewLocation ObserverCamera_approach( const ViewLocation &from, const ViewLocation &to, Real elapsedSeconds, Real smoothSeconds,
	Real topSpeed, ObserverCameraVelocity *velocity );

class ObserverCamera
{
public:
	ObserverCamera();

	void reset( void );
	/// once a frame on a watcher's machine: move the camera and swap the fog when it has to
	void update( UnsignedInt nowMilliseconds );

	/// a player's camera as it came over the network or out of a replay
	void notePlayerView( Int playerIndex, const ViewLocation &view );

	ObserverCameraMode getMode( void ) const { return m_mode; }
	void setMode( ObserverCameraMode mode );
	/// the player whose screen the player mode shows, whose fights the director keeps to and whose
	/// fog is drawn while fog is on; NO_PLAYER for nobody
	void followPlayer( Int playerIndex );
	/// the player being followed, still while the watcher has the camera in his own hands, or
	/// NO_PLAYER
	Int getFollowedPlayerIndex( void ) const { return m_followed; }
	/// the camera is where this put it last frame, the director's or a player's
	Bool isDriving( void ) const { return m_driving; }
	Bool isFogOn( void ) const { return m_fog; }
	void setFog( Bool fog ) { m_fog = fog; }

	/// the player whose fog the screen is drawn in: the local player, or whoever a watcher with fog
	/// on is following
	Int getShroudPlayerIndex( void ) const;

	/// a special power was used: logic tells the director, and never asks it anything back
	void noteSpecialPower( const Player *owner, const Coord3D *from, const Coord3D *at, Bool superweapon );
	/// a superweapon is hitting the ground here this frame, a beam or a warhead.  Keeps the event it
	/// belongs to going a few seconds more, and with follow the event's target moves with it
	void noteSuperweaponHit( const Player *owner, const Coord3D *at, Bool follow );

	/// the watcher's own height back on the view, if the director had raised it over a fight
	void releaseHeight( void );

	enum { NO_PLAYER = -1 };

private:
	void updateShroudViewer( void );
	void holdHeight( Bool hold );
	void driveHeight( Real extra );
	Bool takenByHand( const ViewLocation &current ) const;
	Coord2D keepInMap( const Coord2D &place, const ViewLocation &current ) const;
	Bool isShowingPlayerView( void ) const;
	Bool chooseTarget( const ViewLocation &current, ViewLocation *target );
	Bool directorPlace( const Player *narrowTo, Coord2D *place );
	DirectorEvent *findEvent( UnsignedInt id );
	void dropOldEvents( UnsignedInt frame );

	enum PlaceKind { PLACE_SIGHT, PLACE_FIGHT, PLACE_EVENT };

	ObserverCameraMode m_mode;
	Int m_followed;
	Bool m_fog;
	Int m_shroudViewer;
	Bool m_driving;									///< the camera was put where it is by this, last frame
	Bool m_holdingHeight;						///< the view's own height easing is off while a player's zoom is shown
	Bool m_heightDriven;						///< the director has the camera's height above the ground
	Real m_handHeight;							///< the watcher's own height, the wheel's turns while driven added in
	Real m_drivenHeight;						///< the height the director left the view at last frame
	Coord3D m_drivenTo;							///< where this put the camera last frame, inside the view's constraint
	UnsignedInt m_lastUpdate;
	ObserverCameraVelocity m_velocity;

	ViewLocation m_playerViews[ MAX_PLAYER_COUNT ];

	Bool m_placeValid;
	Coord2D m_place;								///< where the director is looking
	UnsignedInt m_placeSince;				///< the logic frame it went there
	UnsignedInt m_placeScanned;			///< the logic frame the hits were last counted
	const Player *m_placeFor;				///< whose fights the place was picked from, NULL for everybody's
	PlaceKind m_placeKind;					///< a fight, a special power, or a sight picked while nothing was hit
	Real m_placeHeight;							///< how much higher than the watcher's own the place is watched from
	UnsignedInt m_placeEvent;				///< the id of the event the place is, while it is one
	std::vector< Coord2D > m_seen;	///< the last few sights, oldest first, not gone back to while there is another
	std::vector< DirectorEvent > m_events;	///< the special powers still worth watching, oldest first
	UnsignedInt m_nextEventId;
};

extern ObserverCamera TheObserverCamera;

#endif
