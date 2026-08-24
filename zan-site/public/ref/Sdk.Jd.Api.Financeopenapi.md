# Sdk.Jd.Api.Financeopenapi

> 源码: `stdlib/Sdk/Jd/Api/Financeopenapi/FinanceopenapiAccountAllbalanceGetRequest.zan`, `stdlib/Sdk/Jd/Api/Financeopenapi/FinanceopenapiAccountAwardassignListRequest.zan`, `stdlib/Sdk/Jd/Api/Financeopenapi/FinanceopenapiAccountAwardrechargeListRequest.zan`, `stdlib/Sdk/Jd/Api/Financeopenapi/FinanceopenapiAccountCashassignListRequest.zan`, `stdlib/Sdk/Jd/Api/Financeopenapi/FinanceopenapiAccountCashrechargeListRequest.zan`, `stdlib/Sdk/Jd/Api/Financeopenapi/FinanceopenapiAccountCommissionrechargeListRequest.zan`, `stdlib/Sdk/Jd/Api/Financeopenapi/FinanceopenapiAccountVmsListRequest.zan`, `stdlib/Sdk/Jd/Api/Financeopenapi/FinanceopenapiCostListRequest.zan`, `stdlib/Sdk/Jd/Api/Financeopenapi/FinanceopenapiSubaccountAllbalanceGetRequest.zan`, `stdlib/Sdk/Jd/Api/Financeopenapi/FinanceopenapiSubaccountDaycostGetRequest.zan`, `stdlib/Sdk/Jd/Api/Financeopenapi/FinanceopenapiSubaccountFreezeListRequest.zan`, `stdlib/Sdk/Jd/Api/Financeopenapi/FinanceopenapiSubproductBrandbalanceGetRequest.zan`, `stdlib/Sdk/Jd/Api/Financeopenapi/FinanceopenapiSubproductJrwbalanceGetRequest.zan`, `stdlib/Sdk/Jd/Api/Financeopenapi/FinanceopenapiSubproductJtkbalanceGetRequest.zan`, `stdlib/Sdk/Jd/Api/Financeopenapi/FinanceopenapiSubproductZtbalanceGetRequest.zan`, `stdlib/Sdk/Jd/Api/Financeopenapi/JdFinanceopenapiApi.zan`


## FinanceopenapiAccountAllbalanceGetRequest (class)

<c>jingdong.financeopenapi.account.allbalance.get</c> 的请求。

- JdRequest req;

- public FinanceopenapiAccountAllbalanceGetRequest()

- JdRequest Raw()
  - 底层协议请求。


## FinanceopenapiAccountAllbalanceGetResponse (class)

<c>jingdong.financeopenapi.account.allbalance.get</c> 的响应。

- public OpenApiResDto data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## FinanceopenapiAccountAwardassignListRequest (class)

<c>jingdong.financeopenapi.account.awardassign.list</c> 的请求。

- JdRequest req;

- public FinanceopenapiAccountAwardassignListRequest()

- FinanceopenapiAccountAwardassignListRequest BeginDate(string beginDate)
  - 设置 <c>beginDate</c> 参数。

- FinanceopenapiAccountAwardassignListRequest EndDate(string endDate)
  - 设置 <c>endDate</c> 参数。

- FinanceopenapiAccountAwardassignListRequest Page(string page)
  - 设置 <c>page</c> 参数。

- FinanceopenapiAccountAwardassignListRequest PageSize(string pageSize)
  - 设置 <c>pageSize</c> 参数。

- FinanceopenapiAccountAwardassignListRequest SubPin(string subPin)
  - 设置 <c>subPin</c> 参数。

- FinanceopenapiAccountAwardassignListRequest AssignType(int assignType)
  - 设置 <c>assignType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## FinanceopenapiAccountAwardassignListResponse (class)

<c>jingdong.financeopenapi.account.awardassign.list</c> 的响应。

- public OpenApiResDto data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## FinanceopenapiAccountAwardrechargeListRequest (class)

<c>jingdong.financeopenapi.account.awardrecharge.list</c> 的请求。

- JdRequest req;

- public FinanceopenapiAccountAwardrechargeListRequest()

- FinanceopenapiAccountAwardrechargeListRequest BeginDate(string beginDate)
  - 设置 <c>beginDate</c> 参数。

- FinanceopenapiAccountAwardrechargeListRequest EndDate(string endDate)
  - 设置 <c>endDate</c> 参数。

- FinanceopenapiAccountAwardrechargeListRequest Page(string page)
  - 设置 <c>page</c> 参数。

- FinanceopenapiAccountAwardrechargeListRequest PageSize(string pageSize)
  - 设置 <c>pageSize</c> 参数。

- FinanceopenapiAccountAwardrechargeListRequest OrderType(int orderType)
  - 设置 <c>orderType</c> 参数。

