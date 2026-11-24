/*
** Copyright 2026 contributors to Zero Hour Reforged Linux.
** Licensed under GPL-3.0-or-later; see LICENSE.md.
*/
#pragma once

// Shared by archive recognition and installed-language probing. Keep English first:
// it is the preferred fallback when an install contains more than one language.
namespace RetailLanguages
{
static const char *const names[] = { "english", "german", "german2", "french", "spanish", "italian",
	"korean", "chinese", "brazilian", "polish", "russian" };
}
