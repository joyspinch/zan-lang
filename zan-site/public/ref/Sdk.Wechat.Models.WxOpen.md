# Sdk.Wechat.Models.WxOpen

> 源码: `stdlib/Sdk/Wechat/Models/WxOpen/WechatWxOpenModels.zan`


## WechatWxOpenAddJsonResult (class)

从随附的 C# SDK 生成的共享 DTO 实体。
一个 C# 命名空间/类型标识精确对应一个 Zan 模型。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- public string template_id;


## WechatWxOpenAddNearbyPoiJsonResult (class)

- public string audit_id;

- public string poi_id;

- public string related_credential;


## WechatWxOpenApplyItem (class)

- public string appid;

- public int status;

- public string nickname;

- public string headimgurl;


## WechatWxOpenB2BBankTransferFeeJsonResult (class)

- public string merchant_name;

- public int certified_charge_fee_numerator;

- public int uncertified_charge_fee_numerator;

- public string effect_time;

- public string expire_time;


## WechatWxOpenB2BBankTransferRegistrationStatus (class)

- public int wqf_register_state;

- public string wqf_register_state_desc;

- public string request_no;


## WechatWxOpenB2BBatchCreateRetailJsonResult (class)

- public int num_success;

- public int num_failure;

- public List<WechatWxOpenB2BRetailFailureRecord> failure_record_list;


## WechatWxOpenB2BBusinessLicense (class)

- public string business_license_copy;

- public string business_license_number;

- public string merchant_name;

- public string legal_person;

- public string company_address;

- public string business_time;

- public string cert_type;


## WechatWxOpenB2BCreateBankTransferLinkJsonResult (class)

- public string url;

- public string expire_time;


## WechatWxOpenB2BDownloadBillJsonResult (class)

- public string success_bill_url;

- public string refund_bill_url;

- public string all_bill_url;

- public string fund_bill_url;

- public double ended_day_avail_amt;

- public double ended_day_frozen_amt;

- public double ended_day_total_amt;

- public string profit_sharing_bill_url;

- public string profit_refund_bill_url;

- public string bankpay_fund_bill_url;


## WechatWxOpenB2BGetAppKeyJsonResult (class)

- public string appkey;

- public string sandbox_appkey;


## WechatWxOpenB2BGetMerchantApplicationJsonResult (class)

- public List<WechatWxOpenB2BMerchantApplication> list;

- public int total;


## WechatWxOpenB2BGetMerchantBalanceJsonResult (class)

- public List<WechatWxOpenB2BMerchantBalance> balance_list;


## WechatWxOpenB2BGetMerchantInfoJsonResult (class)

- public List<WechatWxOpenB2BMerchantInfo> mch_list;

- public int total;


## WechatWxOpenB2BGetOrderJsonResult (class)

- public string appid;

- public string mchid;

- public string out_trade_no;

- public string order_id;

- public string pay_status;

- public string pay_time;

- public string attach;

- public string payer_openid;

- public WechatWxOpenB2BOrderAmount amount;

- public string wxpay_transaction_id;

- public int env;

- public int settle_status;

- public string settle_finish_time;

- public int platform_profit_percent;

- public long platform_profit_fee;

- public string bank_type;


## WechatWxOpenB2BGetRefundJsonResult (class)

- public string refund_id;

- public string out_refund_no;

- public string order_id;

- public string out_trade_no;

- public string create_time;

- public string refund_time;

- public string refund_status;

- public string refund_desc;

- public WechatWxOpenB2BRefundAmount amount;

- public string wxpay_refund_id;

- public int reverse_sett_state;

- public string reverse_sett_finish_time;

- public int platform_profit_percent;

- public long reverse_sett_amt;

- public WechatWxOpenB2BRefundChannelInfo refund_channel_info;

- public string description;


## WechatWxOpenB2BGetRetailInfoJsonResult (class)

- public List<WechatWxOpenB2BRetailInfo> info;


## WechatWxOpenB2BGetRetailMessageListJsonResult (class)

- public int total_num;

- public List<WechatWxOpenB2BRetailMessageData> data_line;


## WechatWxOpenB2BGetRetailOpenIdListJsonResult (class)

- public List<string> openid_list;

- public string page_context;


## WechatWxOpenB2BIdCardInfo (class)

- public string id_card_copy;

- public string id_card_national;

- public string id_card_name;

- public string id_card_number;

- public string id_card_valid_time;

- public string id_card_address;

- public string id_card_valid_time_begin;


## WechatWxOpenB2BIdDocumentInfo (class)

- public string id_doc_name;

- public string id_doc_number;

- public string id_doc_copy;

- public string doc_period_end;

- public string doc_period_begin;

- public string id_doc_address;

- public string id_doc_copy_back;


## WechatWxOpenB2BMerchantAccountInfo (class)

- public string bank_account_type;

- public string account_bank;

- public string account_name;

- public string bank_address_code;

- public string bank_branch_id;

- public string bank_name;

- public string account_number;


## WechatWxOpenB2BMerchantAccountValidation (class)

- public string account_name;

- public string account_no;

- public double pay_amount;

- public string destination_account_number;

- public string destination_account_name;

- public string destination_account_bank;

- public string city;

- public string remark;

- public string deadline;


## WechatWxOpenB2BMerchantApplication (class)

- public int status;

- public WechatWxOpenB2BMerchantApplicationInnerResponse inner_resp;

- public WechatWxOpenB2BBankTransferRegistrationStatus wqf_register_statement;

- public double wx_pay_rate;

- public double wqf_certified_rate;

- public int bind_scene_status;


## WechatWxOpenB2BMerchantApplicationInnerResponse (class)

- public WechatWxOpenB2BSubMerchantRegistrationStatus sub_merchant_registration_status;


## WechatWxOpenB2BMerchantAuditDetail (class)

- public string param_name;

- public string reject_reason;


## WechatWxOpenB2BMerchantBalance (class)

- public string balance_type;

- public string amount;

- public string currency;


## WechatWxOpenB2BMerchantContactInfo (class)

- public string contact_type;

- public string contact_name;

- public string contact_id_doc_type;

- public string contact_id_card_number;

- public string contact_id_doc_copy;

- public string contact_id_doc_copy_back;

- public string contact_id_doc_period_begin;

- public string contact_id_doc_period_end;

- public string business_authorization_letter;

- public string mobile_phone;

- public string contact_email;


## WechatWxOpenB2BMerchantExtendedRegisterInfo (class)

- public string door_head_file_id;

- public string store_file_id;

- public string online_pay_file_id;

- public string merchant_scale;

- public string authorization_letter_file_id;


## WechatWxOpenB2BMerchantInfo (class)

- public string sub_mchid;

- public string company_name;

- public string bank_name;

- public string bank_account;

- public string wxpay_status;

- public string bank_transfer_status;


## WechatWxOpenB2BMerchantQualification (class)

- public string qualification_type;

- public string qualifications;


## WechatWxOpenB2BOrderAmount (class)

- public long order_amount;

- public long payer_amount;

- public string currency;


## WechatWxOpenB2BProfitSharingAccount (class)

- public string sharing_account_type;

- public string sharing_account;

- public long add_time;

- public long update_time;

- public string name;


## WechatWxOpenB2BQueryProfitSharingAccountJsonResult (class)

- public List<WechatWxOpenB2BProfitSharingAccount> account_list;


## WechatWxOpenB2BQueryProfitSharingOrderJsonResult (class)

- public int order_status;


## WechatWxOpenB2BQueryProfitSharingRemainingAmountJsonResult (class)

- public long remain_amt;


## WechatWxOpenB2BQueryRefundProfitSharingJsonResult (class)

- public int order_status;


## WechatWxOpenB2BQueryWithdrawJsonResult (class)

- public string out_withdraw_no;

- public long withdraw_amount;

- public string status;

- public string fail_reason;


## WechatWxOpenB2BRefundAmount (class)

- public long order_amount;

- public long refund_amount;

- public string currency;


## WechatWxOpenB2BRefundChannelInfo (class)

- public string channel;

- public string user_received_account;

- public string funds_account;


## WechatWxOpenB2BRefundJsonResult (class)

- public string refund_id;

- public string out_refund_no;

- public string order_id;

- public string out_trade_no;


## WechatWxOpenB2BRegisterMerchantJsonResult (class)

- public string order_no;


## WechatWxOpenB2BRetailFailureRecord (class)

- public string mobile_phone;

- public string registration_number;

- public int failure_code;


## WechatWxOpenB2BRetailInfo (class)

- public string mobile_phone;

- public string retail_type;

- public string sub_retail_type;

- public string retail_address;

- public string retail_name;

- public string identification;

- public string principal;

- public string legal_person_name;

- public string openid;

- public int role;

- public int status;

- public long auth_time;

- public long grant_time;

- public double longitude;

- public double latitude;

- public List<string> business_type;

- public string other_business_type;

- public List<WechatWxOpenB2BRetailStaff> staff_list;


## WechatWxOpenB2BRetailMessageData (class)

- public long msg_id;

- public int msg_type;

- public string date;

- public string msg_time;

- public int send_uv;

- public int entry_uv;

- public string business_msg_id;


## WechatWxOpenB2BRetailPreEntry (class)

- public string mobile_phone;

- public string retail_name;

- public string retail_type;

- public string sub_retail_type;

- public string address_province;

- public string address_city;

- public string address_region;

- public string address_street;

- public string registration_number;

- public string biz_name;

- public string corporation_name;

- public double latitude;

- public double longitude;

- public List<string> business_type;

- public string other_business_type;


## WechatWxOpenB2BRetailStaff (class)

- public string openid;

- public int role;

- public long create_time;


## WechatWxOpenB2BSubMerchantRegistrationStatus (class)

- public string applyment_state;

- public string applyment_state_desc;

- public string sign_state;

- public string sign_url;

- public string sub_mchid;

- public WechatWxOpenB2BMerchantAccountValidation account_validation;

- public List<WechatWxOpenB2BMerchantAuditDetail> audit_detail;

- public string legal_validation_url;


## WechatWxOpenB2BUploadMerchantFileJsonResult (class)

- public string file_id;


## WechatWxOpenBatchGetOrderJsonResult (class)

- public List<WechatWxOpenDeliveryDeliveryJsonJsonResultGetOrderJsonResult> order_list;


## WechatWxOpenBindAccountJsonResult (class)

- public JsonValue Value;


## WechatWxOpenCancelCurrencyPayJsonResult (class)

- public string order_id;


## WechatWxOpenCancelSubscribeContractJsonResult (class)

- public JsonValue Value;


## WechatWxOpenCargoModel (class)

- public int count;

- public double weight;

- public double space_x;

- public double space_y;

- public double space_z;

- public List<WechatWxOpenDetailListModel> detail_list;


## WechatWxOpenCategories (class)

- public int first;

- public int second;

- public int audit_status;

- public long audit_id;


## WechatWxOpenCategoriesList (class)

- public List<WechatWxOpenCategory> categories;


## WechatWxOpenCategory (class)

- public int id;

- public string name;

- public int level;

- public List<int> children;

- public int father;

- public WechatWxOpenQualify qualify;

- public int scene;

- public int sensitive_type;


## WechatWxOpenChatToolParticipatorInfo (class)

- public string group_openid;

- public int state;


## WechatWxOpenCheckEncryptedDataJsonResult (class)

- public bool vaild;

- public long create_time;


## WechatWxOpenCityServiceBusinessViewJsonResult (class)

- public string business_type;

- public string query_string;

- public long expire_at;