- FinanceopenapiAccountAwardrechargeListRequest WebsiteType(int websiteType)
  - 设置 <c>websiteType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## FinanceopenapiAccountAwardrechargeListResponse (class)

<c>jingdong.financeopenapi.account.awardrecharge.list</c> 的响应。

- public OpenApiResDto data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## FinanceopenapiAccountCashassignListRequest (class)

<c>jingdong.financeopenapi.account.cashassign.list</c> 的请求。

- JdRequest req;

- public FinanceopenapiAccountCashassignListRequest()

- FinanceopenapiAccountCashassignListRequest BeginDate(string beginDate)
  - 设置 <c>beginDate</c> 参数。

- FinanceopenapiAccountCashassignListRequest EndDate(string endDate)
  - 设置 <c>endDate</c> 参数。

- FinanceopenapiAccountCashassignListRequest Page(string page)
  - 设置 <c>page</c> 参数。

- FinanceopenapiAccountCashassignListRequest PageSize(string pageSize)
  - 设置 <c>pageSize</c> 参数。

- FinanceopenapiAccountCashassignListRequest SubPin(string subPin)
  - 设置 <c>subPin</c> 参数。

- FinanceopenapiAccountCashassignListRequest AssignType(int assignType)
  - 设置 <c>assignType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## FinanceopenapiAccountCashassignListResponse (class)

<c>jingdong.financeopenapi.account.cashassign.list</c> 的响应。

- public OpenApiResDto data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## FinanceopenapiAccountCashrechargeListRequest (class)

<c>jingdong.financeopenapi.account.cashrecharge.list</c> 的请求。

- JdRequest req;

- public FinanceopenapiAccountCashrechargeListRequest()

- FinanceopenapiAccountCashrechargeListRequest BeginDate(string beginDate)
  - 设置 <c>beginDate</c> 参数。

- FinanceopenapiAccountCashrechargeListRequest EndDate(string endDate)
  - 设置 <c>endDate</c> 参数。

- FinanceopenapiAccountCashrechargeListRequest Page(string page)
  - 设置 <c>page</c> 参数。

- FinanceopenapiAccountCashrechargeListRequest PageSize(string pageSize)
  - 设置 <c>pageSize</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## FinanceopenapiAccountCashrechargeListResponse (class)

<c>jingdong.financeopenapi.account.cashrecharge.list</c> 的响应。

- public OpenApiResDto data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## FinanceopenapiAccountCommissionrechargeListRequest (class)

<c>jingdong.financeopenapi.account.commissionrecharge.list</c> 的请求。

- JdRequest req;

- public FinanceopenapiAccountCommissionrechargeListRequest()

- FinanceopenapiAccountCommissionrechargeListRequest BeginDate(string beginDate)
  - 设置 <c>beginDate</c> 参数。

- FinanceopenapiAccountCommissionrechargeListRequest EndDate(string endDate)
  - 设置 <c>endDate</c> 参数。

- FinanceopenapiAccountCommissionrechargeListRequest Page(string page)
  - 设置 <c>page</c> 参数。

- FinanceopenapiAccountCommissionrechargeListRequest PageSize(string pageSize)
  - 设置 <c>pageSize</c> 参数。

- FinanceopenapiAccountCommissionrechargeListRequest OrderType(int orderType)
  - 设置 <c>orderType</c> 参数。

- FinanceopenapiAccountCommissionrechargeListRequest WebsiteType(int websiteType)
  - 设置 <c>websiteType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## FinanceopenapiAccountCommissionrechargeListResponse (class)

<c>jingdong.financeopenapi.account.commissionrecharge.list</c> 的响应。

- public OpenApiResDto data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## FinanceopenapiAccountVmsListRequest (class)

<c>jingdong.financeopenapi.account.vms.list</c> 的请求。

- JdRequest req;

- public FinanceopenapiAccountVmsListRequest()

- FinanceopenapiAccountVmsListRequest BeginDate(string beginDate)
  - 设置 <c>beginDate</c> 参数。

- FinanceopenapiAccountVmsListRequest EndDate(string endDate)
  - 设置 <c>endDate</c> 参数。

- FinanceopenapiAccountVmsListRequest Page(string page)
  - 设置 <c>page</c> 参数。

- FinanceopenapiAccountVmsListRequest PageSize(string pageSize)
  - 设置 <c>pageSize</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## FinanceopenapiAccountVmsListResponse (class)

<c>jingdong.financeopenapi.account.vms.list</c> 的响应。

- public OpenApiResDto data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## FinanceopenapiCostListRequest (class)

<c>jingdong.financeopenapi.cost.list</c> 的请求。

- JdRequest req;

- public FinanceopenapiCostListRequest()

