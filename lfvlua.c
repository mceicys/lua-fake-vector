/* lfvlua.c */
/* Copyright notice is at the end of this file */

#define _CRT_SECURE_NO_WARNINGS

#include <errno.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include "lfv.h"
#include "lfvlua.h"
#include "lfvreader.h"

#if LUA_VERSION_NUM >= 502
	#define cross_lua_load(L, reader, data, chunkname, mode) lua_load(L, reader, data, chunkname, mode)
	#define cross_lua_equal(L, idx1, idx2) lua_compare(L, idx1, idx2, LUA_OPEQ)
	#define cross_lua_pushlstring(L, s, len) lua_pushlstring(L, s, len)
	#define cross_lua_absindex(L, idx) lua_absindex(L, idx)
#else
	#define cross_lua_load(L, reader, data, chunkname, mode) lua_load(L, reader, data, chunkname)
	#define cross_lua_equal(L, idx1, idx2) lua_equal(L, idx1, idx2)
	#define cross_lua_pushlstring(L, s, len) (lua_pushlstring(L, s, len), lua_tostring(L, -1))
	#define cross_lua_absindex(L, idx) ((idx) < 0 ? lua_gettop(L) + ((idx) + 1) : (idx)) /* Doesn't support pseudo-indices */
#endif

#if LUA_VERSION_NUM >= 503
	#define cross_lua_rawgeti(L, idx, n) lua_rawgeti(L, idx, n)
	#define cross_lua_getglobal(L, name) lua_getglobal(L, name)
	#define cross_lua_getfield(L, idx, k) lua_getfield(L, idx, k)
#else
	#define cross_lua_rawgeti(L, idx, n) (lua_rawgeti(L, idx, n), lua_type(L, -1))
	#define cross_lua_getglobal(L, name) (lua_getglobal(L, name), lua_type(L, -1))
	#define cross_lua_getfield(L, idx, k) (lua_getfield(L, idx, k), lua_type(L, -1))
#endif

#if LUA_VERSION_NUM >= 502
	#if !defined (LUA_PATH_SEP)
		#define CROSS_LUA_PATH_SEP ";"
	#else
		#define CROSS_LUA_PATH_SEP LUA_PATH_SEP
	#endif
	
	#if !defined (LUA_PATH_MARK)
		#define CROSS_LUA_PATH_MARK "?"
	#else
		#define CROSS_LUA_PATH_MARK LUA_PATH_MARK
	#endif
#else
	#define CROSS_LUA_PATH_SEP LUA_PATHSEP
	#define CROSS_LUA_PATH_MARK LUA_PATH_MARK
#endif

#ifndef LUA_OK
	#define LUA_OK 0
#endif

typedef char* ExpandFunc(const char* str, int forceExpand, const char* logPath,
	const char** errMsgOut, unsigned* errLineOut);

static const char* ReaderLua(lua_State* l, void* dataIO, size_t* sizeOut);
static int SetupLoadReturn(lua_State* l, const lfv_reader_state* rs, int loadRet);
static int ConvertErrorLfvToLuaLoad(int loadRet);
static const char* FindModulePath(lua_State* l, const char* moduleName);
static int GenericCLuaExpand(lua_State* l, ExpandFunc* func, int isFilePath);
static int GetGlobalTableField(lua_State* l, const char* table, const char* field);
static void TableRawInsert(lua_State* l, int t, int n);

/*--------------------------------------
	lfvLoadTextFile
--------------------------------------*/
int lfvLoadTextFile(lua_State* l, const char* filePath, int forceExpand, const char* logPath,
	int* bin)
{
	lfv_reader_state rs;
	FILE* f;
	int ret;

	if(bin) *bin = 0;

	if(filePath)
		f = fopen(filePath, "r");
	else
		f = stdin;

	if(!f)
	{
		lua_pushfstring(l, "Failed to open '%s': %s", filePath, strerror(errno));
		return LUA_ERRFILE;
	}

	if(lfvInitReaderState(0, f, filePath ? filePath : "stdin", forceExpand, 1, 1, logPath, &rs))
	{
		lua_pushstring(l, rs.earliestError);

		if(bin && rs.errorCode == LFV_ERR_BINARY)
			*bin = 1;

		return ConvertErrorLfvToLuaLoad(rs.errorCode);
	}

	ret = cross_lua_load(l, ReaderLua, (void*)&rs, rs.name, "t");

	if(filePath)
		fclose(f);

	ret = SetupLoadReturn(l, &rs, ret);
	lfvTermReaderState(&rs, 1);
	return ret;
}

