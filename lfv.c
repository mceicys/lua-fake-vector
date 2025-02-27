/* lfv.c */
/* Copyright notice is at the end of this file */

#define _CRT_SECURE_NO_WARNINGS

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lfv.h"
#include "lfvreader.h"

#define MAX_RECURSION_LEVELS 200

#define FALSE 0
#define TRUE 1
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define RETURN_ERROR(str) return SetReaderError(s, str, line)
#define IDENTIFIER_CHARS "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz"
#define NUMERAL_CHARS    "0123456789ABCDEFPXabcdefpx+-."
#define WHITESPACE_CHARS " \t\n\f\r"
#define INIT_BUF_SIZE 256
#define LOG_PREFIX "-- LFV: "
#define STRINGIFY(x) STRINGIFY_(x)
#define STRINGIFY_(x) #x

enum
{
	/* Expansion return values */
	EXPAND_OK, /* Function advanced tok and found good syntax */
	EXPAND_UNFIT, /* Function did not advance tok */
	EXPAND_ERR, /* Function advanced tok but found bad syntax; this result must propagate */

	/* Extra states only used in reader function */
	EXPAND_OFF,
	EXPAND_INIT,
	EXPAND_INIT_FORCE
};

typedef struct delayed_duplication_s {
	size_t expStart, marksStart;
} delayed_duplication;

static char*		ReaderNoSetJmp(void* dataIO, size_t* sizeOut);
static int			SetReaderError(lfv_reader_state* sIO, const char* str, unsigned line);
static int			ExpandBlock(lfv_reader_state* sIO);
static int			ExpandStat(lfv_reader_state* sIO);
static int			ExpandAttnamelist(lfv_reader_state* sIO);
static int			ExpandAttname(lfv_reader_state* sIO);
static int			ExpandAttrib(lfv_reader_state* sIO);
static int			ExpandRetstat(lfv_reader_state* sIO);
static int			ExpandLabel(lfv_reader_state* sIO);
static int			ExpandName(lfv_reader_state* sIO, int checkVector);
static int			ExpandNumeral(lfv_reader_state* sIO);
static int			ExpandString(lfv_reader_state* sIO);
static int			ExpandFuncname(lfv_reader_state* sIO);
static int			ExpandExplist(lfv_reader_state* sIO);
static int			ExpandExp(lfv_reader_state* sIO, delayed_duplication* ddOut);
static int			ExpandArgs(lfv_reader_state* sIO);
static int			ExpandFunctiondef(lfv_reader_state* sIO);
static int			ExpandFuncbody(lfv_reader_state* sIO);
static int			ExpandTableconstructor(lfv_reader_state* sIO);
static int			ExpandFieldlist(lfv_reader_state* sIO);
static int			ExpandField(lfv_reader_state* sIO, size_t* markedVecCompsOut,
					size_t* markedExpsOut);
static int			ExpandFieldsep(lfv_reader_state* sIO);
static int			ExpandBinop(lfv_reader_state* sIO);
static int			ExpandUnop(lfv_reader_state* sIO);
static const char*	DuplicateVecs(lfv_reader_state* sIO, size_t expStart, size_t marksStart,
					size_t* marksAddedOut);
static const char*	MergeFields(lfv_reader_state* sIO, size_t marksStart, size_t numMarks,
					size_t mergeableEnd);
static const char*	CheckMarkPrefix(const lfv_reader_state* s, size_t markIndex,
					size_t* compsOut);
static void			SkipBOMAndPound(lfv_reader_state* sIO);
static size_t		WhitespaceSpan(const char* str, unsigned* lineIO);
static void			ConsumeToken(lfv_reader_state* sIO);
static void			EraseToken(lfv_reader_state* sIO);
static void			NextTokenNoSkip(lfv_reader_state* sIO, size_t* afterConsumeOut);
static void			NextTokenSkipCom(lfv_reader_state* sIO);
static size_t		ExtendToken(lfv_reader_state* sIO, const char* set);
static size_t		ExtendCToken(lfv_reader_state* sIO, const char* cset);
static size_t		ExtendTokenSize(lfv_reader_state* sIO, size_t size);
static size_t		ReadMore(lfv_reader_state* sIO);
static int			EqualToken(const lfv_reader_state* s, const char* cmp);
static int			TokenStartsWith(const lfv_reader_state* s, const char* cmp);
static int			StringStartsWith(const char* str, size_t len, const char* cmp);
static int			SkipLongBracket(lfv_reader_state* sIO);
static int			SkipComment(lfv_reader_state* sIO);
static int			StringIsShortComment(const char* str);
static void			CopyShiftRight(lfv_reader_state* sIO, size_t start, size_t amount,
					int updateMarks);
static size_t		EOFCheckedFRead(void* dstBuf, size_t elementSize, size_t count,
					FILE* stream);
static const char*	StrChrNull(const char* str, int ch);
static size_t		CopyStringNoTerm(char* destIO, const char* str, size_t maxNum);
static size_t		CeilPow2(size_t n);
static void			SetEmptyBufferSize(lfv_reader_state* sIO);
static size_t		EnsureBufSize(lfv_reader_state* sIO, size_t n, int doMemJmp);
static size_t		EnsureNumMarksAlloc(lfv_reader_state* sIO, size_t n, int doMemJmp);
static void*		ReallocOrFree(void* mem, size_t newSize);
static size_t		AddMark(lfv_reader_state* sIO, size_t c);
static void			RemoveMarks(lfv_reader_state* sIO, size_t start, size_t num);
static size_t		AddSizeT(lfv_reader_state* sIO, size_t a, size_t b);
static size_t		MulSizeT(lfv_reader_state* sIO, size_t a, size_t b);
static void			IncRecursionLevel(lfv_reader_state* sIO);
static void			DecRecursionLevel(lfv_reader_state* sIO);

/*--------------------------------------
	lfvExpandFile
--------------------------------------*/
char* lfvExpandFile(const char* filePath, int forceExpand, const char* logPath,
	const char** err, unsigned* errLine)
{
	lfv_reader_state rs;
	FILE* f;
	char* ret = 0;
	size_t retSize = 0;

	if(err) *err = 0;
	if(errLine) *errLine = 0;

	if(filePath)
		f = fopen(filePath, "r");
	else
		f = stdin;

	if(!f)
	{
		if(err) *err = strerror(errno);
		return 0;
	}

	if(!lfvInitReaderState(0, f, filePath ? filePath : "stdin", forceExpand, FALSE, TRUE,
	logPath, &rs))
		ret = lfvReader(&rs, &retSize);

	if(filePath)
		fclose(f);

	if(rs.earliestError)
	{
		if(err) *err = rs.earliestError;
		if(errLine) *errLine = rs.errorLine;
		lfvTermReaderState(&rs, TRUE);
		return 0;
	}

	lfvTermReaderState(&rs, FALSE);
	return ret;
}

/*--------------------------------------
	lfvExpandString
--------------------------------------*/
char* lfvExpandString(const char* chunk, int forceExpand, const char* logPath, const char** err,
	unsigned* errLine)
{
	lfv_reader_state rs;
	char* ret = 0;
	size_t retSize = 0;
	
	if(err) *err = 0;
	if(errLine) *errLine = 0;

	if(!lfvInitReaderState(chunk, 0, chunk, forceExpand, FALSE, FALSE, logPath, &rs))
		ret = lfvReader(&rs, &retSize);
	
	if(rs.earliestError)
	{
		if(err) *err = rs.earliestError;
		if(errLine) *errLine = rs.errorLine;
		lfvTermReaderState(&rs, TRUE);
		return 0;
	}

	lfvTermReaderState(&rs, FALSE);
	return ret;
}

/*--------------------------------------
	lfvFreeBuffer
--------------------------------------*/
void lfvFreeBuffer(char* buf)
{
	if(buf)
		free(buf);
}

/*--------------------------------------
	lfvReader

Returns data->buf. *data must be a lfv_reader_state initialized with lfvInitReaderState. *size
is set to the length of the returned buf that has been preprocessed and is ready to be sent to
the Lua interpreter.

If data->streamThruBuf is 1, keep calling until *size is 0.

If data->streamThruBuf is 0, call once to get the entire null-terminated result. *size does not
include the null terminator.

On failed malloc, sets data's error info and does longjmp to data->memErrJmp.
--------------------------------------*/
char* lfvReader(void* data, size_t* size)
{
	lfv_reader_state* s = (lfv_reader_state*)data;

	if(!setjmp(s->memErrJmp))
		return ReaderNoSetJmp(data, size);

	/* After longjmp */
	*size = 0;
	return 0;
}

