# -*- coding: utf-8 -*-
"""生成最终页面数据：淘宝工具接口脉络分析（在用/死接口、列表×菜单、调用链、投放方式）"""
import io, json
from urllib.parse import urlparse

api = json.load(io.open("api_data.json", encoding="utf-8"))
link = json.load(io.open("linkage.json", encoding="utf-8"))
reqs, usage = api["requests"], api["usage"]
facade = link["facadeMap"]

# ---------- 在用 / 死 ----------
instantiated = set(link["reqCallers"].keys())
called_apis = set()
for rel, u in usage.items():
    called_apis.update(u["apis"])
via_facade = set()
for a in called_apis:
    via_facade.update(facade.get(a, []))
used = instantiated | via_facade
dead = set(reqs.keys()) - used

method_of_req = {}
for m, rs in facade.items():
    for r in rs:
        method_of_req.setdefault(r, []).append(m)

# ---------- 域分类 ----------
HOST_LABEL = {
    "one.alimama.com": "万相台 One",
    "bpcommon.alimama.com": "One 公共",
    "dmp.advgateway.taobao.com": "达摩盘",
    "dmp-gateway.alimama.com": "达摩盘(新)",
    "dmp.taobao.com": "达摩盘",
    "auth.lihuibox.com": "厂商后端",
    "h5api.m.taobao.com": "Mtop H5",
    "sycm.taobao.com": "生意参谋",
    "jzt-api.jd.com": "京东快车",
    "chuangyi.alimama.com": "创意中心",
    "make.chuangyi.taobao.com": "创意制作",
    "chuangyi.taobao.com": "创意中心(旧)",
    "txcs.tmall.com": "天猫超市",
    "xiangqing.wangpu.taobao.com": "旺铺详情",
    "alimama.taobao.com": "阿里妈妈(旧)",
    "alimama2.taobao.com": "阿里妈妈(旧)",
    "item.upload.taobao.com": "商品发布",
    "sale.taobao.com": "营销活动",
    "bench.global.tmall.com": "天猫国际工作台",
    "osweb-b2c-alihealth.tmall.com": "阿里健康",
    "sff.jd.com": "京东(旧)",
}
PATH_LABEL = {
    "campaign": "计划", "campaignGroup": "计划组", "campaigngroup": "计划组",
    "adgroup": "主体", "item": "商品/全店主体", "bidword": "关键词", "keyword": "关键词",
    "wordpackage": "词包", "crowd": "人群", "blackCrowd": "屏蔽人群", "label": "人群标签",
    "adzone": "资源位", "creative": "创意", "report": "报表", "solution": "Solution组合建",
    "material": "素材/商品", "template": "模板", "component": "组件", "member": "会员",
    "scene": "营销场景", "theme": "趋势主题", "trendTheme": "趋势主题", "video": "视频",
    "shopCategory": "店铺类目", "category": "类目", "dmp": "DMP", "tag": "标签",
    "soft": "厂商服务", "auth": "授权", "optimize": "一键优化", "grid": "表格配置",
    "gather": "埋点", "blackName": "黑名单", "cates": "类目云配", "mouse": "滑块",
    "config": "云配置", "sucai": "素材", "xiangqing": "旺铺详情", "potential": "潜力词",
    "cube": "行情", "quota": "厂商配额", "login": "登录",
    "shopcategory": "店铺类目", "trendtheme": "趋势主题", "kuaiche": "快车(京东)",
    "dpss": "银河文案", "api_2": "DMP(旧)", "dspad": "DMP广告",
    "blackname": "黑名单", "taobaocate": "淘宝类目",
}

GENERIC_SEG = {"api", "horizontal", "h5", "v1", "v2", "p4p", "sucai", "1.0",
               "soft", "celue", "api_v2", "resources", "index_new.html"}

def classify(name, meta):
    url = meta.get("url", "")
    try:
        host = urlparse(url).netloc
        path = urlparse(url).path
    except Exception:
        host, path = "", ""
    if host.startswith("auth.lihuibox.com"):  # 原代码拼接缺陷 comsoft → 归一
        host = "auth.lihuibox.com"
    segs = [s for s in path.split("/") if s]
    seg = ""
    for s in segs:
        if s.lower() in GENERIC_SEG or s.startswith("${") or s.startswith("{"):
            continue
        seg = s
        break
    if not seg and segs:
        seg = segs[0]
    domain = PATH_LABEL.get(seg, seg or "其他")
    if domain.startswith("${ServerHost}"):  # 运行时注入的厂商域名
        domain = "厂商服务"
    return {
        "host": host, "hostLabel": HOST_LABEL.get(host, host or "未知"),
        "path": path, "domain": domain,
    }

