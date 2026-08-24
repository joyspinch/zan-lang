# Sdk.Wechat.Models.Mp

> 源码: `stdlib/Sdk/Wechat/Models/Mp/WechatMpModels.zan`


## WechatMpActionInfo (class)

从随附的 C# SDK 生成的共享 DTO 实体。
一个 C# 命名空间/类型标识精确对应一个 Zan 模型。
由 scripts/generate_wechat_mp.py 从 Senparc.Weixin.MP.AdvancedAPIs 生成。

- public List<WechatMpActionList> action_list;


## WechatMpActionList (class)

- public string type;

- public string name;

- public string link;

- public string image;

- public string showtype;

- public string appid;

- public string text;


## WechatMpAddCardAfterPayResultJson (class)

- public string rule_id;

- public List<WechatMpFailMchidItem> fail_mchid_list;

- public List<string> succ_mchid_list;


## WechatMpAddDraftResultJson (class)

- public string media_id;


## WechatMpAddExpressResult (class)

- public long template_id;


## WechatMpAddFriendAutoReplyInfo (class)

- public int type;

- public string content;


## WechatMpAddGroupResult (class)

- public int group_id;


## WechatMpAddLotteryInfoResultJson (class)

- public string lottery_id;

- public int page_id;


## WechatMpAddPageResultJson (class)

- public WechatMpBasePageData data;


## WechatMpAddPayMemberRuleResultJson (class)

- public List<WechatMpFailMchidItem> fail_mchid_list;

- public List<string> succ_mchid_list;


## WechatMpAddProductResult (class)

- public string product_id;


## WechatMpAddShelfResult (class)

- public int shelf_id;


## WechatMpAddStoreJsonResult (class)

- public WechatMpAuditId data;


## WechatMpAddStoreResultData (class)

- public int audit_id;


## WechatMpAddStoreResultJson (class)

- public WechatMpAddStoreResultData data;


## WechatMpAddTemplateJsonResult (class)

- public string priTmplId;


## WechatMpAddtemplateJsonResult (class)

- public string template_id;


## WechatMpAfterPayBatchGetResultJson (class)

- public int total_count;

- public List<WechatMpRuleInfo> rule_list;


## WechatMpAfterPayGetByIdResultJson (class)

- public WechatMpRuleInfo rule_info;


## WechatMpAiCropJsonResult (class)

- public List<WechatMpAiCropResult> results;

- public WechatMpImgSize img_size;


## WechatMpAiCropResult (class)

- public int crop_left;

- public int crop_top;

- public int crop_right;

- public int crop_bottom;


## WechatMpAllCategoryInfo (class)

- public List<WechatMpCategory> categories;


## WechatMpApiGetAuthorizerInfoResultJson (class)

- public WechatMpAuthorizerInfo authorizer_info;

- public string qrcode_url;

- public WechatMpAuthorizationInfo authorization_info;


## WechatMpApiQueryAuthResultJson (class)

- public WechatMpAuthorizationInfo authorization_info;


## WechatMpApplyCodeDownloadJsonResult (class)

- public string buffer;


## WechatMpApplyCodeJsonResult (class)

- public long application_id;


## WechatMpApplyCodeQueryJsonResult (class)

- public string status;

- public long application_id;

- public string isv_application_id;

- public List<WechatMpOneCodeRange> code_generate_list;

- public long create_time;

- public long update_time;


## WechatMpAttrext (class)

- public WechatMpMerChantProductProductPostDataLocation location;

- public int isPostFree;

- public int isHasReceipt;

- public int isUnderGuaranty;

- public int isSupportReplace;


## WechatMpAuditId (class)

- public long audit_id;


## WechatMpAuditResultId (class)

- public int has_audit_id;

- public long audit_id;


## WechatMpAuthDataResultJson (class)

- public string invoice_status;

- public int auth_time;

- public WechatMpUserAuthInfo user_auth_info;


## WechatMpAuthFieldData (class)

- public WechatMpUserFiledData user_field;

- public WechatMpBizField biz_field;


## WechatMpAuthFieldResultJson (class)

- public WechatMpAuthFieldData auth_field;


## WechatMpAuthUrlResultJson (class)

- public string auth_url;

- public string appid;


## WechatMpAuthorizationInfo (class)

- public string authorizer_appid;

- public string authorizer_access_token;

- public int expires_in;

- public string authorizer_refresh_token;

- public List<WechatMpFuncInfo> func_info;


## WechatMpAuthorizerInfo (class)

- public string nick_name;

- public string head_img;

- public WechatMpServiceTypeInfo service_type_info;

- public WechatMpVerifyTypeInfo verify_type_info;

- public string user_name;

- public string alias;


## WechatMpBankCardJsonResult (class)

- public string number;


## WechatMpBaseForm (class)

- public List<string> common_field_id_list;

- public List<string> custom_field_list;

- public List<WechatMpRichField> rich_field_list;


## WechatMpBaseInfoResult (class)

- public string id;

- public string status;

- public WechatMpCardBaseInfoBaseSubMerchantInfo sub_merchant_info;

- public string logo_url;

- public int code_type;

- public string brand_name;

- public string title;

- public string sub_title;

- public string color;

- public string notice;

- public string service_phone;

- public string source;

- public string description;

- public int use_limit;

- public long get_limit;

- public bool use_custom_code;

- public string get_custom_code_mode;

- public bool bind_openid;

- public bool can_share;

- public bool can_give_friend;

- public List<string> location_id_list;

- public WechatMpCardBaseInfoDateInfo date_info;

- public WechatMpCardBaseInfoSku sku;

- public int url_name_type;

- public string custom_url;

- public string custom_url_name;

- public string custom_url_sub_title;

- public string custom_app_brand_user_name;

- public string custom_app_brand_pass;

- public string promotion_url_name;

- public string promotion_url;

- public string promotion_url_sub_title;

- public string promotion_app_brand_user_name;

- public string promotion_app_brand_pass;

- public WechatMpModifyMsgOperation modify_msg_operation;

- public bool use_all_locations;

- public bool need_push_on_view;

- public WechatMpCardBaseInfoMemberCardPayInfo pay_info;


## WechatMpBasePageData (class)

- public long page_id;


## WechatMpBasicInfo (class)

- public string activity_bg_color;

- public string activity_tinyappid;

- public int begin_time;

- public int end_time;

- public int gift_num;

- public int max_partic_times_act;

- public int max_partic_times_one_day;

- public string mch_code;


## WechatMpBatchGetCardMerchantJsonResult (class)

- public List<WechatMpGetCardMerchantJsonResult> list;

- public string next_get;


## WechatMpBatchGetUserInfoData (class)

- public string openid;

- public string lang;


## WechatMpBatchGetUserInfoJsonResult (class)

- public List<WechatMpUserInfoJson> user_info_list;


## WechatMpBizField (class)

- public int show_title;

- public int show_tax_no;

- public int show_addr;

- public int show_phone;

- public int show_bank_type;

- public int show_bank_no;

- public List<WechatMpCustomFieldItem> custom_field;


## WechatMpBizFieldInfo (class)

- public string title;

- public string phone;

- public string tax_no;

- public string addr;

- public string bank_type;

- public string bank_no;

- public List<WechatMpCustomFieldItem> custom_field;


## WechatMpBizLicenseJsonResult (class)

- public string reg_num;

- public string serial;

- public string legal_representative;

- public string enterprise_name;

- public string type_of_organization;

- public string address;

- public string type_of_enterprise;

- public string business_scope;

- public string registered_capital;

- public string paid_in_capital;

- public string valid_period;

- public string registered_date;

- public WechatMpPosition cert_position;


## WechatMpBlockTemplateMessageInfo (class)

- public long id;

- public string tmpl_msg_id;

- public string title;

- public string content;

- public long send_timestamp;

- public string openid;


## WechatMpBonusRule (class)

- public int cost_money_unit;

- public int increase_bonus;

- public int max_increase_bonus;

- public int init_increase_bonus;

- public int cost_bonus_unit;

- public int reduce_money;

- public int least_money_to_use_bonus;

- public int max_reduce_bonus;


## WechatMpBrandInfo (class)

- public WechatMpActionInfo action_info;


## WechatMpBrandInfoActionInfo (class)

- public List<WechatMpBrandInfoActionList> action_list;


## WechatMpBrandInfoActionList (class)

- public string type;


## WechatMpBrandInfoBaseInfo (class)

- public string title;

- public string thumb_url;

- public string brand_tag;

- public int category_id;

- public string store_mgr_type;

- public List<string> store_vendorid_list;

- public string color;


## WechatMpBrandInfoDetailInfo (class)

- public List<string> banner_list;

- public List<string> detail_list;


## WechatMpBrandInfoModuleInfo (class)

- public List<WechatMpBrandInfoModuleList> module_list;


## WechatMpBrandInfoModuleList (class)

- public string type;

- public string native_show;

- public string anti_fake_url;


## WechatMpBusiness (class)

- public WechatMpCardStoreStoreResultJsonBaseInfo base_info;


## WechatMpBusinessData (class)