## WechatWxOpenCityServiceCheckRealNameJsonResult (class)

- public string verify_openid;

- public string verify_real_name;


## WechatWxOpenCityServiceGetHospitalNoticeListJsonResult (class)

- public List<WechatWxOpenCityServiceHospitalNotice> notice_list;

- public List<string> preview_openid;


## WechatWxOpenCityServiceGetMedicalRealNameJsonResult (class)

- public string cipher_real_name;

- public string cipher_algorithm;

- public int key_version;

- public string app_id;

- public string open_id;

- public string openid_id;


## WechatWxOpenCityServiceGetMessageRelationJsonResult (class)

- public int err_code;

- public string err_msg;

- public bool is_subscribed;


## WechatWxOpenCityServiceGetServicePathJsonResult (class)

- public string path;

- public string business_type;

- public string bussiness_type;

- public string app_id;

- public string username;

- public string query_string;


## WechatWxOpenCityServiceHospitalNotice (class)

- public long notice_id;

- public string content;

- public string status;

- public List<string> preview_openid;


## WechatWxOpenCityServiceNoticeIdJsonResult (class)

- public long notice_id;


## WechatWxOpenCityServicePathExtraParameter (class)

- public string key;

- public string value_;


## WechatWxOpenCityServiceSendMessageDataJsonResult (class)

- public string result_page_url;


## WechatWxOpenClientVersionGroup (class)

- public int type;

- public List<string> client_version_list;


## WechatWxOpenCollection (class)

- public string name;

- public int count;

- public int size;

- public int index_count;

- public int index_size;


## WechatWxOpenCommonGetWeAnalysisAppidRetainInfoResultJson (class)

- public string ref_date;

- public List<WechatWxOpenCommonGetWeAnalysisAppidRetainInfoResultJsonVisit> visit_uv_new;

- public List<WechatWxOpenCommonGetWeAnalysisAppidRetainInfoResultJsonVisit> visit_uv;


## WechatWxOpenCommonGetWeAnalysisAppidRetainInfoResultJsonVisit (class)

- public int key;

- public int value_;


## WechatWxOpenComplaintItem (class)

- public string complaint_id;

- public string complaint_time;

- public string complaint_detail;

- public string complaint_state;

- public string payer_phone;

- public string payer_openid;

- public List<WechatWxOpenComplaintOrderInfoItem> complaint_order_info;

- public bool complaint_full_refunded;

- public bool incoming_user_response;

- public int user_complaint_times;

- public List<WechatWxOpenComplaintMediaItem> complaint_media_list;

- public string problem_description;

- public string problem_type;

- public int apply_refund_amount;

- public List<string> user_tag_list;

- public List<WechatWxOpenComplaintServiceOrderInfoItem> service_order_info;


## WechatWxOpenComplaintMediaItem (class)

- public string media_type;

- public List<string> media_url;


## WechatWxOpenComplaintOrderInfoItem (class)

- public string transaction_id;

- public string out_trade_no;

- public int amount;

- public string wxa_out_trade_no;

- public string wx_order_id;


## WechatWxOpenComplaintServiceOrderInfoItem (class)

- public string order_id;

- public string out_order_no;

- public string state;


## WechatWxOpenContactModel (class)

- public string consignor_contact;

- public string receiver_contact;


## WechatWxOpenCreateActivityIdJsonResult (class)

- public string activity_id;

- public long expiration_time;


## WechatWxOpenCreateFundsBillJsonResult (class)

- public string bill_id;


## WechatWxOpenCreateIndex (class)

- public string name;

- public bool unique;

- public List<WechatWxOpenKey> keys;


## WechatWxOpenCreateMapPoiJsonResult (class)

- public string error;

- public WechatWxOpenResultData data;


## WechatWxOpenCreateWithdrawOrderJsonResult (class)

- public string withdraw_no;

- public string wx_withdraw_no;


## WechatWxOpenCurrencyPayJsonResult (class)

- public string order_id;

- public int balance;

- public int used_present_amount;


## WechatWxOpenCustomerServiceBusinessInfo (class)

- public JsonValue business_id;

- public string account_name;

- public string nickname;

- public string icon_media_id;

- public string icon_url;


## WechatWxOpenData (class)

- public int category_status;

- public bool show_wxopen_shelf_state;

- public int upgrade_status;

- public int apply_status;

- public int left_apply_num;

- public int max_apply_num;

- public string data;


## WechatWxOpenDeliveryDeliveryJsonAddOrderModel (class)

- public string order_id;

- public string openid;

- public string delivery_id;

- public string biz_id;

- public string custom_remark;

- public int tagid;

- public int add_source;

- public string wx_appid;

- public WechatWxOpenSenderOrReceiverModel sender;

- public WechatWxOpenSenderOrReceiverModel receiver;

- public WechatWxOpenCargoModel cargo;

- public WechatWxOpenShopModel shop;

- public WechatWxOpenInsuredModel insured;

- public WechatWxOpenServiceType service;

- public long expect_time;

- public int take_mode;


## WechatWxOpenDeliveryDeliveryJsonGetOrderModel (class)

- public string order_id;

- public string openid;

- public string delivery_id;

- public string waybill_id;

- public int print_type;

- public string custom_remark;


## WechatWxOpenDeliveryDeliveryJsonJsonResultAddOrderJsonResult (class)

- public string order_id;

- public string waybill_id;

- public int delivery_sesultcode;

- public string delivery_resultmsg;

- public List<WechatWxOpenWayBillDataModel> waybill_data;


## WechatWxOpenDeliveryDeliveryJsonJsonResultCancelOrderJsonResult (class)

- public int delivery_resultcode;

- public string delivery_resultmsg;


## WechatWxOpenDeliveryDeliveryJsonJsonResultGetOrderJsonResult (class)

- public string print_html;

- public List<WechatWxOpenWayBillDataModel> waybill_data;

- public string order_id;

- public string delivery_id;

- public string waybill_id;

- public int order_status;


## WechatWxOpenDeliveryProviderContact (class)

- public string name;

- public string tel;

- public string mobile;

- public string address;


## WechatWxOpenDeliveryProviderGetContactJsonResult (class)

- public string waybill_id;

- public WechatWxOpenDeliveryProviderContact sender;

- public WechatWxOpenDeliveryProviderContact receiver;


## WechatWxOpenDeliveryProviderPreviewTemplateJsonResult (class)

- public string waybill_id;

- public string rendered_waybill_template;


## WechatWxOpenDetailListModel (class)

- public string name;

- public int count;


## WechatWxOpenDevPluginResultJson (class)

- public List<WechatWxOpenApplyItem> apply_list;


## WechatWxOpenDistrictItem (class)

- public string id;

- public string name;

- public string fullname;

- public List<string> pinyin;

- public WechatWxOpenLoction location;

- public List<int> cidx;


## WechatWxOpenDownloadAdverfundsOrderJsonResult (class)

- public string url;


## WechatWxOpenDownloadBillJsonResult (class)

- public string url;


## WechatWxOpenDownloadIosSettlementBillJsonResult (class)

- public List<WechatWxOpenIosSettlementBillItem> bill_list;


## WechatWxOpenDownloadJsonResult (class)

- public string file_name;

- public string file_content;


## WechatWxOpenDropIndex (class)

- public string name;


## WechatWxOpenExpressExpressJsonJsonResultAddOrderJsonResult (class)

- public double fee;

- public double deliverfee;

- public double couponfee;

- public double tips;

- public double insurancefee;

- public double distance;

- public string waybill_id;

- public int order_status;

- public int finish_code;

- public int pickup_code;

- public int dispatch_duration;

- public int resultcode;

- public string resultmsg;


## WechatWxOpenExpressExpressJsonJsonResultCancelOrderJsonResult (class)

- public double deduct_fee;

- public string desc;

- public int resultcode;

- public string resultmsg;


## WechatWxOpenExpressExpressJsonJsonResultGetOrderJsonResult (class)

- public int order_status;

- public string waybill_id;

- public string rider_name;

- public string rider_phone;

- public double rider_lng;

- public double rider_lat;

- public int reach_time;

- public int resultcode;

- public string resultmsg;


## WechatWxOpenExpressJsonResult (class)

- public int resultcode;

- public string resultmsg;


## WechatWxOpenExter (class)

- public List<WechatWxOpenInner> inner_list;


## WechatWxOpenFaceCertificateInfo (class)

- public string cert_type;

- public string cert_name;

- public string cert_no;


## WechatWxOpenFaceGetVerifyIdJsonResult (class)

- public string verify_id;

- public int expires_in;


## WechatWxOpenFaceQueryVerifyInfoJsonResult (class)

- public int verify_ret;


## WechatWxOpenFamousBrandApplication (class)

- public int apply_for;

- public WechatWxOpenFamousBrandAuditInfo audit_info;


## WechatWxOpenFamousBrandAuditInfo (class)

- public string brand_name;

- public int brand_type;

- public string flagship_in_which_ec_platform;

- public List<string> ec_platform_proof_list;

- public List<string> other_material_list;

- public List<string> authority_certified_proof_list;


## WechatWxOpenFamousBrandProgress (class)

- public int status;


## WechatWxOpenFamousBrandStatusApplication (class)

- public int apply_for;

- public WechatWxOpenFamousBrandStatusAuditInfo audit_info;

- public int status;


## WechatWxOpenFamousBrandStatusAuditInfo (class)

- public string audit_reason;


## WechatWxOpenFamousBrandStatusJsonResult (class)

- public WechatWxOpenFamousBrandProgress progress;

- public WechatWxOpenFamousBrandStatusApplication application;


## WechatWxOpenFeedbackItem (class)

- public long record_id;

- public long create_time;

- public string content;

- public JsonValue phone;

- public string openid;

- public string nickname;

- public string head_url;

- public int type;

- public List<string> mediaIds;

- public string systemInfo;


## WechatWxOpenFileItem (class)

- public string fileid;

- public int max_age;


## WechatWxOpenGenerateNFCSchemeJsonResult (class)

- public string openlink;


## WechatWxOpenGenerateResultJson (class)

- public string url_link;

- public string warn_icon;

- public string entrance_url;

- public string entrance_icon;


## WechatWxOpenGenerateSchemeJsonResult (class)

- public string openlink;


## WechatWxOpenGetAllAccountJsonResult (class)

- public int count;

- public List<WechatWxOpenGetAllAccountList> list;


## WechatWxOpenGetAllAccountList (class)

- public string biz_id;

- public string delivery_id;

- public long create_time;

- public long update_time;

- public int status_code;

- public string alias;

- public string remark_wrong_msg;

- public string remark_content;

- public int quota_num;

- public long quota_update_time;

- public List<WechatWxOpenServiceType> service_type;


## WechatWxOpenGetAllDeliveryData (class)

- public string delivery_id;

- public string delivery_name;

- public int can_use_cash;

- public int can_get_quota;

- public List<WechatWxOpenServiceType> service_type;

- public string cash_biz_id;


## WechatWxOpenGetAllDeliveryJsonResult (class)

- public int count;

- public List<WechatWxOpenGetAllDeliveryData> data;


## WechatWxOpenGetAllImmeDeliveryJsonResult (class)

- public List<WechatWxOpenGetAllImmeDeliveryJsonResultList> list;

- public int resultcode;

- public string resultmsg;


## WechatWxOpenGetAllImmeDeliveryJsonResultList (class)

- public string delivery_id;

- public string delivery_name;


## WechatWxOpenGetBindAccountJsonResult (class)

- public List<WechatWxOpenGetBindAccountJsonResultShopList> shop_list;