# ---------- 接口主表 ----------
iface_rows = []
for name, meta in sorted(reqs.items()):
    cls = classify(name, meta)
    callers = sorted(set(link["reqCallers"].get(name, [])))
    n_calls = 0
    for rel, u in usage.items():
        n_calls += u["requests"].count(name)
    iface_rows.append({
        "name": name, "url": meta.get("url", ""), "method": meta.get("method", ""),
        "inUse": name in used, "host": cls["hostLabel"], "domain": cls["domain"],
        "path": cls["path"], "facade": method_of_req.get(name, []),
        "callers": callers, "nCalls": n_calls,
        "file": meta.get("file", "").split("BizanSoft/")[-1],
    })
iface_rows.sort(key=lambda r: (not r["inUse"], r["host"], r["domain"], r["name"]))

# ---------- BizCode ----------
bizcodes = [
    ["onebpSearch", "关键词推广（原直通车）", "Search", "独立模块；独有关键词/词包两层；资源位固定4个"],
    ["onebpDisplay", "精准人群运营（原引力魔方）", "Display", "显示精细化调控人群入口"],
    ["onebpItemMarketing", "货品运营（已升级店铺）", "Display", "升级店铺专用入口"],
    ["onebpAdStrategyProductSpeed", "货品加速", "Display", "隐藏菜单项"],
    ["onebpAdStrategyCeKuan", "测款快", "Display", "隐藏菜单项"],
    ["onebpAdStrategyShangXin", "上新快", "Display", "隐藏菜单项"],
    ["onebpAdStrategyCrowd", "人群击穿", "Display", ""],
    ["onebpAdStrategyDkx", "拉新快", "Display", "消费者运营子菜单"],
    ["onebpAdStrategyRuHui", "会员快", "Display", "消费者运营子菜单"],
    ["onebpAdStrategyFans", "粉丝快", "Display", "消费者运营子菜单"],
    ["onebpAdStrategyLiuZi", "易获客", "Display", "消费者运营子菜单"],
    ["onebpAdStrategyYuRe", "活动加速", "Display", ""],
    ["onebpAdStrategyWholeShop", "店铺运营", "Display", ""],
    ["onebpMultiAim", "多目标直投", "Display", "隐藏入口最多（无资源位/出价/定向调整等）"],
    ["onebpSite", "全站推广", "Display", "主体=全店商品(AdGroupSite，走 item 端点)；启停需 CampaignSiteoneClick；bizCode=universalBP"],
    ["onebpStarShop", "店铺", "Display", ""],
    ["onebpShortVideo", "超级短视频", "Display", "隐藏创意/资源位/一键优化；人群 labid=3000841"],
    ["adStrategyShortVideoRtb", "超级短视频(RTB)", "Display", ""],
    ["onebpLive", "超级直播", "Display", "隐藏菜单项"],
    ["onebpUnion", "短直联动", "Display", ""],
]

