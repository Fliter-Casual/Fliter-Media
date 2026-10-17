#include "../../include/server/http_server.h"
#include <httplib.h>
#include <iostream>

namespace server
{
// Pimpl (Pointer to Implementation) 实现 (指向实现的指针)

/**
 * 构造 HTTP 服务器实例
 *
 * 初始化内部实现对象（Impl），但不会启动监听。
 *
 * @param config 应用配置（监听地址/端口等）
 */
HttpServer::HttpServer(const app::AppConfig& config)
: config_(config)
, impl_(std::make_unique<Impl>())
{
}

/**
 * 析构 HTTP 服务器实例
 *
 * 确保后台监听线程被停止并回收（等价于调用 Stop()）。
 */
HttpServer::~HttpServer()
{
    Stop();
}


// 回调注册函数（Callback Registration），用于将外部定义的处理函数注入到 HttpServer 类内部，以便在特定事件发生时由 HttpServer 调用
// 注册一个处理 WebRTC Offer SDP 的回调函数
/**
 * 注册 POST /offer 的业务处理器
 *
 * @param handler 处理回调（会被移动保存）
 */
void HttpServer::RegisterWhipHandler(Handler handler) {
    std::lock_guard<std::mutex> lock(mtx_);
    whip_handler_ = std::move(handler);
}

// 注册一个处理 ICE Candidate 的回调函数
/**
 * 注册 PATCH /ice/{id} 的业务处理器
 *
 * @param handler 处理回调（会被移动保存）
 */
void HttpServer::RegisterIceHandler(Handler handler)
{
    std::lock_guard<std::mutex> lock(mtx_);
    ice_handler_ = std::move(handler);
}

void HttpServer::RegisterDeleteHandler(Handler handler) {
    std::lock_guard<std::mutex> lock(mtx_);
    delete_handler_ = std::move(handler);
}

/**
 * 启动 HTTP 服务器（后台线程 listen）
 *
 * 线程安全：内部使用互斥锁保护状态；重复调用为幂等
 */
void HttpServer::Start()
{
    std::lock_guard<std::mutex> lock(mtx_);
    if(running_) return;

    auto& svr = impl_->svr;

    // ========== CORS 配置 ==========
    // OPTIONS 预检请求（浏览器跨域会先发这个）
    // CORS 预检请求处理
    /**
     * 处理浏览器的 CORS 预检请求（OPTIONS）
     *
     * 说明：放行常用方法与头部，并返回 204（无内容）。
     */
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        // 新增 Authorization 头支持（用于鉴权）
        res.set_header("Access-Control-Allow-Headers", "Content-Type, If-Match, If-None-Match, Authorization");
        // 新增 ETag 等响应头暴露
        res.set_header("Access-Control-Expose-Headers", "ETag, Location, Accept-Patch, Link");
        res.set_header("Access-Control-Max-Age", "86400");
        res.status = 204;
    });

    // 让外部系统知道这个服务是否还活着、是否正常工作
    // ========== Get /health - 健康检查 ==========
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("ok", "text/plain");
    });

    // ================================================================
    // POST /whip - WHIP 推流入口
    // ================================================================
    /**
    * WHIP 核心端点
    * 
    * 客户端通过此端点发送 SDP Offer，服务端返回 SDP Answer
    * 同时创建推流会话并返回资源 URL
    */
    svr.Post("/whip", [this](const httplib::Request& req, httplib::Response& res) {
        // 设置 CORS 头
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Expose-Headers", "ETag,Location, Accept-Patch, Link");

        Handler handler;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            handler = whip_handler_;
        }

        if (!handler)
        {
            res.status = 501;
            //cpp-httplib 库中用于设置 HTTP 响应体内容的函数，决定服务器返回给客户端的实际数据和数据格式
            res.set_content("No handler registered", "text/plain"); // 表示纯文本
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
        res.set_content(http_res.body, http_res.headers.count("Content-Type") ? http_res.headers.at("Content-Type") : "text/plain");
    });

    // ================================================================
    // PATCH /whip/resource/:id - WHIP Trickle ICE
    // ================================================================
    /**
    * WHIP Trickle ICE 端点
    * 
    * 用于增量交换 ICE 候选，客户端和服务端都可以通过此端点发送候选
    */
    svr.Patch(R"(/whip/resource/([a-zA-Z0-9_\-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Expose-Headers", "ETag");
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
        if (!http_res.body.empty()) 
        {
            res.set_content(http_res.body,http_res.headers.count("Content-Type") ? http_res.headers.at("Content-Type") : "text/plain");
        }
    });

    // ================================================================
    // DELETE /whip/resource/:id - WHIP 终止会话
    // ================================================================
    /**
      * WHIP 会话终止端点
      * 
      * 客户端通过此端点主动终止推流会话
    */
    svr.Delete(R"(/whip/resource/([a-zA-Z0-9_\-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
 
        Handler handler;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            handler = delete_handler_;
        }
 
        if (!handler) {
            res.status = 501;
            res.set_content("WHIP DELETE handler not registered", "text/plain");
            return;
        }
 
        HttpRequest http_req;
        http_req.method = req.method;
        http_req.path = req.path;
        http_req.body = req.body;
        for(const auto& [k, v] : req.headers) {
            http_req.headers[k] = v;
        }
 
        auto http_res = handler(http_req);
        res.status = http_res.status;
        for(const auto& [k, v] : http_res.headers) {
            res.set_header(k, v);
        }
        if (!http_res.body.empty()) {
            res.set_content(http_res.body, "text/plain");
        }
    });


    // ========== 在后台线程启动服务器 ==========
     /**
     * 后台监听线程入口
     *
     * 说明：阻塞在 `listen()`，Stop() 会通过 `svr.stop()` 使其退出。
     */
    impl_->thread = std::thread([this]()
    {
        std::cout << "[http] Starting server on " << config_.listen_host << ":" << config_.listen_port << std::endl;
        impl_->svr.listen(config_.listen_host.c_str(), config_.listen_port);
    });

    running_ = true;
}

/**
 * 停止 HTTP 服务器并回收后台线程
 *
 * 线程安全：内部使用互斥锁保护状态；重复调用为幂等。
 */
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

/**
 * 查询服务器运行状态
 *
 * @return true 表示已启动且未停止
 */
bool HttpServer::Running() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return running_;
}

} // namespace server
