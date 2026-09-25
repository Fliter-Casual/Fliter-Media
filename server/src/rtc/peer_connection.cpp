/**
 * PeerConnection 管理器实现
 * 
 * 本文件是第二课的核心，演示：
 * 1. 如何使用 libdatachannel 创建 PeerConnection
 * 2. SDP Offer/Answer 协商流程
 * 3. ICE 候选收集和交换
 * 4. 连接状态管理
 */




#include "../../include/rtc/peer_connection.h"
#include <future>
#include <iostream>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <condition_variable>

// libdatachannel 头文件
#include <rtc/rtc.http>

namespace rtc {

/**
 * PeerConnection 上下文
 * 
 * 保存单个 PeerConnection 的所有状态
 */
struct PeerContext {
    std::string peer_id;                                  // Peer 标识符
    std::shared_ptr<::rtc::PeerConnection> pc;            // libdatachannel PC 对象
    std::vector<IceCandidate> local_candidates;           // 本地收集的 ICE 候选
    std::mutex candidates_mtx;                            // 保护候选列表的锁
    bool gathering_complete{false};                       // ICE 收集是否完成
    std::condition_variable gathering_cv;                 // 等待收集完成的条件变量
    std::mutex gathering_mtx;
};

namespace {
// 将 PeerConnection::State 转为字符串
std::string PcStateToString(::rtc::PeerConnection::State state) {
  switch (state) {
  case ::rtc::PeerConnection::State::New:
    return "new";
  case ::rtc::PeerConnection::State::Connecting:
    return "connecting";
  case ::rtc::PeerConnection::State::Connected:
    return "connected";
  case ::rtc::PeerConnection::State::Disconnected:
    return "disconnected";
  case ::rtc::PeerConnection::State::Failed:
    return "failed";
  case ::rtc::PeerConnection::State::Closed:
    return "closed";
  default:
    return "unknown";
  }
}

/**
 * 将 ICE 收集状态转换为字符串
 *
 * @param state ICE gathering 状态
 * @return 可读字符串（如 "complete"）
 */
std::string GatheringStateToString(::rtc::PeerConnection::GatheringState state) {
    switch (state) {
    case ::rtc::PeerConnection::GatheringState::New: return "new";
    case ::rtc::PeerConnection::GatheringState::InProgress: return "gathering";
    case ::rtc::PeerConnection::GatheringState::Complete: return "complete";
    default: return "unknown";
    }
}

// 修正 Answer SDP 中的 DTLS 协商方向属性（a=setup:actpass），确保它与 Offer 端匹配，从而保证 DTLS
// 握手能正常建立 offer 用 actpass(主动被动皆可), Answer 必须用 active(主动发起)
std::string FixDtlsSetupInAnswerSdp(std::string sdp) {
  const std::string from = "a=setup:actpass";
  const std::string to = "a=setup:active";
  std::string::size_type pos = 0;
  while ((pos = sdp.find(from, pos)) != std::string::npos) {
    sdp.replace(pos, from.size(), to);
    pos += to.size();
  }
  return sdp; // 将 SDP 字符串中所有的 a=setup:actpass 替换为 a=setup:active
}

} // namespace


// ============================================================================
// PeerConnectionManager 实现
// ============================================================================

/**
 * 构造 PeerConnectionManager
 *
 * 说明：初始化 libdatachannel 日志与内部配置；后续可通过 HandleOffer 创建 PeerConnection。
 *
 * @param config 应用配置（ICE server、端口范围、等待 ICE 时间等）
 */
PeerConnectionManager::PeerConnectionManager(const app::AppConfig &config)
    : config_(config)
    , rtc_enabled_(true)
{
  // 初始化 libdatachannel 日志
  ::rtc::InitLogger(::rtc::LogLevel::Info);
  std::cout << "[rtc] PeerConnectionManager initialized." << std::endl;
}

/**
 * 析构 PeerConnectionManager
 *
 * 说明：关闭并清理当前保存的所有 PeerConnection。
 */
PeerConnectionManager::~PeerConnectionManager() { 
    // 关闭所有的 PeerConnection
    std::lock_guard<std::mutex> lock(mtx_); //RAII 风格的互斥锁管理器：构造时自动加锁，析构时自动解锁
    for(auto& [id, ctx] : peers_)
    {
      if(ctx && ctx->pc)
      {
        try {
          ctx->pc->close();
        } catch (...) {}
      }
    }
    peers_.clear();
 }

/**
 * 处理 SDP Offer 并生成 Answer
 *
 * @param offer_sdp 浏览器侧发送的 SDP Offer
 * @param peer_id 对端标识（用于后续 Trickle ICE 交换）
 * @return 生成的 SDP Answer；失败返回空字符串
 */
std::string PeerConnectionManager::HandleOffer(const std::string &offer_sdp, const std::string &peer_id) {
  if (!rtc_enabled_)
  {
    std::cerr << "[rtc] RTC not enabled" << std::endl;
    return {}; // 返回一个"空"的对象，具体返回什么类型取决于函数的返回值类型
  }

  std::cout << "\n========== 处理 SDP Offer ==========" << std::endl;
  std::cout << "[rtc] Peer ID: " << peer_id << std::endl;
  std::cout << "[rtc] Offer SDP 长度: " << offer_sdp.size() << " bytes" << std::endl;
    
  // 打印 Offer SDP（查看 SDP 格式）
  std::cout << "\n--- Offer SDP 内容 ---" << std::endl;
  std::cout << offer_sdp << std::endl;
  std::cout << "--- Offer SDP 结束 ---\n" << std::endl;

  //  ========== 1. 配置 ICE 服务器 =========
  ::rtc::Configuration cfg;
  for (const auto &ice : config_.ice_servers) {
    ::rtc::IceServer server(ice.url);
    server.username = ice.username;
    server.password = ice.password;
    cfg.iceServers.push_back(server);
    std::cout << "[rtc] 添加 ICE 服务器: " << ice.url << std::endl;
  }

  //  ========== 2. 创建 PeerConnection =========
  auto ctx = std::make_shared<PeerContext>();
  ctx->peer_id = peer_id;
  ctx->pc = std::make_shared<::rtc::PeerConnection>(cfg);

  std::cout << "[rtc] 已创建 PeerConnection" << std::endl;

  // ========== 3. 用于等待 Answer 生成 ==========
  // libdatachannel 生成 Answer 是异步的（通过回调返回），用 Promise/Future 把异步结果转成同步等待
  auto answer_promise = std::make_shared<std::promise<std::string>>();
  auto answer_future = answer_promise->get_future();

  // ========== 4. 设置 onLocalDescription 回调 ==========
  // (当本地 SDP描述（Local Description）准备好/发生变化时，触发这个回调)
  /**
    * 本地描述生成回调（Answer ready）
    *
    * 说明：libdatachannel 在生成本地描述后触发，使用 promise 将 SDP 字符串传回主流程。
  */
  ctx->pc->onLocalDescription([answer_promise, peer_id](const ::rtc::Description &desc) {
    std::cout << "[rtc] 本地 Answer 已生成" << std::endl;
    try {
      answer_promise->set_value(desc.generateSdp());
    } catch (...) {
      // 忽略重复的 set_value 的异常
    }
  });

  // ========== 5. 设置 onLocalCandidate 回调 ==========
  // (当本地 ICE候选者（Local ICE Candidate）被收集到时，触发这个回调)

  // 本地 ICE 候选回调（Trickle ICE）
  /**
  * 本地 ICE candidate 回调（Trickle ICE）
  *
  * 说明：每收集到一个 candidate 就追加到 ctx->local_candidates，供 HTTP 层拉取返回给浏览器。
  */
  ctx->pc->onLocalCandidate([ctx](const ::rtc::Candidate &cand) {
    std::cout << "[rtc] 收集到本地 ICE 候选: " << std::string(cand) << std::endl;

    std::lock_guard<std::mutex> lock(ctx->candidates_mtx);
    IceCandidate ice_cand;
    ice_cand.candidate = std::string(cand);
    ice_cand.mid = cand.mid();
    ctx->local_candidates.push_back(ice_cand);

  });

  // ========== 6. 设置 onGatheringStateChange 回调 ====
  // (当 ICE候选者的收集状态（Gathering State）发生变化时，触发这个回调)
    /**
    * ICE gathering 状态变化回调
    *
    * 说明：当状态到达 Complete 时，唤醒等待线程（用于在 Answer 中尽可能包含候选）。
    */
  ctx->pc->onGatheringStateChange([ctx](::rtc::PeerConnection::GatheringState state) {
    std::cout << "[rtc] ICE 收集状态: " << GatheringStateToString(state) << std::endl;

    if (state == ::rtc::PeerConnection::GatheringState::Complete){
      std::lock_guard<std::mutex> lock(ctx->gathering_mtx);
      ctx->gathering_complete = true;
      ctx->gathering_cv.notify_all();
    }
  });

  // ========== 7. 设置 onStateChange 回调 ==========
  // 3.4 连接状态变化回调
  /**
  * PeerConnection 状态变化回调
  *
  * 说明：仅用于日志展示连接生命周期（new/connecting/connected/...）。
  */
  ctx->pc->onStateChange([peer_id](::rtc::PeerConnection::State state) {
    std::cout << "[rtc] 连接状态变化: " << PcStateToString(state)
              << " peer=" << peer_id << std::endl;
    }
  });

