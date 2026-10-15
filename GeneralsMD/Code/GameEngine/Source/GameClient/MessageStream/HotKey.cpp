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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: HotKey.cpp /////////////////////////////////////////////////
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
//	Filename: 	HotKey.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "GameClient/HotKey.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Keyboard.h"
#include "GameClient/GameText.h"
#include "Common/AudioEventRTS.h"
#include "Common/GlobalData.h"
#include "Common/INI.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/InGameUI.h"
//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
GameMessageDisposition HotKeyTranslator::translateGameMessage(const GameMessage *msg)
{
	GameMessageDisposition disp = KEEP_MESSAGE;
	GameMessage::Type t = msg->getType();

	if ( t == GameMessage::MSG_RAW_KEY_UP)
	{
		
		//char key = msg->getArgument(0)->integer;
		Int keyState = msg->getArgument(1)->integer;

		// for our purposes here, we don't care to distinguish between right and left keys,
		// so just fudge a little to simplify things.
		Int newModState = 0;

		if( keyState & KEY_STATE_CONTROL )
		{
			newModState |= CTRL;
		}

		if( keyState & KEY_STATE_SHIFT )
		{
			newModState |= SHIFT;
		}

		if( keyState & KEY_STATE_ALT )
		{
			newModState |= ALT;
		}
		// shift is allowed through: a shift-click on a build button queues a batch of units, and
		// the label hotkey is meant to be that click's equal. Ctrl and alt still block the hotkey.
		if( ( newModState & ~SHIFT ) != 0 )
			return disp;
		// Ctrl+F let go of Ctrl first ends on a bare F release; it is still Ctrl+F
		if( keyState & KEY_STATE_PRESSED_WITH_CTRL_ALT )
			return disp;
		const UnsignedByte scanCode = (UnsignedByte)msg->getArgument(0)->integer;
		WideChar typed = TheKeyboard->getPrintableKey(scanCode, 0);
#if defined(_WIN32)
		// The game's own key tables know four layouts and name every key by its US position, so on a
		// Turkish keyboard the key marked i read as an apostrophe.  The letter the player's layout
		// puts on that key is the one a label's '&' means.
		{
			HKL layout = GetKeyboardLayout(0);
			const UINT virtualKey = MapVirtualKeyExW(scanCode, MAPVK_VSC_TO_VK, layout);
			BYTE keys[256] = { 0 };
			WCHAR out[4] = { 0 };
			if( virtualKey != 0 && ToUnicodeEx(virtualKey, scanCode, keys, out, 4, 0x4, layout) == 1 )
				typed = (WideChar)out[0];
		}
#endif
		if(TheHotKeyManager && TheHotKeyManager->executeHotKey(HotKeyManager::nameOf(typed)))
			disp = DESTROY_MESSAGE;
	}
	return disp;
}

//-----------------------------------------------------------------------------
HotKey::HotKey()
{
	m_win = NULL;
	//Added By Sadullah Nader
	//Initializations missing and needed
	m_key.clear();
	//
}

//-----------------------------------------------------------------------------
HotKeyManager::HotKeyManager( void )
{

}

//-----------------------------------------------------------------------------
HotKeyManager::~HotKeyManager( void )
{
	m_hotKeyMap.clear();
}
	
//-----------------------------------------------------------------------------
void HotKeyManager::init( void )
{
	m_hotKeyMap.clear();
}

//-----------------------------------------------------------------------------
void HotKeyManager::reset( void )
{
	m_hotKeyMap.clear();
	lendFixedKeys( NULL, NULL, 0 );
}

//-----------------------------------------------------------------------------
void HotKeyManager::addHotKey( GameWindow *win, const AsciiString& keyIn)
{
	AsciiString key = keyIn;
	key.toLower();
	HotKeyMap::iterator it = m_hotKeyMap.find(key);
	if( it != m_hotKeyMap.end() )
	{
		DEBUG_ASSERTCRASH(FALSE,("Hotkey %s is already mapped to window %s, current window is %s", key.str(), it->second.m_win->winGetInstanceData()->m_decoratedNameString.str(), win->winGetInstanceData()->m_decoratedNameString.str()));
		return;
	}
	HotKey newHK;
	newHK.m_key.set(key);
	newHK.m_win = win;
	m_hotKeyMap[key] = newHK;
}

