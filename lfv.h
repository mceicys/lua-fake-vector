/* lfv.h */
/* Martynas Ceicys */
/* Copyright notice at end of file */

#ifndef LFV_LUA_FAKE_VECTOR_H
#define LFV_LUA_FAKE_VECTOR_H

#include <lua.h>
#include <lauxlib.h>

/*
################################################################################################
	LOAD FUNCTIONS
################################################################################################
*/

/* Loads a text file with vector expansion. Mimics luaL_loadfile. */
int	lfvLoadFile(lua_State* l, const char* filePath, int forceExpand);

/* Loads a string with vector expansion. chunkName is used in error messages. If it's not given,
chunk is used as the name. Return value and stack changes mimic lfvLoadFile. */
int	lfvLoadString(lua_State* l, const char* chunk, const char* chunkName, int forceExpand);

/*
################################################################################################
	ERRORS

These functions return global values indicating an expansion error, or 0 if no expansion error
occurred.

Note that some other compilation error unrelated to expansion may occur whether an expansion
error happened or not, e.g. file couldn't open. This would be reported in a string pushed onto
the stack by the load function, not in these global values. Likewise, expansion may fail while
compilation succeeds (the rest of the script is left unexpanded). Check both.

The expansion error will usually complain about the same thing as Lua's compiler.
################################################################################################
*/

unsigned	lfvErrorLine(); /* Line on which the first error was detected */
const char*	lfvError(); /* Error string */

/*
################################################################################################
	DEBUG OUTPUT
################################################################################################
*/

/* Returns debug file path */
const char*	lfvDebugPath();

/* Sets filePath as the location to append expanded results for debugging. Returns 0 if filePath
was successfully fopen'd and fclose'd. If the file later fails to open or be written to, during
expansion, it fails silently. Clears file contents if clear is true. Outputs even for files that
are not being expanded if outputUnexpanded is true. Adds a header comment to each expanded
result if headerComment is true. filePath string is copied, caller can free it. If filePath is
0, disables output and returns 0. */
int			lfvSetDebugPath(const char* filePath, int clear, int outputUnexpanded,
			int headerComment);

/*
################################################################################################
	C LUA FUNCTIONS

These can be registered to a lua_State to give scripts access to the preprocessor.
################################################################################################
*/

/*	IN	sFilePath, [bForceExpand]
	OUT	CompiledChunk | (nil, sCompileError) */
int	lfvCLuaLoadFile(lua_State* l);

/*	IN	sChunk, [sChunkName], [bForceExpand]
	OUT	CompiledChunk | (nil, sCompileError) */
int	lfvCLuaLoadString(lua_State* l);

/*	OUT	[iErrorLine]

Returns nil if there was no expansion error. */
int	lfvCLuaErrorLine(lua_State* l);

/*	OUT	[sError] */
int	lfvCLuaError(lua_State* l);

#endif

/*
Copyright (C) 2023 Martynas Ceicys

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
