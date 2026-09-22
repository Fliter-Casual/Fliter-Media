//  PeerConnection 管理
// 这是 WebRTC 的核心模块，负责：
// 1. 创建 rtc::PeerConnection
// 2. 处理 SDP Offer，生成 Answer
// 3. 添加远端 ICE 候选

/*
 * PeerConnection 管理器Manager
 * 
 * 核心功能：
 * - 创建和管理 WebRTC PeerConnection
 * - SDP Offer/Answer 协商
 * - ICE 候选收集和交换
 * - 连接状态监控
 * 
 * 重点：
 * -  SDP 协议格式
 * -  ICE 候选交换流程
 * -  libdatachannel 使用
 */


#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

#include "../app/config.h"
#include <rtc/rtc.hpp> // 报错说明系统中没有安装 libdatachannel（轻量级的webrtc替代品）？

namespace rtc{

// 前向声明
struct PeerContext; //PeerConnection 的上下文(保存状态和数据的结构)

// 待处理的 ICE 候选结构
struct IceCandidate {
    std::string candidate; // ICE 候选描述（格式：candidate:...）
    std::string mid; // 媒体标识符（用于关联到具体的 m= 行）
    // Media Stream Identification（媒体流标识）作用是将一个 ICE 候选者（Candidate） 与一个特定的媒体流(音频流/视频流)进行关联。
};

// PeerConnection 管理器
// 管理单个或多个 WebRTC PeerConnection 的生命周期
class PeerConnectionManager {
public:
    /**
     * 构造函数
     * @param config 应用配置
     */
    explicit PeerConnectionManager(const app::AppConfig& config);

    /**
     * 析构函数
     *
     * 说明：会尝试关闭并清理当前管理的所有 PeerConnection。
     * 线程安全：析构期间不应再并发调用本对象的其他方法。
     */
    ~PeerConnectionManager();

    /**
     * 处理 SDP Offer 并生成 Answer SDP
     * 
     * 流程：
     * 1. 创建 PeerConnection
     * 2. 设置远端 Offer
     * 3. 生成本地 Answer
     * 4. 等待 ICE 候选收集
     * 5. 返回完整的 Answer SDP
     * 
     * @param offer_sdp 客户端发送的 SDP Offer
     * @param peer_id 对端标识符（用于后续 ICE 候选交换）
     * @return SDP Answer（失败返回空字符串）
     */
    std::string HandleOffer(const std::string& offer_sdp, const std::string& peer_id);

    /**
    * 添加远端 ICE 候选
    * 
    * @param peer_id 对端标识符
    * @param candidate ICE 候选描述
    * @param mid 媒体标识符
    * @return 是否成功添加
    */
    bool AddIceCandidate(const std::string& peer_id, const std::string& candidate, const std::string& mid = "");

    /**
     * 获取本地 ICE 候选列表
     * 
     * @param peer_id 对端标识符
     * @return 本地收集到的 ICE 候选列表
     */
    std::vector<IceCandidate> GetLocalCandidates(const std::string& peer_id);
    
    /**
     * 关闭指定的 PeerConnection
     * 
     * @param peer_id 对端标识符
     * @return 是否成功关闭
     */
    bool ClosePeer(const std::string& peer_id);

    /**
     * 获取所有 Peer 的 ID 列表
     * 
     * @return 对端标识符列表
    */
    std::vector<std::string> GetPeerIds() const;
    
    /**
     * 检查 RTC 功能是否可用
     *
     * @return true 表示底层 WebRTC/`libdatachannel` 功能可用；false 表示不可用
     */
    bool HasRtc() const { return rtc_enabled_; }

private:
    app::AppConfig config_;
    bool rtc_enabled_{false};

    // PeerConnection 映射表
    std::unordered_map<std::string, std::shared_ptr<PeerContext>> peers_;
    mutable std::mutex mtx_;
};

}