- public WechatMpStoreBaseData base_info;


## WechatMpCardBaseInfoBase (class)

- public WechatMpCardBaseInfoBaseSubMerchantInfo sub_merchant_info;

- public string logo_url;

- public int code_type;

- public string brand_name;

- public string title;

- public string sub_title;

- public string color;

- public string notice;

- public string service_phone;

- public string source;

- public string description;

- public int use_limit;

- public long get_limit;

- public bool use_custom_code;

- public string get_custom_code_mode;

- public bool bind_openid;

- public bool can_share;

- public bool can_give_friend;

- public List<string> location_id_list;

- public WechatMpCardBaseInfoDateInfo date_info;

- public WechatMpCardBaseInfoSku sku;

- public int url_name_type;

- public string custom_url;

- public string custom_url_name;

- public string custom_url_sub_title;

- public string custom_app_brand_user_name;

- public string custom_app_brand_pass;

- public string promotion_url_name;

- public string promotion_url;

- public string promotion_url_sub_title;

- public string promotion_app_brand_user_name;

- public string promotion_app_brand_pass;

- public WechatMpModifyMsgOperation modify_msg_operation;

- public bool use_all_locations;

- public bool need_push_on_view;

- public WechatMpCardBaseInfoMemberCardPayInfo pay_info;


## WechatMpCardBaseInfoBaseSubMerchantInfo (class)

- public int merchant_id;


## WechatMpCardBaseInfoDateInfo (class)

- public string type;

- public long begin_timestamp;

- public long end_timestamp;

- public int fixed_term;

- public int fixed_begin_term;


## WechatMpCardBaseInfoMemberCardPayInfo (class)

- public WechatMpCardBaseInfoMemberCardSwipeCard swipe_card;


## WechatMpCardBaseInfoMemberCardSwipeCard (class)

- public bool is_swipe_card;


## WechatMpCardBaseInfoSku (class)

- public int quantity;

- public int total_quantity;


## WechatMpCardBatchGetResultJson (class)

- public List<string> card_id_list;

- public int total_num;


## WechatMpCardBoardingPassResult (class)

- public string from;

- public string to;

- public string flight;

- public string departure_time;

- public string landing_time;

- public string check_in_url;

- public string gate;

- public string boarding_time;

- public string air_model;

- public WechatMpBaseInfoResult base_info;


## WechatMpCardCashResult (class)

- public double least_cost;

- public double reduce_cost;

- public WechatMpBaseInfoResult base_info;


## WechatMpCardCell (class)

- public long end_time;

- public string card_id;


## WechatMpCardConsumeResultJson (class)

- public WechatMpCardId card;

- public string openid;


## WechatMpCardCreateResultJson (class)

- public string card_id;


## WechatMpCardDecryptResultJson (class)

- public string code;


## WechatMpCardDeleteResultJson (class)

- public JsonValue Value;


## WechatMpCardDetail (class)

- public string card_type;

- public WechatMpCardGeneralCouponResult general_coupon;

- public WechatMpCardGrouponResult groupon;

- public WechatMpCardGiftResult gift;

- public WechatMpCardCashResult cash;

- public WechatMpCardDisCountResult discount;

- public WechatMpCardMemberCardResult member_card;

- public WechatMpCardScenicTicketResult scenic_ticket;

- public WechatMpCardMovieTicketResult movie_ticket;

- public WechatMpCardBoardingPassResult boarding_pass;

- public WechatMpCardLuckyMoneyResult lucky_money;

- public WechatMpCardMeetingTicketResult meeting_ticket;


## WechatMpCardDetailGetResultJson (class)

- public WechatMpCardDetail card;


## WechatMpCardDisCountResult (class)

- public double discount;

- public WechatMpBaseInfoResult base_info;


## WechatMpCardExtInfo (class)

- public string nonce_str;

- public WechatMpUserCard user_card;


## WechatMpCardGeneralCouponResult (class)

- public string default_detail;

- public WechatMpBaseInfoResult base_info;


## WechatMpCardGetResultJson (class)

- public string openid;

- public bool can_consume;

- public int user_card_status;

- public WechatMpGetCard card;


## WechatMpCardGetUrlResultJson (class)

- public string url;


## WechatMpCardGiftResult (class)

- public string gift;

- public WechatMpBaseInfoResult base_info;


## WechatMpCardGiftcardGiftCardDataBaseInfo (class)

- public List<string> mchid_list;

- public int begin_time;

- public int end_time;

- public string status;

- public string create_time;

- public string update_time;


## WechatMpCardGiftcardMktActivityDataInfo (class)

- public WechatMpBasicInfo basic_info;

- public List<WechatMpCardInfoList> card_info_list;

- public WechatMpCustomInfo custom_info;


## WechatMpCardGrouponResult (class)

- public string deal_detail;

- public WechatMpBaseInfoResult base_info;


## WechatMpCardId (class)

- public string card_id;


## WechatMpCardInfoList (class)

- public string card_id;

- public int min_amt;

- public string membership_appid;


## WechatMpCardListItem (class)

- public string code;

- public string card_id;


## WechatMpCardLuckyMoneyResult (class)

- public WechatMpBaseInfoResult base_info;


## WechatMpCardMeetingTicketResult (class)

- public string meeting_detail;

- public string map_url;

- public WechatMpBaseInfoResult base_info;


## WechatMpCardMemberCardResult (class)

- public bool supply_bonus;

- public bool supply_balance;

- public string bonus_cleared;

- public string bonus_rules;

- public string balance_rules;

- public string prerogative;

- public string bind_old_card_url;

- public string activate_url;

- public string background_pic_url;

- public bool wx_activate;

- public bool auto_activate;

- public WechatMpCustomField custom_field1;

- public WechatMpCustomField custom_field2;

- public WechatMpCustomField custom_field3;

- public WechatMpCustomCell custom_cell1;

- public WechatMpBonusRule bonus_rule;

- public int discount;

- public WechatMpBaseInfoResult base_info;


## WechatMpCardMovieTicketResult (class)

- public string detail;

- public WechatMpBaseInfoResult base_info;


## WechatMpCardPicItem (class)

- public string background_pic_url;

- public string outer_img_id;

- public string default_gifting_msg;


## WechatMpCardScenicTicketResult (class)

- public string ticket_class;

- public string guide_url;

- public WechatMpBaseInfoResult base_info;


## WechatMpCardStoreStoreResultJsonBaseInfo (class)

- public string business_name;

- public string address;

- public string telephone;

- public string city;

- public string province;

- public double longitude;

- public double latitude;

- public List<WechatMpPhotoList> photo_list;

- public string open_time;

- public string poi_id;

- public int status;

- public string district;

- public string qualification_num;

- public string qualification_name;


## WechatMpCardStoreStoreResultJsonGetStoreListResultJson (class)

- public List<WechatMpBusiness> business_list;


## WechatMpCardStoreStoreResultJsonLocation (class)

- public double lat;

- public double lng;


## WechatMpCateGoryCollection (class)

- public List<WechatMpMerchantCategory> categories;


## WechatMpCateItem (class)

- public int id;

- public string name;


## WechatMpCategory (class)

- public int id;

- public string name;

- public int level;

- public List<int> children;

- public int father;

- public WechatMpQualify qualify;

- public int scene;

- public int sensitive_type;


## WechatMpCell (class)

- public string title;

- public string url;


## WechatMpChangeOpenIdJsonResult (class)

- public List<WechatMpChangeOpenIdResultItem> result_list;


## WechatMpChangeOpenIdResultItem (class)

- public string ori_openid;

- public string new_openid;

- public string err_msg;


## WechatMpCheckCodeResultJson (class)

- public List<string> exist_code;

- public List<string> not_exist_code;


## WechatMpCheckQualificationJsonResult (class)

- public int result;


## WechatMpCodeActiveQueryJsonResult (class)

- public string code;

- public long application_id;

- public string isv_application_id;

- public string activity_name;

- public string product_brand;

- public string product_title;

- public string product_code;

- public string wxa_appid;

- public string wxa_path;

- public int wxa_type;

- public long code_start;

- public long code_end;


## WechatMpCommJsonResult (class)

- public List<WechatMpCommonItem> items;


## WechatMpCommodityItem (class)

- public string card_id;

- public string title;

- public string pic_url;

- public string desc;


## WechatMpCommonItem (class)

- public string text;

- public WechatMpPos pos;


## WechatMpConTrolResult (class)

- public string biz_control_type;

- public string system_biz_control_type;

- public List<WechatMpPageListItem> list;


## WechatMpConfirmInfo (class)

- public int need_confirm;

- public int already_confirm;


## WechatMpContact (class)

- public string phone;

- public int time_out;


## WechatMpCreateActivityResultJson (class)

- public string activity_id;


## WechatMpCreateCardResultJson (class)

- public string card_id;


## WechatMpCreateGroupResult (class)

- public WechatMpGroupsJsonGroup group;


## WechatMpCreateMapPoiJsonResult (class)

- public string error;

- public WechatMpCreateMapPoiJsonResultData data;


## WechatMpCreateMapPoiJsonResultData (class)

