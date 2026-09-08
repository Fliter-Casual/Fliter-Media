#include "../../include/server/http_server.h"
#include <httplib.h>
#include <iostream>

namespace server
{
// Pimpl实现
struct HttpServer::Impl
{
    httplib::Server svr;
    std::thread thread;
};

HttpServer::HttpServer(const app::AppConfig& config)
    : config_(config)
    ,impl_(std::make_unique<Impl>()){

    }

HttpServer::~HttpServer()
{
    Stop();
}
// 回调注册函数（Callback Registration），用于将外部定义的处理函数注入到 HttpServer 类内部，以便在特定事件发生时由 HttpServer 调用
// 注册一个处理 WebRTC Offer SDP 的回调函数
void HttpServer::RegisterOfferHandler(Handler handler)
{
    std::lock_guard<std::mutex> lock(mtx_);
    offer_handler_ = std::move(handler);
}

// 注册一个处理 ICE Candidate 的回调函数
void HttpServer::RegisterIceHandler(Handler handler)
{
    std::lock_guard<std::mutex> lock(mtx_);
    ice_handler_ = std::move(handler);
}

void HttpServer::Start()
{
    std::lock_guard<std::mutex> lock(mtx_);
    if(running_) return;

    auto& svr = impl_->svr;

    // ========== CORS 配置 ==========
    // OPTIONS 预检请求（浏览器跨域会先发这个）
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, If-Match");
        res.set_header("Access-Control-Expose-Headers", "ETag, Location");
        res.status = 204;
    });

    // 让外部系统知道这个服务是否还活着、是否正常工作
    // ========== 健康检查 ==========
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("ok", "text/plain");
    });

    // ========== POST /offer - SDP Offer 处理 ==========
    svr.Post("/offer", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Expose-Headers", "ETag,Location");

        Handler handler;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            handler = offer_handler_;
        }

        if (!handler)
        {
            res.status = 501;
            //cpp-httplib 库中用于设置 HTTP 响应体内容的函数，决定服务器返回给客户端的实际数据和数据格式
            res.set_content("No handler registered", "text/plain");
            return;
        }

        // 转换为自定义的 HttpRequest
        HttpRequest http_req;
        http_req.method = req.method;
        http_req.path = req.path;
        http_req.body = req.body;
        // 复制请求头(温习范围for结构化绑定)
        for(const auto& [k,v] : req.headers)
        {
            http_req.headers[k] = v;
        }

        // 调用业务处理器
        auto http_res = handler(http_req);

        // 设置响应状态码
        res.status = http_res.status;
        for(const auto& [k,v] : http_res.headers)
        {
            res.set_header(k, v);
        }
        res.set_content(http_res.body, 
        http_res.headers.count("Content-Type") ? http_res.headers.at("Content-Type") : "text/plain");
    });

    // ========== PATCH /ice/:id - ICE 候选交换 ==========
    // 以下这段代码: 注册一个路由，用于处理 PATCH 请求，更新指定会话的 ICE 候选者(更新 Trickle ICE 候选者(结合 WHIP/WHEP 协议语义))
    svr.Patch(R"(/ice/(\w+))", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        Handler handler;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            handler = ice_handler_;
        }

        if(!handler)
        {
            res.status = 501;
            res.set_content("No handler registered", "text/plain");
                return;
        }

        HttpRequest http_req;
        http_req.method = req.method;
        http_req.path = req.path;
        http_req.body = req.body;
        for(const auto& [k,v] : req.headers)
        {
            http_req.headers[k] = v;
        }

        auto http_res = handler(http_req);
        res.status = http_res.status;
        for (const auto& [k,v] : http_res.headers)
        {
            res.set_header(k, v);
        }
        res.set_content(http_res.body, 
        http_res.headers.count("Content-Type") ? http_res.headers.at("Content-Type") : "text/plain");
    });

    // ========== 在后台线程启动服务器 ==========
    impl_->thread = std::thread([this]()
    {
        std::cout << "Starting HTTP server on " << config_.listen_host << ":" << config_.listen_port << std::endl;
        impl_->svr.listen(config_.listen_host.c_str(), config_.listen_port);
    });

    running_ = true;
}

void HttpServer::Stop()
{
    std::lock_guard<std::mutex> lock(mtx_);
    if(!running_) return;

    impl_->svr.stop();
    if (impl_->thread.joinable())
    {
        impl_->thread.join();
    }
    running_ = false;
    std::cout << "HTTP server stopped" << std::endl;
}

bool HttpServer::IsRunning() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return running_;
}

} // namespace server
