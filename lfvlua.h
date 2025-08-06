/* lfvlua.h */
/* Copyright notice is at the end of this file */

#ifndef LFV_LUA_H
#define LFV_LUA_H

#include "lua.h"

/*	OUT	CompiledChunk | sError

Loads a text file with vector expansion. Mimics luaL_LoadFile except precompiled chunks return
an error. If binOut is given, it's set to 1 if a precompiled chunk is the reason for failure.

Returns LUA_OK (0), LUA_ERRSYNTAX, LUA_ERRMEM, LUA_ERRFILE, or any code lua_load returns. */
int lfvLoadTextFile(lua_State* l, const char* filePath, int forceExpand, const char* logPath,
	int* binOut);

/*	OUT	CompiledChunk | sError

Loads a string with vector expansion. Pushed and returned values mimic lfvLoadTextFile. */
int lfvLoadString(lua_State* l, const char* chunk, int forceExpand, const char* logPath);

/*
	C LUA FUNCTIONS
*/

/*	OUT	lfv */
int luaopen_lfv(lua_State* l);

/*	IN	sFilePath, [bForceExpand], [sLogPath]
	OUT	CompiledChunk | (nil, sError) */
int lfvCLuaLoadTextFile(lua_State* l);

/*	IN	sChunk, [bForceExpand], [sLogPath]
	OUT	CompiledChunk | (nil, sError) */
int lfvCLuaLoadString(lua_State* l);

/*	IN	sFilePath, [bForceExpand], [sLogPath]
	OUT	sExpanded | (nil, sError) */
int lfvCLuaExpandFile(lua_State* l);

/*	IN	sChunk, [bForceExpand], [sLogPath]
	OUT	sExpanded | (nil, sError) */
int lfvCLuaExpandString(lua_State* l);

/*	OUT lfv

Inserts lfvCLuaSearcher as the second element in package.searchers if it doesn't already
exist in the array. Typically that's after package.preload and before the .lua searcher.

The LFV module is returned so setup can be done in one statement:
	lfv = require("lfv").EnsureSearcher() */
int lfvCLuaEnsureSearcher(lua_State* l);

/*	IN	sModuleName
	OUT	(Loader, sModulePath) | [sFailReason]

This function can be inserted into package.searchers before the standard .lua file searcher to
enable vector expansion on require'd scripts. */
int lfvCLuaSearcher(lua_State* l);

#endif

/*
Copyright (C) 2025 Martynas Ceicys

Permission is hereby granted, free of charge, to any person obtaining a copy of this software
and associated documentation files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