- public long base_id;

- public long rich_id;


## WechatMpCreateMapPoiResultJson (class)

- public string error;

- public WechatMpCreateMapPoiResultJsonData data;


## WechatMpCreateMapPoiResultJsonData (class)

- public int base_id;

- public int rich_id;


## WechatMpCreateQRResultJson (class)

- public string ticket;

- public string url;

- public string show_qrcode_url;


## WechatMpCreateQrCodeResult (class)

- public string ticket;

- public int expire_seconds;

- public string url;


## WechatMpCreateStoreBusiness (class)

- public WechatMpStoreBaseInfo base_info;


## WechatMpCreateTagResult (class)

- public WechatMpTagJsonTag tag;


## WechatMpCustomCell (class)

- public string name;

- public string tips;

- public string url;

- public string app_brand_user_name;

- public string app_brand_pass;


## WechatMpCustomField (class)

- public int name_type;

- public string url;

- public string app_brand_user_name;

- public string app_brand_pass;


## WechatMpCustomFieldItem (class)

- public string key;

- public string value_;


## WechatMpCustomInfo (class)

- public string type;


## WechatMpCustomInfoJson (class)

- public List<WechatMpCustomServiceCustomerServiceManageJsonCustomInfoJsonCustomInfoJson> kf_list;


## WechatMpCustomItem (class)

- public int StartStandards;

- public int StartFees;

- public int AddStandards;

- public int AddFees;

- public string DestCountry;

- public string DestProvince;

- public string DestCity;


## WechatMpCustomOnlineJson (class)

- public List<WechatMpCustomServiceCustomerServiceManageJsonCustomOnlineJsonCustomOnlineJson> kf_online_list;


## WechatMpCustomServiceCustomerServiceManageJsonCustomInfoJsonCustomInfoJson (class)

- public string kf_account;

- public string kf_nick;

- public int kf_id;

- public string kf_headimgurl;

- public string kf_wx;

- public string invite_wx;

- public long invite_expire_time;

- public string invite_status;


## WechatMpCustomServiceCustomerServiceManageJsonCustomOnlineJsonCustomOnlineJson (class)

- public string kf_account;

- public int status;

- public int kf_id;

- public int auto_accept;

- public int accepted_case;


## WechatMpData (class)

- public WechatMpAllCategoryInfo all_category_info;


## WechatMpDeletePayMemberRuleResultJson (class)

- public List<WechatMpFailMchidItem> fail_mchid_list;

- public List<WechatMpFailMchidItem> succ_mchid_list;


## WechatMpDeliveryInfo (class)

- public int delivery_type;

- public int template_id;

- public List<WechatMpExpress> express;


## WechatMpDetail (class)

- public string text;

- public string img;


## WechatMpDeviceApplyData (class)

- public long apply_id;

- public int audit_status;

- public string audit_comment;


## WechatMpDeviceApplyDataDeviceIdentifiers (class)

- public long device_id;

- public string uuid;

- public long major;

- public long minor;


## WechatMpDeviceApplyResultJson (class)

- public WechatMpDeviceApplyData data;


## WechatMpDeviceListData (class)

- public List<WechatMpDevicesListDevicesItem> devices;

- public long date;

- public string total_count;

- public string page_index;


## WechatMpDeviceListResultJson (class)

- public WechatMpDeviceListData data;


## WechatMpDeviceSearchData (class)

- public List<WechatMpDeviceSearchDataDevices> devices;

- public int total_count;


## WechatMpDeviceSearchDataDevices (class)

- public string comment;

- public long device_id;

- public long major;

- public long minor;

- public string page_ids;

- public int status;

- public long poi_id;

- public string uuid;


## WechatMpDeviceSearchResultJson (class)

- public WechatMpDeviceSearchData data;


## WechatMpDevicesListDevicesItem (class)

- public string device_id;

- public string major;

- public string minor;

- public string uuid;

- public int shake_pv;

- public int shake_uv;

- public int click_pv;

- public int click_uv;


## WechatMpDistrict (class)

- public int id;

- public string name;

- public string fullname;

- public List<string> pinyin;

- public WechatMpWxaMerchantJsonDistrictResultJsonLocation location;

- public List<int> cidx;


## WechatMpDistrictResultJson (class)

- public int status;

- public string message;

- public string data_version;

- public List<WechatMpDistrict> result;


## WechatMpDownGiftCardPageResultJson (class)

- public WechatMpConTrolResult control_info;


## WechatMpDraftContent (class)

- public List<WechatMpDraftContentItem> news_item;


## WechatMpDraftContentItem (class)

- public string url;

- public string title;

- public string author;

- public string digest;

- public string content;

- public string content_source_url;

- public string thumb_media_id;

- public string show_cover_pic;

- public int need_open_comment;

- public int only_fans_can_comment;


## WechatMpDraftItem (class)

- public string title;

- public string author;

- public string digest;

- public string content;

- public string content_source_url;

- public string thumb_media_id;

- public int show_cover_pic;

- public long need_open_comment;

- public long only_fans_can_comment;

- public string url;


## WechatMpDraftListItem (class)

- public string media_id;

- public WechatMpDraftContent content;

- public long update_time;


## WechatMpDraftListResultJson (class)

- public List<WechatMpDraftListItem> item;

- public int total_count;

- public int item_count;


## WechatMpDraftModel (class)

- public string title;

- public string author;

- public string digest;

- public string content;

- public string content_source_url;

- public string thumb_media_id;

- public string show_cover_pic;

- public int need_open_comment;

- public int only_fans_can_comment;


## WechatMpDraftSwitchResultJson (class)

- public int is_open;


## WechatMpDrivingJsonResult (class)

- public string plate_num;

- public string vehicle_type;

- public string owner;

- public string addr;

- public string use_character;

- public string model;

- public string vin;

- public string engine_num;

- public string register_date;

- public string issue_date;

- public string plate_num_b;

- public string record;

- public string passengers_num;

- public string total_quality;

- public string prepare_quality;

- public string overall_size;

- public WechatMpPosition card_position_front;

- public WechatMpPosition card_position_back;

- public WechatMpImgSize img_size;


## WechatMpDrivingLicenseJsonResult (class)

- public string id_num;

- public string name;

- public string sex;

- public string nationality;

- public string address;

- public string birth_date;

- public string issue_date;

- public string car_class;

- public string valid_from;

- public string valid_to;

- public string official_seal;


## WechatMpExpress (class)

- public int id;

- public int price;


## WechatMpExterList (class)

- public List<WechatMpInnerList> inner_list;


## WechatMpFailMchidItem (class)

- public string mchid;

- public int errcode;

- public string errmsg;

- public int occupy_rule_id;

- public string occupy_appid;


## WechatMpFetchShortenJsonResult (class)

- public string long_data;

- public long create_time;

- public int expire_seconds;


## WechatMpFilter (class)

- public int count;


## WechatMpForeverNewsItem (class)

- public string url;

- public string thumb_media_id;

- public string author;

- public string title;

- public string content_source_url;

- public string content;

- public string digest;

- public string show_cover_pic;

- public string thumb_url;

- public int need_open_comment;

- public int only_fans_can_comment;


## WechatMpFreePublishArticleDetail (class)

- public int count;

- public List<WechatMpFreePublishItem> item;


## WechatMpFreePublishBatchGetResultJson (class)

- public List<WechatMpFreePublishListItem> item;

- public int total_count;

- public int item_count;


## WechatMpFreePublishGetArticleItemJson (class)

- public bool is_deleted;

- public string url;

- public string title;

- public string author;

- public string digest;

- public string content;

- public string content_source_url;

- public string thumb_media_id;

- public string show_cover_pic;

- public int need_open_comment;

- public int only_fans_can_comment;


## WechatMpFreePublishGetArticleResultJson (class)

- public List<WechatMpFreePublishGetArticleItemJson> news_item;


## WechatMpFreePublishItem (class)

- public int idx;

- public string article_url;


## WechatMpFreePublishListItem (class)

- public string article_id;

- public WechatMpFreePublishGetArticleResultJson content;

- public long update_time;


## WechatMpFuncInfo (class)

- public WechatMpFuncscopeCategory funcscope_category;

- public WechatMpConfirmInfo confirm_info;


## WechatMpFuncscopeCategory (class)

- public int id;


## WechatMpGenerateShortenJsonResult (class)

- public string short_key;


## WechatMpGetActivateTempInfoResultJson (class)

- public WechatMpUserinfoGetResultUserInfo info;


## WechatMpGetAllExpressResult (class)

- public List<WechatMpTemplateInfo> templates_info;


## WechatMpGetAllGroup (class)

- public List<WechatMpGroupsDetail> groups_detail;


## WechatMpGetAllShelfResult (class)

- public List<WechatMpShelfItem> shelves;


## WechatMpGetApplyProtocolCategory (class)

- public int primary_category_id;

- public string category_name;

- public List<WechatMpGetApplyProtocolSecondaryCategory> secondary_category;


## WechatMpGetApplyProtocolJsonResult (class)

- public List<WechatMpGetApplyProtocolCategory> category;


