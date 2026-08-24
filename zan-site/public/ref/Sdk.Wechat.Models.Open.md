# Sdk.Wechat.Models.Open

> 源码: `stdlib/Sdk/Wechat/Models/Open/WechatOpenModels.zan`


## WechatOpenAccountBasicInfoJsonResult (class)

从随附的 C# SDK 生成的共享 DTO 实体。
一个 C# 命名空间/类型标识精确对应一个 Zan 模型。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- public string appid;

- public int account_type;

- public int principal_type;

- public string principal_name;

- public string credential;

- public int realname_status;

- public WechatOpenWxVerifyInfo wx_verify_info;

- public WechatOpenSignatureInfo signature_info;

- public WechatOpenHeadImageInfo head_image_info;

- public WechatOpenNicknameInfo nickname_info;

- public int registered_country;

- public int customer_type;


## WechatOpenAddCategoryData (class)

- public long first;

- public long second;

- public List<WechatOpenAddCategoryDataCerticates> certicates;


## WechatOpenAddCategoryDataCerticates (class)

- public string key;

- public string value_;


## WechatOpenAddTemplateJsonResult (class)

- public string priTmplId;


## WechatOpenAppealMaterial (class)

- public string reason;

- public List<string> proof_material_ids;


## WechatOpenApplyIcpFilingResultJson (class)

- public List<WechatOpenHintsModel> hints;


## WechatOpenApplyPrivacyInterfaceJsonResult (class)

- public long audit_id;


## WechatOpenApplySetOrderPathInfoJsonResult (class)

- public JsonValue Value;


## WechatOpenAuditDataModel (class)

- public string key_name;

- public string error;

- public string suggest;


## WechatOpenAuthIdentityTreeJsonResult (class)

- public List<WechatOpenAuthIdentityTreeNode> identity_tree_list;


## WechatOpenAuthIdentityTreeNode (class)

- public string name;

- public int node_id;

- public List<WechatOpenAuthIdentityTreeNode> node_list;


## WechatOpenAuthorizationInfo (class)

- public string authorizer_appid;

- public string authorization_appid;

- public string authorizer_access_token;

- public int expires_in;

- public string authorizer_refresh_token;

- public string miniprograminfo;

- public string network;

- public List<WechatOpenFuncscopeCategoryItem> func_info;


## WechatOpenAuthorizationInfoFuncscopeCategory (class)

- public int id;


## WechatOpenAuthorizerAccountInfo (class)

- public string authorizer_appid;

- public string refresh_token;

- public int auth_time;


## WechatOpenAuthorizerInfo (class)

- public string nick_name;

- public string head_img;

- public WechatOpenServiceTypeInfo service_type_info;

- public WechatOpenVerifyTypeInfo verify_type_info;

- public string user_name;

- public string principal_name;

- public WechatOpenBusinessInfo business_info;

- public string qrcode_url;

- public string signature;

- public string alias;

- public WechatOpenMiniProgramInfo MiniProgramInfo;

- public WechatOpenStoreInfo store_info;

- public WechatOpenTalentInfo talent_info;

- public WechatOpenSupplierInfo supplier_info;


## WechatOpenAuthorizerListResult (class)

- public int total_count;

- public List<WechatOpenAuthorizerAccountInfo> list;


## WechatOpenAuthorizerOptionInfoJsonResult (class)

- public string option_name;

- public string option_value;


## WechatOpenAuthorizerOptionResult (class)

- public string authorizer_appid;

- public string option_name;

- public string option_value;


## WechatOpenBaseInfoModel (class)

- public int type;

- public string name;

- public string province;

- public string city;

- public string district;

- public string address;

- public string comment;


## WechatOpenBindOpenAccountJsonResult (class)

- public bool have_open;


## WechatOpenBizInfo (class)

- public string nickname;

- public string appid;

- public string headimg;


## WechatOpenBusinessInfo (class)

- public int open_pay;

- public int open_shake;

- public int open_card;

- public int open_store;

- public int open_scan;


## WechatOpenCategoriesByTypeData (class)

- public List<WechatOpenCategoryByTypeItem> categories;


## WechatOpenCategoriesByTypeJsonResult (class)

- public WechatOpenCategoriesByTypeData categories_list;


## WechatOpenCategoriesList (class)

