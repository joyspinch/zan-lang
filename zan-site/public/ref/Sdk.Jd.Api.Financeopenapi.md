# Sdk.Jd.Api.Financeopenapi

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Financeopenapi/FinanceopenapiAccountAllbalanceGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Financeopenapi/FinanceopenapiAccountAwardassignListRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Financeopenapi/FinanceopenapiAccountAwardrechargeListRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Financeopenapi/FinanceopenapiAccountCashassignListRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Financeopenapi/FinanceopenapiAccountCashrechargeListRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Financeopenapi/FinanceopenapiAccountCommissionrechargeListRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Financeopenapi/FinanceopenapiAccountVmsListRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Financeopenapi/FinanceopenapiCostListRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Financeopenapi/FinanceopenapiSubaccountAllbalanceGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Financeopenapi/FinanceopenapiSubaccountDaycostGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Financeopenapi/FinanceopenapiSubaccountFreezeListRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Financeopenapi/FinanceopenapiSubproductBrandbalanceGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Financeopenapi/FinanceopenapiSubproductJrwbalanceGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Financeopenapi/FinanceopenapiSubproductJtkbalanceGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Financeopenapi/FinanceopenapiSubproductZtbalanceGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Financeopenapi/JdFinanceopenapiApi.zan`


## FinanceopenapiAccountAllbalanceGetRequest (class)

- JdRequest req;

- public FinanceopenapiAccountAllbalanceGetRequest()

- JdRequest Raw()


## FinanceopenapiAccountAllbalanceGetResponse (class)

- public OpenApiResDto data;

- public string Raw;


## FinanceopenapiAccountAwardassignListRequest (class)

- JdRequest req;

- public FinanceopenapiAccountAwardassignListRequest()

- FinanceopenapiAccountAwardassignListRequest BeginDate(string beginDate)

- FinanceopenapiAccountAwardassignListRequest EndDate(string endDate)

- FinanceopenapiAccountAwardassignListRequest Page(string page)

- FinanceopenapiAccountAwardassignListRequest PageSize(string pageSize)

- FinanceopenapiAccountAwardassignListRequest SubPin(string subPin)

- FinanceopenapiAccountAwardassignListRequest AssignType(int assignType)

- JdRequest Raw()


## FinanceopenapiAccountAwardassignListResponse (class)

- public OpenApiResDto data;

- public string Raw;


## FinanceopenapiAccountAwardrechargeListRequest (class)

- JdRequest req;

- public FinanceopenapiAccountAwardrechargeListRequest()

- FinanceopenapiAccountAwardrechargeListRequest BeginDate(string beginDate)

- FinanceopenapiAccountAwardrechargeListRequest EndDate(string endDate)

- FinanceopenapiAccountAwardrechargeListRequest Page(string page)

- FinanceopenapiAccountAwardrechargeListRequest PageSize(string pageSize)

- FinanceopenapiAccountAwardrechargeListRequest OrderType(int orderType)

- FinanceopenapiAccountAwardrechargeListRequest WebsiteType(int websiteType)

- JdRequest Raw()


## FinanceopenapiAccountAwardrechargeListResponse (class)

- public OpenApiResDto data;

- public string Raw;


## FinanceopenapiAccountCashassignListRequest (class)

- JdRequest req;

- public FinanceopenapiAccountCashassignListRequest()

- FinanceopenapiAccountCashassignListRequest BeginDate(string beginDate)

- FinanceopenapiAccountCashassignListRequest EndDate(string endDate)

- FinanceopenapiAccountCashassignListRequest Page(string page)

- FinanceopenapiAccountCashassignListRequest PageSize(string pageSize)

- FinanceopenapiAccountCashassignListRequest SubPin(string subPin)

- FinanceopenapiAccountCashassignListRequest AssignType(int assignType)

- JdRequest Raw()


## FinanceopenapiAccountCashassignListResponse (class)

- public OpenApiResDto data;

- public string Raw;


## FinanceopenapiAccountCashrechargeListRequest (class)

- JdRequest req;

- public FinanceopenapiAccountCashrechargeListRequest()

- FinanceopenapiAccountCashrechargeListRequest BeginDate(string beginDate)

- FinanceopenapiAccountCashrechargeListRequest EndDate(string endDate)

- FinanceopenapiAccountCashrechargeListRequest Page(string page)

- FinanceopenapiAccountCashrechargeListRequest PageSize(string pageSize)

- JdRequest Raw()


## FinanceopenapiAccountCashrechargeListResponse (class)

- public OpenApiResDto data;

- public string Raw;


## FinanceopenapiAccountCommissionrechargeListRequest (class)

- JdRequest req;

- public FinanceopenapiAccountCommissionrechargeListRequest()

- FinanceopenapiAccountCommissionrechargeListRequest BeginDate(string beginDate)

- FinanceopenapiAccountCommissionrechargeListRequest EndDate(string endDate)

- FinanceopenapiAccountCommissionrechargeListRequest Page(string page)

- FinanceopenapiAccountCommissionrechargeListRequest PageSize(string pageSize)

- FinanceopenapiAccountCommissionrechargeListRequest OrderType(int orderType)

- FinanceopenapiAccountCommissionrechargeListRequest WebsiteType(int websiteType)

- JdRequest Raw()


## FinanceopenapiAccountCommissionrechargeListResponse (class)

- public OpenApiResDto data;

- public string Raw;


## FinanceopenapiAccountVmsListRequest (class)

- JdRequest req;

- public FinanceopenapiAccountVmsListRequest()

- FinanceopenapiAccountVmsListRequest BeginDate(string beginDate)

- FinanceopenapiAccountVmsListRequest EndDate(string endDate)

- FinanceopenapiAccountVmsListRequest Page(string page)

- FinanceopenapiAccountVmsListRequest PageSize(string pageSize)

- JdRequest Raw()


## FinanceopenapiAccountVmsListResponse (class)

- public OpenApiResDto data;

- public string Raw;


## FinanceopenapiCostListRequest (class)

- JdRequest req;

- public FinanceopenapiCostListRequest()

- FinanceopenapiCostListRequest BeginDate(string beginDate)

- FinanceopenapiCostListRequest EndDate(string endDate)

- FinanceopenapiCostListRequest OrderTypes(string orderTypes)

- FinanceopenapiCostListRequest SubPin(string subPin)

- FinanceopenapiCostListRequest MoneyType(int moneyType)

- FinanceopenapiCostListRequest PageNo(int pageNo)

- FinanceopenapiCostListRequest PageSize(int pageSize)

- JdRequest Raw()


## FinanceopenapiCostListResponse (class)

- public OpenApiResDto data;

- public string Raw;


## FinanceopenapiSubaccountAllbalanceGetRequest (class)

- JdRequest req;

- public FinanceopenapiSubaccountAllbalanceGetRequest()

- FinanceopenapiSubaccountAllbalanceGetRequest SubPin(string subPin)

- JdRequest Raw()


## FinanceopenapiSubaccountAllbalanceGetResponse (class)

- public OpenApiResDto data;

- public string Raw;


## FinanceopenapiSubaccountDaycostGetRequest (class)

- JdRequest req;

- public FinanceopenapiSubaccountDaycostGetRequest()

- FinanceopenapiSubaccountDaycostGetRequest SubPin(string subPin)

- FinanceopenapiSubaccountDaycostGetRequest Showday(int showday)

- JdRequest Raw()


## FinanceopenapiSubaccountDaycostGetResponse (class)

- public OpenApiResDto data;

- public string Raw;


## FinanceopenapiSubaccountFreezeListRequest (class)

- JdRequest req;

- public FinanceopenapiSubaccountFreezeListRequest()

- FinanceopenapiSubaccountFreezeListRequest BeginDate(string beginDate)

- FinanceopenapiSubaccountFreezeListRequest EndDate(string endDate)

- FinanceopenapiSubaccountFreezeListRequest Page(string page)

- FinanceopenapiSubaccountFreezeListRequest PageSize(string pageSize)

- FinanceopenapiSubaccountFreezeListRequest SubPin(string subPin)

- FinanceopenapiSubaccountFreezeListRequest Channels(string channels)

- JdRequest Raw()


## FinanceopenapiSubaccountFreezeListResponse (class)

- public OpenApiResDto data;

- public string Raw;


## FinanceopenapiSubproductBrandbalanceGetRequest (class)

- JdRequest req;

- public FinanceopenapiSubproductBrandbalanceGetRequest()

- FinanceopenapiSubproductBrandbalanceGetRequest SubPin(string subPin)

- JdRequest Raw()


## FinanceopenapiSubproductBrandbalanceGetResponse (class)

- public OpenApiResDto data;

- public string Raw;


## FinanceopenapiSubproductJrwbalanceGetRequest (class)

- JdRequest req;

- public FinanceopenapiSubproductJrwbalanceGetRequest()

- FinanceopenapiSubproductJrwbalanceGetRequest SubPin(string subPin)

- JdRequest Raw()


## FinanceopenapiSubproductJrwbalanceGetResponse (class)

- public OpenApiResDto data;

- public string Raw;


## FinanceopenapiSubproductJtkbalanceGetRequest (class)

- JdRequest req;

- public FinanceopenapiSubproductJtkbalanceGetRequest()

- FinanceopenapiSubproductJtkbalanceGetRequest SubPin(string subPin)

- JdRequest Raw()


## FinanceopenapiSubproductJtkbalanceGetResponse (class)

- public OpenApiResDto data;

- public string Raw;


## FinanceopenapiSubproductZtbalanceGetRequest (class)

- JdRequest req;

- public FinanceopenapiSubproductZtbalanceGetRequest()

- FinanceopenapiSubproductZtbalanceGetRequest SubPin(string subPin)

- JdRequest Raw()


## FinanceopenapiSubproductZtbalanceGetResponse (class)

- public OpenApiResDto data;

- public string Raw;


## JdFinanceopenapiApi (class)

- JdClient client;

- public JdFinanceopenapiApi(JdClient client)

- async FinanceopenapiAccountAllbalanceGetResponse AccountAllbalanceGetAsync(FinanceopenapiAccountAllbalanceGetRequest request)

- async FinanceopenapiAccountAwardassignListResponse AccountAwardassignListAsync(FinanceopenapiAccountAwardassignListRequest request)

- async FinanceopenapiAccountAwardrechargeListResponse AccountAwardrechargeListAsync(FinanceopenapiAccountAwardrechargeListRequest request)

- async FinanceopenapiAccountCashassignListResponse AccountCashassignListAsync(FinanceopenapiAccountCashassignListRequest request)

- async FinanceopenapiAccountCashrechargeListResponse AccountCashrechargeListAsync(FinanceopenapiAccountCashrechargeListRequest request)

- async FinanceopenapiAccountCommissionrechargeListResponse AccountCommissionrechargeListAsync(FinanceopenapiAccountCommissionrechargeListRequest request)

- async FinanceopenapiAccountVmsListResponse AccountVmsListAsync(FinanceopenapiAccountVmsListRequest request)

- async FinanceopenapiCostListResponse CostListAsync(FinanceopenapiCostListRequest request)

- async FinanceopenapiSubaccountAllbalanceGetResponse SubaccountAllbalanceGetAsync(FinanceopenapiSubaccountAllbalanceGetRequest request)

- async FinanceopenapiSubaccountDaycostGetResponse SubaccountDaycostGetAsync(FinanceopenapiSubaccountDaycostGetRequest request)

- async FinanceopenapiSubaccountFreezeListResponse SubaccountFreezeListAsync(FinanceopenapiSubaccountFreezeListRequest request)

- async FinanceopenapiSubproductBrandbalanceGetResponse SubproductBrandbalanceGetAsync(FinanceopenapiSubproductBrandbalanceGetRequest request)

- async FinanceopenapiSubproductJrwbalanceGetResponse SubproductJrwbalanceGetAsync(FinanceopenapiSubproductJrwbalanceGetRequest request)

- async FinanceopenapiSubproductJtkbalanceGetResponse SubproductJtkbalanceGetAsync(FinanceopenapiSubproductJtkbalanceGetRequest request)

- async FinanceopenapiSubproductZtbalanceGetResponse SubproductZtbalanceGetAsync(FinanceopenapiSubproductZtbalanceGetRequest request)
