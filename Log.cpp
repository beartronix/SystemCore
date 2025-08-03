/*
  This file is part of the DSP-Crowd project
  https://www.dsp-crowd.com

  Author(s):
      - Johannes Natter, office@dsp-crowd.com

  File created on 19.03.2021

  Copyright (C) 2021, Johannes Natter

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#ifndef CONFIG_PROC_LOG_HAVE_CHRONO
#if defined(__unix__) || defined(_WIN32)
#define CONFIG_PROC_LOG_HAVE_CHRONO			1
#else
#define CONFIG_PROC_LOG_HAVE_CHRONO			0
#endif
#endif

#ifndef CONFIG_PROC_LOG_HAVE_STDOUT
#if defined(__unix__) || defined(_WIN32)
#define CONFIG_PROC_LOG_HAVE_STDOUT			1
#else
#define CONFIG_PROC_LOG_HAVE_STDOUT			0
#endif
#endif

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#if CONFIG_PROC_LOG_HAVE_CHRONO
#include <chrono>
#include <time.h>
#endif
#if CONFIG_PROC_HAVE_DRIVERS
#include <mutex>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;

#if CONFIG_PROC_LOG_HAVE_CHRONO
using namespace chrono;
#endif

typedef void (*FuncEntryLogCreate)(
			const int severity,
			const void *pProc,
			const char *filename,
			const char *function,
			const int line,
			const int16_t code,
			const char *msg,
			const size_t len);

typedef uint32_t (*FuncCntTimeCreate)();

static FuncEntryLogCreate pFctEntryLogCreate = NULL;
static FuncCntTimeCreate pFctCntTimeCreate = NULL;
static int widthCntTime = 0;

const char* red		= "\033[0;31m";
const char* yellow	= "\033[0;33m";
const char* green	= "\033[0;32m";
const char* cyan	= "\033[0;36m";
const char* reset	= "\033[0m";

#if CONFIG_PROC_LOG_HAVE_CHRONO
static system_clock::time_point tOld;
const int cDiffSecMax = 9;
const int cDiffMsMax = 999;
#endif

const size_t cLogEntryBufferSize = 230;
static int levelLog = 3;
#if CONFIG_PROC_HAVE_DRIVERS
static mutex mtxPrint;
#endif

void levelLogSet(int lvl)
{
	levelLog = lvl;
}

void entryLogCreateSet(FuncEntryLogCreate pFct)
{
	pFctEntryLogCreate = pFct;
}

void cntTimeCreateSet(FuncCntTimeCreate pFct, int width)
{
	if (width < -20 || width > 20)
		return;

	pFctCntTimeCreate = pFct;
	widthCntTime = width;
}

static const char *severityToStr(const int severity)
{
	switch (severity)
	{
	case 1: return "ERR";
	case 2: return "WRN";
	case 3: return "INF";
	case 4: return "DBG";
	case 5: return "COR";
	default: break;
	}
	return "INV";
}

static int pBufSaturate(int lenDone, char * &pBuf, const char *pBufEnd)
{
	if (lenDone < 0)
		return lenDone;

	if (lenDone > pBufEnd - pBuf)
		lenDone = pBufEnd - pBuf;

	pBuf += lenDone;

	return lenDone;
}

int16_t entryLogSimpleCreate(
			const int isErr,
			const int16_t code,
			const char *msg, ...)
{
#if CONFIG_PROC_HAVE_DRIVERS
	lock_guard<mutex> lock(mtxPrint); // Guard not defined!
#endif
	FILE *pStream = isErr ? stderr : stdout;
	int lenDone;
	va_list args;

	va_start(args, msg);
	lenDone = vfprintf(pStream, msg, args);
	if (lenDone < 0)
	{
		va_end(args);
		return code;
	}
	va_end(args);

	fflush(pStream);

	return code;
}

int16_t entryLogCreate(
			const int severity,
			const void *pProc,
			const char *filename,
			const char *function,
			const int line,
			const int16_t code,
			const char *msg, ...)
{
#if CONFIG_PROC_HAVE_DRIVERS
	lock_guard<mutex> lock(mtxPrint); // Guard not defined!
#endif
	char *pBufStart = (char *)malloc(cLogEntryBufferSize);
	if (!pBufStart)
		return code;

	char *pBuf = pBufStart;
	char *pBufEnd = pBuf + cLogEntryBufferSize - 1;

	*pBuf = 0;
	*pBufEnd = 0;

#if CONFIG_PROC_LOG_HAVE_CHRONO
	// get time
	system_clock::time_point t = system_clock::now();
	milliseconds durDiffMs = duration_cast<milliseconds>(t - tOld);

	// build day
	time_t tTt = system_clock::to_time_t(t);
	char timeBuf[32];
	tm tTm {};
#ifdef _WIN32
	::localtime_s(&tTm, &tTt);
#else
	::localtime_r(&tTt, &tTm);
#endif
	strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d", &tTm);

	// build time
	system_clock::duration dur = t.time_since_epoch();

	hours durDays = duration_cast<hours>(dur) / 24;
	dur -= durDays * 24;

	hours durHours = duration_cast<hours>(dur);
	dur -= durHours;

	minutes durMinutes = duration_cast<minutes>(dur);
	dur -= durMinutes;

	seconds durSecs = duration_cast<seconds>(dur);
	dur -= durSecs;

	milliseconds durMillis = duration_cast<milliseconds>(dur);
	dur -= durMillis;

	// build diff
	long long tDiff = durDiffMs.count();
	int tDiffSec = int(tDiff / 1000);
	int tDiffMs = int(tDiff % 1000);
	bool diffMaxed = false;

	if (tDiffSec > cDiffSecMax)
	{
		tDiffSec = cDiffSecMax;
		tDiffMs = cDiffMsMax;

		diffMaxed = true;
	}
#endif
	// merge
	int lenDone, lenPrefix2 = 73;

#if CONFIG_PROC_LOG_HAVE_CHRONO
	lenDone = snprintf(pBuf, pBufEnd - pBuf,
					"%s  %02d:%02d:%02d.%03d "
					"%c%d.%03d  ",
					timeBuf,
					int(durHours.count()), int(durMinutes.count()),
					int(durSecs.count()), int(durMillis.count()),
					diffMaxed ? '>' : '+', tDiffSec, tDiffMs);
	if (pBufSaturate(lenDone, pBuf, pBufEnd) < 0)
		goto exitLogEntryCreate;
#endif
	if (pFctCntTimeCreate)
	{
		uint32_t cntTime = pFctCntTimeCreate();

		lenDone = snprintf(pBuf, pBufEnd - pBuf,
						"%*" PRIu32 "  ",
						widthCntTime, cntTime);

		if (pBufSaturate(lenDone, pBuf, pBufEnd) < 0)
			goto exitLogEntryCreate;
	}

	if (pProc)
	{
		lenDone = snprintf(pBuf, pBufEnd - pBuf,
						"%s  %-20s  %p %s:%-4d  ",
						severityToStr(severity),
						function, pProc, filename, line);
	}
	else
	{
		lenDone = snprintf(pBuf, pBufEnd - pBuf,
						"%s  %-20s  %s:%-4d  ",
						severityToStr(severity),
						function, filename, line);
	}

	if (pBufSaturate(lenDone, pBuf, pBufEnd) < 0)
		goto exitLogEntryCreate;

	// prefix padding
	while (lenDone < lenPrefix2 && pBuf < pBufEnd)
	{
		*pBuf++ = ' ';
		++lenDone;
	}

	// user msg
	va_list args;

	va_start(args, msg);
	lenDone = vsnprintf(pBuf, pBufEnd - pBuf, msg, args);
	if (pBufSaturate(lenDone, pBuf, pBufEnd) < 0)
	{
		va_end(args);
		goto exitLogEntryCreate;
	}
	va_end(args);

#if CONFIG_PROC_LOG_HAVE_STDOUT
	// create log entry
	if (severity <= levelLog)
	{
#if CONFIG_PROC_LOG_HAVE_CHRONO
		tOld = t;
#endif
#ifdef _WIN32
		HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

		if (severity == 1)
		{
			SetConsoleTextAttribute(hConsole, 4);
			fprintf(stderr, "%s\r\n", pBufStart);
		}
		else
		if (severity == 2)
		{
			SetConsoleTextAttribute(hConsole, 6);
			fprintf(stderr, "%s\r\n", pBufStart);
		}
		else
			fprintf(stdout, "%s\r\n", pBufStart);

		SetConsoleTextAttribute(hConsole, 7);
#else

#if 1
		if (severity == 1)
			fprintf(stderr, "%s%s%s\r\n", red, pBufStart, reset);
		else
		if (severity == 2)
			fprintf(stderr, "%s%s%s\r\n", yellow, pBufStart, reset);
		else
		if (severity == 3)
			fprintf(stderr, "%s%s%s\r\n", green, pBufStart, reset);
		else
			fprintf(stdout, "%s%s%s\r\n", cyan, pBufStart, reset);
#endif
	}
#endif
#endif
	if (pFctEntryLogCreate)
		pFctEntryLogCreate(severity,
			pProc, filename, function, line, code,
			pBufStart, pBuf - pBufStart);

exitLogEntryCreate:
	free(pBufStart);

	return code;
}

