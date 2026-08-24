# Sdk.Wechat.Models.Work

> 源码: `stdlib/Sdk/Wechat/Models/Work/WechatWorkModels.zan`


## WechatWorkAddCalendarJsonResult (class)

从随附的 C# SDK 生成的共享 DTO 实体。
一个 C# 命名空间/类型标识精确对应一个 Zan 模型。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- public string cal_id;


## WechatWorkAddCheckinRecordInfo (class)

- public string userid;

- public long checkin_time;

- public string location_title;

- public string location_detail;

- public List<string> mediaids;

- public string notes;

- public int device_type;

- public long lat;

- public long lng;

- public string device_detail;

- public string wifiname;

- public string wifimac;


## WechatWorkAddContactWayResult (class)

- public string config_id;

- public string qr_code;


## WechatWorkAddCorpCustomerTagResult (class)

- public WechatWorkCorpTagGroup tag_group;


## WechatWorkAddCorpTagRequestTag (class)

- public string name;

- public long order;


## WechatWorkAddCorpTagResult (class)

- public WechatWorkAddCorpTagResultTagGroup tag_group;


## WechatWorkAddCorpTagResultTagGroup (class)

- public string group_id;

- public string group_name;

- public long create_time;

- public long order;

- public List<WechatWorkAddCorpTagResultTagGroupTag> tag;


## WechatWorkAddCorpTagResultTagGroupTag (class)

- public string id;

- public string name;

- public long create_time;

- public long order;


## WechatWorkAddMessageTemplateResult (class)

- public List<string> fail_list;

- public string msgid;


## WechatWorkAddRuleRequestRuleInfo (class)

- public WechatWorkAddRuleRequestRuleInfoOwnerCorpRange owner_corp_range;

- public WechatWorkAddRuleRequestRuleInfoMemberCorpRange member_corp_range;


## WechatWorkAddRuleRequestRuleInfoMemberCorpRange (class)

- public List<string> groupids;

- public List<string> corpids;


## WechatWorkAddRuleRequestRuleInfoOwnerCorpRange (class)

- public List<string> departmentids;

- public List<string> userids;


## WechatWorkAddRuleResult (class)

- public int rule_id;


## WechatWorkAddScheduleJsonResult (class)

- public string schedule_id;


## WechatWorkAddStrategyTagRequestTag (class)

- public string name;

- public long order;


## WechatWorkAddStrategyTagResult (class)

- public WechatWorkAddStrategyTagResultTagGroup tag_group;


## WechatWorkAddStrategyTagResultTagGroup (class)

- public string group_id;

- public string group_name;

- public long create_time;

- public long order;

- public List<WechatWorkAddStrategyTagResultTagGroupTag> tag;


## WechatWorkAddStrategyTagResultTagGroupTag (class)

- public string id;

- public string name;

- public long create_time;

- public long order;


## WechatWorkAddTagMemberResult (class)

- public string invalidlist;

- public List<long> invalidparty;


## WechatWorkAdminItem (class)

- public string userid;

- public int auth_type;


## WechatWorkAdminList (class)

- public string userid;


## WechatWorkApplyEventRequestApplyData (class)

- public List<WechatWorkApplyEventRequestApplyDataContents> contents;


## WechatWorkApplyEventRequestApplyDataContents (class)

- public string control;

- public string id;

- public WechatWorkApplyEventRequestApplyDataContentsValue value_;


## WechatWorkApplyEventRequestApplyDataContentsValue (class)

- public string text;

- public string new_number;

- public string new_money;

- public WechatWorkApplyEventRequestDate date;

- public WechatWorkApplyEventRequestSelector selector;

- public List<WechatWorkApplyEventRequestMember> members;

- public List<WechatWorkApplyEventRequestDepartment> departments;

- public List<WechatWorkApplyEventRequestFile> files;

- public List<WechatWorkApplyEventRequestTableChildren> children;

- public WechatWorkApplyEventRequestLocation location;

- public List<WechatWorkApplyEventRequestRelatedApproval> related_approval;

- public WechatWorkApplyEventRequestDateRange date_range;

- public string formula;

- public WechatWorkApplyEventRequestVacation vacation;

- public WechatWorkApplyEventRequestAttendance attendance;


## WechatWorkApplyEventRequestApprover (class)

- public int attr;

- public List<string> userid;


## WechatWorkApplyEventRequestAttendance (class)

- public WechatWorkApplyEventRequestAttendanceDateRange date_range;


## WechatWorkApplyEventRequestAttendanceDateRange (class)

- public string type;

- public WechatWorkApplyEventRequestAttendanceDateRangeData new_begin;

- public WechatWorkApplyEventRequestAttendanceDateRangeData new_end;

- public long new_duration;


## WechatWorkApplyEventRequestAttendanceDateRangeData (class)

- public long timestamp;

- public int time_type;


## WechatWorkApplyEventRequestDate (class)

- public string type;

- public long s_timestamp;


## WechatWorkApplyEventRequestDateRange (class)

- public string type;

- public WechatWorkApplyEventRequestDateRangeData new_begin;

- public WechatWorkApplyEventRequestDateRangeData new_end;

- public long new_duration;


## WechatWorkApplyEventRequestDateRangeData (class)

- public long timestamp;

- public int time_type;


## WechatWorkApplyEventRequestDepartment (class)

- public string openapi_id;

- public string name;


## WechatWorkApplyEventRequestFile (class)

- public string file_id;


## WechatWorkApplyEventRequestLocation (class)

- public string latitude;

- public string longitude;

- public string title;

- public string address;

- public long time;


## WechatWorkApplyEventRequestMember (class)

- public string userid;

- public string name;


## WechatWorkApplyEventRequestRelatedApproval (class)

- public string sp_no;


## WechatWorkApplyEventRequestSelector (class)

- public string type;

- public List<WechatWorkApplyEventRequestSelectorOption> options;


## WechatWorkApplyEventRequestSelectorOption (class)

- public string key;

- public List<WechatWorkApplyEventRequestTextLang> value_;


## WechatWorkApplyEventRequestSummaryList (class)

- public List<WechatWorkApplyEventRequestTextLang> summary_info;


## WechatWorkApplyEventRequestTableChildren (class)

- public List<WechatWorkApplyEventRequestApplyDataContents> list;


## WechatWorkApplyEventRequestTextLang (class)

- public string text;

- public string lang;


## WechatWorkApplyEventRequestVacation (class)

- public WechatWorkApplyEventRequestVacationSelector selector;

- public WechatWorkApplyEventRequestVacationAttendance attendance;


## WechatWorkApplyEventRequestVacationAttendance (class)

- public WechatWorkApplyEventRequestAttendanceDateRange date_range;


## WechatWorkApplyEventRequestVacationSelector (class)

- public string type;

- public List<WechatWorkApplyEventRequestSelectorOption> options;


## WechatWorkApplyEventResult (class)

- public string sp_no;


## WechatWorkApprovalCopyTemplateResult (class)

- public string template_id;


## WechatWorkApprovalCreateTemplateRequestTemplateContent (class)

- public List<WechatWorkApprovalCreateTemplateRequestTemplateContentControls> controls;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControls (class)

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsProperty property;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfig config;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfig (class)

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigDate date;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigSelector selector;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigContact contact;

- public WechatWorkApprovalTemplateTipsConfig tips;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigFile file;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigTable table;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigAttendance attendance;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigLocation location;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigRelatedApproval related_approval;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigDateRange date_range;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigAttendance (class)

- public int type;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigAttendanceDateRange date_range;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigAttendanceDateRange (class)

- public string type;

- public int official_holiday;

- public int perday_duration;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigContact (class)

- public string type;

- public string mode;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigDate (class)

- public string type;

- public List<WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigSelectorOptions> options;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigDateRange (class)

- public string type;

- public int official_holiday;

- public int perday_duration;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigFile (class)

- public int is_only_photo;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigLocation (class)

- public int distance;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigRelatedApproval (class)

- public List<string> template_id;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigSelector (class)

- public string type;

- public List<WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigSelectorOptions> options;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigSelectorOptions (class)

- public string key;

- public List<WechatWorkApprovalCreateTemplateRequestTextAndLang> value_;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigTable (class)

- public int print_format;

- public List<WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigTableChildren> children;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigTableChildren (class)

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigTableChildrenProperty property;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigTableChildrenConfig config;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigTableChildrenConfig (class)

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigDate date;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigSelector selector;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigContact contact;

- public WechatWorkApprovalTemplateTipsConfig tips;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigTableChildrenConfigFile file;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigLocation location;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigRelatedApproval related_approval;

- public WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigDateRange date_range;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigTableChildrenConfigFile (class)

- public int is_only_photo;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsConfigTableChildrenProperty (class)

- public string control;

- public string id;

- public List<WechatWorkApplyEventRequestTextLang> title;

- public List<WechatWorkApplyEventRequestTextLang> placeholder;

- public int require;

- public int un_print;


## WechatWorkApprovalCreateTemplateRequestTemplateContentControlsProperty (class)

- public string control;

- public string id;

- public List<WechatWorkApprovalCreateTemplateRequestTextAndLang> title;

- public List<WechatWorkApprovalCreateTemplateRequestTextAndLang> placeholder;

- public int require;

- public int un_print;


## WechatWorkApprovalCreateTemplateRequestTextAndLang (class)

- public string text;

- public string lang;


## WechatWorkApprovalCreateTemplateResult (class)

- public string template_id;


## WechatWorkApprovalTemplateAttendanceConfig (class)

- public int type;

- public WechatWorkApprovalTemplateAttendanceDateRange date_range;


## WechatWorkApprovalTemplateAttendanceDateRange (class)

- public string type;


## WechatWorkApprovalTemplateContactConfig (class)

- public string type;

- public string mode;


## WechatWorkApprovalTemplateDateConfig (class)

- public string type;


## WechatWorkApprovalTemplateLink (class)

- public string title;

- public string url;


## WechatWorkApprovalTemplateOptionRelation (class)

- public string key;

- public List<WechatWorkApprovalTemplateRelatedControl> relation_list;


## WechatWorkApprovalTemplatePlainText (class)

- public string content;


## WechatWorkApprovalTemplateRelatedControl (class)

- public string related_control_id;

- public int action;


## WechatWorkApprovalTemplateRichText (class)

- public List<WechatWorkApprovalTemplateRichTextSegment> sub_text;


## WechatWorkApprovalTemplateRichTextSegment (class)

- public int type;

- public WechatWorkApprovalTemplateRichTextSegmentContent content;


