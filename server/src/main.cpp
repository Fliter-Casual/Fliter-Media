#include <chrono>
#include <csignal>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>
#include <atomic>

#include "../include/app/config.h"
#include "../include/server/http_server.h"
#include "rtc/peer_connection.h"

namespace {

// 全局停止标志
std::atomic<bool> g_stop_flag{false};

/**
 * 进程信号处理回调
 *
 * 收到 SIGINT/SIGTERM 后将停止标志置位，主循环会在下一次轮询时退出。
 *
 * @param signo 信号编号（本例中不区分具体信号）
 */
extern "C" void HandleSignal(int) {
    g_stop_flag = true; //让程序在收到终止信号时能够优雅退出，而不是被强制杀死
}

/**
 * 打印命令行用法说明
 *
 * @param argv0 程序名（通常为 argv[0]）
 */
void PrintUsage(const char* argv0) {
    std::cout << "Usage: " << argv0 << " [--port <port>]\n"
              << "\n"
              << "WebRTC , SDP 处理\n"
              << "\n"
              << "Options:\n"
              << "  --port <port>    HTTP 监听端口 (默认 8090)\n"
              << "  --help           显示帮助信息\n"
              << "\n"
              << "Environment:\n"
              << "  FLITER_MEDIA_PORT   HTTP 监听端口\n"
              << "  FLITER_MEDIA_ANSWER_WAIT_ICE_MS  等待 ICE 候选的时间(ms)\n";
}

/**
 * 解析命令行参数并写入配置
 *
 * 支持参数：
 * - `--port <port>`：覆盖监听端口
 * - `--help/-h`：打印用法并返回 false
 *
 * @param argc 参数数量
 * @param argv 参数数组
 * @param cfg 输出配置（会被就地修改）
 * @return true 表示解析成功且可继续运行；false 表示应退出（如 --help 或参数错误）
 */
bool ParseArgs(int argc, char** argv, app::AppConfig& cfg) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            PrintUsage(argv[0]);
            return false;
        }
        
        if (arg == "--port" && i + 1 < argc) {
            try {
                int p = std::stoi(argv[++i]);
                if (p > 0 && p <= 65535) {
                    cfg.listen_port = static_cast<std::uint16_t>(p);
                } else {
                    std::cerr << "无效端口号\n";
                    return false;
                }
            } catch (...) {
                std::cerr << "无效端口号\n";
                return false;
            }
            continue;
        }
        
        std::cerr << "未知参数: " << arg << "\n";
        PrintUsage(argv[0]);
        return false;
    }
    return true;
}

/**
 * 去除字符串两端空白字符
 *
 * @param s 输入字符串
 * @return 去除前后空白后的新字符串（不修改原字符串）
 */
std::string Trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

/**
 * 从请求路径提取 peer_id
 *
 * 期望路径格式为：/ice/{peer_id}
 *
 * @param path HTTP 请求路径
 * @return peer_id；若路径不匹配则返回空字符串
 */
std::string ExtractPeerId(const std::string& path) {
    // 路径格式：/ice/{peer_id}
    if (path.rfind("/ice/", 0) != 0) return "";
    return path.substr(5);  // 跳过 "/ice/"
}

/**
 * Trickle ICE 的解析结果（单条 ICE candidate + 关联的 mid）
 */
struct TrickleCandidate {
    std::string mid;
    std::string candidate;
};

/**
 * 解析 Trickle ICE SDP Fragment（`application/trickle-ice-sdpfrag`）
 *
 * 支持解析形如：
 * - `a=mid:0`
 * - `a=candidate:...`
 *
 * @param body HTTP 请求体（可能包含多行）
 * @return 解析出的候选列表；若 body 为空则返回空列表
 */
std::vector<TrickleCandidate> ParseTrickleIce(const std::string& body) {
    std::vector<TrickleCandidate> result;
    std::istringstream iss(body); // 输入参数
    std::string line; // 输出参数
    std::string current_mid;
    
    while (std::getline(iss, line))  // 逐行读取请求体
    {
        line = Trim(line);
        if (line.empty()) continue;
        
        // 去掉 "a=" 前缀
        if (line.rfind("a=", 0) == 0) {
            line = line.substr(2);
        }
        
        // 解析 mid
        if (line.rfind("mid:", 0) == 0) {
            current_mid = Trim(line.substr(4));
            continue;
        }
        
        // 解析 candidate
        if (line.rfind("candidate:", 0) == 0) {
            TrickleCandidate cand;
            cand.mid = current_mid;
            cand.candidate = line;
            result.push_back(cand);
        }
    }
    
    return result;
}

/**
 * 生成简单的 Peer ID
 *
 * 说明：仅用于示例（单进程内递增），不保证全局唯一。
 *
 * @return 类似 "peer_0" 的字符串
 */
std::string GeneratePeerId() {
    // 生成唯一递增的 ID 字符串
    static std::atomic<int> counter{0};
    return "peer_" + std::to_string(counter.fetch_add(1));
}

}// namespace 