//-----------------------------------------------------------------------------
GameWindow *HotKeyManager::findHotKey( const AsciiString& keyIn, Bool *pressable ) const
{
	AsciiString key = keyIn;
	key.toLower();
	HotKeyMap::const_iterator it = m_hotKeyMap.find(key);
	if( it == m_hotKeyMap.end() || it->second.m_win == NULL )
		return NULL;

	GameWindow *win = it->second.m_win;
	const UnsignedInt status = win->winGetStatus();
	*pressable = !BitTest( status, WIN_STATUS_HIDDEN ) && BitTest( status, WIN_STATUS_ENABLED );
	return win;
}

//-----------------------------------------------------------------------------
Bool HotKeyManager::executeHotKey( const AsciiString& keyIn )
{
	AsciiString key = keyIn;
	key.toLower();
	HotKeyMap::iterator it = m_hotKeyMap.find(key);
	if( it == m_hotKeyMap.end() )
		return FALSE;
	GameWindow *win = it->second.m_win;
	if( !win )
		return FALSE;
	if( !BitTest( win->winGetStatus(), WIN_STATUS_HIDDEN ) )
	{
		if( BitTest( win->winGetStatus(), WIN_STATUS_ENABLED ) )
 		{
			// what a player's own key (Hotkeys.ini) pressed can be read back from the log
			const CommandButton *command = (const CommandButton *)GadgetButtonGetData( win );
			const DrawableList *selected = TheInGameUI ? TheInGameUI->getAllSelectedDrawables() : NULL;
			DEBUG_LOG(( "HOTKEYS: %s pressed %s, selection led by %s\n", key.str(), command ? command->getName().str() : "a window",
				( selected && !selected->empty() ) ? selected->front()->getTemplate()->getName().str() : "nothing" ));
 			TheWindowManager->winSendSystemMsg( win->winGetParent(), GBM_SELECTED, (WindowMsgData)win, win->winGetWindowId() );
 
 			// here we make the same click sound that the GUI uses when you click a button
 			AudioEventRTS buttonClick("GUIClick");
 
 			if( TheAudio )
 			{
 				TheAudio->addAudioEvent( &buttonClick );
 			}  // end if
			return TRUE;
 		}

		const CommandButton *disabled = (const CommandButton *)GadgetButtonGetData( win );
		DEBUG_LOG(( "HOTKEYS: %s found %s disabled\n", key.str(), disabled ? disabled->getName().str() : "a window" ));
		AudioEventRTS disabledClick( "GUIClickDisabled" );
		if( TheAudio )
		{
			TheAudio->addAudioEvent( &disabledClick );
		}
	}
	return FALSE;
}

//-----------------------------------------------------------------------------
/** The map key for a letter: lower case, as UTF-8, so a Turkish letter is itself rather than the
	* low byte of itself.  I, dotted İ and dotless ı are all one key: a Turkish label's İptal and an
	* English player's I key have to meet, and so do the I key of a Turkish Q layout and a label's I. */
AsciiString HotKeyManager::nameOf( WideChar c )
{
	if( c == 0x0130 || c == 0x0131 || c == u'I' )
		c = u'i';
	else if( c >= u'A' && c <= u'Z' )
		c = c + ( u'a' - u'A' );
	else if( c >= 0x00C0 && c <= 0x00DE && c != 0x00D7 )
		c = c + 0x20;
	else if( c >= 0x0100 && c <= 0x017F && ( c & 1 ) == 0 )
		c = c + 1;	// Latin Extended-A pairs: Ğ ğ, Ş ş

	char utf8[ 4 ] = { 0 };
	if( c < 0x80 )
		utf8[ 0 ] = (char)c;
	else if( c < 0x800 )
	{
		utf8[ 0 ] = (char)( 0xC0 | ( c >> 6 ) );
		utf8[ 1 ] = (char)( 0x80 | ( c & 0x3F ) );
	}
	else
	{
		utf8[ 0 ] = (char)( 0xE0 | ( c >> 12 ) );
		utf8[ 1 ] = (char)( 0x80 | ( ( c >> 6 ) & 0x3F ) );
		utf8[ 2 ] = (char)( 0x80 | ( c & 0x3F ) );
	}
	return AsciiString( utf8 );
}

//-----------------------------------------------------------------------------
AsciiString HotKeyManager::searchHotKey( const AsciiString& label)
{
	return searchHotKey(TheGameText->fetch(label));
}