## WechatWorkApprovalTemplateRichTextSegmentContent (class)

- public WechatWorkApprovalTemplatePlainText plain_text;

- public WechatWorkApprovalTemplateLink link;


## WechatWorkApprovalTemplateTableConfig (class)

- public List<WechatWorkGetTemplateDetailResultTemplateContentControls> children;

- public List<string> stat_field;


## WechatWorkApprovalTemplateTipsConfig (class)

- public List<WechatWorkApprovalTemplateTipsContent> tips_content;


## WechatWorkApprovalTemplateTipsContent (class)

- public WechatWorkApprovalTemplateRichText text;

- public string lang;


## WechatWorkApprovalTemplateVacationItem (class)

- public int id;

- public List<WechatWorkGetTemplateDetailResultTextAndLang> name;


## WechatWorkApprovalTemplateVacationList (class)

- public List<WechatWorkApprovalTemplateVacationItem> item;


## WechatWorkApprovalnode (class)

- public int NodeStatus;

- public int NodeAttr;

- public int NodeType;

- public WechatWorkItems Items;


## WechatWorkApprovalnodes (class)

- public List<WechatWorkApprovalnode> ApprovalNode;


## WechatWorkArticle (class)

- public string title;

- public string description;

- public string url;

- public string picurl;

- public string appid;

- public string pagepath;


## WechatWorkAsynchronousJobId (class)

- public string jobid;


## WechatWorkAsynchronousReplacePartyItem (class)

- public string partyid;

- public int action;


## WechatWorkAsynchronousReplacePartyResult (class)

- public List<WechatWorkAsynchronousReplacePartyItem> result;

- public int status;

- public string type;

- public int total;

- public int percentage;

- public int remaintime;


## WechatWorkAsynchronousReplaceUserItem (class)

- public string userid;

- public int action;


## WechatWorkAsynchronousReplaceUserResult (class)

- public List<WechatWorkAsynchronousReplaceUserItem> result;

- public int status;

- public string type;

- public int total;

- public int percentage;

- public int remaintime;


## WechatWorkAttendee (class)

- public string userid;


## WechatWorkAttendeeResult (class)

- public int response_status;

- public string userid;


## WechatWorkAttr (class)

- public string name;

- public string value_;


## WechatWorkAuthUserInfo (class)

- public string userid;


## WechatWorkBaseInfo (class)

- public long date;

- public int record_type;

- public string name;

- public string name_ex;

- public string departs_name;

- public string acctid;

- public WechatWorkRuleInfo rule_info;

- public int day_type;


## WechatWorkBatchGetMaterialItem (class)

- public string media_id;

- public string filename;

- public long update_time;

- public JsonValue articles;


## WechatWorkBatchGetMaterialResult (class)

- public string type;

- public int total_count;

- public int item_count;

- public List<WechatWorkBatchGetMaterialItem> itemlist;


## WechatWorkBeaconInfo (class)

- public double distance;

- public long major;

- public long minor;

- public string uuid;


## WechatWorkBehaviorData (class)

- public long stat_time;

- public int chat_cnt;

- public int message_cnt;

- public double reply_percentage;

- public int avg_reply_time;

- public int negative_feedback_cnt;

- public int new_apply_cnt;

- public int new_contact_cnt;


## WechatWorkCalendar (class)

- public List<string> admins;

- public int set_as_default;

- public string summary;

- public string color;

- public string description;

- public int is_public;

- public WechatWorkPublicRange public_range;

- public int is_corp_calendar;

- public List<WechatWorkShare> shares;


## WechatWorkCalendarResult (class)

- public string cal_id;

- public List<string> admins;

- public int set_as_default;

- public string summary;

- public string color;

- public string description;

- public int is_public;

- public WechatWorkPublicRange public_range;

- public int is_corp_calendar;

- public List<WechatWorkShare> shares;


## WechatWorkCalendarUpdate (class)

- public string cal_id;

- public List<string> admins;

- public int set_as_default;

- public string summary;

- public string color;

- public string description;

- public int is_public;

- public WechatWorkPublicRange public_range;

- public int is_corp_calendar;

- public List<WechatWorkShare> shares;


## WechatWorkCaller (class)

- public string userid;

- public int duration;


## WechatWorkCardActionInfo (class)

- public int type;

- public string url;

- public string appid;

- public string pagepath;


## WechatWorkCardImage (class)

- public string url;

- public double aspect_ratio;


## WechatWorkChatInfo (class)

- public string chatid;

- public string name;

- public string owner;

- public List<string> userlist;


## WechatWorkCheckinBiweekly (class)

- public bool enable_weekday_recurrence;

- public List<int> odd_workdays;

- public List<int> even_workdays;


## WechatWorkCheckinCorrectionReminder (class)

- public bool open_remind;

- public int buka_remind_day;

- public int buka_remind_month;


## WechatWorkCheckinLateRule (class)

- public int offwork_after_time;

- public int onwork_flex_time;

- public bool allow_offwork_after_time;

- public List<WechatWorkCheckinLateTimeRule> timerules;


## WechatWorkCheckinLateTimeRule (class)

- public int offwork_after_time;

- public int onwork_flex_time;


## WechatWorkCheckinMonthApprovalItem (class)

- public int type;

- public int vacation_id;

- public int count;

- public int duration;

- public int time_type;

- public string name;


## WechatWorkCheckinMonthBaseInfo (class)

- public int record_type;

- public string name;

- public string name_ex;

- public string departs_name;

- public WechatWorkCheckinMonthRuleInfo rule_info;

- public string acctid;


## WechatWorkCheckinMonthData (class)

- public WechatWorkCheckinMonthBaseInfo base_info;

- public WechatWorkCheckinMonthSummaryInfo summary_info;

- public List<WechatWorkCheckinMonthExceptionInfo> exception_infos;

- public List<WechatWorkCheckinMonthApprovalItem> sp_items;

- public WechatWorkCheckinMonthOverworkInfo overwork_info;


## WechatWorkCheckinMonthExceptionInfo (class)

- public int exception;

- public int count;

- public int duration;


## WechatWorkCheckinMonthOverworkInfo (class)

- public int workday_over_sec;

- public int holidays_over_sec;

- public int restdays_over_sec;

- public int workdays_over_as_vacation;

- public int workdays_over_as_money;

- public int restdays_over_as_vacation;

- public int restdays_over_as_money;

- public int holidays_over_as_vacation;

- public int holidays_over_as_money;


## WechatWorkCheckinMonthRuleInfo (class)

- public int groupid;

- public string groupname;


## WechatWorkCheckinMonthSummaryInfo (class)

- public int work_days;

- public int regular_days;

- public int rest_days;

- public int except_days;

- public int regular_work_sec;

- public int standard_work_sec;


## WechatWorkCheckinOvertimeCalculation (class)

- public int ot_workingday_time_start;

- public int ot_workingday_time_min;

- public int ot_workingday_time_max;

- public int ot_nonworkingday_time_min;

- public int ot_nonworkingday_time_max;

- public int ot_nonworkingday_spanday_time;

- public WechatWorkCheckinOvertimeRestInfo ot_workingday_restinfo;

- public WechatWorkCheckinOvertimeRestInfo ot_nonworkingday_restinfo;


## WechatWorkCheckinOvertimeDayConfig (class)

- public bool allow_ot;

- public int type;

- public WechatWorkCheckinOvertimeModeConfig apply;

- public WechatWorkCheckinOvertimeModeConfig checkin;

- public WechatWorkCheckinOvertimeModeConfig applycheckin;

- public bool ot_trans_enable;

- public int ot_trans_type;

- public WechatWorkCheckinOvertimeVacation vacation;

- public int ot_time_range;


## WechatWorkCheckinOvertimeDeductionItem (class)

- public int ot_time;

- public int rest_time;


## WechatWorkCheckinOvertimeDeductionRule (class)

- public List<WechatWorkCheckinOvertimeDeductionItem> items;


## WechatWorkCheckinOvertimeFixedTimeRule (class)

- public int fix_time_begin_sec;

- public int fix_time_end_sec;


## WechatWorkCheckinOvertimeInfo (class)

- public int type;

- public bool allow_ot_workingday;

- public bool allow_ot_nonworkingday;

- public WechatWorkCheckinOvertimeCalculation otcheckinfo;

- public WechatWorkCheckinOvertimeCalculation otapplyinfo;


## WechatWorkCheckinOvertimeInfoV2 (class)

- public WechatWorkCheckinOvertimeDayConfig workdayconf;

- public WechatWorkCheckinOvertimeDayConfig restdayconf;

- public WechatWorkCheckinOvertimeDayConfig holidayconf;

- public WechatWorkCheckinOvertimeUnitConfig time_unit_config;


## WechatWorkCheckinOvertimeModeConfig (class)

- public int ot_time_start;

- public int ot_time_min;

- public int ot_time_max;

- public WechatWorkCheckinOvertimeRestInfo restinfo;


## WechatWorkCheckinOvertimeRestInfo (class)

- public int type;

- public WechatWorkCheckinOvertimeFixedTimeRule fix_time_rule;

- public List<WechatWorkCheckinOvertimeFixedTimeRule> fix_time_rule_list;

- public WechatWorkCheckinOvertimeDeductionRule cal_ottime_rule;


## WechatWorkCheckinOvertimeUnitConfig (class)

- public int ot_time_unit;

- public int perday_duration_secs;

- public int rounding_method;

- public int rounding_precision;

- public int step_size;


## WechatWorkCheckinOvertimeVacation (class)

- public int trans_ratio;

- public bool sync_vacation;


## WechatWorkCheckinRange (class)

- public List<long> party_id;

- public List<string> userid;

- public List<long> tagid;


## WechatWorkCheckinReporter (class)

- public string userid;

- public long tagid;


## WechatWorkCheckinReporterInfo (class)

- public List<WechatWorkCheckinReporter> reporters;

- public long updatetime;


## WechatWorkCheckinRestTime (class)

- public int rest_begin_time;

- public int rest_end_time;


## WechatWorkCheckinRuleSchedule (class)

- public int schedule_id;

- public string schedule_name;

- public List<WechatWorkCheckintime> time_section;

- public int limit_aheadtime;

- public int limit_offtime;

- public bool noneed_offwork;

- public bool allow_flex;

- public int flex_on_duty_time;

- public int flex_off_duty_time;

- public WechatWorkCheckinLateRule late_rule;

- public int max_allow_arrive_early;

- public int max_allow_arrive_late;


## WechatWorkCheckinScheduleContainer (class)

- public List<WechatWorkCheckinScheduleDay> scheduleList;