/*--------------------------------------
	lfvInitReaderState

Returns 1 in case of a malloc error and sets error info in s.
--------------------------------------*/
int lfvInitReaderState(const char* chunk, FILE* file, const char* name, int force, int stream,
	int skipBOMPound, const char* logPath, lfv_reader_state* s)
{
	s->level = 0;
	s->streamThruBuf = stream;
	s->skipBOMAndPound = skipBOMPound;
	s->chk = chunk;
	s->f = file;
	s->name = name;
	s->buf = 0;
	s->bufSize = 0;
	s->numBuf = 0;
	s->tok = 0;
	s->tokSize = 0;
	s->line = 1;
	s->beforeSkip = 0;
	s->marks = 0;
	s->numMarks = 0;
	s->numMarksAlloc = 0;
	s->topResult = force ? EXPAND_INIT_FORCE : EXPAND_INIT;
	s->earliestError = 0;
	s->errorLine = 0;
	s->log = 0;
	s->logPath = logPath;

	if(s->chk)
	{
		/* Given a string */
		size_t chkLen = strlen(s->chk);
		s->f = 0;

		if(s->streamThruBuf)
			s->numBuf = MIN(chkLen, INIT_BUF_SIZE - 1); /* Allocate buffer for partial copy */
		else
			s->numBuf = chkLen; /* Allocate buffer to fit the whole thing */

		s->bufSize = s->numBuf + 1;
		s->buf = (char*)malloc(s->bufSize);

		if(!s->buf)
		{
			SetReaderError(s, "Failed to malloc buf", s->line);
			SetEmptyBufferSize(s);
			return 1;
		}

		s->chk += CopyStringNoTerm(s->buf, s->chk, s->numBuf);
		s->buf[s->numBuf] = 0;
	}
	else if(s->f)
	{
		/* Only given a file, allocate buffer and initialize with first read */
		if(!EnsureBufSize(s, INIT_BUF_SIZE, FALSE) || !s->buf /* suppress warning */)
			return 1;

		s->numBuf = EOFCheckedFRead(s->buf, 1, INIT_BUF_SIZE - 1, s->f);
		s->buf[s->numBuf] = 0;
	}

	return 0;
}

/*--------------------------------------
	lfvTermReaderState
--------------------------------------*/
void lfvTermReaderState(lfv_reader_state* s, int freeBuf)
{
	if(freeBuf && s->buf)
	{
		free(s->buf);
		s->buf = 0;
	}

	if(s->marks)
	{
		free(s->marks);
		s->marks = 0;
	}

	if(s->log)
	{
		fputc('\n', s->log);
		fclose(s->log);
		s->log = 0;
	}
}

/*--------------------------------------
	lfvResolveName
--------------------------------------*/
const char* lfvResolveName(const lfv_reader_state* s)
{
	return s->name ? s->name : (s->f ? "file" : "string");
}

/*--------------------------------------
	ReaderNoSetJmp

Don't call this directly, call lfvReader.
--------------------------------------*/
static char* ReaderNoSetJmp(void* data, size_t* size)
{
	lfv_reader_state* s = (lfv_reader_state*)data;
	*size = 0;

	if(!s->buf || s->topResult == EXPAND_ERR)
		return 0;

	/* Flush characters sent out last call */
	if(s->tok && s->numBuf > s->tok)
	{
		size_t i;

		for(i = s->tok; i < s->numBuf; i++)
			s->buf[i - s->tok] = s->buf[i];
	}

	s->numBuf -= s->tok;
	s->buf[s->numBuf] = 0;
	s->tok = 0;

	if(!s->streamThruBuf)
		while(ReadMore(s)); /* Not streaming; read everything into buffer */

	if(s->topResult >= EXPAND_INIT /* includes EXPAND_INIT_FORCE */ )
	{
		/* First call to reader */
		if(s->skipBOMAndPound)
			SkipBOMAndPound(s);

		/* Look for expansion request */
		NextTokenSkipCom(s);
		ExtendToken(s, IDENTIFIER_CHARS "()");

		if(TokenStartsWith(s, "LFV_EXPAND_VECTORS()"))
		{
			size_t i;
			ExtendTokenSize(s, 20);

			for(i = s->tok; i < s->tok + s->tokSize; i++)
				s->buf[i] = ' '; /* Clear token so fake function isn't actually called */

			s->topResult = EXPAND_OK;
			NextTokenSkipCom(s);
		}
		else
			s->topResult = s->topResult == EXPAND_INIT_FORCE ? EXPAND_OK : EXPAND_OFF;

		if(s->logPath)
		{
			s->log = fopen(s->logPath, "a");

			if(s->log)
			{
				if(s->topResult == EXPAND_OK)
					fprintf(s->log, LOG_PREFIX "vector expansion of %s\n", lfvResolveName(s));
				else if(s->name)
				{
					fprintf(s->log, LOG_PREFIX "not expanding %s\n", s->name);
					fclose(s->log);
					s->log = 0;
				}
			}
		}
	}

	if(s->topResult == EXPAND_OK)
	{
		while(1)
		{
			/* Start/continue main block; like ExpandBlock but pause so we can return back to
			caller after each stat or retstat */
			size_t saveTok = s->tok;
			unsigned line = s->line;
			int statRes = ExpandStat(s);

			if(statRes == EXPAND_UNFIT)
			{
				int res;
				line = s->line;
				res = ExpandRetstat(s);

				if(res == EXPAND_UNFIT)
				{
					ExtendTokenSize(s, 1);

					if(s->buf[s->tok])
					{
						SetReaderError(s, "Main block contains tokens that do not form a stat "
							"or retstat", line);
						s->topResult = EXPAND_ERR;
					}
				}
				else if(res == EXPAND_ERR)
				{
					SetReaderError(s, "Bad retstat in main block", line);
					s->topResult = EXPAND_ERR;
				}
			}
			else if(statRes == EXPAND_ERR)
			{
				SetReaderError(s, "Bad stat in main block", line);
				s->topResult = EXPAND_ERR;
			}

			*size = s->tok;

			if(s->topResult != EXPAND_OK || s->streamThruBuf || s->tok == saveTok)
				break;
		}
	}
	else if(s->topResult == EXPAND_OFF)
	{
		/* Not doing expansion anymore */
		if(s->streamThruBuf)
		{
			if(s->tok)
				*size = s->tok; /* First iteration: output everything up to token */
			else
			{
				/* Second iteration: output anything leftover in buffer */
				/* Subsequent iterations: read and output as much as buf can handle */
				if(!s->numBuf)
					ReadMore(s);

				*size = s->numBuf;
				s->numBuf = 0; /* Pretend we flushed for next call */
			}
		}
		else
			*size = s->numBuf;
	}

	if(s->log)
	{
		fwrite(s->buf, 1, *size, s->log); /* Log preprocessed result */

		if(s->earliestError)
		{
			fprintf(s->log, "\n" LOG_PREFIX "expansion error ('%s' ln %u): %s",
				lfvResolveName(s), s->errorLine, s->earliestError);
		}
	}

	if(!s->streamThruBuf)
		s->buf[*size] = 0;

	return s->buf;
}

/*--------------------------------------
	SetReaderError

str must be constant. Always returns EXPAND_ERR.
--------------------------------------*/
static int SetReaderError(lfv_reader_state* s, const char* str, unsigned line)
{
	if(!s->earliestError)
	{
		s->earliestError = str;
		s->errorLine = line;
	}

	return EXPAND_ERR;
}

/*--------------------------------------
	ExpandBlock
--------------------------------------*/
static int ExpandBlock(lfv_reader_state* s)
{
	unsigned line = s->line;
	
	while(1)
	{
		int res = ExpandStat(s);

		if(res == EXPAND_UNFIT)
			break;
		else if(res == EXPAND_ERR)
			RETURN_ERROR("Bad stat in block");
	}

	if(ExpandRetstat(s) == EXPAND_ERR)
		RETURN_ERROR("Bad retstat in block");

	return EXPAND_OK;
}