## WechatMpGetApplyProtocolSecondaryCategory (class)

- public int secondary_category_id;

- public string category_name;

- public List<string> need_qualification_stuffs;

- public int can_choose_prepaid_card;

- public int can_choose_payment_card;


## WechatMpGetAuditStatusResultJson (class)

- public WechatMpGetAuditStatusResultJsonData data;


## WechatMpGetAuditStatusResultJsonData (class)

- public long apply_time;

- public string audit_comment;

- public int audit_status;

- public long audit_time;


## WechatMpGetBillAuthUrlResultJson (class)

- public string auth_url;

- public int expire_time;


## WechatMpGetByFilterResult (class)

- public List<WechatMpOrder> order_list;


## WechatMpGetByIdExpressResult (class)

- public WechatMpTemplateInfo template_info;


## WechatMpGetByIdGroup (class)

- public WechatMpMerChantGroupGroupResultGroupDetail group_detail;


## WechatMpGetByIdOrderResult (class)

- public WechatMpOrder order;


## WechatMpGetByIdShelfResult (class)

- public WechatMpShelfInfo shelf_info;

- public string shelf_banner;

- public string shelf_name;

- public int shelf_id;


## WechatMpGetByStatusProductInfo (class)

- public string product_id;

- public int status;

- public WechatMpProductBase product_base;

- public List<WechatMpSkuList> sku_list;

- public WechatMpAttrext attrext;

- public WechatMpDeliveryInfo delivery_info;


## WechatMpGetByStatusResult (class)

- public List<WechatMpGetByStatusProductInfo> products_info;


## WechatMpGetCard (class)

- public string card_id;

- public string begin_time;

- public string end_time;


## WechatMpGetCardBizuinInfoList (class)

- public string ref_date;

- public int view_cnt;

- public int view_user;

- public int receive_cnt;

- public int receive_user;

- public int verify_cnt;

- public int verify_user;

- public int given_cnt;

- public int given_user;

- public int expire_cnt;

- public int expire_user;


## WechatMpGetCardBizuinInfoResultJson (class)

- public List<WechatMpGetCardBizuinInfoList> list;


## WechatMpGetCardInfoItem (class)

- public string ref_date;

- public string card_id;

- public int card_type;

- public int view_cnt;

- public int view_user;

- public int receive_cnt;

- public int receive_user;

- public int verify_cnt;

- public int verify_user;

- public int given_cnt;

- public int given_user;

- public int expire_cnt;

- public int expire_user;


## WechatMpGetCardInfoResultJson (class)

- public List<WechatMpGetCardInfoItem> list;


## WechatMpGetCardListResultJson (class)

- public List<WechatMpCardListItem> card_list;


## WechatMpGetCardMemberCardDetailResultJson (class)

- public List<WechatMpGetCardMemberCardInfoItem> GetCardMemberCardDetail;


## WechatMpGetCardMemberCardInfoItem (class)

- public string ref_date;

- public int view_cnt;

- public int view_user;

- public int receive_cnt;

- public int receive_user;

- public int active_user;

- public int verify_cnt;

- public int verify_user;

- public int total_user;

- public int total_receive_user;


## WechatMpGetCardMemberCardInfoResultJson (class)

- public List<WechatMpGetCardMemberCardInfoItem> GetCardMemberCardInfo;


## WechatMpGetCardMerchantJsonResult (class)

- public string appid;

- public string name;

- public int primary_category_id;

- public int secondary_category_id;

- public string submit_time;

- public int result;


## WechatMpGetCategoryJsonResult (class)

- public List<WechatMpGetCategoryJsonResulttData> data;


## WechatMpGetCategoryJsonResulttData (class)

- public int id;

- public string name;


## WechatMpGetCategoryResult (class)

- public List<string> category_list;


## WechatMpGetCoinsInfoResultJson (class)

- public int free_coin;

- public int pay_coin;

- public int total_coin;


## WechatMpGetContactResultJson (class)

- public WechatMpContact contact;


## WechatMpGetCouponPutData (class)

- public string shop_id;

- public int card_status;

- public string card_id;

- public string card_describe;

- public string start_date;

- public string end_date;


## WechatMpGetCurrentAutoreplyInfoResult (class)

- public int is_add_friend_reply_open;

- public int is_autoreply_open;

- public WechatMpAddFriendAutoReplyInfo add_friend_autoreply_info;

- public WechatMpMessageDefaultAutoReplyInfo message_default_autoreply_info;

- public WechatMpKeywordAutoReplyInfo keyword_autoreply_info;


## WechatMpGetDepositCountResultJson (class)

- public int count;


## WechatMpGetDeviceListData (class)

- public int recordcount;

- public int pageindex;

- public int pagecount;

- public List<WechatMpGetDeviceListDataRecord> records;


## WechatMpGetDeviceListDataRecord (class)

- public long shop_id;

- public string ssid;

- public string bssid;


## WechatMpGetDeviceListResult (class)

- public WechatMpGetDeviceListData data;


## WechatMpGetDeviceStatusData (class)

- public long apply_time;

- public string audit_comment;

- public int audit_status;

- public long audit_time;


## WechatMpGetDeviceStatusResultJson (class)

- public WechatMpGetDeviceStatusData data;


## WechatMpGetDistrictResultJson (class)

- public int status;

- public string message;

- public string data_version;

- public List <List<WechatMpResult>> result;


## WechatMpGetDraftCountResultJson (class)

- public int total_count;


## WechatMpGetDraftResultJson (class)

- public List<WechatMpDraftItem> news_item;


## WechatMpGetForeverMediaResultJson (class)

- public string content_type;


## WechatMpGetForeverMediaVideoResultJson (class)

- public string title;

- public string description;

- public string down_url;


## WechatMpGetFreePublishResultJson (class)

- public string publish_id;

- public int publish_status;

- public string article_id;

- public WechatMpFreePublishArticleDetail article_detail;

- public List<int> fail_idx;


## WechatMpGetGiftCardPageInfoResultJson (class)

- public WechatMpGiftCardPageData page;


## WechatMpGetGiftCardPageListResultJson (class)

- public List<string> page_id_list;


## WechatMpGetGroupIdResult (class)

- public int groupid;


## WechatMpGetHomePageData (class)

- public long shop_id;

- public int template_id;

- public string url;


## WechatMpGetHomePageResult (class)

- public WechatMpGetHomePageData date;


## WechatMpGetHtmlResultJson (class)

- public string content;


## WechatMpGetIndustryItem (class)

- public string first_class;

- public string second_class;


## WechatMpGetIndustryJsonResult (class)

- public WechatMpGetIndustryItem primary_industry;

- public WechatMpGetIndustryItem secondary_industry;


## WechatMpGetInvoiceInfoResultJson (class)

- public string card_id;

- public int begin_time;

- public int end_time;

- public string openid;

- public string type;

- public string payee;

- public string detail;

- public WechatMpUserInfo user_info;


## WechatMpGetInvoiceListResultJson (class)

- public List<WechatMpInvoiceItemInfo> item_list;


## WechatMpGetMediaCountResultJson (class)

- public int voice_count;

- public int video_count;

- public int image_count;

- public int news_count;


## WechatMpGetMerchantAuditInfo (class)

- public int audit_id;

- public int status;

- public string reason;


## WechatMpGetMerchantAuditInfoJson (class)

- public WechatMpMerchantAuditInfo data;


## WechatMpGetMerchantAuditInfoResultJson (class)

- public WechatMpGetMerchantAuditInfo data;


## WechatMpGetMerchantCategoryResult (class)

- public WechatMpWxaMerchantJsonMerchatResultJsonAllCategoryInfo data;


## WechatMpGetMerchantCategoryResultJson (class)

- public WechatMpData data;


## WechatMpGetMsgList (class)

- public string openid;

- public string opercode;

- public string text;

- public string time;

- public string worker;


## WechatMpGetMsgListResultJson (class)

- public List<WechatMpGetMsgList> recordList;

- public int number;

- public long msgid;


## WechatMpGetNewsResultJson (class)

- public List<WechatMpForeverNewsItem> news_item;


## WechatMpGetOrderListOrderList (class)

- public string order_id;

- public string status;

- public int create_time;

- public int pay_finish_time;

- public string desc;

- public string free_coin_count;

- public string pay_coin_count;

- public string refund_free_coin_count;

- public string refund_pay_coin_count;

- public string openid;

- public string order_type;


## WechatMpGetOrderListResultJson (class)

- public int total_num;

- public List<WechatMpGetOrderListOrderList> order_list;


## WechatMpGetPDFResultJson (class)

- public string pdf_url;

- public int pdf_url_expire_time;


## WechatMpGetPayMchResultJson (class)

- public WechatMpPayMchInfoData paymch_info;


## WechatMpGetPayMemberRuleResultJson (class)

- public string card_id;

- public string occupy_appid;

- public bool is_locked;


## WechatMpGetPrivateTemplateJsonResult (class)

- public List<WechatMpGetPrivateTemplateTemplateItem> template_list;


## WechatMpGetPrivateTemplateTemplateItem (class)

