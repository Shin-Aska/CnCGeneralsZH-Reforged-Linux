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

//============================================================================
// FileSystem::isForkUIPath
//============================================================================
Bool FileSystem::isForkUIPath(const AsciiString& filename)
{
	// A mod integrates into our game and never brings its own interface: every window layout, the
	// fonts (Language.ini, HeaderTemplate.ini), the shell's colours and transitions and the image sheets
	// the shell and the HUD frames are cut from.  The image sheets a mod's cameos live in
	// (SAUserInterface512 and the like) and the Generals Challenge portraits stay the mod's: those are
	// its units and its generals.  The textures our sheets name are added in ArchiveFileSystem::loadMods.
	AsciiString path = filename;
	path.toLower();
	std::string p(path.str());
	for (size_t i = 0; i < p.size(); ++i)
	{
		if (p[i] == '/')
			p[i] = '\\';
	}
	if (p.compare(0, strlen("window\\"), "window\\") == 0)
		return TRUE;
	const size_t slash = p.find_last_of('\\');
	const std::string dir = slash == std::string::npos ? std::string() : p.substr(0, slash + 1);
	const std::string name = slash == std::string::npos ? p : p.substr(slash + 1);
	// Data\<language>\Language.ini and HeaderTemplate.ini
	if (dir.compare(0, strlen("data\\"), "data\\") == 0 && dir.find('\\', strlen("data\\")) == dir.size() - 1
		&& (name == "language.ini" || name == "headertemplate.ini"))
		return TRUE;
	if ((dir == "data\\ini\\" || dir == "data\\ini\\default\\")
		&& (name == "shellmenuscheme.ini" || name == "windowtransitions.ini" || name == "ingameui.ini" || name == "controlbarresizer.ini"))
		return TRUE;
	static const char *const sheets[] = { "handcreatedmappedimages.ini", "scshelluserinterface512.ini",
		"scsmshelluserinterface512.ini", "scgameuserinterface512.ini", "sclogosuserinterface512.ini",
		"scpurchasepowers512.ini", "sacontrolbar512.ini", "sncontrolbar512.ini", "sucontrolbar512.ini" };
	if (dir.compare(0, strlen("data\\ini\\mappedimages\\"), "data\\ini\\mappedimages\\") == 0)
	{
		for (size_t i = 0; i < sizeof(sheets) / sizeof(sheets[0]); ++i)
		{
			if (name == sheets[i])
				return TRUE;
		}
	}
	return FALSE;
}
