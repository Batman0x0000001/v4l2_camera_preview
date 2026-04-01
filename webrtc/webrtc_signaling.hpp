#pragma once

#include"webrtc_bridge.h"

#include<functional>
#include<memory>
#include<mutex>
#include<string>
#include<vector>

namespace rtc{
    class WebSocket;
    class WebSocketServer;
}

class WebRtcSignalingServer
{
public:
    //using AnswerCallback =定义类型别名，等价于 typedef
    //std::function 可以存储任何可调用对象。函数指针做不到存储带捕获的 lambda
    using AnswerCallback = std::function<void(const std::string&)>;
    using CandidateCallback = std::function<void(const std::string&,const std::string&)>;
    using LogCallback = std::function<void(WebRtcBridgeLogLevel, const std::string&)>;
private:
    int m_Port = 9001;

    AnswerCallback m_OnAnswer;
    CandidateCallback m_OnCandidate;
    LogCallback m_OnLog;

    std::shared_ptr<rtc::WebSocketServer> m_Server;
    std::shared_ptr<rtc::WebSocket> m_Client;

    std::vector<std::string> m_PendingMessages;
    mutable std::mutex m_Mutex;
public:
    WebRtcSignalingServer  (int port,AnswerCallback onAnswer,CandidateCallback onCandidate,LogCallback onLog);
    ~WebRtcSignalingServer ();

    int Start();
    void Stop();

    void SendOffer(const std::string& sdp);
    void SendCandidate(const std::string& mid,const std::string& candidate);

    int Port()const;
    
private:
    void AttachClient(const std::shared_ptr<rtc::WebSocket>& client);
    void FlushPendingMessages();
    void QueueOrSend(const std::string& message);
    void HandleMessage(const std::string& message);

    void EmitLog(WebRtcBridgeLogLevel level,const std::string& message)const;

    /*
        static成员变量：整个类只有一份。普通成员变量每个对象有自己的一份。
        static成员函数：不需要对象实例，直接用类名调用。
            限制：static 成员函数内部不能访问 this，也不能访问非 static 成员变量，因为它不属于任何对象
    */
   
    static std::string BuildDescriptionMessage(const std::string& type,const std::string& sdp);
    static std::string BuildCandidateMessage(const std::string& mid,const std::string& candidate);
    static bool ParseCandidateMessage(const std::string& message,std::string& outMid,std::string& outCandidate);

};
