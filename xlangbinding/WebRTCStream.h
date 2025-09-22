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
#include <rtc/rtc.hpp>
#include <deque>
#include <mutex>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "xlang.h"
#include "xpackage.h"


namespace X {
// Represents one encoded frame (audio or video)
struct EncodedFrame {
	std::vector<uint8_t> data;
	bool isKeyframe;
	uint64_t timestampUs;
};

// Represents one channel (audio or video)
struct MediaChannel {
	std::string id;                  // e.g. "video_main", "audio_eng"
	std::string kind;                // "video" or "audio"
	std::string codec;               // e.g. "H264", "opus"
	std::deque<EncodedFrame> buffer; // ring buffer
	std::mutex mutex;
	size_t maxFrames;

	 // cache SPS/PPS/IDR
	std::vector<uint8_t> sps;
	std::vector<uint8_t> pps;
	std::vector<uint8_t> idr;
	bool gotIdr = false;
};

class WebRTCStream {

public:
	BEGIN_PACKAGE(WebRTCStream)
		APISET().AddFunc<4>("AddChannel", &WebRTCStream::AddChannelAPI);
		APISET().AddFunc<4>("PushFrame", &WebRTCStream::PushFrameAPI);
		APISET().AddFunc<0>("CreatePeer", &WebRTCStream::CreatePeerAPI);
		APISET().AddFunc<2>("HandleOffer", &WebRTCStream::HandleOfferAPI);
	    APISET().AddFunc<2>("HandleOfferSync", &WebRTCStream::HandleOfferSyncAPI);
		APISET().AddFunc<2>("HandleCandidate", &WebRTCStream::HandleCandidateAPI);

		// Events (user subscribes in XLang)
		APISET().AddEvent("OnLocalDescription"); //Event 0
		APISET().AddEvent("OnLocalCandidate");//Event 1
	END_PACKAGE

	WebRTCStream() : running(true), worker(&WebRTCStream::loop, this) {}
	~WebRTCStream() {
		running = false;
		cv.notify_all();
		if (worker.joinable())
			worker.join();
	}

	// Create new audio/video channel
	void addChannel(const std::string &id, const std::string &kind, const std::string &codec,
	                size_t maxFrames = 200);

	// Push frame into channel
	void pushFrame(const std::string &channelId, const std::vector<uint8_t> &data, bool isKeyframe,
	               uint64_t ts);
	void pushFrame2(const std::string &channelId, const std::vector<uint8_t> &data, bool isKeyframe,
	               uint64_t ts);
	// Create new PeerConnection for a client
	std::shared_ptr<rtc::PeerConnection> createPeer2();
	std::shared_ptr<rtc::PeerConnection> createPeer();
	// Handle signaling from browser
	std::string WebRTCStream::handleOfferSync(std::shared_ptr<rtc::PeerConnection> pc,
	                                          const std::string &sdp); 
	std::string WebRTCStream::handleOfferSync2(std::shared_ptr<rtc::PeerConnection> pc,
	                                          const std::string &sdp); 
	void handleOffer(std::shared_ptr<rtc::PeerConnection> pc, const std::string &sdp);
	void handleCandidate(std::shared_ptr<rtc::PeerConnection> pc, const std::string &candidate);

	// Dummy hooks for signaling integration
	void onLocalDescription(const std::string &sdpType, const std::string &sdp);
	void onLocalCandidate(const std::string &candidate);

public: //APIS
	void AddChannelAPI(std::string id, std::string kind, std::string codec, int maxFrames) {
		addChannel(id, kind, codec, (size_t)maxFrames);
	}

	void PushFrameAPI(std::string channelId, X::Bin bin, bool isKeyframe, unsigned long long ts) {
		std::vector<uint8_t> data((uint8_t *)bin->Data(), (uint8_t *)bin->Data() + bin->Size());
		pushFrame(channelId, data, isKeyframe, ts);
	}

	unsigned long long CreatePeerAPI() {
		auto pc = createPeer();
		// wrap pointer as opaque object for XLang
		return (uintptr_t)pc.get();
	}
	std::string WebRTCStream::HandleOfferSyncAPI(unsigned long long pcPtr, std::string sdp) {
		auto pc = (rtc::PeerConnection *)pcPtr;
		return handleOfferSync(std::shared_ptr<rtc::PeerConnection>(pc, [](auto *) {}), sdp);
	}

	void HandleOfferAPI(unsigned long long pcPtr, std::string sdp) {
		auto pc = (rtc::PeerConnection *)pcPtr;
		handleOffer(std::shared_ptr<rtc::PeerConnection>(pc, [](auto *) {}), sdp);
	}

	void HandleCandidateAPI(unsigned long long pcPtr, std::string cand) {
		auto pc = (rtc::PeerConnection *)pcPtr;
		handleCandidate(std::shared_ptr<rtc::PeerConnection>(pc, [](auto *) {}), cand);
	}

private:
	std::atomic<bool> running;
	std::thread worker;
	std::condition_variable cv;
	std::mutex cvMutex;

	// Broadcast latest frame from each channel to all clients
	void broadcast();
	void broadcast2();
	void loop1() {
		while (running) {
			std::unique_lock<std::mutex> lock(cvMutex);
			cv.wait(lock, [this] { return !running || hasFramesReady(); });
			if (!running)
				break;

			broadcast();
		}
	}
	void loop2() {
		while (running) {
			std::unique_lock<std::mutex> lock(cvMutex);
			cv.wait(lock, [this] { return !running || hasFramesReady(); });
			if (!running)
				break;

			for (auto &kv : channels) {
				auto &ch = kv.second;
				EncodedFrame latest;
				{
					std::lock_guard<std::mutex> chlock(ch->mutex);
					if (!ch->buffer.empty())
						latest = ch->buffer.back();
				}
				if (!latest.data.empty()) {
					if (ch->kind == "data") {
						// 🚀 Send raw frame over DataChannel
						broadcastData(ch->id, latest.data);
					} else {
						// fallback: RTP-based media
						broadcast();
					}
				}
			}
		}
	}

	// Replace your current loop() with this FIFO version
	void loop();
	bool hasFramesReady() {
		for (auto &kv : channels) {
			auto &ch = kv.second;
			std::lock_guard<std::mutex> lock(ch->mutex);
			if (!ch->buffer.empty())
				return true;
		}
		return false;
	}
	bool broadcastData(const std::string &channelId, const std::vector<uint8_t> &data);

private:
	std::map<std::string, std::shared_ptr<MediaChannel>> channels;

	struct Client {
		std::shared_ptr<rtc::PeerConnection> pc;
		std::map<std::string, std::shared_ptr<rtc::Track>> tracks; // channelId -> track
		std::map<std::string, std::shared_ptr<rtc::DataChannel>> dataChannels; // data channels
	};
	std::vector<Client> clients;
	std::mutex clientMutex;
};
} // namespace X