## WechatWorkCheckinScheduleDay (class)

- public int day;

- public WechatWorkCheckinScheduleInfo schedule_info;


## WechatWorkCheckinScheduleInfo (class)

- public int schedule_id;

- public string schedule_name;

- public List<WechatWorkCheckinScheduleTimeSection> time_section;


## WechatWorkCheckinScheduleTimeSection (class)

- public int id;

- public int work_sec;

- public int off_work_sec;

- public int remind_work_sec;

- public int remind_off_work_sec;


## WechatWorkCheckinUserSchedule (class)

- public string userid;

- public int yearmonth;

- public int groupid;

- public string groupname;

- public WechatWorkCheckinScheduleContainer schedule;


## WechatWorkCheckindate (class)

- public List<int> workdays;

- public List<WechatWorkCheckintime> checkintime;

- public int flex_time;

- public bool noneed_offwork;

- public int limit_aheadtime;

- public bool allow_flex;

- public int flex_on_duty_time;

- public int flex_off_duty_time;

- public int max_allow_arrive_early;

- public int max_allow_arrive_late;

- public WechatWorkCheckinLateRule late_rule;

- public WechatWorkCheckinBiweekly biweekly;


## WechatWorkCheckintime (class)

- public int work_sec;

- public int off_work_sec;

- public int remind_work_sec;

- public int remind_off_work_sec;

- public int time_id;

- public bool allow_rest;

- public int rest_begin_time;

- public int rest_end_time;

- public List<WechatWorkCheckinRestTime> rest_times;

- public int earliest_work_sec;

- public int latest_work_sec;

- public int earliest_off_work_sec;

- public int latest_off_work_sec;

- public bool no_need_checkon;

- public bool no_need_checkoff;


## WechatWorkConclusions (class)

- public WechatWorkExternalJsonCOMMONContactWayResultText text;

- public WechatWorkExternalJsonCOMMONContactWayResultImage image;

- public WechatWorkExternalJsonCOMMONContactWayResultLink link;

- public WechatWorkExternalJsonCOMMONContactWayResultMiniprogram miniprogram;


## WechatWorkContactSync (class)

- public string access_token;

- public int expires_in;


## WechatWorkContentItem (class)

- public string key;

- public string value_;


## WechatWorkConversationContent (class)

- public string text;

- public string media_id;

- public string filename;

- public long filesize;

- public string title;

- public string description;

- public string url;

- public double latitude;

- public double longitude;

- public string location_name;

- public string address;


## WechatWorkConversationRecord (class)

- public string msgid;

- public string msgtype;

- public string from;

- public string to;

- public string roomid;

- public long timestamp;

- public WechatWorkConversationContent content;


## WechatWorkCorpTag (class)

- public string id;

- public string name;

- public long create_time;

- public long order;

- public bool deleted;


## WechatWorkCorpTagGroup (class)

- public string group_id;

- public string group_name;

- public long create_time;

- public long order;

- public bool deleted;

- public List<WechatWorkCorpTag> tag;


## WechatWorkCorpgroupBaseUnionIdToExternalUserIdResult (class)

- public List<WechatWorkUnionIdToExternalUserIdResultExternalUserIdInfo> external_userid_info;


## WechatWorkCreateChatResult (class)

- public string chatid;


## WechatWorkCreateCustomerAcquisitionLinkResult (class)

- public WechatWorkCustomerAcquisitionLinkResult link;


## WechatWorkCreateDepartmentResult (class)

- public long id;


## WechatWorkCreateLivingResult (class)

- public string livingid;


## WechatWorkCreateTagResult (class)

- public int tagid;


## WechatWorkCustomerAcquisitionLinkInfo (class)

- public string link_name;

- public string url;

- public long create_time;

- public bool skip_verify;


## WechatWorkCustomerAcquisitionLinkResult (class)

- public string link_id;

- public string link_name;

- public string url;

- public long create_time;


## WechatWorkCustomerAcquisitionPriority (class)

- public int priority_type;

- public List<string> priority_userid_list;


## WechatWorkCustomerAcquisitionRange (class)

- public List<string> user_list;

- public List<long> department_list;


## WechatWorkCustomerTagCorpTagJsonGetCorpTagListResult (class)

- public List<WechatWorkCorpTagGroup> tag_group;


## WechatWorkData (class)

- public string lang;

- public string text;


## WechatWorkDelTagMemberResult (class)

- public int tagid;

- public List<string> userlist;

- public List<long> partylist;


## WechatWorkDepartmentDetail (class)

- public long id;

- public string name;

- public string name_en;

- public List<string> department_leader;

- public long parentid;

- public long order;


## WechatWorkDepartmentIdList (class)

- public long id;

- public long parentid;

- public long order;


## WechatWorkDepartmentList (class)

- public long id;

- public string name;

- public long parentid;

- public long order;


## WechatWorkDeptUser (class)

- public string userid;

- public int department;


## WechatWorkEmphasisContentInfo (class)

- public string title;

- public string desc;


## WechatWorkExceptionInfo (class)

- public int count;

- public int duration;

- public int exception;


## WechatWorkExportContactData (class)

- public string url;

- public long size;

- public string md5;


## WechatWorkExtattr (class)

- public List<WechatWorkAttr> attrs;


## WechatWorkExternalContact (class)

- public string external_userid;

- public string name;

- public string position;

- public string avatar;

- public string corp_name;

- public string corp_full_name;

- public int type;

- public int gender;

- public string unionid;

- public WechatWorkExternalGetExternalContactResultExternalProfile external_profile;


## WechatWorkExternalContactList (class)

- public WechatWorkExternalContact external_contact;

- public WechatWorkExternalExternalJsonGetExternalContactInfoBatchResultFollowUser follow_info;


## WechatWorkExternalExternalJsonAddMessageTemplateRequestAttachment (class)

- public string msgtype;

- public WechatWorkExternalExternalJsonAddMessageTemplateRequestImage image;

- public WechatWorkExternalExternalJsonAddMessageTemplateRequestLink link;

- public WechatWorkExternalExternalJsonAddMessageTemplateRequestMiniprogram miniprogram;

- public WechatWorkExternalExternalJsonAddMessageTemplateRequestVideo video;

- public WechatWorkExternalExternalJsonAddMessageTemplateRequestFile file;


## WechatWorkExternalExternalJsonAddMessageTemplateRequestFile (class)

- public string media_id;


## WechatWorkExternalExternalJsonAddMessageTemplateRequestImage (class)

- public string media_id;

- public string pic_url;


## WechatWorkExternalExternalJsonAddMessageTemplateRequestLink (class)

- public string title;

- public string picurl;

- public string desc;

- public string url;


## WechatWorkExternalExternalJsonAddMessageTemplateRequestMiniprogram (class)

- public string title;

- public string pic_media_id;

- public string appid;

- public string page;


## WechatWorkExternalExternalJsonAddMessageTemplateRequestText (class)

- public string content;


## WechatWorkExternalExternalJsonAddMessageTemplateRequestVideo (class)

- public string media_id;


## WechatWorkExternalExternalJsonGetCorpTagListResult (class)

- public List<WechatWorkTagGroup> tag_group;


## WechatWorkExternalExternalJsonGetExternalContactInfoBatchResultFollowUser (class)

- public string userid;

- public string remark;

- public string description;

- public int createtime;

- public string state;

- public string oper_userid;

- public int add_way;

- public string remark_corp_name;

- public List<string> remark_mobiles;

- public List<string> tag_id;


## WechatWorkExternalExternalJsonGetGroupMsgListV2ResultAttachment (class)

- public string msgtype;

- public WechatWorkExternalExternalJsonGetGroupMsgListV2ResultImage image;

- public WechatWorkExternalExternalJsonGetGroupMsgListV2ResultLink link;

- public WechatWorkExternalExternalJsonGetGroupMsgListV2ResultMiniprogram miniprogram;

- public WechatWorkExternalExternalJsonGetGroupMsgListV2ResultVideo video;

- public WechatWorkExternalExternalJsonGetGroupMsgListV2ResultFile file;


## WechatWorkExternalExternalJsonGetGroupMsgListV2ResultFile (class)

- public string media_id;


## WechatWorkExternalExternalJsonGetGroupMsgListV2ResultImage (class)

- public string media_id;

- public string pic_url;


## WechatWorkExternalExternalJsonGetGroupMsgListV2ResultLink (class)

- public string title;

- public string picurl;

- public string desc;

- public string url;


## WechatWorkExternalExternalJsonGetGroupMsgListV2ResultMiniprogram (class)

- public string title;

- public string pic_media_id;

- public string appid;

- public string page;


## WechatWorkExternalExternalJsonGetGroupMsgListV2ResultText (class)

- public string content;


## WechatWorkExternalExternalJsonGetGroupMsgListV2ResultVideo (class)

- public string media_id;


## WechatWorkExternalExternalJsonListContactWayResultContactWay (class)

- public string config_id;


## WechatWorkExternalExternalJsonSendWelcomeMsgRequestAttachment (class)

- public string msgtype;

- public WechatWorkExternalExternalJsonSendWelcomeMsgRequestImage image;

- public WechatWorkExternalExternalJsonSendWelcomeMsgRequestLink link;

- public WechatWorkExternalExternalJsonSendWelcomeMsgRequestMiniprogram miniprogram;

- public WechatWorkExternalExternalJsonSendWelcomeMsgRequestVideo video;

- public WechatWorkExternalExternalJsonSendWelcomeMsgRequestFile file;


## WechatWorkExternalExternalJsonSendWelcomeMsgRequestFile (class)

- public string media_id;


## WechatWorkExternalExternalJsonSendWelcomeMsgRequestImage (class)

- public string media_id;

- public string pic_url;


## WechatWorkExternalExternalJsonSendWelcomeMsgRequestLink (class)

- public string title;

- public string picurl;

- public string desc;

- public string url;


## WechatWorkExternalExternalJsonSendWelcomeMsgRequestMiniprogram (class)

- public string title;

- public string pic_media_id;

- public string appid;

- public string page;


## WechatWorkExternalExternalJsonSendWelcomeMsgRequestText (class)

- public string content;


## WechatWorkExternalExternalJsonSendWelcomeMsgRequestVideo (class)

- public string media_id;


## WechatWorkExternalGetExternalContactResultExternalAttr (class)

- public int type;

- public string name;

- public WechatWorkExternalGetExternalContactResultText text;

- public WechatWorkExternalGetExternalContactResultWeb web;

- public WechatWorkExternalGetExternalContactResultMiniprogram miniprogram;


