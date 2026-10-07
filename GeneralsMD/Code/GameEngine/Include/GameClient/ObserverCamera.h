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

#include "Common/AsciiString.h"
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

/// -directorrecord's scouting pass plays the match headless first and writes down what is worth
/// filming, so the filming pass can be there before it starts.  A fight runs from the scan it was
/// first hot on, start, to the last, starting at place and last seen at target; peak is its hottest
/// scan and sides the mask of every player who dealt or took a hit in it.  A special power is a
/// moment of one frame fired from place at target, peak its weight against other powers
struct DirectorMoment
{
	UnsignedInt start;
	UnsignedInt last;
	Coord2D place;
	Coord2D target;
	Real peak;
	UnsignedInt sides;
	Bool power;
	Bool superweapon;
};

/// one hit a player dealt another he is at war with, for the scouting pass: where, what it counts
/// for, and the two players' masks together
struct DirectorFightHit
{
	Coord2D position;
	Real weight;
	UnsignedInt sides;
};

/// the scouting pass's fights carried over one scan of the hits.  Each crowd of hits, hottest first,
/// joins the fight still going nearest it, or starts a new one on this frame; a fight with no crowd
/// near it for a few seconds is over and is not joined again
void ObserverCamera_trackFights( std::vector< DirectorMoment > &fights, const std::vector< DirectorFightHit > &hits, UnsignedInt frame );
/// whether a moment is worth the camera's time: every special power, and a fight that got hot enough
/// and lasted.  The rest fizzled
Bool ObserverCamera_worthFilming( const DirectorMoment &moment );
/// the moment the director goes to wait at on frame: of the fights worth filming and the superweapons
/// that begin within the pre-roll after frame, a superweapon first and then the hottest.  -1 for none
Int ObserverCamera_prerollMoment( const std::vector< DirectorMoment > &timeline, UnsignedInt frame );
/// whether the fight going on at place on frame is one the scouting pass saw fizzle, which the
/// director does not cut to.  FALSE where the pass saw nothing, so a match it did not scout is
/// filmed as before
Bool ObserverCamera_fizzles( const std::vector< DirectorMoment > &timeline, const Coord2D &place, UnsignedInt frame );
/// a fight worth filming going on at frame, or beginning within the pre-roll, far enough from first
/// to want a pane of its own: the hottest such, where it begins; FALSE for none
Bool ObserverCamera_plannedSecond( const std::vector< DirectorMoment > &timeline, const Coord2D &first, UnsignedInt frame, Coord2D *second );
/// a moment as one line of the timeline file, and back; FALSE for a line that is not one
AsciiString ObserverCamera_formatMoment( const DirectorMoment &moment );
Bool ObserverCamera_parseMoment( const char *line, DirectorMoment *moment );

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
/// -directorrecord's second fight: the hottest place among the hits far enough from first that the
/// two halves of a split screen never show the same ground.  FALSE when nothing that far was hit
Bool ObserverCamera_secondPlace( const std::vector< DirectorHeat > &hits, const Coord2D &first, Coord2D *place, Real *heat );
/// whether the recording's picture is split, given whether it is now, how far apart the two places
/// are and how long ago it last went on or off: on, after a rest, for a second fight that is big on
/// its own and next to a first one; off once it has been held a while and the second fight has burnt
/// down, and at once when the second fight is over or the two places come together
Bool ObserverCamera_holdSplit( Bool split, Real firstHeat, Real secondHeat, Real apart, UnsignedInt framesSince );

/// -directorrecord's panes.  The picture is cut by rays from one point, every ray a multiple of 45
/// degrees, and pane i is the wedge from ray i counterclockwise to ray i + 1.  Angles are degrees,
/// 0 to the right and 90 up the screen
enum { OBSERVER_MOST_PANES = 8 };
/// the rays for count panes, ascending from 0 to 360; the number of rays, which is count, or 0 for
/// fewer than two panes.  Pane 0 is the one the radar belongs to and the one left when the panes go
Int ObserverCamera_paneLayout( Int count, Real *rays );
/// the pane pixel x, y falls in, the rays meeting at origin x, y (pixels, y down)
Int ObserverCamera_paneOf( Real x, Real y, Real originX, Real originY, const Real *rays, Int count );
/// how far the rays' meeting point has to move, away from pane 0, before pane 0 is the whole of a
/// width by height picture: the panes come in from there and go back out to it
Real ObserverCamera_paneExit( const Real *rays, Int count, Int width, Int height );
/// the direction, in pixels with y down, that meeting point moves in to take the other panes away
Coord2D ObserverCamera_paneExitDirection( const Real *rays );
/// the largest circle that fits in each pane with the rays meeting in the middle of the picture, kept
/// off the screen's edges, the rays and the radar's frame, a rectangle radarHalf wide and high each
/// way from the middle.  Its centre is where the pane's subject is put and its radius, in pixels,
/// what the subject is fitted into; centres and radii have count entries
void ObserverCamera_paneCircles( const Real *rays, Int count, Int width, Int height, const Coord2D &radarHalf,
	Coord2D *centres, Real *radii );