//-----------------------------------------------------------------------------
AsciiString HotKeyManager::searchHotKey( const UnicodeString& uStr )
{
	if(uStr.isEmpty())
		return AsciiString::TheEmptyString;

	const WideChar *marker = (const WideChar *)uStr.str();
	while (marker && *marker)
	{
		if (*marker == u'&')
		{
			// found a '&' - now look for the next char
			return nameOf( *(marker+1) );
		}
		marker++;
	}
	return AsciiString::TheEmptyString;	
}

//-----------------------------------------------------------------------------
/** The player's command button keys from Hotkeys.ini, by button name in lower case: an upper case
	* letter, or 0 for a button the player wants without a key.  Read once while the engine starts and
	* kept for the whole run, so it lives outside the manager, which every game client rebuilds. */
static std::map< std::string, WideChar > s_playerButtonKeys;

/** Grid keys (Hotkeys.ini's CommandBarGrid), by command bar slot: ControlBar.wnd's ButtonCommand01
	* to 14 stand two rows of seven read down the columns, and take the twelve letters of the Q W E R T
	* Y / A S D F G H / Z X C V B N block that are no GRID_FIXED_KEYS.  Q W E R T top the first five
	* columns, Z X D F B under them, each under its own column where the fixed A, S and G leave a gap;
	* the sixth column is H under attack move's place (slot 11) and the seventh N, sell's key, under
	* the rally point's (slot 13).  Those two places have no key of their own: a fixed order standing
	* there shows its own letter, and on a bar without such an order the place borrows the free fixed
	* letter (HotKeyManager::lendFixedKeys).  The launcher's Hotkeys page draws the same table
	* (hotkeys.js, GRID_KEYS). */
static const WideChar GRID_KEYS[] = { 'Q', 'Z', 'W', 'X', 'E', 'D', 'R', 'F', 'T', 'B', 0, 'H', 0, 'N' };
static const Int GRID_KEY_COUNT = sizeof( GRID_KEYS ) / sizeof( GRID_KEYS[ 0 ] );
static Bool s_commandGrid = FALSE;

/** The owner's fixed keys, 2026-10-12, on every bar: A attack move, S stop, G guard, Y rally
	* point, V evacuate, N sell, C combat drop, and H for land mines, camo netting and the GLA's fake
	* and real switches, which go by button name.  No other button may have one of these letters.
	* With grid keys on only GRID_FIXED_KEYS hold, on any bar and slot; sell and the H buttons take
	* their place's key.  The launcher holds the same tables (hotkeys.js, FIXED_KEYS and
	* GRID_FIXED_LETTERS). */
static const WideChar FIXED_KEYS[] = { 'A', 'S', 'G', 'Y', 'V', 'N', 'C', 'H' };
static const WideChar GRID_FIXED_KEYS[] = { 'A', 'S', 'G', 'Y', 'V', 'C' };
static const char *const FIXED_H_BUTTONS[] =
{
	"command_upgradechinamines", "command_upgradeempmines", "command_upgradecamonetting", "command_upgradeglacamonetting",
	"command_upgradeglaworkerfakecommandset", "command_upgradeglaworkerrealcommandset"
};
static const char FIXED_H_PREFIX[] = "command_becomerealgla";	///< a fake building's switch to the real one

static WideChar fixedButtonKey( const AsciiString& lowerName, Int commandType )
{
	if( strncmp( lowerName.str(), FIXED_H_PREFIX, sizeof( FIXED_H_PREFIX ) - 1 ) == 0 )
		return 'H';
	for( Int each = 0; each < (Int)( sizeof( FIXED_H_BUTTONS ) / sizeof( FIXED_H_BUTTONS[ 0 ] ) ); each++ )
		if( lowerName.compare( FIXED_H_BUTTONS[ each ] ) == 0 )
			return 'H';
	switch( commandType )
	{
		case GUI_COMMAND_ATTACK_MOVE:			return 'A';
		case GUI_COMMAND_STOP:						return 'S';
		case GUI_COMMAND_GUARD:						return 'G';
		case GUI_COMMAND_SET_RALLY_POINT:	return 'Y';
		case GUI_COMMAND_EVACUATE:				return 'V';
		case GUI_COMMAND_SELL:						return 'N';
		case GUI_COMMAND_COMBATDROP:			return 'C';
	}
	return 0;
}

/** The scope Hotkeys.ini names mod `mod`'s buttons by, "<scope>:<button>": the -mod folder's name,
	* lower case and without spaces.  The launcher's Hotkeys page uses the same (hotkeys.js, modScope). */
