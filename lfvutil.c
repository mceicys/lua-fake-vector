/* lfvutil.c */
/* Copyright notice is at the end of this file */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lfv.h"

static const char* programName = 0;
static const char* inputFilePath = 0;
static int forceExpansion = 0;

/*--------------------------------------
	LastCharSkipped
--------------------------------------*/
static char* LastCharSkipped(char* str, int ch)
{
	char* ret = strrchr(str, ch);
	return ret ? ret + 1 : str;
}

/*--------------------------------------
	CalcProgramName
--------------------------------------*/
static void CalcProgramName(char** argv, int argc)
{
	char* temp;

	if(argc < 1 || !argv[0])
	{
		programName = "lfvutil";
		return;
	}
	
	temp = argv[0];
	temp = LastCharSkipped(temp, '/');
	temp = LastCharSkipped(temp, '\\');
	programName = temp;
}

/*--------------------------------------
	ReadOption
--------------------------------------*/
static int ReadOption(const char* const* vals, int num, int* consume)
{
	*consume = 0;

	if(!num || vals[0][0] != '-')
	{
		printf("Argument must be an option starting with -\n");
		return 1;
	}

	if(!strcmp(vals[0], "-h"))
	{
		printf(
"%s [-h] [-i inputFile] [-f]\n"
"\n"
"If inputFile is not given, reads from stdin.\n"
"\n"
"If -f is set, vector expansion is forced even if the script does not begin \n"
"with 'LFV_EXPAND_VECTORS()'.\n",
		programName);

		*consume = 1;
		exit(0);
	}
	else if(!strcmp(vals[0], "-i"))
	{
		if(num < 2)
		{
			printf("Expected inputFile after '-i'\n");
			return 1;
		}

		inputFilePath = vals[1];
		*consume = 2;
	}
	else if(!strcmp(vals[0], "-f"))
	{
		forceExpansion = 1;
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
	main
--------------------------------------*/
int main(int argc, char** argv)
{
	int i, errOpt;
	char* result;
	const char* errExp;
	unsigned errLine;

	CalcProgramName(argv, argc);

	for(i = 1; i < argc;)
	{
		int consume;

		if((errOpt = ReadOption((const char**)argv + i, argc - i, &consume)))
			return errOpt;

		i += consume;
	}

	result = lfvExpandFile(inputFilePath, forceExpansion, 0, &errExp, &errLine);

	if(errExp)
	{
		printf("Expansion error (ln %u): %s\n", errLine, errExp);
		lfvFreeBuffer(result);
		return 1;
	}

	printf("%s", result);
	lfvFreeBuffer(result);
	return 0;
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