- public int resultcode;

- public string resultmsg;


## WechatWxOpenGetBindAccountJsonResultShopList (class)

- public string delivery_id;

- public string shopid;

- public string audit_result;


## WechatWxOpenGetBusinessJsonResult (class)

- public WechatWxOpenCustomerServiceBusinessInfo business_info;


## WechatWxOpenGetComplaintDetailJsonResult (class)

- public WechatWxOpenComplaintItem complaint;


## WechatWxOpenGetComplaintListJsonResult (class)

- public List<WechatWxOpenComplaintItem> complaints;

- public int total;


## WechatWxOpenGetDistrictJsonResult (class)

- public string data_version;

- public List <List<WechatWxOpenDistrictItem>> result;


## WechatWxOpenGetDomainInfoJsonResult (class)

- public List<string> requestdomain;

- public List<string> wsrequestdomain;

- public List<string> uploaddomain;

- public List<string> downloaddomain;

- public List<string> udpdomain;

- public List<string> bizdomain;


## WechatWxOpenGetFeedbackJsonResult (class)

- public List<WechatWxOpenFeedbackItem> list;

- public long total_num;


## WechatWxOpenGetJsErrDetailJsonResult (class)

- public bool success;

- public string openid;

- public List<WechatWxOpenJsErrDetailItem> data;

- public long totalCount;


## WechatWxOpenGetJsErrListJsonResult (class)

- public bool success;

- public string openid;

- public List<WechatWxOpenJsErrListItem> data;

- public long totalCount;


## WechatWxOpenGetJsonResult (class)

- public long qrcodejump_open;

- public long qrcodejump_pub_quota;

- public long list_size;

- public List<WechatWxOpenRule> rule_list;


## WechatWxOpenGetKfListResultJson (class)

- public List<WechatWxOpenKfInfo> kf_list;


## WechatWxOpenGetKfWorkBoundJsonResult (class)

- public string entityName;

- public string corpid;

- public long bindTime;


## WechatWxOpenGetMerchantCategoryJsonResult (class)

- public WechatWxOpenCategoriesList all_category_info;


## WechatWxOpenGetNearbyDetailPageJsonResult (class)

- public JsonValue Value;


## WechatWxOpenGetNearbyOfficialServiceInfoJsonResult (class)

- public WechatWxOpenServiceInfos data;


## WechatWxOpenGetNearbyPoiListJsonResult (class)

- public WechatWxOpenData data;


## WechatWxOpenGetNegotiationHistoryJsonResult (class)

- public List<WechatWxOpenHistoryItem> history;

- public int total;


## WechatWxOpenGetOnlineKfListResultJson (class)

- public List<WechatWxOpenOnlineKfInfo> kf_online_list;


## WechatWxOpenGetOpenDataJsonResult (class)

- public List<WechatWxOpenOpenDataItem> data_list;


## WechatWxOpenGetOperationPerformanceJsonResult (class)

- public string default_time_data;

- public string compare_time_data;


## WechatWxOpenGetOrderListJsonResult (class)

- public string last_index;

- public bool has_more;

- public List<WechatWxOpenOrderModel> order_list;


## WechatWxOpenGetPathJsonResult (class)

- public string openid;

- public string delivery_id;

- public string waybill_id;

- public int path_item_num;

- public List<WechatWxOpenPathItemListModel> path_item_list;


## WechatWxOpenGetPerformanceDataJsonResult (class)

- public WechatWxOpenPerformanceData data;


## WechatWxOpenGetPluginListResultJson (class)

- public List<WechatWxOpenPluginList> plugin_list;


## WechatWxOpenGetPluginOpenPidJsonResult (class)

- public string openpid;


## WechatWxOpenGetPrinterJsonResult (class)

- public int count;

- public List<string> openid;

- public List<string> tagid_list;


## WechatWxOpenGetQuotaJsonResult (class)

- public int quota_num;


## WechatWxOpenGetRecentAverageResultJson (class)

- public int averageData;


## WechatWxOpenGetSceneListJsonResult (class)

- public List<WechatWxOpenOperationScene> scene;


## WechatWxOpenGetStoreWxaAttrJsonResult (class)

- public bool is_exist;

- public WechatWxOpenStoreWxaAttr store_wxa_attr;

- public WechatWxOpenWeappCategory weapp_category;


## WechatWxOpenGetUploadFileSignJsonResult (class)

- public string sign;

- public string cos_url;


## WechatWxOpenGetUserEncryptKeyJsonResult (class)

- public List<WechatWxOpenUserEncryptKeyInfo> key_info_list;


## WechatWxOpenGetUserNotifyJsonResult (class)

- public WechatWxOpenUserNotifyInfo notify_info;


## WechatWxOpenGetUserPhoneNumberJsonResult (class)

- public WechatWxOpenPhoneInfo phone_info;


## WechatWxOpenGetUserRiskRankResult (class)

- public int risk_rank;


## WechatWxOpenGetVersionListJsonResult (class)

- public List<WechatWxOpenClientVersionGroup> cvlist;


## WechatWxOpenGetWeAnalysisAppidDailySummaryTrendResultJson (class)

- public List<WechatWxOpenGetWeAnalysisAppidDailySummaryTrendResultJsonList> list;


## WechatWxOpenGetWeAnalysisAppidDailySummaryTrendResultJsonList (class)

- public string ref_date;

- public int visit_total;

- public int share_pv;

- public int share_uv;


## WechatWxOpenGetWeAnalysisAppidDailyVisitTrendResultJson (class)

- public List<WechatWxOpenGetWeAnalysisAppidDailyVisitTrendResultJsonList> list;


## WechatWxOpenGetWeAnalysisAppidDailyVisitTrendResultJsonList (class)

- public string ref_date;

- public int session_cnt;

- public int visit_pv;

- public int visit_uv;

- public int visit_uv_new;

- public double stay_time_uv;

- public double stay_time_session;

- public double visit_depth;


## WechatWxOpenGetWeAnalysisAppidMonthlyVisitTrendResultJson (class)

- public List<WechatWxOpenGetWeAnalysisAppidMonthlyVisitTrendResultJsonList> list;


## WechatWxOpenGetWeAnalysisAppidMonthlyVisitTrendResultJsonList (class)

- public string ref_date;

- public int session_cnt;

- public int visit_pv;

- public int visit_uv;

- public int visit_uv_new;

- public double stay_time_uv;

- public double stay_time_session;

- public double visit_depth;


## WechatWxOpenGetWeAnalysisAppidUserPortraitResultJson (class)

- public string ref_date;

- public WechatWxOpenGetWeAnalysisAppidUserPortraitResultJsonVisitUv visit_uv_new;

- public WechatWxOpenGetWeAnalysisAppidUserPortraitResultJsonVisitUv visit_uv;


## WechatWxOpenGetWeAnalysisAppidUserPortraitResultJsonVisitUv (class)

- public List<WechatWxOpenGetWeAnalysisAppidUserPortraitResultJsonVisitUvItem> province;

- public List<WechatWxOpenGetWeAnalysisAppidUserPortraitResultJsonVisitUvItem> city;

- public List<WechatWxOpenGetWeAnalysisAppidUserPortraitResultJsonVisitUvItem> genders;

- public List<WechatWxOpenGetWeAnalysisAppidUserPortraitResultJsonVisitUvItem> platforms;

- public List<WechatWxOpenGetWeAnalysisAppidUserPortraitResultJsonVisitUvItem> devices;

- public List<WechatWxOpenGetWeAnalysisAppidUserPortraitResultJsonVisitUvItem> ages;


## WechatWxOpenGetWeAnalysisAppidUserPortraitResultJsonVisitUvItem (class)

- public int id;

- public string name;

- public int value_;


## WechatWxOpenGetWeAnalysisAppidVisitDistributionResultJson (class)

- public string ref_date;

- public List<WechatWxOpenGetWeAnalysisAppidVisitDistributionResultJsonList> list;


## WechatWxOpenGetWeAnalysisAppidVisitDistributionResultJsonList (class)

- public int index;

- public List<WechatWxOpenGetWeAnalysisAppidVisitDistributionResultJsonListItemList> item_list;


## WechatWxOpenGetWeAnalysisAppidVisitDistributionResultJsonListItemList (class)

- public int key;

- public int value_;

- public int access_source_visit_uv;


## WechatWxOpenGetWeAnalysisAppidVisitPageResultJson (class)

- public string ref_date;

- public List<WechatWxOpenGetWeAnalysisAppidVisitPageResultJsonList> list;


## WechatWxOpenGetWeAnalysisAppidVisitPageResultJsonList (class)

- public string page_path;

- public int page_visit_pv;

- public string page_visit_uv;

- public double page_staytime_pv;

- public int entrypage_pv;

- public int exitpage_pv;

- public int page_share_pv;

- public int page_share_uv;


## WechatWxOpenGetWeAnalysisAppidWeeklyVisitTrendResultJson (class)

- public List<WechatWxOpenGetWeAnalysisAppidWeeklyVisitTrendResultJsonList> list;


## WechatWxOpenGetWeAnalysisAppidWeeklyVisitTrendResultJsonList (class)

- public string ref_date;

- public int session_cnt;

- public int visit_pv;

- public int visit_uv;

- public int visit_uv_new;

- public double stay_time_uv;

- public double stay_time_session;

- public double visit_depth;


## WechatWxOpenHistoryItem (class)

- public string log_id;

- public string operator_;

- public string operate_time;

- public string operate_type;

- public string operate_details;

- public List<WechatWxOpenComplaintMediaItem> complaint_media_list;


## WechatWxOpenImmediateDeliveryAddOrderJsonResult (class)

- public string waybill_id;

- public int order_status;

- public int finish_code;

- public int pickup_code;

- public double fee;

- public double deliverfee;

- public double couponfee;

- public double tips;

- public double insurancefee;

- public double insurancfee;

- public int distance;

- public string delivery_token;

- public int dispatch_duration;

- public int resultcode;

- public string resultmsg;


## WechatWxOpenImmediateDeliveryAgent (class)

- public string name;

- public string phone;

- public int is_phone_encrypted;

- public double lng;

- public double lat;


## WechatWxOpenImmediateDeliveryBoundAccount (class)

- public string shopid;

- public string delivery_id;

- public int audit_result;


## WechatWxOpenImmediateDeliveryBoundAccountListJsonResult (class)

- public List<WechatWxOpenImmediateDeliveryBoundAccount> shop_list;

- public int resultcode;

- public string resultmsg;


## WechatWxOpenImmediateDeliveryCancelOrderJsonResult (class)

- public double deduct_fee;

- public string desc;

- public int resultcode;

- public string resultmsg;


## WechatWxOpenImmediateDeliveryCargo (class)

- public double goods_value;

- public double goods_height;

- public double goods_width;

- public double goods_length;

- public double goods_weight;

- public WechatWxOpenImmediateDeliveryGoodsDetail goods_detail;

- public string goods_pickup_info;

- public string cargo_first_class;

- public string cargo_second_class;


## WechatWxOpenImmediateDeliveryCompany (class)

- public string delivery_id;

- public string delivery_name;


## WechatWxOpenImmediateDeliveryCompanyListJsonResult (class)

- public List<WechatWxOpenImmediateDeliveryCompany> list;

- public int resultcode;

- public string resultmsg;


## WechatWxOpenImmediateDeliveryContact (class)

- public string name;

- public string city;

- public string address;

- public string address_detail;

- public string phone;

- public double lng;

- public double lat;

- public int coordinate_type;