- public List<WechatOpenWxOpenAPIsCategoryListJsonCategoryListJsonResultCategory> categories;


## WechatOpenCategoryByTypeItem (class)

- public int id;

- public string name;

- public int level;

- public int father;

- public List<WechatOpenCategoryByTypeItem> children;

- public int sensitive_type;

- public JsonValue qualify;


## WechatOpenCategoryListJsonResult (class)

- public WechatOpenCategoriesList categories_list;


## WechatOpenCategroyInfo (class)

- public string first_class;

- public string second_class;

- public string third_class;

- public int first_id;

- public int second_id;

- public int third_id;


## WechatOpenCheckWxVerifyNickNameJsonResult (class)

- public bool hit_condition;

- public string wording;


## WechatOpenCityModel (class)

- public List<WechatOpenDistrictCodeItem> sub_list;

- public int type;

- public string code;

- public string name;


## WechatOpenCodePrivacyInfoJsonResult (class)

- public List<string> without_auth_list;

- public List<string> without_conf_list;


## WechatOpenCodeResultJson (class)

- public List<WechatOpenVersionList> version_list;


## WechatOpenComponentAccessTokenResult (class)

- public string component_access_token;

- public int expires_in;


## WechatOpenContact (class)

- public string consignor_contact;

- public string receiver_contact;


## WechatOpenCreateIcpVerifyTaskResultJson (class)

- public string task_id;


## WechatOpenDistrictCodeItem (class)

- public int type;

- public string code;

- public string name;


## WechatOpenDraftInfo (class)

- public string create_time;

- public string user_version;

- public string user_desc;

- public int draft_id;


## WechatOpenExpInfo (class)

- public int exp_time;

- public string exp_version;

- public string exp_desc;


## WechatOpenExter (class)

- public List<WechatOpenInner> inner_list;


## WechatOpenFastRegisterBetaWeAppResult (class)

- public string unique_id;

- public string authorize_url;


## WechatOpenFastRegisterEnterpriseWeAppResult (class)

- public string taskid;

- public string authorize_url;

- public int status;


## WechatOpenFastRegisterJsonResult (class)

- public string appid;

- public string authorization_code;

- public bool is_wx_verify_succ;

- public bool is_link_succ;


## WechatOpenFastRegisterPersonalWeAppResult (class)

- public string taskid;

- public string authorize_url;

- public int status;


## WechatOpenFetchDataSettingJsonResult (class)

- public bool is_pre_fetch_open;

- public int pre_fetch_type;

- public string pre_fetch_url;

- public string pre_env;

- public string pre_function_name;

- public bool is_period_fetch_open;

- public int period_fetch_type;

- public string period_fetch_url;

- public string period_env;

- public string period_function_name;


## WechatOpenFuncInfos (class)

- public int status;

- public int id;

- public string name;


## WechatOpenFuncscopeCategoryItem (class)

- public WechatOpenAuthorizationInfoFuncscopeCategory funcscope_category;


## WechatOpenGetAppealRecordsJsonResult (class)

- public List<WechatOpenMaterialInfo> records;


## WechatOpenGetAuditResultJson (class)

- public string auditid;

- public int status;

- public string reason;

- public string screenshot;


## WechatOpenGetAuthorizerInfoResult (class)

- public WechatOpenAuthorizerInfo authorizer_info;

- public WechatOpenAuthorizationInfo authorization_info;


## WechatOpenGetCategoryJsonResulttData (class)

- public int id;

- public string name;


## WechatOpenGetCategoryResultJson (class)

- public List<WechatOpenCategroyInfo> category_list;


## WechatOpenGetDomainConfirmFileResult (class)

- public string file_name;

- public string file_content;


## WechatOpenGetEffectiveDomainResultDomain (class)

- public List<string> requestdomain;

- public List<string> wsrequestdomain;

- public List<string> uploaddomain;

- public List<string> downloaddomain;

- public List<string> udpdomain;

- public List<string> tcpdomain;


## WechatOpenGetEffectiveDomainResultJson (class)

- public WechatOpenGetEffectiveDomainResultDomain mp_domain;

- public WechatOpenGetEffectiveDomainResultDomain third_domain;

- public WechatOpenGetEffectiveDomainResultDomain direct_domain;

- public WechatOpenGetEffectiveDomainResultDomain effective_domain;


