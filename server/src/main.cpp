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

}

// 解析 trickle-ice-sdpfrag 格式
std::vector<rtc::IceCandidate> ParseTrickleIceSdpFrag(const std::string& body)
{
    std::vector<rtc::IceCandidate> out;
    std::istringstream iss(body);
    std::string line;
    std::string current_mid;
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


    while (std::getline(iss, line)) {
        // 去掉 \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        
        // 去掉 a= 前缀
        if (line.rfind("a=", 0) == 0) line = line.substr(2);
        
        if (line == "end-of-candidates" || line == "a=end-of-candidates") continue;
        
        if (line.rfind("mid:", 0) == 0) {
            current_mid = line.substr(4);
            continue;
        }
        
        if (line.rfind("candidate:", 0) == 0) {
            out.push_back(rtc::IceCandidate{line, current_mid});
            continue;
        }
    }
    
    return out;
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
    // ========== 1. 解析命令行参数 ==========
    app::AppConfig config;

    for(int i=1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc)
        {
            try 
            {
                config.listen_port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
            }
            catch (...) {
                std::cerr << "Invalid port number." << std::endl;
                return 1;
            }
        }
    }

    std::cout << "========================================\n\n";
    std::cout << "==========   demo     ==================\n\n";
    std::cout << "========================================\n\n";

    // ========== 2. 创建 HTTP 服务器模块 ==========
    server::HttpServer http(config);
    rtc::PeerConnectionManager rtc_mgr(config);

    if(!rtc_mgr.HasRtc())
    {
       std::cerr << "RTC 功能不可用！请检查 libdatachannel 是否正确安装。\n";
        return 1; 
    }

    // ========== 3. 设置 RTC 回调 ==========
    rtc_mgr.SetLocalCandidateCallback([](const std::string& cand_line, const std::string& mid)
    {
        std::cout << "[MAIN] 本地 ICE 候选: mid=" << (mid.empty() ? "-" : mid) 
                  << ", candidate=" << cand_line << std::endl;
    });

    // ========== 4. 向 http 对象注册一个 Offer 处理函数 ==========
    http.RegisterOfferHandler([&rtc_mgr](const server::HttpRequest& req) -> server::HttpResponse {
        std::cout << "[HTTP] POST /offer 收到, Body 大小: " << req.body.size() << std::endl;

        //  检查 Content-Type
         if (!ContentTypeStartsWith(req, "application/sdp")) {
            return server::HttpResponse{415, "Content-Type 必须是 application/sdp\n", {}};
        }
        
        if (req.body.empty()) {
            return server::HttpResponse{400, "SDP Offer 不能为空\n", {}};
        }
        // 处理 Offer，生成 Answer
        std::string answer = rtc_mgr.HandleOffer(req.body);
        
        if (answer.empty()) {
            return server::HttpResponse{500, "生成 Answer 失败\n", {}};
        }
        
        // 返回 Answer
        std::string peer_id = GeneratePeerId();
        server::HttpResponse resp;
        resp.status = 201;
        resp.body = answer;
        resp.headers["Content-Type"] = "text/plain";  // 调试用，方便在 DevTools 查看
        resp.headers["Location"] = "/ice/" + peer_id;
        
        std::cout << "[HTTP] 返回 Answer，peer_id=" << peer_id << std::endl;
        return resp;
    });

    // PATCH /ice/:id 处理器
    http.RegisterIceHandler([&rtc_mgr](const server::HttpRequest& req) -> server::HttpResponse {
        std::string peer_id = ExtractPeerId(req.path);
        
        if (peer_id.empty()) {
            return server::HttpResponse{400, "无效的 Peer ID\n", {}};
        }
        std::cout << "[HTTP] PATCH /ice/" << peer_id << " 收到，Body 大小: " << req.body.size() << std::endl;
        if (!ContentTypeStartsWith(req, "application/trickle-ice-sdpfrag")) {
            return server::HttpResponse{415, "Content-Type 必须是 application/trickle-icesdpfrag\n", {}};
        }
        if (req.body.empty()) {
            // 空 PATCH 表示轮询或 end-of-candidates
            std::cout << "[HTTP] 收到空 ICE PATCH（轮询或结束）\n";
            return server::HttpResponse{204, "", {}};
        }
        // 解析候选
        auto candidates = ParseTrickleIceSdpFrag(req.body);
        
        if (candidates.empty()) {
            return server::HttpResponse{400, "无效的 trickle-ice-sdpfrag\n", {}};
        }
        // 添加到 PeerConnection
        for (const auto& cand : candidates) {
            rtc_mgr.AddIceCandidate(peer_id, cand.candidate, cand.mid);
        }
        return server::HttpResponse{204, "", {}};
    });

    // ========== 5. 启动 HTTP 服务器 ==========
    http.Start();

    // ========== 6. 等待信号 ==========
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

    while (g_stop_flag == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

     // ========== 7. 清理资源 ==========
    std::cout << "\n正在停止服务器...\n";
    http.Stop();
    rtc_mgr.Close();
    std::cout << "服务器已停止\n";
    return 0;


} // main