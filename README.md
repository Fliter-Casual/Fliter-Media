# Fliter-Media [C++ WebRTC SFU（WHIP/WHEP）]
基于 libdatachannel 与 cpp-httplib，致力于实现一款支持 WHIP/WHEP 协议的轻量级 WebRTC SFU 服务器，用于多人音视频通话。
目前实现了一个最小化的 WebRTC 服务器，专注于 SDP 协商 和 ICE 候选交换，后续添加推拉流等功能。

## 目录结构

```
Fliter-Media/
├── server/                    # C++ 服务端
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── app/config.h       # 配置（端口、ICE 服务器）
│   │   ├── server/http_server.h
│   │   └── rtc/peer_connection.h
│   ├── src/
│   │   ├── main.cpp           # 入口
│   │   ├── server/http_server.cpp
│   │   └── rtc/peer_connection.cpp
│   └── build/                 # 编译输出目录
├── web/
│   └── test.html              # 浏览器测试页面
└── README.md                  
```

##  服务端启动

```bash
# 进入 server 目录
cd ~/Fliter-Media/server

# 创建并进入 build 目录
mkdir -p build && cd build

# 编译
cmake ..
make -j$(nproc) 
```
云服务器 CPU两核 内存2G 建议直接用 make 单核编译,否则容易CPU 打满 + 内存耗尽,导致云服务器编译卡死

```bash
# 在 build 目录下运行
./Fliter-Media

# 或指定端口，记得开放端口(防火墙)
./Fliter-Media --port 8090
```

后序打开网页（启动客户端），测试链接步骤如下图

启动服务端
![alt text](image.png)

启动客户端并建立连接
![alt text](image-1.png)

断开重连测试
![alt text](image.png)

SDP日志查看
![alt text](image-2.png)