static std::string modScope( Int mod )
{
	std::string path( TheGlobalData->m_mods[ mod ].str() );
	while( !path.empty() && ( path[ path.size() - 1 ] == '\\' || path[ path.size() - 1 ] == '/' ) )
		path.erase( path.size() - 1 );
	const size_t slash = path.find_last_of( "\\/" );
	if( slash != std::string::npos )
		path.erase( 0, slash + 1 );
	std::string scope;
	for( size_t at = 0; at < path.size(); at++ )
		if( !isspace( (unsigned char)path[ at ] ) )
			scope += (char)tolower( (unsigned char)path[ at ] );
	return scope;
}

/** A lower case button name without the "m<n>~" -modfactions puts on a mod's own names; *mod gets
	* that mod's index, or -1 for a name with no prefix. */
static std::string withoutModPrefix( const char *lowerName, Int *mod )
{
	*mod = -1;
	const char *tilde = strchr( lowerName, '~' );
	if( lowerName[ 0 ] != 'm' || tilde == NULL || tilde == lowerName + 1 )
		return lowerName;
	for( const char *digit = lowerName + 1; digit < tilde; digit++ )
		if( !isdigit( (unsigned char)*digit ) )
			return lowerName;
	*mod = atoi( lowerName + 1 ) - 1;
	return tilde + 1;
}

/** The player's key for a button, into *letter; FALSE leaves it alone.  A mod's own button (ownMod,
	* under -modfactions) takes only its mod's "<scope>:<button>" entry.  A plain name, which under a
	* plain -mod may be any loaded mod's, takes the last loaded mod's scoped entry that exists, then
	* the unscoped one, which is retail's. */
static Bool findPlayerKey( const std::string& bare, Int ownMod, WideChar *letter )
{
	std::map< std::string, WideChar >::const_iterator it;
	const Int modCount = TheGlobalData ? (Int)TheGlobalData->m_mods.size() : 0;
	for( Int mod = modCount - 1; mod >= 0; mod-- )
	{
		if( ownMod >= 0 && mod != ownMod )
			continue;
		it = s_playerButtonKeys.find( modScope( mod ) + ":" + bare );
		if( it != s_playerButtonKeys.end() )
		{
			*letter = it->second;
			return TRUE;
		}
	}
	if( ownMod >= 0 )
		return FALSE;
	it = s_playerButtonKeys.find( bare );
	if( it == s_playerButtonKeys.end() )
		return FALSE;
	*letter = it->second;
	return TRUE;
}

static Bool isOneOf( WideChar letter, const WideChar *letters, Int count )
{
	for( Int each = 0; each < count; each++ )
		if( letters[ each ] == letter )
			return TRUE;
	return FALSE;
}

//-----------------------------------------------------------------------------
WideChar HotKeyManager::playerButtonKey( const AsciiString& button, WideChar labelKey, Int barSlot,
																				 Int commandType )
{
	AsciiString lower = button;
	lower.toLower();
	Int ownMod = -1;
	const std::string bare = withoutModPrefix( lower.str(), &ownMod );
	const WideChar fixed = fixedButtonKey( AsciiString( bare.c_str() ), commandType );
	if( s_commandGrid && barSlot >= 0 )
	{
		if( fixed != 0 && isOneOf( fixed, GRID_FIXED_KEYS, (Int)( sizeof( GRID_FIXED_KEYS ) / sizeof( GRID_FIXED_KEYS[ 0 ] ) ) ) )
			return fixed;
		return gridSlotKey( barSlot );
	}
	if( fixed != 0 )
		return fixed;

	WideChar letter = labelKey;
	findPlayerKey( bare, ownMod, &letter );
	if( letter >= 'a' && letter <= 'z' )
		letter -= 'a' - 'A';
	if( isOneOf( letter, FIXED_KEYS, (Int)( sizeof( FIXED_KEYS ) / sizeof( FIXED_KEYS[ 0 ] ) ) ) )
		return 0;
	// a button off the bar gives a grid letter up to the bar
	if( s_commandGrid && isOneOf( letter, GRID_KEYS, GRID_KEY_COUNT ) )
		return 0;
	return letter;
}

//-----------------------------------------------------------------------------
/** What the bar on show lends the two keyless places, see lendFixedKeys. */
static WideChar s_lentKeys[ GRID_KEY_COUNT ] = { 0 };

WideChar HotKeyManager::gridSlotKey( Int barSlot )
{
	if( !s_commandGrid || barSlot < 0 || barSlot >= GRID_KEY_COUNT )
		return 0;
	return GRID_KEYS[ barSlot ] != 0 ? GRID_KEYS[ barSlot ] : s_lentKeys[ barSlot ];
}