int main(int argc,char** argv)
{

    // 加载配置
    app::AppConfig config;

    // 从环境变量读取配置
    if (const char* port = std::getenv("SPARK_MEDIA_PORT")) {
        try {
            int p = std::stoi(port);
            if (p > 0 && p <= 65535) {
                config.listen_port = static_cast<std::uint16_t>(p);
            }
        } catch (...) {}
    }

    if (const char* wait = std::getenv("SPARK_MEDIA_ANSWER_WAIT_ICE_MS")) {
        try {
            int w = std::stoi(wait);
            if (w >= 0 && w <= 30000) {
                config.answer_wait_ice_ms = w;
            }
        } catch (...) {}
    }

    // 解析命令行参数
    if (!ParseArgs(argc, argv, config)) {
        return 1;
    }

    // 初始化 RTC 管理器
    rtc::PeerConnectionManager rtc_mgr(config);
    
    // 初始化 HTTP 服务器
    server::HttpServer http(config);



    // ========== 注册 POST /offer 处理器 ==========
    /**
     * POST /offer 业务处理器
     *
     * 责任：
     * - 校验 Content-Type 与请求体
     * - 生成 peer_id 并调用 RTC 层 HandleOffer() 生成 Answer SDP
     * - 返回 201 + Answer（并通过 Location 暴露后续 ICE 交换地址）
     */
    http.RegisterOfferHandler([&rtc_mgr](const server::HttpRequest& req) -> server::HttpResponse {
        std::cout << "\n[main] 收到 POST /offer 请求" << std::endl;

        // 验证 Content-Type
        auto ct_it = req.headers.find("Content-Type");
        if (ct_it == req.headers.end()) {
            // 尝试小写
            for (const auto& [k, v] : req.headers) {
                if (k == "content-type") {
                    ct_it = req.headers.find(k);
                    break;
                }
            }
        }
        
        bool is_sdp = false;
        if (ct_it != req.headers.end()) {
            is_sdp = ct_it->second.find("application/sdp") != std::string::npos;
        }
        
        if (!is_sdp) {
            return server::HttpResponse{415, "Content-Type 必须是 application/sdp\n", {}};
        }
        
        if (req.body.empty()) {
            return server::HttpResponse{400, "请求体不能为空\n", {}};
        }

        // 生成 Peer ID
        std::string peer_id = GeneratePeerId();

        // 处理 Offer，生成 Answer
        std::string answer = rtc_mgr.HandleOffer(req.body, peer_id);
        
        if (answer.empty()) {
            return server::HttpResponse{500, "生成 Answer 失败\n", {}};
        }
        
        // 返回 Answer
        server::HttpResponse resp;
        resp.status = 201;
        resp.body = answer;
        resp.headers["Content-Type"] = "text/plain";  // 调试用，方便在 DevTools 查看
        resp.headers["Location"] = "/ice/" + peer_id;
        
        std::cout << "[main] 返回 Answer，peer_id=" << peer_id << std::endl;
        return resp;
    });

    // ========== 注册 PATCH /ice/{peer_id} 处理器 ==========
    /**
     * PATCH /ice/{peer_id} 业务处理器
     *
     * 责任：
     * - 从路径提取 peer_id
     * - 解析 Trickle ICE SDP Fragment 并调用 RTC 层 AddIceCandidate()
     * - 将本地候选以 `application/trickle-ice-sdpfrag` 格式返回给浏览器
     */
    http.RegisterIceHandler([&rtc_mgr](const server::HttpRequest& req) -> server::HttpResponse {
        std::string peer_id = ExtractPeerId(req.path);
        
        if (peer_id.empty()) {
            return server::HttpResponse{400, "无效路径\n", {}};
        }
        
        std::cout << "\n[main] 收到 PATCH /ice/" << peer_id << " 请求" << std::endl;

        
        // 解析候选
        auto candidates = ParseTrickleIce(req.body);
        
        if (candidates.empty() && !req.body.empty()) {
            return server::HttpResponse{400, "无效的 Trickle ICE 格式\n", {}};
        }

        // 添加到远端候选
        for (const auto& cand : candidates) {
            rtc_mgr.AddIceCandidate(peer_id, cand.candidate, cand.mid);
        }

        // 返回本地候选
        auto local_cands = rtc_mgr.GetLocalCandidates(peer_id);
        
        if (local_cands.empty()) {
            return server::HttpResponse{204, "", {}};
        }

        // 构造响应体
        std::string body;
        std::string last_mid;
        for (const auto& cand : local_cands) {
            if (!cand.mid.empty() && cand.mid != last_mid) {
                body += "a=mid:" + cand.mid + "\r\n";
                last_mid = cand.mid;
            }
            std::string line = Trim(cand.candidate);
            if (line.rfind("a=", 0) != 0) {
                line = "a=" + line;
            }
            body += line + "\r\n";
        }
        body += "a=end-of-candidates\r\n";

        server::HttpResponse resp;
        resp.status = 200;
        resp.body = body;
        resp.headers["Content-Type"] = "application/trickle-ice-sdpfrag";
        return resp;
    });

    // ==========  启动 HTTP 服务器 ==========
    http.Start();

    // ==========  等待信号(终止信号 ctrl+C/kill) ==========
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    std::cout << "\n========================================\n";
    std::cout << "服务器已启动: http://" << config.listen_host << ":" << config.listen_port << "\n";
    std::cout << "========================================\n";
    std::cout << "API 端点:\n";
    std::cout << "  POST /offer    - 发送 SDP Offer\n";
    std::cout << "  PATCH /ice/:id - ICE 候选交换\n";
    std::cout << "  GET /health    - 健康检查\n";
    std::cout << "========================================\n";
    std::cout << "按 Ctrl+C 停止服务器...\n\n";

    // 主循环
    while (!g_stop_flag ) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 停止服务器
    std::cout << "\n正在停止服务器...\n";
    http.Stop();
    rtc_mgr.Close();
    std::cout << "服务器已停止\n";
    return 0;


} // main