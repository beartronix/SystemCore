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
#include "Processing.h"

#include <cinttypes>
#if CONFIG_PROC_HAVE_CHRONO
#include <chrono>
using namespace chrono;
#endif
#include <stdarg.h>
#if CONFIG_PROC_HAVE_DRIVERS
#include <mutex>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

#include "stm32-uart/cpp/SystemDebugging.h"

#include "util.hpp"

using namespace std;

typedef void (*FuncEntryLogCreate)(
			const int severity,
			const char *filename,
			const char *function,
			const int line,
			const int16_t code,
			const char *msg,
			const size_t len);

static FuncEntryLogCreate pFctEntryLogCreate = NULL;

#if CONFIG_PROC_HAVE_CHRONO
static system_clock::time_point tOld;
#else
static uint32_t tOld;
#endif

const int cDiffSecMax = 9;
const int cDiffMsMax = 999;

const size_t cLogEntryBufferSize = 1024;
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

int16_t logEntryCreate(const int severity, const char *filename, const char *function, const int line, const int16_t code, const char *msg, ...)
{
#if CONFIG_PROC_LOG_DIRECT
	const char* red = "\033[0;31m";
	const char* yellow = "\033[0;33m";
	const char* reset = "\033[37m";
#endif

#if CONFIG_PROC_HAVE_DRIVERS
	lock_guard<mutex> lock(mtxPrint); // Guard not defined!
#endif
	char *pBuf = new dNoThrow char[cLogEntryBufferSize];
	if (!pBuf)
		return code;

	char *pStart = pBuf;
	char *pEnd = pStart + cLogEntryBufferSize - 1;

	*pStart = 0;
	*pEnd = 0;

	va_list args;

#if CONFIG_PROC_HAVE_CHRONO
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

	// merge
	pStart += snprintf(pStart, pEnd - pStart,
					"%s  %02d:%02d:%02d.%03d "
					"%c%d.%03d  %4d  %s  %-20s  ",
					timeBuf,
					int(durHours.count()), int(durMinutes.count()),
					int(durSecs.count()), int(durMillis.count()),
					diffMaxed ? '>' : '+', tDiffSec, tDiffMs,
					line, severityToStr(severity), function);
#else
	uint32_t now = millis(), t = now;

	uint32_t ms = t % 1000;
	t -= ms;
	t /= 1000;

	uint32_t sec = t % 60;
	t -= sec;
	t /= 60;

	uint32_t min = t % 60;
	t -= min;
	t /= 60;

	uint32_t hours = t % 24;
	t -= hours;
	t /= 24;

	// build diff
	uint32_t durDiffMs = now - tOld;
	long long tDiff = durDiffMs;
	int tDiffSec = int(tDiff / 1000);
	int tDiffMs = int(tDiff % 1000);
	bool diffMaxed = false;

	if (tDiffSec > cDiffSecMax)
	{
		tDiffSec = cDiffSecMax;
		tDiffMs = cDiffMsMax;

		diffMaxed = true;
	}

	// merge
	pStart += snprintf(pStart, pEnd - pStart,
					"%02d:%02d:%02d.%03d "
					"%c%d.%03d  %4d  %s  %-20s  ",
					int(hours), int(min),
					int(sec), int(ms),
					diffMaxed ? '>' : '+', tDiffSec, tDiffMs,
					line, severityToStr(severity), function);
#endif

	va_start(args, msg);
	pStart += vsnprintf(pStart, pEnd - pStart, msg, args);
	va_end(args);

	// Creating log entry
	if (severity <= levelLog)
	{
#if CONFIG_PROC_LOG_DIRECT
#ifdef _WIN32
		HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

		if (severity == 1)
		{
			SetConsoleTextAttribute(hConsole, 4);
			cerr << pBuf << "\r\n" << flush;
		}
		else
		if (severity == 2)
		{
			SetConsoleTextAttribute(hConsole, 6);
			cerr << pBuf << "\r\n" << flush;
		}
		else
			cout << pBuf << "\r\n" << flush;

		SetConsoleTextAttribute(hConsole, 7);
#else
		if (severity == 1)
			fprintf(stderr, "%s%s%s\r\n", red, pBuf, reset);
		else
		if (severity == 2)
			fprintf(stderr, "%s%s%s\r\n", yellow, pBuf, reset);
		else
			fprintf(stdout, "%s\r\n", pBuf);
#endif
#endif
		tOld = now;
	}

	if (pFctEntryLogCreate)
		pFctEntryLogCreate(severity, filename, function, line, code, pBuf, pStart - pBuf);


	delete[] pBuf;

	return code;
}