## WechatOpenGetEffectiveWebViewDomainResultJson (class)

- public List<string> mp_webviewdomain;

- public List<string> third_webviewdomain;

- public List<string> direct_webviewdomain;

- public List<string> effective_webviewdomain;


## WechatOpenGetGrayReleasePlanResultJson (class)

- public WechatOpenGrayReleasePlan gray_release_plan;

- public List<WechatOpenVersionList> version_list;


## WechatOpenGetIcpEntranceInfoModel (class)

- public int status;

- public bool is_canceling;

- public List<WechatOpenAuditDataModel> audit_data;

- public int available;

- public int sms_verify_status;


## WechatOpenGetIcpEntranceInfoResultJson (class)

- public WechatOpenGetIcpEntranceInfoModel info;


## WechatOpenGetIllegalRecordsJsonResult (class)

- public JsonValue Value;


## WechatOpenGetListJsonResult (class)

- public List<WechatOpenWxaEmbeddedInfo> wxa_embedded_list;

- public int embedded_flag;


## WechatOpenGetOnlineIcpOrderResultJson (class)

- public WechatOpenIcpSubjectModel icp_subject;

- public List<WechatOpenIcpAppletsModel> icp_applets;


## WechatOpenGetOrderJsonResult (class)

- public WechatOpenOrder order;


## WechatOpenGetOrderListJsonResult (class)

- public string last_index;

- public bool has_more;

- public List<WechatOpenOrder> order_list;


## WechatOpenGetOrderPathInfoJsonResult (class)

- public WechatOpenGetOrderPathInfoMsg msg;


## WechatOpenGetOrderPathInfoMsg (class)

- public string path;

- public List<string> img_list;

- public string video;

- public string test_account;

- public string test_pwd;

- public string test_remark;

- public int status;

- public long apply_time;


## WechatOpenGetPageResultJson (class)

- public List<string> page_list;


## WechatOpenGetPrefetchDNSDomainData (class)

- public string url;

- public int status;


## WechatOpenGetPrefetchDNSDomainResultJson (class)

- public List<WechatOpenGetPrefetchDNSDomainData> prefetch_dns_domain;

- public int size_limit;


## WechatOpenGetPrivacyInterfaceJsonResult (class)

- public List<WechatOpenPrivacyInterfaceInfo> interface_list;


## WechatOpenGetPrivacySettingDataOwnerSetting (class)

- public string contact_email;

- public string contact_phone;

- public string contact_qq;

- public string contact_weixin;

- public string ext_file_media_id;

- public string notice_method;

- public string store_expire_timestamp;


## WechatOpenGetPrivacySettingDataPrivacyDesc (class)

- public List<WechatOpenGetPrivacySettingDataPrivacyDescList> privacy_desc_list;


## WechatOpenGetPrivacySettingDataPrivacyDescList (class)

- public string privacy_desc;

- public string privacy_key;


## WechatOpenGetPrivacySettingDataSettingList (class)

- public string privacy_key;

- public string privacy_text;

- public string privacy_label;


## WechatOpenGetPrivacySettingResult (class)

- public int code_exist;

- public List<string> privacy_list;

- public List<WechatOpenGetPrivacySettingDataSettingList> setting_list;

- public long update_time;

- public WechatOpenGetPrivacySettingDataOwnerSetting owner_setting;

- public WechatOpenGetPrivacySettingDataPrivacyDesc privacy_desc;


## WechatOpenGetPubTemplateKeyWordsByIdJsonResult (class)

- public string count;

- public List<WechatOpenGetPubTemplateKeyWordsByIdJsonResultData> data;


## WechatOpenGetPubTemplateKeyWordsByIdJsonResultData (class)

- public int kid;

- public string name;

- public string example;

- public string rule;


## WechatOpenGetPubTemplateTitlesJsonResult (class)

- public List<WechatOpenGetPubTemplateTitlesJsonResultData> data;

- public int count;


## WechatOpenGetPubTemplateTitlesJsonResultData (class)

- public string tid;

- public string title;

- public int type;

- public string categoryId;


## WechatOpenGetShowWxaItemJsonResult (class)

- public int can_open;

- public int is_open;

- public string appid;

- public string nickname;

- public string headimg;


## WechatOpenGetTemplateDraftListResultJson (class)

- public List<WechatOpenDraftInfo> draft_list;