//-----------------------------------------------------------------------------
void HotKeyManager::gridLentKeys( const AsciiString *buttons, const Int *types, Int count, WideChar *lent )
{
	const Int fixedCount = (Int)( sizeof( GRID_FIXED_KEYS ) / sizeof( GRID_FIXED_KEYS[ 0 ] ) );
	WideChar used[ MAX_COMMANDS_PER_SET + GRID_KEY_COUNT ];
	Int usedCount = 0;
	for( Int each = 0; each < count; each++ )
	{
		if( buttons[ each ].isEmpty() )
			continue;
		AsciiString lower = buttons[ each ];
		lower.toLower();
		Int mod = -1;
		const WideChar fixed = fixedButtonKey( AsciiString( withoutModPrefix( lower.str(), &mod ).c_str() ), types[ each ] );
		if( isOneOf( fixed, GRID_FIXED_KEYS, fixedCount ) )
			used[ usedCount++ ] = fixed;
	}

	for( Int slot = 0; slot < GRID_KEY_COUNT; slot++ )
		lent[ slot ] = 0;
	// a place takes the order's letter that usually stands there first: attack move's A, then guard's
	// G or the rally point's Y; then any fixed letter still free, in GRID_FIXED_KEYS' order
	static const struct { Int slot; WideChar letter; } OWN[] = { { 10, 'A' }, { 12, 'G' }, { 12, 'Y' } };
	for( Int pass = 0; pass < 2; pass++ )
	{
		const Int choices = pass == 0 ? (Int)( sizeof( OWN ) / sizeof( OWN[ 0 ] ) ) : GRID_KEY_COUNT * fixedCount;
		for( Int each = 0; each < choices; each++ )
		{
			const Int slot = pass == 0 ? OWN[ each ].slot : each / fixedCount;
			const WideChar letter = pass == 0 ? OWN[ each ].letter : GRID_FIXED_KEYS[ each % fixedCount ];
			if( GRID_KEYS[ slot ] != 0 || lent[ slot ] != 0 || isOneOf( letter, used, usedCount ) )
				continue;
			lent[ slot ] = letter;
			used[ usedCount++ ] = letter;
		}
	}
}

//-----------------------------------------------------------------------------
void HotKeyManager::lendFixedKeys( const AsciiString *buttons, const Int *types, Int count )
{
	if( buttons == NULL )
	{
		for( Int slot = 0; slot < GRID_KEY_COUNT; slot++ )
			s_lentKeys[ slot ] = 0;
		return;
	}
	gridLentKeys( buttons, types, count, s_lentKeys );
}

//-----------------------------------------------------------------------------
/** CommandBarGrid, Enabled = Yes or No, End.  Only the player's own Hotkeys.ini carries it, and
	* nothing in it reaches the INI CRC. */
/*static*/ void INI::parseCommandBarGridDefinition( INI *ini )
{
	static const FieldParse fields[] =
	{
		{ "Enabled", INI::parseBool, NULL, 0 },
		{ NULL, NULL, NULL, 0 }
	};
	ini->initFromINI( &s_commandGrid, fields );
	DEBUG_LOG(( "HOTKEYS: grid keys %s\n", s_commandGrid ? "on" : "off" ));
}

//-----------------------------------------------------------------------------
/** CommandButtonHotKey <button name>, Key = one letter or NONE, End.  Only the player's own
	* Hotkeys.ini carries these, and nothing in it reaches the INI CRC. */
/*static*/ void INI::parseCommandButtonHotKeyDefinition( INI *ini )
{
	AsciiString name = ini->getNextToken();
	name.toLower();

	static const FieldParse fields[] =
	{
		{ "Key", INI::parseAsciiString, NULL, 0 },
		{ NULL, NULL, NULL, 0 }
	};
	AsciiString key;
	ini->initFromINI( &key, fields );

	WideChar letter = 0;
	if( key.getLength() == 1 && isalpha( (unsigned char)key.getCharAt( 0 ) ) )
		letter = (WideChar)toupper( (unsigned char)key.getCharAt( 0 ) );
	else if( key.compareNoCase( "NONE" ) != 0 )
		throw INI_INVALID_DATA;

	s_playerButtonKeys[ name.str() ] = letter;
	DEBUG_LOG(( "HOTKEYS: button %s key %s\n", name.str(), key.str() ));
}

//-----------------------------------------------------------------------------
HotKeyManager *TheHotKeyManager = NULL;

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

