/**
 * HTTP 服务器
 * 
 * 基于 cpp-httplib 实现的精简 HTTP 服务器
 * 提供 WebRTC 信令的 RESTful API
 * 
 * 支持的端点：
 * - POST /offer - 创建 PeerConnection，交换 SDP
 * - PATCH /ice/{id} - Trickle ICE 候选交换
 * - GET /health - 健康检查
 */

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "../app/config.h"
#include <httplib.h>


namespace server
{
    // HTTP 请求结构体
    struct HttpRequest
    {
        std::string method;
        std::string path;
        std::string body;
        std::unordered_map<std::string,std::string> headers;
    };
    // HTTP 响应结构体
    struct HttpResponse
    {
        int status = 200;
        std::string body;
        std::unordered_map<std::string,std::string> headers;
    };

    class HttpServer
    {
    public:
        using Handler = std::function<HttpResponse(const HttpRequest&)>;

      /**
      * 构造 HTTP 服务器
      *
      * 说明：仅保存配置与初始化内部实现，不会立即开始监听端口。
      *
      * @param config 应用配置（监听地址/端口等）
      */
        explicit HttpServer(const app::AppConfig& config);// 构造函数, 禁止隐式类型转换
        
        /**
       * 析构函数
       *
       * 说明：若服务器仍在运行，会尝试调用 Stop() 进行停止与线程回收。
       */
        ~HttpServer();

        /**
        * 注册 POST /offer 的业务处理器
        *
        * @param handler 处理 HTTP 请求并返回响应的回调；若为空则对应端点返回 501
        */
        void RegisterOfferHandler(Handler handler); // 注册 Offer 处理函数
        
        /**
        * 注册 PATCH /ice/{id} 的业务处理器
        *
        * @param handler 处理 HTTP 请求并返回响应的回调；若为空则对应端点返回 501
        */
        void RegisterIceHandler(Handler handler); // 注册 ICE 处理函数

        /**
        * 启动 HTTP 服务器
        *
        * 说明：启动服务器并开始监听配置中的地址和端口。
        */
        void Start();

        /**
        * 停止 HTTP 服务器
        *
        * 说明：停止服务器并回收(join)后台线程。
        */
        void Stop();

        /**
        * 检查 HTTP 服务器是否正在运行
        *
        * @return true 如果服务器正在运行
        * @return false 如果服务器未运行
        */
        bool Running() const;

    private:
        app::AppConfig config_;
        Handler offer_handler_;
        Handler ice_handler_;
        bool running_{false};
        mutable std::mutex mtx_;// mutable 允许在 const 成员函数中修改此成员（锁状态改变不影响逻辑 const）

        // Pimpl 模式，隐藏httplib实现细节
        // 这样，Impl 的完整定义（里面具体有哪些成员变量、成员函数）就可以放到 .cpp 文件中，完全隐藏起来
        struct Impl {
            httplib::Server svr;
            std::thread thread;
        };
        std::unique_ptr<Impl> impl_;
    };

} // namespace server