## WechatOpenGetTemplateListJsonResult (class)

- public List<WechatOpenGetTemplateListJsonResultData> data;


## WechatOpenGetTemplateListJsonResultData (class)

- public string priTmplId;

- public string title;

- public string content;

- public string example;

- public int type;


## WechatOpenGetTemplateListResultJson (class)

- public List<WechatOpenTemplateInfo> template_list;


## WechatOpenGetVersionInfoJsonResult (class)

- public WechatOpenExpInfo exp_info;

- public WechatOpenReleaseInfo release_info;


## WechatOpenGetWeappSupportVersionResultJson (class)

- public string now_version;

- public WechatOpenUvInfo uv_info;

- public List<WechatOpenVersionList> version_list;


## WechatOpenGetWebViewDomainConfirmFileResultJson (class)

- public string file_name;

- public string file_content;


## WechatOpenGetWxaMpLinkForShowJsonRsult (class)

- public int total_num;

- public List<WechatOpenBizInfo> biz_info_list;


## WechatOpenGetWxaSearchStatusJsonResult (class)

- public int status;


## WechatOpenGrayReleasePlan (class)

- public int status;

- public long create_timestamp;

- public int gray_percentage;


## WechatOpenHeadImageInfo (class)

- public string head_image_url;

- public int modify_used_count;

- public int modify_quota;


## WechatOpenHintsModel (class)

- public int errcode;

- public string err_field;

- public string errmsg;


## WechatOpenIcpAppletsBaseInfoModel (class)

- public string appid;

- public string name;

- public List<int> service_content_types;

- public List<WechatOpenNrlxDetailModel> nrlx_details;

- public string comment;


## WechatOpenIcpAppletsModel (class)

- public WechatOpenIcpAppletsBaseInfoModel base_info;

- public WechatOpenPrincipalInfoModel principal_info;


## WechatOpenIcpMaterialsModel (class)

- public List<string> commitment_letter;

- public List<string> business_name_change_letter;

- public List<string> party_building_confirmation_letter;

- public List<string> promise_video;

- public List<string> authenticity_responsibility_letter;

- public List<string> authenticity_commitment_letter;

- public List<string> website_construction_proposal;

- public List<string> subject_other_materials;

- public List<string> applets_other_materials;

- public List<string> holding_certificate_photo;


## WechatOpenIcpSubjectModel (class)

- public WechatOpenBaseInfoModel base_info;

- public WechatOpenPersonalInfoModel personal_info;

- public WechatOpenOrganizeInfoModel organize_info;

- public WechatOpenPrincipalInfoModel principal_info;

- public WechatOpenLegalPersonInfoModel legal_person_info;


## WechatOpenIllegalMaterial (class)

- public string content;

- public string content_url;


## WechatOpenInner (class)

- public string name;

- public string url;


## WechatOpenIsTradeManagedJsonResult (class)

- public bool is_trade_managed;


## WechatOpenIsTradeManagementConfirmationCompletedJsonResult (class)

- public bool completed;


## WechatOpenItem (class)

- public int status;

- public string username;

- public string appid;

- public string source;

- public string nickname;

- public int selected;

- public int nearby_display_status;

- public int released;

- public string headimg_url;

- public List<WechatOpenFuncInfos> func_infos;

- public int copy_verify_status;

- public string email;


## WechatOpenJsApiTicketResult (class)

- public string ticket;

- public int expires_in;


## WechatOpenJsCode2JsonResult (class)

- public string openid;

- public string session_key;

- public string unionid;


## WechatOpenLegalPersonInfoModel (class)

- public string name;

- public string certificate_number;


## WechatOpenMaterial (class)

- public WechatOpenIllegalMaterial illegal_material;

- public WechatOpenAppealMaterial appeal_material;


## WechatOpenMaterialInfo (class)

- public string appeal_record_id;

- public string appeal_time;

- public int appeal_count;

- public int appeal_from;

- public int appeal_status;

- public string audit_time;

- public string audit_reason;

- public string punish_description;

- public List<WechatOpenMaterial> materials;


## WechatOpenMiniProgramInfo (class)

- public WechatOpenMiniProgramInfoNetwork network;

- public List<WechatOpenMiniProgramInfoCategories> categories;

- public int visit_status;


## WechatOpenMiniProgramInfoCategories (class)

