/*
Copyright (C) 2025 The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
You may not use this file except in compliance with the License.
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*/


#pragma once

#include "xlang.h"
#include "xpackage.h"
#include <rtc/rtc.hpp>
#include <string>
#include "singleton.h"
#include "WebRTCStream.h"

namespace X {
namespace LibDataChannel {

class DataChannel {
	BEGIN_PACKAGE(DataChannel)
	APISET().AddFunc<1>("send", &DataChannel::Send);
	APISET().AddFunc<0>("close", &DataChannel::Close);
	APISET().AddRTFunc<1>("onmessage", &DataChannel::OnMessageCallback);
	END_PACKAGE

public:
	DataChannel();
	void Set(std::shared_ptr<rtc::PeerConnection> pc,std::shared_ptr<rtc::DataChannel> dc);
	~DataChannel();

	bool Send(const std::string &message);
	void Close();
	void OnMessageCallback(X::XRuntime *rt, X::XObj *pContext, X::Value callback);
	X::Value LocalDescription();

private:
	std::shared_ptr<rtc::PeerConnection> m_pc;
	std::shared_ptr<rtc::DataChannel> m_dc;
	X::Value m_messageCallback;
	std::string m_cachedLocalSDP; // stored once negotiation fires
	std::string m_cachedSDPType;  // "offer" or "answer"
};

class Manager : public Singleton<Manager> {
	X::Value m_curModule;

	BEGIN_PACKAGE(Manager)
		// Create a new DataChannel with label and options
		APISET().AddRTFunc<2>("create", &Manager::Create);
		// Set default ICE servers/options
		APISET().AddRTFunc<1>("setDefaultOptions", &Manager::SetDefaultOptions);
		APISET().AddClass<0, DataChannel>("DataChannel");
	    APISET().AddClass<0, WebRTCStream>("WebRTCStream");
	END_PACKAGE

public:
	void SetModule(X::Value curModule) { m_curModule = curModule; }
	X::Value Create(X::XRuntime *rt, X::XObj *pContext, std::string label, X::Value options);
	void SetDefaultOptions(X::XRuntime *rt, X::XObj *pContext, X::Value options);
};

} // namespace LibDataChannel
} // namespace X
