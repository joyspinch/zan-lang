# Sdk.Jd.Api.Club

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Club/ClubPopCommentreplySaveRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Club/JdClubApi.zan`


## ClubPopCommentreplySaveRequest (class)

- JdRequest req;

- public ClubPopCommentreplySaveRequest()

- ClubPopCommentreplySaveRequest CommentId(string commentId)

- ClubPopCommentreplySaveRequest Content(string content)

- ClubPopCommentreplySaveRequest ReplyId(string replyId)

- JdRequest Raw()


## ClubPopCommentreplySaveResponse (class)

- public string resultCode;

- public string resultMsg;

- public string Raw;


## JdClubApi (class)

- JdClient client;

- public JdClubApi(JdClient client)

- async ClubPopCommentreplySaveResponse PopCommentreplySaveAsync(ClubPopCommentreplySaveRequest request)
