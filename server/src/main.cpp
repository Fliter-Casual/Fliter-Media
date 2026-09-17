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

#include "../include/app/config.h"
#include "../include/server/http_server.h"
#include "rtc/peer_connection.h"

namespace {
// 全局停止标志
volatile sig_atomic_t g_stop_flag = 0;

void HandleSignal(int)
{
    g_stop_flag = 1;
}

// 检查 Content_Type 是否以指定前缀开头
bool ContentTypeStartsWith(const server::HttpRequest& req, const std::string& prefix) {
    auto it = req.headers.find("Content-Type");
    if (it == req.headers.end()) return false;
    std::string ct = it->second;
    // 去掉 charset 等参数
    auto semi = ct.find(';');
    if (semi != std::string::npos) ct = ct.substr(0, semi);
    return ct.rfind(prefix, 0) == 0;
}

// 从路径中提取 Peer ID (客户端的唯一标识符), 如从"/ice/abc123" 提取出 abc123
std::string ExtractPeerID(const std::string& path)
{
    auto pos = path.rfind('/');
    if(pos != std::string::npos && pos + 1 < path.size()) // (std::string::npos是未找到时赋给pos的值)
    {
        return path.substr(pos + 1);
    }
    return {};
}

// 生成随机 Peer ID
std::string GeneratePeerID()
{
    static std::atomic<int> counter{0};
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return "peer_" + std::to_string(now) + "_" + std::to_string(counter++);
}

// 解析 trickle-ice-sdpfrag 格式
std::vector<rtc_02::PendingCandidate> ParseTrickleIceSdpFrag(const std::string& body)
{
    std::vector<rtc_02::PendingCandidate> out;
    std::istringstream iss(body);
    std::string line;
    std::string current_mid;

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
            out.push_back(rtc_02::PendingCandidate{line, current_mid});
            continue;
        }
    }
    
    return out;
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
    rtc_02::PeerConnectionManager rtc_mgr(config);

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
            rtc_mgr.AddRemoteCandidate(cand.candidate, cand.mid);
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