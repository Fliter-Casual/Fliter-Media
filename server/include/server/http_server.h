#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "../app/config.h"

//  前置声明 httplib::Server,避免在头文件中引入 httplib.h，减少编译依赖
namespace httplib
{
    class Server;
}

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
        explicit HttpServer(const app::AppConfig& config);// 构造函数, 禁止隐式类型转换
        ~HttpServer();

        void RegisterOfferHandler(Handler handler); // 注册 Offer 处理函数
        void RegisterIceHandler(Handler handler); // 注册 ICE 处理函数

        void Start();
        void Stop();
        bool Running() const;

    private:
        app::AppConfig config_;
        Handler offer_handler_;
        Handler ice_handler_;
        bool running_{false};
        mutable std::mutex mtx_;// mutable 允许在 const 成员函数中修改此成员（锁状态改变不影响逻辑 const）

        // Pimpl 模式，隐藏httplib实现细节
        // 这样，Impl 的完整定义（里面具体有哪些成员变量、成员函数）就可以放到 .cpp 文件中，完全隐藏起来
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