/// how far from around the farthest of the things within reach of it lie, on the ground, and never less
/// than least
Real ObserverCamera_extentAround( const std::vector< DirectorHeat > &things, const Coord2D &around, Real reach, Real least );
/// the zoom from nearest to farthest at which a pane's circle reaches reach on the ground, from the
/// reach measured at two zooms: the ground a picture covers grows in a straight line with the zoom,
/// which is how far the eye stands back
Real ObserverCamera_zoomForReach( Real zoomA, Real reachA, Real zoomB, Real reachB, Real reach, Real nearest, Real farthest );
/// the corners of what pane shows of a width by height picture, the rays meeting at origin: its wedge
/// cut by the screen's edges, at most eight points; the number of them
Int ObserverCamera_panePolygon( const Real *rays, Int count, Int pane, const Coord2D &origin, Int width, Int height,
	Coord2D *vertices );
/// how far an animation of length frames that started on start is on frame, eased in and out, 0 to 1
Real ObserverCamera_easeFrames( UnsignedInt frame, UnsignedInt start, UnsignedInt length );
/// the dark edge each side of the gold of a line between panes and of the radar's frame, in pixels
enum { OBSERVER_PANE_LINE_EDGE = 2 };
/// the gold of a line between panes, in pixels, for a picture height pixels high: 6 at 720, 9 at 1080
Int ObserverCamera_paneLineWidth( Int height );
/// how far either side of a seam the join keeps pane 0's pixels, which is where its line is drawn:
/// the whole edged line's half width, measured along a row or column across a 45 degree line
Int ObserverCamera_paneSeamBand( Int height );

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

	/// -directorrecord's panes: the players' bases for the first seconds of a match, then two fights
	/// far apart side by side.  Each pane past the first is drawn first, for the recording only and
	/// never presented, then the frame everybody sees; the recording joins them along the rays
	Bool isSplit( void ) const { return m_split; }
	/// how many panes the next frame is drawn in, 1 when the picture is whole
	Int getDrawnPaneCount( void ) const { return m_paneProgress > 0.0f ? m_paneCount : 1; }
	const Real *getPaneRays( void ) const { return m_paneRays; }
	/// where the rays meet on the screen, in pixels, which slides in from off the screen and back out
	Coord2D getPaneOrigin( void ) const { return m_paneOrigin; }
	/// how far the radar in the bottom left corner is slid out to the left, 0 to 1
	Real getCornerRadarSlide( void ) const { return m_cornerRadarSlide; }
	/// the radar framed on the rays' meeting point, pane 0's, while there are panes; its middle comes
	/// with the meeting point and goes off the screen with it
	Bool isRadarFramed( void ) const { return m_paneCount >= 2 && m_paneProgress > 0.0f; }
	Coord2D getFramedRadarMiddle( Real radarDiagonal ) const;
	/// the framed radar's rectangle as it was drawn, frame included: the recording takes it from pane 0
	void setRadarFrame( const IRegion2D &frame ) { m_radarFrame = frame; }
	const IRegion2D &getRadarFrame( void ) const { return m_radarFrame; }
	Bool isDrawingSecond( void ) const { return m_drawingPane != 0; }
	Int getDrawingPane( void ) const { return m_drawingPane; }
	/// the view moved to a pane's camera for one draw, and put back after it
	void beginPanePass( Int pane );
	void endPanePass( void );

	/// -directorrecord's scouting pass, once a pass of a headless run: count the fights on every scan
	/// and a checkpoint of the logic's CRC now and then
	void scout( void );
	/// and at its end, the timeline written to the -directorscout file
	void finishScout( void );

	enum { NO_PLAYER = -1 };