## WechatWxOpenImmediateDeliveryGetOrderJsonResult (class)

- public int order_status;

- public string waybill_id;

- public string rider_name;

- public string rider_phone;

- public double rider_lng;

- public double rider_lat;

- public int reach_time;

- public int resultcode;

- public string resultmsg;


## WechatWxOpenImmediateDeliveryGoodsDetail (class)

- public List<WechatWxOpenImmediateDeliveryGoodsItem> goods;


## WechatWxOpenImmediateDeliveryGoodsItem (class)

- public int good_count;

- public string good_name;

- public double good_price;

- public string good_unit;


## WechatWxOpenImmediateDeliveryJsonResult (class)

- public int resultcode;

- public string resultmsg;


## WechatWxOpenImmediateDeliveryOrderInfo (class)

- public string delivery_service_code;

- public long expected_delivery_time;

- public string poi_seq;

- public string note;

- public long order_time;

- public int is_insured;

- public double declared_value;

- public double tips;

- public int is_direct_delivery;

- public double cash_on_delivery;

- public double cash_on_pickup;

- public int rider_pick_method;

- public int is_finish_code_needed;

- public int is_pickup_code_needed;

- public long expected_finish_time;

- public long expected_pick_time;

- public int order_type;


## WechatWxOpenImmediateDeliveryPreAddOrderJsonResult (class)

- public double fee;

- public double deliverfee;

- public double couponfee;

- public double tips;

- public double insurancefee;

- public double insurancfee;

- public int distance;

- public string delivery_token;

- public int dispatch_duration;

- public int resultcode;

- public string resultmsg;


## WechatWxOpenImmediateDeliveryShop (class)

- public string wxa_path;

- public string img_url;

- public string goods_name;

- public int goods_count;

- public string wxa_appid;

- public JsonValue detail_list;


## WechatWxOpenInner (class)

- public string name;


## WechatWxOpenInsuredModel (class)

- public int use_insured;

- public int insured_value;


## WechatWxOpenIosSettlementBillItem (class)

- public string month;

- public string bill_url;


## WechatWxOpenIsTradeManagedJsonResult (class)

- public bool is_trade_managed;


## WechatWxOpenIsTradeManagementConfirmationCompletedJsonResult (class)

- public bool completed;


## WechatWxOpenItems (class)

- public List<WechatWxOpenMapPoiItem> item;


## WechatWxOpenJsCode2JsonResult (class)

- public string openid;

- public string session_key;

- public string unionid;


## WechatWxOpenJsErrDetailItem (class)

- public string Count;

- public string sdkVersion;

- public string ClientVersion;

- public string errorStackMd5;

- public string TimeStamp;

- public string appVersion;

- public string errorMsgMd5;

- public string errorMsg;

- public string errorStack;

- public string Ds;

- public string OsName;

- public string openId;

- public string pluginversion;

- public string appId;

- public string DeviceModel;

- public string source;

- public string route;

- public string nickname;


## WechatWxOpenJsErrListItem (class)

- public string errorMsgMd5;

- public string errorMsg;

- public long uv;

- public long pv;

- public string errorStackMd5;

- public string errorStack;

- public string pvPercent;

- public string uvPercent;


## WechatWxOpenKey (class)

- public string name;

- public string direction;


## WechatWxOpenKfInfo (class)

- public string kf_nick;

- public string kf_id;

- public string kf_headimgurl;

- public string kf_wx;

- public string kf_openid;


## WechatWxOpenLibraryGetJsonResult (class)

- public string id;

- public string title;

- public List<WechatWxOpenLibraryGetJsonResultKeywordList> keyword_list;


## WechatWxOpenLibraryGetJsonResultKeywordList (class)

- public int keyword_id;

- public string name;

- public string example;


## WechatWxOpenLibraryListJsonResult (class)

- public List<WechatWxOpenLibraryListJsonResultList> list;

- public int total_count;


## WechatWxOpenLibraryListJsonResultList (class)

- public string id;

- public string title;


## WechatWxOpenListBusinessJsonResult (class)

- public List<WechatWxOpenCustomerServiceBusinessInfo> list;


## WechatWxOpenListJsonResult (class)

- public List<WechatWxOpenListJsonResultList> list;


## WechatWxOpenListJsonResultList (class)

- public string template_id;

- public string title;

- public string content;

- public string example;


## WechatWxOpenLiveBroadcastAddGoodsJsonResult (class)

- public long goodsId;

- public long auditId;


## WechatWxOpenLiveBroadcastAssistantInfo (class)

- public long timestamp;

- public string headimg;

- public string nickname;

- public string alias;

- public string openid;


## WechatWxOpenLiveBroadcastAssistantListJsonResult (class)

- public List<WechatWxOpenLiveBroadcastAssistantInfo> list;

- public int count;

- public int maxCount;


## WechatWxOpenLiveBroadcastAssistantUser (class)

- public string username;

- public string nickname;


## WechatWxOpenLiveBroadcastAuditIdJsonResult (class)

- public long auditId;


## WechatWxOpenLiveBroadcastCreateRoomJsonResult (class)

- public long roomId;

- public string qrcode_url;


## WechatWxOpenLiveBroadcastFollowerInfo (class)

- public string openid;

- public long subscribe_time;

- public long room_id;

- public int room_status;


## WechatWxOpenLiveBroadcastGetFollowersJsonResult (class)

- public List<WechatWxOpenLiveBroadcastFollowerInfo> followers;

- public long page_break;


## WechatWxOpenLiveBroadcastGetLiveInfoJsonResult (class)

- public List<WechatWxOpenLiveBroadcastRoomInfo> room_info;

- public int total;

- public List<WechatWxOpenLiveBroadcastReplayInfo> live_replay;


## WechatWxOpenLiveBroadcastGoodsId (class)

- public long goodsId;


## WechatWxOpenLiveBroadcastGoodsInfo (class)

- public string goods_id;

- public string goodsId;

- public string name;

- public string cover_img_url;

- public string coverImgUrl;

- public string url;

- public int price_type;

- public int priceType;

- public double price;

- public double price2;

- public int audit_status;

- public int third_party_tag;

- public int thirdPartyTag;

- public string thirdPartyAppid;


## WechatWxOpenLiveBroadcastGoodsInfoRequest (class)

- public string coverImgUrl;

- public string name;

- public int priceType;

- public double price;

- public double price2;

- public string url;

- public string thirdPartyAppid;

- public long goodsId;


## WechatWxOpenLiveBroadcastGoodsKeyJsonResult (class)

- public List<string> vendorGoodsKey;


## WechatWxOpenLiveBroadcastGoodsListJsonResult (class)

- public List<WechatWxOpenLiveBroadcastGoodsInfo> goods;

- public int total;


## WechatWxOpenLiveBroadcastGoodsVideoJsonResult (class)

- public string url;


## WechatWxOpenLiveBroadcastPushMessageJsonResult (class)

- public string message_id;


## WechatWxOpenLiveBroadcastPushUrlJsonResult (class)

- public string pushAddr;


## WechatWxOpenLiveBroadcastReplayInfo (class)

- public string create_time;

- public string expire_time;

- public string media_url;


## WechatWxOpenLiveBroadcastRoleInfo (class)

- public string headingimg;

- public string nickname;

- public string openid;

- public List<int> roleList;

- public string updateTimestamp;

- public string username;


## WechatWxOpenLiveBroadcastRoleJsonResult (class)

- public string codeurl;


## WechatWxOpenLiveBroadcastRoleListJsonResult (class)

- public int total;

- public List<WechatWxOpenLiveBroadcastRoleInfo> list;


## WechatWxOpenLiveBroadcastRoomGoodsInfo (class)

- public string name;

- public string cover_img;

- public string url;

- public long price;

- public long price2;

- public int price_type;

- public long goods_id;

- public string third_party_appid;


## WechatWxOpenLiveBroadcastRoomInfo (class)

- public string name;

- public string cover_img;

- public long start_time;

- public long end_time;

- public string anchor_name;

- public long roomid;

- public List<WechatWxOpenLiveBroadcastRoomGoodsInfo> goods;

- public int live_status;

- public string share_img;

- public int live_type;

- public int close_like;

- public int close_goods;

- public int close_comment;

- public int close_kf;

- public int close_replay;

- public int is_feeds_public;

- public string creater_openid;

- public string feeds_img;


## WechatWxOpenLiveBroadcastSharedCodeJsonResult (class)

- public string cdnUrl;

- public string pagePath;

- public string posterUrl;


## WechatWxOpenLiveBroadcastSubAnchorJsonResult (class)

- public string username;


## WechatWxOpenLoction (class)

- public double lat;

- public double lng;


## WechatWxOpenMapPoiItem (class)

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


## WechatWxOpenMediaCheckAsyncJsonResult (class)

- public string trace_id;


## WechatWxOpenMiniDramaActor (class)

- public string name;

- public string photo_material_id;

- public string role;

- public string profile;


## WechatWxOpenMiniDramaActorList (class)

- public List<WechatWxOpenMiniDramaActor> actor;


## WechatWxOpenMiniDramaApplyUploadJsonResult (class)

- public string upload_id;


## WechatWxOpenMiniDramaAuditDetail (class)

- public int status;

- public int audit_type;

- public long create_time;

- public long audit_time;


## WechatWxOpenMiniDramaAuditDramaJsonResult (class)

- public long drama_id;


## WechatWxOpenMiniDramaAuthorizationJsonResult (class)

- public List<WechatWxOpenMiniDramaAuthorizationOperationResult> result;


## WechatWxOpenMiniDramaAuthorizationOperationResult (class)

- public long drama_id;

- public int errcode;

- public string errmsg;


## WechatWxOpenMiniDramaAuthorizeObject (class)

- public long drama_id;

- public string authorized_appid;

- public long authorized_time;

- public long authz_expire_time;


## WechatWxOpenMiniDramaAuthorizedApp (class)

- public string authorized_appid;

- public long authorized_time;

- public long authz_expire_time;


## WechatWxOpenMiniDramaAuthorizedObject (class)

- public long drama_id;

- public string authorizer_appid;

- public long authorized_time;

- public long authz_expire_time;


## WechatWxOpenMiniDramaCdnLog (class)

- public string date;

- public string name;

- public string url;

- public long start_time;

- public long end_time;


## WechatWxOpenMiniDramaCdnUsageItem (class)

- public long time;

- public long value_;


## WechatWxOpenMiniDramaCopyrightAuthorizationInfo (class)

- public string authorizer_appid;

- public int authorization_type;

- public string authorized_appid;

- public string authorized_subject_cert_no;

- public long drama_id;

- public long authorized_time;

- public long expire_time;


## WechatWxOpenMiniDramaCopyrightAuthorizationListJsonResult (class)

- public int total_count;

- public List<WechatWxOpenMiniDramaCopyrightAuthorizationInfo> list;


## WechatWxOpenMiniDramaCopyrightInfo (class)

- public int copyright_role;

- public int apply_for_copyright_protection;

- public string copyright_verification;

- public List<string> proof_of_production;

- public List<string> purchase_or_broadcast_authorization_certificate;


## WechatWxOpenMiniDramaDramaInfo (class)

- public long drama_id;

- public long create_time;

- public string name;

- public string cover_url;

- public int media_count;

- public string producer;

- public string playwright;

- public string description;

- public string production_license;

- public WechatWxOpenMiniDramaAuditDetail audit_detail;

- public List<WechatWxOpenMiniDramaMediaReference> media_list;

- public int expedited;

- public string recommendations;

- public string promotion_poster;