/*--------------------------------------
	ExpandStat
--------------------------------------*/
static int ExpandStat(lfv_reader_state* s)
{
	int res;
	unsigned line = s->line;
	ExtendTokenSize(s, 1);

	if(s->buf[s->tok] == ';')
	{
		/* Empty statement */
		s->tokSize = 1;
		NextTokenSkipCom(s);
		return EXPAND_OK;
	}

	if(s->buf[s->tok] == ':')
	{
		if(ExpandLabel(s) != EXPAND_OK)
			RETURN_ERROR("Expected label at ':'");

		return EXPAND_OK;
	}

	ExtendToken(s, IDENTIFIER_CHARS);

	if(EqualToken(s, "break"))
	{
		NextTokenSkipCom(s);
		return EXPAND_OK;
	}

	if(EqualToken(s, "goto"))
	{
		NextTokenSkipCom(s);

		if(ExpandName(s, FALSE) != EXPAND_OK)
			RETURN_ERROR("Expected Name after 'goto'");

		return EXPAND_OK;
	}

	if(EqualToken(s, "do"))
	{
		NextTokenSkipCom(s);

		if(ExpandBlock(s) != EXPAND_OK)
			RETURN_ERROR("Expected block after 'do'");

		ExtendToken(s, IDENTIFIER_CHARS);

		if(!EqualToken(s, "end"))
			RETURN_ERROR("Expected 'end' after 'do block'");

		NextTokenSkipCom(s);
		return EXPAND_OK;
	}

	if(EqualToken(s, "while"))
	{
		NextTokenSkipCom(s);

		if(ExpandExp(s, 0) != EXPAND_OK)
			RETURN_ERROR("Expected exp after 'while'");

		ExtendToken(s, IDENTIFIER_CHARS);
		
		if(!EqualToken(s, "do"))
			RETURN_ERROR("Expected 'do' after 'while exp'");

		NextTokenSkipCom(s);

		if(ExpandBlock(s) != EXPAND_OK)
			RETURN_ERROR("Expected block after 'while exp do'");

		ExtendToken(s, IDENTIFIER_CHARS);

		if(!EqualToken(s, "end"))
			RETURN_ERROR("Expected 'end' after 'while exp do block'");

		NextTokenSkipCom(s);
		return EXPAND_OK;
	}

	if(EqualToken(s, "repeat"))
	{
		NextTokenSkipCom(s);

		if(ExpandBlock(s) != EXPAND_OK)
			RETURN_ERROR("Expected block after 'repeat'");

		ExtendToken(s, IDENTIFIER_CHARS);

		if(!EqualToken(s, "until"))
			RETURN_ERROR("Expected 'until' after 'repeat block'");

		NextTokenSkipCom(s);

		if(ExpandExp(s, 0) != EXPAND_OK)
			RETURN_ERROR("Expected exp after 'repeat block until'");

		return EXPAND_OK;
	}

	if(EqualToken(s, "if"))
	{
		NextTokenSkipCom(s);

		if(ExpandExp(s, 0) != EXPAND_OK)
			RETURN_ERROR("Expected exp after 'if'");

		ExtendToken(s, IDENTIFIER_CHARS);

		if(!EqualToken(s, "then"))
			RETURN_ERROR("Expected 'then' after 'if exp'");

		NextTokenSkipCom(s);

		if(ExpandBlock(s) != EXPAND_OK)
			RETURN_ERROR("Expected block after 'if exp then'");

		ExtendToken(s, IDENTIFIER_CHARS);

		while(EqualToken(s, "elseif"))
		{
			NextTokenSkipCom(s);

			if(ExpandExp(s, 0) != EXPAND_OK)
				RETURN_ERROR("Expected exp after 'elseif'");

			ExtendToken(s, IDENTIFIER_CHARS);

			if(!EqualToken(s, "then"))
				RETURN_ERROR("Expected 'then' after 'elseif exp'");

			NextTokenSkipCom(s);

			if(ExpandBlock(s) != EXPAND_OK)
				RETURN_ERROR("Expected block after 'elseif exp then'");

			ExtendToken(s, IDENTIFIER_CHARS);
		}

		if(EqualToken(s, "else"))
		{
			NextTokenSkipCom(s);

			if(ExpandBlock(s) != EXPAND_OK)
				RETURN_ERROR("Expected block after 'else'");

			ExtendToken(s, IDENTIFIER_CHARS);
		}

		if(!EqualToken(s, "end"))
		{
			RETURN_ERROR("Expected 'end' after 'if exp then block {elseif exp then block} "
				"[else block]'");
			return EXPAND_ERR;
		}

		NextTokenSkipCom(s);
		return EXPAND_OK;
	}

	if(EqualToken(s, "for"))
	{
		NextTokenSkipCom(s);

		if(ExpandExplist(s) != EXPAND_OK)
			RETURN_ERROR("Expected explist after 'for'");

		ExtendTokenSize(s, 1);

		if(s->buf[s->tok] != '=')
		{
			ExtendToken(s, IDENTIFIER_CHARS);

			if(!EqualToken(s, "in"))
				RETURN_ERROR("Expected '=' or 'in' after 'for explist'");
		}

		NextTokenSkipCom(s);

		if(ExpandExplist(s) != EXPAND_OK)
			RETURN_ERROR("Expected explist after 'for explist =|in'");

		ExtendToken(s, IDENTIFIER_CHARS);

		if(!EqualToken(s, "do"))
			RETURN_ERROR("Expected 'do' after 'for explist =|in explist'");

		NextTokenSkipCom(s);

		if(ExpandBlock(s) != EXPAND_OK)
			RETURN_ERROR("Expected block after 'for explist =|in explist do'");

		ExtendToken(s, IDENTIFIER_CHARS);

		if(!EqualToken(s, "end"))
			RETURN_ERROR("Expected 'end' after 'for explist =|in explist do block'");

		NextTokenSkipCom(s);
		return EXPAND_OK;
	}

	if(EqualToken(s, "function"))
	{
		NextTokenSkipCom(s);

		if(ExpandFuncname(s) != EXPAND_OK)
			RETURN_ERROR("Expected funcname after 'function'");

		if(ExpandFuncbody(s) != EXPAND_OK)
			RETURN_ERROR("Expected funcbody after 'function funcname'");

		return EXPAND_OK;
	}

	if(EqualToken(s, "local"))
	{
		NextTokenSkipCom(s);
		ExtendToken(s, IDENTIFIER_CHARS);

		if(EqualToken(s, "function"))
		{
			NextTokenSkipCom(s);

			if(ExpandName(s, FALSE) != EXPAND_OK)
				RETURN_ERROR("Expected Name after 'local function'");

			if(ExpandFuncbody(s) != EXPAND_OK)
				RETURN_ERROR("Expected funcbody after 'local function Name'");
		}
		else
		{
			if(ExpandAttnamelist(s) != EXPAND_OK)
				RETURN_ERROR("Expected 'function' or attnamelist after 'local'");

			if(ExtendToken(s, "=") == 1)
			{
				NextTokenSkipCom(s);

				if(ExpandExplist(s) != EXPAND_OK)
					RETURN_ERROR("Expected explist after 'local attnamelist ='");
			}
		}

		return EXPAND_OK;
	}

	/*
	Do explist ['=' explist] to handle:
		varlist '=' explist
		functioncall
	*/
	res = ExpandExplist(s);

	if(res == EXPAND_OK)
	{
		ExtendTokenSize(s, 1);

		if(s->buf[s->tok] == '=')
		{
			NextTokenSkipCom(s);

			if(ExpandExplist(s) != EXPAND_OK)
				RETURN_ERROR("Expected explist after 'explist ='");
		}

		return EXPAND_OK;
	}
	else if(res == EXPAND_ERR)
		RETURN_ERROR("Bad explist at start of stat");

	return EXPAND_UNFIT;
}

/*--------------------------------------
	ExpandAttnamelist
--------------------------------------*/
static int ExpandAttnamelist(lfv_reader_state* s)
{
	unsigned line = s->line;
	size_t numNames = 0;

	while(1)
	{
		int res = ExpandAttname(s);

		if(res != EXPAND_OK)
		{
			if(res == EXPAND_ERR)
				RETURN_ERROR("Bad attname in attnamelist");
			else if(numNames)
				RETURN_ERROR("attnamelist expected attname after ','");
			else
				return EXPAND_UNFIT;
		}

		numNames++;

		if(ExtendToken(s, ",") == 1)
			NextTokenSkipCom(s);
		else
			break;
	}

	return EXPAND_OK;
}

/*--------------------------------------
	ExpandAttname
--------------------------------------*/
static int ExpandAttname(lfv_reader_state* s)
{
	unsigned line = s->line;
	const char* err;
	size_t start = s->tok;
	size_t saveNumMarks = s->numMarks;
	int res = ExpandName(s, TRUE);

	if(res != EXPAND_OK)
		return res;

	if(ExpandAttrib(s) == EXPAND_ERR)
		RETURN_ERROR("Bad attrib in attname");

	if(err = DuplicateVecs(s, start, saveNumMarks, 0))
		RETURN_ERROR(err);

	return EXPAND_OK;
}

/*--------------------------------------
	ExpandAttrib
--------------------------------------*/
static int ExpandAttrib(lfv_reader_state* s)
{
	unsigned line = s->line;
	ExtendTokenSize(s, 1);
	
	if(s->buf[s->tok] != '<')
		return EXPAND_UNFIT;

	NextTokenSkipCom(s);

	if(ExpandName(s, FALSE) != EXPAND_OK)
		RETURN_ERROR("attrib expected Name after '<'");

	if(ExtendToken(s, ">") != 1)
		RETURN_ERROR("attrib expected '>' after '<Name'");

	NextTokenSkipCom(s);
	return EXPAND_OK;
}

/*--------------------------------------
	ExpandRetstat
--------------------------------------*/
static int ExpandRetstat(lfv_reader_state* s)
{
	unsigned line = s->line;
	ExtendToken(s, IDENTIFIER_CHARS);

	if(!EqualToken(s, "return"))
		return EXPAND_UNFIT;

	NextTokenSkipCom(s);

	if(ExpandExplist(s) == EXPAND_ERR)
		RETURN_ERROR("Bad explist after 'return'");

	if(s->buf[s->tok] == ';')
		NextTokenSkipCom(s);

	return EXPAND_OK;
}

/*--------------------------------------
	ExpandLabel
--------------------------------------*/
static int ExpandLabel(lfv_reader_state* s)
{
	unsigned line = s->line;
	ExtendToken(s, ":");

	if(s->tokSize != 2)
		return EXPAND_UNFIT;

	NextTokenSkipCom(s);

	if(ExpandName(s, FALSE) != EXPAND_OK)
		RETURN_ERROR("label expected Name after '::'");

	ExtendToken(s, ":");

	if(s->tokSize != 2)
		RETURN_ERROR("label expected '::' after '::Name'");

	NextTokenSkipCom(s);
	return EXPAND_OK;
}

