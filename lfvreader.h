/* lfvreader.h */
/* Copyright notice is at the end of this file */

#ifndef LFV_READER_H
#define LFV_READER_H

#include <setjmp.h>
#include <stdio.h>

typedef struct lfv_reader_state_s {
	jmp_buf		memErrJmp;
	unsigned	level; /* recursion level */
	int			streamThruBuf, skipBOMAndPound;
	const char*	chk;
	FILE*		f;
	const char*	name;
	char*		buf; /* realloc'd null-terminated parse stream */
	size_t		bufSize;
	size_t		numBuf; /* Excludes null terminator */
	size_t		tok, tokSize; /* Char index and length in buf */
	unsigned	line;
	size_t		beforeSkip; /* tok value before skipping whitespace and comments together */
	size_t*		marks; /* realloc'd stack of char indices pointing at tokens relevant to vector
					   duplication */
	size_t		numMarksAlloc, numMarks;
	int			topResult;
	const char*	earliestError;
	unsigned	errorLine;
	const char*	logPath;
	FILE*		log;
} lfv_reader_state;

char*		lfvReader(void* dataIO, size_t* sizeOut);
int			lfvInitReaderState(const char* chunk, FILE* file, const char* name, int force,
			int stream, int skipBOMPound, const char* logPath, lfv_reader_state* sOut);
void		lfvTermReaderState(lfv_reader_state* sIO, int freeBuf);
const char*	lfvResolveName(const lfv_reader_state* s);

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
