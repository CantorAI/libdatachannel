#include "WebRTCStream.h"
#include <iostream>

WebRTCStream::WebRTCStream() {}

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
}

std::shared_ptr<rtc::PeerConnection> WebRTCStream::createPeer() {
	rtc::Configuration config;
	auto pc = std::make_shared<rtc::PeerConnection>(config);

	Client client;
	client.pc = pc;

	for (auto &kv : channels) {
		auto &ch = kv.second;
		if (ch->kind == "video") {
			auto track =
			    pc->addTrack(rtc::Description::Video(ch->id, rtc::Description::Direction::SendOnly));
			client.tracks[ch->id] = track;
		} else if (ch->kind == "audio") {
			auto track =
			    pc->addTrack(rtc::Description::Audio(ch->id, rtc::Description::Direction::SendOnly));
			client.tracks[ch->id] = track;
		}
	}

	{
		std::lock_guard<std::mutex> lock(clientMutex);
		clients.push_back(std::move(client));
	}

	pc->onLocalDescription([this](rtc::Description desc) {
		onLocalDescription(desc.typeString(), std::string(desc));
	});
	pc->onLocalCandidate([this](rtc::Candidate cand) { onLocalCandidate(cand.candidate()); });

	return pc;
}

void WebRTCStream::handleOffer(std::shared_ptr<rtc::PeerConnection> pc,
    const std::string& sdp) {
    pc->setRemoteDescription(rtc::Description(sdp, "offer"));
    pc->createAnswer();
}

void WebRTCStream::handleCandidate(std::shared_ptr<rtc::PeerConnection> pc,
    const std::string& candidate) {
    pc->addRemoteCandidate(rtc::Candidate(candidate));
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
				// Explicit conversion from uint8_t → std::byte
				rtc::binary data(latest.data.size());
				std::transform(latest.data.begin(), latest.data.end(), data.begin(),
				               [](uint8_t b) { return static_cast<std::byte>(b); });

				auto it = client.tracks.find(ch->id);
				if (it != client.tracks.end()) {
					it->second->send(data);
				}
			}
		}
	}
}


// === Dummy hooks ===
void WebRTCStream::onLocalDescription(const std::string& sdpType,
    const std::string& sdp) {
    // Replace with REST response
    std::cout << "[Dummy] LocalDescription type=" << sdpType
        << " sdp=" << sdp.substr(0, 50) << "...\n";
}

void WebRTCStream::onLocalCandidate(const std::string& candidate) {
    // Replace with REST response
    std::cout << "[Dummy] LocalCandidate: " << candidate << "\n";
}