## WechatWorkExternalGetExternalContactResultExternalProfile (class)

- public List<WechatWorkExternalGetExternalContactResultExternalAttr> external_attr;


## WechatWorkExternalGetExternalContactResultMiniprogram (class)

- public string appid;

- public string pagepath;

- public string title;


## WechatWorkExternalGetExternalContactResultText (class)

- public string value_;


## WechatWorkExternalGetExternalContactResultWeb (class)

- public string url;

- public string title;


## WechatWorkExternalJsonCOMMONContactWayResultImage (class)

- public string media_id;

- public string pic_url;


## WechatWorkExternalJsonCOMMONContactWayResultLink (class)

- public string title;

- public string picurl;

- public string desc;

- public string url;


## WechatWorkExternalJsonCOMMONContactWayResultMiniprogram (class)

- public string title;

- public string pic_media_id;

- public string appid;

- public string page;


## WechatWorkExternalJsonCOMMONContactWayResultText (class)

- public string content;


## WechatWorkFollowUser (class)

- public string userid;

- public string remark;

- public string description;

- public int createtime;

- public string state;

- public string oper_userid;

- public int add_way;

- public string remark_corp_name;

- public List<string> remark_mobiles;

- public List<WechatWorkFollowUserTags> tags;


## WechatWorkFollowUserTags (class)

- public string group_name;

- public string tag_name;

- public string tag_id;

- public int type;


## WechatWorkGetAdminListResult (class)

- public List<WechatWorkAdminItem> admin;


## WechatWorkGetAgentResult (class)

- public string agentid;

- public string name;

- public string square_logo_url;

- public string round_logo_url;

- public string description;

- public WechatWorkThirdPartyAllowUserinfos allow_userinfos;

- public WechatWorkThirdPartyAllowPartys allow_partys;

- public WechatWorkThirdPartyAllowTags allow_tags;

- public int close;

- public string redirect_domain;

- public int report_location_flag;

- public int isreportuser;


## WechatWorkGetAppInfoAllowPartys (class)

- public List<long> partyid;


## WechatWorkGetAppInfoAllowTags (class)

- public List<int> tagid;


## WechatWorkGetAppInfoAllowUserInfos (class)

- public List<WechatWorkGetAppInfoAllowUserInfosUser> user;


## WechatWorkGetAppInfoAllowUserInfosUser (class)

- public string userid;

- public string status;


## WechatWorkGetAppInfoResult (class)

- public string agentid;

- public string name;

- public string square_logo_url;

- public string description;

- public WechatWorkGetAppInfoAllowUserInfos allow_userinfos;

- public WechatWorkGetAppInfoAllowPartys allow_partys;

- public WechatWorkGetAppInfoAllowTags allow_tags;

- public int close;

- public string redirect_domain;

- public int report_location_flag;

- public int isreportuser;

- public string home_url;


## WechatWorkGetAppListAppInfo (class)

- public string agentid;

- public string name;

- public string square_logo_url;


## WechatWorkGetAppListResult (class)

- public List<WechatWorkGetAppListAppInfo> agentlist;


## WechatWorkGetApprovalDataJsonResult (class)

- public int count;

- public int total;

- public long next_spnum;

- public List<WechatWorkGetApprovalDataJsonResultData> data;


## WechatWorkGetApprovalDataJsonResultData (class)

- public string spname;

- public string apply_name;

- public string apply_org;

- public List<string> approval_name;

- public List<string> notify_name;

- public int sp_status;

- public JsonValue sp_num;

- public WechatWorkGetApprovalDataJsonResultDataExpense expense;

- public WechatWorkGetApprovalDataJsonResultDataLeave leave;

- public WechatWorkGetApprovalDataJsonResultDataComm comm;


## WechatWorkGetApprovalDataJsonResultDataComm (class)

- public string apply_data;


## WechatWorkGetApprovalDataJsonResultDataExpense (class)

- public int expense_type;

- public string reason;

- public List<WechatWorkGetApprovalDataJsonResultDataExpenseItem> item;


## WechatWorkGetApprovalDataJsonResultDataExpenseItem (class)

- public int expenseitem_type;

- public long time;

- public double sums;

- public string reason;


## WechatWorkGetApprovalDataJsonResultDataLeave (class)

- public int timeunit;

- public int leave_type;

- public long start_time;

- public long end_time;

- public int duration;

- public string reason;


## WechatWorkGetApprovalDetailResult (class)

- public WechatWorkGetApprovalDetailResultInfo info;


## WechatWorkGetApprovalDetailResultAttendance (class)

- public WechatWorkGetApprovalDetailResultAttendanceDateRange date_range;


## WechatWorkGetApprovalDetailResultAttendanceDateRange (class)

- public string type;

- public WechatWorkGetApprovalDetailResultAttendanceDateRangeData new_begin;

- public WechatWorkGetApprovalDetailResultAttendanceDateRangeData new_end;

- public long new_duration;


## WechatWorkGetApprovalDetailResultAttendanceDateRangeData (class)

- public long timestamp;

- public int time_type;


## WechatWorkGetApprovalDetailResultDate (class)

- public string type;

- public long s_timestamp;


## WechatWorkGetApprovalDetailResultDateRange (class)

- public string type;

- public WechatWorkGetApprovalDetailResultDateRangeData new_begin;

- public WechatWorkGetApprovalDetailResultDateRangeData new_end;

- public long new_duration;


## WechatWorkGetApprovalDetailResultDateRangeData (class)

- public long timestamp;

- public int time_type;


## WechatWorkGetApprovalDetailResultDepartment (class)

- public string openapi_id;

- public string name;


## WechatWorkGetApprovalDetailResultFile (class)

- public string file_id;


## WechatWorkGetApprovalDetailResultInfo (class)

- public string sp_no;

- public string sp_name;

- public int sp_status;

- public string template_id;

- public long apply_time;

- public WechatWorkGetApprovalDetailResultInfoApplyer applyer;

- public List<WechatWorkGetApprovalDetailResultInfoSpRecord> sp_record;

- public List<WechatWorkGetApprovalDetailResultInfoNotifyer> notifyer;

- public WechatWorkGetApprovalDetailResultInfoApplyData apply_data;

- public List<WechatWorkGetApprovalDetailResultInfoComments> comments;


## WechatWorkGetApprovalDetailResultInfoApplyData (class)

- public List<WechatWorkGetApprovalDetailResultInfoApplyDataContents> contents;


## WechatWorkGetApprovalDetailResultInfoApplyDataContents (class)

- public string control;

- public string id;

- public List<WechatWorkGetApprovalDetailResultTextLang> title;

- public WechatWorkGetApprovalDetailResultInfoApplyDataContentsValue value_;


## WechatWorkGetApprovalDetailResultInfoApplyDataContentsValue (class)

- public string text;

- public List<string> tips;

- public string new_number;

- public string new_money;

- public WechatWorkGetApprovalDetailResultDate date;

- public WechatWorkGetApprovalDetailResultSelector selector;

- public List<WechatWorkGetApprovalDetailResultMember> members;

- public List<WechatWorkGetApprovalDetailResultDepartment> departments;

- public List<WechatWorkGetApprovalDetailResultFile> files;

- public List<WechatWorkGetApprovalDetailResultTableChildren> children;

- public WechatWorkGetApprovalDetailResultLocation location;

- public List<WechatWorkGetApprovalDetailResultRelatedApproval> related_approval;

- public WechatWorkGetApprovalDetailResultDateRange date_range;

- public string formula;

- public WechatWorkGetApprovalDetailResultVacation vacation;

- public WechatWorkGetApprovalDetailResultAttendance attendance;


## WechatWorkGetApprovalDetailResultInfoApplyer (class)

- public string userid;

- public string partyid;


## WechatWorkGetApprovalDetailResultInfoComments (class)

- public WechatWorkGetApprovalDetailResultInfoCommentsCommentUserInfo commentUserInfo;

- public long commenttime;

- public string commentcontent;

- public string commentid;

- public List<string> media_id;


## WechatWorkGetApprovalDetailResultInfoCommentsCommentUserInfo (class)

- public string userid;


## WechatWorkGetApprovalDetailResultInfoNotifyer (class)

- public string userid;


## WechatWorkGetApprovalDetailResultInfoSpRecord (class)

- public int sp_status;

- public int approverattr;

- public List<WechatWorkGetApprovalDetailResultInfoSpRecordDetails> details;


## WechatWorkGetApprovalDetailResultInfoSpRecordDetails (class)

- public WechatWorkGetApprovalDetailResultInfoSpRecordDetailsApprover approver;

- public string speech;

- public int sp_status;

- public long sptime;

- public List<string> media_id;


## WechatWorkGetApprovalDetailResultInfoSpRecordDetailsApprover (class)

- public string userid;


## WechatWorkGetApprovalDetailResultLocation (class)

- public string latitude;

- public string longitude;

- public string title;

- public string address;

- public long time;


## WechatWorkGetApprovalDetailResultMember (class)

- public string userid;

- public string name;


## WechatWorkGetApprovalDetailResultRelatedApproval (class)

- public string sp_no;


## WechatWorkGetApprovalDetailResultSelector (class)

- public string type;

- public List<WechatWorkGetApprovalDetailResultSelectorOption> options;


## WechatWorkGetApprovalDetailResultSelectorOption (class)

- public string key;

- public List<WechatWorkGetApprovalDetailResultTextLang> value_;


## WechatWorkGetApprovalDetailResultTableChildren (class)

- public List<WechatWorkGetApprovalDetailResultInfoApplyDataContents> list;


## WechatWorkGetApprovalDetailResultTextLang (class)

- public string text;

- public string lang;


## WechatWorkGetApprovalDetailResultVacation (class)

- public WechatWorkGetApprovalDetailResultVacationSelector selector;

- public WechatWorkGetApprovalDetailResultVacationAttendance attendance;


## WechatWorkGetApprovalDetailResultVacationAttendance (class)

- public WechatWorkGetApprovalDetailResultAttendanceDateRange date_range;


## WechatWorkGetApprovalDetailResultVacationSelector (class)

- public string type;

- public List<WechatWorkGetApprovalDetailResultSelectorOption> options;


## WechatWorkGetApprovalInfoRequestFilter (class)

- public string key;

- public string value_;


## WechatWorkGetApprovalInfoResult (class)

- public List<string> sp_no_list;


## WechatWorkGetAuthInfoResult (class)

- public WechatWorkThirdPartyAuthCorpInfo auth_corp_info;

- public WechatWorkThirdPartyAuthInfo auth_info;