  // ========== 8. 设置 onTrack 回调 ==========
  // 3.5 媒体轨道回调（本课仅打印日志，不做转发）
  /**
    * 远端媒体轨道回调
    *
    * 说明：此处只做信息打印，便于理解 Track/SSRC 等概念；不做媒体转发与落盘。
  */
  ctx->pc->onTrack([peer_id](std::shared_ptr<::rtc::Track> track) {
    std::cout << "[rtc] 收到媒体轨道 ,RTC Received track: mid=" << track->mid()
              << ", 类型 type=" << track->description().type() 
              << ", (peer=" << peer_id << ")" << std::endl;
    // TODO:demo 先打印，后面再做转发
    // 打印媒体详细信息
    try {
            auto ssrcs = track->description().getSSRCs();
            std::cout << "[rtc] 轨道 SSRC 数量: " << ssrcs.size() << std::endl;
            for (const auto& ssrc : ssrcs) {
                std::cout << "[rtc]   SSRC: " << ssrc << std::endl;
            }
    } catch (...) {}
  });

  // ========= 9. 设置远端 offer ===============
  try {
    std::cout << "[rtc] 设置远端 Offer" << std::endl;
    ctx->pc->setRemoteDescription(::rtc::Description(offer_sdp, "offer"));

    // 触发生成本地 Answer
    std::cout << "[rtc] 生成本地 Answer" << std::endl;
    ctx->pc->setLocalDescription(); // 触发 Answer 生成
  } catch (const std::exception &e) {
    std::cerr << "[RTC] Failed to set remote description: " << e.what()
              << std::endl;
    return {};
  }

