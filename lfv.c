/* lfv.c */
/* Martynas Ceicys */
/* Copyright notice at end of file */

#include "lfv.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#define FALSE 0
#define TRUE 1
#define RETURN_ERROR(str) return ReaderError(str, line)
#define IDENTIFIER_CHARS "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz"
#define NUMERAL_CHARS "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-."
#define WHITESPACE_CHARS " \t\n\f\r"
#define READ_SIZE 256

typedef struct reader_state_s {
	lua_State*		l;
	FILE			*f, *fo;
	const char*		name;
	char*			buf; /* realloc'd null-terminated parse stream */
	size_t			bufSize;
	size_t			numBuf; /* Excludes null terminator */
	size_t			tok, tokSize; /* Char index and length in buf */
	size_t			line;
	size_t			comment; /* tok value before skipping comments */
	size_t*			vecs; /* realloc'd stack of char indices pointing at vector prefixes; stores
						  indices for all current ExpandExp calls on the call stack */
	size_t			numVecsAlloc, numVecs;
	int				topResult;
} reader_state;

enum
{
	/* Expansion return values */
	EXPAND_OK,
	EXPAND_UNFIT,
	EXPAND_ERR,

	/* Extra states only used in reader function */
	EXPAND_OFF,
	EXPAND_INIT,
	EXPAND_FORCE
};

static const char*	earliestError = 0;
static unsigned		errorLine = 0;
static char*		debugPath = 0;
static int			debugAlwaysOutput = FALSE;
static int			debugHeader = FALSE;

void		ResetError();
int			ReaderError(const char* str, unsigned line);
void		InitReaderState(lua_State* l, const char* chunk, FILE* file, const char* name,
			int force, reader_state* sOut);
void		TermReaderState(reader_state* sIO);
const char*	VectorExpressionReader(lua_State* l, void* data, size_t* size);
int			ExpandBlock(reader_state* sIO);
int			ExpandStat(reader_state* sIO);
int			ExpandRetstat(reader_state* sIO);
int			ExpandLabel(reader_state* sIO);
int			ExpandName(reader_state* sIO, int checkVector);
int			ExpandNumeral(reader_state* sIO);
int			ExpandString(reader_state* sIO);
int			ExpandFuncname(reader_state* sIO);
int			ExpandExplist(reader_state* sIO);
int			ExpandExp(reader_state* sIO);
int			ExpandArgs(reader_state* sIO);
int			ExpandFunctiondef(reader_state* sIO);
int			ExpandFuncbody(reader_state* sIO);
int			ExpandTableconstructor(reader_state* sIO);
int			ExpandBinop(reader_state* sIO);
int			ExpandUnop(reader_state* sIO);
void		ResetTokenSize(reader_state* sIO);
size_t		WhitespaceSpan(const char* str, size_t* lineIO);
void		ConsumeToken(reader_state* sIO);
void		NextTokenNoSkip(reader_state* sIO);
void		NextTokenSkipCom(reader_state* sIO);
size_t		ExtendToken(reader_state* sIO, const char* set);
size_t		ExtendCToken(reader_state* sIO, const char* cset);
size_t		ExtendTokenSize(reader_state* sIO, size_t size);
size_t		ReadMore(reader_state* sIO);
int			EqualToken(const reader_state* s, const char* cmp);
int			SkipLongBracket(reader_state* sIO);
int			SkipComment(reader_state* sIO);
size_t		CeilPow2(size_t n);
void		EnsureBufSize(reader_state* sIO, size_t n);
void		EnsureNumVecsAlloc(reader_state* sIO, size_t n);