/*--------------------------------------
	lfvLoadString
--------------------------------------*/
int	lfvLoadString(lua_State* l, const char* chunk, int forceExpand, const char* logPath)
{
	lfv_reader_state rs;
	int ret;

	if(lfvInitReaderState(chunk, 0, chunk, forceExpand, 1, 0, logPath, &rs))
	{
		lua_pushstring(l, rs.earliestError);
		return ConvertErrorLfvToLuaLoad(rs.errorCode);
	}

	ret = cross_lua_load(l, ReaderLua, (void*)&rs, rs.name, "t");
	ret = SetupLoadReturn(l, &rs, ret);
	lfvTermReaderState(&rs, 1);
	return ret;
}

/*--------------------------------------
	luaopen_lfv
--------------------------------------*/
int luaopen_lfv(lua_State* l)
{
	luaL_Reg* reg;

	luaL_Reg functions[] = {
		{"LoadTextFile", lfvCLuaLoadTextFile},
		{"LoadString", lfvCLuaLoadString},
		{"ExpandFile", lfvCLuaExpandFile},
		{"ExpandString", lfvCLuaExpandString},
		{"Searcher", lfvCLuaSearcher},
		{0, 0}
	};

	luaL_Reg closures[] = {
		{"EnsureSearcher", lfvCLuaEnsureSearcher},
		{0, 0}
	};

	lua_createtable(l, 0, (sizeof(functions) / sizeof(luaL_Reg) - 1) + (sizeof(closures) / sizeof(luaL_Reg) - 1));

	for(reg = functions; reg->name; reg++)
	{
		lua_pushcfunction(l, reg->func);
		lua_setfield(l, -2, reg->name);
	}

	for(reg = closures; reg->name; reg++)
	{
		lua_pushvalue(l, -1); /* Upvalue 1 is the lib table */
		lua_pushcclosure(l, reg->func, 1);
		lua_setfield(l, -2, reg->name);
	}

	return 1;
}

/*--------------------------------------
	lfvCLuaLoadTextFile
--------------------------------------*/
int	lfvCLuaLoadTextFile(lua_State* l)
{
	if(lfvLoadTextFile(l, luaL_checkstring(l, 1), lua_toboolean(l, 2),
	luaL_optstring(l, 3, 0), 0) != LUA_OK)
	{
		lua_pushnil(l);
		lua_insert(l, -2);
		return 2;
	}

	return 1;
}

/*--------------------------------------
	lfvCLuaLoadString
--------------------------------------*/
int	lfvCLuaLoadString(lua_State* l)
{
	if(lfvLoadString(l, luaL_checkstring(l, 1), lua_toboolean(l, 2),
		luaL_optstring(l, 3, 0)) != LUA_OK)
	{
		lua_pushnil(l);
		lua_insert(l, -2);
		return 2;
	}

	return 1;
}

/*--------------------------------------
	lfvCLuaExpandFile
--------------------------------------*/
int	lfvCLuaExpandFile(lua_State* l)
{
	return GenericCLuaExpand(l, lfvExpandFile, 1);
}

/*--------------------------------------
	lfvCLuaExpandString
--------------------------------------*/
int	lfvCLuaExpandString(lua_State* l)
{
	return GenericCLuaExpand(l, lfvExpandString, 0);
}

