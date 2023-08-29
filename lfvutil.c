/* lfvutil.c */
/* Martynas Ceicys */
/* Copyright notice at end of file */

#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>

#include "lfv.h"

#define FALSE 0
#define TRUE 1

static int		forceExpansion = FALSE;
static char*	outputFilePath = 0;

/*--------------------------------------
	ReadOption
--------------------------------------*/
int ReadOption(char* vals[], int num, int* consume)
{
	*consume = 0;

	if(!num || vals[0][0] != '/')
	{
		printf("Argument must be an option starting with /\n");
		return 1;
	}

	if(!strcmp(vals[0], "/o"))
	{
		size_t len;

		if(num < 2)
		{
			printf("Expected outputFile after '/o'\n");
			return 1;
		}

		len = strlen(vals[1]);

		if(!len)
		{
			printf("outputFile strlen is 0\n");
			return 1;
		}

		outputFilePath = (char*)malloc(len + 1);
		strncpy(outputFilePath, vals[1], len);
		outputFilePath[len] = 0;
		*consume = 2;
	}
	else if(!strcmp(vals[0], "/f"))
	{
		forceExpansion = TRUE;
		*consume = 1;
	}
	else
	{
		printf("Invalid option '%s'\n", vals[0]);
		return 1;
	}

	return 0;
}

/*--------------------------------------
	StrRPBrk
--------------------------------------*/
const char* StrRPBrk(const char* str, const char* breakSet)
{
	const char* br = breakSet;
	const char* last = 0;

	while(*br != '\0')
	{
		const char* ch = strrchr(str, *br);

		if(ch && (!last || ch > last))
			last = ch;

		br++;
	}

	return last;
}

/*--------------------------------------
	ExtensionDot
--------------------------------------*/
const char* ExtensionDot(const char* str)
{
	const char* ext = strrchr(str, '.');
	const char* slash = StrRPBrk(str, "/\\");

	if(ext && slash && slash - str > ext - str)
		ext = 0;

	return ext;
}

/*--------------------------------------
	main
--------------------------------------*/
int main(int argc, char* argv[])
{
	int i, err;
	const char* inputFilePath;
	lua_State* l;

	if(argc < 2)
	{
		printf(
"LFVUTIL inputFile [/o outputFile] [/f]"
"\n\n"
"If outputFile is not given, it is set to inputFile with '_expanded' appended\n"
"to the filename."
"\n\n"
"If /f is set, vector expansion is forced even if the file does not begin with\n"
"'LFV_EXPAND_VECTORS()'."
"\n"
		);

		return 0;
	}

	inputFilePath = argv[1];

	for(i = 2; i < argc;)
	{
		int consume;

		if(err = ReadOption(argv + i, argc - i, &consume))
			return err;

		i += consume;
	}

	if(!outputFilePath)
	{
		const size_t APPEND_LEN = 9;
		size_t inLen = strlen(inputFilePath);
		size_t outLen = inLen + APPEND_LEN;
		const char* ext = ExtensionDot(inputFilePath);
		size_t append = ext ? ext - inputFilePath : inLen;
		outputFilePath = (char*)malloc(outLen + 1);
		strncpy(outputFilePath, inputFilePath, append);
		strncpy(outputFilePath + append, "_expanded", APPEND_LEN);
		strncpy(outputFilePath + append + APPEND_LEN, inputFilePath + append, inLen - append);
		outputFilePath[outLen] = 0;
	}

	if(!strcmp(inputFilePath, outputFilePath))
	{
		printf("inputFile and outputFile should not be equal\n");
		return 1;
	}

	l = luaL_newstate();
	lfvSetDebugPath(outputFilePath, TRUE, TRUE, FALSE);
	err = lfvLoadFile(l, inputFilePath, forceExpansion);

	if(err)
		printf("%s\n", lua_tostring(l, -1));

	if(lfvError())
		printf("expansion error ln %u: %s\n", lfvErrorLine(), lfvError());

	printf("Expanded '%s' to '%s'\n", inputFilePath, outputFilePath);
	lua_pop(l, 1);
	lua_close(l);

	if(outputFilePath)
	{
		free(outputFilePath);
		outputFilePath = 0;
	}

	return 0;
}

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
