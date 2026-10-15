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

// Stateless path classification shared by the full filesystem and isolated archive tests.
#include "PreRTS.h"
#include "Common/FileSystem.h"

//============================================================================
// FileSystem::isMapPath
//============================================================================
Bool FileSystem::isMapPath(const AsciiString& filename)
{
	// A mod's Maps.big carries its own edits of the stock maps under the same names (Contra's Twilight
	// Flame is not ours), and a lobby has to agree on one file: retail's and the fork's maps win always.
	return strncasecmp(filename.str(), "maps", 4) == 0 && (filename.str()[4] == '\\' || filename.str()[4] == '/');
}