- public string template_id;

- public string title;

- public string primary_industry;

- public string deputy_industry;

- public string content;

- public string example;


## WechatMpGetProductResult (class)

- public WechatMpProductInfoData product_info;


## WechatMpGetPropertyResult (class)

- public List<WechatMpPropertyItem> properties;


## WechatMpGetPubTemplateKeyWordsByIdJsonResult (class)

- public string count;

- public List<WechatMpGetPubTemplateKeyWordsByIdJsonResultData> data;


## WechatMpGetPubTemplateKeyWordsByIdJsonResultData (class)

- public int kid;

- public string name;

- public string example;

- public string rule;


## WechatMpGetPubTemplateTitlesJsonResult (class)

- public List<WechatMpGetPubTemplateTitlesJsonResultData> data;

- public int count;


## WechatMpGetPubTemplateTitlesJsonResultData (class)

- public string tid;

- public string title;

- public int type;

- public string categoryId;


## WechatMpGetQrcodeData (class)

- public string qrcode_url;


## WechatMpGetQrcodeResult (class)

- public WechatMpGetQrcodeData data;


## WechatMpGetRecordResult (class)

- public int retcode;

- public List<WechatMpRecordJson> recordlist;


## WechatMpGetSendResult (class)

- public string msg_id;

- public string msg_status;


## WechatMpGetSessionListResultJson (class)

- public List<WechatMpSingleSessionList> sessionlist;


## WechatMpGetSessionStateResultJson (class)

- public string kf_account;

- public string createtime;


## WechatMpGetShakeInfoResultJson (class)

- public WechatMpShakeInfoData data;


## WechatMpGetSkuResult (class)

- public List<WechatMpSku> sku_table;


## WechatMpGetSpeedResult (class)

- public int speed;

- public int realspeed;


## WechatMpGetStatisticsData (class)

- public string shop_id;

- public long statis_time;

- public int total_user;

- public int homepage_uv;

- public int new_fans;

- public int total_fans;


## WechatMpGetStatisticsResult (class)

- public List<WechatMpGetStatisticsData> date;


## WechatMpGetStoreBaseInfo (class)

- public int available_state;

- public int update_status;

- public string sid;

- public string business_name;

- public string branch_name;

- public string province;

- public string city;

- public string district;

- public string address;

- public List<string> categories;

- public int offset_type;

- public string longitude;

- public string latitude;

- public string telephone;

- public List<WechatMpStorePhoto> photo_list;

- public string recommend;

- public string special;

- public string introduction;

- public string open_time;

- public int avg_price;


## WechatMpGetStoreBusiness (class)

- public WechatMpGetStoreBaseInfo base_info;


## WechatMpGetStoreCardResultJson (class)

- public string card_id;


## WechatMpGetStoreInfoResultJson (class)

- public WechatMpBusiness business;


## WechatMpGetStoreListBaseInfo (class)

- public string sid;

- public string poi_id;

- public string business_name;

- public string branch_name;

- public string address;

- public string telephone;

- public List<string> categories;

- public string city;

- public string province;

- public int offset_type;

- public double longitude;

- public double latitude;

- public List<WechatMpGetStoreListBaseInfoPhotoList> photo_list;

- public string introduction;

- public string recommend;

- public string special;

- public string open_time;

- public int avg_price;

- public int available_state;

- public string district;

- public int update_status;


## WechatMpGetStoreListBaseInfoPhotoList (class)

- public string photo_url;


## WechatMpGetStoreListBusiness (class)

- public WechatMpGetStoreListBaseInfo base_info;


## WechatMpGetStoreResultJson (class)

- public WechatMpGetStoreBusiness business;


## WechatMpGetSubResult (class)

- public List<WechatMpCateItem> cate_list;


## WechatMpGetTemplateListJsonResult (class)

- public List<WechatMpGetTemplateListJsonResultData> data;


## WechatMpGetTemplateListJsonResultData (class)

- public string priTmplId;

- public string title;

- public string content;

- public string example;

- public int type;


## WechatMpGetUserTitleUrlResultJson (class)

- public string url;


## WechatMpGetWaitCaseResultJson (class)

- public int count;

- public List<WechatMpSingleWaitCase> waitcaselist;


## WechatMpGetpayPriceResultJson (class)

- public string order_id;

- public string price;

- public string free_coin;

- public string pay_coin;


## WechatMpGiftCardItem (class)

- public string card_id;

- public double price;

- public string code;

- public string default_gifting_msg;

- public string background_pic_url;

- public string outer_img_id;

- public string accepter_openid;


## WechatMpGiftCardOrder (class)

- public string order_id;

- public string page_id;

- public string trans_id;

- public int create_time;

- public int pay_finish_time;

- public double total_price;

- public string open_id;

- public string accepter_openid;

- public string outer_str;

- public bool IsChatRoom;

- public List<WechatMpGiftCardItem> card_list;


## WechatMpGiftCardOrderItemResultJson (class)

- public WechatMpGiftCardOrder order;


## WechatMpGiftCardOrderListResultJson (class)

- public int total_count;

- public List<WechatMpGiftCardOrder> order_list;


## WechatMpGiftCardPageData (class)

- public int page_id;

- public string page_title;

- public bool support_multi;

- public bool support_buy_for_self;

- public string banner_pic_url;

- public List<WechatMpGiftCardThemeItem> theme_list;

- public List<WechatMpThemeCategoryItem> category_list;

- public string address;

- public string service_phone;

- public string biz_description;

- public bool need_receipt;

- public WechatMpCell cell_1;

- public WechatMpCell cell_2;


## WechatMpGiftCardThemeItem (class)

- public string theme_pic_url;

- public string title;

- public string title_color;

- public bool show_sku_title_first;

- public List<WechatMpCommodityItem> item_list;

- public List<WechatMpCardPicItem> pic_item_list;

- public int category_index;

- public bool is_banner;


## WechatMpGroup (class)

- public int group_id;


## WechatMpGroupAddData (class)

- public string group_id;

- public string group_name;


## WechatMpGroupAddResultJson (class)

- public WechatMpGroupAddData data;


## WechatMpGroupDetail (class)

- public string group_name;

- public List<string> product_list;


## WechatMpGroupGetDetailData (class)

- public string group_id;

- public string group_name;

- public int total_count;

- public List<WechatMpGroupGetDetailDevices> devices;


## WechatMpGroupGetDetailDevices (class)

- public string device_id;

- public string uuid;

- public string major;

- public string minor;

- public string comment;

- public string poi_id;


## WechatMpGroupGetDetailResultJson (class)

- public WechatMpGroupGetDetailData data;


## WechatMpGroupGetListData (class)

- public int total_count;

- public List<WechatMpGroupGetListGroups> groups;


## WechatMpGroupGetListGroups (class)

- public string group_id;

- public string group_name;


## WechatMpGroupGetListResultJson (class)

- public WechatMpGroupGetListData data;


## WechatMpGroupsDetail (class)

- public int group_id;

- public string group_name;


## WechatMpGroupsJson (class)

- public List<WechatMpGroupsJsonGroup> groups;


## WechatMpGroupsJsonGroup (class)

- public int id;

- public string name;

- public int count;


## WechatMpIdCardJsonResult (class)

- public string type;

- public string name;

- public string id;

- public string addr;

- public string gender;

- public string nationality;

- public string valid_date;


## WechatMpImgSize (class)

- public int w;

- public int h;


## WechatMpInnerList (class)

- public string name;


## WechatMpInsertCardResultJson (class)

- public string code;

- public string openid;

- public string unionid;


## WechatMpInvoiceBaseInfo (class)

- public string logo_url;

- public string title;

- public string custom_url_name;

- public string custom_url;

- public string custom_url_sub_title;

- public string promotion_url_name;

- public string promotion_url;

- public string promotion_url_sub_title;


## WechatMpInvoiceDetail (class)

- public string fpqqlsh;

- public string jym;

- public string kprq;

- public string fpdm;

- public string fphm;

- public string pdfurl;


## WechatMpInvoiceInvoiceJsonInvoicePlatformResultJsonInfo (class)

- public string name;

- public int num;

- public string unit;

- public int price;


## WechatMpInvoiceItem (class)

- public string card_id;

- public string encrypt_code;


## WechatMpInvoiceItemInfo (class)

- public string card_id;

- public int begin_time;

- public int end_time;

- public string openid;

- public string type;

- public string payee;

- public string detail;

- public WechatMpUserInfo user_info;


## WechatMpInvoicePlatformUserData (class)

- public string s_pdf_media_id;

- public string s_trip_pdf_media_id;

- public int fee;

- public string title;

- public int billing_time;

- public string billing_no;

- public string billing_code;

- public List<WechatMpProjectInfo> info;

- public int fee_without_tax;

- public int tax;

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


## WechatMpKeywordAutoReplyInfo (class)

- public List<WechatMpKeywordAutoReplyInfoItem> list;


## WechatMpKeywordAutoReplyInfoItem (class)

- public string rule_name;

- public long create_time;

- public int reply_mode;

- public List<WechatMpKeywordListInfoItem> keyword_list_info;

