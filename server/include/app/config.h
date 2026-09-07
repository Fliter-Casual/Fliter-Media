#pragma once 

#include <cstdint>
#include <string>
#include <vector>

namespace app
{
    // ICE 服务器配置信息(Server Configuration)
    struct IceServer
    {
        std::string uri;  // ICE 服务器地址(URI)，例如："turn:turn.example.com:3478" 或 "stun:stun.l.google.com:19302"
        std::string username;  // TURN 服务器认证凭证，STUN 协议不使用此字段
        std::string password;  // TURN 服务器认证密码，STUN 协议不使用此字段
    };

    // 应用配置
    struct AppConfig
    {
        // HTTP 监听配置
        std::string listen_host = "0.0.0.0";
        std::uint16_t listen_port = 8090;

        // ICE 服务器列表
        std::vector<IceServer> ice_servers{
            {"stun:stun.l.google.com:19302","",""},
            // 生产环境请配置自己的 TURN 服务器
            // {"turn:your-turn-server:3478?transport=udp", "username", "password"}
        };

        // Answer 中等待 ICE 候选的时间（毫秒）
        int answer_wait_ice_ms = 1500; // 等待一段时间可以让 Answer SDP 包含更多候选，减少 Trickle ICE 往返
    };
}