## WechatWorkGetCalendarJsonResult (class)

- public List<WechatWorkCalendarResult> calendar_list;


## WechatWorkGetChainCorpInfoListItem (class)

- public int groupid;

- public string corpid;

- public string pending_corpid;

- public string corp_name;

- public string custom_id;

- public string invite_userid;

- public bool is_joined;


## WechatWorkGetChainCorpInfoListResult (class)

- public List<WechatWorkGetChainCorpInfoListItem> group_corps;

- public bool has_more;

- public string next_cursor;


## WechatWorkGetChainCorpInfoResult (class)

- public string corp_name;

- public int groupid;

- public string custom_id;

- public int qualification_status;

- public bool is_joined;


## WechatWorkGetChainGroupItem (class)

- public int groupid;

- public string group_name;

- public int parentid;

- public long order;


## WechatWorkGetChainGroupResult (class)

- public List<WechatWorkGetChainGroupItem> groups;


## WechatWorkGetChainListResult (class)

- public List<WechatWorkGetChainListResultChain> chains;


## WechatWorkGetChainListResultChain (class)

- public string chain_id;

- public string chain_name;


## WechatWorkGetChainUserCustomIdResult (class)

- public string user_custom_id;


## WechatWorkGetChatResult (class)

- public WechatWorkChatInfo chat_info;


## WechatWorkGetCheckinDataJsonResult (class)

- public List<WechatWorkGetCheckinDataJsonResultResult> checkindata;


## WechatWorkGetCheckinDataJsonResultResult (class)

- public string userid;

- public string groupname;

- public string checkin_type;

- public string exception_type;

- public long sch_checkin_time;

- public long checkin_time;

- public string location_title;

- public string location_detail;

- public string wifiname;

- public string notes;

- public string wifimac;

- public List<string> mediaids;

- public int lat;

- public int lng;


## WechatWorkGetCheckinDayDataJsonResult (class)

- public List<WechatWorkGetCheckinDayDataJsonResultResult> datas;


## WechatWorkGetCheckinDayDataJsonResultResult (class)

- public WechatWorkBaseInfo base_info;

- public WechatWorkSummaryInfo summary_info;

- public List<WechatWorkHolidayInfo> holiday_infos;

- public List<WechatWorkExceptionInfo> exception_infos;

- public WechatWorkOtInfo ot_info;

- public List<WechatWorkSpItem> sp_items;


## WechatWorkGetCheckinMonthDataJsonResult (class)

- public List<WechatWorkCheckinMonthData> datas;


## WechatWorkGetCheckinOptionJsonResult (class)

- public List<WechatWorkInfo> info;


## WechatWorkGetCheckinScheduleListJsonResult (class)

- public List<WechatWorkCheckinUserSchedule> schedule_list;


## WechatWorkGetConversationRecordsResult (class)

- public bool has_more;

- public string next_cursor;

- public List<WechatWorkConversationRecord> records;


## WechatWorkGetCorpCheckinOptionJsonResult (class)

- public List<WechatWorkGroup> group;


## WechatWorkGetCorpSharedChainListResult (class)

- public string user_custom_id;

- public List<WechatWorkGetCorpSharedChainListResultChains> chains;


## WechatWorkGetCorpSharedChainListResultChains (class)

- public string chain_id;

- public string chain_name;


## WechatWorkGetCorpTokenResult (class)

- public string access_token;

- public int expires_in;


## WechatWorkGetCountResult (class)

- public int total_count;

- public int image_count;

- public int voice_count;

- public int video_count;

- public int file_count;

- public int mpnews_count;


## WechatWorkGetCustomerAcquisitionLinkDetailResult (class)

- public WechatWorkCustomerAcquisitionLinkInfo link;

- public WechatWorkCustomerAcquisitionRange range;

- public WechatWorkCustomerAcquisitionPriority priority_option;


## WechatWorkGetCustomerAcquisitionLinkListResult (class)

- public List<string> link_id_list;

- public string next_cursor;


## WechatWorkGetDepartmentIdListResult (class)

- public List<WechatWorkDepartmentIdList> department_id;


## WechatWorkGetDepartmentListResult (class)

- public List<WechatWorkDepartmentList> department;


## WechatWorkGetDepartmentMemberInfoResult (class)

- public List<WechatWorkGetMemberResult> userlist;


## WechatWorkGetDepartmentMemberResult (class)

- public List<WechatWorkUserListSimple> userlist;


## WechatWorkGetDepartmentResult (class)

- public WechatWorkDepartmentDetail department;


## WechatWorkGetDialRecordJsonResult (class)

- public List<WechatWorkRecord> record;


## WechatWorkGetExportContactResult (class)

- public int status;

- public List<WechatWorkExportContactData> data_list;


## WechatWorkGetExternalContactInfoBatchResult (class)

- public List<WechatWorkExternalContactList> external_contact_list;

- public string next_cursor;


## WechatWorkGetExternalContactListResult (class)

- public List<string> external_userid;


## WechatWorkGetExternalContactResultJson (class)

- public WechatWorkExternalContact external_contact;

- public List<WechatWorkFollowUser> follow_user;

- public string next_cursor;


## WechatWorkGetFollowUserListResult (class)

- public List<string> follow_user;


## WechatWorkGetForeverMpNewsResult (class)

- public string type;

- public WechatWorkGetForeverMpNewsResultMpNews mpnews;


## WechatWorkGetForeverMpNewsResultMpNews (class)

- public JsonValue articles;


## WechatWorkGetGroupChatGroupByDayListResult (class)

- public List<WechatWorkGetGroupChatItem> items;


## WechatWorkGetGroupChatItem (class)

- public string owner;

- public WechatWorkGetGroupChatItemData data;


## WechatWorkGetGroupChatItemData (class)

- public int new_chat_cnt;

- public int chat_total;

- public int chat_has_msg;

- public int new_member_cnt;

- public int member_total;

- public int member_has_msg;

- public int msg_total;


## WechatWorkGetGroupChatListResult (class)

- public int total;

- public int next_offset;

- public List<WechatWorkGetGroupChatItem> items;


## WechatWorkGetGroupMsgListV2Result (class)

- public string next_cursor;

- public List<WechatWorkGroupMsgList> group_msg_list;


## WechatWorkGetGroupMsgSendResultResult (class)

- public string next_cursor;

- public List<WechatWorkSendList> send_list;


## WechatWorkGetGroupMsgTaskResult (class)

- public string next_cursor;

- public List<WechatWorkTaskList> task_list;


## WechatWorkGetHardwareCheckinDataJsonResult (class)

- public List<WechatWorkHardwareCheckinData> checkindata;


## WechatWorkGetInvoiceInfoResultJson (class)

- public string card_id;

- public int begin_time;

- public int end_time;

- public string openid;

- public string type;

- public string payee;

- public string detail;

- public WechatWorkInvoiceUserData user_info;


## WechatWorkGetJoinQrcodeResult (class)

- public string join_qrcode;


## WechatWorkGetKFListResult (class)

- public WechatWorkKFItem external;


## WechatWorkGetLivingCodeResult (class)

- public string living_code;


## WechatWorkGetLivingShareInfoResult (class)

- public string livingid;

- public string viewer_userid;

- public string viewer_external_userid;

- public string invitor_userid;

- public string invitor_external_userid;


## WechatWorkGetLivingStateExternalUserInfo (class)

- public string external_userid;

- public int type;

- public string name;

- public int watch_time;

- public int is_comment;

- public int is_mic;


## WechatWorkGetLivingStateInfo (class)

- public List<WechatWorkGetLivingStateUserInfo> users;

- public List<WechatWorkGetLivingStateExternalUserInfo> external_users;


## WechatWorkGetLivingStateUserInfo (class)

- public string userid;

- public int watch_time;

- public int is_comment;

- public int is_mic;


## WechatWorkGetLoginInfoResult (class)

- public int usertype;

- public WechatWorkLoginInfoUserInfo user_info;

- public WechatWorkLoginInfoCorpInfo corp_info;

- public List<WechatWorkLoginInfoAgentItem> agent;

- public WechatWorkLoginInfoAuthInfo auth_info;


## WechatWorkGetLoginUrlResult (class)

- public string login_url;

- public int expires_in;


## WechatWorkGetMemberIdListResult (class)

- public string next_cursor;

- public List<WechatWorkDeptUser> dept_user;


## WechatWorkGetMemberResult (class)

- public string userid;

- public string name;

- public List<long> department;

- public List<int> order;

- public string position;

- public string mobile;

- public int gender;

- public string email;

- public string biz_mail;

- public List<int> is_leader_in_dept;

- public List<string> direct_leader;

- public string avatar;

- public string alias;

- public int status;

- public string telephone;

- public string english_name;

- public string open_userid;

- public int main_department;

- public WechatWorkExtattr extattr;

- public int enable;

- public string wxplugin_status;

- public string qr_code;

- public WechatWorkMailListMemberMemberBaseExternalProfile external_profile;

- public string external_position;

- public string address;


## WechatWorkGetMessageStatisticsResult (class)

- public List<WechatWorkMessageStatistics> statistics;


## WechatWorkGetMomentList (class)

- public string moment_id;

- public string creator;

- public long create_time;

- public int create_type;

- public int visible_type;

- public WechatWorkGetMomentListText text;

- public List<WechatWorkGetMomentListImage> image;

- public WechatWorkGetMomentListVideo video;

- public WechatWorkGetMomentListLink link;

- public WechatWorkGetMomentListLocation location;


## WechatWorkGetMomentListImage (class)

- public string media_id;


## WechatWorkGetMomentListLink (class)

- public string title;

- public string url;


## WechatWorkGetMomentListLocation (class)

- public long latitude;

- public long longitude;

- public string name;


## WechatWorkGetMomentListResult (class)

- public List<WechatWorkGetMomentList> moment_list;

- public string next_cursor;


## WechatWorkGetMomentListText (class)

- public string content;


## WechatWorkGetMomentListVideo (class)

- public string media_id;

- public string thumb_media_id;


## WechatWorkGetMomentTask (class)

- public string userid;

- public int publish_status;


## WechatWorkGetMomentTaskResult (class)

- public List<WechatWorkGetMomentTask> task_list;

- public string next_cursor;


## WechatWorkGetOpenApprovalData (class)

- public string ThirdNo;

- public string OpenTemplateId;

- public string OpenSpName;

- public int OpenSpstatus;

- public int ApplyTime;

- public string ApplyUsername;

- public string ApplyUserParty;

- public string ApplyUserImage;

- public string ApplyUserId;