/*--------------------------------------
	ExpandName

If checkVector is true, adds the char index of a vector prefix ("v2", "v3" or "q4") to s->marks.
s->marks may be realloc'd.
--------------------------------------*/
static int ExpandName(lfv_reader_state* s, int checkVector)
{
	ExtendTokenSize(s, 1);

	if(StrChrNull("0123456789", s->buf[s->tok]))
		return EXPAND_UNFIT;

	ExtendToken(s, IDENTIFIER_CHARS);

	if(!s->tokSize)
		return EXPAND_UNFIT;

	if(
		EqualToken(s, "and") ||
		EqualToken(s, "break") ||
		EqualToken(s, "do") ||
		EqualToken(s, "else") ||
		EqualToken(s, "elseif") ||
		EqualToken(s, "end") ||
		EqualToken(s, "false") ||
		EqualToken(s, "for") ||
		EqualToken(s, "function") ||
		EqualToken(s, "goto") ||
		EqualToken(s, "if") ||
		EqualToken(s, "in") ||
		EqualToken(s, "local") ||
		EqualToken(s, "nil") ||
		EqualToken(s, "not") ||
		EqualToken(s, "or") ||
		EqualToken(s, "repeat") ||
		EqualToken(s, "return") ||
		EqualToken(s, "then") ||
		EqualToken(s, "true") ||
		EqualToken(s, "until") ||
		EqualToken(s, "while")
	)
		return EXPAND_UNFIT;

	if(checkVector && s->tokSize >= 2)
	{
		int add = FALSE;
		const char* c = &s->buf[s->tok];

		if(c[0] == 'v')
		{
			if(c[1] == '2' || c[1] == '3')
				add = TRUE;
		}
		else if(c[0] == 'q' && c[1] == '4')
			add = TRUE;

		if(add)
			AddMark(s, s->tok);
	}

	NextTokenSkipCom(s);
	return EXPAND_OK;
}

/*--------------------------------------
	ExpandNumeral
--------------------------------------*/
static int ExpandNumeral(lfv_reader_state* s)
{
	unsigned line = s->line;
	char *tok;
	size_t cur;
	int hex = FALSE;

	/* Needs to start with a digit */
	if(!StrChrNull("0123456789", s->buf[s->tok]))
		return EXPAND_UNFIT;

	/* Get potential numeral chars */
	ExtendToken(s, NUMERAL_CHARS);
	tok = s->buf + s->tok;
	cur = 0;

	/* Parse numeral to truncate */
	if(tok[0] == '0' && tolower(tok[1]) == 'x')
	{
		/* Hexadecimal */
		size_t digits = 0;
		hex = TRUE;
		cur += 2;

		/* Whole part */
		for(; StrChrNull("0123456789abcdef", tolower(tok[cur])); cur++, digits++);

		/* Fractional part */
		if(tok[cur] == '.')
			for(cur++; StrChrNull("0123456789abcdef", tolower(tok[cur])); cur++, digits++);

		if(!digits)
			RETURN_ERROR("Hex numeral must have a digit");
	}
	else
	{
		/* Decimal */
		/* Whole part */
		for(; StrChrNull("0123456789", tok[cur]); cur++);

		/* Fractional part */
		if(tok[cur] == '.')
			for(cur++; StrChrNull("0123456789", tok[cur]); cur++);
	}

	/* Exponent */
	if(tolower(tok[cur]) == (hex ? 'p' : 'e'))
	{
		size_t save;
		cur++;

		if(tok[cur] == '+' || tok[cur] == '-')
			cur++;

		save = cur;
		for(; StrChrNull("0123456789", tok[cur]); cur++);

		if(cur == save)
			RETURN_ERROR("Exponent of numeral must have a digit");
	}

	s->tokSize = cur;
	NextTokenSkipCom(s);
	return EXPAND_OK;
}

/*--------------------------------------
	ExpandString
--------------------------------------*/
static int ExpandString(lfv_reader_state* s)
{
	unsigned line = s->line;
	char openQuote;
	ExtendTokenSize(s, 1);
	openQuote = s->buf[s->tok];

	if(openQuote == '"' || openQuote == '\'')
	{
		while(1)
		{
			NextTokenNoSkip(s, 0);
			ExtendCToken(s, "'\"\n");
			NextTokenNoSkip(s, 0);

			if(StrChrNull("'\"", s->buf[s->tok]))
			{
				if(s->buf[s->tok] != openQuote || (s->tok && s->buf[s->tok - 1] == '\\'))
					continue; /* Not matching or escaped */

				NextTokenSkipCom(s);
				return EXPAND_OK;
			}
			else
				RETURN_ERROR("Unclosed short string literal");
		}
	}
	else if(SkipLongBracket(s))
	{
		s->tokSize = 0; /* SkipLongBracket may have ended on a comment, hack past it */
		NextTokenSkipCom(s);
		return EXPAND_OK;
	}

	return EXPAND_UNFIT;
}

/*--------------------------------------
	ExpandFuncname
--------------------------------------*/
static int ExpandFuncname(lfv_reader_state* s)
{
	unsigned line = s->line;
	int res = ExpandName(s, FALSE);

	if(res != EXPAND_OK)
		return res;

	while(s->buf[s->tok] == '.')
	{
		NextTokenSkipCom(s);

		if(ExpandName(s, FALSE) != EXPAND_OK)
			RETURN_ERROR("funcname expected Name after 'Name.'");
	}

	if(s->buf[s->tok] == ':')
	{
		NextTokenSkipCom(s);

		if(ExpandName(s, FALSE) != EXPAND_OK)
			RETURN_ERROR("funcname expected Name after ':'");
	}

	return EXPAND_OK;
}

/*--------------------------------------
	ExpandExplist
--------------------------------------*/
static int ExpandExplist(lfv_reader_state* s)
{
	unsigned line = s->line;
	int res = ExpandExp(s, 0);

	if(res != EXPAND_OK)
		return res;

	ExtendTokenSize(s, 1);

	while(s->buf[s->tok] == ',')
	{
		NextTokenSkipCom(s);

		if(ExpandExp(s, 0) != EXPAND_OK)
			RETURN_ERROR("explist expected exp after ','");
	}

	return EXPAND_OK;
}

/*--------------------------------------
	ExpandExp

If dd is given, vector Names are left marked and dd's members are filled. Otherwise,
DuplicateVecs is called for the marks found.
--------------------------------------*/
#define EXP_ERROR(str) \
{ \
	s->numMarks = saveNumMarks; \
	DecRecursionLevel(s); \
	return SetReaderError(s, str, line); \
}

