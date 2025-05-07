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

uint8_t SingleWireTransfering::idStarted = 0;

SingleWireTransfering::SingleWireTransfering()
	: Processing("SingleWireTransfering")
	, mModeDebug(0)
	, mSyncedTransfer(false)
	, mSendReady(false)
	, mValidBuf(0)
	, mpSend(NULL)
	, mpUser(NULL)
	, mDataWriteEnabled(0) // used by IRQ only
	, mIdxBufDataWrite(0)  // used by IRQ only
	, mIdxBufDataRead(0)   // used by main only
	, mBufTxPending(0)     // set by main, cleared by user (main or IRQ)
	, mContentIdOut(IdContentTaToScNone)
	, mValidIdTx(0)
	, mpDataTx(NULL)
	, mLenTx(0)
{
	mState = StStart;

	mBufId[0] = 0;
	mBufId[1] = 0;

	mBufInCmd[0] = 0;
	mBufInCmd[sizeof(mBufInCmd) - 1] = 0;
	mBufOutProc[0] = 0;
	mBufOutProc[sizeof(mBufOutProc) - 1] = 0;
	mBufOutLog[0] = 0;
	mBufOutLog[sizeof(mBufOutLog) - 1] = 0;
	mBufOutCmd[0] = 0;
	mBufOutCmd[sizeof(mBufOutCmd) - 1] = 0;

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
	{
		if (*pData == FlowSchedToTarget ||
				*pData == FlowTargetToSched)
		{
			mBufId[0] = *pData;
			mBufId[1] = 0;

			mDataWriteEnabled = 0;
			continue;
		}

		if (*pData == IdContentScToTaCmd)
		{
			mBufId[1] = *pData;

			mIdxBufDataWrite = 0;
			mDataWriteEnabled = 1;
			continue;
		}

		if (!mDataWriteEnabled)
			continue; // process entire byte stream

		if (mIdxBufDataWrite >= sizeof(mBufInCmd) - 1)
		{
			mDataWriteEnabled = 0;
			continue;
		}

		mBufInCmd[mIdxBufDataWrite] = *pData;
		++mIdxBufDataWrite;

		if (*pData == IdContentEnd ||
				*pData == IdContentCut)
		{
			mDataWriteEnabled = 0;
			continue;
		}
	}
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

	mContentIdOut = IdContentTaToScLog;
	mpDataTx = mBufOutLog;
	mValidIdTx = cBufValidOutLog;

	contentOutSend();

	while (mBufTxPending);
	mValidBuf &= ~cBufValidOutLog;
}

Success SingleWireTransfering::process()
{
	Success success;
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

		data = mBufId[0];
		if (!data)
			break;
		mBufId[0] = 0;

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

		if (!mSyncedTransfer)
		{
			mState = StContentOutSentWait;
			break;
		}

		while (mBufTxPending);
		mValidBuf &= ~mValidIdTx;

		mState = StFlowControlRcvdWait;

		break;
	case StContentOutSentWait:

		if (mBufTxPending)
			break;

		mValidBuf &= ~mValidIdTx;

		mState = StFlowControlRcvdWait;

		break;
	case StContentIdInRcvdWait:

		data = mBufId[1];
		if (!data)
			break;
		mBufId[1] = 0;

		if ((data != IdContentScToTaCmd) ||
				(mValidBuf & cBufValidInCmd))
		{
			mState = StFlowControlRcvdWait;
			break;
		}

		mIdxBufDataRead = 0;
		mState = StCmdRcvdWait;

		break;
	case StCmdRcvdWait:

		success = dataInReceive();
		if (success == Pending)
			break;

		if (success == Positive)
			mValidBuf |= cBufValidInCmd;

		mState = StFlowControlRcvdWait;

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

	mBufTxPending = 1;
	mpSend(mpDataTx, mLenTx, mpUser);
}

Success SingleWireTransfering::dataInReceive()
{
	while (1)
	{
		if ((mIdxBufDataRead > mIdxBufDataWrite) ||
				(mIdxBufDataRead >= sizeof(mBufInCmd) - 1))
			return -1;

		if (mIdxBufDataRead == mIdxBufDataWrite)
		{
			if (mDataWriteEnabled)
				return Pending;

			return -1; // EOF
		}

		if (mBufInCmd[mIdxBufDataRead] != IdContentEnd)
		{
			++mIdxBufDataRead;
			continue;
		}

		mBufInCmd[mIdxBufDataRead] = 0;

		break;
	}

	return Positive;
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