- public string first;

- public string second;


## WechatOpenMiniProgramInfoNetwork (class)

- public List<string> RequestDomain;

- public List<string> WsRequestDomain;

- public List<string> UploadDomain;

- public List<string> DownloadDomain;


## WechatOpenModifyDomainDirectlyResultJson (class)

- public List<string> requestdomain;

- public List<string> wsrequestdomain;

- public List<string> uploaddomain;

- public List<string> downloaddomain;

- public List<string> udpdomain;

- public List<string> tcpdomain;


## WechatOpenModifyWxaJumpDomainResult (class)

- public string published_wxa_jump_h5_domain;

- public string testing_wxa_jump_h5_domain;

- public string invalid_wxa_jump_h5_domain;


## WechatOpenModifyWxaServerDomainResult (class)

- public string published_wxa_server_domain;

- public string testing_wxa_server_domain;

- public string invalid_wxa_server_domain;


## WechatOpenMpAPIsOpenJsonCreateJsonResult (class)

- public string open_appid;


## WechatOpenMpAPIsOpenJsonGetJsonResult (class)

- public string open_appid;


## WechatOpenNicknameInfo (class)

- public string nickname;

- public int modify_used_count;

- public int modify_quota;


## WechatOpenNrlxDetailModel (class)

- public int type;

- public string code;

- public string media;


## WechatOpenNrlxItemModel (class)

- public int type;

- public string name;


## WechatOpenOAuthAccessTokenResult (class)

- public string access_token;

- public int expires_in;

- public string refresh_token;

- public string openid;

- public string scope;

- public string unionid;

- public int is_snapshotuser;


## WechatOpenOAuthUserInfo (class)

- public string openid;

- public string nickname;

- public int sex;

- public string province;

- public string city;

- public string country;

- public string headimgurl;

- public List<string> privilege;

- public string unionid;


## WechatOpenOrder (class)

- public string transaction_id;

- public string merchant_id;

- public string sub_merchant_id;

- public string merchant_trade_no;

- public string description;

- public int paid_amount;

- public string openid;

- public long trade_create_time;

- public long pay_time;

- public int order_state;

- public bool in_complaint;

- public WechatOpenOrderShipping shipping;


## WechatOpenOrderKey (class)

- public string order_number_type;

- public string transaction_id;

- public string mchid;

- public string out_trade_no;


## WechatOpenOrderShipping (class)

- public int delivery_mode;

- public int logistics_type;

- public bool finish_shipping;

- public string goods_desc;

- public int finish_shipping_count;

- public List<WechatOpenOrderShippingList> shipping_list;


## WechatOpenOrderShippingList (class)

- public string goods_desc;

- public long upload_time;

- public string tracking_no;

- public string express_company;

- public string item_desc;

- public WechatOpenContact contact;


## WechatOpenOrganizeInfoModel (class)

- public int certificate_type;

- public string certificate_number;

- public string certificate_address;

- public string certificate_photo;


## WechatOpenPayer (class)

- public string openid;


## WechatOpenPersonalInfoModel (class)

- public string residence_permit;


## WechatOpenPreAuthCodeResult (class)

- public string pre_auth_code;

- public int expires_in;


## WechatOpenPrincipalInfoModel (class)

- public string name;

- public string mobile;

- public string email;

- public string emergency_contact;

- public int certificate_type;

- public string certificate_number;

- public string certificate_validity_date_start;

- public string certificate_validity_date_end;

- public string certificate_photo_front;

- public string certificate_photo_back;

- public string authorization_letter;

- public string verify_task_id;


## WechatOpenPrivacyInterfaceInfo (class)

- public string api_name;

- public string api_ch_name;

- public string api_desc;

- public long apply_time;

- public int status;

- public long audit_id;

- public string fail_reason;

- public string api_link;

- public string group_name;


## WechatOpenProvinceModel (class)

- public List<WechatOpenCityModel> sub_list;

- public int type;

- public string code;

- public string name;


## WechatOpenQRConnectAccessTokenResult (class)

- public string unionid;

- public string access_token;

- public int expires_in;

- public string refresh_token;

- public string openid;

- public string scope;


## WechatOpenQRConnectUserInfo (class)

- public string openid;

- public string nickname;

- public int sex;

- public string province;

- public string city;

- public string country;