- public List<WechatMpReplyListInfoItem> reply_list_info;


## WechatMpKeywordListInfoItem (class)

- public int type;

- public int match_mode;

- public string content;


## WechatMpListResultJson (class)

- public List<WechatMpListResultJsonComment> comment;

- public int total;


## WechatMpListResultJsonComment (class)

- public int user_comment_id;

- public long create_time;

- public string content;

- public int comment_type;

- public string openid;

- public WechatMpListResultJsonCommentReply reply;


## WechatMpListResultJsonCommentReply (class)

- public string content;

- public long create_time;


## WechatMpM1GroupInfo (class)

- public WechatMpFilter filter;

- public int group_id;


## WechatMpM2GroupInfos (class)

- public List<WechatMpGroup> groups;


## WechatMpMapPoiData (class)

- public List<WechatMpPoiInMap> item;


## WechatMpMediaListNewsItem (class)

- public string media_id;

- public WechatMpMediaNewsContent content;

- public long update_time;


## WechatMpMediaListNewsResult (class)

- public List<WechatMpMediaListNewsItem> item;

- public int total_count;

- public int item_count;


## WechatMpMediaListOthersItem (class)

- public string media_id;

- public string name;

- public long update_time;

- public string url;


## WechatMpMediaListOthersResult (class)

- public List<WechatMpMediaListOthersItem> item;

- public int total_count;

- public int item_count;


## WechatMpMediaNewsContent (class)

- public List<WechatMpMediaNewsContentItem> news_item;


## WechatMpMediaNewsContentItem (class)

- public string url;

- public string thumb_url;

- public string thumb_media_id;

- public string author;

- public string title;

- public string content_source_url;

- public string content;

- public string digest;

- public string show_cover_pic;

- public int need_open_comment;

- public int only_fans_can_comment;


## WechatMpMemberCardDealResultJson (class)

- public double result_bonus;

- public double result_balance;

- public string openid;


## WechatMpMemberRule (class)

- public string card_id;

- public int least_cost;

- public int max_cost;

- public string jump_url;

- public string app_brand_id;

- public string app_brand_pass;


## WechatMpMenuJsonResult (class)

- public string content;


## WechatMpMerChantGroupGroupResultGroupDetail (class)

- public int group_id;

- public string group_name;

- public List<string> product_list;


## WechatMpMerChantProductProductPostDataLocation (class)

- public string country;

- public string province;

- public string city;

- public string address;


## WechatMpMerchantAuditInfo (class)

- public long audit_id;

- public int status;

- public string reason;


## WechatMpMerchantCategory (class)

- public int id;

- public string name;

- public int level;

- public List<int> children;

- public int father;

- public WechatMpQualifyExterList qualify;

- public int scene;

- public int sensitive_type;


## WechatMpMerchantInfoGetResultJson (class)

- public List<string> brand_tag_list;

- public List<WechatMpMerchantInfoGetVerifiedList> verified_list;


## WechatMpMerchantInfoGetVerifiedCateList (class)

- public string verified_cate_id;

- public string verified_cate_name;


## WechatMpMerchantInfoGetVerifiedList (class)

- public string verified_firm_code;

- public List<WechatMpMerchantInfoGetVerifiedCateList> verified_cate_list;


## WechatMpMessageDefaultAutoReplyInfo (class)

- public int type;

- public string content;


## WechatMpModifyMsgOperation (class)

- public WechatMpCardCell card_cell;

- public WechatMpUrlCell url_cell;


## WechatMpNewsInfo (class)

- public List<WechatMpNewsInfoItem> list;


## WechatMpNewsInfoItem (class)

- public string title;

- public string author;

- public string digest;

- public int show_cover;

- public string cover_url;

- public string content_url;

- public string source_url;


## WechatMpNewsModel (class)

- public string thumb_media_id;

- public string author;

- public string title;

- public string content_source_url;

- public string content;

- public string digest;

- public string show_cover_pic;

- public string thumb_url;

- public int need_open_comment;

- public int only_fans_can_comment;


## WechatMpNonTaxDownloadBillJsonResult (class)

- public string content;


## WechatMpNonTaxDownloadBillRequest (class)

- public string appid;

- public string mch_id;

- public string bill_date;

- public string bill_type;


## WechatMpNonTaxFeeItem (class)

- public long no;

- public string item_id;

- public string item_name;

- public long overdue;

- public long penalty;

- public long fee;


## WechatMpNonTaxGetOrderJsonResult (class)

- public string appid;

- public string openid;

- public string order_id;

- public long create_time;

- public long pay_finish_time;

- public string desc;

- public long fee;

- public int fee_type;

- public string trans_id;

- public int status;

- public string bank_id;

- public string bank_name;

- public string bank_account;

- public long refund_finish_time;

- public string refund_reason;

- public string refund_order_id;

- public string refund_out_id;

- public string payment_notice_no;

- public string order_no;

- public string department_code;

- public string department_name;

- public int payment_notice_type;

- public string region_code;

- public List<WechatMpNonTaxFeeItem> items;

- public string bill_type_code;

- public string bill_no;

- public int payment_info_source;

- public WechatMpNonTaxPartialRefundInfo partial_refund_info;

- public List<WechatMpNonTaxNotifyHistory> notify_history;

- public string scene;


## WechatMpNonTaxGetOrderListJsonResult (class)

- public List<string> order_id_list;

- public string paid_order_id;


## WechatMpNonTaxMicroPayJsonResult (class)

- public string order_id;


## WechatMpNonTaxNotifyDetail (class)

- public long notify_time;

- public int ret;

- public string ret_errmsg;

- public long cost_time;

- public string wxnontaxstr;

- public int status;

- public string url;

- public int errcode;

- public string errmsg;

- public string third_resp;

- public string third_resp_data;


## WechatMpNonTaxNotifyHistory (class)

- public string appid;

- public string name;

- public List<WechatMpNonTaxNotifyDetail> notify_detail;

- public int notify_cnt;


## WechatMpNonTaxPartialRefundInfo (class)

- public string refund_order_id;

- public string refund_reason;

- public long refund_fee;

- public long refund_finish_time;

- public string refund_out_id;

- public int refund_status;


## WechatMpNonTaxQueryFeeJsonResult (class)

- public string user_name;

- public long fee;

- public List<WechatMpNonTaxFeeItem> items;

- public string payment_notice_no;

- public string department_code;

- public string department_name;

- public int payment_notice_type;

- public string region_code;

- public long payment_notice_create_time;

- public string payment_expire_date;


## WechatMpNonTaxRefundJsonResult (class)

- public string refund_order_id;


## WechatMpNonTaxUnifiedOrderJsonResult (class)

- public string user_name;

- public long fee;

- public List<WechatMpNonTaxFeeItem> items;

- public string payment_notice_no;

- public string department_code;

- public string department_name;

- public int payment_notice_type;

- public string region_code;

- public long payment_notice_create_time;

- public string payment_expire_date;


## WechatMpNormal (class)

- public int StartStandards;

- public int StartFees;

- public int AddStandards;

- public int AddFees;


## WechatMpOneCodeRange (class)

- public long code_start;

- public long code_end;


## WechatMpOpenIdResultJson (class)

- public int total;

- public int count;

- public WechatMpOpenIdResultJsonData data;

- public string next_openid;


## WechatMpOpenIdResultJsonData (class)

- public List<string> openid;


## WechatMpOpenPluginTokenData (class)

- public string is_open;

- public string wifi_token;


## WechatMpOrder (class)

- public string order_id;

- public int order_status;

- public int order_total_price;

- public string order_create_time;

- public int order_express_price;

- public string buyer_openid;

- public string buyer_nick;

- public string receiver_name;

- public string receiver_province;

- public string receiver_city;

- public string receiver_address;

- public string receiver_mobile;

- public string receiver_phone;

- public string product_id;

- public string product_name;

- public int product_price;

- public string product_sku;

- public int product_count;

- public string product_img;

- public string delivery_id;

- public string delivery_company;

- public string trans_id;

- public string receiver_zone;


## WechatMpPageListData (class)

- public List<WechatMpPageListPages> pages;

- public long date;

- public int total_count;

- public int page_index;


## WechatMpPageListItem (class)

- public string page_id;

- public string page_control_type;

- public string system_page_control_type;


## WechatMpPageListPages (class)

- public int page_id;

- public int click_pv;

- public int click_uv;

- public int shake_pv;

- public int shake_uv;


## WechatMpPageListResultJson (class)

- public WechatMpPageListData data;


## WechatMpPayActiveResultJson (class)

- public string reward;


## WechatMpPayGetOrderOrderInfo (class)

- public string order_id;

- public string status;

- public string create_time;

- public string pay_finish_time;

- public string desc;

- public string free_coin_count;

- public string pay_coin_count;

- public string refund_free_coin_count;

- public string refund_pay_coin_count;

- public string openid;

- public string order_type;


## WechatMpPayGetOrderResultJson (class)

- public WechatMpPayGetOrderOrderInfo order_info;


## WechatMpPayGiftCardResultJson (class)

