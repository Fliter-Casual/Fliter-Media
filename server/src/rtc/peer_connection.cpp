#include "../../include/rtc/peer_connection.h"
#include <iostream>
#include <future>


namespace rtc_02 {

namespace {
// 将 PeerConnection::State 转为字符串
std::string PcStateToString(::rtc::PeerConnection::State state) {
    switch (state){
        case ::rtc::PeerConnection::State::New: return "new";
        case ::rtc::PeerConnection::State::Connecting: return "connecting";
        case ::rtc::PeerConnection::State::Connected: return "connected";
        case ::rtc::PeerConnection::State::Disconnected: return "disconnected";
        case ::rtc::PeerConnection::State::Failed: return "failed";
        case ::rtc::PeerConnection::State::Closed: return "closed";
        default: return "unknown";
    }
}

// 修正 Answer SDP 中的 DTLS 协商方向属性，确保它与 Offer 端匹配，从而保证 DTLS 握手能正常建立
// offer 用 actpass(主动被动皆可), Answer 必须用 active(主动发起)
std::string FixDtlsSetupInAnswerSdp(std::string sdp)
{
    const std::string from = "a=setup:actpass";
    const std::string to = "a=setup:active";
    std::string::size_type pos = 0;
    while((pos = sdp.find(from, pos)) != std::string::npos)
    {
        sdp.replace(pos, from.size(), to);
        pos += to.size();
    }
    return sdp; //将 SDP 字符串中所有的 a=setup:actpass 替换为 a=setup:active
}

} // namespace

// 构造函数: 初始化 libdatachannel
PeerConnectionManager::PeerConnectionManager(const app::AppConfig& config)
: config_(config) 
{
    rtc_enabled_ = true;
    ::rtc::InitLogger(::rtc::LogLevel::Info);
    std::cout << "RTC libadatachannel initialized." << std::endl;
}

PeerConnectionManager::~PeerConnectionManager(){
    Close();
}

// 核心---HandleOffer 实现
std::string PeerConnectionManager::HandleOffer(const std::string& offer_sdp) {
    if(!rtc_enabled_)
        return {};  // 返回一个"空"的对象，具体返回什么类型取决于函数的返回值类型

    //  ========== 1. 配置 ICE 服务器 =========
    ::rtc::Configuration cfg;
    for(const auto& ice : config_.ice_servers)
    {
        ::rtc::IceServer server(ice.url);
        server.username = ice.username;
        server.password = ice.password;
        cfg.iceServers.push_back(server);
    }

    //  ========== 2. 创建 PeerConnection =========
    pc_ = std::make_unique<::rtc::PeerConnection>(cfg);

    // ========== 3. 准备 Answer Promise ==========
    // libdatachannel 生成 Answer 是异步的，需要通过 Promise/Future 等待
    auto answer_promise = std::make_shared<std::promise<std::string>>();
    auto answer_future = answer_promise->get_future();

    // ========== 4. 设置 onLocalDescription 回调 ==========(当本地 SDP 描述（Local Description）准备好/发生变化时，触发这个回调)
    pc_->onLocalDescription([answer_promise](const ::rtc::Description& desc) {
        try {
            answer_promise->set_value(desc.generateSdp());
        }
        catch (...) {
            // 忽略重复的 set_value 的异常
        }
    });

    // ========== 5. 设置 onLocalCandidate 回调 ==========(当本地 ICE 候选者（Local ICE Candidate）被收集到时，触发这个回调)
    pc_->onLocalCandidate([this](const ::rtc::Candidate& cand)
    {
        if(local_cand_cb_)
        {
            local_cand_cb_(std::string(cand), cand.mid()); // mid(媒体流标识)
        }
    });

    // ========== 6. 设置 onGatheringStateChange 回调 ====(当 ICE 候选者的收集状态（Gathering State）发生变化时，触发这个回调)
    pc_->onGatheringStateChange([this](::rtc::PeerConnection::GatheringState st) {
        if (gathering_cb_)
        {
            gathering_cb(st == ::rtc::PeerConnection::GatheringState::Complete);
        }
    });

    // ========== 7. 设置 onStateChange 回调 ==========
    pc_->onStateChange([this](::rtc::PeerConnection::State state) {
        std::cout << "RTC PeerConnection State: " << PcStateToString(state) << std::endl;
        if (pc_state_cb_) {
            pc_state_cb_(PcStateToString(state));
        }
    });

    // ========== 8. 设置 onTrack 回调 ==========
    pc_->onTrack([this](std::shared_ptr<::rtc::Track> track) {
        std::cout << "RTC Received track: mid=" << track->mid() 
        << ", type=" << track->description().type() << std::endl;
        // demo 先打印，后面再做转发
    });

    // ========= 9. 设置远端 offer ===============
    try
    {
        std::cout << "RTC Setting remote offer SDP:\n" << offer_sdp << std::endl;
        pc_->setRemoteDescription(::rtc::Description(offer_sdp, "offer"));
        pc_->setLocalDescription(); // 触发 Answer 生成
    }
    catch (const std::exception& e) 
    {
        std::cerr << "[RTC] Failed to set remote description: " << e.what() << std::endl;
        return {};
    }

//  ========== 10. 等待 Answer 生成 ==========
    auto status = answer_future.wait_for(std::chrono::seconds(3));
    if( status != std::future_status::ready)
    {
        std::cerr << "RTC Failed to create answer within timeout" << std::endl;
        return {};
    }

    auto answer_sdp = answer_future.get();

//  ========== 11. 等待 ICE 候选（可选） ==========
if ( config_.answer_wait_ice_ms > 0)
{
    std::cout << "RTC Waiting for ICE candidates for " 
                << config_.answer_wait_ice_ms << "ms" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(config_.answer_wait_ice_ms));

    // 重新生成 Answer, 此时包含了候选
}

    
}// 核心---HandleOffer 实现



} // namespace rtc_02