static int ExpandExp(lfv_reader_state* s, delayed_duplication* dd)
{
	unsigned line = s->line;
	const char* err;
	size_t start = s->tok;
	int hang = TRUE; /* Next token should be a ref or value (true on start and after
					 operator) */
	int ref = FALSE; /* Last token completed a potential object reference
					 (callable/accessible) */
	int par = 0; /* Parenthesis level, expression ends if below 0 */
	size_t saveNumMarks = s->numMarks;
	IncRecursionLevel(s);

	while(1)
	{
		int unop;

		if(!hang)
		{
			/* Try binop */
			int binop = ExpandBinop(s);

			if(binop == EXPAND_OK)
			{
				hang = TRUE;
				ref = FALSE;
				continue;
			}
			else if(binop == EXPAND_ERR)
				EXP_ERROR("Bad binop in exp");
		}

		/* Try unop */
		unop = ExpandUnop(s);

		if(unop == EXPAND_OK)
		{
			hang = TRUE;
			ref = FALSE;
			continue;
		}
		else if(unop == EXPAND_ERR)
			EXP_ERROR("Bad unop in exp");

		if(hang)
		{
			int res;

			/* Try reserved values */
			ExtendToken(s, IDENTIFIER_CHARS);

			if(EqualToken(s, "nil") || EqualToken(s, "false") || EqualToken(s, "true"))
			{
				NextTokenSkipCom(s);
				hang = FALSE;
				ref = FALSE;
				continue;
			}

			/* Try literals */
			res = ExpandString(s);

			if(res == EXPAND_OK)
			{
				hang = FALSE;
				ref = FALSE;
				continue;
			}
			else if(res == EXPAND_ERR)
				EXP_ERROR("Bad LiteralString in exp");

			res = ExpandNumeral(s);

			if(res == EXPAND_OK)
			{
				hang = FALSE;
				ref = FALSE;
				continue;
			}
			else if(res == EXPAND_ERR)
				EXP_ERROR("Bad Numeral in exp");

			/* Try ... */
			ExtendToken(s, ".");

			if(s->tokSize == 3)
			{
				hang = FALSE;
				ref = FALSE;
				NextTokenSkipCom(s);
				continue;
			}

			/* Try functiondef */
			res = ExpandFunctiondef(s);

			if(res == EXPAND_OK)
			{
				hang = FALSE;
				ref = TRUE;
				continue;
			}
			else if(res == EXPAND_ERR)
				EXP_ERROR("Bad functiondef in exp");

			/* Try tableconstructor */
			res = ExpandTableconstructor(s);

			if(res == EXPAND_OK)
			{
				hang = FALSE;
				ref = FALSE; /* Tables can't be called or accessed immediately */
				continue;
			}
			else if(res == EXPAND_ERR)
				EXP_ERROR("Bad tableconstructor in exp");

			/* Try to start a prefixexp, either a Name or a '(' */
			ExtendTokenSize(s, 1);

			if(s->buf[s->tok] == '(')
			{
				NextTokenSkipCom(s);
				par++;
				hang = TRUE;
				ref = FALSE;
				continue;
			}

			res = ExpandName(s, TRUE);

			if(res == EXPAND_OK)
			{
				hang = FALSE;
				ref = TRUE;
				continue;
			}
			else if(res == EXPAND_ERR)
				EXP_ERROR("Bad Name in exp");
		}

		if(ref)
		{
			/* Some sort of object/prefixexp set up */
			/* Check for function call */
			ExtendToken(s, ":");

			if(s->tokSize == 1)
			{
				NextTokenSkipCom(s);

				if(ExpandName(s, FALSE) != EXPAND_OK)
					EXP_ERROR("Expected Name after ':' in exp functioncall");

				if(ExpandArgs(s) != EXPAND_OK)
					EXP_ERROR("Expected args after ':Name' in exp functioncall");

				hang = FALSE;
				ref = TRUE;
				continue;
			}
			else
			{
				int res = ExpandArgs(s);

				if(res == EXPAND_OK)
				{
					hang = FALSE;
					ref = TRUE;
					continue;
				}
				else if(res == EXPAND_ERR)
					EXP_ERROR("Bad args in exp functioncall");
			}

			/* Check for access */
			ExtendTokenSize(s, 1);

			if(s->buf[s->tok] == '[')
			{
				NextTokenSkipCom(s);

				if(ExpandExp(s, 0) != EXPAND_OK)
					EXP_ERROR("Expected exp after '[' in exp var");

				if(s->buf[s->tok] != ']')
					EXP_ERROR("Expected ']' after '[explist' in exp var");

				NextTokenSkipCom(s);
				hang = FALSE;
				ref = TRUE;
				continue;
			}

			if(s->buf[s->tok] == '.')
			{
				NextTokenSkipCom(s);

				if(ExpandName(s, TRUE) != EXPAND_OK)
					EXP_ERROR("Expected Name after '.' in exp var");

				hang = FALSE;
				ref = TRUE;
				continue;
			}
		}

		/* Closing parenthesis */
		ExtendTokenSize(s, 1);

		if(s->buf[s->tok] == ')')
		{
			if(--par < 0)
				break;
			else
			{
				NextTokenSkipCom(s);
				continue;
			}
		}

		/* No recognizable unit or went out of parenthesis scope */
		break;
	}

	if(par > 0)
		EXP_ERROR("exp has unclosed parenthesis");

	if(start != s->tok && hang)
		EXP_ERROR("exp has hanging operator");

	if(dd)
	{
		dd->expStart = start;
		dd->marksStart = saveNumMarks;
	}
	else if(err = DuplicateVecs(s, start, saveNumMarks, 0))
		EXP_ERROR(err);

	DecRecursionLevel(s);
	return start == s->tok ? EXPAND_UNFIT : EXPAND_OK;
}

/*--------------------------------------
	ExpandArgs
--------------------------------------*/
static int ExpandArgs(lfv_reader_state* s)
{
	unsigned line = s->line;
	int res;
	ExtendTokenSize(s, 1);

	if(s->buf[s->tok] == '(')
	{
		NextTokenSkipCom(s);

		if(ExpandExplist(s) == EXPAND_ERR)
			RETURN_ERROR("Bad explist after '(' in args");

		if(s->buf[s->tok] != ')')
			RETURN_ERROR("Expected ')' after '(explist' in args");

		NextTokenSkipCom(s);
		return EXPAND_OK;
	}

	res = ExpandTableconstructor(s);

	if(res == EXPAND_OK)
		return EXPAND_OK;
	else if(res == EXPAND_ERR)
		RETURN_ERROR("Bad tableconstructor in args");

	res = ExpandString(s);

	if(res == EXPAND_OK)
		return EXPAND_OK;
	else if(res == EXPAND_ERR)
		RETURN_ERROR("Bad LiteralString in args");

	return EXPAND_UNFIT;
}

/*--------------------------------------
	ExpandFunctiondef
--------------------------------------*/
static int ExpandFunctiondef(lfv_reader_state* s)
{
	unsigned line = s->line;
	ExtendToken(s, IDENTIFIER_CHARS);

	if(!EqualToken(s, "function"))
		return EXPAND_UNFIT;

	NextTokenSkipCom(s);

	if(ExpandFuncbody(s) != EXPAND_OK)
		RETURN_ERROR("functiondef expected funcbody after 'function'");

	return EXPAND_OK;
}

/*--------------------------------------
	ExpandFuncbody
--------------------------------------*/
static int ExpandFuncbody(lfv_reader_state* s)
{
	unsigned line = s->line;
	ExtendTokenSize(s, 1);

	if(s->buf[s->tok] != '(')
		return EXPAND_UNFIT;

	NextTokenSkipCom(s);

	if(ExpandExplist(s) == EXPAND_ERR)
		RETURN_ERROR("Bad explist in funcbody after '('");

	ExtendTokenSize(s, 1);

	if(s->buf[s->tok] != ')')
		RETURN_ERROR("funcbody expected ')' after '(explist'");

	NextTokenSkipCom(s);

	if(ExpandBlock(s) != EXPAND_OK)
		RETURN_ERROR("funcbody expected block after '(explist)'");

	ExtendToken(s, IDENTIFIER_CHARS);

	if(!EqualToken(s, "end"))
		RETURN_ERROR("funcbody expected 'end' after '(explist) block'");

	NextTokenSkipCom(s);
	return EXPAND_OK;
}

/*--------------------------------------
	ExpandTableconstructor
--------------------------------------*/
static int ExpandTableconstructor(lfv_reader_state* s)
{
	unsigned line = s->line;
	ExtendTokenSize(s, 1);

	if(s->buf[s->tok] != '{')
		return EXPAND_UNFIT;

	NextTokenSkipCom(s);

	if(ExpandFieldlist(s) == EXPAND_ERR)
		RETURN_ERROR("Bad fieldlist in tableconstructor");

	ExtendTokenSize(s, 1);

	if(s->buf[s->tok] != '}')
		RETURN_ERROR("tableconstructor expected '}' after '{fieldlist'");

	NextTokenSkipCom(s);
	return EXPAND_OK;
}

/*--------------------------------------
	ExpandFieldlist
--------------------------------------*/
static int ExpandFieldlist(lfv_reader_state* s)
{
	size_t marksStart = 0;
	size_t prepComps = 0;
	size_t remExps = 0;
	size_t mergeableEnd = 0;
	int prepLine = s->line;
	const char* err;

	while(1)
	{
		/* Get field */
		unsigned line = s->line;
		size_t saveNumMarks = s->numMarks;
		size_t markedVecComps = 0, markedExps = 0;
		int ret = ExpandField(s, &markedVecComps, &markedExps);

		if(ret == EXPAND_ERR)
			RETURN_ERROR("Bad field in fieldlist");
		else if(ret == EXPAND_UNFIT)
			break;

		/* Check state of prep */
		if(markedVecComps)
		{
			/* ExpandField marked a left-hand vector */
			if(prepComps)
			{
				/* We're still prepping the last vector; cancel further prep and merge now */
				if(err = MergeFields(s, marksStart, prepComps - remExps, mergeableEnd))
					RETURN_ERROR(err);
			}

			/* Start prep */
			marksStart = s->numMarks - markedExps - 1;
			prepComps = markedVecComps;
			remExps = prepComps - 1;
			mergeableEnd = s->beforeSkip;
			prepLine = line;
		}

		if(prepComps)
		{
			/* Prepping a vector */
			if(markedExps)
			{
				/* ExpandField marked mergeable expression(s) */
				if(markedExps > remExps)
					RETURN_ERROR("exp duplicates too many times for left-hand vector in fieldlist");

				remExps -= markedExps;
				mergeableEnd = s->beforeSkip;

				if(!remExps)
				{
					/* Prep is finished; merge */
					if(err = MergeFields(s, marksStart, prepComps, mergeableEnd))
						RETURN_ERROR(err);

					prepComps = 0;
				}
			}
			else if(!markedVecComps)
			{
				/* We're prepping and ExpandField did not mark anything; cancel further prep and
				merge now */
				if(err = MergeFields(s, marksStart, prepComps - remExps, mergeableEnd))
					RETURN_ERROR(err);

				prepComps = 0;
			}
		}
		else
			s->numMarks = saveNumMarks; /* Not prepping; discard mergeable expression marks */

		/* Get fieldsep */
		ret = ExpandFieldsep(s);

		if(ret == EXPAND_ERR)
			RETURN_ERROR("Bad fieldsep in fieldlist");
		else if(ret == EXPAND_UNFIT)
			break;
	}

	if(prepComps)
	{
		/* fieldlist is done but we're still prepping; merge now */
		if(err = MergeFields(s, marksStart, prepComps - remExps, mergeableEnd))
			return SetReaderError(s, err, s->line);

		prepComps = 0;
	}

	return EXPAND_OK;
}

