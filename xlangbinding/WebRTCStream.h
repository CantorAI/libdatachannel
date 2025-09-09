#pragma once
#include <rtc/rtc.hpp>
#include <deque>
#include <mutex>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Represents one encoded frame (audio or video)
struct EncodedFrame {
    std::vector<uint8_t> data;
    bool isKeyframe;
    uint64_t timestampUs;
};

// Represents one channel (audio or video)
struct MediaChannel {
    std::string id;                   // e.g. "video_main", "audio_eng"
    std::string kind;                 // "video" or "audio"
    std::string codec;                // e.g. "H264", "opus"
    std::deque<EncodedFrame> buffer;  // ring buffer
    std::mutex mutex;
    size_t maxFrames;
};

class WebRTCStream {
public:
    WebRTCStream();

    // Create new audio/video channel
    void addChannel(const std::string& id,
        const std::string& kind,
        const std::string& codec,
        size_t maxFrames = 200);

    // Push frame into channel
    void pushFrame(const std::string& channelId,
        const std::vector<uint8_t>& data,
        bool isKeyframe,
        uint64_t ts);

    // Create new PeerConnection for a client
    std::shared_ptr<rtc::PeerConnection> createPeer();

    // Handle signaling from browser
    void handleOffer(std::shared_ptr<rtc::PeerConnection> pc, const std::string& sdp);
    void handleCandidate(std::shared_ptr<rtc::PeerConnection> pc, const std::string& candidate);

    // Broadcast latest frame from each channel to all clients
    void broadcast();

    // Dummy hooks for signaling integration
    void onLocalDescription(const std::string& sdpType, const std::string& sdp);
    void onLocalCandidate(const std::string& candidate);

private:
	std::map<std::string, std::shared_ptr<MediaChannel>> channels;

    struct Client {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::map<std::string, std::shared_ptr<rtc::Track>> tracks; // channelId -> track
    };
    std::vector<Client> clients;
    std::mutex clientMutex;
};