- public WechatWorkApprovalnodes ApprovalNodes;

- public WechatWorkNotifynodes NotifyNodes;

- public int approverstep;


## WechatWorkGetOpenApprovalDataJsonResult (class)

- public WechatWorkGetOpenApprovalData data;


## WechatWorkGetPermanentCodeResult (class)

- public string access_token;

- public int expires_in;

- public string permanent_code;

- public WechatWorkThirdPartyAuthCorpInfo auth_corp_info;

- public WechatWorkThirdPartyAuthInfo auth_info;

- public WechatWorkGetPermanentCodeResultAuthUserInfo auth_user_info;


## WechatWorkGetPermanentCodeResultAuthUserInfo (class)

- public string userid;

- public string name;

- public string avatar;


## WechatWorkGetPreAuthCodeResult (class)

- public string pre_auth_code;

- public int expires_in;


## WechatWorkGetRegisterCodeResult (class)

- public string register_code;

- public int expires_in;


## WechatWorkGetRegisterInfoResult (class)

- public string corpid;

- public WechatWorkContactSync contact_sync;

- public WechatWorkAuthUserInfo auth_user_info;

- public string state;


## WechatWorkGetResultResult (class)

- public int status;

- public WechatWorkGetResultResultResult result;


## WechatWorkGetResultResultResult (class)

- public string chain_id;

- public int import_status;

- public List<WechatWorkGetResultResultResultFailList> fail_list;


## WechatWorkGetResultResultResultFailList (class)

- public string custom_id;

- public string corp_name;

- public string errmsg;

- public int errcode;

- public List<WechatWorkGetResultResultResultFailListContactInfoList> contact_info_list;


## WechatWorkGetResultResultResultFailListContactInfoList (class)

- public string mobile;

- public string errmsg;

- public int errcode;


## WechatWorkGetRuleInfoResult (class)

- public WechatWorkGetRuleInfoResultRuleInfo rule_info;


## WechatWorkGetRuleInfoResultRuleInfo (class)

- public WechatWorkGetRuleInfoResultRuleInfoOwnerCorpRange owner_corp_range;

- public WechatWorkGetRuleInfoResultRuleInfoMemberCorpRange member_corp_range;


## WechatWorkGetRuleInfoResultRuleInfoMemberCorpRange (class)

- public List<string> groupids;

- public List<string> corpids;


## WechatWorkGetRuleInfoResultRuleInfoOwnerCorpRange (class)

- public List<string> departmentids;

- public List<string> userids;


## WechatWorkGetScheduleJsonResult (class)

- public List<WechatWorkScheduleItem> schedule_list;


## WechatWorkGetShakeInfoResult (class)

- public WechatWorkShakeInfoData data;


## WechatWorkGetStrategyTagListResult (class)

- public List<WechatWorkGetStrategyTagListResultTagGroup> tag_group;


## WechatWorkGetStrategyTagListResultTagGroup (class)

- public string group_id;

- public string group_name;

- public long create_time;

- public long order;

- public int strategy_id;

- public List<WechatWorkGetStrategyTagListResultTagGroupTag> tag;


## WechatWorkGetStrategyTagListResultTagGroupTag (class)

- public string id;

- public string name;

- public long create_time;

- public long order;


## WechatWorkGetSuiteTokenResult (class)

- public string suite_access_token;

- public int expires_in;


## WechatWorkGetTagListResult (class)

- public List<WechatWorkTagItem> taglist;


## WechatWorkGetTagMemberResult (class)

- public List<WechatWorkTagUserList> userlist;

- public List<long> partylist;


## WechatWorkGetTemplateDetailResult (class)

- public List<WechatWorkGetTemplateDetailResultTextAndLang> template_names;

- public WechatWorkGetTemplateDetailResultTemplateContent template_content;


## WechatWorkGetTemplateDetailResultTemplateContent (class)

- public List<WechatWorkGetTemplateDetailResultTemplateContentControls> controls;


## WechatWorkGetTemplateDetailResultTemplateContentControls (class)

- public WechatWorkGetTemplateDetailResultTemplateContentControlsProperty property;

- public WechatWorkGetTemplateDetailResultTemplateContentControlsConfig config;


## WechatWorkGetTemplateDetailResultTemplateContentControlsConfig (class)

- public WechatWorkApprovalTemplateDateConfig date;

- public WechatWorkGetTemplateDetailResultTemplateContentControlsConfigSelector selector;

- public WechatWorkApprovalTemplateContactConfig contact;

- public WechatWorkApprovalTemplateTableConfig table;

- public WechatWorkApprovalTemplateAttendanceConfig attendance;

- public WechatWorkApprovalTemplateVacationList vacation_list;

- public WechatWorkApprovalTemplateTipsConfig tips;


## WechatWorkGetTemplateDetailResultTemplateContentControlsConfigSelector (class)

- public string type;

- public int exp_type;

- public List<WechatWorkGetTemplateDetailResultTemplateContentControlsConfigSelectorOptions> options;

- public List<WechatWorkApprovalTemplateOptionRelation> op_relations;


## WechatWorkGetTemplateDetailResultTemplateContentControlsConfigSelectorOptions (class)

- public string key;

- public List<WechatWorkGetTemplateDetailResultTextAndLang> value_;


## WechatWorkGetTemplateDetailResultTemplateContentControlsProperty (class)

- public string control;

- public string id;

- public List<WechatWorkGetTemplateDetailResultTextAndLang> title;

- public List<WechatWorkGetTemplateDetailResultTextAndLang> placeholder;

- public int require;

- public int un_print;


## WechatWorkGetTemplateDetailResultTextAndLang (class)

- public string text;

- public string lang;


## WechatWorkGetTicketResultJson (class)

- public string ticket;

- public int expires_in;


## WechatWorkGetTokenResult (class)

- public int expires_in;

- public string access_token;


## WechatWorkGetUserBehaviorDataListResult (class)

- public List<WechatWorkBehaviorData> behavior_data;


## WechatWorkGetUserDetailResult (class)

- public string userid;

- public string name;

- public List<int> department;

- public string position;

- public string mobile;

- public int gender;

- public string email;

- public string avatar;

- public string qr_code;

- public string biz_mail;

- public string address;


## WechatWorkGetUserInfoByTicketResult (class)

- public string corpid;

- public string userid;

- public string name;

- public string mobile;

- public string gender;

- public string email;

- public string avatar;

- public string qr_code;


## WechatWorkGetUserLivingInfoResponse (class)

- public WechatWorkGetUserLivingInfoResult living_info;


## WechatWorkGetUserLivingInfoResult (class)

- public string theme;

- public long living_start;

- public int living_duration;

- public int status;

- public long reserve_start;

- public int reserve_living_duration;

- public string description;

- public string anchor_userid;

- public int main_department;

- public int viewer_num;

- public int comment_num;

- public int mic_num;

- public int open_replay;

- public int replay_status;

- public int type;

- public string push_stream_url;

- public int online_count;

- public int subscribe_count;


## WechatWorkGetUserLivingResponse (class)

- public string next_cursor;

- public List<string> livingid_list;


## WechatWorkGetUserLivingWatchStateResponse (class)

- public int ending;

- public string next_key;

- public WechatWorkGetLivingStateInfo stat_info;


## WechatWorkGetUseridResult (class)

- public string userid;


## WechatWorkGetWorkBenchTemplateJsonResult (class)

- public string type;

- public int agentid;

- public WechatWorkWorkBenchKeyDataModel keydata;

- public WechatWorkWorkBenchImageModel image;

- public WechatWorkWorkBenchListModel list;

- public WechatWorkWorkBenchWebViewModel webview;

- public bool replace_useer_data;


## WechatWorkGroup (class)

- public int grouptype;

- public int groupid;

- public List<WechatWorkCheckindate> checkindate;

- public List<WechatWorkSpeWorkdays> spe_workdays;

- public List<WechatWorkSpeOffdays> spe_offdays;

- public bool sync_holidays;

- public string groupname;

- public bool need_photo;

- public List<WechatWorkWifimacInfos> wifimac_infos;

- public bool note_can_use_local_pic;

- public bool allow_checkin_offworkday;

- public bool allow_apply_offworkday;

- public List<WechatWorkLocInfos> loc_infos;

- public WechatWorkCheckinRange range;

- public long create_time;

- public List<string> white_users;

- public int type;

- public WechatWorkCheckinReporterInfo reporterinfo;

- public WechatWorkCheckinOvertimeInfo ot_info;

- public WechatWorkCheckinOvertimeInfoV2 ot_info_v2;

- public int allow_apply_bk_cnt;

- public int allow_apply_bk_day_limit;

- public bool buka_limit_next_month;

- public bool option_out_range;

- public string create_userid;

- public bool use_face_detect;

- public bool open_face_live_detect;

- public string update_userid;

- public List<WechatWorkCheckinRuleSchedule> schedulelist;

- public int offwork_interval_time;

- public long buka_restriction;

- public int span_day_time;

- public int standard_work_duration;

- public bool open_sp_checkin;

- public int checkin_method_type;

- public bool sync_out_checkin;

- public WechatWorkCheckinCorrectionReminder buka_remind;


## WechatWorkGroupChat (class)

- public string chat_id;

- public string name;

- public string owner;

- public long create_time;

- public string notice;

- public string member_version;

- public List<WechatWorkMemberList> member_list;

- public List<WechatWorkAdminList> admin_list;


## WechatWorkGroupChatAddJoinWayResult (class)

- public string config_id;


## WechatWorkGroupChatGetJoinWayResult (class)

- public WechatWorkJoinWay join_way;


## WechatWorkGroupChatGetResult (class)

- public WechatWorkGroupChat group_chat;


## WechatWorkGroupChatList (class)

- public string chat_id;

- public int status;


## WechatWorkGroupChatListResult (class)

- public List<WechatWorkGroupChatList> group_chat_list;

- public string next_cursor;


## WechatWorkGroupChatOwnerFilter (class)

- public List<string> userid_list;


## WechatWorkGroupMsgList (class)

- public string msgid;

- public string creator;

- public string create_time;

- public int create_type;

- public WechatWorkExternalExternalJsonGetGroupMsgListV2ResultText text;

- public List<WechatWorkExternalExternalJsonGetGroupMsgListV2ResultAttachment> attachments;


## WechatWorkGroupWelcomeTemplateAddResult (class)

- public string template_id;


## WechatWorkGroupWelcomeTemplateGetResult (class)

- public WechatWorkExternalGetExternalContactResultText text;

- public WechatWorkGroupWelcomeTemplateImage image;