/*--------------------------------------
	ExpandField

If markedVecComps is given, the field is 'exp = exp', and the left hand side contains a vector
Name, the Name is marked and *markedVecComps is set to the number of components.

If markedExps is given and the field is 'exp', the beginning of the expression and each of its
duplicates are marked and *markedExps is set to the number of added marks.

If markedExps is given and the field is 'exp = exp', only the duplicates of the right hand side
are marked, not the left expression nor the original right expression.
--------------------------------------*/
static int ExpandField(lfv_reader_state* s, size_t* markedVecComps, size_t* markedExps)
{
	unsigned line = s->line;
	int ret;
	const char* err;
	delayed_duplication dd;
	ExtendTokenSize(s, 1);

	if(s->buf[s->tok] == '[')
	{
		/* Do '[' exp ']' '=' exp */
		NextTokenSkipCom(s);

		if(ExpandExp(s, 0) != EXPAND_OK)
			RETURN_ERROR("field expected exp after '['");

		ExtendTokenSize(s, 1);

		if(s->buf[s->tok] != ']')
			RETURN_ERROR("field expected ']' after '[exp'");

		NextTokenSkipCom(s);
		ExtendTokenSize(s, 1);

		if(s->buf[s->tok] != '=')
			RETURN_ERROR("field expected '=' after '[exp]'");

		NextTokenSkipCom(s);

		if(ExpandExp(s, 0) != EXPAND_OK)
			RETURN_ERROR("field expected exp after '[exp] ='");

		return EXPAND_OK;
	}

	/*
	Do exp ['=' exp] to handle:
		Name '=' exp
		exp
	*/
	ret = ExpandExp(s, &dd);

	if(ret == EXPAND_ERR)
		RETURN_ERROR("Bad exp in field");
	else if(ret == EXPAND_UNFIT)
		return EXPAND_UNFIT;

	ExtendTokenSize(s, 1);

	if(s->buf[s->tok] == '=')
	{
		size_t numNameVecs = s->numMarks - dd.marksStart;

		if(numNameVecs > 1)
			RETURN_ERROR("field expected 'exp =' to contain no more than one vector Name");

		if(markedVecComps)
		{
			if(numNameVecs && (err = CheckMarkPrefix(s, s->numMarks - 1, markedVecComps)))
				RETURN_ERROR(err);
		}
		else
			s->numMarks = dd.marksStart; /* Caller isn't tracking; discard the mark */

		NextTokenSkipCom(s);

		if(ExpandExp(s, &dd) != EXPAND_OK)
			RETURN_ERROR("field expected exp after 'exp ='");

		if(err = DuplicateVecs(s, dd.expStart, dd.marksStart, markedExps))
			RETURN_ERROR(err);

		if(markedExps && *markedExps)
		{
			/* Only keep marks to duplicates */
			RemoveMarks(s, s->numMarks - *markedExps, 1);
			(*markedExps)--;
		}

		return EXPAND_OK;
	}

	/* exp */
	if(err = DuplicateVecs(s, dd.expStart, dd.marksStart, markedExps))
		RETURN_ERROR(err);

	return EXPAND_OK;
}

/*--------------------------------------
	ExpandFieldsep
--------------------------------------*/
static int ExpandFieldsep(lfv_reader_state* s)
{
	ExtendTokenSize(s, 1);

	if(StrChrNull(",;", s->buf[s->tok]))
	{
		NextTokenSkipCom(s);
		return EXPAND_OK;
	}

	return EXPAND_UNFIT;
}

/*--------------------------------------
	ExpandBinop
--------------------------------------*/
static int ExpandBinop(lfv_reader_state* s)
{
	ExtendTokenSize(s, 1);

	if(!s->tokSize)
		return EXPAND_UNFIT;

	switch(s->buf[s->tok])
	{
		case '+': case '-': case '*': case '^': case '%': case '&': case '|':
		{
			/* Single-char binops */
			NextTokenSkipCom(s);
			return EXPAND_OK;
		}
		case '/':
		{
			if(ExtendToken(s, "/") >= 2)
				s->tokSize = 2;

			NextTokenSkipCom(s);
			return EXPAND_OK;
		}
		case '>':
		{
			if(ExtendToken(s, ">=") > 2)
				s->tokSize = 2;

			NextTokenSkipCom(s);
			return EXPAND_OK;
		}
		case '<':
		{
			if(ExtendToken(s, "<=") > 2)
				s->tokSize = 2;

			NextTokenSkipCom(s);
			return EXPAND_OK;
		}
		case '.':
		{
			if(ExtendToken(s, ".") != 2)
				return EXPAND_UNFIT;

			NextTokenSkipCom(s);
			return EXPAND_OK;
		}
		case '~':
		{
			if(ExtendToken(s, "~=") >= 2 && s->buf[s->tok + 1] == '=')
				s->tokSize = 2;
			else
				s->tokSize = 1;

			NextTokenSkipCom(s);
			return EXPAND_OK;
		}
		case '=':
		{
			if(ExtendToken(s, "=") >= 2)
			{
				s->tokSize = 2;
				NextTokenSkipCom(s);
				return EXPAND_OK;
			}
			else
				return EXPAND_UNFIT;
		}
	}

	ExtendToken(s, IDENTIFIER_CHARS);

	if(EqualToken(s, "and") || EqualToken(s, "or"))
	{
		NextTokenSkipCom(s);
		return EXPAND_OK;
	}

	return EXPAND_UNFIT;
}

/*--------------------------------------
	ExpandUnop
--------------------------------------*/
static int ExpandUnop(lfv_reader_state* s)
{
	char c = s->buf[s->tok];
	ExtendTokenSize(s, 1);

	if(!s->tokSize)
		return EXPAND_UNFIT;

	if(c == '-' || c == '#' || c == '~')
	{
		NextTokenSkipCom(s);
		return EXPAND_OK;
	}

	ExtendToken(s, IDENTIFIER_CHARS);

	if(EqualToken(s, "not"))
	{
		NextTokenSkipCom(s);
		return EXPAND_OK;
	}

	return EXPAND_UNFIT;
}

/*--------------------------------------
	DuplicateVecs

Returns 0 on success or an error string.

s->marks[marksStart .. s->numMarks - 1] must point to vector prefixes. Expression is kept as is
if no marks are given. If marksAdded is given, the expression and its duplicates are marked
(after the vector marks are consumed) and *marksAdded is set to the number of new marks.
--------------------------------------*/
static const char* DuplicateVecs(lfv_reader_state* s, size_t expStart, size_t marksStart,
	size_t* marksAdded)
{
	const char COMPS[4] = {'x', 'y', 'z', 'w'};
	size_t i;
	size_t minVec = 4;
	size_t expEnd = s->beforeSkip; /* Don't include latest comments or whitespace in exp */
	size_t expLen = expEnd - expStart;
	size_t dupStarts[4] = {expStart};

	if(s->numMarks <= marksStart)
	{
		if(marksAdded)
		{
			AddMark(s, expStart);
			*marksAdded = 1;
		}

		return 0;
	}

	/* Find minimum vector size in expression */
	for(i = marksStart; i < s->numMarks; i++)
	{
		size_t num;
		const char* err = CheckMarkPrefix(s, i, &num);

		if(err)
			return err;

		if(num < minVec)
			minVec = num;
	}

	/* Duplicate expression to make it a per-component expression list */
	/* Shift first */
	CopyShiftRight(
		s,
		expEnd,
		AddSizeT(s, MulSizeT(s, expLen, (minVec - 1)), minVec - 1), /* Duplicates + commas */
		FALSE
	);

	/* Duplicate */
	for(i = 1; i < minVec; i++)
	{
		dupStarts[i] = expStart + (expLen + 1) * i;
		s->buf[dupStarts[i] - 1] = ',';
		strncpy(&s->buf[dupStarts[i]], &s->buf[expStart], expLen);
	}

	/* Change names to be per-component */
	for(i = 0; i < minVec; i++)
	{
		size_t compStart = expStart + (expLen + 1) * i;
		size_t v;

		for(v = marksStart; v < s->numMarks; v++)
		{
			char* c = &s->buf[compStart + s->marks[v] - expStart];

			if(c[0] == 'v')
				c[0] = ' ';

			c[1] = COMPS[i];
		}
	}

	/* Remove newlines except from last duplicate to keep line number consistent */
	for(i = 0; i < minVec - 1; i++)
	{
		size_t compStart = expStart + (expLen + 1) * i;
		size_t compEnd = compStart + expLen;
		size_t j;

		for(j = compStart; j < compEnd; j++)
		{
			if(s->buf[j] == '\n' || s->buf[j] == '\r')
				s->buf[j] = ' ';
			else if(StringIsShortComment(s->buf + j))
			{
				/* Remove short comment so it doesn't comment out the subsequent duplicates */
				for(; j < compEnd; j++)
				{
					char c = s->buf[j];
					s->buf[j] = ' ';

					if(c == '\n')
						break;
				}

				if(j >= compEnd)
					break;
			}
		}
	}

	/* Update marks */
	s->numMarks = marksStart;

	if(marksAdded)
	{
		for(i = 0; i < minVec; i++)
			AddMark(s, dupStarts[i]);

		*marksAdded = minVec;
	}

	return 0;
}