  //  ========== 10. 等待 Answer 生成 ==========
  auto status = answer_future.wait_for(std::chrono::seconds(3));
  if (status != std::future_status::ready) {
    std::cerr << "RTC Failed to create answer within timeout" << std::endl;
    return {};
  }

  auto answer_sdp = answer_future.get();

  //  ========== 11. 等待 ICE 候选（可选） ==========
  if (config_.answer_wait_ice_ms > 0) {
    std::cout << "[rtc] 等待 ICE 候选收集 (" << config_.answer_wait_ice_ms << "ms)..." << std::endl;

    std::unique_lock<std::mutex> lock(ctx->gathering_mtx);
    // 等待 ICE 收集完成或超时：超时后仍会继续返回当前可用的 Answer SDP
    ctx->gathering_cv.wait_for(lock, std::chrono::milliseconds(config_.answer_wait_ice_ms), [&ctx] {
      return ctx->gathering_complete;
    });

    // 重新生成 Answer, 此时包含了候选
    try {
      if (auto ld = pc_->localDescription(); ld.has_value()) {
        answer_sdp = ld->generateSdp();
      }
    } catch (const std::exception &e) {
      std::cerr << "[rtc] 重新生成 Answer 失败: " << e.what() << std::endl;
    }
  }
  // ========== 12. 修复 DTLS setup ==========
  answer_sdp = FixDtlsSetupInAnswerSdp(std::move(answer_sdp));
  std::cout << "[RTC] Generated answer SDP:\n" << answer_sdp << std::endl;