- public string url;


## WechatMpPayMchInfoData (class)

- public string mchid;

- public string s_pappid;


## WechatMpPayRechargeResultJson (class)

- public string order_id;

- public string qrcode_url;

- public string qrcode_buffer;


## WechatMpPhotoList (class)

- public string photo_url;


## WechatMpPictureResult (class)

- public string image_url;


## WechatMpPlateNumJsonResult (class)

- public string number;


## WechatMpPoiInMap (class)

- public string branch_name;

- public string address;

- public double longitude;

- public double latitude;

- public string telephone;

- public string category;

- public string sosomap_poi_uid;

- public int data_supply;

- public List<string> pic_urls;

- public List<string> card_id_list;


## WechatMpPoiStoreJsonStoreResultJsonGetStoreListResultJson (class)

- public List<WechatMpGetStoreListBusiness> business_list;

- public string total_count;


## WechatMpPoiUploadImageResultJson (class)

- public string url;


## WechatMpPos (class)

- public WechatMpPosLocation left_top;

- public WechatMpPosLocation right_top;

- public WechatMpPosLocation right_bottom;

- public WechatMpPosLocation left_bottom;


## WechatMpPosLocation (class)

- public int x;

- public int y;


## WechatMpPosition (class)

- public WechatMpPos pos;


## WechatMpProduct (class)

- public string product_id;

- public int mod_action;


## WechatMpProductBase (class)

- public string name;

- public List<string> category_id;

- public List<string> img;

- public List<WechatMpDetail> detail;

- public List<WechatMpProperty> property;

- public List<WechatMpSkuInfo> sku_info;

- public int buy_limit;

- public string main_img;

- public string detail_html;


## WechatMpProductCardInfoJsonResult (class)

- public string product_key;

- public string DOM;


## WechatMpProductCreateResultJson (class)

- public string pid;


## WechatMpProductGetListJsonResult (class)

- public int total;

- public List<WechatMpProductGetListKeyList> key_list;


## WechatMpProductGetListKeyList (class)

- public string keystandard;

- public string keystr;

- public string category_id;

- public string category_name;

- public string update_time;

- public string status;


## WechatMpProductGetQrCodeJsonResult (class)

- public string pic_url;

- public string qrcode_url;


## WechatMpProductInfoData (class)

- public string product_id;

- public WechatMpProductBase product_base;

- public List<WechatMpSkuList> sku_list;

- public WechatMpAttrext attrext;

- public WechatMpDeliveryInfo delivery_info;


## WechatMpProjectInfo (class)

- public string name;

- public int num;

- public string unit;

- public int price;


## WechatMpProperty (class)

- public string id;

- public string vid;


## WechatMpPropertyItem (class)

- public string id;

- public string name;

- public List<WechatMpPropertyValue> property_value;


## WechatMpPropertyValue (class)

- public string id;

- public string name;


## WechatMpQrCodeJsonResult (class)

- public List<WechatMpQrCodeResult> code_results;

- public WechatMpImgSize img_size;


## WechatMpQrCodeResult (class)

- public string type_name;

- public string data;

- public WechatMpPos pos;


## WechatMpQualify (class)

- public List<WechatMpExterList> exter_list;


## WechatMpQualifyExterList (class)

- public List<WechatMpQualifyInnerList> exter_list;


## WechatMpQualifyInnerList (class)

- public List<WechatMpQualifyValue> inner_list;


## WechatMpQualifyValue (class)

- public string name;


## WechatMpQueryBlockTemplateMessageResult (class)

- public WechatMpBlockTemplateMessageInfo msginfo;


## WechatMpQueryInvoiceResultJson (class)

- public WechatMpInvoiceDetail invoicedetail;


## WechatMpQueryLotteryJsonResult (class)

- public WechatMpQueryLotteryResult result;


## WechatMpQueryLotteryResult (class)

- public string lottery_id;

- public string title;

- public string desc;

- public int onoff;

- public long begin_time;

- public long expire_time;

- public string sponsor_appid;

- public string appid;

- public long prize_count_limit;

- public long prize_count;

- public string jump_url;

- public long expired_prizes;

- public long drawed_prizes;

- public long available_prizes;

- public long expired_value;

- public long drawed_value;

- public long available_value;


## WechatMpQueryRecoResultResultJson (class)

- public string result;


## WechatMpRecordJson (class)

- public string worker;

- public string openid;

- public int opercode;

- public long time;

- public string text;


## WechatMpRegisterData (class)

- public string secretkey;


## WechatMpRegisterResultJson (class)

- public JsonValue data;


## WechatMpRelationItem (class)

- public long page_id;

- public long device_id;

- public string uuid;

- public long major;

- public long minor;


## WechatMpRelationSearchDate (class)

- public List<WechatMpRelationItem> relations;

- public int total_count;


## WechatMpRelationSearchResultJson (class)

- public WechatMpRelationSearchDate data;


## WechatMpReplyListInfoItem (class)

- public int type;

- public WechatMpNewsInfo news_info;

- public string content;


## WechatMpResult (class)

- public string id;

- public string name;

- public string fullname;

- public List<string> pinyin;

- public WechatMpCardStoreStoreResultJsonLocation location;

- public List<int> cidx;


## WechatMpRichField (class)

- public int type;

- public string name;

- public List<string> values;


## WechatMpRuleInfo (class)

- public string type;

- public WechatMpCardGiftcardGiftCardDataBaseInfo base_info;

- public WechatMpMemberRule member_rule;


## WechatMpScanTicketCheckJsonResult (class)

- public string keystandard;

- public string keystr;

- public string openid;

- public string scene;

- public string is_check;

- public string is_contact;


## WechatMpScanTitleResultJson (class)

- public int title_type;

- public string title;

- public string phone;

- public string tax_no;

- public string addr;

- public string bank_type;

- public string bank_no;


## WechatMpSearchMapPoiItem (class)

- public string branch_name;

- public string address;

- public double longitude;

- public double latitude;

- public string telephone;

- public string category;

- public string sosomap_poi_uid;

- public int data_supply;

- public JsonValue pic_urls;

- public JsonValue card_id_list;


## WechatMpSearchMapPoiJson (class)

- public WechatMpMapPoiData data;


## WechatMpSearchMapPoiResultJson (class)

- public WechatMpSearchMapPoidata data;


## WechatMpSearchMapPoidata (class)

- public List<WechatMpSearchMapPoiItem> item;


## WechatMpSearchPagesData (class)

- public List<WechatMpSearchPagesDataPage> pages;

- public int total_count;


## WechatMpSearchPagesDataPage (class)

- public string comment;

- public string description;

- public string icon_url;

- public long page_id;

- public string page_url;

- public string title;


## WechatMpSearchPagesResultJson (class)

- public WechatMpSearchPagesData data;


## WechatMpSendChannelMessageRequest (class)

- public JsonValue Value;


## WechatMpSendMenuContent (class)

- public string id;

- public string content;


## WechatMpSendResult (class)

- public int type;

- public string msg_id;

- public string msg_data_id;


## WechatMpSendTemplateMessageResult (class)

- public long msgid;


## WechatMpServiceTypeInfo (class)

- public int id;


## WechatMpSetPDFResultJson (class)

- public string s_media_id;


## WechatMpSetPrizeBucketRepeatTicketList (class)

- public string ticket;


## WechatMpSetPrizeBucketResultJson (class)

- public List<WechatMpSetPrizeBucketRepeatTicketList> repeat_ticket_list;


## WechatMpSetUrlResultJson (class)

- public string invoice_url;


## WechatMpShakeInfoData (class)

- public string page_id;

- public WechatMpShakeInfoDataBeaconInfo beacon_info;

- public string openid;

- public long poi_id;


## WechatMpShakeInfoDataBeaconInfo (class)

- public double distance;

- public long major;

- public long minor;

- public string uuid;


## WechatMpShelfCreateDataCardList (class)

- public string card_id;

- public string thumb_url;


## WechatMpShelfCreateResultJson (class)

- public string url;

- public int page_id;


## WechatMpShelfFilter (class)

- public int count;


## WechatMpShelfGroup (class)

- public int group_id;

- public WechatMpShelfFilter filter;


## WechatMpShelfGroupInfo (class)

- public List<WechatMpShelfGroup> groups;

- public string img_background;


## WechatMpShelfInfo (class)

- public List<WechatMpShelfModuleInfo> module_infos;


## WechatMpShelfItem (class)

- public WechatMpShelfInfo shelf_info;

- public string shelf_banner;

- public string shelf_name;

- public int shelf_id;


## WechatMpShelfModuleInfo (class)

- public WechatMpShelfGroupInfo group_infos;

- public WechatMpShelfGroup group_info;

- public int eid;


## WechatMpShopGetData (class)

- public string shop_name;

- public string ssid;

- public List<string> ssid_list;

- public List<WechatMpShopGetSsidPasswordList> ssid_password_list;

- public string password;

- public int protocol_type;

- public int ap_count;

- public int template_id;

- public string homepage_url;

- public int bar_type;

- public string sid;

- public string poi_id;

