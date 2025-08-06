/* lfv.h */
/* Copyright notice is at the end of this file */

#ifndef LFV_H
#define LFV_H

/* Returns string of file contents with vector expansion or 0 on error. Free result with
lfvFreeBuffer.

If filePath is 0, reads stdin.

If forceExpand is 0, vector expansion is only done if the script's first statement is
'LFV_EXPAND_VECTORS()'. Even without expansion, a copy of the script is returned.

If logPath is given and the file there can be opened, results are appended to the file.

If 0 is returned, the err parameters are set to describe the error and are set to 0 otherwise.
Each of these parameters is optional.

UTF-8 BOM and first line starting with '#' are ignored and replaced with white space. */
char* lfvExpandFile(const char* filePath, int forceExpand, const char* logPath,
	const char** errMsgOut, unsigned* errLineOut);

/* Returns copy of chunk with vector expansion or 0 on error. Free result with lfvFreeBuffer.
See lfvExpandFile for info on parameters. */
char* lfvExpandString(const char* chunk, int forceExpand, const char* logPath,
	const char** errMsgOut, unsigned* errLineOut);

/* Frees buffer returned by expand func; does nothing if 0 */
void lfvFreeBuffer(char* buf);

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