/*--------------------------------------
	lfvLoadFile
--------------------------------------*/
int lfvLoadFile(lua_State* l, const char* filePath, int forceExpand)
{
	reader_state rs;
	int ret;
	FILE* f;

	ResetError();

	if(filePath)
		f = fopen(filePath, "r");
	else
	{
		f = stdin;
		filePath = "stdin";
	}

	if(!f)
	{
		lua_pushstring(l, strerror(errno));
		return LUA_ERRFILE;
	}

	InitReaderState(l, 0, f, filePath, forceExpand, &rs);

	/* Skip optional UTF-8 byte order mark */
	ExtendTokenSize(&rs, 3);

	if(EqualToken(&rs, "\xEF\xBB\xBF"))
		ConsumeToken(&rs);

	/* Skip first line if it starts with '#' */
	ExtendTokenSize(&rs, 1);

	if(rs.buf[rs.tok] == '#')
	{
		ExtendCToken(&rs, "\n");
		ConsumeToken(&rs);
	}

	rs.tokSize = 0;
	ret = lua_load(l, VectorExpressionReader, (void*)&rs, filePath, "t");
	fclose(f);
	TermReaderState(&rs);
	return ret;
}

/*--------------------------------------
	lfvLoadString
--------------------------------------*/
int lfvLoadString(lua_State* l, const char* chunk, const char* chunkName, int forceExpand)
{
	reader_state rs;
	int ret;

	ResetError();

	if(!chunkName)
		chunkName = chunk;

	InitReaderState(l, chunk, 0, chunkName, forceExpand, &rs);
	ret = lua_load(l, VectorExpressionReader, (void*)&rs, chunkName, "t");
	TermReaderState(&rs);
	return ret;
}

/*--------------------------------------
	lfvErrorLine
--------------------------------------*/
unsigned lfvErrorLine() {return errorLine;}

/*--------------------------------------
	lfvError
--------------------------------------*/
const char* lfvError() {return earliestError;}

/*--------------------------------------
	lfvDebugPath
--------------------------------------*/
const char* lfvDebugPath()
{
	return debugPath;
}

/*--------------------------------------
	lfvSetDebugPath
--------------------------------------*/
int lfvSetDebugPath(const char* filePath, int clear, int outputUnexpanded, int headerComment)
{
	if(debugPath)
	{
		free(debugPath);
		debugPath = 0;
	}

	debugAlwaysOutput = outputUnexpanded;
	debugHeader = headerComment;

	if(filePath)
	{
		FILE* f = fopen(filePath, clear ? "w" : "a");

		if(!f || fclose(f))
			return -1;

		debugPath = (char*)malloc(strlen(filePath) + 1);
		strcpy(debugPath, filePath);
	}

	return 0;
}

