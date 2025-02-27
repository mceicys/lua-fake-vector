/* lfvlua.c */
/* Copyright notice is at the end of this file */

#define _CRT_SECURE_NO_WARNINGS

#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#include "lfvlua.h"
#include "lfvreader.h"

static const char* ReaderLua(lua_State* l, void* dataIO, size_t* sizeOut);

/*--------------------------------------
	lfvLoadFile
--------------------------------------*/
int lfvLoadFile(lua_State* l, const char* filePath, int forceExpand, const char* logPath)
{
	lfv_reader_state rs;
	FILE* f;
	int ret;

	if(filePath)
		f = fopen(filePath, "r");
	else
		f = stdin;

	if(!f)
	{
		lua_pushstring(l, strerror(errno));
		return LUA_ERRFILE;
	}

	if(lfvInitReaderState(0, f, filePath ? filePath : "stdin", forceExpand, 1, 1, logPath, &rs))
	{
		lua_pushstring(l, rs.earliestError);
		return LUA_ERRMEM;
	}

	ret = lua_load(l, ReaderLua, (void*)&rs, rs.name, "t");

	if(filePath)
		fclose(f);

	if(ret)
	{
		lfvTermReaderState(&rs, 1);
		return ret;
	}

	lfvTermReaderState(&rs, 1);
	return ret;
}

/*--------------------------------------
	lfvLoadString
--------------------------------------*/
int	lfvLoadString(lua_State* l, const char* chunk, const char* chunkName, int forceExpand,
	const char* logPath)
{
	lfv_reader_state rs;
	int ret;

	if(lfvInitReaderState(chunk, 0, chunkName, forceExpand, 1, 0, logPath, &rs))
	{
		lua_pushstring(l, rs.earliestError);
		return LUA_ERRMEM;
	}

	ret = lua_load(l, ReaderLua, (void*)&rs, chunkName, "t");
	
	if(ret)
	{
		lfvTermReaderState(&rs, 1);
		return ret;
	}

	lfvTermReaderState(&rs, 1);
	return ret;
}

/*--------------------------------------
LUA	lfvCLuaLoadFile
--------------------------------------*/
int	lfvCLuaLoadFile(lua_State* l)
{
	if(lfvLoadFile(l, luaL_checkstring(l, 1), lua_toboolean(l, 2),
	luaL_optstring(l, 3, 0)) != LUA_OK)
	{
		lua_pushnil(l);
		lua_insert(l, -2);
		return 2;
	}

	return 1;
}

/*--------------------------------------
LUA	lfvCLuaLoadString
--------------------------------------*/
int	lfvCLuaLoadString(lua_State* l)
{
	if(lfvLoadString(l, luaL_checkstring(l, 1), luaL_optstring(l, 2, 0),
	lua_toboolean(l, 3), luaL_optstring(l, 4, 0)) != LUA_OK)
	{
		lua_pushnil(l);
		lua_insert(l, -2);
		return 2;
	}

	return 1;
}

/*--------------------------------------
	ReaderLua
--------------------------------------*/
static const char* ReaderLua(lua_State* l, void* data, size_t* size)
{
	const char* res = lfvReader(data, size);
	lfv_reader_state* s = (lfv_reader_state*)data;

	if(s->earliestError)
	{
		luaL_error(l, "Expansion error ('%s' ln %I): %s", lfvResolveName(s),
			(lua_Integer)s->errorLine, s->earliestError);

		return 0;
	}

	return res;
}

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
