/*
Copyright (C) 2025 The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
You may not use this file except in compliance with the License.
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*/


#include "WebRTCStream.h"
#include <iostream>
#include <regex>
#include <future>

namespace X {


void WebRTCStream::addChannel(const std::string &id, const std::string &kind,
                              const std::string &codec, size_t maxFrames) {
	auto ch = std::make_shared<MediaChannel>();
	ch->id = id;
	ch->kind = kind;
	ch->codec = codec;
	ch->maxFrames = maxFrames;

	channels[id] = std::move(ch);
}


void WebRTCStream::pushFrame(const std::string& channelId,
    const std::vector<uint8_t>& data,
    bool isKeyframe,
    uint64_t ts) {
    auto it = channels.find(channelId);
    if (it == channels.end()) return;
    auto& ch = it->second;
    std::lock_guard<std::mutex> lock(ch->mutex);
    if (ch->buffer.size() >= ch->maxFrames) ch->buffer.pop_front();
    ch->buffer.push_back({ data, isKeyframe, ts });

	// signal worker to broadcast
	cv.notify_one();
}
std::shared_ptr<rtc::PeerConnection> WebRTCStream::createPeer() {
	rtc::Configuration config;
	auto pc = std::make_shared<rtc::PeerConnection>(config);

	Client client;
	client.pc = pc;

	{
		std::lock_guard<std::mutex> lock(clientMutex);
		clients.push_back(std::move(client));
	}

	pc->onStateChange([](rtc::PeerConnection::State state) {
		std::cout << "Peer state = " << (int)state << "\n";
	});

	pc->onLocalDescription([this](rtc::Description desc) {
		onLocalDescription(desc.typeString(), std::string(desc));
	});
	pc->onLocalCandidate([this](rtc::Candidate cand) { onLocalCandidate(cand.candidate()); });

	return pc;
}

std::shared_ptr<rtc::PeerConnection> WebRTCStream::createPeer2() {
	rtc::Configuration config;
	auto pc = std::make_shared<rtc::PeerConnection>(config);

	Client client;
	client.pc = pc;

	try {
		for (auto &kv : channels) {
			auto &ch = kv.second;

			std::shared_ptr<rtc::Track> track;
			if (ch->kind == "video") {
				rtc::Description::Video desc(ch->id, rtc::Description::Direction::SendOnly);
				//rtc::Description::Video desc("0", rtc::Description::Direction::SendOnly);
				//rtc::Description::Video desc("video", rtc::Description::Direction::SendOnly);

				// Pick one codec the browser supports. Chrome/Edge/Safari all support VP8,
				// and most support H264. You can add multiple if you want.
				//desc.addH264Codec(96); // PT=96, standard dynamic payload
				//desc.addH264Codec(102, "42e01f"); // PT=102, H.264 baseline profile
				desc.addH264Codec(109, "42e01f"); // baseline profile
				//desc.addH264Codec(96, "42e01f"); // baseline profile
				desc.addSSRC(123456, "video-send");
				auto track = pc->addTrack(desc);

				track->onOpen(
				    [id = ch->id]() { std::cout << "Video track " << id << " is now open\n"; });
				client.tracks[ch->id] = track;

			} else if (ch->kind == "audio") {
				//rtc::Description::Audio desc(ch->id, rtc::Description::Direction::SendOnly);
				rtc::Description::Audio desc("1", rtc::Description::Direction::SendOnly);
				desc.addOpusCodec(111); // PT=111 is the usual for Opus
				auto track = pc->addTrack(desc);
				track->onOpen(
				    [id = ch->id]() { std::cout << "Audio track " << id << " is now open\n"; });
				client.tracks[ch->id] = track;
			} else {
				auto dc = pc->createDataChannel(ch->id);
				continue;
			}

			if (track) {
				client.tracks[ch->id] = track;
			}
		}

		{
			std::lock_guard<std::mutex> lock(clientMutex);
			clients.push_back(std::move(client));
		}
		pc->onStateChange([this](rtc::PeerConnection::State state) {
			std::cout << "Peer state = " << (int)state << "\n";
			if (state == rtc::PeerConnection::State::Connected) {
				// safe to broadcast
			}
		});

		pc->onLocalDescription([this](rtc::Description desc) {
			try {
				onLocalDescription(desc.typeString(), std::string(desc));
			} catch (const std::exception &e) {
				std::cerr << "onLocalDescription error: " << e.what() << std::endl;
			}
		});
		pc->onLocalCandidate([this](rtc::Candidate cand) {
			try {
				onLocalCandidate(cand.candidate());
			} catch (const std::exception &e) {
				std::cerr << "onLocalCandidate error: " << e.what() << std::endl;
			}
		});
	} catch (const std::exception &e) {
		std::cerr << "createPeer error: " << e.what() << std::endl;
	}

	return pc;
}

