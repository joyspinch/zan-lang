# System.Net.Sip

> 源码: `packages/Zan.Net/src/System/Net/Sip/SipClient.zan`, `packages/Zan.Net/src/System/Net/Sip/SipMessage.zan`, `packages/Zan.Net/src/System/Net/Sip/SdpMessage.zan`, `packages/Zan.Net/src/System/Net/Sip/SipCall.zan`, `packages/Zan.Net/src/System/Net/Sip/RtpPacket.zan`


## SipClient (class)

基于 UDP 的 SIP 用户代理客户端（RFC 3261），支持信令通信与语音呼叫（VoIP 打电话）。

- string host;
- int port;
- string localTag;
- int cseq;
- string localIp;

- SipClient(string host, int port, string tag)
- void SetLocalIp(string ip)
- string GetLocalIp()
- async SipMessage OptionsAsync()
- async SipMessage RegisterAsync(string user, int expires)
- async SipCall InviteAsync(string callerUser, string calleeUser, int localRtpPort)
- async SipCall InviteWithSdpAsync(string callerUser, string calleeUser, SdpMessage localSdp, int localRtpPort)
- async int SendAckAsync(UdpClient udp, SipCall call, string targetUri, string branch)
- async bool HangupAsync(SipCall call)
- async bool CancelAsync(SipCall call)
- void AddCommon(SipMessage req, string aor, string method)
- async SipMessage TransactAsync(SipMessage req)


## SipCall (class)

SIP 语音通话会话（Dialog），维护呼叫两端协商状态、通话时长与语音媒体通道。

- string callId;
- string callerUri;
- string calleeUri;
- string localTag;
- string remoteTag;
- int cseq;
- int state;
- int localRtpPort;
- string remoteRtpHost;
- int remoteRtpPort;
- SdpMessage localSdp;
- SdpMessage remoteSdp;
- RtpChannel rtpChannel;
- long connectedAt;
- int lastStatusCode;
- string lastReason;

- SipCall(string callId, string caller, string callee, string tag, int seq, int localRtpPort)
- string GetCallId()
- string GetCallerUri()
- string GetCalleeUri()
- string GetLocalTag()
- string GetRemoteTag()
- void SetRemoteTag(string tag)
- int GetCSeq()
- void SetCSeq(int seq)
- int NextCSeq()
- int GetState()
- void SetState(int s)
- bool IsActive()
- bool IsConnected()
- SdpMessage GetLocalSdp()
- void SetLocalSdp(SdpMessage sdp)
- SdpMessage GetRemoteSdp()
- void SetRemoteSdp(SdpMessage sdp)
- int GetLocalRtpPort()
- string GetRemoteRtpHost()
- int GetRemoteRtpPort()
- int GetLastStatusCode()
- void SetLastStatus(int code, string reason)
- string GetLastReason()
- int GetDurationSeconds()
- RtpChannel GetOrCreateRtpChannel()
- void Close()


## SipCallState (class)

SIP 呼叫状态常量：
- `static const int Idle = 0;` 空闲
- `static const int Calling = 1;` 呼叫已发出
- `static const int Ringing = 2;` 对方振铃中（180 Ringing）
- `static const int Connected = 3;` 通话已接通（200 OK + ACK 握手完成）
- `static const int Terminating = 4;` 正在挂断
- `static const int Terminated = 5;` 通话已结束 / 挂断


## SdpMessage (class)

会话描述协议（RFC 4566），负责音视频媒体格式、IP 与 RTP 端口协商。

- int version;
- string originUser;
- long sessionId;
- long sessionVersion;
- string connectionIp;
- string sessionName;
- string mediaType;
- int mediaPort;
- int payloadType;
- string codec;
- int sampleRate;
- List<string> attributes;

- static SdpMessage CreateAudio(string ip, int rtpPort)
- static SdpMessage CreateAudioWithCodec(string ip, int rtpPort, int pt, string codecName, int rate)
- string GetConnectionIp()
- int GetMediaPort()
- int GetPayloadType()
- string GetCodec()
- int GetSampleRate()
- List<string> GetAttributes()
- void AddAttribute(string attr)
- string Serialize()
- static SdpMessage Parse(string text)


## RtpPacket (class)

实时传输协议（RFC 3550）数据包，装载语音编码采样（如 PCMU / PCMA / G.711）。

- int version;
- bool marker;
- int payloadType;
- int sequenceNumber;
- long timestamp;
- long ssrc;
- byte[] payload;

- static RtpPacket Create(int pt, int seq, long ts, long ssrcVal, byte[] data)
- int GetVersion()
- bool GetMarker()
- void SetMarker(bool m)
- int GetPayloadType()
- void SetPayloadType(int pt)
- int GetSequenceNumber()
- void SetSequenceNumber(int seq)
- long GetTimestamp()
- void SetTimestamp(long ts)
- long GetSsrc()
- void SetSsrc(long s)
- byte[] GetPayload()
- void SetPayload(byte[] data)
- byte[] Encode()
- static RtpPacket Decode(byte[] buf, int offset, int length)


## RtpChannel (class)

轻量级 RTP 媒体通道，基于 UDP 传输语音采样帧。

- RtpChannel(int localPort, string remoteHost, int remotePort)
- int GetLocalPort()
- string GetRemoteHost()
- int GetRemotePort()
- async int SendRtpAsync(RtpPacket packet)
- async int SendAudioFrameAsync(byte[] pcmData, int payloadType, int sampleIncrement)
- RtpPacket RecvRtp(int maxBytes)
- void Close()


## SipMessage (class)

- bool isRequest;
- string method;
- string uri;
- int status;
- string reason;
- List<SipHeader> headers;
- string body;

- static SipMessage Request(string method, string uri)
- static SipMessage Response(int status, string reason)
- bool GetIsRequest()
- string GetMethod()
- string GetUri()
- int GetStatus()
- string GetReason()
- string GetBody()
- void SetBody(string b)
- SipMessage Add(string name, string headerValue)
- string Header(string name)
- static bool NameEq(string a, string b)
- string Serialize()
- static int ParsePositiveInt(string s)
- static SipMessage Parse(string data)
- static string ExtractTag(string headerValue)
- static string ExtractUri(string headerValue)


## SipHeader (class)

- string name;
- string value;
- SipHeader(string name, string value)