private:
	void updateShroudViewer( void );
	void holdHeight( Bool hold );
	void driveHeight( Real extra );
	Bool takenByHand( const ViewLocation &current ) const;
	Coord2D keepInMap( const Coord2D &place, const ViewLocation &current ) const;
	void aimView( const Coord2D &look, Real zoom );
	Coord2D placeOnPixel( const Coord2D &subject, const Coord2D &pixel, Real zoom );
	Real pixelError( const Coord2D &subject, const Coord2D &pixel );
	Coord2D mapShift( Int pane, const Coord2D &origin, const Coord2D &look, Coord2D *worst, Real *past );
	Real groundReach( const Coord2D &subject, const Coord2D &centre, Real radius, Real zoom );
	Real zoomInMap( Int pane, const Coord2D &subject, Real nearest, Real wanted );
	Coord2D placePane( Int pane, const Coord2D &subject, const Coord2D &pixel, Real zoom );
	void logPanes( UnsignedInt frame ) const;
	void pickIntroBases( void );
	Region2D mapRegion( void ) const;
	void updateSplit( void );
	void updateIntroPlaces( void );
	void advancePanes( UnsignedInt frame );
	void fitPanes( UnsignedInt frame );
	Real paneZoom( Int pane ) const;
	void stepPaneCameras( const ViewLocation &step, Real elapsedSeconds );
	Bool isShowingPlayerView( void ) const;
	Bool chooseTarget( const ViewLocation &current, ViewLocation *target );
	Bool directorPlace( const Player *narrowTo, Coord2D *place );
	DirectorEvent *findEvent( UnsignedInt id );
	void dropOldEvents( UnsignedInt frame );
	void loadTimeline( void );
	void checkTimeline( UnsignedInt frame );

	/// a sight, a fight going on, a special power, or a fight the timeline says is about to begin
	enum PlaceKind { PLACE_SIGHT, PLACE_FIGHT, PLACE_EVENT, PLACE_UPCOMING };

	/// a logic frame and the logic's CRC on it, which the two passes compare to know they played one match
	struct CrcCheckpoint
	{
		UnsignedInt frame;
		UnsignedInt crc;
	};

	std::vector< DirectorMoment > m_scouted;		///< the scouting pass's fights and special powers so far
	std::vector< CrcCheckpoint > m_scoutCrcs;		///< and its checkpoints
	UnsignedInt m_scoutScanned;									///< the logic frame it last counted the hits on
	std::vector< DirectorMoment > m_timeline;		///< what the scouting pass saw, for the filming pass
	std::vector< CrcCheckpoint > m_timelineCrcs;
	Bool m_timelineLoaded;
	Int m_placeMoment;													///< the timeline's moment the director waits at, while the place is one
	Coord2D m_skippedFight;											///< the last fizzling fight the director passed over, so it is logged once

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

	std::vector< DirectorHeat > m_fights;	///< the last scan's hits one player dealt another he is at war with; the split counts only these
	Bool m_split;										///< -directorrecord wants two fights side by side
	UnsignedInt m_splitChanged;			///< the logic frame the split last went on or off
	Coord2D m_secondPlace;					///< the second fight, shown in the second pane

	enum PanePhase { PANES_NONE, PANES_RADAR_OUT, PANES_IN, PANES_HELD, PANES_OUT, PANES_RADAR_IN };
	PanePhase m_panePhase;
	UnsignedInt m_panePhaseStart;		///< the logic frame the phase began on
	Bool m_intro;										///< the panes are the match's opening, one a player
	Bool m_introDone;
	Bool m_introGlide;								///< the opening has gone and the camera glides to the director's place, however far
	Coord2D m_panesLeftPlace;						///< pane 0's place while its panes are up, kept as they go
	Int m_paneCount;								///< 0 with no panes
	Real m_paneRays[ OBSERVER_MOST_PANES ];
	Real m_paneProgress;						///< 0 for no panes on the screen, 1 for all of them, eased
	Real m_paneExit;								///< how far off the meeting point goes, in pixels
	Real m_paneBaseZoom;							///< the view's zoom when the panes started, which they rise from
	Coord2D m_paneCentres[ OBSERVER_MOST_PANES ];	///< where each pane's subject is put, the rays meeting in the middle
	Real m_paneRadii[ OBSERVER_MOST_PANES ];		///< how much of its pane each subject is fitted into, in pixels
	Real m_paneExtent[ OBSERVER_MOST_PANES ];		///< how far each pane's subject spreads on the ground
	Real m_paneFit[ OBSERVER_MOST_PANES ];			///< each pane's zoom with the panes all in, eased on logic frames
	Bool m_paneFitValid;							///< m_paneFit holds a fit of these panes
	UnsignedInt m_paneFitFrame;						///< the logic frame m_paneFit last moved on
	Coord2D m_radarHalf;							///< half the framed radar's size, taken from its last draw
	ViewLocation m_aimFrom;							///< the angle, pitch and height every pane camera is tried at this update
	Real m_paneShifted[ OBSERVER_MOST_PANES ];		///< how far the map moved each pane's look point off its subject last frame
	Real m_paneError[ OBSERVER_MOST_PANES ];		///< how far from its pixel the view drew each pane's subject last frame, before the map's shift
	Coord2D m_paneShown[ OBSERVER_MOST_PANES ];		///< the subject each pane was placed on last frame
	Coord2D m_paneWorst[ OBSERVER_MOST_PANES ];		///< the ground under each pane's vertex furthest past the map last frame
	Real m_paneOutside[ OBSERVER_MOST_PANES ];		///< how far past the map that vertex still was once the look point moved
	Coord2D m_paneOrigin;						///< where the rays meet now, in pixels
	Real m_cornerRadarSlide;
	IRegion2D m_radarFrame;
	const Player *m_panePlayers[ OBSERVER_MOST_PANES ];	///< the intro's player for each pane
	Coord2D m_paneSubject[ OBSERVER_MOST_PANES ];			///< what each pane past the first looks at
	ViewLocation m_paneGlide[ OBSERVER_MOST_PANES ];		///< where each pane's subject glide has got to
	ObserverCameraVelocity m_paneVelocity[ OBSERVER_MOST_PANES ];
	ViewLocation m_paneView[ OBSERVER_MOST_PANES ];		///< each pane's camera for its draw
	Coord2D m_mainOffset;						///< how far the camera's look point sat from its subject last frame, on the ground
	ViewLocation m_firstView;				///< the camera's own place, put back after a pane is drawn
	Int m_drawingPane;
};

extern ObserverCamera TheObserverCamera;

#endif