- public WechatWorkExternalExternalJsonAddMessageTemplateRequestLink link;

- public WechatWorkExternalGetExternalContactResultMiniprogram miniprogram;


## WechatWorkGroupWelcomeTemplateImage (class)

- public string media_id;

- public string pic_url;


## WechatWorkHardwareCheckinData (class)

- public string userid;

- public long checkin_time;

- public string device_sn;

- public string device_name;


## WechatWorkHolidayInfo (class)

- public string sp_number;

- public WechatWorkSpTitle sp_title;

- public WechatWorkSpDescription sp_description;


## WechatWorkImageTextArea (class)

- public int type;

- public string url;

- public string appid;

- public string pagepath;

- public string title;

- public string desc;

- public string image_url;


## WechatWorkImportChainContactResult (class)

- public string jobid;


## WechatWorkInfo (class)

- public string userid;

- public WechatWorkGroup group;


## WechatWorkInviteMemberListResultJson (class)

- public List<string> invaliduser;

- public List<string> invalidparty;

- public List<string> invalidtag;


## WechatWorkInviteMemberResult (class)

- public int type;


## WechatWorkInvitor (class)

- public string userid;


## WechatWorkInvoiceItem (class)

- public string card_id;

- public string encrypt_code;


## WechatWorkInvoiceUserData (class)

- public int fee;

- public string title;

- public int billing_time;

- public string billing_no;

- public string billing_code;

- public List<WechatWorkProjectInfo> info;

- public int fee_without_tax;

- public int tax;

- public string s_pdf_media_id;

- public string s_trip_pdf_media_id;

- public string check_code;

- public string buyer_number;

- public string buyer_address_and_phone;

- public string buyer_bank_account;

- public string seller_number;

- public string seller_address_and_phone;

- public string seller_bank_account;

- public string remarks;

- public string cashier;

- public string maker;


## WechatWorkItem (class)

- public string ItemName;

- public string ItemParty;

- public string ItemImage;

- public string ItemUserId;

- public int ItemStatus;

- public string ItemSpeech;

- public int ItemOpTime;


## WechatWorkItems (class)

- public List<WechatWorkItem> Item;


## WechatWorkJoinWay (class)

- public string config_id;

- public int scene;

- public string remark;

- public int auto_create_room;

- public string room_base_name;

- public int room_base_id;

- public List<string> chat_id_list;

- public string qr_code;

- public string state;


## WechatWorkKFItem (class)

- public List<string> user;

- public List<int> party;

- public List<int> tag;


## WechatWorkLinkedArticle (class)

- public string title;

- public string description;

- public string url;

- public string picurl;

- public string btntxt;


## WechatWorkLinkedCorpAgentPermissionListResult (class)

- public List<string> userids;

- public List<string> department_ids;


## WechatWorkLinkedCorpDepartment (class)

- public long department_id;

- public string department_name;

- public long parentid;

- public long order;


## WechatWorkLinkedCorpDepartmentListResult (class)

- public List<WechatWorkLinkedCorpDepartment> department_list;


## WechatWorkLinkedCorpExtendedAttribute (class)

- public string name;

- public int type;

- public WechatWorkLinkedCorpTextAttribute text;

- public WechatWorkLinkedCorpWebAttribute web;


## WechatWorkLinkedCorpExtendedAttributes (class)

- public List<WechatWorkLinkedCorpExtendedAttribute> attrs;


## WechatWorkLinkedCorpMassResult (class)

- public List<string> invaliduser;

- public List<string> invalidparty;

- public List<string> invalidtag;


## WechatWorkLinkedCorpSimpleUser (class)

- public string corpid;

- public string userid;

- public string name;

- public List<string> department;


## WechatWorkLinkedCorpSimpleUserListResult (class)

- public List<WechatWorkLinkedCorpSimpleUser> userlist;


## WechatWorkLinkedCorpTextAttribute (class)

- public string value_;


## WechatWorkLinkedCorpUser (class)

- public string mobile;

- public string telephone;

- public string email;

- public string position;

- public WechatWorkLinkedCorpExtendedAttributes extattr;

- public string corpid;

- public string userid;

- public string name;

- public List<string> department;


## WechatWorkLinkedCorpUserGetResult (class)

- public WechatWorkLinkedCorpUser user_info;


## WechatWorkLinkedCorpUserListResult (class)

- public List<WechatWorkLinkedCorpUser> userlist;


## WechatWorkLinkedCorpWebAttribute (class)

- public string url;

- public string title;


## WechatWorkListAppShareInfoResult (class)

- public int ending;

- public string next_cursor;

- public List<WechatWorkListAppShareInfoResultCorpList> corp_list;


## WechatWorkListAppShareInfoResultCorpList (class)

- public string corpid;

- public string corp_name;

- public int agentid;


## WechatWorkListContactWayResult (class)

- public List<WechatWorkExternalExternalJsonListContactWayResultContactWay> contact_way;

- public string next_cursor;


## WechatWorkListIdsResult (class)

- public List<int> rule_ids;


## WechatWorkLivingActivityDetail (class)

- public string description;

- public List<string> image_list;


## WechatWorkLocInfos (class)

- public int lat;

- public int lng;

- public string loc_title;

- public string loc_detail;

- public int distance;


## WechatWorkLoginCheckResultJson (class)

- public string corpid;

- public string userid;

- public string session_key;

- public string open_userid;


## WechatWorkLoginInfoAgentItem (class)

- public int agentid;

- public int auth_type;


## WechatWorkLoginInfoAuthInfo (class)

- public List<WechatWorkLoginInfoAuthInfoDepartmentItem> department;


## WechatWorkLoginInfoAuthInfoDepartmentItem (class)

- public string id;

- public string writable;


## WechatWorkLoginInfoCorpInfo (class)

- public string corpid;


## WechatWorkLoginInfoUserInfo (class)

- public string email;

- public string userid;

- public string name;

- public string avatar;


## WechatWorkMailListMemberMemberBaseExternalAttr (class)

- public int type;

- public string name;

- public WechatWorkMailListMemberMemberBaseText text;

- public WechatWorkMailListMemberMemberBaseWeb web;

- public WechatWorkMailListMemberMemberBaseMiniprogram miniprogram;


## WechatWorkMailListMemberMemberBaseExternalProfile (class)

- public List<WechatWorkMailListMemberMemberBaseExternalAttr> external_attr;


## WechatWorkMailListMemberMemberBaseMiniprogram (class)

- public string appid;

- public string pagepath;

- public string title;


## WechatWorkMailListMemberMemberBaseText (class)

- public string value_;


## WechatWorkMailListMemberMemberBaseWeb (class)

- public string url;

- public string title;


## WechatWorkMainTitle (class)

- public string title;

- public string desc;


## WechatWorkMainTitleInfo (class)

- public string title;

- public string desc;


## WechatWorkMassLinkerCorpLinkerCorpDataFile (class)

- public string media_id;


## WechatWorkMassLinkerCorpLinkerCorpDataImage (class)

- public string media_id;


## WechatWorkMassLinkerCorpLinkerCorpDataNews (class)

- public List<WechatWorkLinkedArticle> articles;


## WechatWorkMassLinkerCorpLinkerCorpDataText (class)

- public string content;


## WechatWorkMassLinkerCorpLinkerCorpDataVideo (class)

- public string media_id;

- public string title;

- public string description;


## WechatWorkMassResult (class)

- public string invaliduser;

- public string invalidparty;

- public string invalidtag;

- public string unlicenseduser;

- public string msgid;

- public string response_code;


## WechatWorkMassUpdateTemplateCardUpdateTemplateCardRequestButton (class)

- public string replace_name;


## WechatWorkMemberList (class)

- public string userid;

- public string group_nickname;

- public string name;

- public int type;

- public int join_time;

- public int join_scene;

- public string unionid;

- public WechatWorkInvitor invitor;


## WechatWorkMessageStatistics (class)

- public long date;

- public int total_send;

- public int total_receive;

- public int text_count;

- public int image_count;

- public int voice_count;

- public int video_count;

- public int file_count;

- public int link_count;

- public int location_count;

- public int active_users;

- public int active_groups;


## WechatWorkMiniprogramNotice (class)

- public string appid;

- public string page;

- public string title;

- public string description;

- public bool emphasis_first_item;

- public List<WechatWorkContentItem> content_item;


## WechatWorkModifyRuleRequestRuleInfo (class)

- public WechatWorkModifyRuleRequestRuleInfoOwnerCorpRange owner_corp_range;

- public WechatWorkModifyRuleRequestRuleInfoMemberCorpRange member_corp_range;


## WechatWorkModifyRuleRequestRuleInfoMemberCorpRange (class)

- public List<string> groupids;

- public List<string> corpids;


## WechatWorkModifyRuleRequestRuleInfoOwnerCorpRange (class)

- public List<string> departmentids;

- public List<string> userids;


## WechatWorkMpArticle (class)

- public string title;

- public string thumb_media_id;

- public string author;

- public string content_source_url;

- public string content;

- public string digest;


## WechatWorkMpnews (class)

- public List<WechatWorkMpArticle> articles;


## WechatWorkNotifynode (class)

- public string ItemName;

- public string ItemParty;

- public string ItemImage;

- public string ItemUserId;


## WechatWorkNotifynodes (class)

- public List<WechatWorkNotifynode> NotifyNode;


## WechatWorkOAuth2OAuth2ResultGetUserInfoResult (class)

- public string CorpId;

- public string UserId;

- public string OpenId;

- public string DeviceId;

- public string user_ticket;

- public string user_doc_ticket;

- public int expires_in;

- public string external_userid;


## WechatWorkOaDataOpenOaDataOpenJsonGetCheckinDayDataJsonResultCheckintime (class)

- public long work_sec;

- public long off_work_sec;


## WechatWorkOtInfo (class)

- public int ot_status;

- public long ot_duration;

- public List<long> exception_duration;


## WechatWorkOwnerFilter (class)

- public List<string> userid_list;

- public List<int> partyid_list;


## WechatWorkProjectInfo (class)

- public string name;

- public int num;

- public string unit;

- public int price;


## WechatWorkPublicRange (class)

- public List<string> userids;

- public List<long> partyids;


## WechatWorkQuoteAreaInfo (class)

- public int type;

- public string url;

- public string appid;

- public string pagepath;

- public string title;

- public string quote_text;


## WechatWorkRecord (class)

- public int call_time;

- public int total_duration;

- public int call_type;

- public WechatWorkCaller caller;

- public JsonValue callee;

- public int calltime;