- public string homepage_wxa_user_name;

- public string homepage_wxa_path;

- public string finishpage_url;

- public string finishpage_wxa_user_name;

- public string finishpage_wxa_path;

- public int finishpage_type;


## WechatMpShopGetSsidPasswordList (class)

- public string ssid;

- public string password;


## WechatMpShopListData (class)

- public int totalcount;

- public int pageindex;

- public int pagecount;

- public List<WechatMpShopListRecords> records;


## WechatMpShopListRecords (class)

- public string shop_id;

- public string shop_name;

- public string ssid;

- public List<string> ssid_list;

- public int protocol_type;

- public string sid;


## WechatMpShortUrlResult (class)

- public string short_url;


## WechatMpSingleSessionList (class)

- public string openid;

- public string createtime;


## WechatMpSingleWaitCase (class)

- public string openid;

- public string kf_account;

- public string createtime;


## WechatMpSku (class)

- public string id;

- public string name;

- public List<WechatMpValue> value_list;


## WechatMpSkuInfo (class)

- public string id;

- public List<string> vid;


## WechatMpSkuList (class)

- public string sku_id;

- public int price;

- public string icon_url;

- public string product_code;

- public int ori_price;

- public int quantity;


## WechatMpStatisticsDataItem (class)

- public int click_pv;

- public int click_uv;

- public long ftime;

- public int shake_pv;

- public int shake_uv;


## WechatMpStatisticsResultJson (class)

- public List<WechatMpStatisticsDataItem> data;


## WechatMpStoreBaseData (class)

- public int status;

- public string sid;

- public string business_name;

- public string branch_name;

- public string province;

- public string city;

- public string district;

- public string address;

- public List<string> categories;

- public int offset_type;

- public string longitude;

- public string latitude;

- public string telephone;

- public List<WechatMpStorePhoto> photo_list;

- public string recommend;

- public string special;

- public string introduction;

- public string open_time;

- public int avg_price;


## WechatMpStoreBaseInfo (class)

- public string sid;

- public string business_name;

- public string branch_name;

- public string province;

- public string city;

- public string district;

- public string address;

- public List<string> categories;

- public int offset_type;

- public string longitude;

- public string latitude;

- public string telephone;

- public List<WechatMpStorePhoto> photo_list;

- public string recommend;

- public string special;

- public string introduction;

- public string open_time;

- public int avg_price;


## WechatMpStoreJsonResult (class)

- public WechatMpBusinessData business;


## WechatMpStorePhoto (class)

- public string photo_url;


## WechatMpSubmerChantBatchGetInfoList (class)

- public List<WechatMpSubmerChantSubmitInfo> info;

- public int next_begin_id;


## WechatMpSubmerChantBatchGetJsonResult (class)

- public List<WechatMpSubmerChantBatchGetInfoList> info_list;

- public WechatMpSubmerChantSubmitInfo info;


## WechatMpSubmerChantSubmitInfo (class)

- public string merchant_id;

- public int create_time;

- public int update_time;

- public string brand_name;

- public string logo_url;

- public string status;

- public int begin_time;

- public int end_time;

- public int primary_category_id;

- public int secondary_category_id;


## WechatMpSubmerChantSubmitJsonResult (class)

- public WechatMpSubmerChantSubmitInfo info;


## WechatMpSubmitFreePublishResultJson (class)

- public string publish_id;


## WechatMpTagJson (class)

- public List<WechatMpTagJsonTag> tags;


## WechatMpTagJsonTag (class)

- public int id;

- public string name;

- public int count;


## WechatMpTemplateInfo (class)

- public int Id;

- public string Name;

- public int Assumer;

- public int Valuation;

- public List<WechatMpTopFeeItem> TopFee;


## WechatMpThemeCategoryItem (class)

- public string title;


## WechatMpTicketToCodeJsonResult (class)

- public string code;

- public long application_id;

- public string isv_application_id;

- public string activity_name;

- public string product_brand;

- public string product_title;

- public string product_code;

- public string wxa_appid;

- public string wxa_path;

- public int wxa_type;

- public long code_start;

- public long code_end;


## WechatMpTopFeeItem (class)

- public long Type;

- public WechatMpNormal Normal;

- public List<WechatMpCustomItem> Custom;


## WechatMpTranslateContentResultJson (class)

- public string from_content;

- public string to_content;


## WechatMpUpdateBrandResultJson (class)

- public string pid;


## WechatMpUpdatePageResultJson (class)

- public WechatMpBasePageData data;


## WechatMpUpdateStoreBaseInfo (class)

- public string poi_id;

- public string telephone;

- public List<WechatMpStorePhoto> photo_list;

- public string recommend;

- public string special;

- public string introduction;

- public string open_time;

- public int avg_price;


## WechatMpUpdateStoreBusiness (class)

- public WechatMpUpdateStoreBaseInfo base_info;


## WechatMpUpdateStoreJsonResult (class)

- public WechatMpAuditResultId data;


## WechatMpUpdateStoreResultData (class)

- public int has_audit_id;

- public string audit_id;


## WechatMpUpdateStoreResultJson (class)

- public WechatMpUpdateStoreResultData data;


## WechatMpUpdateUserGiftCardResultJson (class)

- public int result_bonus;

- public int result_balance;

- public string openid;


## WechatMpUpdateUserResultJson (class)

- public int result_bonus;

- public int result_balance;

- public string openid;


## WechatMpUploadForeverMediaResult (class)

- public string media_id;

- public string url;


## WechatMpUploadImageData (class)

- public string pic_url;


## WechatMpUploadImageResultJson (class)

- public WechatMpUploadImageData data;


## WechatMpUploadImgResult (class)

- public string url;


## WechatMpUploadTemporaryMediaResult (class)

- public int type;

- public string media_id;

- public string thumb_media_id;

- public long created_at;


## WechatMpUrlCell (class)

- public long end_time;

- public string text;

- public string url;


## WechatMpUserAuthInfo (class)

- public WechatMpUserFiledInfo user_field;

- public WechatMpBizFieldInfo biz_field;


## WechatMpUserCard (class)

- public WechatMpInvoicePlatformUserData invoice_user_data;


## WechatMpUserFiledData (class)

- public int show_title;

- public int show_phone;

- public int show_email;

- public List<WechatMpCustomFieldItem> custom_field;


## WechatMpUserFiledInfo (class)

- public string title;

- public string phone;

- public string email;

- public List<WechatMpCustomFieldItem> custom_field;


## WechatMpUserInfo (class)

- public int fee;

- public string title;

- public int billing_time;

- public string billing_no;

- public string billing_code;

- public List<WechatMpInvoiceInvoiceJsonInvoicePlatformResultJsonInfo> info;

- public bool accept;

- public int fee_without_tax;

- public int tax;

- public string pdf_url;

- public string trip_pdf_url;

- public int reimburse_status;

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


## WechatMpUserInfoJson (class)

- public int subscribe;

- public string openid;

- public string nickname;

- public int sex;

- public string language;

- public string city;

- public string province;

- public string country;

- public string headimgurl;

- public long subscribe_time;

- public string unionid;

- public string remark;

- public int groupid;

- public List<int> tagid_list;

- public string subscribe_scene;

- public long qr_scene;

- public string qr_scene_str;


## WechatMpUserTagListResult (class)

- public List<int> tagid_list;


## WechatMpUserinfoGetResult (class)

- public string openid;

- public string nickname;

- public string membership_number;

- public int bonus;

- public string sex;

- public WechatMpUserinfoGetResultUserInfo user_info;

- public int user_card_status;

- public bool has_active;


## WechatMpUserinfoGetResultUserInfo (class)

- public List<WechatMpUserinfoGetResultUserInfoItem> common_field_list;

- public List<WechatMpUserinfoGetResultUserInfoItem> custom_field_list;


## WechatMpUserinfoGetResultUserInfoItem (class)

- public string name;

- public string value_;

- public List<string> value_list;


## WechatMpValue (class)

- public string id;

- public string name;


## WechatMpVerifyTypeInfo (class)

- public int id;


## WechatMpVideoMediaIdResult (class)

- public string media_id;

- public string type;

- public long created_at;


## WechatMpWiFiConnectUrlData (class)

- public string connect_url;


## WechatMpWiFiConnectUrlResultJson (class)

- public WechatMpWiFiConnectUrlData data;


## WechatMpWiFiGetCouponPutJsonResult (class)

- public WechatMpGetCouponPutData data;


## WechatMpWiFiOpenPluginTokenJsonResult (class)

- public WechatMpOpenPluginTokenData data;


## WechatMpWiFiRegisterJsonResult (class)

- public WechatMpRegisterData data;


## WechatMpWiFiShopGetJsonResult (class)

- public WechatMpShopGetData data;


## WechatMpWiFiShopListJsonResult (class)

- public WechatMpShopListData data;


## WechatMpWxaMerchantJsonDistrictResultJsonLocation (class)

- public double lat;

- public double lng;


## WechatMpWxaMerchantJsonMerchatResultJsonAllCategoryInfo (class)

- public WechatMpCateGoryCollection all_category_info;