- public string headimgurl;

- public List<string> privilege;

- public string unionid;


## WechatOpenQrCodeJumpGetJsonResult (class)

- public List<WechatOpenQrCodeJumpRule> rule_list;

- public int qrcodejump_open;

- public int list_size;

- public int qrcodejump_pub_quota;

- public int total_count;


## WechatOpenQrCodeJumpRule (class)

- public string prefix;

- public string path;

- public int state;

- public int open_version;

- public List<string> debug_url;


## WechatOpenQualify (class)

- public List<WechatOpenExter> exter_list;


## WechatOpenQueryAuthAndIcpJsonResult (class)

- public int procedure_status;

- public JsonValue orderid;

- public string refill_reason;

- public string fail_reason;

- public JsonValue icp_audit;


## WechatOpenQueryAuthJsonResult (class)

- public int task_status;

- public string auth_url;

- public int apply_status;

- public string orderid;

- public string appid;

- public string refill_reason;

- public string fail_reason;


## WechatOpenQueryAuthResult (class)

- public WechatOpenAuthorizationInfo authorization_info;


## WechatOpenQueryIcpCertificateTypesItemModel (class)

- public int type;

- public int subject_type;

- public string name;

- public string remark;


## WechatOpenQueryIcpCertificateTypesResultJson (class)

- public List<WechatOpenQueryIcpCertificateTypesItemModel> items;


## WechatOpenQueryIcpDistrictCodeResultJson (class)

- public List<WechatOpenProvinceModel> items;


## WechatOpenQueryIcpNrlxTypesResultJson (class)

- public List<WechatOpenNrlxItemModel> items;


## WechatOpenQueryIcpServiceCoententTypesItemModel (class)

- public int type;

- public int parent_type;

- public string name;

- public string remark;


## WechatOpenQueryIcpServiceContentTypesResultJson (class)

- public List<WechatOpenQueryIcpServiceCoententTypesItemModel> items;


## WechatOpenQueryIcpSubjectTypesResultJson (class)

- public List<WechatOpenSubjectTypesItemModel> items;


## WechatOpenQueryIcpVerifyTaskResultJson (class)

- public bool is_finish;

- public int face_status;


## WechatOpenQueryNickNameJsonResult (class)

- public string nickname;

- public int audit_stat;

- public string fail_reason;

- public long create_time;

- public long audit_time;


## WechatOpenQueryQuotaResultJson (class)

- public int rest;

- public int limit;

- public int speedup_rest;

- public int speedup_limit;


## WechatOpenReauthJsonResult (class)

- public string taskid;

- public string auth_url;


## WechatOpenRefreshAccessTokenResult (class)

- public string access_token;

- public int expires_in;

- public string refresh_token;

- public string openid;

- public string scope;


## WechatOpenRefreshAuthorizerTokenResult (class)

- public string authorizer_access_token;

- public int expires_in;

- public string authorizer_refresh_token;


## WechatOpenReleaseInfo (class)

- public int release_time;

- public string release_version;

- public string release_desc;


## WechatOpenSameEntityJsonResult (class)

- public bool same_entity;


## WechatOpenServiceTypeInfo (class)

- public int id;


## WechatOpenSetNickNameJsonResult (class)

- public string wording;

- public int audit_id;


## WechatOpenSetPrefetchDNSDomainData (class)

- public string url;


## WechatOpenSetPrefetchDNSDomainResultJson (class)

- public JsonValue Value;


## WechatOpenSetPrivacySettingDataSettingList (class)

- public string privacy_key;

- public string privacy_text;


## WechatOpenSetWebViewDomainDirectlyResultJson (class)

- public List<string> webviewdomain;


## WechatOpenSetWebViewDomainJsonResult (class)

- public List<string> webviewdomain;


## WechatOpenSetWebViewDomainResultJson (class)

- public List<string> webviewdomain;


## WechatOpenShipping (class)

- public string tracking_no;

- public string express_company;

- public string item_desc;

- public WechatOpenContact contact;


## WechatOpenSignatureInfo (class)

- public string signature;

- public int modify_used_count;

- public int modify_quota;


## WechatOpenStoreInfo (class)

- public int id;


## WechatOpenSubjectTypesItemModel (class)

- public int type;

- public string name;

- public string remark;


## WechatOpenSubmitAuditPageInfo (class)