## WechatWorkReminders (class)

- public int is_remind;

- public int remind_before_event_secs;

- public List<int> remind_time_diffs;

- public int is_repeat;

- public int repeat_type;

- public int repeat_unit;

- public int repeat_until;

- public JsonValue is_custom_repeat;

- public int repeat_interval;

- public List<int> repeat_day_of_week;

- public List<int> repeat_day_of_month;

- public int timezone;


## WechatWorkRuleInfo (class)

- public int groupid;

- public string groupname;

- public int scheduleid;

- public string schedulename;

- public List<WechatWorkOaDataOpenOaDataOpenJsonGetCheckinDayDataJsonResultCheckintime> checkintime;


## WechatWorkSchedule (class)

- public List<string> admins;

- public string organizer;

- public int start_time;

- public int end_time;

- public int is_whole_day;

- public List<WechatWorkAttendee> attendees;

- public string summary;

- public string description;

- public WechatWorkReminders reminders;

- public string location;

- public string cal_id;


## WechatWorkScheduleItem (class)

- public string schedule_id;

- public string organizer;

- public List<WechatWorkAttendeeResult> attendees;

- public string summary;

- public string description;

- public WechatWorkReminders reminders;

- public string location;

- public int start_time;

- public int end_time;

- public int status;

- public int sequence;


## WechatWorkScheduleUpdate (class)

- public string schedule_id;

- public List<string> admins;

- public string organizer;

- public int start_time;

- public int end_time;

- public int is_whole_day;

- public List<WechatWorkAttendee> attendees;

- public string summary;

- public string description;

- public WechatWorkReminders reminders;

- public string location;

- public string cal_id;


## WechatWorkSendList (class)

- public string external_userid;

- public string chat_id;

- public string userid;

- public int status;

- public int send_time;


## WechatWorkSetCheckinScheduleItem (class)

- public string userid;

- public int day;

- public int schedule_id;


## WechatWorkSetMuteResult (class)

- public List<string> invaliduser;


## WechatWorkSetScopeResult (class)

- public List<string> invaliduser;

- public List<int> invalidparty;

- public List<int> invalidtag;


## WechatWorkSetWorkBenchTemplateJsonResult (class)

- public JsonValue Value;


## WechatWorkShakeInfoData (class)

- public string page_id;

- public WechatWorkBeaconInfo beacon_info;

- public string userid;

- public string openid;


## WechatWorkShare (class)

- public string userid;

- public int permission;


## WechatWorkSource (class)

- public string icon_url;

- public string desc;

- public int desc_color;


## WechatWorkSourceInfo (class)

- public string icon_url;

- public string desc;

- public int desc_color;


## WechatWorkSpDescription (class)

- public List<WechatWorkData> data;


## WechatWorkSpItem (class)

- public int type;

- public long vacation_id;

- public string name;

- public long count;

- public long duration;

- public int time_type;


## WechatWorkSpTitle (class)

- public List<WechatWorkData> data;


## WechatWorkSpeOffdays (class)

- public int timestamp;

- public string notes;

- public List<WechatWorkCheckintime> checkintime;

- public int type;

- public int begtime;

- public int endtime;


## WechatWorkSpeWorkdays (class)

- public int timestamp;

- public string notes;

- public List<WechatWorkCheckintime> checkintime;

- public int type;

- public int begtime;

- public int endtime;


## WechatWorkSummaryInfo (class)

- public long checkin_count;

- public long regular_work_sec;

- public long standard_work_sec;

- public long earliest_time;

- public long lastest_time;


## WechatWorkTag (class)

- public string id;

- public string name;

- public long create_time;

- public long order;

- public bool deleted;


## WechatWorkTagGroup (class)

- public string group_id;

- public string group_name;

- public long create_time;

- public long order;

- public bool deleted;

- public List<WechatWorkTag> tag;


## WechatWorkTagItem (class)

- public string tagid;

- public string tagname;


## WechatWorkTagUserList (class)

- public string userid;

- public string name;


## WechatWorkTaskList (class)

- public string userid;

- public int status;

- public int send_time;


## WechatWorkTaskcardBtn (class)

- public string key;

- public string name;

- public string replace_name;

- public string color;

- public bool is_bold;


## WechatWorkTaskcardNotice (class)

- public string title;

- public string description;

- public string url;

- public string task_id;

- public List<WechatWorkTaskcardBtn> btn;


## WechatWorkTemplateCardBase (class)

- public string card_type;

- public WechatWorkSource source;

- public string task_id;

- public WechatWorkMainTitle main_title;


## WechatWorkTextcard (class)

- public string title;

- public string description;

- public string url;

- public string btntxt;


## WechatWorkThirdPartyAgent (class)

- public string agentid;

- public string name;

- public string square_logo_url;

- public string round_logo_url;

- public string appid;

- public int auth_mode;

- public bool is_customized_app;

- public WechatWorkThirdPartyAgentPrivilege privilege;


## WechatWorkThirdPartyAgentPrivilege (class)

- public int level;

- public List<int> allow_party;

- public List<string> allow_user;

- public List<int> allow_tag;

- public List<int> extra_party;

- public List<string> extra_user;

- public List<int> extra_tag;


## WechatWorkThirdPartyAllowPartys (class)

- public List<int> partyid;


## WechatWorkThirdPartyAllowTags (class)

- public List<int> tagid;


## WechatWorkThirdPartyAllowUserinfos (class)

- public List<WechatWorkThirdPartyUser> user;


## WechatWorkThirdPartyAuthCorpInfo (class)

- public string corpid;

- public string corp_name;

- public string corp_type;

- public string corp_square_logo_url;

- public string corp_user_max;

- public string corp_agent_max;

- public string corp_full_name;

- public long verified_end_time;

- public int subject_type;

- public string corp_wxqrcode;

- public string corp_scale;

- public string corp_industry;

- public string corp_sub_industry;


## WechatWorkThirdPartyAuthInfo (class)

- public List<WechatWorkThirdPartyAgent> agent;

- public WechatWorkThirdPartyAuthUserInfo auth_user_info;


## WechatWorkThirdPartyAuthThirdPartyAuthJsonThirdPartyAuthResultGetUserInfoResult (class)

- public string CorpId;

- public string OpenId;

- public string UserId;

- public string DeviceId;

- public string user_ticket;

- public int expires_in;

- public string open_userid;


## WechatWorkThirdPartyAuthUserInfo (class)

- public string email;

- public string mobile;

- public string userid;


## WechatWorkThirdPartyUser (class)

- public string userid;

- public string status;


## WechatWorkTransferSessionResult (class)

- public string userid;

- public string session_key;


## WechatWorkTransferSessionResultJson (class)

- public string userid;

- public string session_key;


## WechatWorkUnionIdToExternalUserIdResultExternalUserIdInfo (class)

- public string corpid;

- public string external_userid;


## WechatWorkUnionIdToPendingIdResult (class)

- public string pending_id;


## WechatWorkUpdateScheduleJsonResult (class)

- public string schedule_id;


## WechatWorkUpdateTaskCardResultJson (class)

- public List<string> invaliduser;


## WechatWorkUpgradeChatIdForNewCorpRequest (class)

- public JsonValue Value;


## WechatWorkUpgradeChatIdForNewCorpResult (class)

- public JsonValue Value;


## WechatWorkUploadForeverResultJson (class)

- public string media_id;


## WechatWorkUserListSimple (class)

- public string userid;

- public string name;

- public List<long> department;


## WechatWorkUserMute (class)

- public string userid;

- public JsonValue status;


## WechatWorkVacationGetCorpConfResult (class)

- public List<WechatWorkVacationGetCorpConfResultLists> lists;


## WechatWorkVacationGetCorpConfResultLists (class)

- public int id;

- public string name;

- public int time_attr;

- public int duration_type;

- public WechatWorkVacationGetCorpConfResultListsQuotaAttr quota_attr;

- public int perday_duration;

- public int is_newovertime;

- public int enter_comp_time_limit;

- public WechatWorkVacationGetCorpConfResultListsExpireRule expire_rule;


## WechatWorkVacationGetCorpConfResultListsExpireRule (class)

- public int type;

- public int duration;

- public WechatWorkVacationGetCorpConfResultListsExpireRuleDate date;

- public bool extern_duration_enable;

- public WechatWorkVacationGetCorpConfResultListsExpireRuleDate extern_duration;


## WechatWorkVacationGetCorpConfResultListsExpireRuleDate (class)

- public int month;

- public int day;


## WechatWorkVacationGetCorpConfResultListsQuotaAttr (class)

- public int type;

- public long autoreset_time;

- public long autoreset_duration;

- public int quota_rule_type;

- public WechatWorkVacationGetCorpConfResultListsQuotaAttrQuotaRules quota_rules;

- public bool at_entry_date;

- public int auto_reset_month_day;


## WechatWorkVacationGetCorpConfResultListsQuotaAttrQuotaRules (class)

- public List<WechatWorkVacationGetCorpConfResultListsQuotaAttrQuotaRulesList> list;

- public bool based_on_actual_work_time;


## WechatWorkVacationGetCorpConfResultListsQuotaAttrQuotaRulesList (class)

- public int quota;

- public int begin;

- public int end;


## WechatWorkVacationGetUserVacationQuotaResult (class)

- public List<WechatWorkVacationGetUserVacationQuotaResultLists> lists;


## WechatWorkVacationGetUserVacationQuotaResultLists (class)

- public int id;

- public int assignduration;

- public int usedduration;

- public int leftduration;

- public string vacationname;

- public int real_assignduration;


## WechatWorkVoice (class)

- public string media_id;


## WechatWorkWebhookWebhookNewsNews (class)

- public List<WechatWorkArticle> articles;


## WechatWorkWifimacInfos (class)

- public string wifiname;

- public string wifimac;


## WechatWorkWorkArticle (class)

- public string title;

- public string description;

- public string url;

- public string picurl;


## WechatWorkWorkBenchImageModel (class)

- public string url;

- public string jump_url;

- public string pagepath;


## WechatWorkWorkBenchKeyDataItemModel (class)

- public string key;

- public string data;

- public string jump_url;

- public string pagepath;


## WechatWorkWorkBenchKeyDataModel (class)

- public List<WechatWorkWorkBenchKeyDataItemModel> items;


## WechatWorkWorkBenchListModel (class)

- public JsonValue Value;


## WechatWorkWorkBenchWebViewModel (class)

- public string url;

- public string jump_url;

- public string pagepath;

- public string height;

- public bool hide_title;

- public bool enable_webview_click;
