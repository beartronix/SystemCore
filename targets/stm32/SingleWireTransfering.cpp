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

#include "SingleWireTransfering.h"
#include "SingleWire.h"

#define dForEach_ProcState(gen) \
		gen(StStart) \
		gen(StFlowControlRcvdWait) \
		gen(StContentOutSend) \
		gen(StContentOutSentWait) \
		gen(StContentIdInRcvdWait) \
		gen(StCmdRcvdWait) \

#define dGenProcStateEnum(s) s,
dProcessStateEnum(ProcState);

#if 0
#define dGenProcStateString(s) #s,
dProcessStateStr(ProcState);
#endif

using namespace std;

static char bufRx[2];
static uint8_t bufRxIdxIrq = 0; // used by IRQ only
static uint8_t bufRxIdxWritten = 0; // set by IRQ, cleared by main
static uint8_t bufTxPending = 0; // set by main, cleared by IRQ

uint8_t SingleWireTransfering::idStarted = 0;

SingleWireTransfering::SingleWireTransfering()
	: Processing("SingleWireTransfering")
	, mModeDebug(0)
	, mSendReady(false)
	, mValidBuf(0)
	, mpSend(NULL)
	, mpUser(NULL)
	, mContentIdOut(IdContentTaToScNone)
	, mValidIdTx(0)
	, mpDataTx(NULL)
	, mIdxRx(0)
	, mLenTx(0)
{
	mState = StStart;

	mBufInCmd[0] = 0;
	mBufOutProc[0] = 0;
	mBufOutLog[0] = 0;
	mBufOutCmd[0] = 0;
}

/* member functions */

void SingleWireTransfering::fctDataSendSet(FuncDataSend pFct, void *pUser)
{
	mpSend = pFct;
	mpUser = pUser;
}

void SingleWireTransfering::dataReceived(char *pData, size_t len)
{
	char *pDest = &bufRx[bufRxIdxIrq];

	*pDest = *pData;
	(void)len;

	bufRxIdxWritten = bufRxIdxIrq + 1;
	bufRxIdxIrq ^= 1;
}

void SingleWireTransfering::dataSent()
{
	bufTxPending = 0;
}

void SingleWireTransfering::logImmediateSend()
{
	mLenTx = sizeof(mBufOutLog);
	if (mLenTx < 3)
	{
		mValidBuf &= ~cBufValidOutLog;
		return;
	}

	mContentIdOut = IdContentTaToScLog;
	mpDataTx = mBufOutLog;
	mValidIdTx = cBufValidOutLog;

	contentOutSend();

	while (bufTxPending);
	mValidBuf &= ~cBufValidOutLog;
}

Success SingleWireTransfering::process()
{
	char data;

	switch (mState)
	{
	case StStart:

		if (!mpSend)
			return procErrLog(-1, "err");

		if (idStarted & cStartedTrans)
			return procErrLog(-1, "err");

		idStarted |= cStartedTrans;
		mSendReady = true;

		mState = StFlowControlRcvdWait;

		break;
	case StFlowControlRcvdWait:

		if (!byteReceived(&data))
			break;

		if (data == FlowSchedToTarget)
			mState = StContentIdInRcvdWait;

		if (!mModeDebug)
			break;

		if (data == FlowTargetToSched)
			mState = StContentOutSend;

		break;
	case StContentOutSend:

		if (mValidBuf & cBufValidOutCmd) // highest prio
		{
			mContentIdOut = IdContentTaToScCmd;
			mLenTx = sizeof(mBufOutCmd);
			mpDataTx = mBufOutCmd;
			mValidIdTx = cBufValidOutCmd;
		}
		else if (mValidBuf & cBufValidOutLog)
		{
			mContentIdOut = IdContentTaToScLog;
			mLenTx = sizeof(mBufOutLog);
			mpDataTx = mBufOutLog;
			mValidIdTx = cBufValidOutLog;
		}
		else if (mValidBuf & cBufValidOutProc) // lowest prio
		{
			mContentIdOut = IdContentTaToScProc;
			mLenTx = sizeof(mBufOutProc);
			mpDataTx = mBufOutProc;
			mValidIdTx = cBufValidOutProc;
		}
		else
			mLenTx = sizeof(mpDataTx[0]);

		// <content ID>...<zero byte><content end>
		if (mLenTx < 3)
			mContentIdOut = IdContentTaToScNone;

		contentOutSend();

		mState = StContentOutSentWait;

		break;
	case StContentOutSentWait:

		if (bufTxPending)
			break;

		mValidBuf &= ~mValidIdTx;

		mState = StFlowControlRcvdWait;

		break;
	case StContentIdInRcvdWait:

		if (!byteReceived(&data))
			break;

		if ((data != IdContentScToTaCmd) ||
				(mValidBuf & cBufValidInCmd))
		{
			mState = StFlowControlRcvdWait;
			break;
		}

		mIdxRx = 0;
		mBufInCmd[mIdxRx] = 0;

		mState = StCmdRcvdWait;

		break;
	case StCmdRcvdWait:

		if (!byteReceived(&data))
			break;

		if (data == FlowTargetToSched)
		{
			mBufInCmd[0] = 0;
			mState = StContentOutSend;
			break;
		}

		if (data == IdContentEnd)
		{
			mBufInCmd[mIdxRx] = 0;
			mValidBuf |= cBufValidInCmd;

			mState = StFlowControlRcvdWait;
			break;
		}

		if (mIdxRx >= sizeof(mBufInCmd) - 1)
			break;

		mBufInCmd[mIdxRx] = data;
		++mIdxRx;

		break;
	default:
		break;
	}

	return Pending;
}

void SingleWireTransfering::contentOutSend()
{
	mpDataTx[0] = mContentIdOut;

	if (mContentIdOut != IdContentTaToScNone)
	{
		// protect strlen(). Zero byte and 'content end' identifier byte must be stored at least
		mLenTx -= 2;
		mpDataTx[mLenTx] = 0;

		mLenTx = strlen(mpDataTx);

		mpDataTx[mLenTx] = 0;
		++mLenTx;

		mpDataTx[mLenTx] = IdContentEnd;
		++mLenTx;
	}

	bufTxPending = 1;
	mpSend(mpDataTx, mLenTx, mpUser);
}

uint8_t SingleWireTransfering::byteReceived(char *pData)
{
	uint8_t idxWr = bufRxIdxWritten;

	if (!idxWr)
		return 0;

	--idxWr;
	*pData = bufRx[idxWr];

	bufRxIdxWritten = 0;

	return 1;
}

void SingleWireTransfering::processInfo(char *pBuf, char *pBufEnd)
{
#if 0
	dInfo("State\t\t%s\n", ProcStateString[mState]);
#else
	(void)pBuf;
	(void)pBufEnd;
#endif
}

/* static functions */