- public WechatWxOpenMiniDramaActorList actor_list;

- public int status;

- public string other_material;


## WechatWxOpenMiniDramaFinderEvent (class)

- public string encrypted_event_id;

- public string event_name;

- public string event_url;

- public string src_appid;

- public string drama_id;


## WechatWxOpenMiniDramaFlushDramaItem (class)

- public string drama_name;

- public string src_appid;

- public string drama_id;


## WechatWxOpenMiniDramaGetAuthorizeAppsJsonResult (class)

- public List<WechatWxOpenMiniDramaAuthorizedApp> objects;


## WechatWxOpenMiniDramaGetAuthorizeObjectsJsonResult (class)

- public int total_count;

- public List<WechatWxOpenMiniDramaAuthorizeObject> objects;


## WechatWxOpenMiniDramaGetAuthorizedObjectsJsonResult (class)

- public int total_count;

- public List<WechatWxOpenMiniDramaAuthorizedObject> objects;


## WechatWxOpenMiniDramaGetCdnLogsJsonResult (class)

- public int total_count;

- public List<WechatWxOpenMiniDramaCdnLog> domestic_cdn_logs;


## WechatWxOpenMiniDramaGetCdnUsageDataJsonResult (class)

- public int data_interval;

- public List<WechatWxOpenMiniDramaCdnUsageItem> item_list;


## WechatWxOpenMiniDramaGetDramaJsonResult (class)

- public WechatWxOpenMiniDramaDramaInfo drama_info;


## WechatWxOpenMiniDramaGetFinderEventJsonResult (class)

- public List<WechatWxOpenMiniDramaFinderEvent> finder_event_list;


## WechatWxOpenMiniDramaGetLatestAuditInfoJsonResult (class)

- public WechatWxOpenMiniDramaAuditDetail audit_detail;


## WechatWxOpenMiniDramaGetMediaJsonResult (class)

- public WechatWxOpenMiniDramaMediaInfo media_info;


## WechatWxOpenMiniDramaGetMediaLinkJsonResult (class)

- public WechatWxOpenMiniDramaMediaPlayInfo media_info;


## WechatWxOpenMiniDramaGetMonetizationJsonResult (class)

- public List<WechatWxOpenMiniDramaMonetizationItem> list;


## WechatWxOpenMiniDramaGetPublishedDramaJsonResult (class)

- public List<WechatWxOpenMiniDramaPublishedDrama> list;


## WechatWxOpenMiniDramaGetTaskJsonResult (class)

- public WechatWxOpenMiniDramaTaskInfo task_info;


## WechatWxOpenMiniDramaListDramasJsonResult (class)

- public List<WechatWxOpenMiniDramaDramaInfo> drama_info_list;


## WechatWxOpenMiniDramaListMediaJsonResult (class)

- public List<WechatWxOpenMiniDramaMediaInfo> media_info_list;


## WechatWxOpenMiniDramaListPackagesJsonResult (class)

- public int total_count;

- public List<WechatWxOpenMiniDramaTrafficPackage> package_list;


## WechatWxOpenMiniDramaMediaAuditDetail (class)

- public int status;

- public long create_time;

- public long audit_time;

- public string reason;

- public List<string> evidence_material_id_list;


## WechatWxOpenMiniDramaMediaIdJsonResult (class)

- public long media_id;


## WechatWxOpenMiniDramaMediaInfo (class)

- public long media_id;

- public long create_time;

- public long expire_time;

- public long drama_id;

- public string file_size;

- public int duration;

- public string name;

- public string description;

- public string cover_url;

- public string original_url;

- public string mp4_url;

- public string hls_url;

- public WechatWxOpenMiniDramaMediaAuditDetail audit_detail;


## WechatWxOpenMiniDramaMediaPlayInfo (class)

- public long media_id;

- public int duration;

- public string name;

- public string description;

- public string cover_url;

- public string mp4_url;

- public string hls_url;


## WechatWxOpenMiniDramaMediaReference (class)

- public long media_id;


## WechatWxOpenMiniDramaMonetizationItem (class)

- public int iaa_type;

- public int vip_flag;

- public string src_appid;

- public string drama_id;


## WechatWxOpenMiniDramaPartInfo (class)

- public int part_number;

- public string etag;


## WechatWxOpenMiniDramaPlayerDramaIdentity (class)

- public string src_appid;

- public string drama_id;


## WechatWxOpenMiniDramaPromotionJsonResult (class)

- public List<WechatWxOpenMiniDramaPromotionStatus> list;


## WechatWxOpenMiniDramaPromotionStatus (class)

- public int status;

- public string src_appid;

- public string drama_id;


## WechatWxOpenMiniDramaPublishDramaItem (class)

- public long publish_time;

- public string drama_name;

- public string src_appid;

- public string drama_id;


## WechatWxOpenMiniDramaPublishedDrama (class)

- public long publish_time;

- public string src_appid;

- public string drama_id;


## WechatWxOpenMiniDramaPullUploadJsonResult (class)

- public long task_id;


## WechatWxOpenMiniDramaReplaceMediaItem (class)

- public long old;


## WechatWxOpenMiniDramaTaskInfo (class)

- public long id;

- public int task_type;

- public int status;

- public int errcode;

- public string errmsg;

- public long create_time;

- public long finish_time;

- public long media_id;


## WechatWxOpenMiniDramaTrafficPackage (class)

- public long start_time;

- public long end_time;

- public long used;

- public long all;

- public string order_id;

- public int status;

- public int is_deleted;

- public string package_id;


## WechatWxOpenMpTemplateMsg (class)

- public string appid;

- public string template_id;

- public string url;

- public JsonValue miniprogram;

- public JsonValue data;


## WechatWxOpenNovelAppAuthorizationInput (class)

- public string grantee_appid;

- public long expire_time;


## WechatWxOpenNovelAuditInfo (class)

- public int audit_status;

- public long create_time;

- public long audit_time;

- public string reason;

- public string suggestion;


## WechatWxOpenNovelAuthorizationJsonResult (class)

- public List<WechatWxOpenNovelAuthorizationOperationResult> results;


## WechatWxOpenNovelAuthorizationOperationResult (class)

- public int errcode;

- public string errmsg;


## WechatWxOpenNovelBatchCreateChaptersJsonResult (class)

- public List<string> chapter_id_list;

- public List<string> chapter_id;

- public List<string> conflict_original_id_list;


## WechatWxOpenNovelBookAuthorizationInfo (class)

- public string book_id;

- public string grantor_appid;

- public string grantee_appid;

- public long expire_time;

- public int sum;


## WechatWxOpenNovelBookAuthorizationInput (class)

- public string book_id;

- public string grantee_appid;

- public long expire_time;


## WechatWxOpenNovelBookIdJsonResult (class)

- public string book_id;


## WechatWxOpenNovelBookInfo (class)

- public string book_id;

- public string title;

- public string intro;

- public string cover_url;

- public string author;

- public int first_category_id;

- public string first_category_name;

- public int second_category_id;

- public string second_category_name;

- public int third_category_id;

- public string third_category_name;

- public int complete_status;

- public int upload_scene;

- public int chapter_cnt;

- public int volume_cnt;

- public List<WechatWxOpenNovelVolumeInfo> volume_list;

- public long total_word_cnt;

- public WechatWxOpenNovelAuditInfo audit_info;

- public long create_time;

- public string original_id;

- public int chapter_order_method;

- public string custom_info;

- public int ban_status;


## WechatWxOpenNovelChapterIdJsonResult (class)

- public string chapter_id;


## WechatWxOpenNovelChapterInfo (class)

- public string book_id;

- public string chapter_id;

- public string chapter_title;

- public string content;

- public int word_cnt;

- public long create_time;

- public WechatWxOpenNovelAuditInfo audit_info;

- public int volume_index;

- public string original_id;

- public long seq;

- public string custom_info;

- public int ban_status;


## WechatWxOpenNovelChapterInput (class)

- public string chapter_title;

- public string content;

- public string original_id;

- public long seq;

- public string custom_info;


## WechatWxOpenNovelChapterPreviewSetting (class)

- public int chapter_index;

- public int words;


## WechatWxOpenNovelChapterSequence (class)

- public string chapter_id;

- public long seq;


## WechatWxOpenNovelGetBookJsonResult (class)

- public WechatWxOpenNovelBookInfo book;


## WechatWxOpenNovelGetChapterJsonResult (class)

- public WechatWxOpenNovelChapterInfo chapter;


## WechatWxOpenNovelGetPreviewSettingJsonResult (class)

- public WechatWxOpenNovelPreviewSetting setting;


## WechatWxOpenNovelListBooksJsonResult (class)

- public List<WechatWxOpenNovelBookInfo> book_list;

- public int total_cnt;

- public long last_id;


## WechatWxOpenNovelListChaptersJsonResult (class)

- public List<WechatWxOpenNovelChapterInfo> chapter_list;

- public int total_cnt;


## WechatWxOpenNovelPreviewSetting (class)

- public string book_id;

- public int default_words;

- public List<WechatWxOpenNovelChapterPreviewSetting> chapter_setting;


## WechatWxOpenNovelQueryAppAuthorizationJsonResult (class)

- public List<WechatWxOpenNovelBookAuthorizationInfo> appid_results;

- public List<WechatWxOpenNovelBookAuthorizationInfo> book_results;

- public string next_cursor;


## WechatWxOpenNovelQueryBookAuthorizationJsonResult (class)

- public List<WechatWxOpenNovelBookAuthorizationInfo> results;


## WechatWxOpenNovelReplaceChapterJsonResult (class)

- public string new_chapter_id;


## WechatWxOpenNovelVolumeInfo (class)

- public string volume_title;

- public int start_index;

- public int end_index;


## WechatWxOpenOnlineKfInfo (class)

- public string kf_account;

- public int status;

- public string kf_id;

- public string kf_openid;


## WechatWxOpenOpenDataItem (class)

- public string cloud_id;

- public JsonValue json;


## WechatWxOpenOperationScene (class)

- public string name;

- public JsonValue value_;


## WechatWxOpenOrderKeyModel (class)

- public int order_number_type;

- public string transaction_id;

- public string mchid;

- public string out_trade_no;


## WechatWxOpenOrderModel (class)

- public string transaction_id;

- public string merchant_id;

- public string sub_merchant_id;

- public string merchant_trade_no;

- public string description;

- public long paid_amount;

- public string openid;

- public long trade_create_time;

- public long pay_time;

- public int order_state;

- public bool in_complaint;

- public WechatWxOpenShippingModel shipping;


## WechatWxOpenPage (class)

- public string path;

- public string query;


## WechatWxOpenPager (class)

- public int Offset;

- public int Limit;

- public int Total;


## WechatWxOpenPathItemListModel (class)

- public long action_time;

- public int action_type;

- public string action_msg;


## WechatWxOpenPayTimeRangeModel (class)

- public long begin_time;

- public long end_time;


## WechatWxOpenPayerModel (class)

- public string openid;


## WechatWxOpenPerformanceData (class)

- public WechatWxOpenPerformanceDataBody body;


## WechatWxOpenPerformanceDataBody (class)

- public List<WechatWxOpenPerformanceDataTable> tables;

- public int count;


## WechatWxOpenPerformanceDataField (class)

- public string refdate;

- public string value_;


## WechatWxOpenPerformanceDataLine (class)

- public List<WechatWxOpenPerformanceDataField> fields;


## WechatWxOpenPerformanceDataQuery (class)

- public string field;

- public string value_;


## WechatWxOpenPerformanceDataTable (class)