/*--------------------------------------
	MergeFields

Returns 0 on success or an error string.

The first mark is the left-hand-vector's prefix.
Each subsequent mark is on each expression in sequence that is not assigned to a variable.
mergeableEnd is the end index of the last field that will be merged to this vector.

{q4Example = expression0, expression1, expression2, ["other"] = stuff}
 ^                        ^            ^          ^
 mark0                    mark1        mark2      mergeableEnd
--------------------------------------*/
static const char* MergeFields(lfv_reader_state* s, size_t marksStart, size_t numMarks,
	size_t mergeableEnd)
{
	const char COMPS[4] = {'x', 'y', 'z', 'w'};
	const char* err = 0;
	char* vecName;
	size_t vecLen;
	size_t wantComps;
	int qua;
	size_t leftAssignLen;
	size_t i;

	if(!numMarks)
		return "MergeFields called without marks";

	/* Turn first vector prefix into x component */
	vecName = s->buf + s->marks[marksStart];
	
	if(err = CheckMarkPrefix(s, marksStart, &wantComps))
		return err;

	if(wantComps > 4) /* suppress warning */
		return "MergeFields got a vector prefix with more than 4 components";

	if(numMarks > wantComps)
		return "MergeFields got too many marks for the given vector Name";

	qua = (wantComps == 4) ? 1 : 0;

	if(!qua)
		vecName[0] = ' ';

	vecName[1] = 'x';
	vecName += 2; /* Point to name without prefix */
	vecLen = strspn(vecName, IDENTIFIER_CHARS);
	leftAssignLen = AddSizeT(s, AddSizeT(s, vecLen, qua), 2);
		/* ['q'] + component + name + '=' */

	#define WRITE_VECTOR_LEFT_ASSIGNMENT \
	{ \
		if(qua) \
			s->buf[insert++] = 'q'; \
		s->buf[insert++] = COMPS[i]; \
		strncpy(s->buf + insert, vecName, vecLen); \
		insert += vecLen; \
		s->buf[insert++] = '='; \
	}

	/* Insert component assignment for each marked expression: "qyVar="... */
	for(i = 1; i < numMarks; i++)
	{
		size_t insert = s->marks[marksStart + i];

		if(insert > mergeableEnd)
			return "MergeFields got a mark past mergeableEnd";

		CopyShiftRight(s, insert, leftAssignLen, TRUE);
		mergeableEnd += leftAssignLen;
		WRITE_VECTOR_LEFT_ASSIGNMENT;
	}

	/* Insert assignment to nil for each remaining component: "qzVar=nil,qwVar=nil" */
	if(i < wantComps)
	{
		size_t insert = mergeableEnd;
		size_t remaining = wantComps - i;

		size_t neededSpace = MulSizeT(s, AddSizeT(s, leftAssignLen, 4), remaining);
			/* (',' + 'v3Var=' + 'nil') * remaining */

		CopyShiftRight(s, mergeableEnd, neededSpace, TRUE);

		for(; i < wantComps; i++)
		{
			s->buf[insert++] = ',';
			WRITE_VECTOR_LEFT_ASSIGNMENT;
			strncpy(s->buf + insert, "nil", 3);
			insert += 3;
		}
	}

	RemoveMarks(s, marksStart, numMarks);
	return err;
}

/*--------------------------------------
	CheckMarkPrefix
--------------------------------------*/
static const char* CheckMarkPrefix(const lfv_reader_state* s, size_t markIndex, size_t* comps)
{
	char* vec = s->buf + s->marks[markIndex];

	if(vec[0] == 'v')
	{
		if(vec[1] == '2')
			*comps = 2;
		else if(vec[1] == '3')
			*comps = 3;
		else
			return "Expected '2' or '3' after 'v' in vector prefix";
	}
	else if(vec[0] == 'q')
	{
		if(vec[1] == '4')
			*comps = 4;
		else
			return "Expected '4' after 'q' in vector prefix";
	}
	else
		return "Expected 'v' or 'q' in vector prefix";

	return 0;
}

/*--------------------------------------
	SkipBOMAndPound
--------------------------------------*/
static void SkipBOMAndPound(lfv_reader_state* s)
{
	/* Skip optional UTF-8 byte order mark */
	ExtendTokenSize(s, 3);

	if(EqualToken(s, "\xEF\xBB\xBF"))
		EraseToken(s);

	/* Skip first line if it starts with '#' */
	ExtendTokenSize(s, 1);

	if(s->buf[s->tok] == '#')
	{
		ExtendCToken(s, "\n");
		EraseToken(s);
	}

	s->tokSize = 0;
}

/*--------------------------------------
	WhitespaceSpan
--------------------------------------*/
static size_t WhitespaceSpan(const char* str, unsigned* line)
{
	size_t span = 0;

	while(1)
	{
		switch(*str)
		{
			case '\n':
				(*line)++;
			case ' ':
			case '\t':
			case '\f':
			case '\r':
				span++;
				str++;
				break;
			default:
				return span;
		}
	}
}

/*--------------------------------------
	ConsumeToken
--------------------------------------*/
static void ConsumeToken(lfv_reader_state* s)
{
	size_t i;

	for(i = 0; i < s->tokSize; i++)
	{
		if(s->buf[s->tok] == '\n')
			s->line++;

		s->tok++;
	}

	s->tokSize = 0;
}

/*--------------------------------------
	EraseToken
--------------------------------------*/
static void EraseToken(lfv_reader_state* s)
{
	size_t i;

	for(i = 0; i < s->tokSize; i++)
	{
		if(s->buf[s->tok] == '\n')
			s->line++;

		s->buf[s->tok] = ' ';
		s->tok++;
	}

	s->tokSize = 0;
}

/*--------------------------------------
	NextTokenNoSkip

Finds the first character of the next token.
--------------------------------------*/
static void NextTokenNoSkip(lfv_reader_state* s, size_t* afterConsume)
{
	/* Get start of next token */
	ConsumeToken(s);

	if(afterConsume)
		*afterConsume = s->tok;

	s->tok += WhitespaceSpan(s->buf + s->tok, &s->line);

	while(s->tok >= s->numBuf)
	{
		size_t read = ReadMore(s);

		if(!read)
		{
			s->tokSize = 0;
			return; /* No more tokens */
		}

		s->tok += WhitespaceSpan(s->buf + s->tok, &s->line);
	}

	s->tokSize = s->buf[s->tok] ? 1 : 0;
}

/*--------------------------------------
	NextTokenSkipCom

NextTokenNoSkip that checks for and skips comments.
--------------------------------------*/
static void NextTokenSkipCom(lfv_reader_state* s)
{
	NextTokenNoSkip(s, &s->beforeSkip);

	if(s->tokSize)
	{
		while(SkipComment(s));
		s->tokSize = s->buf[s->tok] ? 1 : 0;
	}
}

/*--------------------------------------
	ExtendToken

Probes the token only allowing chars in set, reading more into s->buf if needed. Sets and returns
s->tokSize.
--------------------------------------*/
#define EXTEND_TOKEN_IMP(spnfunc) \
	s->tokSize = spnfunc(s->buf + s->tok, set); \
	\
	if(s->tokSize) \
	{ \
		while(s->tok + s->tokSize >= s->numBuf) \
		{ \
			if(!ReadMore(s)) \
				break; \
			\
			s->tokSize += spnfunc(s->buf + s->tok + s->tokSize, set); \
		} \
	} \
	\
	return s->tokSize;

static size_t ExtendToken(lfv_reader_state* s, const char* set)
{
	EXTEND_TOKEN_IMP(strspn);
}

/*--------------------------------------
	ExtendCToken

Like ExtendToken but looks for characters not in set.
--------------------------------------*/
static size_t ExtendCToken(lfv_reader_state* s, const char* set)
{
	EXTEND_TOKEN_IMP(strcspn);
}

/*--------------------------------------
	ExtendTokenSize

Tries to set s->tokSize equal to size, reading more into s->buf if needed. Returns s->tokSize.
--------------------------------------*/
static size_t ExtendTokenSize(lfv_reader_state* s, size_t size)
{
	if(s->tokSize >= size)
	{
		s->tokSize = size;
		return s->tokSize;
	}

	while(s->tokSize < size)
	{
		size_t remaining = s->numBuf - s->tok;

		if(remaining < size)
		{
			s->tokSize = remaining;

			if(!ReadMore(s))
				break;
		}
		else
			s->tokSize = size;
	}

	return s->tokSize;
}

/*--------------------------------------
	ReadMore
--------------------------------------*/
static size_t ReadMore(lfv_reader_state* s)
{
	size_t read = 0;

	if(s->chk)
	{
		if(*s->chk)
		{
			if(s->numBuf >= s->bufSize - 1)
				EnsureBufSize(s, AddSizeT(s, AddSizeT(s, s->numBuf, INIT_BUF_SIZE), 1), TRUE);

			read = CopyStringNoTerm(s->buf + s->numBuf, s->chk, s->bufSize - s->numBuf - 1);
			s->chk += read;
		}
	}
	else if(s->f && !feof(s->f))
	{
		if(s->numBuf >= s->bufSize - 1)
			EnsureBufSize(s, AddSizeT(s, AddSizeT(s, s->numBuf, INIT_BUF_SIZE), 1), TRUE);

		read = fread(s->buf + s->numBuf, 1, s->bufSize - s->numBuf - 1, s->f);
	}

	s->numBuf += read;
	s->buf[s->numBuf] = 0;
	return read;
}