  // 保存 PeerContext
  {
    std::lock_guard<std::mutex> lock(mtx_);
    peers_[peer_id] = ctx;
  }
  std::cout << "========== Offer 处理完成 ==========\n" << std::endl;
  return answer_sdp;

  // ========== 13. 添加缓冲的候选 ==========
  // {
  //   std::lock_guard<std::mutex> lock(mtx_);
  //   for (const auto &cand : buffered_candidates_) {
  //     try {
  //       if (cand.mid.empty()) {
  //         pc_->addRemoteCandidate(::rtc::Candidate(cand.candidate));
  //       } else {
  //         pc_->addRemoteCandidate(::rtc::Candidate(cand.candidate, cand.mid));
  //       }
  //     } catch (const std::exception &e) {
  //       std::cerr << "RTC Failed to add buffered candidate: " << e.what()
  //                 << std::endl;
  //     }
  //   }
  //   buffered_candidates_.clear();
  // }
  // return answer_sdp;
  
} // 核心---HandleOffer 实现

// 其他方法实现

/**
 * 向指定 Peer 添加远端 ICE 候选
 *
 * @param peer_id 对端标识
 * @param candidate ICE candidate 行（`candidate:...`）
 * @param mid 媒体 mid（可为空）
 * @return true 表示添加成功；false 表示 peer 不存在或添加失败
 */
bool PeerConnectionManager::AddIceCandidate(const std::string& peer_id, const std::string &candidate,
                                               const std::string &mid) {
  std::lock_guard<std::mutex> lock(mtx_);

  auto it = peers_.find(peer_id);
  if (it == peers_.end()) {
    std::cerr << "[rtc] Peer " << peer_id << " not found." << std::endl;
    return false;
  }

  try {
       std::cout << "[rtc] 添加远端 ICE 候选: " << candidate 
                 << " (mid=" << mid << ", peer=" << peer_id << ")" << std::endl;
     
       if (mid.empty()) {
           it->second->pc->addRemoteCandidate(::rtc::Candidate(candidate));
      } else {
            it->second->pc->addRemoteCandidate(::rtc::Candidate(candidate, mid));
      }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[rtc] 添加 ICE 候选失败: " << e.what() << std::endl;
        return false;
    }
}

/**
 * 获取指定 Peer 已收集到的本地 ICE 候选
 *
 * @param peer_id 对端标识
 * @return 本地候选列表（若 peer 不存在返回空）
 */
std::vector<IceCandidate> PeerConnectionManager::GetLocalCandidates(const std::string& peer_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = peers_.find(peer_id);
    if (it == peers_.end()) {
        return {};
    }

    std::lock_guard<std::mutex> cand_lock(it->second->candidates_mtx);
    return it->second->local_candidates;
}

/**
 * 关闭并移除指定 Peer
 *
 * @param peer_id 对端标识
 * @return true 表示找到并关闭/移除成功；false 表示不存在
 */
bool PeerConnectionManager::ClosePeer(const std::string& peer_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = peers_.find(peer_id);
    if (it == peers_.end()) {
        return false;
    }

    try {
        if (it->second->pc) {
            it->second->pc->close();
        }
    } catch (...) {}

    peers_.erase(it);
    std::cout << "[rtc] 关闭 Peer: " << peer_id << std::endl;
    return true;
}

/**
 * 获取当前已创建的所有 Peer ID
 *
 * @return peer_id 列表
 */
std::vector<std::string> PeerConnectionManager::GetPeerIds() const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::string> ids;
    ids.reserve(peers_.size());
    for (const auto& [id, _] : peers_) {
        ids.push_back(id);
    }
    return ids;
}

} // namespace rtc

