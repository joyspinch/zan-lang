# Sdk.Jd.Api.Club

> 源码: `stdlib/Sdk/Jd/Api/Club/ClubPopCommentreplySaveRequest.zan`, `stdlib/Sdk/Jd/Api/Club/JdClubApi.zan`


## ClubPopCommentreplySaveRequest (class)

<c>jingdong.club.pop.commentreply.save</c> 的请求。

- JdRequest req;

- public ClubPopCommentreplySaveRequest()

- ClubPopCommentreplySaveRequest CommentId(string commentId)
  - 设置 <c>commentId</c> 参数。

- ClubPopCommentreplySaveRequest Content(string content)
  - 设置 <c>content</c> 参数。

- ClubPopCommentreplySaveRequest ReplyId(string replyId)
  - 设置 <c>replyId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## ClubPopCommentreplySaveResponse (class)

<c>jingdong.club.pop.commentreply.save</c> 的响应。

- public string resultCode;

- public string resultMsg;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## JdClubApi (class)

jingdong.club.* 的强类型客户端。

- JdClient client;

- public JdClubApi(JdClient client)

- async ClubPopCommentreplySaveResponse PopCommentreplySaveAsync(ClubPopCommentreplySaveRequest request)
  - 执行 <c>jingdong.club.pop.commentreply.save</c>。