/*--------------------------------------
LUA	lfvCLuaLoadFile
--------------------------------------*/
int lfvCLuaLoadFile(lua_State* l)
{
	if(lfvLoadFile(l, luaL_checkstring(l, 1), lua_toboolean(l, 2)) != LUA_OK)
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
int lfvCLuaLoadString(lua_State* l)
{
	if(lfvLoadString(l, luaL_checkstring(l, 1), luaL_optstring(l, 2, 0),
	lua_toboolean(l, 3)) != LUA_OK)
	{
		lua_pushnil(l);
		lua_insert(l, -2);
		return 2;
	}

	return 1;
}

/*--------------------------------------
LUA	lfvCLuaErrorLine
--------------------------------------*/
int lfvCLuaErrorLine(lua_State* l)
{
	unsigned line = lfvErrorLine();

	if(line)
		lua_pushinteger(l, line);
	else
		lua_pushnil(l);
	
	return 1;
}

/*--------------------------------------
LUA	lfvCLuaError
--------------------------------------*/
int lfvCLuaError(lua_State* l)
{
	const char* err = lfvError();

	if(err)
		lua_pushstring(l, err);
	else
		lua_pushnil(l);

	return 1;
}

/*--------------------------------------
	ResetError
--------------------------------------*/
void ResetError()
{
	errorLine = 0;
	earliestError = 0;
}

/*--------------------------------------
	ReaderError

str must be constant. Always returns EXPAND_ERR.
--------------------------------------*/
int ReaderError(const char* str, unsigned line)
{
	if(!earliestError)
	{
		earliestError = str;
		errorLine = line;
	}

	return EXPAND_ERR;
}

/*--------------------------------------
	InitReaderState
--------------------------------------*/
void InitReaderState(lua_State* l, const char* chunk, FILE* file, const char* name, int force,
	reader_state* s)
{
	s->l = l;
	s->f = file;
	s->fo = 0;
	s->name = name;
	s->buf = 0;
	s->bufSize = 0;
	s->numBuf = 0;
	s->tok = 0;
	s->tokSize = 0;
	s->line = 1;
	s->comment = 0;
	s->vecs = 0;
	s->numVecs = 0;
	s->numVecsAlloc = 0;
	s->topResult = force ? EXPAND_FORCE : EXPAND_INIT;

	if(chunk)
	{
		/* Given a string, allocate buffer and copy the whole thing */
		s->numBuf = strlen(chunk);
		s->bufSize = s->numBuf + 1;
		s->buf = (char*)malloc(s->bufSize);
		strncpy(s->buf, chunk, s->numBuf);
		s->buf[s->numBuf] = 0;
	}
	else if(s->f)
	{
		/* Only given a file, allocate buffer and initialize with first read */
		EnsureBufSize(s, READ_SIZE);
		s->numBuf = fread(s->buf, 1, READ_SIZE - 1, s->f);
		s->buf[s->numBuf] = 0;
	}
}

/*--------------------------------------
	TermReaderState
--------------------------------------*/
void TermReaderState(reader_state* s)
{
	if(s->buf)
	{
		free(s->buf);
		s->buf = 0;
	}

	if(s->vecs)
	{
		free(s->vecs);
		s->vecs = 0;
	}

	if(s->fo)
	{
		fputc('\n', s->fo);
		fclose(s->fo);
		s->fo = 0;
	}
}

/*--------------------------------------
	VectorExpressionReader
--------------------------------------*/
const char* VectorExpressionReader(lua_State* l, void* data, size_t* sizeOut)
{
	reader_state* s = (reader_state*)data;

	/* Discard characters parsed by Lua */
	if(s->tok && s->numBuf > s->tok)
	{
		size_t i;

		for(i = 0; i < s->numBuf - s->tok; i++)
			s->buf[i] = s->buf[s->tok + i];
	}

	s->numBuf -= s->tok;
	s->buf[s->numBuf] = 0;
	s->tok = 0;

	if(s->topResult >= EXPAND_INIT /* includes EXPAND_FORCE */ )
	{
		/* Look for expansion request if this is the first reader call */
		NextTokenSkipCom(s);
		ExtendToken(s, IDENTIFIER_CHARS "()");

		if(EqualToken(s, "LFV_EXPAND_VECTORS()"))
		{
			size_t i;

			for(i = s->tok; i < s->tok + s->tokSize; i++)
				s->buf[i] = ' '; /* Clear token so fake function isn't actually called */

			s->topResult = EXPAND_OK;
			NextTokenSkipCom(s);
		}
		else
			s->topResult = s->topResult == EXPAND_FORCE ? EXPAND_OK : EXPAND_OFF;

		if(debugPath && (s->topResult == EXPAND_OK || debugAlwaysOutput))
		{
			s->fo = fopen(debugPath, "a");

			if(s->fo && debugHeader)
			{
				fprintf(s->fo, "-- vector expansion of %s\n",
					(s->f ? s->name : "string"));
			}
		}
	}

	if(s->topResult == EXPAND_OK)
	{
		/* Start/continue main block; same as ExpandBlock but given to Lua after each statement */
		int line = s->line;
		int statRes = ExpandStat(s);

		if(statRes == EXPAND_UNFIT)
		{
			int res;
			line = s->line;
			res = ExpandRetstat(s);

			if(res == EXPAND_ERR)
			{
				ReaderError("Bad retstat in main block", line);
				s->topResult = EXPAND_ERR;
			}
		}
		else if(statRes == EXPAND_ERR)
		{
			ReaderError("Bad stat in main block", line);
			s->topResult = EXPAND_ERR;
		}

		*sizeOut = s->tok;
	}
	else
	{
		if(s->tok)
			*sizeOut = s->tok;
		else
		{
			if(!s->numBuf)
				ReadMore(s);

			*sizeOut = s->numBuf;
			s->numBuf = 0;
		}
	}

	/* Output expanded result */
	if(s->fo)
		fwrite(s->buf, 1, *sizeOut, s->fo);

	return s->buf;
}

/*--------------------------------------
	ExpandBlock
--------------------------------------*/
int ExpandBlock(reader_state* s)
{
	int line = s->line;
	
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
int ExpandStat(reader_state* s)
{
	int res;
	int line = s->line;
	ResetTokenSize(s);

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

		if(ExpandExp(s) != EXPAND_OK)
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

		if(ExpandExp(s) != EXPAND_OK)
			RETURN_ERROR("Expected exp after 'repeat block until'");

		return EXPAND_OK;
	}

	if(EqualToken(s, "if"))
	{
		NextTokenSkipCom(s);

		if(ExpandExp(s) != EXPAND_OK)
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

			if(ExpandExp(s) != EXPAND_OK)
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
			if(ExpandExplist(s) != EXPAND_OK)
				RETURN_ERROR("Expected 'function' or explist after 'local'");

			if(s->buf[s->tok] == '=')
			{
				NextTokenSkipCom(s);

				if(ExpandExplist(s) != EXPAND_OK)
					RETURN_ERROR("Expected explist after 'local explist ='");
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
	ExpandRetstat
--------------------------------------*/
int ExpandRetstat(reader_state* s)
{
	int line = s->line;
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
int ExpandLabel(reader_state* s)
{
	int line = s->line;
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

If checkVector is true, adds the char index of a vector prefix ("v2", "v3", or "q4") to s->vecs.
s->vecs may be realloc'd.
--------------------------------------*/
int ExpandName(reader_state* s, int checkVector)
{
	if(strchr("0123456789", s->buf[s->tok]))
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
		{
			EnsureNumVecsAlloc(s, s->numVecs + 1);
			s->vecs[s->numVecs++] = s->tok;
		}
	}

	NextTokenSkipCom(s);
	return EXPAND_OK;
}

/*--------------------------------------
	ExpandNumeral
--------------------------------------*/
int ExpandNumeral(reader_state* s)
{
	int line = s->line;
	size_t i, numSize;
	char *end, saveEnd;
	int isNum;

	ResetTokenSize(s);

	if(!s->tokSize)
		return EXPAND_UNFIT;

	if(!strchr("0123456789", s->buf[s->tok]))
		return EXPAND_UNFIT;

	ExtendToken(s, NUMERAL_CHARS);

	if(!lua_checkstack(s->l, 1))
		RETURN_ERROR("Can't expand Lua stack to check numeral");

	/* Check for - or + in the middle of the token (comment or operator) */
	for(i = 1; i < s->tokSize; i++)
	{
		int c = s->buf[s->tok + i];

		if(c == '-' || c == '+')
		{
			s->tokSize = i;
			break;
		}
	}

#if LUA_VERSION_NUM >= 503
	end = &s->buf[s->tok + s->tokSize];
	saveEnd = *end;
	*end = 0; /* HACK: Temporarily null terminate after numeral */
	numSize = lua_stringtonumber(s->l, &s->buf[s->tok]);
	*end = saveEnd;

	if(!numSize)
		RETURN_ERROR("Bad numeral");

	lua_pop(s->l, 1);
	s->tokSize = numSize - 1;

#else
	/* Push temporary string onto Lua stack so Lua can check if it's a number */
	lua_pushlstring(s->l, &s->buf[s->tok], s->tokSize);
	isNum = lua_isnumber(s->l, -1);
	lua_pop(s->l, 1);

	if(!isNum)
		RETURN_ERROR("Bad numeral");
#endif

	NextTokenSkipCom(s);
	return EXPAND_OK;
}

/*--------------------------------------
	ExpandString
--------------------------------------*/
int ExpandString(reader_state* s)
{
	int line = s->line;
	char openQuote;
	ResetTokenSize(s);
	openQuote = s->buf[s->tok];

	if(openQuote == '"' || openQuote == '\'')
	{
		while(1)
		{
			NextTokenNoSkip(s);
			ExtendCToken(s, "'\"\n");
			NextTokenNoSkip(s);

			if(strchr("'\"", s->buf[s->tok]))
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
int ExpandFuncname(reader_state* s)
{
	int line = s->line;
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
int ExpandExplist(reader_state* s)
{
	int line = s->line;
	int res = ExpandExp(s);

	if(res != EXPAND_OK)
		return res;

	while(s->buf[s->tok] == ',')
	{
		NextTokenSkipCom(s);

		if(ExpandExp(s) != EXPAND_OK)
			RETURN_ERROR("explist expected exp after ','");
	}

	return EXPAND_OK;
}

/*--------------------------------------
	ExpandExp
--------------------------------------*/
#define EXP_ERROR(str) \
{ \
	s->numVecs = saveNumVecs; \
	return ReaderError(str, line); \
}

int ExpandExp(reader_state* s)
{
	int line = s->line;
	size_t start = s->tok;
	int hang = TRUE; /* Next token should be a ref or value (true on start and after
					 operator) */
	int ref = FALSE; /* Last token completed a potential object reference
					 (callable/accessible) */
	int par = 0; /* Parenthesis level, expression ends if below 0 */
	size_t saveNumVecs = s->numVecs;

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
			ResetTokenSize(s);

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
			ResetTokenSize(s);

			if(s->buf[s->tok] == '[')
			{
				NextTokenSkipCom(s);

				if(ExpandExp(s) != EXPAND_OK)
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
		ResetTokenSize(s);

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

	if(s->numVecs > saveNumVecs)
	{
		const char COMPS[4] = {'x', 'y', 'z', 'w'};
		size_t i;
		size_t minVec = 4;
		size_t expEnd = s->comment; /* Don't include latest comments in expression */
		size_t expLen = expEnd - start;
		size_t addLen;

		/* Trim ending whitespace in expression */
		while(expLen > 2 && strchr(WHITESPACE_CHARS, s->buf[expEnd - 1]))
		{
			expEnd--;
			expLen--;
		}

		/* Find minimum vector size in expression */
		for(i = saveNumVecs; i < s->numVecs; i++)
		{
			size_t num = s->buf[s->vecs[i] + 1] - '0';

			if(num < minVec)
				minVec = num;
		}

		/* Duplicate expression to make it a per-component expression list */
		addLen = expLen * (minVec - 1) + minVec - 1; /* Duplicates + commas */
		EnsureBufSize(s, s->numBuf + addLen + 1);

		/* Shift chars after expression rightward to make room for duplicates */
		for(i = 0; i < s->numBuf - expEnd; i++)
		{
			size_t j = s->numBuf - 1 - i;
			s->buf[j + addLen] = s->buf[j];
		}

		s->numBuf += addLen;
		s->buf[s->numBuf] = 0;
		s->comment += addLen;
		s->tok += addLen;

		/* Duplicate */
		for(i = 1; i < minVec; i++)
		{
			size_t dupStart = start + (expLen + 1) * i;
			s->buf[dupStart - 1] = ',';
			strncpy(&s->buf[dupStart], &s->buf[start], expLen);
		}

		/* Change names to be per-component */
		for(i = 0; i < minVec; i++)
		{
			size_t compStart = start + (expLen + 1) * i;
			size_t v;

			for(v = saveNumVecs; v < s->numVecs; v++)
			{
				char* c = &s->buf[compStart + s->vecs[v] - start];

				if(c[0] == 'v')
					c[0] = ' ';

				c[1] = COMPS[i];
			}
		}

		/* Remove newlines except from last duplicate to keep line number consistent */
		for(i = 0; i < minVec - 1; i++)
		{
			size_t compStart = start + (expLen + 1) * i;
			size_t j;

			for(j = compStart; j < compStart + expLen; j++)
			{
				if(s->buf[j] == '\n' || s->buf[j] == '\r')
					s->buf[j] = ' ';
			}
		}
	}

	s->numVecs = saveNumVecs;
	return start == s->tok ? EXPAND_UNFIT : EXPAND_OK;
}

/*--------------------------------------
	ExpandArgs
--------------------------------------*/
int ExpandArgs(reader_state* s)
{
	int line = s->line;
	int res;
	ResetTokenSize(s);

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
int ExpandFunctiondef(reader_state* s)
{
	int line = s->line;
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
int ExpandFuncbody(reader_state* s)
{
	int line = s->line;
	ResetTokenSize(s);

	if(s->buf[s->tok] != '(')
		return EXPAND_UNFIT;

	NextTokenSkipCom(s);

	if(ExpandExplist(s) == EXPAND_ERR)
		RETURN_ERROR("Bad explist in funcbody after '('");

	ResetTokenSize(s);

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
int ExpandTableconstructor(reader_state* s)
{
	int line = s->line;
	int level = 1;
	ResetTokenSize(s);

	if(s->buf[s->tok] != '{')
		return EXPAND_UNFIT;

	while(1)
	{
		char c;
		NextTokenSkipCom(s);
		c = s->buf[s->tok];

		if(c == '{')
		{
			level++;
			NextTokenSkipCom(s);
		}
		else if(c == '}')
		{
			level--;
			NextTokenSkipCom(s);

			if(!level)
				break;
		}
		else if(c == '-')
		{
			/* Not a comment, go forward */
			ResetTokenSize(s);
			NextTokenSkipCom(s);
		}
		else if(!c)
			RETURN_ERROR("Unclosed tableconstructor");

		/* Skip characters, stop for curly braces and potential comments */
		ExtendCToken(s, "{}-");
	}

	return EXPAND_OK;
}

/*--------------------------------------
	ExpandBinop
--------------------------------------*/
int ExpandBinop(reader_state* s)
{
	char nc;
	ResetTokenSize(s);

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
			ExtendToken(s, "/");

			if(s->buf[s->tok + 1] == '/')
				s->tokSize = 2;
			else
				s->tokSize = 1;

			NextTokenSkipCom(s);
			return EXPAND_OK;
		}
		case '>':
		{
			ExtendToken(s, ">=");
			nc = s->buf[s->tok + 1];

			if(nc == '>' || nc == '=')
				s->tokSize = 2;
			else
				s->tokSize = 1;

			NextTokenSkipCom(s);
			return EXPAND_OK;
		}
		case '<':
		{
			ExtendToken(s, "<=");
			nc = s->buf[s->tok + 1];

			if(nc == '<' || nc == '=')
				s->tokSize = 2;
			else
				s->tokSize = 1;

			NextTokenSkipCom(s);
			return EXPAND_OK;
		}
		case '.':
		{
			ExtendToken(s, ".");

			if(s->tokSize != 2)
				return EXPAND_UNFIT;

			NextTokenSkipCom(s);
			return EXPAND_OK;
		}
		case '~':
		{
			ExtendToken(s, "~=");

			if(s->buf[s->tok + 1] == '=')
				s->tokSize = 2;
			else
				s->tokSize = 1;

			NextTokenSkipCom(s);
			return EXPAND_OK;
		}
		case '=':
		{
			ExtendToken(s, "=");

			if(s->buf[s->tok + 1] == '=')
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
int ExpandUnop(reader_state* s)
{
	char c = s->buf[s->tok];
	ResetTokenSize(s);

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
	ResetTokenSize
--------------------------------------*/
void ResetTokenSize(reader_state* s)
{
	if(s->buf[s->tok])
		s->tokSize = 1;
	else
		s->tokSize = 0;
}

/*--------------------------------------
	WhitespaceSpan
--------------------------------------*/
size_t WhitespaceSpan(const char* str, size_t* line)
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
void ConsumeToken(reader_state* s)
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
	NextTokenNoSkip

Finds the first character of the next token.
--------------------------------------*/
void NextTokenNoSkip(reader_state* s)
{
	/* Get start of next token */
	ConsumeToken(s);
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

	s->tokSize = 1;
}

/*--------------------------------------
	NextTokenSkipCom

NextTokenNoSkip that checks for and skips comments.
--------------------------------------*/
void NextTokenSkipCom(reader_state* s)
{
	NextTokenNoSkip(s);
	s->comment = s->tok;

	if(s->tokSize)
	{
		while(SkipComment(s));
		ResetTokenSize(s);
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

size_t ExtendToken(reader_state* s, const char* set)
{
	EXTEND_TOKEN_IMP(strspn);
}

/*--------------------------------------
	ExtendCToken

Like ExtendToken but looks for characters not in set.
--------------------------------------*/
size_t ExtendCToken(reader_state* s, const char* set)
{
	EXTEND_TOKEN_IMP(strcspn);
}

/*--------------------------------------
	ExtendTokenSize

Tries to set s->tokSize equal to size, reading more into s->buf if needed. Returns s->tokSize.
--------------------------------------*/
size_t ExtendTokenSize(reader_state* s, size_t size)
{
	if(s->tokSize >= size)
	{
		s->tokSize = size;
		return s->tokSize;
	}

	while(s->tokSize < size)
	{
		size_t rem = s->numBuf - s->tok;

		if(rem < size)
		{
			s->tokSize = rem;

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
size_t ReadMore(reader_state* s)
{
	size_t read;

	if(!s->f)
		return 0; /* Reader is not loading from a file, nothing to read */

	if(s->numBuf < s->bufSize - 1)
		read = fread(s->buf + s->numBuf, 1, s->bufSize - s->numBuf - 1, s->f);
	else
	{
		EnsureBufSize(s, s->numBuf + READ_SIZE);
		read = fread(s->buf + s->numBuf, 1, READ_SIZE, s->f);
	}

	s->numBuf += read;
	s->buf[s->numBuf] = 0;
	return read;
}

/*--------------------------------------
	EqualToken
--------------------------------------*/
int EqualToken(const reader_state* s, const char* cmp)
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
	SkipLongBracket
--------------------------------------*/
int SkipLongBracket(reader_state* s)
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
	NextTokenNoSkip(s);

	while(1)
	{
		ExtendCToken(s, "]");
		NextTokenNoSkip(s);
		ExtendToken(s, "]=");
		c = s->buf + s->tok;

		if(*c == ']')
		{
			size_t endLevel = strspn(++c, "=");
			c += endLevel;
			s->tok = c - s->buf;
			s->tokSize = 1;

			if(*c == ']' && level == endLevel)
			{
				NextTokenNoSkip(s);
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
int SkipComment(reader_state* s)
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
			NextTokenNoSkip(s);
		}

		return TRUE;
	}

	return FALSE;
}

/*--------------------------------------
	CeilPow2
--------------------------------------*/
size_t CeilPow2(size_t n)
{
	if(!(n & (n - 1)))
		return n;

	while(n & (n - 1))
		n &= n - 1;

	n <<= 1;
	return n ? n : (size_t)-1;
}

/*--------------------------------------
	EnsureBufSize
--------------------------------------*/
void EnsureBufSize(reader_state* s, size_t n)
{
	if(s->bufSize < n)
	{
		s->bufSize = CeilPow2(n);
		s->buf = (char*)realloc(s->buf, s->bufSize);
	}
}

/*--------------------------------------
	EnsureNumVecsAlloc
--------------------------------------*/
void EnsureNumVecsAlloc(reader_state* s, size_t n)
{
	if(s->numVecsAlloc < n)
	{
		s->numVecsAlloc = CeilPow2(n);
		s->vecs = (size_t*)realloc(s->vecs, sizeof(size_t) * s->numVecsAlloc);
	}
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