# ---------- 调用链（基于 sequences.json + 逐行代码验证） ----------
chains = [
 {"name": "同步计划（数据底座）", "trigger": "计划列表 → 同步", "file": "One/Download/FormNotifyCamp.cs",
  "steps": [["CampaignGetRequest", "POST /campaign/horizontal/findPage.json", "先拉第1页拿总数，再并发拉 2..N 页（信号量限流+随机延迟）"]],
  "order": "一切写操作的前置：本地 SQLite 是列表渲染唯一数据源。同步后清孤儿(人群/资源位/关键词/词包/创意)并写 CommKv 时间戳(TTL 600s 防重拉)"},
 {"name": "同步主体", "trigger": "计划/计划组列表 → 同步主体", "file": "One/Download/FormNotifyAdGroup.cs",
  "steps": [["AdGroupGetRequest", "POST /adgroup/horizontal/findPage.json", "普通主体"],
            ["AdGroupGetRequest", "GET /item/horizontal/findPage.json", "onebpSite 全店模式(itemSelectedMode=shop)改走 item 端点"]],
  "order": "端点随 bizCode+itemSelectedMode 切换，zan 请求层必须做此分支"},
 {"name": "批量新建计划", "trigger": "计划列表 → 批量新建计划（FormCampNew → CampDownloader.add）", "file": "Downloader/One/CampDownloader.cs",
  "steps": [["SolutionAddRequest", "POST /solution/addList.json", "一次提交 计划+占位主体(itemId=10000001 测试商品+词\"淘宝\" matchScope:16 bidPrice:0.05)+关键词/词包"],
            ["CampaignUpdateStatusRequest", "POST /campaign/updatePart.json", "★对新 campaignId 置 displayStatus=pause（创建即暂停，防误花费）"],
            ["AdGroupGetRequest", "→ AdGroupDeleteRequest", "仅 delete=true 分支：删占位单元"]],
  "order": "campaignId 从 SolutionAdd 响应 info.message 解析；errorDetails 非空直接 throw；每步间 WaitMilliseconds 延时；无自动回滚"},
 {"name": "批量推广主体（给计划加商品）", "trigger": "计划列表 → 批量推广主体（FormAdgroupNew → NewAdGroupList）", "file": "One/Common/Camp/FormCampAdGroupAdd.cs + One/Search/Camp/FormCampCreate.cs",
  "steps": [["CampaignSiteoneClickRequest", "POST /campaign/onebpSite/oneClick.json", "仅 onebpSite 分支：bizCode=universalBP 把 itemIdList 纳入全站池"],
            ["CreativeFindRequireRequest", "POST /creative/findRequire.json", "非 site 分支：查创意素材规格"],
            ["SolutionAdGroupAddRequest", "POST /adgroup/addList.json", "campaignId + adgroupList(material+creativeInfo)"],
            ["(关键词计划) Get_WordPackageSuggestDefault", "/wordpackage/suggest/default/list.json", "FormCampCreate 路径：词包建议→KeyWordSuggestKr/Default→KeyMarketTrendsGet 均价→再 addList"]],
  "order": "campaignId 来自本地已同步缓存；成功后必须 CommKvDao.DeleteTimeOutCamp 强制重同步；itemIdList 来自 OneTaobaoItemDao(FormNotifyTaobaoItem 预同步)"},
 {"name": "全站推广建计划/启停", "trigger": "Display(onebpSite) → 新建计划 / 暂停删除主体", "file": "One/Display/Camp/FormCampCreateSite.cs + One/Common/AdGroup/FormAdGroupStop.cs",
  "steps": [["CampaignSiteoneClickRequest", "POST /campaign/onebpSite/oneClick.json", "建计划前置预热 itemIds，硬等 2 秒"],
            ["SolutionAddRequest", "POST /solution/addList.json", "promotionScene=promotion_scene_site"],
            ["CampaignUpdateStatusRequest", "置 pause", ""],
            ["(启停删) AdGroupUpdatePartRequest / SiteItemDeleteRequest", "/item/updatePart.json | /item/batchDelete.json", ""],
            ["CampaignDiffFindListRequest", "POST /campaign/diff/findList.json", "删商品后查其他全站计划中同商品"],
            ["CampaignOneRecoverRequest", "POST /campaign/onebpSite/oneRecover.json", "把 diff 的 list 原样回传恢复其他计划投放"]],
  "order": "删主体会连带影响其他全站计划 → DiffFindList 的输出是 oneRecover 的唯一输入"},
 {"name": "克隆计划", "trigger": "计划列表 → 计划克隆", "file": "One/Common/Camp/FormCampCopy.cs + One/Search/Camp/FormCampCopy.cs",
  "steps": [["(系统快捷) CampaignCopyRequest", "POST /solution/copy.json", "→ 解析新 campaignId → pause"],
            ["(完整克隆) AdzoneListAggregatedtGetRequest", "POST /adzone/horizontal/findListAggregated.json", "读源计划资源位"],
            ["CrowdListGetRequest", "POST /crowd/findList.json", "读源计划人群(label/过滤器)"],
            ["SolutionAddRequest", "POST /solution/addList.json", "建带人群模板的新计划"],
            ["SolutionAdGroupAddRequest", "POST /adgroup/addList.json", "复制源单元"],
            ["CampaignAdvancedBatchUpdateRequest", "POST /campaign/advanced/batchUpdate.json", "回写 adzoneLists（必须等计划+单元都建好）"],
            ["(Search版续) KeyWordAdd→WordpackageAdd→CrowdBatchModify→CreativeUpdateAndTemplate", "逐层复制", "先 KeyWordGet/WordPackageGet/CrowdGet/CreativeGet 全量下载源数据"]],
  "order": "新 campaignId 全部来自上一步响应逐级传递；adzoneLists 回写在最后；每计划 try/catch 不回滚半成品"},
 {"name": "批量添加创意", "trigger": "创意/计划列表 → 批量添加创意 / 批量添加创意组", "file": "One/Display/Creative/FormCreativeAdd.cs + One/Common/Creative/FormCreativeGroupAdd.cs",
  "steps": [["CreativeAddRequest", "POST /creative/add.json", "creativeList(imagePath/size/format:2图12视频/videoId)"],
            ["CreativeBindRequest", "POST /creative/bind.json", "★响应 data.creativeList 的 creativeIds → 绑定到单元（两步不可合并不可乱序）"],
            ["(创意组) CreativeFindRequireV2Request", "POST /creative/findRequire.json", "规格→收集主图/3:4图/SKU图/视频"],
            ["(创意组) CreativeRecommendRequest + XiangQingApolePageRenderRequest", "/creative/material/recommend.json + 旺铺详情", "推荐标题+商品详情抓素材"],
            ["(创意组) CreativeUpdateAndTemplateRequest", "POST /creative/updateCreativeAndTemplate.json", "smartCreativePackageList 一步写入(bind 已注释省略)"]],
  "order": "add 成功 bind 失败 → 孤儿创意无回滚；创意组路线依赖后端自动关联"},
 {"name": "一键修补创意素材", "trigger": "创意列表 → 一键修补创意素材", "file": "One/Common/Creative/FormCreativeMainVideo.cs",
  "steps": [["CreativeGetRequest", "POST /creative/horizontal/findPage.json", "拉现有创意(行 id 来源)"],
            ["CreativeFindRequireV2Request", "POST /creative/findRequire.json", "检测缺失规格(800x1200图/800x800视频等)"],
            ["CreativeRecommendRequest", "POST /creative/material/recommend.json", "缺标题时"],
            ["RecommendVideoListRequest / CreativeIncludeImagesRequest", "make.chuangyi.taobao.com", "缺视频/图片时"],
            ["CreativeUpdateV2Request", "POST /creative/update.json", "批量回写补齐行"]],
  "order": "findRequire 的缺失清单决定后续读步骤是否执行；update 前全部只读"},
 {"name": "视频创意（现状：仅推荐视频一条活路）", "trigger": "创意列表 → 视频相关菜单", "file": "FormCreativeMainVideo.cs（注释剥离后核实）",
  "steps": [["RecommendVideoListRequest", "make.chuangyi.taobao.com/p4p/itemVideo/recommendedVideos", "memberId/nickName/outsideItemNumId → 推荐视频（一键修补链内调用）"],
            ["CreativeUpdateV2Request", "POST /creative/update.json", "回写 videoId/format=12"]],
  "order": "⚠ 死代码警示（勿迁移）：① 旧 TaobaoP4pApi 上传链(checkSum→csrf→upload→ByUrl，5个Request)在 FormCreativeList L1208-1333 整体被注释；② BizanSoft 中 GetItemMainVideoRequest/GetShortVideoListRequest/CelueShortVideoRequest/RestoreVideoToSucaiRequest 有定义但主工程零调用"},
 {"name": "添加 DMP 人群", "trigger": "人群菜单 → 批量添加DMP人群", "file": "One/Common/Crowd/FormCrowdDmpNew.cs",
  "steps": [["DmpV2CrowdListRequest", "POST dmp.advgateway/api/dmp/v2/crowd/list", "窗体加载时预载达摩盘人群"],
            ["CrowdGetRequest", "POST /crowd/horizontal/findPage.json", "onebpShortVideo 时先读现有人群防覆盖"],
            ["LableDmpConvertRequest", "POST /label/dmpConvert.json", "★dmpCrowdIds→站内label；labid 硬编码：默认3000496/货品运营3000836/短视频3000841"],
            ["CrowdBatchModifyRequest", "POST /crowd/batchModifyV2.json", "整包替换；计划级 adgroupId=0"]],
  "order": "mx_crowdId=\"{targetType}_{labelId}_{optionValue}\" 由 convert 输出拼装；追加人群必须先合并本地已有人群"},
 {"name": "自定义/通用/小二推荐人群", "trigger": "人群菜单 → 批量添加自定义人群 / 通用人群 / 小二推荐人群", "file": "FormCrowdCustom.cs + FormCrowdCurrency.cs + FormCrowdSuggestAdd.cs",
  "steps": [["(自定义) CrowdLabelGetRequest", "POST /label/findList.json", "模板id硬编码 3000624/3000524/3000625/3000623/3000844，需 materialIdList 过滤"],
            ["(自定义) CrowdBatchModifyRequest", "POST /crowd/batchModifyV2.json", "勾选 option 后整包写入"],
            ["(推荐) DmpCrowdTopicRequest → DmpCrowdTopicTemplateRequest", "dmp.advgateway/api/dmp/crowd/topic(/template)", "主题→模板"],
            ["(推荐) DmpCountJxTemplateIndicatorRequest", "dmp-gateway/api/dmp/crowd/countJxTemplateIndicator", "覆盖人数(结果缓存 CommCrowdCoverageDao)"],
            ["(推荐) OneApplyJxTemplateRequest", "/api/dmp/crowd/applyJxTemplate.json", "→ DmpCrowdIds"],
            ["(推荐) LableDmpConvertRequest", "/label/dmpConvert.json labid=3000525", ""],
            ["(推荐) CrowdGetRequest → CrowdDeleteRequest", "读现有 → /crowd/batchDelete.json", "先清后写"],
            ["(推荐) CrowdBatchModifyRequest", "写入新人群", ""]],
  "order": "推荐链最长：选模板→预估→应用→转换→清旧→写新"},
 {"name": "人群复制", "trigger": "人群菜单 → 批量添加人群-复制", "file": "One/Search/Crowd/FormCrowdCopy.cs + One/Common/Crowd/FormCrowdCopy.cs",
  "steps": [["(Search) 本地 OneCrowdDao 源人群", "排除 targetType 48/15", "128 单独处理"],
            ["(非DMP) CrowdCopyRequest", "POST /crowd/copy.json", "campaignIdList 一次多目标"],
            ["(DMP targetType=128) CrowdBatchModifyRequest", "POST /crowd/batchModifyV2.json", "★DMP 人群不能走 copy.json"],
            ["(Common) CrowdListGetRequest", "POST /crowd/findList.json", "读源→batchModifyV2 写目标(不读目标现有，会覆盖)"]],
  "order": "目标计划原有人群被整包覆盖，无合并保护"},
 {"name": "批量添加关键词 + 潜力词", "trigger": "关键词菜单 → 批量添加关键词 / 词包潜力词发现", "file": "One/Search/KeyWord/FormKeyWordAdd.cs + One/Search/WordPackage/FormWordPackageAiByGroup.cs",
  "steps": [["WordPackageGetRequest", "POST /wordpackage/findList.json", "查现有词包避免冲突"],
            ["KeyWordSuggestKrGetRequest", "POST /bidword/suggest/kr/list.json", "建议词/出价"],
            ["KeyWordAddRequest", "POST /bidword/add.json", ""],
            ["(潜力词) KeyWordPotentialBidwordFindListRequest", "POST /potential/bidword/findList.json", "逐 adgroup 按日期范围"],
            ["(潜力词) KeyWordAddRequest | WordpackageBlackWordSetRequest", "/bidword/add.json | /bidword/blackword/update.json", "加竞价词与加黑名单互斥"]],
  "order": "仅 onebpSearch；adgroupId/campaignId 来自本地缓存"},
 {"name": "一键优化执行", "trigger": "一键优化 → 执行任务", "file": "One/Common/FormOptimizeCampaignList.cs",
  "steps": [["OptimizeConfigGetRequest", "{厂商服务器}api/optimize/info", "拉 AutoOptimize 规则配置"],
            ["AdGroupGetRequest", "POST /item/horizontal/findPage.json", "拉主体+近31天报表(transaction_cost 是触发条件)"],
            ["AdGroupUpdatePartRequest", "POST /item/updatePart.json", "暂停亏损单元"],
            ["KeyWordGetRequest → KeyWordUpdateRequest", "/bidword/findList.json → /bidword/update.json", "关键词调价(按天窗口)"],
            ["WordPackageGetRequest → WordpackageUpdatePriceRequest", "/wordpackage/findList.json → /wordpackage/update.json", "词包调价"],
            ["CrowdGetRequest → CrowdModifyDiscountRequest", "/crowd/horizontal/findPage.json → modifyDiscount.json", "adgroup 级溢价"],
            ["CrowdGetRequest → CrowdModifyPriceRequest", "campaign 级(adgroupId=null) → modifyBidPrice.json", "计划级出价"],
            ["AdzoneGetRequest → AdzoneUpdatePriceRequest", "/adzone/horizontal/findPage.json → updatePrice.json", "资源位调价"]],
  "order": "「读→判→改」成对出现：更新接口需完整实体回传(现值+id)；每实体独立 try/catch 失败继续"},
 {"name": "同步报表数据", "trigger": "计划列表 → 计划分时/分省/城市数据", "file": "One/Download/FormNotifyCampHour/Province/City.cs",
  "steps": [["CampaignReportGetRequest", "POST /report/query.json", "bizCode=universalBP, rptType=campaign|area, splitType=hour|day, unifyType=zhai, queryDomains=[date|province|city], strategyCampaignIdIn"],
            ["CampaignGroupReportGetRequest", "POST /report/query.json", "计划组维度"]],
  "order": "campaignId 列表依赖本地计划缓存；单计划单次调用计划间并行；结果落本地 FreeSql 报表表"},
 {"name": "同步主体销量", "trigger": "主体列表 → 同步主体销量", "file": "One/Download/FormNotifyTaobaoItemSell.cs",
  "steps": [["MtopTaobaoSellPcManageAsync", "POST h5api.m.taobao.com/h5/mtop.taobao.sell.pc.manage.async/1.0/", "天猫店走 mtop.tmall.* 变体"],
            ["本地回填", "OneTaobaoItemDao.payCnt30d", "按 itemId 匹配更新30天付款数"]],
  "order": "依赖 _m_h5_tk cookie 的 mtop 签名(OneCookieManager.Seth5Token 从 WebView2 提取)；前置是 FormNotifyTaobaoItem 已建好宝贝表"},
 {"name": "趋势主题计划", "trigger": "计划 → 批量修改趋势主题 / 新建趋势明星计划", "file": "One/Search/Camp/FormCampTrendTheme.cs",
  "steps": [["RecommendThemeGetRequest", "推荐主题", ""],
            ["RecommendItemGetRequest", "主题商品", ""],
            ["ThemeAssociationGetRequest", "主题关联", ""],
            ["CampaignUpdateRequest", "更新计划主题", ""]],
  "order": ""},
 {"name": "屏蔽人群管理", "trigger": "人群菜单 → 批量调整/查看屏蔽人群", "file": "One/Common/Crowd/FormCrowdCustomFilters.cs",
  "steps": [["BlackCrowdFindListRequest", "查屏蔽人群", ""],
            ["BlackCrowdBatchModifyRequest", "批量屏蔽", ""],
            ["BlackCrowdBatchDeleteRequest", "批量解除", ""]],
  "order": ""},
]

