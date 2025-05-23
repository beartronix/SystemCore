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
		gen(StContentOutSent) \
		gen(StCmdReceive) \

#define dGenProcStateEnum(s) s,
dProcessStateEnum(ProcState);

#if 0
#define dGenProcStateString(s) #s,
dProcessStateStr(ProcState);
#endif

#define dForEach_RcvState(gen) \
		gen(StRcvStart) \
		gen(StRcvContentId) \
		gen(StRcvContentData) \

#define dGenRcvStateEnum(s) s,
dProcessStateEnum(RcvState);

#if 0
#define dGenRcvStateString(s) #s,
dProcessStateStr(RcvState);
#endif

using namespace std;

uint8_t SingleWireTransfering::idStarted = 0;

SingleWireTransfering::SingleWireTransfering()
	: Processing("SingleWireTransfering")
	, mModeDebug(0)
	, mSyncedTransfer(false)
	, mSendReady(false)
	, mValidBuf(0)
	, mpSend(NULL)
	, mpUser(NULL)
	, mIdxBufDataWrite(0)  // used by IRQ only
	, mBufTxPending(0)     // set by main, cleared by user (main or IRQ)
	, mContentIdOut(IdContentTaToScNone)
	, mValidIdTx(0)
	, mpDataTx(NULL)
	, mLenTx(0)
	, mStateRcv(StRcvStart)
{
	mState = StStart;

	mBufId[0] = 0;
	mBufId[1] = 0;

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
	size_t i = 0;

	for (; i < len; ++i, ++pData)
		byteProcess(*pData);
}

void SingleWireTransfering::dataSent()
{
	mBufTxPending = 0;
}

void SingleWireTransfering::logImmediateSend()
{
	mLenTx = sizeof(mBufOutLog);
	if (mLenTx < 3)
	{
		mValidBuf &= ~cBufValidOutLog;
		return;
	}

	// unsolicited
	char idFlow = FlowTargetToSched;

	mBufTxPending = 1;
	mpSend(&idFlow, sizeof(idFlow), mpUser);
	while (mBufTxPending);

	// content
	mContentIdOut = IdContentTaToScLog;
	mpDataTx = mBufOutLog;
	mValidIdTx = cBufValidOutLog;

	contentOutSend();
	while (mBufTxPending);

	// clear valid flag
	mValidBuf &= ~cBufValidOutLog;
}

Success SingleWireTransfering::process()
{
	bool cmdPending;
	bool ok;

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

		if (mBufId[1] == IdContentScToTaCmd)
		{
			mState = StCmdReceive;
			break;
		}

		if (!mModeDebug)
			break;

		if (mBufId[0] != FlowTargetToSched)
			break;
		mBufId[0] = 0;

		mState = StContentOutSend;

		break;
	case StContentOutSend:

		cmdPending = mValidBuf & cBufValidInCmd;

		if (mValidBuf & cBufValidOutCmd) // highest prio
		{
			mContentIdOut = IdContentTaToScCmd;
			mLenTx = sizeof(mBufOutCmd);
			mpDataTx = mBufOutCmd;
			mValidIdTx = cBufValidOutCmd;
		}
		else if (mValidBuf & cBufValidOutLog && !cmdPending)
		{
			mContentIdOut = IdContentTaToScLog;
			mLenTx = sizeof(mBufOutLog);
			mpDataTx = mBufOutLog;
			mValidIdTx = cBufValidOutLog;
		}
		else if (mValidBuf & cBufValidOutProc && !cmdPending) // lowest prio
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

		if (!mSyncedTransfer)
		{
			mState = StContentOutSentWait;
			break;
		}

		while (mBufTxPending);

		mState = StContentOutSent;

		break;
	case StContentOutSentWait:

		if (mBufTxPending)
			break;

		mState = StContentOutSent;

		break;
	case StContentOutSent:

		mValidBuf &= ~mValidIdTx;

		if (mValidIdTx == cBufValidOutCmd)
			mBufId[1] = 0;

		mState = StFlowControlRcvdWait;

		break;
	case StCmdReceive:

		ok = dataInTerminate();
		if (!ok)
		{
			mBufId[1] = 0;

			mState = StFlowControlRcvdWait;
			break;
		}

		mValidBuf |= cBufValidInCmd;

		mState = StFlowControlRcvdWait;

		break;
	default:
		break;
	}

	return Pending;
}

void SingleWireTransfering::byteProcess(uint8_t ch)
{
	switch (mStateRcv)
	{
	case StRcvStart:

		if (ch == FlowTargetToSched)
		{
			mBufId[0] = ch;
			break;
		}

		if (ch == FlowSchedToTarget)
		{
			mStateRcv = StRcvContentId;
			break;
		}

		break;
	case StRcvContentId:

		if (ch != IdContentScToTaCmd)
		{
			mStateRcv = StRcvStart;
			break;
		}

		if (mBufId[1]) // cmd not processed yet
		{
			mStateRcv = StRcvStart;
			break;
		}

		mIdxBufDataWrite = 0;

		mStateRcv = StRcvContentData;

		break;
	case StRcvContentData:

		mBufInCmd[mIdxBufDataWrite] = ch;
		++mIdxBufDataWrite;

		if (ch == IdContentEnd)
		{
			mBufId[1] = IdContentScToTaCmd;

			mStateRcv = StRcvStart;
			break;
		}

		if (mIdxBufDataWrite < sizeof(mBufInCmd) - 1)
			break;

		mStateRcv = StRcvStart;

		break;
	default:
		break;
	}
}

bool SingleWireTransfering::dataInTerminate()
{
	uint8_t i;

	i = 0;
	for (; i < sizeof(mBufInCmd) - 1; ++i)
	{
		if (mBufInCmd[i] != IdContentEnd)
			continue;

		mBufInCmd[i] = 0;

		return true;
	}

	return false;
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

	mBufTxPending = 1;
	mpSend(mpDataTx, mLenTx, mpUser);
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

