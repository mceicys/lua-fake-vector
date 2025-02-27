/* lfvlua.h */
/* Copyright notice is at the end of this file */

#ifndef LFV_LUA_H
#define LFV_LUA_H

#include <lua.h>

/* Loads a text file with vector expansion. Mimics luaL_loadfile. */
int lfvLoadFile(lua_State* l, const char* filePath, int forceExpand, const char* logPath);

/* Loads a string with vector expansion. chunkName is used in error messages if it's not 0.
Return value and stack changes mimic lfvLoadFile. */
int lfvLoadString(lua_State* l, const char* chunk, const char* chunkName, int forceExpand,
	const char* logPath);

/*
	C LUA FUNCTIONS
	These can be registered to a lua_State to give scripts access to the preprocessor.
*/

/*	IN	sFilePath, [bForceExpand], [sLogPath]
	OUT	CompiledChunk | (nil, sError) */
int lfvCLuaLoadFile(lua_State* l);

/*	IN	sChunk, [sChunkName], [bForceExpand], [sLogPath]
	OUT	CompiledChunk | (nil, sError) */
int lfvCLuaLoadString(lua_State* l);

#endif

/*
Copyright (C) 2025 Martynas Ceicys

Permission is hereby granted, free of charge, to any person obtaining a copy of this software
and associated documentation files (the “Software”), to deal in the Software without
restriction, including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
