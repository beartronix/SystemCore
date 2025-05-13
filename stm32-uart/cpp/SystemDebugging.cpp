/*
  This file is part of the DSP-Crowd project
  https://www.dsp-crowd.com

  Author(s):
      - Johannes Natter, office@dsp-crowd.com

  File created on 30.11.2019

  Copyright (C) 2019, Johannes Natter

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

#include <string.h>

#include "./SystemDebugging.h"
#include "env.h"
#include "util.h"

#define dForEach_ProcState(gen) \
	gen(StCmdRcvdWait) \
	gen(StCmdInterpret) \
	gen(StCmdSendStart) \
	gen(StCmdSentWait) \

#define dGenProcStateEnum(s) s,
dProcessStateEnum(ProcState);

#define CMD(x)		(!strncmp(pEnv->buffInCmd, x, strlen(x)))

#define dNumCmds		32
Command commands[dNumCmds] = {};

using namespace std;

SystemDebugging::SystemDebugging(Processing *pTreeRoot)
	: Processing("SystemDebugging")
	, mpTreeRoot(pTreeRoot)
{
	mState = StCmdRcvdWait;

	entryLogCreateSet(SystemDebugging::entryLogCreate);
}


bool SystemDebugging::cmdReg(const char *pId, CmdFunc pFunc)
{
	Command *pCmd = freeCmdStructGet();

	if (!pCmd)
	{
		errLog(-1, "Max registered commands reached");
		return false;
	}

	pCmd->id = pId;
	pCmd->func = pFunc;

	infLog("Registered command '%s'", pId);

	return true;
}

Command *SystemDebugging::freeCmdStructGet()
{
	Command *pCmd = commands;

	for (size_t i = 0; i < dNumCmds; ++i, ++pCmd)
	{
		if (pCmd->id && pCmd->func)
			continue;

		return pCmd;
	}

	return NULL;
}



/* member functions */
Success SystemDebugging::initialize()
{
	pEnv = new dNoThrow Environment;

	if (!mpTreeRoot)
		return procErrLog(0, -1, "tree root not set");

	return Positive;
}

Success SystemDebugging::process()
{
	uint32_t diffMs = millis() - mStartMs;
	commandInterpret();

	if (diffMs < 1000)
		return Pending;

	mStartMs = millis();
	procTreeSend();

	return Pending;
}

void SystemDebugging::commandInterpret()
{
	char *pBuf = pEnv->buffOutCmd;
	char *pBufEnd = pBuf + sizeof(pEnv->buffOutCmd);
	Command *pCmd = commands;

	switch (mState)
	{
	case StCmdRcvdWait: // fetch

		if (!(pEnv->buffValid & dBuffValidInCmd))
			break;

		mState = StCmdInterpret;

		break;
	case StCmdInterpret: // interpret/decode and execute

		if (CMD("aaaaa"))
		{
			pEnv->debugMode ^= 1;
			dInfo("Debug mode %d", pEnv->debugMode);
			mState = StCmdSendStart;
			break;
		}

		if (!pEnv->debugMode)
		{
			// don't answer
			pEnv->buffValid &= ~dBuffValidInCmd;
			mState = StCmdRcvdWait;

			break;
		}

		procInfLog("Received command: %s", pEnv->buffInCmd);

		*pEnv->buffOutCmd = 0;

		for (size_t i = 0; i < dNumCmds; ++i, ++pCmd)
		{
			if (!CMD(pCmd->id))
				continue;

			if (!pCmd->func)
				continue;

			const char *pArg = pEnv->buffInCmd + strlen(pCmd->id);

			if (*pArg)
				++pArg;

			pCmd->func(pArg, pBuf, pBufEnd);

			if (!*pEnv->buffOutCmd)
				dInfo("Done");

			mState = StCmdSendStart;
			break;
		}

		if (*pEnv->buffOutCmd)
			break;

		dInfo("Unknown command");
		mState = StCmdSendStart;

		break;
	case StCmdSendStart: // write back

		pEnv->buffValid |= dBuffValidOutCmd;
		mState = StCmdSentWait;

		break;
	case StCmdSentWait:

		if (pEnv->buffValid & dBuffValidOutCmd)
			break;

		pEnv->buffValid &= ~dBuffValidInCmd;
		mState = StCmdRcvdWait;

		break;
	default:
		break;
	}
}

void SystemDebugging::procTreeSend()
{
	if (!pEnv->debugMode)
		return; // minimize CPU load in production

	mpTreeRoot->processTreeStr(pEnv->buffOutProc, pEnv->buffOutProc + sizeof(pEnv->buffOutProc), true, true);

	fprintf(stdout, PROC_TREE_PREFIX "\033[2J\033[H%s\r\n", pEnv->buffOutProc);
	// add another \r\n to the existing so we can parse multiple lines until \r\n\r\n on the receiver side
}

void SystemDebugging::processInfo(char *pBuf, char *pBufEnd)
{
#if 0
	dInfo("Firmware\t\t%s\n", dFwVersion);
	dInfo("En High sens\t%d\n", HAL_GPIO_ReadPin(HighSensEn_GPIO_Port, HighSensEn_Pin));
	dInfo("En RX\t\t%d\n", HAL_GPIO_ReadPin(RxEn_GPIO_Port, RxEn_Pin));
#endif
}

int SystemDebugging::sLevelLog = 3;
void SystemDebugging::levelLogSet(int lvl)
{
	sLevelLog = lvl;
}

/* static functions */
void SystemDebugging::entryLogCreate(
		const int severity,
		const char *filename,
		const char *function,
		const int line,
		const int16_t code,
		const char *msg,
		const size_t len)
{
	const char* red = "\033[0;31m";
	const char* yellow = "\033[0;33m";
	const char* reset = "\033[37m";
#if CONFIG_PROC_HAVE_DRIVERS
	Guard lock(mtxLogEntries);
#endif
	(void)filename;
	(void)function;
	(void)line;
	(void)code;

	if (severity > sLevelLog)
		return;

	if (severity == 1)
		fprintf(stderr, LOG_PREFIX "%s%s%s\r\n", red, msg, reset);
	else
	if (severity == 2)
		fprintf(stderr, LOG_PREFIX "%s%s%s\r\n", yellow, msg, reset);
	else
		fprintf(stdout, LOG_PREFIX "%s\r\n", msg);
}