- public string id;

- public List<WechatWxOpenPerformanceDataLine> lines;

- public string zh;


## WechatWxOpenPhoneInfo (class)

- public string phoneNumber;

- public string purePhoneNumber;

- public int countryCode;

- public WechatWxOpenWatermark watermark;


## WechatWxOpenPluginList (class)

- public string appid;

- public int status;

- public string nickname;

- public string headimgurl;


## WechatWxOpenPreAddOrderCargo (class)

- public string goods_value;

- public string goods_height;

- public string goods_length;

- public string goods_width;

- public string goods_weight;

- public WechatWxOpenPreAddOrderGoods goods_detail;

- public string goods_pickup_info;

- public string goods_delivery_info;

- public string cargo_first_class;

- public string cargo_second_class;


## WechatWxOpenPreAddOrderGoods (class)

- public int good_count;

- public string good_name;

- public string good_price;

- public string good_unit;


## WechatWxOpenPreAddOrderJsonResult (class)

- public double fee;

- public double deliverfee;

- public double couponfee;

- public double tips;

- public double insurancefee;

- public double distance;

- public int dispatch_duration;

- public string delivery_token;

- public int resultcode;

- public string resultmsg;


## WechatWxOpenPreAddOrderOrderInfo (class)

- public string delivery_service_code;

- public int order_type;

- public long expected_delivery_time;

- public long expected_finish_time;

- public long expected_pick_time;

- public string poi_seq;

- public string note;

- public long order_time;

- public int is_insured;

- public double declared_value;

- public double tips;

- public int is_direct_delivery;

- public double cash_on_delivery;

- public double cash_on_pickup;

- public int rider_pick_method;

- public int is_finish_code_needed;

- public int is_pickup_code_needed;


## WechatWxOpenPreAddOrderReceiver (class)

- public string name;

- public string city;

- public string address;

- public string address_detail;

- public string phone;

- public double lng;

- public double lat;

- public int coordinate_type;


## WechatWxOpenPreAddOrderSender (class)

- public string name;

- public string city;

- public string address;

- public string address_detail;

- public string phone;

- public double lng;

- public double lat;

- public int coordinate_type;


## WechatWxOpenPreAddOrderShop (class)

- public string wxa_path;

- public string img_url;

- public string goods_name;

- public int goods_count;

- public string wxa_appid;


## WechatWxOpenPreCancelOrderJsonResult (class)

- public double deduct_fee;

- public string desc;

- public int resultcode;

- public string resultmsg;


## WechatWxOpenPresentCurrencyJsonResult (class)

- public int balance;

- public string order_id;

- public long present_balance;


## WechatWxOpenQualify (class)

- public List<WechatWxOpenExter> exter_list;


## WechatWxOpenQueryAdverFundsFilter (class)

- public long settle_begin;

- public long settle_end;

- public int fund_type;


## WechatWxOpenQueryAdverFundsItem (class)

- public long settle_begin;

- public long settle_end;

- public int total_amount;

- public int remain_amount;

- public long expire_time;

- public int fund_type;

- public string fund_id;


## WechatWxOpenQueryAdverFundsJsonResult (class)

- public List<WechatWxOpenQueryAdverFundsItem> adver_funds_list;

- public int total_page;


## WechatWxOpenQueryBizBalanceAvailable (class)

- public string amount;

- public string currency_code;


## WechatWxOpenQueryBizBalanceJsonResult (class)

- public WechatWxOpenQueryBizBalanceAvailable balance_available;


## WechatWxOpenQueryDownloadOrderJsonResult (class)

- public string authorization_state;


## WechatWxOpenQueryFundsBillItem (class)

- public string bill_id;

- public long oper_time;

- public long settle_begin;

- public long settle_end;

- public string fund_id;

- public string transfer_account_name;

- public long transfer_account_uid;

- public int transfer_amount;

- public int status;


## WechatWxOpenQueryFundsBillJsonResult (class)

- public List<WechatWxOpenQueryFundsBillItem> bill_list;

- public int total_page;


## WechatWxOpenQueryOrderJsonResult (class)

- public WechatWxOpenXPayXPayJsonQueryOrderJsonResultOrder order;


## WechatWxOpenQueryPublishGoodsItem (class)

- public string id;

- public int publish_status;

- public string errmsg;


## WechatWxOpenQueryPublishGoodsJsonResult (class)

- public List<WechatWxOpenQueryPublishGoodsItem> publish_item;

- public int status;


## WechatWxOpenQueryPunishmentReasonsJsonResult (class)

- public string appid;

- public string nickname;

- public string merchant_code;

- public List<string> limited_functions;

- public string other_limited_functions;

- public List<WechatWxOpenXPayRecoverySpecification> recovery_specifications;


## WechatWxOpenQueryRecoverBillFilter (class)

- public long recover_time_begin;

- public long recover_time_end;

- public string bill_id;


## WechatWxOpenQueryRecoverBillItem (class)

- public string bill_id;

- public long recover_time;

- public long settle_begin;

- public long settle_end;

- public string fund_id;

- public string recover_account_name;

- public int recover_amount;

- public JsonValue refund_order_list;


## WechatWxOpenQueryRecoverBillJsonResult (class)

- public List<WechatWxOpenQueryRecoverBillItem> bill_list;

- public int total_page;


## WechatWxOpenQuerySchemeInfo (class)

- public string appid;

- public string path;

- public string query;

- public long create_time;

- public long expire_time;

- public string env_version;


## WechatWxOpenQuerySchemeJsonResult (class)

- public WechatWxOpenQuerySchemeInfo scheme_info;

- public WechatWxOpenQuerySchemeQuotaInfo quota_info;


## WechatWxOpenQuerySchemeQuotaInfo (class)

- public long remain_visit_quota;


## WechatWxOpenQuerySubscribeContractJsonResult (class)

- public string authorization_state;


## WechatWxOpenQueryTransferAccountItem (class)

- public string transfer_account_name;

- public long transfer_account_uid;

- public long transfer_account_agency_id;

- public string transfer_account_agency_name;

- public int state;

- public int bind_result;

- public string error_msg;


## WechatWxOpenQueryTransferAccountJsonResult (class)

- public List<WechatWxOpenQueryTransferAccountItem> acct_list;


## WechatWxOpenQueryUploadGoodsItem (class)

- public string id;

- public string name;

- public int price;

- public string remark;

- public string item_url;

- public int upload_status;

- public string errmsg;


## WechatWxOpenQueryUploadGoodsJsonResult (class)

- public List<WechatWxOpenQueryUploadGoodsItem> upload_item;

- public int status;


## WechatWxOpenQueryUrlLinkCloudBase (class)

- public string env;

- public string doamin;

- public string path;

- public string query;

- public string resource_appid;


## WechatWxOpenQueryUrlLinkInfo (class)

- public string appid;

- public string path;

- public string query;

- public long create_time;

- public long expire_time;

- public string env_version;

- public WechatWxOpenQueryUrlLinkCloudBase cloud_base;


## WechatWxOpenQueryUrlLinkJsonResult (class)

- public WechatWxOpenQueryUrlLinkInfo url_link_info;

- public WechatWxOpenQueryUrlLinkQuotaInfo quota_info;


## WechatWxOpenQueryUrlLinkQuotaInfo (class)

- public long remain_visit_quota;


## WechatWxOpenQueryUserBalanceJsonResult (class)

- public int balance;

- public int present_balance;

- public int sum_save;

- public int sum_present;

- public int sum_balance;

- public int sum_cost;

- public bool first_save_flag;


## WechatWxOpenQueryWithdrawOrderJsonResult (class)

- public string withdraw_no;

- public int status;

- public string withdraw_amount;

- public string wx_withdraw_no;

- public string withdraw_success_timestamp;

- public string create_time;

- public string fail_reason;


## WechatWxOpenQuickCheckStudentIdentityJsonResult (class)

- public int bind_status;

- public bool is_student;


## WechatWxOpenReOrderJsonResult (class)

- public double fee;

- public double deliverfee;

- public double couponfee;

- public double tips;

- public double insurancefee;

- public double distance;

- public string waybill_id;

- public int order_status;

- public int finish_code;

- public int pickup_code;

- public int dispatch_duration;

- public int resultcode;

- public string resultmsg;


## WechatWxOpenRealTimeLogData (class)

- public List<WechatWxOpenRealTimeLogItem> list;

- public long total;


## WechatWxOpenRealTimeLogItem (class)

- public int level;

- public string libraryVersion;

- public string clientVersion;

- public string id;

- public long timestamp;

- public int platform;

- public string url;

- public List<WechatWxOpenRealTimeLogMessage> msg;

- public string traceid;

- public string filterMsg;


## WechatWxOpenRealTimeLogMessage (class)

- public long time;

- public JsonValue msg;

- public int level;


## WechatWxOpenRealTimeLogSearchJsonResult (class)

- public WechatWxOpenRealTimeLogData data;


## WechatWxOpenRedPacketCoverUrlData (class)

- public string url;


## WechatWxOpenRedPacketCoverUrlJsonResult (class)

- public WechatWxOpenRedPacketCoverUrlData data;


## WechatWxOpenRefundOrderJsonResult (class)

- public string refund_order_id;

- public string refund_wx_order_id;

- public string pay_order_id;

- public string pay_wx_order_id;


## WechatWxOpenRegisterBusinessJsonResult (class)

- public JsonValue business_id;


## WechatWxOpenResetUserSessionKeyJsonResult (class)

- public string openid;

- public string session_key;


## WechatWxOpenResultData (class)

- public int base_id;

- public int rich_id;


## WechatWxOpenResultFileList (class)

- public string fileid;

- public string download_url;

- public int status;

- public string errmsg;


## WechatWxOpenRule (class)

- public string prefix;

- public long permit_sub_rule;

- public string path;

- public long open_version;

- public List<string> debug_url;

- public long state;


## WechatWxOpenSearchMapPoiJsonResult (class)

- public WechatWxOpenItems data;


## WechatWxOpenSecSecJsonJsonResultGetOrderJsonResult (class)

- public WechatWxOpenOrderModel order;


## WechatWxOpenSendSubscribePrePaymentJsonResult (class)

- public JsonValue Value;


## WechatWxOpenSenderOrReceiverModel (class)

- public string name;

- public string tel;

- public string mobile;

- public string company;

- public string post_code;

- public string country;

- public string province;

- public string city;

- public string area;

- public string address;


## WechatWxOpenService (class)

- public string icon_url;

- public int type;

- public int id;

- public string name;


## WechatWxOpenServiceInfos (class)

- public List<WechatWxOpenService> srvice_infos;


## WechatWxOpenServiceMarketInvokeJsonResult (class)

- public string data;

- public string request_id;


## WechatWxOpenServiceType (class)

- public int service_type;

- public string service_name;


## WechatWxOpenShippingListModel (class)

- public string tracking_no;

- public string express_company;

- public string item_desc;

- public WechatWxOpenContactModel contact;


## WechatWxOpenShippingModel (class)

- public int delivery_mode;

- public int logistics_type;

- public bool finish_shipping;

- public string goods_desc;

- public int finish_shipping_count;

- public List<WechatWxOpenShippingShippingListModel> shipping_list;


## WechatWxOpenShippingShippingListModel (class)

- public string goods_desc;

- public long upload_time;

- public string tracking_no;

- public string express_company;

- public string item_desc;

- public WechatWxOpenContactModel contact;


## WechatWxOpenShopModel (class)

- public string wxa_path;

- public string img_url;

- public string goods_name;

- public int goods_count;