		// Extract mids from offer SDP
std::string extractMid(const std::string &sdp, const std::string &media) {
	std::regex re("m=" + media + "[\\s\\S]*?a=mid:(\\S+)");
	std::smatch match;
	if (std::regex_search(sdp, match, re)) {
		return match[1].str();
	}
	return {};
};

std::string WebRTCStream::handleOfferSync(std::shared_ptr<rtc::PeerConnection> pc,
                                          const std::string &sdp) {
	std::promise<std::string> prom;
	auto fut = prom.get_future();

	try {

		std::string videoMid = extractMid(sdp, "video");
		std::string audioMid = extractMid(sdp, "audio");

		// Create tracks with mids from the offer
		for (auto &kv : channels) {
			auto &ch = kv.second;
			std::shared_ptr<rtc::Track> track;

			if (ch->kind == "video" && !videoMid.empty()) {
				rtc::Description::Video desc(videoMid, rtc::Description::Direction::SendOnly);
				desc.addH264Codec(109, "42e01f"); // Baseline profile
				desc.addSSRC(123456, "video-send");
				track = pc->addTrack(desc);
			} else if (ch->kind == "audio" && !audioMid.empty()) {
				rtc::Description::Audio desc(audioMid, rtc::Description::Direction::SendOnly);
				desc.addOpusCodec(111);
				track = pc->addTrack(desc);
			}

			if (track) {
				track->onOpen([id = ch->id]() { std::cout << "Track " << id << " is now open\n"; });
				std::lock_guard<std::mutex> lock(clientMutex);
				clients.back().tracks[ch->id] = track;
			}
		}

		// Now finish negotiation
		pc->onLocalDescription([&prom](rtc::Description desc) {
			try {
				prom.set_value(std::string(desc));
			} catch (...) {
				// ignore duplicate set_value
			}
		});

		pc->setRemoteDescription(rtc::Description(sdp, "offer"));
		pc->createAnswer();

	} catch (const std::exception &e) {
		std::cerr << "handleOfferSync error: " << e.what() << std::endl;
		return {};
	}

	return fut.get();
}

std::string WebRTCStream::handleOfferSync2(std::shared_ptr<rtc::PeerConnection> pc,
                                          const std::string &sdp) {
	std::promise<std::string> prom;
	auto fut = prom.get_future();

	try {
		pc->onLocalDescription([&prom](rtc::Description desc) {
			try {
				prom.set_value(std::string(desc));
			} catch (...) {
				// ignore multiple set_value calls
			}
		});

		pc->setRemoteDescription(rtc::Description(sdp, "offer"));
		pc->createAnswer();
	} catch (const std::exception &e) {
		std::cerr << "handleOfferSync error: " << e.what() << std::endl;
		return {};
	}

	return fut.get();
}

void WebRTCStream::handleOffer(std::shared_ptr<rtc::PeerConnection> pc, const std::string &sdp) {
	try {
		pc->setRemoteDescription(rtc::Description(sdp, "offer"));
		pc->createAnswer();
	} catch (const std::exception &e) {
		std::cerr << "handleOffer error: " << e.what() << std::endl;
	}
}

void WebRTCStream::handleCandidate(std::shared_ptr<rtc::PeerConnection> pc,
                                   const std::string &candidate) {
	try {
		pc->addRemoteCandidate(rtc::Candidate(candidate));
	} catch (const std::exception &e) {
		std::cerr << "handleCandidate error: " << e.what() << std::endl;
	}
}

void WebRTCStream::broadcast() {
	std::lock_guard<std::mutex> lock(clientMutex);

	for (auto &client : clients) {
		for (auto &kv : channels) {
			auto &ch = kv.second;
			EncodedFrame latest;
			{
				std::lock_guard<std::mutex> chlock(ch->mutex);
				if (!ch->buffer.empty())
					latest = ch->buffer.back();
			}
			if (!latest.data.empty()) {
				try {
					rtc::binary data(latest.data.size());
					std::transform(latest.data.begin(), latest.data.end(), data.begin(),
					               [](uint8_t b) { return static_cast<std::byte>(b); });

					auto it = client.tracks.find(ch->id);
					if (it != client.tracks.end()) {
						auto &track = it->second;
						if (track && track->isOpen()) {
							auto *rtp = reinterpret_cast<rtc::RtpHeader *>(data.data());
							rtp->setSsrc(123456);
							track->send(data);

						}
					}
				} catch (const std::exception &e) {
					std::cerr << "broadcast send error: " << e.what() << std::endl;
				}
			}
		}
	}
}

void WebRTCStream::onLocalDescription(const std::string &sdpType, const std::string &sdp) {
	X::ARGS args(2);
	args.push_back(sdpType);
	args.push_back(sdp);
	X::KWARGS kp_dummy;
	Fire(0, args, kp_dummy);

}

void WebRTCStream::onLocalCandidate(const std::string &candidate) {
	X::ARGS args(1);
	args.push_back(candidate);
	X::KWARGS kp_dummy;
	Fire(1, args, kp_dummy);
}

} // namespace X
