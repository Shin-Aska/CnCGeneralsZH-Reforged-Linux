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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////


// FILE: HotKey.h /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Sep 2002
//
//	Filename: 	HotKey.h
//
//	author:		Chris Huybregts
//	
//	purpose:	
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __HOT_KEY_H_
#define __HOT_KEY_H_

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "Common/SubsystemInterface.h"
#include "Common/MessageStream.h"
//-----------------------------------------------------------------------------
// FORWARD REFERENCES /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
class AsciiString;
class GameWindow;
//-----------------------------------------------------------------------------
// TYPE DEFINES ///////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
class HotKeyTranslator : public GameMessageTranslator
{
public:
	virtual GameMessageDisposition translateGameMessage(const GameMessage *msg);
	virtual ~HotKeyTranslator() { }
};

//-----------------------------------------------------------------------------
class HotKey
{
public:
	HotKey( void );
	GameWindow *m_win;
	AsciiString m_key;	
	// we may need a checkmark system.
};

//-----------------------------------------------------------------------------
class HotKeyManager : public SubsystemInterface
{
public:
	HotKeyManager( void );
	~HotKeyManager( void );
	// Inherited from subsystem interface -----------------------------------------------------------
	virtual	void init( void );															///< Initialize the Hotkey system
	virtual void update( void ) {}														///< A No-op for us
	virtual void reset( void );															///< Reset
	//-----------------------------------------------------------------------------------------------

	void addHotKey( GameWindow *win, const AsciiString& key);
	Bool executeHotKey( const AsciiString& key); // called fromt eh HotKeyTranslator
	
	AsciiString searchHotKey( const AsciiString& label);
	AsciiString searchHotKey( const UnicodeString& uStr );
	static AsciiString nameOf( WideChar c );	///< the map key for a letter: lower case, UTF-8

	/** The window this key is bound to at the moment, or NULL for a key nothing
		* claims.  When there is one, pressable comes back with the answer
		* executeHotKey would give: the window is showing and enabled, so the press
		* would land. */
	GameWindow *findHotKey( const AsciiString& key, Bool *pressable ) const;

	/** The key a command button gets: the one the player gave it in Hotkeys.ini (the launcher's
		* Hotkeys page writes it, INI::parseCommandButtonHotKeyDefinition reads it), 0 where the player
		* took the key away, and labelKey, the letter after the label's '&', where the player left the
		* button alone; upper case.  Attack move, stop, guard, rally point, evacuate and sell have their
		* fixed letters and no other button gets one.  With grid keys on (INI::parseCommandBarGridDefinition) a button on the command bar,
		* barSlot its ButtonCommand number less one, gets its place's key instead, except attack move, stop, guard, rally point, evacuate and combat drop, and any other button
		* (barSlot -1) loses a letter the grid uses. */
	static WideChar playerButtonKey( const AsciiString& button, WideChar labelKey, Int barSlot,
																	 Int commandType );

	/** The key command bar slot barSlot gives a button with grid keys on, 0 for a slot without one or
		* with grid keys off.  The two keyless places take what lendFixedKeys lent them. */
	static WideChar gridSlotKey( Int barSlot );

	/** The bar about to show these `count` buttons (an empty name for none) lends the keyless places
		* the fixed letters it has no order for: 11 attack move's A, 13 guard's G or the rally point's Y,
		* then any of A S G Y V C still free, so a building's bar keys those places too.  NULL buttons,
		* which reset() passes, takes them back. */
	static void lendFixedKeys( const AsciiString *buttons, const Int *types, Int count );

	/** lendFixedKeys' choice into lent, one letter or 0 per grid place. */
	static void gridLentKeys( const AsciiString *buttons, const Int *types, Int count, WideChar *lent );

private:
	typedef std::map<AsciiString, HotKey> HotKeyMap;
	HotKeyMap m_hotKeyMap;
};
extern HotKeyManager *TheHotKeyManager;
//-----------------------------------------------------------------------------
// INLINING ///////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// EXTERNALS //////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

#endif // __HOT_KEY_H_