- FinanceopenapiCostListRequest BeginDate(string beginDate)
  - 设置 <c>beginDate</c> 参数。

- FinanceopenapiCostListRequest EndDate(string endDate)
  - 设置 <c>endDate</c> 参数。

- FinanceopenapiCostListRequest OrderTypes(string orderTypes)
  - 设置 <c>orderTypes</c> 参数。

- FinanceopenapiCostListRequest SubPin(string subPin)
  - 设置 <c>subPin</c> 参数。

- FinanceopenapiCostListRequest MoneyType(int moneyType)
  - 设置 <c>moneyType</c> 参数。

- FinanceopenapiCostListRequest PageNo(int pageNo)
  - 设置 <c>pageNo</c> 参数。

- FinanceopenapiCostListRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## FinanceopenapiCostListResponse (class)

<c>jingdong.financeopenapi.cost.list</c> 的响应。

- public OpenApiResDto data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## FinanceopenapiSubaccountAllbalanceGetRequest (class)

<c>jingdong.financeopenapi.subaccount.allbalance.get</c> 的请求。

- JdRequest req;

- public FinanceopenapiSubaccountAllbalanceGetRequest()

- FinanceopenapiSubaccountAllbalanceGetRequest SubPin(string subPin)
  - 设置 <c>subPin</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## FinanceopenapiSubaccountAllbalanceGetResponse (class)

<c>jingdong.financeopenapi.subaccount.allbalance.get</c> 的响应。

- public OpenApiResDto data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## FinanceopenapiSubaccountDaycostGetRequest (class)

<c>jingdong.financeopenapi.subaccount.daycost.get</c> 的请求。

- JdRequest req;

- public FinanceopenapiSubaccountDaycostGetRequest()

- FinanceopenapiSubaccountDaycostGetRequest SubPin(string subPin)
  - 设置 <c>subPin</c> 参数。

- FinanceopenapiSubaccountDaycostGetRequest Showday(int showday)
  - 设置 <c>showday</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## FinanceopenapiSubaccountDaycostGetResponse (class)

<c>jingdong.financeopenapi.subaccount.daycost.get</c> 的响应。

- public OpenApiResDto data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## FinanceopenapiSubaccountFreezeListRequest (class)

<c>jingdong.financeopenapi.subaccount.freeze.list</c> 的请求。

- JdRequest req;

- public FinanceopenapiSubaccountFreezeListRequest()

- FinanceopenapiSubaccountFreezeListRequest BeginDate(string beginDate)
  - 设置 <c>beginDate</c> 参数。

- FinanceopenapiSubaccountFreezeListRequest EndDate(string endDate)
  - 设置 <c>endDate</c> 参数。

- FinanceopenapiSubaccountFreezeListRequest Page(string page)
  - 设置 <c>page</c> 参数。

- FinanceopenapiSubaccountFreezeListRequest PageSize(string pageSize)
  - 设置 <c>pageSize</c> 参数。

- FinanceopenapiSubaccountFreezeListRequest SubPin(string subPin)
  - 设置 <c>subPin</c> 参数。

- FinanceopenapiSubaccountFreezeListRequest Channels(string channels)
  - 设置 <c>channels</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## FinanceopenapiSubaccountFreezeListResponse (class)

<c>jingdong.financeopenapi.subaccount.freeze.list</c> 的响应。

- public OpenApiResDto data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## FinanceopenapiSubproductBrandbalanceGetRequest (class)

<c>jingdong.financeopenapi.subproduct.brandbalance.get</c> 的请求。

- JdRequest req;

- public FinanceopenapiSubproductBrandbalanceGetRequest()

- FinanceopenapiSubproductBrandbalanceGetRequest SubPin(string subPin)
  - 设置 <c>subPin</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## FinanceopenapiSubproductBrandbalanceGetResponse (class)

<c>jingdong.financeopenapi.subproduct.brandbalance.get</c> 的响应。

- public OpenApiResDto data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## FinanceopenapiSubproductJrwbalanceGetRequest (class)

<c>jingdong.financeopenapi.subproduct.jrwbalance.get</c> 的请求。

- JdRequest req;

- public FinanceopenapiSubproductJrwbalanceGetRequest()

- FinanceopenapiSubproductJrwbalanceGetRequest SubPin(string subPin)
  - 设置 <c>subPin</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## FinanceopenapiSubproductJrwbalanceGetResponse (class)

<c>jingdong.financeopenapi.subproduct.jrwbalance.get</c> 的响应。

- public OpenApiResDto data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## FinanceopenapiSubproductJtkbalanceGetRequest (class)

<c>jingdong.financeopenapi.subproduct.jtkbalance.get</c> 的请求。

- JdRequest req;

- public FinanceopenapiSubproductJtkbalanceGetRequest()

