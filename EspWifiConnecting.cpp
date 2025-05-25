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

#include <chrono>
#include <esp_err.h>

#include "EspWifiConnecting.h"

#define dForEach_ProcState(gen) \
		gen(StStart) \
		gen(StConnectedWait) \
		gen(StMain) \

#define dGenProcStateEnum(s) s,
dProcessStateEnum(ProcState);

#if 0
#define dGenProcStateString(s) #s,
dProcessStateStr(ProcState);
#endif

using namespace std;
using namespace chrono;

const uint32_t cUpdateDelayMs = 200;

bool EspWifiConnecting::connected = false;

#define WIFI_FAIL_BIT		BIT1
#define WIFI_CONNECTED_BIT	BIT0

EspWifiConnecting::EspWifiConnecting()
	: Processing("EspWifiConnecting")
	, mpHostname("dspc-esp")
	, mpSsid(NULL)
	, mpPassword(NULL)
	, mpNetIf(NULL)
	, mStartMs(0)
	, mEventGroupWifi()
	, mCntRetryConn(0)
	, mRssi(0)
{
	mState = StStart;
}

/* member functions */

Success EspWifiConnecting::process()
{
	uint32_t curTimeMs = millis();
	uint32_t diffMs = curTimeMs - mStartMs;
	Success success;
#if 0
	dStateTrace;
#endif
	switch (mState)
	{
	case StStart:

		if (!mpHostname)
			return procErrLog(-1, "network hostname not set");

		if (!mpSsid)
			return procErrLog(-1, "WiFi SSID not set");

		if (!mpPassword)
			return procErrLog(-1, "WiFi password not set");

		success = wifiConfigure();
		if (success != Positive)
			return procErrLog(-1, "could not configure WiFi");

		procDbgLog("WiFi configured");

		mState = StConnectedWait;

		break;
	case StConnectedWait:

		if (!connected)
			break;

		procDbgLog("WiFi connected");

		mStartMs = curTimeMs;
		mState = StMain;

		break;
	case StMain:

		if (diffMs < cUpdateDelayMs)
			break;
		mStartMs = curTimeMs;

		//procDbgLog("WiFi disconnected");

		break;
	default:
		break;
	}

	return Pending;
}

/*
 * Literature
 * - https://freertos.org/Documentation/02-Kernel/04-API-references/12-Event-groups-or-flags/01-xEventGroupCreate
 * - https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/esp_event.html
 * - https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_netif.html
 */
Success EspWifiConnecting::wifiConfigure()
{
	esp_event_handler_instance_t hEventAnyId;
	esp_event_handler_instance_t hEventGotIp;
	wifi_init_config_t cfgInitWifi;
	esp_err_t res;

	mEventGroupWifi = xEventGroupCreate();

	res = esp_netif_init();
	if (res != ESP_OK)
		return procErrLog(-1, "could not init network interface: %s (0x%04x)",
							esp_err_to_name(res), res);

	res = esp_event_loop_create_default();
	if (res != ESP_OK)
		return procErrLog(-1, "could not create event loop: %s (0x%04x)",
							esp_err_to_name(res), res);

	mpNetIf = esp_netif_create_default_wifi_sta();
	if (!mpNetIf)
		return procErrLog(-1, "could not create default network interface");

	res = esp_netif_set_hostname(mpNetIf, mpHostname);
	if (res != ESP_OK)
		return procErrLog(-1, "could not set hostname: %s (0x%04x)",
							esp_err_to_name(res), res);

	cfgInitWifi = WIFI_INIT_CONFIG_DEFAULT();
	res = esp_wifi_init(&cfgInitWifi);
	if (res != ESP_OK)
		return procErrLog(-1, "could not init wifi configuration: %s (0x%04x)",
							esp_err_to_name(res), res);

	res = esp_event_handler_instance_register(WIFI_EVENT,
				ESP_EVENT_ANY_ID,
				&wifiChanged,
				this,
				&hEventAnyId);
	if (res != ESP_OK)
		return procErrLog(-1, "could not register event handler: %s (0x%04x)",
							esp_err_to_name(res), res);

	res = esp_event_handler_instance_register(IP_EVENT,
				IP_EVENT_STA_GOT_IP,
				&ipChanged,
				this,
				&hEventGotIp);
	if (res != ESP_OK)
		return procErrLog(-1, "could not register event handler: %s (0x%04x)",
							esp_err_to_name(res), res);

	res = esp_wifi_set_mode(WIFI_MODE_STA);
	if (res != ESP_OK)
		return procErrLog(-1, "could not set WiFi mode: %s (0x%04x)",
							esp_err_to_name(res), res);

	wifi_config_t cfgWifi;

	res = esp_wifi_get_config(WIFI_IF_STA, &cfgWifi);
	if (res != ESP_OK)
		return procErrLog(-1, "could not get WiFi configuration: %s (0x%04x)",
							esp_err_to_name(res), res);

	strncpy((char *)cfgWifi.sta.ssid, mpSsid, sizeof(cfgWifi.sta.ssid));
	strncpy((char *)cfgWifi.sta.password, mpPassword, sizeof(cfgWifi.sta.password));

	/* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
	 * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
	 * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
	 * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
	 */
	cfgWifi.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	cfgWifi.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

	memset(cfgWifi.sta.sae_h2e_identifier, 0, sizeof(cfgWifi.sta.sae_h2e_identifier));
	cfgWifi.sta.sae_h2e_identifier[0] = 1;

	res = esp_wifi_set_config(WIFI_IF_STA, &cfgWifi);
	if (res != ESP_OK)
		return procErrLog(-1, "could not set WiFi configuration: %s (0x%04x)",
							esp_err_to_name(res), res);

	res = esp_wifi_start();
	if (res != ESP_OK)
		return procErrLog(-1, "could not start WiFi: %s (0x%04x)",
							esp_err_to_name(res), res);

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(mEventGroupWifi,
						WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
						pdFALSE,
						pdFALSE,
						portMAX_DELAY);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT)
		procDbgLog("connected to SSID: %s", mpSsid);
	else if (bits & WIFI_FAIL_BIT)
		procWrnLog("could not connect to SSID: %s", mpSsid);
	else
		procWrnLog("unexpected event");

	return Positive;
}

