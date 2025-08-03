/*
  This file is part of the DSP-Crowd project
  https://www.dsp-crowd.com

  Author(s):
      - Johannes Natter, office@dsp-crowd.com

  File created on 30.01.2024

  Copyright (C) 2024, Johannes Natter

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

#ifndef ESP_WIFI_CONNECTING_H
#define ESP_WIFI_CONNECTING_H

#include <esp_event.h>
#include <esp_wifi.h>
#include <freertos/event_groups.h>

#include "Processing.h"

class EspWifiConnecting : public Processing
{

public:

	static EspWifiConnecting *create()
	{
		return new (std::nothrow) EspWifiConnecting;
	}

	// input

	void hostnameSet	(const char *pName)		{ mpHostname = pName; }
	void ssidSet		(const char *pSsid)		{ mpSsid = pSsid; }
	void passwordSet	(const char *pPassword)	{ mpPassword = pPassword; }

	// output

	static bool isOk();

protected:

	virtual ~EspWifiConnecting() {}

private:

	EspWifiConnecting();
	EspWifiConnecting(const EspWifiConnecting &)
		: Processing("")
		, mpHostname(""), mpSsid(NULL), mpPassword(NULL), mpNetIf(NULL)
		, mStarted(false), mStartMs(0), mEventGroupWifi()
		, mCntRetryConn(0), mRssi(0)
	{
		mState = 0;
	}
	EspWifiConnecting &operator=(const EspWifiConnecting &)
	{
		mpHostname = "";
		mpSsid = NULL;
		mpPassword = NULL;
		mpNetIf = NULL;

		mStarted = false;
		mStartMs = 0;
		mEventGroupWifi = {};

		mCntRetryConn = 0;
		mRssi = 0;

		mState = 0;

		return *this;
	}

	/*
	 * Naming of functions:  objectVerb()
	 * Example:              peerAdd()
	 */

	/* member functions */
	Success process();
	Success shutdown();
	void processInfo(char *pBuf, char *pBufEnd);

	Success wifiConfigure();
	void infoWifiUpdate();

	/* member variables */
	const char *mpHostname;
	const char *mpSsid;
	const char *mpPassword;
	esp_netif_t *mpNetIf;
	bool mStarted;
	uint32_t mStartMs;
	EventGroupHandle_t mEventGroupWifi;
	uint32_t mCntRetryConn;
	int8_t mRssi;

	/* static functions */
	static void wifiChanged(void *arg, esp_event_base_t event_base,
							int32_t event_id, void *event_data);
	static void ipChanged(void *arg, esp_event_base_t event_base,
							int32_t event_id, void *event_data);
	static uint32_t millis();

	/* static variables */
	static bool connected;

	/* constants */

};

#endif