# ---------- 隐式前置（所有调用共享的约定） ----------
preconditions = [
 ["鉴权引导（一切的根）", "WebView2 登录 → CoreWebView 经 DevTools(Network.getResponseBody) 拦截 member/checkAccess.json 响应 → 解析 MemberInfo.csrfId → P4pApiManager.SetOneCsrfID/SetDmpCsrfID/SetLoginPointId。无 csrfId 全部写接口无效"],
 ["请求信封", "每个 POST 的 URL 固定带 _aem_uid=mx_<随机>&lite2=false&bizCode&csrfId，body 内再次携带 bizCode/lite2=false/csrfId/loginPointId；固定头 Bx-V: 2.5.36、X-Requested-With: XMLHttpRequest（OneApiClient.cs 56-78）"],
 ["Cookie 管道", "CoreWebView.GetCookies → _tb_token_→SetTbToken/SetDmpToken；XSRF-TOKEN@dmp.taobao.com→SetDmpXSRFTOKEN（DMP 人群接口必需）；_m_h5_tk→Seth5Token（mtop 签名必需）"],
 ["本地缓存即数据源", "写操作的 campaignId/adgroupId/itemId/素材全取自 FreeSql 本地表（OneCampaignDao/OneAdGroupDao/…），由 FormNotify* 后台窗体预同步。写操作的真正前置是「先跑对应同步任务」，不是写前即时读"],
 ["缓存新鲜度门控", "CommKvDao 超时标记(QueryTimeOut/DeleteTimeOutCamp)控制缓存；写成功后必须 DeleteTimeOutCamp 强制下次重同步"],
 ["创建即暂停", "所有建计划链（SolutionAdd/copy.json/oneClick）最后一步必为 CampaignUpdateStatusRequest displayStatus=pause，防新建即烧钱"],
 ["占位单元模式", "SolutionAdd 要求计划必须带单元 → itemId=10000001\"测试商品\"+词\"淘宝\"(matchScope:16,bidPrice:0.05) 占位；真实主体事后 adgroup/addList.json 追加"],
 ["batchModifyV2 整包替换", "/crowd/batchModifyV2.json 是整包覆盖不是增量。追加人群必须先 CrowdGet 合并再提交，否则清空原有人群；计划级人群固定 adgroupId=0"],
 ["mx_crowdId 规则", "\"{targetType}_{labelId}_{optionValue}\" 由 dmpConvert/findList 返回拼装，是人群行唯一标识；labid 硬编码：3000496默认/3000836宝贝营销/3000841短视频/3000525模板推荐/3000624,3000524,3000625,3000623,3000844自定义"],
 ["限速与并发", "每次 API 调用间 GlobalVariables.WaitMilliseconds() 随机延时；批量循环 SemaphoreSlim/DynamicConcurrencyThrottler（GlobalVariables.processorOneCount）"],
 ["bizCode 路由", "同一 URL 在不同 bizCode 下行为不同，写操作 bizCode 必须与目标计划场景一致；全站推广专用 bizCode=universalBP（oneClick/report）"],
 ["错误约定", "info.errorCode/info.message/errorDetails 判定；\"不存在\"类错误静默跳过，其余 throw；登出态抛 LoginOutException → GlobalVariables.loginOut() 重登录；ErrorHelper.SendError 上报厂商"],
 ["创意两步绑定", "/creative/add.json 只建不绑，必须跟进 /creative/bind.json；智能创意包路线用 updateCreativeAndTemplate.json 一步到位"],
 ["外部域鉴权独立", "chuangyi.alimama.com / make.chuangyi.taobao.com（视频素材）、dmp.taobao.com（XSRF-TOKEN）、dmp-gateway（DMP模板）、h5api.m.taobao.com（_m_h5_tk mtop签名）各有独立鉴权，不能套用 one.alimama.com 的 csrfId 信封"],
 ["遗留死代码", "FormCreativeList.cs L1208-1333 视频上传链整体被注释，Subway/P4P Request 已 Compile Remove；视频功能以现役 chuangyi/make.chuangyi 素材链为准"],
]

data = {
    "stats": {"defined": len(reqs), "inUse": len(used), "dead": len(dead),
              "menus": sum(len(l["menus"]) for l in link["lists"]),
              "lists": len([l for l in link["lists"] if l["form"]]),
              "facadeMethods": len(facade)},
    "interfaces": iface_rows,
    "deadList": sorted(dead),
    "bizcodes": bizcodes,
    "chains": chains,
    "preconditions": preconditions,
    "lists": link["lists"],
}
io.open("page_data.json", "w", encoding="utf-8").write(json.dumps(data, ensure_ascii=False))
print("接口行:", len(iface_rows), " 在用:", sum(1 for r in iface_rows if r["inUse"]),
      " 死:", len(dead), " 链:", len(chains), " 前置:", len(preconditions),
      " 列表:", data["stats"]["lists"], " 菜单:", data["stats"]["menus"])