void EspWifiConnecting::processInfo(char *pBuf, char *pBufEnd)
{
#if 1
	//dInfo("State\t\t\t%s\n", ProcStateString[mState]);
	dInfo("RSSI\t\t\t%ddBm\n", (int)mRssi);
#endif
}

/* static functions */

bool EspWifiConnecting::isOk()
{
	return connected;
}

void EspWifiConnecting::wifiChanged(void* arg, esp_event_base_t event_base,
						int32_t event_id, void* event_data)
{
	if (event_base != WIFI_EVENT)
	{
		wrnLog("invalid event type");
		return;
	}

	if (event_id == WIFI_EVENT_STA_START)
	{
		esp_wifi_connect();
		return;
	}

	if (event_id != WIFI_EVENT_STA_DISCONNECTED)
		return;

	wrnLog("disconnected from AP");

	EspWifiConnecting *pWifi = (EspWifiConnecting *)arg;

	if (pWifi->mCntRetryConn > 5)
	{
		xEventGroupSetBits(pWifi->mEventGroupWifi, WIFI_FAIL_BIT);
		return;
	}

	++pWifi->mCntRetryConn;
	dbgLog("retry to connect to the AP: %u", pWifi->mCntRetryConn);

	esp_wifi_connect();
}

void EspWifiConnecting::ipChanged(void *arg, esp_event_base_t event_base,
						int32_t event_id, void *event_data)
{
	if (event_base != IP_EVENT)
	{
		wrnLog("invalid event type");
		return;
	}

	if (event_id != IP_EVENT_STA_GOT_IP)
		return;

	ip_event_got_ip_t *pEvent = (ip_event_got_ip_t *)event_data;
	dbgLog("IP:      " IPSTR, IP2STR(&pEvent->ip_info.ip));
	dbgLog("Gateway: " IPSTR, IP2STR(&pEvent->ip_info.gw));
	dbgLog("Netmask: " IPSTR, IP2STR(&pEvent->ip_info.netmask));

	EspWifiConnecting *pWifi = (EspWifiConnecting *)arg;

	xEventGroupSetBits(pWifi->mEventGroupWifi, WIFI_CONNECTED_BIT);

	pWifi->mCntRetryConn = 0;
	connected = true;
}

uint32_t EspWifiConnecting::millis()
{
	auto now = steady_clock::now();
	auto nowMs = time_point_cast<milliseconds>(now);
	return (uint32_t)nowMs.time_since_epoch().count();
}