- public string address;

- public string tag;

- public string first_class;

- public string second_class;

- public string third_class;

- public string title;

- public int first_id;

- public int second_id;

- public int third_id;


## WechatOpenSubmitAuthAndIcpJsonResult (class)

- public List<string> hints;

- public string procedure_id;

- public string pay_url;


## WechatOpenSupplierInfo (class)

- public int id;


## WechatOpenTalentInfo (class)

- public int id;


## WechatOpenTemplateInfo (class)

- public string create_time;

- public string user_version;

- public string user_desc;

- public int template_id;


## WechatOpenUploadCombinedShippingInfoSubOrder (class)

- public WechatOpenOrderKey order_key;

- public int logistics_type;

- public int delivery_mode;

- public bool is_all_delivered;

- public List<WechatOpenShipping> shipping_list;


## WechatOpenUvInfo (class)

- public List<WechatOpenUvItemInfo> items;


## WechatOpenUvItemInfo (class)

- public int percentage;

- public string version;


## WechatOpenVerifyTypeInfo (class)

- public int id;


## WechatOpenVersionList (class)

- public long commit_time;

- public string user_version;

- public string user_desc;

- public int app_version;


## WechatOpenVisitStatusJsonResult (class)

- public int status;


## WechatOpenWxOpenAPIsCategoryListJsonCategoryListJsonResultCategory (class)

- public int id;

- public string name;

- public int level;

- public int father;

- public List<int> children;

- public int sensitive_type;

- public WechatOpenQualify qualify;


## WechatOpenWxOpenAPIsGetCategoryJsonGetCategoryJsonResult (class)

- public int limit;

- public int quota;

- public int category_limit;

- public List<WechatOpenWxOpenAPIsGetCategoryJsonGetCategoryJsonResultCategory> categories;


## WechatOpenWxOpenAPIsGetCategoryJsonGetCategoryJsonResultCategory (class)

- public int first;

- public string first_name;

- public int second;

- public string second_name;

- public int audit_status;

- public string audit_reason;


## WechatOpenWxVerifyInfo (class)

- public bool qualification_verify;

- public bool naming_verify;

- public bool annual_review;

- public long annual_review_begin_time;

- public long annual_review_end_time;


## WechatOpenWxaAPIsDomainDomainJsonModifyDomainResultJson (class)

- public List<string> requestdomain;

- public List<string> wsrequestdomain;

- public List<string> uploaddomain;

- public List<string> downloaddomain;

- public List<string> udpdomain;

- public List<string> tcpdomain;


## WechatOpenWxaAPIsModifyDomainModifyDomainJsonModifyDomainResultJson (class)

- public List<string> requestdomain;

- public List<string> wsrequestdomain;

- public List<string> uploaddomain;

- public List<string> downloaddomain;


## WechatOpenWxaAPIsNewTmplNewTmplJsonGetCategoryJsonResult (class)

- public List<WechatOpenGetCategoryJsonResulttData> data;


## WechatOpenWxaAuthAuthDataContactInfo (class)

- public string name;

- public string email;

- public string mobile;


## WechatOpenWxaAuthAuthDataInvoiceInfo (class)

- public int invoice_type;

- public WechatOpenWxaAuthAuthDataInvoiceInfoElectronic electronic;

- public WechatOpenWxaAuthAuthDataInvoiceInfoVat vat;

- public string invoice_title;


## WechatOpenWxaAuthAuthDataInvoiceInfoElectronic (class)

- public string id;

- public string desc;


## WechatOpenWxaAuthAuthDataInvoiceInfoVat (class)

- public string enterprise_phone;

- public string id;

- public string enterprise_address;

- public string bank_name;

- public string bank_account;

- public string mailing_address;

- public string address;

- public string name;

- public string phone;

- public string province;

- public string city;

- public string district;

- public string desc;


## WechatOpenWxaAuthJsonResult (class)

- public string taskid;

- public string auth_url;


## WechatOpenWxaEmbeddedInfo (class)

- public string appid;

- public string create_time;

- public string headimg;

- public string name;

- public string nickname;

- public string reason;

- public int status;


## WechatOpenWxaMpLinkGetJsonResult (class)

- public WechatOpenWxopens wxopens;


## WechatOpenWxopens (class)

- public List<WechatOpenItem> items;
