/*
  This file is part of the DSP-Crowd project
  https://www.dsp-crowd.com

  Author(s):
      - Johannes Natter, office@dsp-crowd.com
      - Stefan Egger, office@beartronics.at

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
#include <cstdio>
#include <cstdlib>

#include "SingleWireTransfering.h"

using namespace std;

 extern UART_HandleTypeDef huart1;

uint8_t SingleWireTransfering::buffRx[2];
uint8_t SingleWireTransfering::buffRxIdxIrq = 0; // used by IRQ only
uint8_t SingleWireTransfering::buffRxIdxWritten = 0; // set by IRQ, cleared by main
uint8_t SingleWireTransfering::buffTxPending = 0;

#define TX_ONLY 1


#define dForEach_ProcState(gen) \
		gen(StFlowControlByteRcv) \
		gen(StContentIdOutSend) \
		gen(StContentIdOutSendWait) \
		gen(StDataSend) \
		gen(StDataSendDoneWait) \
		gen(StContentIdInRcv) \
		gen(StCmdRcv) \

#define dGenProcStateEnum(s) s,
dProcessStateEnum(ProcState);

#if 1
#define dGenProcStateString(s) #s,
dProcessStateStr(ProcState);
#endif

enum SwtFlowControlBytes
{
	FlowMasterSlave = 0xF0,
	FlowSlaveMaster
};

enum SwtContentIdOutBytes
{
	ContentOutNone = 0x00,
	ContentOutLog = 0xC0,
	ContentOutCmd,
	ContentOutProc,
};

enum SwtContentIdInBytes
{
	ContentInCmd = 0xC0,
};

SingleWireTransfering::SingleWireTransfering()
	: Processing("SingleWireTransfering")
{
	mState = StFlowControlByteRcv;
}

SingleWireTransfering::~SingleWireTransfering()
{
}

/* member functions */
Success SingleWireTransfering::initialize()
{
	HAL_UART_Receive_IT(&huart1, SingleWireTransfering::buffRx, 1);

	// entryLogCreateSet(logEntryCreated);

	return Positive;
}

Success SingleWireTransfering::process()
{
	uint8_t data;

	switch (mState)
	{
	case StFlowControlByteRcv:
	{
#if !TX_ONLY
		if (!byteReceived(&data))
			return Pending;
		if (data == FlowMasterSlave)
			mState = StContentIdInRcv;

		if (data == FlowSlaveMaster)
			mState = StContentIdOutSend;
#else

		mState = StContentIdOutSend;
#endif
		break;
	}
	case StContentIdOutSend:

		if (pEnv->buffValid & dBuffValidOutCmd)
		{
			mValidIdTx = dBuffValidOutCmd;
			mpDataTx = pEnv->buffOutCmd;
			mContentTx = ContentOutCmd;
		}
		else if (pEnv->buffValid & dBuffValidOutLog)
		{
			mValidIdTx = dBuffValidOutLog;
			mpDataTx = pEnv->buffOutLog;
			mContentTx = ContentOutLog;
		}
		else if (pEnv->buffValid & dBuffValidOutProc)
		{
			mValidIdTx = dBuffValidOutProc;
			mpDataTx = pEnv->buffOutProc;
			mContentTx = ContentOutProc;
		}
		else
			mContentTx = ContentOutNone;

		// SingleWireTransfering::buffTxPending = 1;
		fwrite(&mContentTx, 1, 1, mpFile);// HAL_UART_Transmit_IT(&huart1, &mContentTx, 1);

		mState = StContentIdOutSendWait;

		break;
	case StContentIdOutSendWait:

		if (SingleWireTransfering::buffTxPending)
			return Pending;

#if !TX_ONLY
		if (mContentTx == ContentOutNone)
			mState = StFlowControlByteRcv;
		else
#endif
			mState = StDataSend;

		break;
	case StDataSend:

		// SingleWireTransfering::buffTxPending = 1;
		fwrite((uint8_t*)&mpDataTx, 1, 1, mpFile);
		// HAL_UART_Transmit_IT(&huart1, (uint8_t *)mpDataTx, strlen(mpDataTx) + 1);

		mState = StDataSendDoneWait;

		break;
	case StDataSendDoneWait:

		if (SingleWireTransfering::buffTxPending)
			return Pending;

		pEnv->buffValid &= ~mValidIdTx;

		mState = StFlowControlByteRcv;

		break;
#if !TX_ONLY
	case StContentIdInRcv:

		if (!byteReceived(&data))
			return Pending;

		mIdxRx = 0;

		if (data == ContentInCmd)
			mState = StCmdRcv;
		else
			mState = StFlowControlByteRcv;

		break;
	case StCmdRcv:

		if (!byteReceived(&data))
			return Pending;

		if (pEnv->buffValid & dBuffValidInCmd)
		{
			// Consumer not finished. Discard command
			mState = StFlowControlByteRcv;
			return Pending;
		}

		if (mIdxRx == sizeof(pEnv->buffInCmd) - 1)
			data = 0;

		pEnv->buffInCmd[mIdxRx] = data;
		++mIdxRx;

		if (!data)
		{
			pEnv->buffValid |= dBuffValidInCmd;
			mState = StFlowControlByteRcv;
		}

		break;
#endif
	default:
		break;
	}

	return Pending;
}

uint8_t SingleWireTransfering::byteReceived(uint8_t *pData)
{
	uint8_t idxWr = SingleWireTransfering::buffRxIdxWritten;

	if (!idxWr)
		return 0;

	--idxWr;
	*pData = SingleWireTransfering::buffRx[idxWr];

	SingleWireTransfering::buffRxIdxWritten = 0;

	return 1;
}

void SingleWireTransfering::processInfo(char *pBuf, char *pBufEnd)
{
	(void)pBuf;
	(void)pBufEnd;
}

// /* static functions */
// extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
// {
// 	SingleWireTransfering::buffRxIdxWritten = SingleWireTransfering::buffRxIdxIrq + 1;
// 	SingleWireTransfering::buffRxIdxIrq ^= 1;
// 	HAL_UART_Receive_IT(&huart1, &SingleWireTransfering::buffRx[SingleWireTransfering::buffRxIdxIrq], 1);

// 	//HAL_GPIO_WritePin(LED_Blue_GPIO_Port, LED_Blue_Pin, dLedSet);
// 	//HAL_GPIO_WritePin(LED_Blue_GPIO_Port, LED_Blue_Pin, dLedClear);
// }

// extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
// {
// 	(void)huart;

// 	SingleWireTransfering::buffTxPending = 0;

// 	//HAL_GPIO_WritePin(LED_Blue_GPIO_Port, LED_Blue_Pin, dLedSet);
// 	//HAL_GPIO_WritePin(LED_Blue_GPIO_Port, LED_Blue_Pin, dLedClear);

// 	//HAL_GPIO_WritePin(LED_Blue_GPIO_Port, LED_Blue_Pin, dLedSet);
// 	//HAL_GPIO_WritePin(LED_Blue_GPIO_Port, LED_Blue_Pin, dLedClear);
// }