- FinanceopenapiSubproductJtkbalanceGetRequest SubPin(string subPin)
  - 设置 <c>subPin</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## FinanceopenapiSubproductJtkbalanceGetResponse (class)

<c>jingdong.financeopenapi.subproduct.jtkbalance.get</c> 的响应。

- public OpenApiResDto data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## FinanceopenapiSubproductZtbalanceGetRequest (class)

<c>jingdong.financeopenapi.subproduct.ztbalance.get</c> 的请求。

- JdRequest req;

- public FinanceopenapiSubproductZtbalanceGetRequest()

- FinanceopenapiSubproductZtbalanceGetRequest SubPin(string subPin)
  - 设置 <c>subPin</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## FinanceopenapiSubproductZtbalanceGetResponse (class)

<c>jingdong.financeopenapi.subproduct.ztbalance.get</c> 的响应。

- public OpenApiResDto data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## JdFinanceopenapiApi (class)

jingdong.financeopenapi.* 的强类型客户端。

- JdClient client;

- public JdFinanceopenapiApi(JdClient client)

- async FinanceopenapiAccountAllbalanceGetResponse AccountAllbalanceGetAsync(FinanceopenapiAccountAllbalanceGetRequest request)
  - 执行 <c>jingdong.financeopenapi.account.allbalance.get</c>。

- async FinanceopenapiAccountAwardassignListResponse AccountAwardassignListAsync(FinanceopenapiAccountAwardassignListRequest request)
  - 执行 <c>jingdong.financeopenapi.account.awardassign.list</c>。

- async FinanceopenapiAccountAwardrechargeListResponse AccountAwardrechargeListAsync(FinanceopenapiAccountAwardrechargeListRequest request)
  - 执行 <c>jingdong.financeopenapi.account.awardrecharge.list</c>。

- async FinanceopenapiAccountCashassignListResponse AccountCashassignListAsync(FinanceopenapiAccountCashassignListRequest request)
  - 执行 <c>jingdong.financeopenapi.account.cashassign.list</c>。

- async FinanceopenapiAccountCashrechargeListResponse AccountCashrechargeListAsync(FinanceopenapiAccountCashrechargeListRequest request)
  - 执行 <c>jingdong.financeopenapi.account.cashrecharge.list</c>。

- async FinanceopenapiAccountCommissionrechargeListResponse AccountCommissionrechargeListAsync(FinanceopenapiAccountCommissionrechargeListRequest request)
  - 执行 <c>jingdong.financeopenapi.account.commissionrecharge.list</c>。

- async FinanceopenapiAccountVmsListResponse AccountVmsListAsync(FinanceopenapiAccountVmsListRequest request)
  - 执行 <c>jingdong.financeopenapi.account.vms.list</c>。

- async FinanceopenapiCostListResponse CostListAsync(FinanceopenapiCostListRequest request)
  - 执行 <c>jingdong.financeopenapi.cost.list</c>。

- async FinanceopenapiSubaccountAllbalanceGetResponse SubaccountAllbalanceGetAsync(FinanceopenapiSubaccountAllbalanceGetRequest request)
  - 执行 <c>jingdong.financeopenapi.subaccount.allbalance.get</c>。

- async FinanceopenapiSubaccountDaycostGetResponse SubaccountDaycostGetAsync(FinanceopenapiSubaccountDaycostGetRequest request)
  - 执行 <c>jingdong.financeopenapi.subaccount.daycost.get</c>。

- async FinanceopenapiSubaccountFreezeListResponse SubaccountFreezeListAsync(FinanceopenapiSubaccountFreezeListRequest request)
  - 执行 <c>jingdong.financeopenapi.subaccount.freeze.list</c>。

- async FinanceopenapiSubproductBrandbalanceGetResponse SubproductBrandbalanceGetAsync(FinanceopenapiSubproductBrandbalanceGetRequest request)
  - 执行 <c>jingdong.financeopenapi.subproduct.brandbalance.get</c>。

- async FinanceopenapiSubproductJrwbalanceGetResponse SubproductJrwbalanceGetAsync(FinanceopenapiSubproductJrwbalanceGetRequest request)
  - 执行 <c>jingdong.financeopenapi.subproduct.jrwbalance.get</c>。

- async FinanceopenapiSubproductJtkbalanceGetResponse SubproductJtkbalanceGetAsync(FinanceopenapiSubproductJtkbalanceGetRequest request)
  - 执行 <c>jingdong.financeopenapi.subproduct.jtkbalance.get</c>。

- async FinanceopenapiSubproductZtbalanceGetResponse SubproductZtbalanceGetAsync(FinanceopenapiSubproductZtbalanceGetRequest request)
  - 执行 <c>jingdong.financeopenapi.subproduct.ztbalance.get</c>。