## WechatWxOpenShortLinkGenerateResult (class)

- public string link;


## WechatWxOpenSoterVerifySignatureJsonResult (class)

- public bool is_ok;


## WechatWxOpenStartDownloadOrderJsonResult (class)

- public string task_id;


## WechatWxOpenStartPublishGoodsItem (class)

- public string id;


## WechatWxOpenStartUploadGoodsItem (class)

- public string id;

- public string name;

- public int price;

- public string remark;

- public string item_url;


## WechatWxOpenStoreWxaAttr (class)

- public long appuin;

- public long create_time;

- public long update_time;

- public long owner_uin;

- public int owner_type;

- public WechatWxOpenStoreWxaAuditInfo storewxa_audit_info;


## WechatWxOpenStoreWxaAuditInfo (class)

- public long audit_id;

- public int status;

- public string reason;


## WechatWxOpenSubOrdersModel (class)

- public WechatWxOpenOrderKeyModel order_key;

- public int logistics_type;

- public int delivery_mode;

- public bool is_all_delivered;

- public List<WechatWxOpenShippingListModel> shipping_list;


## WechatWxOpenSubmitSubscribePayOrderJsonResult (class)

- public JsonValue Value;


## WechatWxOpenTestUpdateOrderJsonResult (class)

- public JsonValue Value;


## WechatWxOpenTradeTypeMaterial (class)

- public int type;

- public string media_id;


## WechatWxOpenTransactionGuaranteeActionJsonResult (class)

- public bool success;


## WechatWxOpenTransactionGuaranteeBusinessInfo (class)

- public string appid;

- public string headImg;

- public string nickName;


## WechatWxOpenTransactionGuaranteeCommentContent (class)

- public string txt;

- public List<WechatWxOpenTransactionGuaranteeCommentMedia> media;


## WechatWxOpenTransactionGuaranteeCommentDetailInfo (class)

- public WechatWxOpenTransactionGuaranteeCommentItem content;


## WechatWxOpenTransactionGuaranteeCommentExtraInfo (class)

- public bool isAlreadySendTmpl;


## WechatWxOpenTransactionGuaranteeCommentInfoJsonResult (class)

- public WechatWxOpenTransactionGuaranteeCommentDetailInfo info;

- public WechatWxOpenTransactionGuaranteeCommentProcessInfo processInfo;

- public WechatWxOpenTransactionGuaranteeOldComment oldComment;


## WechatWxOpenTransactionGuaranteeCommentItem (class)

- public string commentId;

- public long amount;

- public string orderId;

- public string createTime;

- public string payTime;

- public string wxPayId;

- public WechatWxOpenTransactionGuaranteeOrderInfo orderInfo;

- public WechatWxOpenTransactionGuaranteeUserInfo userInfo;

- public WechatWxOpenTransactionGuaranteeBusinessInfo bizInfo;

- public int score;

- public WechatWxOpenTransactionGuaranteeCommentContent content;

- public WechatWxOpenTransactionGuaranteeCommentExtraInfo extInfo;

- public WechatWxOpenTransactionGuaranteeProductInfo productInfo;


## WechatWxOpenTransactionGuaranteeCommentListJsonResult (class)

- public bool success;

- public int offset;

- public int total;

- public List<WechatWxOpenTransactionGuaranteeCommentItem> commentList;


## WechatWxOpenTransactionGuaranteeCommentMedia (class)

- public string img;

- public string thumbImg;

- public string video;

- public string videoCover;

- public int videoDuration;


## WechatWxOpenTransactionGuaranteeCommentProcessAction (class)

- public int type;

- public long updateTime;


## WechatWxOpenTransactionGuaranteeCommentProcessInfo (class)

- public List<WechatWxOpenTransactionGuaranteeCommentProcessAction> actionList;

- public string commentId;


## WechatWxOpenTransactionGuaranteeCommentReply (class)

- public string commentId;

- public string commentReplyId;

- public string createTime;

- public string updateTime;

- public WechatWxOpenTransactionGuaranteeReplyContent commentReplyContent;

- public WechatWxOpenTransactionGuaranteeReplyObject commentReplyObject;


## WechatWxOpenTransactionGuaranteeComplaintDetailJsonResult (class)

- public WechatWxOpenTransactionGuaranteeComplaintOrder complaintOrder;

- public List<WechatWxOpenTransactionGuaranteeComplaintProgressItem> item;

- public WechatWxOpenTransactionGuaranteeReturnBill returnBill;


## WechatWxOpenTransactionGuaranteeComplaintOrder (class)

- public string complaintOrderId;

- public string openid;

- public long createTime;

- public string phoneNumber;

- public int type;

- public int status;

- public WechatWxOpenTransactionGuaranteeCustomerMaterial customerMaterial;

- public string orderId;

- public string outTradeNo;

- public string productName;

- public long payTime;

- public string totalCost;

- public long expireTime;

- public string headImgUrl;

- public string nickName;

- public int appealState;


## WechatWxOpenTransactionGuaranteeComplaintProgressItem (class)

- public int itemType;

- public long time;

- public string content;

- public List<string> mediaIdList;

- public string phoneNumber;

- public int blameResult;

- public string nickName;

- public int appealItemType;


## WechatWxOpenTransactionGuaranteeCustomerMaterial (class)

- public string content;

- public List<string> mediaIdList;


## WechatWxOpenTransactionGuaranteeOldComment (class)

- public string commentId;

- public string createTime;

- public int score;

- public WechatWxOpenTransactionGuaranteeOldCommentContent content;


## WechatWxOpenTransactionGuaranteeOldCommentContent (class)

- public string ext;

- public List<WechatWxOpenTransactionGuaranteeCommentMedia> media;


## WechatWxOpenTransactionGuaranteeOrderInfo (class)

- public string busiOrderId;


## WechatWxOpenTransactionGuaranteePenaltyListJsonResult (class)

- public List<WechatWxOpenTransactionGuaranteePenaltyRecord> appealList;

- public int currentScore;

- public int totalNum;


## WechatWxOpenTransactionGuaranteePenaltyRecord (class)

- public string illegalOrderId;

- public string complaintOrderId;

- public string illegalWording;

- public int status;

- public int minusScore;

- public string orderId;

- public long illegalTime;

- public long updateTime;


## WechatWxOpenTransactionGuaranteeProduct (class)

- public string name;

- public string picUrl;


## WechatWxOpenTransactionGuaranteeProductInfo (class)

- public List<WechatWxOpenTransactionGuaranteeProduct> productList;


## WechatWxOpenTransactionGuaranteeReply (class)

- public string commentId;

- public string replyId;

- public string createTime;

- public string updateTime;

- public WechatWxOpenTransactionGuaranteeReplyContent replyContent;

- public WechatWxOpenTransactionGuaranteeReplyObject replyObject;


## WechatWxOpenTransactionGuaranteeReplyCollection (class)

- public WechatWxOpenTransactionGuaranteeReply reply;

- public List<WechatWxOpenTransactionGuaranteeCommentReply> commentReplyList;


## WechatWxOpenTransactionGuaranteeReplyContent (class)

- public string content;


## WechatWxOpenTransactionGuaranteeReplyListJsonResult (class)

- public WechatWxOpenTransactionGuaranteeReplyCollection list;


## WechatWxOpenTransactionGuaranteeReplyObject (class)

- public string nickname;

- public string imgUrl;


## WechatWxOpenTransactionGuaranteeReturnBill (class)

- public string returnId;

- public string waybillId;

- public string deliveryName;

- public int orderStatus;


## WechatWxOpenTransactionGuaranteeStatusJsonResult (class)

- public bool isActived;

- public string msg;

- public List<string> reasons;


## WechatWxOpenTransactionGuaranteeUserInfo (class)

- public string openid;

- public string headImg;

- public string nickName;


## WechatWxOpenUpdatableMessageParameter (class)

- public string name;

- public string value_;


## WechatWxOpenUpdatePrinterJsonResult (class)

- public JsonValue Value;


## WechatWxOpenUploadVpFileJsonResult (class)

- public string file_id;


## WechatWxOpenUsageResultJson (class)

- public string all;

- public string effectiveAll;

- public string effectiveUse;

- public long startServiceTime;

- public long endServiceTime;

- public int total;

- public List<WechatWxOpenUsageResultJsonDetailList> detailList;


## WechatWxOpenUsageResultJsonDetailList (class)

- public string pkgId;

- public int status;

- public long startTime;

- public long endTime;

- public string used;

- public string all;

- public string spuId;

- public string skuId;

- public int source;


## WechatWxOpenUserEncryptKeyInfo (class)

- public string encrypt_key;

- public int version;

- public long expire_in;

- public string iv;

- public long create_time;


## WechatWxOpenUserNotifyInfo (class)

- public int notify_type;

- public string content_json;

- public int code_state;

- public long code_expire_time;


## WechatWxOpenWatermark (class)

- public int timestamp;

- public string appid;


## WechatWxOpenWayBillDataModel (class)

- public string key;

- public string value_;


## WechatWxOpenWeappCategory (class)

- public List<WechatWxOpenCategories> categories;


## WechatWxOpenWeappTemplateMsg (class)

- public string template_id;

- public string page;

- public string form_id;

- public JsonValue data;

- public string emphasis_keyword;


## WechatWxOpenWeixinExpressAddReturnIdJsonResult (class)

- public string return_id;


## WechatWxOpenWeixinExpressDeliveryInfo (class)

- public string delivery_id;

- public string delivery_name;


## WechatWxOpenWeixinExpressDeliveryListJsonResult (class)

- public List<WechatWxOpenWeixinExpressDeliveryInfo> delivery_list;

- public int count;


## WechatWxOpenWeixinExpressGetReturnIdJsonResult (class)

- public int status;

- public string waybill_id;

- public int order_status;

- public string delivery_id;

- public string delivery_name;


## WechatWxOpenWeixinExpressGoodsInfo (class)

- public List<WechatWxOpenWeixinExpressGoodsItem> detail_list;


## WechatWxOpenWeixinExpressGoodsItem (class)

- public string goods_name;

- public string goods_img_url;

- public string goods_desc;


## WechatWxOpenWeixinExpressInsuranceApplyPayJsonResult (class)

- public string pay_url;


## WechatWxOpenWeixinExpressInsuranceClaimJsonResult (class)

- public string report_no;

- public int is_home_pick_up;


## WechatWxOpenWeixinExpressInsuranceCreateChargeJsonResult (class)

- public string order_id;


## WechatWxOpenWeixinExpressInsuranceCreateOrderJsonResult (class)

- public string policy_no;

- public string insurance_end_date;

- public long estimate_amount;

- public long premium;


## WechatWxOpenWeixinExpressInsuranceGoodsItem (class)

- public string name;

- public string url;


## WechatWxOpenWeixinExpressInsuranceOpenStatusJsonResult (class)

- public int is_open;


## WechatWxOpenWeixinExpressInsuranceOrder (class)

- public string order_no;

- public string policy_no;

- public string report_no;

- public string delivery_no;

- public string refund_delivery_no;

- public long premium;

- public long estimate_amount;

- public int status;

- public string pay_fail_reason;

- public long pay_finish_time;

- public int is_home_pick_up;

- public string insurance_end_date;


## WechatWxOpenWeixinExpressInsuranceOrderListJsonResult (class)

- public int total;

- public List<WechatWxOpenWeixinExpressInsuranceOrder> list;


## WechatWxOpenWeixinExpressInsurancePayOrder (class)

- public string order_id;

- public int order_status;

- public long total_price;