/*--------------------------------------
	EqualToken
--------------------------------------*/
static int EqualToken(const lfv_reader_state* s, const char* cmp)
{
	const char* ctok = s->buf + s->tok;
	size_t i = 0;

	if(!s->tokSize)
		return *cmp == 0;

	for(; i < s->tokSize && cmp[i]; i++)
	{
		if(*(ctok + i) != cmp[i])
			return FALSE;
	}

	return i == s->tokSize && cmp[i] == 0;
}

/*--------------------------------------
	TokenStartsWith
--------------------------------------*/
static int TokenStartsWith(const lfv_reader_state* s, const char* cmp)
{
	return StringStartsWith(s->buf + s->tok, s->tokSize, cmp);
}

/*--------------------------------------
	StringStartsWith
--------------------------------------*/
static int StringStartsWith(const char* str, size_t len, const char* cmp)
{
	size_t i = 0;

	if(!len)
		return *cmp == 0;

	for(; i < len && cmp[i]; i++)
	{
		if(*(str + i) != cmp[i])
			return FALSE;
	}

	return cmp[i] == 0;
}

/*--------------------------------------
	SkipLongBracket
--------------------------------------*/
static int SkipLongBracket(lfv_reader_state* s)
{
	const char* c;
	size_t level;

	ExtendToken(s, "[=");
	c = s->buf + s->tok;

	if(*c != '[')
		return FALSE;

	level = strspn(++c, "=");
	c += level;

	if(*c != '[')
		return FALSE;

	/* Skip bracket section */
	NextTokenNoSkip(s, 0);

	while(1)
	{
		ExtendCToken(s, "]");
		NextTokenNoSkip(s, 0);
		ExtendToken(s, "]=");
		c = s->buf + s->tok;

		if(*c == ']')
		{
			size_t endLevel = strspn(++c, "=");
			c += endLevel;
			s->tok = c - s->buf;
			s->tokSize = s->buf[s->tok] ? 1 : 0;

			if(*c == ']' && level == endLevel)
			{
				NextTokenNoSkip(s, 0);
				break;
			}
		}
		else
			break; /* Null terminator, unclosed section */
	}

	return TRUE;
}

/*--------------------------------------
	SkipComment
--------------------------------------*/
static int SkipComment(lfv_reader_state* s)
{
	const char* c;
	ExtendToken(s, "-[=");
	c = s->buf + s->tok;

	if(*c != '-')
		return FALSE;

	c++;

	if(*c == '-')
	{
		/* Comment */
		s->tok += 2;
		s->tokSize -= 2;

		if(!SkipLongBracket(s))
		{
			/* Not a long bracket section, normal comment, skip line */
			ExtendCToken(s, "\n");
			NextTokenNoSkip(s, 0);
		}

		return TRUE;
	}

	return FALSE;
}

/*--------------------------------------
	StringIsShortComment
--------------------------------------*/
static int StringIsShortComment(const char* str)
{
	return str[0] == '-' && str[1] == '-' &&
		(str[2] != '[' || str[3 + strspn(str+3, "=")] != '[');
}

/*--------------------------------------
	CopyShiftRight
--------------------------------------*/
static void CopyShiftRight(lfv_reader_state* s, size_t start, size_t amount, int updateMarks)
{
	size_t i;
	EnsureBufSize(s, AddSizeT(s, AddSizeT(s, s->numBuf, amount), 1), TRUE);

	if(s->numBuf)
	{
		i = s->numBuf;

		while(1)
		{
			s->buf[i + amount] = s->buf[i];

			if(i == start)
				break;

			i--;
		}
	}

	s->numBuf += amount;
	s->beforeSkip += amount;
	s->tok += amount;

	if(updateMarks)
	{
		for(i = 0; i < s->numMarks; i++)
		{
			if(s->marks[i] >= start)
				s->marks[i] += amount;
		}
	}
}

/*--------------------------------------
	EOFCheckedFRead

Checks feof before doing fread so user can Ctrl-Z stdin once (on Windows) and won't be prompted
again.
--------------------------------------*/
static size_t EOFCheckedFRead(void* dstBuf, size_t elementSize, size_t count, FILE* stream)
{
	if(feof(stream))
		return 0;

	return fread(dstBuf, elementSize, count, stream);
}

/*--------------------------------------
	StrChrNull
--------------------------------------*/
static const char* StrChrNull(const char* str, int ch)
{
	if(!ch)
		return 0;

	return strchr(str, ch);
}

/*--------------------------------------
	CopyStringNoTerm
--------------------------------------*/
static size_t CopyStringNoTerm(char* dest, const char* str, size_t maxNum)
{
	size_t i;

	for(i = 0; i < maxNum && str[i]; i++)
		dest[i] = str[i];

	return i;
}

/*--------------------------------------
	CeilPow2
--------------------------------------*/
static size_t CeilPow2(size_t n)
{
	if(!(n & (n - 1)))
		return n;

	while(n & (n - 1))
		n &= n - 1;

	n <<= 1;
	return n ? n : (size_t)-1;
}

/*--------------------------------------
	SetEmptyBufferSize
--------------------------------------*/
static void SetEmptyBufferSize(lfv_reader_state* s)
{
	s->bufSize = s->numBuf = s->tok = s->tokSize = 0;
}

/*--------------------------------------
	EnsureBufSize
--------------------------------------*/
static size_t EnsureBufSize(lfv_reader_state* s, size_t n, int doMemJmp)
{
	if(s->bufSize < n)
	{
		s->bufSize = CeilPow2(n);
		s->buf = (char*)ReallocOrFree(s->buf, s->bufSize);

		if(!s->buf)
		{
			SetReaderError(s, "Failed to EnsureBufSize", s->line);
			SetEmptyBufferSize(s);

			if(doMemJmp)
				longjmp(s->memErrJmp, 1);

			return 0;
		}
	}

	return s->bufSize;
}

/*--------------------------------------
	EnsureNumMarksAlloc
--------------------------------------*/
static size_t EnsureNumMarksAlloc(lfv_reader_state* s, size_t n, int doMemJmp)
{
	if(s->numMarksAlloc < n)
	{
		s->numMarksAlloc = CeilPow2(n);

		s->marks = (size_t*)ReallocOrFree(s->marks,
			MulSizeT(s, sizeof(size_t), s->numMarksAlloc));

		if(!s->marks)
		{
			SetReaderError(s, "Failed to EnsureNumMarksAlloc", s->line);
			s->numMarks = s->numMarksAlloc = 0;

			if(doMemJmp)
				longjmp(s->memErrJmp, 1);

			return 0;
		}
	}

	return s->numMarksAlloc;
}

/*--------------------------------------
	ReallocOrFree
--------------------------------------*/
static void* ReallocOrFree(void* mem, size_t newSize)
{
	void* newMem = realloc(mem, newSize);

	if(!newMem)
	{
		if(mem)
			free(mem);
	}

	return newMem;
}

/*--------------------------------------
	AddMark
--------------------------------------*/
static size_t AddMark(lfv_reader_state* s, size_t c)
{
	EnsureNumMarksAlloc(s, AddSizeT(s, s->numMarks, 1), TRUE);
	s->marks[s->numMarks] = c;
	return s->numMarks++;
}

/*--------------------------------------
	RemoveMarks
--------------------------------------*/
static void RemoveMarks(lfv_reader_state* s, size_t start, size_t num)
{
	size_t i;
	
	if(start + num >= s->numMarks)
		s->numMarks = start;
	else
	{
		for(i = start + num; i < s->numMarks; i++)
			s->marks[i - num] = s->marks[i];

		s->numMarks -= num;
	}
}

/*--------------------------------------
	AddSizeT
--------------------------------------*/
static size_t AddSizeT(lfv_reader_state* s, size_t a, size_t b)
{
	size_t c = a + b;

	if(c < a)
	{
		SetReaderError(s, "size_t add wrapped around", s->line);
		longjmp(s->memErrJmp, 1);
	}

	return c;
}

/*--------------------------------------
	MulSizeT
--------------------------------------*/
static size_t MulSizeT(lfv_reader_state* s, size_t a, size_t b)
{
	size_t c = a * b;

	if(a && c / a != b)
	{
		SetReaderError(s, "size_t multiply wrapped around", s->line);
		longjmp(s->memErrJmp, 1);
	}

	return c;
}

/*--------------------------------------
	IncRecursionLevel
--------------------------------------*/
static void IncRecursionLevel(lfv_reader_state* s)
{
	s->level++;

	if(s->level > MAX_RECURSION_LEVELS)
	{
		SetReaderError(s, "Recursion went over " STRINGIFY(MAX_RECURSION_LEVELS) " levels",
			s->line);

		longjmp(s->memErrJmp, 1);
	}
}

/*--------------------------------------
	DecRecursionLevel
--------------------------------------*/
static void DecRecursionLevel(lfv_reader_state* s)
{
	s->level--;
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