/*--------------------------------------
	lfvCLuaEnsureSearcher
--------------------------------------*/
int lfvCLuaEnsureSearcher(lua_State* l)
{
	const int WANT_INDEX = 2;
	int i, found = 0;

	if(cross_lua_getglobal(l, "package") != LUA_TTABLE)
		luaL_error(l, "'package' is not a table");

	if(cross_lua_getfield(l, -1, "searchers") != LUA_TTABLE)
	{
		lua_pop(l, 1);

		if(cross_lua_getfield(l, -1, "loaders") != LUA_TTABLE)
		{
			lua_pop(l, 1);
			return luaL_error(l, "Neither 'package.searchers' nor 'package.loaders' are tables");
		}
	}

	lua_remove(l, -2); /* package */
	lua_pushcfunction(l, lfvCLuaSearcher);

	for(i = 1; !found; i++)
	{
		int type = cross_lua_rawgeti(l, -2, i);
		found = cross_lua_equal(l, -2, -1);
		lua_pop(l, 1);

		if(type == LUA_TNIL)
			break;
	}

	if(found)
		lua_pop(l, 1); /* lfvCLuaSearcher */
	else
		TableRawInsert(l, -2, i < WANT_INDEX ? i : WANT_INDEX);

	lua_pop(l, 1); /* searchers/loaders */

	/* Return lib table */
	lua_pushvalue(l, lua_upvalueindex(1));
	return 1;
}

/*--------------------------------------
	lfvCLuaSearcher
--------------------------------------*/
int	lfvCLuaSearcher(lua_State* l)
{
	const char* moduleName = luaL_checkstring(l, 1);
	const char* modulePath = FindModulePath(l, moduleName);
	int err, bin;

	if(!modulePath)
		return 0;

	err = lfvLoadTextFile(l, modulePath, 0, 0, &bin);

	if(err == LUA_OK)
	{
		lua_insert(l, -2);
		return 2;
	}
	else if(bin)
	{
		/* Don't throw an error, let another searcher try loading */
		return 1;
	}
	else
	{
		return luaL_error(l, "LFV failed to load module '%s' from file '%s':\n\t%s",
			moduleName, modulePath, lua_tostring(l, -1));
	}
}

/*--------------------------------------
	ReaderLua
--------------------------------------*/
static const char* ReaderLua(lua_State* l, void* data, size_t* size)
{
	const char* res = lfvReader(data, size);
	lfv_reader_state* s = (lfv_reader_state*)data;
	(void)l; /* Suppress unreferenced parameter warning */

	if(s->earliestError)
	{
		*size = 0;
		return 0;
	}

	return res;
}

/*--------------------------------------
	SetupLoadReturn

Adjusts lua_load's pushed and returned values to account for expansion errors.
--------------------------------------*/
static int SetupLoadReturn(lua_State* l, const lfv_reader_state* rs, int loadRet)
{
	char nameBuf[LFV_NAME_BUF_SIZE];
	const char* splitter;

	if(!rs->earliestError)
		return loadRet; /* Nothing to add */

	if(loadRet == LUA_OK)
	{
		/* lua_load loaded something despite failed expansion; destroy it */
		lua_pop(l, 1);
	}

	splitter = loadRet == LUA_OK ? "" : "\n";

	lua_pushfstring(l, "Expansion error ('%s' ln %d): %s%s",
		lfvResolveName(rs, nameBuf, sizeof(nameBuf)),
		(int)rs->errorLine,
		rs->earliestError,
		splitter
	);

	if(loadRet != LUA_OK)
	{
		lua_insert(l, -2); /* Put expansion error first */
		lua_concat(l, 2); /* Concatenate expansion and lua_load error strings */
	}

	/* We'll assume lua_load failed due to expansion failing, so LFV error takes priority */
	return ConvertErrorLfvToLuaLoad(rs->errorCode);
}

/*--------------------------------------
	ConvertErrorLfvToLuaLoad
--------------------------------------*/
static int ConvertErrorLfvToLuaLoad(int lfvCode)
{
	switch(lfvCode)
	{
	case LFV_OK:
		return LUA_OK;
	case LFV_ERR_BINARY:
		return LUA_ERRSYNTAX;
	case LFV_ERR_SYNTAX:
		return LUA_ERRSYNTAX;
	case LFV_ERR_RUNTIME:
		return LUA_ERRSYNTAX;
	case LFV_ERR_MEMORY:
		return LUA_ERRMEM;
	case LFV_ERR_FILE:
		return LUA_ERRFILE;
	default:
		return LUA_ERRSYNTAX;
	}
}

