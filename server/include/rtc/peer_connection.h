//  PeerConnection 管理
// 这是 WebRTC 的核心模块，负责：
// 1. 创建 rtc::PeerConnection
// 2. 处理 SDP Offer，生成 Answer
// 3. 添加远端 ICE 候选

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../app/config.h"
#include <rtc/rtc.hpp> // 报错说明系统中没有安装 libdatachannel（轻量级的webrtc替代品）？

namespace rtc_02{

// 待处理的 ICE 候选
struct PendingCandidate {
    std::string candidate;
    std::string mid; // Media Stream Identification（媒体流标识）作用是将一个 ICE 候选者（Candidate） 与一个特定的 媒体流（Media Stream） 进行关联。
};

// PeerConnection 管理器
class PeerConnectionManager {
public:
    using PcStateCallback = std::function<void(const std::string& state)>; // peerconnection的对等连接状态
    using LocalCandidateCallback = std::function<void(const std::string& candidate_sdp_line, const std::string& mid)>;
    using GatheringStateCallback = std::function<void(bool complete)>;

    explicit PeerConnectionManager(const app::AppConfig& config);
    ~PeerConnectionManager();

    // 处理 Offer, 返回  Answer SDP
    std::string HandleOffer(const std::string& offer_sdp);

    // 添加远端 ICE 候选
    void AddRemoteCandidate(const std::string& candidate, const std::string& mid = "");

    // 关闭连接
    void Close();

    // 设置回调
    void SetPcStateCallback(PcStateCallback cb);
    void SetLocalCandidateCallback(LocalCandidateCallback cb);
    void SetGatheringStateCallback(GatheringStateCallback cb);

    bool HasRtc() const { return rtc_enabled_; }

private:
    app::AppConfig config_;
    bool rtc_enabled_{false};
    std::shared_ptr<::rtc::PeerConnection> pc_;
    std::vector<PendingCandidate> buffered_candidates_;
    mutable std::mutex candidates_mutex_;

    PcStateCallback pc_state_cb_;           // peerconnection的对等连接状态回调
    LocalCandidateCallback local_cand_cb_;  // 本地ICE候选者回调
    GatheringStateCallback gathering_cb_;  // ICE收集状态回调
};

}
