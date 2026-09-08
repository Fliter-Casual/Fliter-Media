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

void HttpServer::RegisterOfferHandler(Handler handler)
{
    std::lock_guard<std::mutex> lock(mtx_);
    offer_handler_ = std::move(handler);
}

void HttpServer::RegisterIceHandler(Handler handler)
{
    std::lock_guard<std::mutex> lock(mtx_);
    ice_handler_ = std::move(handler);
}

void HttpServer::Start()
{
}

} // namespace server