/*--------------------------------------
	FindModulePath

OUT	[sFilePath]

Returns and pushes sFilePath, or returns 0 and pushes nothing.
--------------------------------------*/
static const char* FindModulePath(lua_State* l, const char* moduleName)
{
	const char* separatedModuleName;
	int pathType;
	const char *curPath, *nextPath, *finalPath = 0;

	luaL_checkstack(l, 4, 0);
	separatedModuleName = luaL_gsub(l, moduleName, ".", LUA_DIRSEP);
	pathType = GetGlobalTableField(l, "package", "path");

	if(pathType != LUA_TSTRING)
		return (lua_pop(l, 2), (const char*)0);

	curPath = lua_tostring(l, -1);

	while(curPath)
	{
		size_t len;
		const char *truncatedPath, *substitutedPath;
		FILE* file;

		nextPath = strchr(curPath, *CROSS_LUA_PATH_SEP);

		if(nextPath)
		{
			len = nextPath - curPath;
			nextPath++;
		}
		else
			len = strlen(curPath);

		truncatedPath = cross_lua_pushlstring(l, curPath, len);
		substitutedPath = luaL_gsub(l, truncatedPath, CROSS_LUA_PATH_MARK, separatedModuleName);
		lua_remove(l, -2); /* truncatedPath */
		file = fopen(substitutedPath, "r");

		if(file)
		{
			fclose(file);
			finalPath = substitutedPath;
			break;
		}

		lua_pop(l, 1); /* substitutedPath */
		curPath = nextPath;
	}

	/* Remove package.path and separatedModuleName */
	if(finalPath)
	{
		lua_remove(l, -2);
		lua_remove(l, -2);
	}
	else
		lua_pop(l, 2);
	
	return finalPath;
}

/*--------------------------------------
	GenericCLuaExpand

IN	sSource, [bForceExpand], [sLogPath]
OUT	sExpanded | (nil, sError)
--------------------------------------*/
static int GenericCLuaExpand(lua_State* l, ExpandFunc* func, int isFilePath)
{
	char nameBuf[LFV_NAME_BUF_SIZE];
	const char* errMsg;
	unsigned int errLine;
	const char* source = luaL_checkstring(l, 1);
	char* expanded = func(source, lua_toboolean(l, 2), luaL_optstring(l, 3, 0), &errMsg, &errLine);

	if(expanded)
	{
		lua_pushstring(l, expanded);
		lfvFreeBuffer(expanded);
		return 1;
	}
	else
	{
		const char* name = isFilePath ? source : lfvTruncatedName(source, nameBuf, sizeof(nameBuf));
		lua_pushnil(l);
		lua_pushfstring(l, "Expansion error ('%s' ln %d): %s", name, (int)errLine, errMsg);
		return 2;
	}
}

/*--------------------------------------
	GetGlobalTableField

OUT	table[field] | nil
--------------------------------------*/
static int GetGlobalTableField(lua_State* l, const char* table, const char* field)
{
	int tableType;

	luaL_checkstack(l, 2, 0);
	tableType = cross_lua_getglobal(l, table);

	if(tableType == LUA_TTABLE)
	{
		cross_lua_getfield(l, -1, field);
		lua_remove(l, -2);
	}
	else
	{
		lua_pop(l, 1);
		lua_pushnil(l);
	}

	return lua_type(l, -1);
}

/*--------------------------------------
	TableRawInsert

Pops and inserts top value into t[n], pushing all elements [n, #t] up one index.
--------------------------------------*/
static void TableRawInsert(lua_State* l, int t, int n)
{
	int i, type;

	luaL_checkstack(l, 2, 0);
	t = cross_lua_absindex(l, t);
	n = cross_lua_absindex(l, n);
	type = cross_lua_rawgeti(l, t, n); /* Get first value */

	for(i = n; ; i++)
	{
		if(type == LUA_TNIL)
		{
			lua_pop(l, 1);
			break;
		}

		/* Get next value, preserving it on stack */
		type = cross_lua_rawgeti(l, t, i + 1);

		/* Overwrite next value in table */
		lua_insert(l, -2);
		lua_rawseti(l, t, i + 1);
	}

	lua_rawseti(l, t, n);
}

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
