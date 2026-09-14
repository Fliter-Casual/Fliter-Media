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
    

} // main