- public long create_time;

- public long pay_time;

- public bool can_refund;

- public long refund_time;

- public int refund_status;

- public long refund_amt;


## WechatWxOpenWeixinExpressInsurancePayOrderListJsonResult (class)

- public int total;

- public List<WechatWxOpenWeixinExpressInsurancePayOrder> list;


## WechatWxOpenWeixinExpressInsurancePlace (class)

- public string province;

- public string city;

- public string county;

- public string address;


## WechatWxOpenWeixinExpressInsuranceProductInfo (class)

- public string order_path;

- public List<WechatWxOpenWeixinExpressInsuranceGoodsItem> goods_list;


## WechatWxOpenWeixinExpressInsuranceSummaryJsonResult (class)

- public int total;

- public int claim_num;

- public int claim_succ_num;

- public long premium;

- public long funds;

- public bool need_close;


## WechatWxOpenWeixinExpressIntracityAddOrderJsonResult (class)

- public string wx_store_id;

- public string wx_order_id;

- public string store_order_id;

- public string service_trans_id;

- public double distance;

- public string trans_order_id;

- public string waybill_id;

- public long fee;

- public string fetch_code;

- public string order_seq;


## WechatWxOpenWeixinExpressIntracityAddressInfo (class)

- public string province;

- public string city;

- public string area;

- public string street;

- public string house;

- public double lat;

- public double lng;

- public string phone;


## WechatWxOpenWeixinExpressIntracityBalanceDetail (class)

- public long balance;

- public string service_trans_id;

- public string service_trans_name;

- public List<WechatWxOpenWeixinExpressIntracityBalanceOrder> order_list;


## WechatWxOpenWeixinExpressIntracityBalanceOrder (class)

- public string payorder_id;

- public long charge_amt;

- public long unused_amt;

- public long begin_time;

- public long end_time;


## WechatWxOpenWeixinExpressIntracityBalanceQueryJsonResult (class)

- public string wx_store_id;

- public string appid;

- public long all_balance;

- public List<WechatWxOpenWeixinExpressIntracityBalanceDetail> balance_detail;


## WechatWxOpenWeixinExpressIntracityCancelOrderJsonResult (class)

- public string wx_store_id;

- public string wx_order_id;

- public string store_order_id;

- public int order_status;

- public string appid;

- public long deductfee;


## WechatWxOpenWeixinExpressIntracityCargo (class)

- public string cargo_name;

- public double cargo_weight;

- public long cargo_price;

- public int cargo_type;

- public int cargo_num;

- public List<WechatWxOpenWeixinExpressIntracityCargoItem> item_list;


## WechatWxOpenWeixinExpressIntracityCargoInfo (class)

- public string cargo_name;

- public double cargo_weight;

- public long cargo_price;

- public int cargo_type;

- public int cargo_num;

- public List<WechatWxOpenWeixinExpressIntracityCargoInfoItem> item_list;


## WechatWxOpenWeixinExpressIntracityCargoInfoItem (class)

- public string item_name;

- public string item_pic_url;

- public int num;


## WechatWxOpenWeixinExpressIntracityCargoItem (class)

- public string item_name;

- public string item_pic_url;

- public int count;


## WechatWxOpenWeixinExpressIntracityCity (class)

- public int city_code;

- public string city_name;


## WechatWxOpenWeixinExpressIntracityCitySupport (class)

- public string service_trans_id;

- public List<WechatWxOpenWeixinExpressIntracityCity> city_list;


## WechatWxOpenWeixinExpressIntracityCreateStoreJsonResult (class)

- public string wx_store_id;

- public string appid;

- public string out_store_id;


## WechatWxOpenWeixinExpressIntracityFlow (class)

- public int flow_type;

- public string appid;

- public string wx_store_id;

- public string pay_order_id;

- public string service_trans_id;

- public long pay_amount;

- public long pay_time;

- public string pay_status;

- public long create_time;

- public long consume_deadline;

- public long refund_time;

- public long refund_amount;

- public string openid;

- public int delivery_status;

- public string refund_status;

- public long deduct_amount;

- public string bill_id;

- public long delivery_finished_time;


## WechatWxOpenWeixinExpressIntracityGetCityJsonResult (class)

- public List<WechatWxOpenWeixinExpressIntracityCitySupport> support_list;


## WechatWxOpenWeixinExpressIntracityGetPayModeJsonResult (class)

- public string pay_mode;

- public string pay_appid;

- public string pay_component_appid;


## WechatWxOpenWeixinExpressIntracityPreAddOrderJsonResult (class)

- public string service_trans_id;

- public double distance;

- public long fee;


## WechatWxOpenWeixinExpressIntracityQueryFlowJsonResult (class)

- public int total;

- public List<WechatWxOpenWeixinExpressIntracityFlow> flow_list;

- public long total_pay_amt;

- public long total_refund_amt;

- public long total_deduct_amt;


## WechatWxOpenWeixinExpressIntracityQueryOrderJsonResult (class)

- public string wx_order_id;

- public string store_order_id;

- public string wx_store_id;

- public int order_status;

- public string appid;

- public string user_openid;

- public string service_trans_id;

- public string delivery_no;

- public double distance;

- public long actualfee;

- public long deductfee;

- public long create_time;

- public long accept_time;

- public long finish_time;

- public long fetch_time;

- public long cancel_time;

- public long expected_finish_time;

- public string fetch_code;

- public string recv_code;

- public string order_seq;

- public WechatWxOpenWeixinExpressIntracityTransporterInfo transporter_info;

- public WechatWxOpenWeixinExpressIntracityStoreSnapshot store_info;

- public WechatWxOpenWeixinExpressIntracityReceiverInfo receiver_info;

- public WechatWxOpenWeixinExpressIntracityCargoInfo cargo_info;


## WechatWxOpenWeixinExpressIntracityQueryStoreJsonResult (class)

- public List<WechatWxOpenWeixinExpressIntracityStoreInfo> store_list;

- public int total;

- public string appid;


## WechatWxOpenWeixinExpressIntracityReceiverInfo (class)

- public string receiver_name;

- public string address;

- public string phone_num;

- public double lng;

- public double lat;


## WechatWxOpenWeixinExpressIntracityStoreChargeJsonResult (class)

- public string payurl;

- public string appid;

- public string wx_store_id;


## WechatWxOpenWeixinExpressIntracityStoreInfo (class)

- public string wx_store_id;

- public string out_store_id;

- public string store_name;

- public string city_id;

- public int order_pattern;

- public string service_trans_prefer;

- public WechatWxOpenWeixinExpressIntracityAddressInfo address_info;


## WechatWxOpenWeixinExpressIntracityStoreKey (class)

- public string wx_store_id;

- public string out_store_id;


## WechatWxOpenWeixinExpressIntracityStoreRefundJsonResult (class)

- public string appid;

- public string wx_store_id;

- public long refund_amount;


## WechatWxOpenWeixinExpressIntracityStoreSnapshot (class)

- public string store_name;

- public string wx_store_id;

- public string address;

- public double lng;

- public double lat;

- public string phone_num;


## WechatWxOpenWeixinExpressIntracityStoreUpdateContent (class)

- public string store_name;

- public int order_pattern;

- public string service_trans_prefer;

- public WechatWxOpenWeixinExpressIntracityAddressInfo address_info;


## WechatWxOpenWeixinExpressIntracityTransporterInfo (class)

- public string transporter_name;

- public string transporter_phone;


## WechatWxOpenWeixinExpressPathContact (class)

- public string name;

- public string phone;

- public string province;

- public string city;

- public string area;

- public string street;

- public string address;

- public string id;


## WechatWxOpenWeixinExpressPathNode (class)

- public long action_time;

- public int action_type;

- public string action_msg;

- public string pickup_courier_name;

- public string pickup_courier_phone;

- public string delivery_courier_name;

- public string delivery_courier_phone;


## WechatWxOpenWeixinExpressQueryTraceJsonResult (class)

- public WechatWxOpenWeixinExpressWaybillInfo waybill_info;

- public WechatWxOpenWeixinExpressShopInfo shop_info;

- public WechatWxOpenWeixinExpressDeliveryInfo delivery_info;


## WechatWxOpenWeixinExpressReturnAddress (class)

- public string name;

- public string tel;

- public string mobile;

- public string company;

- public string post_code;

- public string country;

- public string province;

- public string city;

- public string area;

- public string address;


## WechatWxOpenWeixinExpressReturnGoodsItem (class)

- public string name;

- public string url;


## WechatWxOpenWeixinExpressShopInfo (class)

- public WechatWxOpenWeixinExpressGoodsInfo goods_info;


## WechatWxOpenWeixinExpressTraceWaybillJsonResult (class)

- public string waybill_token;


## WechatWxOpenWeixinExpressUserBindingJsonResult (class)

- public int exist;


## WechatWxOpenWeixinExpressWaybillInfo (class)

- public int status;

- public string waybill_id;


## WechatWxOpenWxCloudFunctionJsonResult (class)

- public string resp_data;


## WechatWxOpenWxDatabaseAddJsonResult (class)

- public List<string> id_list;


## WechatWxOpenWxDatabaseAggregateJsonResult (class)

- public List<string> data;


## WechatWxOpenWxDatabaseCollectionJsonResult (class)

- public List<WechatWxOpenCollection> collections;

- public WechatWxOpenPager pager;


## WechatWxOpenWxDatabaseCountJsonResult (class)

- public int count;


## WechatWxOpenWxDatabaseDeleteJsonResult (class)

- public int deleted;


## WechatWxOpenWxDatabaseMigrateJsonResult (class)

- public int job_id;


## WechatWxOpenWxDatabaseMigrateQueryInfoJsonResult (class)

- public string status;

- public int record_success;

- public int record_fail;

- public string err_msg;

- public string file_url;


## WechatWxOpenWxDatabaseQueryJsonResult (class)

- public WechatWxOpenPager pager;

- public List<string> data;


## WechatWxOpenWxDatabaseUpdateJsonResult (class)

- public int matched;

- public int modified;

- public string id;


## WechatWxOpenWxDeleteFileJsonResult (class)

- public List<WechatWxOpenResultFileList> delete_list;


## WechatWxOpenWxDownloadFileJsonResult (class)

- public List<WechatWxOpenResultFileList> file_list;


## WechatWxOpenWxQcloudTokenJsonResult (class)

- public string secretid;

- public string secretkey;

- public string token;

- public int expired_time;


## WechatWxOpenWxUploadFileJsonResult (class)

- public string url;

- public string token;

- public string authorization;

- public string file_id;

- public string cos_file_id;


## WechatWxOpenXPayRecoverySpecification (class)

- public string limitation_case_id;

- public string limitation_reason_type;

- public string limitation_reason;

- public string limitation_reason_describe;

- public JsonValue relate_limitations;

- public string other_relate_limitations;

- public string recover_way;

- public string recover_way_param;

- public string recover_help_url;

- public string limitation_action_type;

- public string limitation_start_date;

- public string limitation_date;


## WechatWxOpenXPayXPayJsonQueryOrderJsonResultOrder (class)

- public string order_id;

- public long create_time;

- public long update_time;

- public int status;

- public int biz_type;

- public int order_fee;

- public int coupon_fee;

- public int paid_fee;

- public int order_type;

- public int refund_fee;

- public long paid_time;

- public long provide_time;

- public string biz_meta;

- public int env_type;

- public string token;

- public int left_fee;

- public string wx_order_id;

- public string channel_order_id;

- public string wxpay_order_id;

- public long sett_time;

- public int sett_state;
