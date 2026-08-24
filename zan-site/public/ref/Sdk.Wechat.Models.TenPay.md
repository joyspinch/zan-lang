# Sdk.Wechat.Models.TenPay

> 源码: `stdlib/Sdk/Wechat/Models/TenPay/WechatPayModels.zan`


## WechatPayAccountInfo (class)

从随附的 C# SDK 生成的共享 DTO 实体。
一个 C# 命名空间/类型标识精确对应一个 Zan 模型。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- public string bank_account_type;

- public string account_name;

- public string account_bank;

- public string bank_address_code;

- public string bank_branch_id;

- public string account_number;


## WechatPayAccountVerificationInfo (class)

- public int account_validation_amount;

- public string account_name;

- public string destination_account_remark;

- public string destination_account_deadline;


## WechatPayAddPaygiftActivityMerchantsReturnJson (class)

- public string activity_id;

- public List<WechatPayInvalidMerchantIdList> invalid_merchant_id_list;

- public string add_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayAddProfitsharingReceiverReturnJson (class)

- public string sub_mchid;

- public string brand_mchid;

- public string type;

- public string account;

- public string name;

- public string relation_type;

- public string custom_relation;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayAppReturnJson (class)

- public string prepay_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayApply4SubAccountInfo (class)

- public string bank_account_type;

- public string account_name;

- public string account_bank;

- public string bank_address_code;

- public string bank_branch_id;

- public string account_number;


## WechatPayApply4SubAccountVerificationInfo (class)

- public int account_validation_amount;

- public string account_name;

- public string destination_account_remark;

- public string destination_account_deadline;


## WechatPayApply4SubApplymentReturnJson (class)

- public string applyment_id;

- public string out_request_no;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayApply4SubAuditDetail (class)

- public string field;

- public string field_name;

- public string reject_reason;


## WechatPayApply4SubBusinessLicenseInfo (class)

- public string license_copy;

- public string license_number;

- public string merchant_name;

- public string legal_person;


## WechatPayApply4SubContactInfo (class)

- public string contact_type;

- public string contact_name;

- public string contact_id_number;

- public string mobile_phone;

- public string contact_email;


## WechatPayApply4SubCurrentAdditionInfo (class)

- public string legal_person_commitment;

- public string legal_person_video;

- public List<string> business_addition_pics;

- public string business_addition_msg;


## WechatPayApply4SubCurrentAppInfo (class)

- public string app_appid;

- public string app_sub_appid;

- public List<string> app_pics;


## WechatPayApply4SubCurrentApplymentQueryResultJson (class)

- public string business_code;

- public long applyment_id;

- public string sub_mchid;

- public string sign_url;

- public string applyment_state;

- public string applyment_state_msg;

- public List<WechatPayApply4SubCurrentAuditDetail> audit_detail;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayApply4SubCurrentApplymentResultJson (class)

- public long applyment_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayApply4SubCurrentAuditDetail (class)

- public string field;

- public string field_name;

- public string reject_reason;


## WechatPayApply4SubCurrentBankAccountInfo (class)

- public string bank_account_type;

- public string account_name;

- public string account_bank;

- public string bank_address_code;

- public string bank_branch_id;

- public string bank_name;

- public string account_number;


## WechatPayApply4SubCurrentBizStoreInfo (class)

- public string biz_store_name;

- public string biz_address_code;

- public string biz_store_address;

- public List<string> store_entrance_pic;

- public List<string> indoor_pic;

- public string biz_sub_appid;


## WechatPayApply4SubCurrentBusinessInfo (class)

- public string merchant_shortname;

- public string service_phone;

- public WechatPayApply4SubCurrentSalesInfo sales_info;


## WechatPayApply4SubCurrentBusinessLicenseInfo (class)

- public string license_copy;

- public string license_number;

- public string merchant_name;

- public string legal_person;

- public string license_address;

- public string period_begin;

- public string period_end;


## WechatPayApply4SubCurrentCertificateInfo (class)

- public string cert_copy;

- public string cert_type;

- public string cert_number;

- public string merchant_name;

- public string company_address;

- public string legal_person;

- public string period_begin;

- public string period_end;

- public string certificate_letter_copy;


## WechatPayApply4SubCurrentContactInfo (class)

- public string contact_type;

- public string contact_name;

- public string contact_id_doc_type;

- public string contact_id_number;

- public string contact_id_doc_copy;

- public string contact_id_doc_copy_back;

- public string contact_period_begin;

- public string contact_period_end;

- public string business_authorization_letter;

- public string openid;

- public string mobile_phone;

- public string contact_email;


## WechatPayApply4SubCurrentFinanceInstitutionInfo (class)

- public string finance_type;

- public List<string> finance_license_pics;


## WechatPayApply4SubCurrentIdCardInfo (class)

- public string id_card_copy;

- public string id_card_national;

- public string id_card_name;

- public string id_card_number;

- public string id_card_address;

- public string card_period_begin;

- public string card_period_end;


## WechatPayApply4SubCurrentIdDocInfo (class)

- public string id_doc_copy;

- public string id_doc_copy_back;

- public string id_doc_name;

- public string id_doc_number;

- public string id_doc_address;

- public string doc_period_begin;

- public string doc_period_end;


## WechatPayApply4SubCurrentIdentityInfo (class)

- public string id_holder_type;

- public string id_doc_type;

- public string authorize_letter_copy;

- public WechatPayApply4SubCurrentIdCardInfo id_card_info;

- public WechatPayApply4SubCurrentIdDocInfo id_doc_info;

- public bool owner;


## WechatPayApply4SubCurrentMiniProgramInfo (class)

- public string mini_program_appid;

- public string mini_program_sub_appid;

- public List<string> mini_program_pics;


## WechatPayApply4SubCurrentMpInfo (class)

- public string mp_appid;

- public string mp_sub_appid;

- public List<string> mp_pics;


## WechatPayApply4SubCurrentSalesInfo (class)

- public List<string> sales_scenes_type;

- public WechatPayApply4SubCurrentBizStoreInfo biz_store_info;

- public WechatPayApply4SubCurrentMpInfo mp_info;

- public WechatPayApply4SubCurrentMiniProgramInfo mini_program_info;

- public WechatPayApply4SubCurrentAppInfo app_info;

- public WechatPayApply4SubCurrentWebInfo web_info;

- public WechatPayApply4SubCurrentWeworkInfo wework_info;


## WechatPayApply4SubCurrentSettlementInfo (class)

- public string settlement_id;

- public string qualification_type;

- public List<string> qualifications;

- public string activities_id;

- public string activities_rate;

- public List<string> activities_additions;

- public string debit_activities_rate;

- public string credit_activities_rate;


## WechatPayApply4SubCurrentSubjectInfo (class)

- public string subject_type;

- public bool finance_institution;

- public WechatPayApply4SubCurrentBusinessLicenseInfo business_license_info;

- public WechatPayApply4SubCurrentCertificateInfo certificate_info;

- public WechatPayApply4SubCurrentFinanceInstitutionInfo finance_institution_info;

- public WechatPayApply4SubCurrentIdentityInfo identity_info;

- public List<WechatPayApply4SubCurrentUboInfo> ubo_info_list;


## WechatPayApply4SubCurrentUboInfo (class)

- public string ubo_id_doc_type;

- public string ubo_id_doc_copy;

- public string ubo_id_doc_copy_back;

- public string ubo_id_doc_name;

- public string ubo_id_doc_number;

- public string ubo_id_doc_address;

- public string ubo_period_begin;

- public string ubo_period_end;


## WechatPayApply4SubCurrentWebInfo (class)

- public string domain;

- public string web_authorisation;

- public string web_appid;


## WechatPayApply4SubCurrentWeworkInfo (class)

- public string sub_corp_id;

- public List<string> wework_pics;


## WechatPayApply4SubIdDocInfo (class)

- public string id_doc_type;

- public string id_card_copy;

- public string id_card_national;

- public string id_card_name;

- public string id_card_number;

- public string card_period_begin;

- public string card_period_end;


## WechatPayApply4SubMediaUploadResultJson (class)

- public string media_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayApply4SubModifySettlementResultJson (class)

- public string application_no;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayApply4SubSalesSceneInfo (class)

- public string store_name;

- public string store_url;

- public string store_qr_code;

- public string mini_program_sub_appid;


## WechatPayApply4SubSettlementModificationResultJson (class)

- public string account_name;

- public string account_type;

- public string account_bank;

- public string bank_name;

- public string bank_branch_id;

- public string account_number;

- public string verify_result;

- public string verify_fail_reason;

- public string verify_finish_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayApply4SubSettlementResultJson (class)

- public string account_type;

- public string account_bank;

- public string bank_name;

- public string bank_branch_id;

- public string account_number;

- public string verify_result;

- public string verify_fail_reason;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayApply4SubUboInfo (class)

- public string id_type;

- public string id_card_copy;

- public string id_card_national;

- public string name;

- public string id_number;

- public string id_period_begin;

- public string id_period_end;


## WechatPayApply4SubjectApplicationAdditionInfo (class)

- public List<string> confirm_mchid_list;


## WechatPayApply4SubjectApplicationContactInfo (class)

- public string name;

- public string mobile;

- public string id_card_number;

- public string contact_type;

- public string contact_id_doc_type;

- public string contact_id_doc_copy;

- public string contact_id_doc_copy_back;

- public string contact_period_begin;

- public string contact_period_end;


## WechatPayApply4SubjectApplicationIdentificationInfo (class)

- public string id_holder_type;

- public string identification_type;

- public string identification_name;

- public string identification_number;

- public string identification_valid_date;

- public string identification_front_copy;

- public string identification_back_copy;

- public string authorize_letter_copy;

- public bool owner;

- public string identification_address;


## WechatPayApply4SubjectApplicationResultJson (class)

- public long applyment_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayApply4SubjectApplicationSubjectInfo (class)

- public string subject_type;

- public bool is_finance_institution;

- public WechatPayApply4SubjectBusinessLicenceInfo business_licence_info;

- public WechatPayApply4SubjectCertificateInfo certificate_info;

- public string company_prove_copy;

- public WechatPayApply4SubjectAssistProveInfo assist_prove_info;

- public List<WechatPayApply4SubjectSpecialOperationInfo> special_operation_list;

- public WechatPayApply4SubjectFinanceInstitutionInfo finance_institution_info;


## WechatPayApply4SubjectApplicationUboInfo (class)

- public string ubo_id_doc_type;

- public string ubo_id_doc_copy;

- public string ubo_id_doc_copy_back;

- public string ubo_id_doc_name;

- public string ubo_id_doc_number;

- public string ubo_id_doc_address;

- public string ubo_period_begin;

- public string ubo_period_end;


## WechatPayApply4SubjectApplymentReturnJson (class)

- public string applyment_id;

- public string out_request_no;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayApply4SubjectAssistProveInfo (class)

- public string micro_biz_type;

- public string store_name;

- public string store_address_code;

- public string store_address;

- public string store_header_copy;

- public string store_indoor_copy;


## WechatPayApply4SubjectAuditResultJson (class)

- public string applyment_state;

- public string qrcode_data;

- public string reject_param;

- public string reject_reason;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayApply4SubjectAuthorizationStateResultJson (class)

- public string authorize_state;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayApply4SubjectBusinessLicenceInfo (class)

- public string licence_number;

- public string licence_copy;

- public string merchant_name;

- public string legal_person;

- public string company_address;

- public string licence_valid_date;


## WechatPayApply4SubjectCertificateInfo (class)

- public string cert_type;

- public string cert_number;

- public string cert_copy;

- public string merchant_name;

- public string legal_person;

- public string company_address;

- public string cert_valid_date;


## WechatPayApply4SubjectContactInfo (class)

- public string contact_name;

- public string contact_id_number;

- public string mobile_phone;

- public string contact_email;


## WechatPayApply4SubjectFinanceInstitutionInfo (class)

- public string finance_type;

- public List<string> finance_license_pics;


## WechatPayApply4SubjectIdentityInfo (class)

- public string id_doc_type;

- public string id_card_copy;

- public string id_card_national;

- public string id_card_name;

- public string id_card_number;

- public string card_period_begin;

- public string card_period_end;


## WechatPayApply4SubjectInfo (class)

- public string subject_type;

- public string business_license_copy;

- public string business_license_number;

- public string merchant_name;

- public string company_address;


## WechatPayApply4SubjectMediaUploadResultJson (class)

- public string media_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayApply4SubjectSpecialOperationInfo (class)

- public int category_id;

- public List<string> operation_copy_list;


## WechatPayApplyElecsignReturnJson (class)

- public string state;

- public string create_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayAssignSmartGuideReturnJson (class)

- public string guide_id;

- public string out_trade_no;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayAssociateBusifavorReturnJson (class)

- public string wechatpay_associate_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayAuditDetail (class)

- public string field;

- public string field_name;

- public string reject_reason;


## WechatPayBankInfo (class)

- public string bank_alias_code;

- public string bank_alias;

- public bool need_branch;


## WechatPayBasePayCloseCombineOrderRequestDataSubOrder (class)

- public string mchid;

- public string out_trade_no;


## WechatPayBasePayCombineTransactionsRequestDataAmount (class)

- public int total_amount;

- public string currency;


## WechatPayBasePayCombineTransactionsRequestDataCombinePayerInfo (class)

- public string openid;


## WechatPayBasePayCombineTransactionsRequestDataH5Info (class)

- public string type;

- public string app_name;

- public string app_url;

- public string bundle_id;

- public string package_name;


## WechatPayBasePayCombineTransactionsRequestDataSceneInfo (class)

- public string device_id;

- public string payer_client_ip;

- public WechatPayBasePayCombineTransactionsRequestDataH5Info h5_info;


## WechatPayBasePayCombineTransactionsRequestDataSettleInfo (class)

- public bool profit_sharing;

- public long subsidy_amount;


## WechatPayBasePayCombineTransactionsRequestDataSubOrder (class)

- public string mchid;

- public string attach;

- public WechatPayBasePayCombineTransactionsRequestDataAmount amount;

- public string out_trade_no;

- public string goods_tag;

- public string description;

- public WechatPayBasePayCombineTransactionsRequestDataSettleInfo settle_info;


## WechatPayBasePayRefundRequsetDataAmount (class)

- public int refund;

- public List<WechatPayBasePayRefundRequsetDataFrom> from;

- public int total;

- public string currency;


## WechatPayBasePayRefundRequsetDataFrom (class)

- public string account;

- public string amount;


## WechatPayBasePayRefundRequsetDataGoodsDetail (class)

- public string merchant_goods_id;

- public string wechatpay_goods_id;

- public string goods_name;

- public int unit_price;

- public int refund_amount;

- public int refund_quantity;


## WechatPayBasePayReturnJsonCombineOrderReturnJsonAmount (class)

- public int total;

- public string currency;


## WechatPayBasePayReturnJsonCombineOrderReturnJsonCombinePayerInfo (class)

- public string openid;


## WechatPayBasePayReturnJsonCombineOrderReturnJsonSceneInfo (class)

- public string device_id;


## WechatPayBasePayReturnJsonCombineOrderReturnJsonSubOrder (class)

- public string mchid;

- public string trade_type;

- public string trade_state;

- public string bank_type;

- public string attach;

- public string success_time;

- public string transaction_id;

- public string out_trade_no;

- public WechatPayBasePayReturnJsonCombineOrderReturnJsonAmount amount;


## WechatPayBasePayReturnJsonOrderReturnJsonAmount (class)

- public int total;

- public int payer_total;

- public string currency;

- public string payer_currency;

- public WechatPayBasePayReturnJsonOrderReturnJsonSceneInfo scene_info;


## WechatPayBasePayReturnJsonOrderReturnJsonGoodsDetail (class)

- public string goods_id;

- public int quantity;

- public int unit_price;

- public int discount_amount;

- public string goods_remark;


## WechatPayBasePayReturnJsonOrderReturnJsonPayer (class)

- public string openid;

- public string sp_openid;

- public string sub_openid;


## WechatPayBasePayReturnJsonOrderReturnJsonPromotionDetail (class)

- public string coupon_id;

- public string name;

- public string scope;

- public string type;

- public int amount;

- public string stock_id;

- public string wechatpay_contribute;

- public string merchant_contribute;

- public string other_contribute;

- public string currency;

- public List<WechatPayBasePayReturnJsonOrderReturnJsonGoodsDetail> goods_detail;


## WechatPayBasePayReturnJsonOrderReturnJsonSceneInfo (class)

- public string device_id;


## WechatPayBasePayReturnJsonRefundReturnJsonAmount (class)

- public int total;

- public int refund;

- public List<WechatPayBasePayReturnJsonRefundReturnJsonFrom> from;

- public int payer_total;

- public int payer_refund;

- public int settlement_refund;

- public int settlement_total;

- public int discount_refund;

- public string currency;


## WechatPayBasePayReturnJsonRefundReturnJsonFrom (class)

- public int account;

- public int amount;


## WechatPayBasePayReturnJsonRefundReturnJsonGoodsDetail (class)

- public string merchant_goods_id;

- public string wechatpay_goods_id;

- public string goods_name;

- public int unit_price;

- public int refund_amount;

- public int refund_quantity;


## WechatPayBasePayReturnJsonRefundReturnJsonPromotionDetail (class)

- public string promotion_id;

- public string scope;

- public string type;

- public int amount;

- public int refund_amount;

- public List<WechatPayBasePayReturnJsonRefundReturnJsonGoodsDetail> goods_detail;


## WechatPayBasePayTransactionsRequestDataAmount (class)

- public int total;

- public string currency;


## WechatPayBasePayTransactionsRequestDataDetail (class)

- public string invoice_id;

- public List<WechatPayBasePayTransactionsRequestDataGoodsDetail> goods_detail;

- public int cost_price;


## WechatPayBasePayTransactionsRequestDataGoodsDetail (class)

- public string goods_name;

- public string wechatpay_goods_id;

- public int quantity;

- public string merchant_goods_id;

- public int unit_price;


## WechatPayBasePayTransactionsRequestDataH5Info (class)

- public string type;

- public string app_name;

- public string app_url;

- public string bundle_id;

- public string package_name;


## WechatPayBasePayTransactionsRequestDataPayer (class)

- public string openid;

- public string sp_openid;

- public string sub_openid;


## WechatPayBasePayTransactionsRequestDataSceneInfo (class)

- public WechatPayBasePayTransactionsRequestDataStoreInfo store_info;

- public string device_id;

- public string payer_client_ip;

- public WechatPayBasePayTransactionsRequestDataH5Info h5_info;


## WechatPayBasePayTransactionsRequestDataSettleInfo (class)

- public bool profit_sharing;


## WechatPayBasePayTransactionsRequestDataStoreInfo (class)

- public string address;

- public string area_code;

- public string name;

- public string id;


## WechatPayBatchesReturnJson (class)

- public string out_batch_no;

- public string batch_id;

- public string create_time;

- public string batch_status;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBillReturnJson (class)

- public string hash_type;

- public string hash_value;

- public string download_url;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBranchInfo (class)

- public string branch_code;

- public string branch_name;


## WechatPayBrandApplymentAdminInfo (class)

- public string admin_name;

- public string id_doc_type;

- public string id_card_number;


## WechatPayBrandApplymentBasicInfo (class)

- public string brand_name;

- public string brand_logo;


## WechatPayBrandApplymentQueryResultJson (class)

- public string applyment_id;

- public string business_code;

- public string applyment_state;

- public string applyment_state_desc;

- public string authorization_confirmation_qr_code;

- public string reject_reason;

- public string brand_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandApplymentResultJson (class)

- public string applyment_id;

- public string business_code;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandApplymentSubjectInfo (class)

- public string subject_type;

- public string subject_name;

- public string unified_social_credit_code;


## WechatPayBrandApplymentTrademarkCertificate (class)

- public string certificate;

- public string name;

- public string number;

- public string valid_begin_time;

- public string valid_end_time;

- public string international_class;

- public string holder;

- public string license;

- public string authorization_begin_time;

- public string authorization_end_time;

- public List<string> certificate_list;

- public List<string> license_list;


## WechatPayBrandApplymentTrademarkInfo (class)

- public string trademark_exists;

- public WechatPayBrandApplymentTrademarkCertificate trademark_registration_certificate;

- public WechatPayBrandApplymentTrademarkCertificate logo_trademark_registration_certificate;

- public string no_trademark_addition_prove;

- public List<string> no_trademark_addition_prove_list;


## WechatPayBrandCardActiveLinkInfo (class)

- public string payment_scene;

- public List<string> appid_list;

- public string card_link_mchid;

- public string service_id;


## WechatPayBrandCardActiveLinksResultJson (class)

- public string brand_id;

- public int total_num;

- public List<WechatPayBrandCardActiveLinkInfo> active_link_list;

- public int page_index;

- public int page_size;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandCardConfigApplymentResultJson (class)

- public string business_code;

- public string applyment_id;

- public string brand_id;

- public string applyment_state;

- public string scheduled_publish_time;

- public string reject_reason;

- public string actual_publish_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandCardConfigPreviewResultJson (class)

- public string business_code;

- public string applyment_id;

- public string brand_id;

- public string card_preview_url;

- public string url_expired_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandCardConfigPublishResultJson (class)

- public string business_code;

- public string applyment_id;

- public string brand_id;

- public string publish_type;

- public string scheduled_publish_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandCardConfigSubmitResultJson (class)

- public string business_code;

- public string applyment_id;

- public string brand_id;

- public string card_preview_url;

- public string url_expired_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandCardCustomerServiceInfo (class)

- public string customer_service_type;

- public string customer_service_phone;

- public string customer_service_path;

- public string appid;


## WechatPayBrandCardLinkApplymentResultJson (class)

- public string configuration_state;

- public string reject_reason;

- public string business_code;

- public string brand_id;

- public string payment_scene;

- public string appid;

- public string card_link_mchid;

- public string service_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandCardLinkCancelResultJson (class)

- public string business_code;

- public string brand_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandCardLinkResultJson (class)

- public string business_code;

- public string brand_id;

- public string payment_scene;

- public string appid;

- public string card_link_mchid;

- public string service_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandCardMiniProgramInfo (class)

- public string appid;

- public string default_jump_path;

- public string button_text;


## WechatPayBrandCardServiceInfo (class)

- public string service_classify_name;

- public string service_name;

- public string service_jump_type;

- public string service_jump_path;

- public string appid;


## WechatPayBrandMediaUploadResultJson (class)

- public string media_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandMemberCardCommonFieldValue (class)

- public string name;

- public string value_;


## WechatPayBrandMemberCardCustomField (class)

- public string type;

- public string name;

- public List<string> values;


## WechatPayBrandMemberCardCustomFieldValue (class)

- public string name;

- public List<string> user_chosen_values;


## WechatPayBrandMemberCardImageUploadResultJson (class)

- public string media_url;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandMemberCardJumpInformation (class)

- public string jump_appid;

- public string jump_path;


## WechatPayBrandMemberCardListResultJson (class)

- public List<WechatPayBrandMemberCardResultJson> data;

- public long total_count;

- public int offset;

- public int limit;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandMemberCardPointBalanceResultJson (class)

- public string out_request_no;

- public string brand_id;

- public string card_id;

- public string openid;

- public string user_card_code;

- public long point_balance;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandMemberCardPointExchangeResultJson (class)

- public string record_id;

- public string exchange_coupon_template_id;

- public string brand_id;

- public string card_id;

- public string openid;

- public string user_card_code;

- public string state;

- public string product_coupon_id;

- public string product_coupon_stock_type;

- public string stock_id;

- public string coupon_code;

- public string reject_reason;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandMemberCardPreAuthTokenResultJson (class)

- public string token;

- public string expire_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandMemberCardPurchaseInformation (class)

- public long price;

- public string jump_appid;

- public string jump_path;


## WechatPayBrandMemberCardResultJson (class)

- public string out_request_no;

- public string card_id;

- public string brand_id;

- public string appid;

- public string card_type;

- public string card_title;

- public string card_color;

- public string card_picture_url;

- public string code_mode;

- public string code_type;

- public WechatPayBrandMemberCardJumpInformation code_jump_information;

- public string benefits;

- public string notify_url;

- public bool need_pinned;

- public bool need_display_level;

- public string init_level;

- public string service_phone;

- public string legal_agreement;

- public WechatPayBrandMemberCardValidDateInformation valid_date_information;

- public WechatPayBrandMemberCardJumpInformation member_information;

- public WechatPayBrandMemberCardJumpInformation points_information;

- public WechatPayBrandMemberCardJumpInformation balance_information;

- public WechatPayBrandMemberCardPurchaseInformation purchase_information;

- public WechatPayBrandMemberCardUserInformation user_information;

- public string state;

- public string create_time;

- public string modify_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandMemberCardUserCardListResultJson (class)

- public List<WechatPayBrandMemberCardUserCardResultJson> data;

- public long total_count;

- public int offset;

- public int limit;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandMemberCardUserCardResultJson (class)

- public string user_card_code;

- public string card_id;

- public string openid;

- public string card_color;

- public string card_picture_url;

- public string brand_id;

- public string card_type;

- public string phone_number;

- public string level;

- public WechatPayBrandMemberCardValidDateInformation valid_date_information;

- public string pickup_time;

- public WechatPayBrandMemberCardUserProfileInformation user_information;

- public string attach;

- public string user_card_state;

- public string invalid_reason;

- public string invalid_time;

- public string create_time;

- public string modify_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandMemberCardUserFeedResultJson (class)

- public string brand_id;

- public string card_id;

- public string user_card_code;

- public string out_request_no;

- public string cell;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandMemberCardUserInformation (class)

- public List<string> common_field_list;

- public List<WechatPayBrandMemberCardCustomField> custom_field_list;


## WechatPayBrandMemberCardUserProfileInformation (class)

- public List<WechatPayBrandMemberCardCommonFieldValue> common_field_list;

- public List<WechatPayBrandMemberCardCustomFieldValue> custom_field_list;


## WechatPayBrandMemberCardValidDateInformation (class)

- public string type;

- public string available_begin_time;

- public string available_end_time;

- public int available_day_after_receive;


## WechatPayBrandStoreAddress (class)

- public string address_code;

- public string address_detail;

- public string address_complements;

- public string longitude;

- public string latitude;


## WechatPayBrandStoreBasics (class)

- public string store_reference_id;

- public string branch_name;


## WechatPayBrandStoreBindRecipientResultJson (class)

- public string store_id;

- public string mchid;

- public string company_name;

- public string recipient_state;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandStoreBusiness (class)

- public string service_phone;

- public string business_hours;


## WechatPayBrandStoreListResultJson (class)

- public List<WechatPayBrandStoreResultJson> data;

- public int offset;

- public int limit;

- public long total_count;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandStoreRecipient (class)

- public string mchid;

- public string company_name;

- public string recipient_state;


## WechatPayBrandStoreResultJson (class)

- public string store_id;

- public string store_state;

- public string audit_state;

- public string review_reject_reason;

- public WechatPayBrandStoreBasics store_basics;

- public WechatPayBrandStoreAddress store_address;

- public WechatPayBrandStoreBusiness store_business;

- public List<WechatPayBrandStoreRecipient> store_recipient;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandStoreStateResultJson (class)

- public string store_id;

- public string store_state;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBrandStoreUnbindRecipientResultJson (class)

- public string failed_reason;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBuildPartnershipsReturnJson (class)

- public WechatPayMarketingReturnJsonBuildPartnershipsReturnJsonPartner partner;

- public WechatPayMarketingReturnJsonBuildPartnershipsReturnJsonAuthorizedData authorized_data;

- public string state;

- public string build_time;

- public string create_time;

- public string update_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayBusinessLicenseInfo (class)

- public string license_copy;

- public string license_number;

- public string merchant_name;

- public string legal_person;


## WechatPayBuyerInfo (class)

- public string buyer_type;

- public string buyer_name;

- public string buyer_taxpayer_num;

- public string buyer_address;

- public string buyer_phone;


## WechatPayCancelApply4SubjectApplymentReturnJson (class)

- public string applyment_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCancelServiceOrderReturnJson (class)

- public string appid;

- public string mchid;

- public string out_order_no;

- public string service_id;

- public string order_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCancelTransferReturnJson (class)

- public string out_bill_no;

- public string transfer_bill_no;

- public string state;

- public string update_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCertificatesResultJson (class)

- public List<WechatPayDatum> data;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayChainBrandProfitsharingAmountsResultJson (class)

- public string transaction_id;

- public long unsplit_amount;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayChainBrandProfitsharingBillResultJson (class)

- public string hash_type;

- public string hash_value;

- public string download_url;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayChainBrandProfitsharingBrandConfigResultJson (class)

- public string brand_mchid;

- public long max_ratio;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayChainBrandProfitsharingFinishOrderResultJson (class)

- public string sub_mchid;

- public string transaction_id;

- public string out_order_no;

- public string order_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayChainBrandProfitsharingOrderResultJson (class)

- public string brand_mchid;

- public string sub_mchid;

- public string transaction_id;

- public string out_order_no;

- public string order_id;

- public string status;

- public List<WechatPayChainBrandProfitsharingReceiverResult> receivers;

- public long finish_amount;

- public string finish_description;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayChainBrandProfitsharingReceiverRequestData (class)

- public string type;

- public string account;

- public long amount;

- public string description;

- public string name;


## WechatPayChainBrandProfitsharingReceiverResult (class)

- public string type;

- public string account;

- public long amount;

- public string description;

- public string result;

- public string finish_time;

- public string fail_reason;

- public string detail_id;


## WechatPayChainBrandProfitsharingReceiverResultJson (class)

- public string brand_mchid;

- public string type;

- public string account;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayChainBrandProfitsharingReturnOrderResultJson (class)

- public string sub_mchid;

- public string order_id;

- public string out_order_no;

- public string out_return_no;

- public string return_mchid;

- public long amount;

- public string return_no;

- public string result;

- public string fail_reason;

- public string finish_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCheckFapiaoStatusReturnJson (class)

- public string sub_mchid;

- public string fapiao_status;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCityInfo (class)

- public string city_code;

- public string city_name;


## WechatPayCloseCombineTransactionsReturnJson (class)

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCombineH5Info (class)

- public string type;

- public string app_name;

- public string app_url;

- public string bundle_id;

- public string package_name;


## WechatPayCombineOrderReturnJson (class)

- public string combine_appid;

- public string combine_mchid;

- public string combine_out_trade_no;

- public WechatPayBasePayReturnJsonCombineOrderReturnJsonSceneInfo scene_info;

- public List<WechatPayBasePayReturnJsonCombineOrderReturnJsonSubOrder> sub_orders;

- public WechatPayBasePayReturnJsonCombineOrderReturnJsonCombinePayerInfo combine_payer_info;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCombinePayer (class)

- public string openid;


## WechatPayCombineSettleInfo (class)

- public bool profit_sharing;

- public int subsidy_amount;


## WechatPayCombineSubOrder (class)

- public string mchid;

- public string attach;

- public int amount;

- public string out_trade_no;

- public string description;

- public WechatPayCombineSettleInfo settle_info;


## WechatPayCombineSubOrderInfo (class)

- public string mchid;

- public string out_trade_no;


## WechatPayCombineTransactionsReturnJson (class)

- public string prepay_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayComplaintReturnJsonQueryComplaintReturnJsonComplaintMediaList (class)

- public string media_type;

- public List<string> media_url;


## WechatPayComplaintReturnJsonQueryComplaintReturnJsonComplaintOrderInfo (class)

- public string transaction_id;

- public string out_trade_no;

- public int amount;


## WechatPayComplaintReturnJsonQueryComplaintsReturnJsonComplaintMediaList (class)

- public string media_type;

- public List<string> media_url;


## WechatPayComplaintReturnJsonQueryComplaintsReturnJsonComplaintOrderInfo (class)

- public string transaction_id;

- public string out_trade_no;

- public int amount;


## WechatPayComplaintReturnJsonQueryComplaintsReturnJsonData (class)

- public string complaint_id;

- public string complaint_time;

- public string complaint_detail;

- public string complaint_state;

- public string payer_phone;

- public List<WechatPayComplaintReturnJsonQueryComplaintsReturnJsonComplaintMediaList> complaint_media_list;

- public List<WechatPayComplaintReturnJsonQueryComplaintsReturnJsonComplaintOrderInfo> complaint_order_info;

- public bool complaint_full_refunded;

- public string problem_description;

- public bool incoming_user_response;

- public int user_complaint_times;


## WechatPayComplaintReturnJsonQueryNegotiationHistorysReturnJsonComplaintMediaList (class)

- public string media_type;

- public List<string> media_url;


## WechatPayComplaintReturnJsonQueryNegotiationHistorysReturnJsonData (class)

- public WechatPayComplaintReturnJsonQueryNegotiationHistorysReturnJsonComplaintMediaList complaint_media_list;

- public string log_id;

- public string operate_time;

- public string operate_type;

- public string operate_details;

- public List<string> image_list;


## WechatPayComplaintReturnJsonUploadImageReturnJson (class)

- public string media_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCompleteServiceOrderReturnJson (class)

- public string appid;

- public string mchid;

- public string out_order_no;

- public string service_id;

- public string service_introduction;

- public string state;

- public string state_description;

- public long total_amount;

- public List<WechatPayPayScoreReturnJsonCompleteServiceOrderReturnJsonPostPayments> post_payments;

- public List<WechatPayPayScoreReturnJsonCompleteServiceOrderReturnJsonPostDiscounts> post_discounts;

- public WechatPayPayScoreReturnJsonCompleteServiceOrderReturnJsonRiskFund risk_fund;

- public WechatPayPayScoreReturnJsonCompleteServiceOrderReturnJsonTimeRange time_range;

- public WechatPayPayScoreReturnJsonCompleteServiceOrderReturnJsonLocation location;

- public string order_id;

- public bool need_collection;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayContactInfo (class)

- public string contact_type;

- public string contact_name;

- public string contact_id_number;

- public string mobile_phone;

- public string contact_email;


## WechatPayCouponCodeCount (class)

- public long total_count;

- public long available_count;


## WechatPayCreateBusifavorStockReturnJson (class)

- public string stock_id;

- public string create_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCreateComplaintNotifyUrlReturnJson (class)

- public string mchid;

- public string url;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCreateDirectCompleteServiceOrderReturnJson (class)

- public string appid;

- public string mchid;

- public string out_order_no;

- public string service_id;

- public string order_id;

- public string service_introduction;

- public string state;

- public string state_description;

- public List<WechatPayPayScoreReturnJsonCreateDirectCompleteServiceOrderReturnJsonPostPayments> post_payments;

- public List<WechatPayPayScoreReturnJsonCreateDirectCompleteServiceOrderReturnJsonPostDiscounts> post_discounts;

- public WechatPayPayScoreReturnJsonCreateDirectCompleteServiceOrderReturnJsonTimeRange time_range;

- public WechatPayPayScoreReturnJsonCreateDirectCompleteServiceOrderReturnJsonLocation location;

- public string attach;

- public string notify_url;

- public long total_amount;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCreateFapiaoCardTemplateReturnJson (class)

- public string template_id;

- public string create_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCreateFapiaoReturnJson (class)

- public string fapiao_apply_id;

- public string status;

- public string create_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCreateMerchantRiskNotifyUrlReturnJson (class)

- public string notify_url;

- public string create_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCreateParkingReturnJson (class)

- public string id;

- public string out_parking_no;

- public string plate_number;

- public string plate_color;

- public string start_time;

- public string parking_name;

- public int free_duration;

- public string state;

- public string block_reason;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCreateProfitsharingReturnJson (class)

- public string brand_mchid;

- public string sub_mchid;

- public string transaction_id;

- public string out_order_no;

- public string order_id;

- public string state;

- public List<WechatPayProfitsharingReturnJsonCreateProfitsharingReturnJsonReceiver> receivers;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCreateServiceOrderReturnJson (class)

- public string appid;

- public string mchid;

- public string out_order_no;

- public string service_id;

- public string service_introduction;

- public string state;

- public string state_description;

- public List<WechatPayPayScoreReturnJsonCreateServiceOrderReturnJsonPostPayments> post_payments;

- public List<WechatPayPayScoreReturnJsonCreateServiceOrderReturnJsonPostDiscounts> post_discounts;

- public WechatPayPayScoreReturnJsonCreateServiceOrderReturnJsonRiskFund risk_fund;

- public WechatPayPayScoreReturnJsonCreateServiceOrderReturnJsonTimeRange time_range;

- public WechatPayPayScoreReturnJsonCreateServiceOrderReturnJsonLocation location;

- public string attach;

- public string notify_url;

- public string order_id;

- public string package;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCreateStockReturnJson (class)

- public string stock_id;

- public string create_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayCreateUniqueThresholdActivityReturnJson (class)

- public string activity_id;

- public string create_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayDatum (class)

- public string serial_no;

- public string effective_time;

- public string expire_time;

- public WechatPayEncryptCertificate encrypt_certificate;


## WechatPayDeactivateBusifavorCouponReturnJson (class)

- public string wechatpay_deactivate_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayDeleteMerchantRiskNotifyUrlReturnJson (class)

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayDeletePaygiftActivitiyMerchantsReturnJson (class)

- public string activity_id;

- public string delete_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayDeleteProfitsharingReceiverReturnJson (class)

- public string sub_mchid;

- public string brand_mchid;

- public string type;

- public string account;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayDeliveryPlanInfo (class)

- public string plan_id;

- public string plan_name;

- public string plan_state;

- public string delivery_start_time;

- public string delivery_end_time;

- public string product_coupon_id;

- public string usage_mode;

- public string stock_id;

- public string stock_bundle_id;

- public string recommend_word;

- public string brand_id;

- public long total_count;

- public long user_limit;

- public long daily_limit;

- public bool reuse_coupon_config;


## WechatPayDeliveryPlanListResultJson (class)

- public long total_count;

- public List<WechatPayDeliveryPlanInfo> plan_list;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayDeliveryPlanModifyContent (class)

- public string plan_name;

- public string delivery_end_time;

- public long total_count;

- public long user_limit;

- public long daily_limit;

- public string recommend_word;


## WechatPayDeliveryPlanNotifyUrlResultJson (class)

- public string notify_url;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayDeliveryPlanResultJson (class)

- public WechatPayDeliveryPlanInfo plan;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayDisassociateBusifavorReturnJson (class)

- public string wechatpay_disassociate_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayDistributeStockReturnJson (class)

- public string coupon_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayDownloadStockRefundFlowReturnJson (class)

- public string url;

- public string hash_value;

- public string hash_type;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayDownloadStockUseFlowReturnJson (class)

- public string url;

- public string hash_value;

- public string hash_type;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceCancelWithdrawBankAccountInfo (class)

- public string account_name;

- public string account_bank;

- public string bank_branch_id;

- public string bank_branch_name;

- public string account_number;


## WechatPayEcommerceCancelWithdrawIdentityInfo (class)

- public string id_doc_type;

- public string identification_name;

- public string identification_no;


## WechatPayEcommerceCancelWithdrawPayeeInfo (class)

- public string account_type;

- public WechatPayEcommerceCancelWithdrawBankAccountInfo bank_account_info;

- public WechatPayEcommerceCancelWithdrawIdentityInfo identity_info;


## WechatPayEcommerceCancelWithdrawProofMedia (class)

- public string proof_media_type;

- public string proof_media;


## WechatPayEcommerceCancelWithdrawQueryResultJson (class)

- public string applyment_id;

- public string out_request_no;

- public string cancel_state;

- public string cancel_state_description;

- public string withdraw;

- public string withdraw_state;

- public string withdraw_state_description;

- public List<WechatPayEcommerceCancellationAccountWithdrawResult> account_withdraw_result;

- public string modify_time;

- public string sub_mchid;

- public List<WechatPayEcommerceCancellationAccountInfo> account_info;

- public WechatPayEcommerceCancellationConfirmInfo confirm_cancel;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceCancellationAccountInfo (class)

- public string out_account_type;

- public int amount;


## WechatPayEcommerceCancellationAccountWithdrawResult (class)

- public string out_account_type;

- public string pay_state;

- public string state_description;


## WechatPayEcommerceCancellationApplyResultJson (class)

- public string applyment_id;

- public string out_request_no;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceCancellationBlockReason (class)

- public string type;

- public string description;


## WechatPayEcommerceCancellationConfirmInfo (class)

- public string confirm_cancel_url;


## WechatPayEcommerceCancellationMediaUploadResultJson (class)

- public string media_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceCancellationValidationResultJson (class)

- public string sub_mchid;

- public string merchant_state;

- public string validate_result;

- public List<WechatPayEcommerceCancellationAccountInfo> account_info;

- public List<WechatPayEcommerceCancellationBlockReason> block_reasons;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceEcommerceCombineRequestDataCombineSceneInfo (class)

- public string payer_client_ip;

- public string device_id;

- public WechatPayCombineH5Info h5_info;


## WechatPayEcommerceFundflowBillDownloadItem (class)

- public int bill_sequence;

- public string hash_type;

- public string hash_value;

- public string download_url;

- public string encrypt_key;

- public string nonce;


## WechatPayEcommerceFundflowBillResultJson (class)

- public int download_bill_count;

- public List<WechatPayEcommerceFundflowBillDownloadItem> download_bill_list;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceFundsToOverseaAvailableAmountResultJson (class)

- public string transaction_id;

- public long available_abroad_amount;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceFundsToOverseaBillResultJson (class)

- public string hash_type;

- public string hash_value;

- public string download_url;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceFundsToOverseaExpressInfo (class)

- public string courier_number;

- public string express_company_name;


## WechatPayEcommerceFundsToOverseaGoodsInfo (class)

- public string goods_name;

- public string goods_category;

- public long goods_unit_price;

- public long goods_quantity;


## WechatPayEcommerceFundsToOverseaOrderResultJson (class)

- public string out_order_id;

- public string sub_mchid;

- public string order_id;

- public string result;

- public string fail_reason;

- public long amount;

- public long foreign_amount;

- public string foreign_currency;

- public long rate;

- public string exchange_rate_time;

- public string estimate_exchange_rate_time;

- public long departure_amount;

- public long fee;

- public string charge_mchid;

- public string charge_account_type;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceFundsToOverseaPayeeInfo (class)

- public string payee_id;


## WechatPayEcommerceFundsToOverseaPresaleInfo (class)

- public string type;

- public long total_amount;

- public string deposit_transaction_id;

- public string balance_transaction_id;


## WechatPayEcommerceFundsToOverseaSellerInfo (class)

- public string oversea_business_name;

- public string oversea_shop_name;

- public string seller_id;


## WechatPayEcommerceLegacyCancelApplicationMaterial (class)

- public string application_type;

- public string application_media_id;


## WechatPayEcommerceLegacyCancelApplicationResultJson (class)

- public string out_apply_no;

- public string sub_mchid;

- public string reject_reason;

- public string cancel_state;

- public string update_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceLegacyCancelWithdrawAdditionalMaterials (class)

- public List<string> additional_media;


## WechatPayEcommerceLegacyCancelWithdrawBankAccountInfo (class)

- public string account_name;

- public string account_bank;

- public string bank_branch_id;

- public string bank_name;

- public string account_number;


## WechatPayEcommerceLegacyCancelWithdrawIdentityInfo (class)

- public string id_doc_type;

- public string identification_name;

- public string identification_no;


## WechatPayEcommerceLegacyCancelWithdrawPayeeInfo (class)

- public string account_type;

- public WechatPayEcommerceLegacyCancelWithdrawBankAccountInfo bank_account_info;

- public WechatPayEcommerceLegacyCancelWithdrawIdentityInfo identity_info;


## WechatPayEcommerceLegacyCancelWithdrawProofMedia (class)

- public string proof_media_type;

- public string proof_media;


## WechatPayEcommerceLegacyCancelWithdrawProofMediaList (class)

- public List<WechatPayEcommerceLegacyCancelWithdrawProofMedia> proof_payee_media;


## WechatPayEcommerceLegacyCancelWithdrawQueryResultJson (class)

- public WechatPayEcommerceLegacyCancelWithdrawStatus withdrawl_apply;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceLegacyCancelWithdrawStatus (class)

- public string applyment_id;

- public string out_request_no;

- public string state;

- public string fail_reason;

- public string modify_time;


## WechatPayEcommercePlatformBalanceResultJson (class)

- public int available_amount;

- public int pending_amount;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommercePlatformWithdrawalApplyResultJson (class)

- public string withdraw_id;

- public string out_request_no;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommercePlatformWithdrawalQueryResultJson (class)

- public string solution;

- public string status;

- public string withdraw_id;

- public string out_request_no;

- public int amount;

- public string create_time;

- public string update_time;

- public string reason;

- public string remark;

- public string bank_memo;

- public string account_type;

- public string account_number;

- public string account_bank;

- public string bank_name;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceRefundAdvanceReturnResultJson (class)

- public string refund_id;

- public string advance_return_id;

- public int return_amount;

- public string payer_mchid;

- public string payer_account;

- public string payee_mchid;

- public string payee_account;

- public string result;

- public string success_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceRefundFundsFrom (class)

- public string account;

- public int amount;


## WechatPayEcommerceRefundPromotionDetail (class)

- public string promotion_id;

- public string scope;

- public string type;

- public int amount;

- public int refund_amount;


## WechatPayEcommerceRefundRequestAmount (class)

- public int refund;

- public List<WechatPayEcommerceRefundFundsFrom> from;

- public int total;

- public string currency;


## WechatPayEcommerceRefundResultAmount (class)

- public int refund;

- public List<WechatPayEcommerceRefundFundsFrom> from;

- public int payer_refund;

- public int discount_refund;

- public string currency;

- public int advance;


## WechatPayEcommerceRefundResultJson (class)

- public string refund_id;

- public string out_refund_no;

- public string transaction_id;

- public string out_trade_no;

- public string channel;

- public string user_received_account;

- public string success_time;

- public string create_time;

- public string status;

- public WechatPayEcommerceRefundResultAmount amount;

- public List<WechatPayEcommerceRefundPromotionDetail> promotion_detail;

- public string refund_account;

- public string funds_account;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceSubMerchantBalanceResultJson (class)

- public string sub_mchid;

- public int available_amount;

- public int pending_amount;

- public string account_type;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceSubMerchantDayEndWithdrawalResultJson (class)

- public string sp_mchid;

- public string sub_mchid;

- public string status;

- public string withdraw_id;

- public string out_request_no;

- public int total_amount;

- public int success_amount;

- public int fail_amount;

- public int refund_amount;

- public string create_time;

- public string update_time;

- public string reason;

- public string remark;

- public string bank_memo;

- public string account_type;

- public string account_number;

- public string account_bank;

- public string bank_name;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceSubMerchantWithdrawalApplyResultJson (class)

- public string sub_mchid;

- public string withdraw_id;

- public string out_request_no;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceSubMerchantWithdrawalQueryResultJson (class)

- public string sp_mchid;

- public string sub_mchid;

- public string status;

- public string withdraw_id;

- public string out_request_no;

- public int amount;

- public string create_time;

- public string update_time;

- public string reason;

- public string remark;

- public string bank_memo;

- public string account_type;

- public string account_number;

- public string account_bank;

- public string bank_name;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEcommerceWithdrawalAbnormalBillResultJson (class)

- public string hash_type;

- public string hash_value;

- public string download_url;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayEncryptCertificate (class)

- public string algorithm;

- public string nonce;

- public string associated_data;

- public string ciphertext;


## WechatPayFailCodes (class)

- public string coupon_code;

- public string code;

- public string message;


## WechatPayFapiaoInfo (class)

- public string invoice_type;

- public string invoice_content;

- public int invoice_amount;

- public List<WechatPayInvoiceItem> items;


## WechatPayFinishProfitsharingReturnJson (class)

- public string sub_mchid;

- public string transaction_id;

- public string out_order_no;

- public string order_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayGetFapiaoFileReturnJson (class)

- public string fapiao_apply_id;

- public string download_url;

- public string file_hash;

- public int expires_in;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayGetMerchantInfoReturnJson (class)

- public string sub_mchid;

- public string merchant_name;

- public string taxpayer_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayGetTitleUrlReturnJson (class)

- public string title_url;

- public int expires_in;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayGetUserTitleReturnJson (class)

- public string title_type;

- public string title_name;

- public string taxpayer_id;

- public string address;

- public string phone;

- public string bank_name;

- public string bank_account;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayGivePermissionReturnJson (class)

- public string apply_permissions_token;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayGoldplanChangeCustomPageStatusReturnJson (class)

- public string sub_mchid;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayGoldplanChangeGoldplanStatusReturnJson (class)

- public string sub_mchid;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayGoldplanCloseAdvertisingShowReturnJson (class)

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayGoldplanOpenAdvertisingShowReturnJson (class)

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayGoldplanSetAdvertisingIndustryFilterReturnJson (class)

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayH5ReturnJson (class)

- public string h5_url;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayIdDocInfo (class)

- public string id_doc_type;

- public string id_card_copy;

- public string id_card_national;

- public string id_card_name;

- public string id_card_number;

- public string card_period_begin;

- public string card_period_end;


## WechatPayImmediateServiceMessage (class)

- public List<WechatPayImmediateServiceMessageBlock> blocks;

- public string sender_identity;

- public string custom_data;


## WechatPayImmediateServiceMessageBlock (class)

- public string type;

- public JsonValue text;

- public JsonValue image;

- public JsonValue link;

- public JsonValue faq_list;

- public JsonValue button;

- public JsonValue button_group;


## WechatPayInactiveMerchantVerificationResultJson (class)

- public string sub_mchid;

- public string verification_id;

- public string state;

- public string fail_reason;

- public string create_time;

- public string finish_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayInactiveMerchantVerificationSubmitResultJson (class)

- public string verification_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayInvalidMerchantIdList (class)

- public string mchid;

- public string invalid_reason;


## WechatPayInvoiceItem (class)

- public string item_name;

- public int item_quantity;

- public int item_price;

- public int item_total_amount;


## WechatPayJsApiReturnJson (class)

- public string prepay_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayLimitCard (class)

- public string name;

- public List<string> bin;


## WechatPayMarketingBuildPartnershipsRequestDataAuthorizedData (class)

- public string business_type;

- public string stock_id;


## WechatPayMarketingBuildPartnershipsRequestDataPartner (class)

- public string type;

- public string appid;

- public string merchant_id;


## WechatPayMarketingCreateBusifavorStockRequestDataAvailableDayTime (class)

- public int begin_time;

- public int end_time;


## WechatPayMarketingCreateBusifavorStockRequestDataAvailableWeek (class)

- public List<int> week_day;

- public List<WechatPayMarketingCreateBusifavorStockRequestDataAvailableDayTime> available_day_time;


## WechatPayMarketingCreateBusifavorStockRequestDataCouponAvailableTime (class)

- public string available_begin_time;

- public string available_end_time;

- public int available_day_after_receive;

- public WechatPayMarketingCreateBusifavorStockRequestDataAvailableWeek available_week;

- public List<WechatPayMarketingCreateBusifavorStockRequestDataIrregularyAvaliableTime> irregulary_avaliable_time;

- public int wait_days_after_receive;


## WechatPayMarketingCreateBusifavorStockRequestDataCouponUseRule (class)

- public WechatPayMarketingCreateBusifavorStockRequestDataCouponAvailableTime coupon_available_time;

- public WechatPayMarketingCreateBusifavorStockRequestDataFixedNormalCoupon fixed_normal_coupon;

- public WechatPayMarketingCreateBusifavorStockRequestDataDiscountCoupon discount_coupon;

- public WechatPayMarketingCreateBusifavorStockRequestDataExchangeCoupon exchange_coupon;

- public string use_method;

- public string mini_programs_appid;

- public string mini_programs_path;


## WechatPayMarketingCreateBusifavorStockRequestDataCustomEntrance (class)

- public WechatPayMarketingCreateBusifavorStockRequestDataMiniProgramsInfo mini_programs_info;

- public string appid;

- public string hall_id;

- public string store_id;

- public string code_display_mode;


## WechatPayMarketingCreateBusifavorStockRequestDataDiscountCoupon (class)

- public int discount_percent;

- public int transaction_minimum;


## WechatPayMarketingCreateBusifavorStockRequestDataDisplayPatternInfo (class)

- public string description;

- public string merchant_logo_url;

- public string merchant_name;

- public string background_color;

- public string coupon_image_url;


## WechatPayMarketingCreateBusifavorStockRequestDataExchangeCoupon (class)

- public int exchange_price;

- public int transaction_minimum;


## WechatPayMarketingCreateBusifavorStockRequestDataFixedNormalCoupon (class)

- public int discount_amount;

- public int transaction_minimum;


## WechatPayMarketingCreateBusifavorStockRequestDataIrregularyAvaliableTime (class)

- public string begin_time;

- public string end_time;


## WechatPayMarketingCreateBusifavorStockRequestDataMiniProgramsInfo (class)

- public string mini_programs_appid;

- public string mini_programs_path;

- public string entrance_words;

- public string guiding_words;


## WechatPayMarketingCreateBusifavorStockRequestDataNotifyConfig (class)

- public string notify_appid;


## WechatPayMarketingCreateBusifavorStockRequestDataStockSendRule (class)

- public int max_coupons;

- public int max_coupons_per_user;

- public int max_coupons_by_day;

- public bool natural_person_limit;

- public bool prevent_api_abuse;

- public bool transferable;

- public bool shareable;


## WechatPayMarketingCreateStockRequsetDataCouponUseRule (class)

- public WechatPayMarketingCreateStockRequsetDataFixedNormalCoupon fixed_normal_coupon;

- public List<string> goods_tag;

- public List<string> limit_pay;

- public WechatPayLimitCard limit_card;

- public List<string> trade_type;

- public bool combine_use;

- public List<string> available_items;

- public List<string> available_merchants;


## WechatPayMarketingCreateStockRequsetDataFixedNormalCoupon (class)

- public long coupon_amount;

- public long transaction_minimum;


## WechatPayMarketingCreateStockRequsetDataStockUseRule (class)

- public long max_coupons;

- public long max_amount;

- public long max_amount_by_day;

- public long max_coupons_per_user;

- public bool natural_person_limit;

- public bool prevent_api_abuse;


## WechatPayMarketingCreateUniqueThresholdActivityRequestDataActivityBaseInfo (class)

- public string activity_name;

- public string activity_second_title;

- public string merchant_logo_url;

- public string background_color;

- public string begin_time;

- public string end_time;

- public WechatPayMarketingCreateUniqueThresholdActivityRequestDataAvailablePeriods available_periods;

- public string out_request_no;

- public string delivery_purpose;

- public string mini_programs_appid;

- public string mini_programs_path;


## WechatPayMarketingCreateUniqueThresholdActivityRequestDataAdvancedSetting (class)

- public string delivery_user_category;

- public string merchant_member_appid;

- public List<string> goods_tags;


## WechatPayMarketingCreateUniqueThresholdActivityRequestDataAvailableDayTime (class)

- public string begin_day_time;

- public string end_day_time;


## WechatPayMarketingCreateUniqueThresholdActivityRequestDataAvailablePeriods (class)

- public List<WechatPayMarketingCreateUniqueThresholdActivityRequestDataAvailableTime> available_time;

- public List<WechatPayMarketingCreateUniqueThresholdActivityRequestDataAvailableDayTime> available_day_time;


## WechatPayMarketingCreateUniqueThresholdActivityRequestDataAvailableTime (class)

- public string begin_time;

- public string end_time;


## WechatPayMarketingCreateUniqueThresholdActivityRequestDataAwardList (class)

- public string stock_id;

- public string original_image_url;

- public string thumbnail_url;


## WechatPayMarketingCreateUniqueThresholdActivityRequestDataAwardSendRule (class)

- public int transaction_amount_minimum;

- public string send_content;

- public string award_type;

- public List<WechatPayMarketingCreateUniqueThresholdActivityRequestDataAwardList> award_list;

- public string merchant_option;

- public List<string> merchant_id_list;


## WechatPayMarketingModifyBusifavorStockInformationRequestDataCouponUseRule (class)

- public string use_method;

- public string mini_programs_appid;

- public string mini_programs_path;


## WechatPayMarketingModifyBusifavorStockInformationRequestDataCustomEntrance (class)

- public WechatPayMarketingModifyBusifavorStockInformationRequestDataMiniProgramsInfo mini_programs_info;

- public string appid;

- public string hall_id;

- public string code_display_mode;


## WechatPayMarketingModifyBusifavorStockInformationRequestDataDisplayPatternInfo (class)

- public string description;

- public string merchant_logo_url;

- public string merchant_name;

- public string background_color;

- public string coupon_image_url;


## WechatPayMarketingModifyBusifavorStockInformationRequestDataMiniProgramsInfo (class)

- public string mini_programs_appid;

- public string mini_programs_path;

- public string entrance_words;

- public string guiding_words;


## WechatPayMarketingModifyBusifavorStockInformationRequestDataNotifyConfig (class)

- public string notify_appid;


## WechatPayMarketingModifyBusifavorStockInformationRequestDataStockSendRule (class)

- public bool prevent_api_abuse;


## WechatPayMarketingQueryPartnershipsRequestDataAuthorizedData (class)

- public string business_type;

- public string stock_id;


## WechatPayMarketingQueryPartnershipsRequestDataPartner (class)

- public string type;

- public string appid;

- public string merchant_id;


## WechatPayMarketingReturnJsonBuildPartnershipsReturnJsonAuthorizedData (class)

- public string business_type;

- public List<string> scenarios;

- public string stock_id;


## WechatPayMarketingReturnJsonBuildPartnershipsReturnJsonPartner (class)

- public string type;

- public string appid;

- public string merchant_id;


## WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonAvailableDayTime (class)

- public int begin_time;

- public int end_time;


## WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonAvailableWeek (class)

- public List<int> week_day;

- public List<WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonAvailableDayTime> available_day_time;


## WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonCouponAvailableTime (class)

- public string available_begin_time;

- public string available_end_time;

- public int available_day_after_receive;

- public WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonAvailableWeek available_week;

- public List<WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonIrregularyAvaliableTime> irregulary_avaliable_time;

- public int wait_days_after_receive;


## WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonCouponUseRule (class)

- public WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonCouponAvailableTime coupon_available_time;

- public WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonFixedNormalCoupon fixed_normal_coupon;

- public WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonDiscountCoupon discount_coupon;

- public WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonExchangeCoupon exchange_coupon;

- public string use_method;

- public string mini_programs_appid;

- public string mini_programs_path;


## WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonCustomEntrance (class)

- public WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonMiniProgramsInfo mini_programs_info;

- public string appid;

- public string hall_id;

- public string store_id;

- public string code_display_mode;


## WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonDiscountCoupon (class)

- public int discount_percent;

- public long transaction_minimum;


## WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonDisplayPatternInfo (class)

- public string description;

- public string merchant_logo_url;

- public string merchant_name;

- public string background_color;

- public string coupon_image_url;


## WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonExchangeCoupon (class)

- public long exchange_price;

- public long transaction_minimum;


## WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonFixedNormalCoupon (class)

- public long discount_amount;

- public long transaction_minimum;


## WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonIrregularyAvaliableTime (class)

- public string begin_time;

- public string end_time;


## WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonMiniProgramsInfo (class)

- public string mini_programs_appid;

- public string mini_programs_path;

- public string entrance_words;

- public string guiding_words;


## WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonAvailableDayTime (class)

- public int begin_time;

- public int end_time;


## WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonAvailableWeek (class)

- public List<int> week_day;

- public List<WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonAvailableDayTime> available_day_time;


## WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonCouponAvailableTime (class)

- public string available_begin_time;

- public string available_end_time;

- public int available_day_after_receive;

- public WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonAvailableWeek available_week;

- public List<WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonIrregularyAvaliableTime> irregulary_avaliable_time;

- public int wait_days_after_receive;


## WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonCouponUseRule (class)

- public WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonCouponAvailableTime coupon_available_time;

- public WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonFixedNormalCoupon fixed_normal_coupon;

- public WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonDiscountCoupon discount_coupon;

- public WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonExchangeCoupon exchange_coupon;

- public string use_method;

- public string mini_programs_appid;

- public string mini_programs_path;


## WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonCustomEntrance (class)

- public WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonMiniProgramsInfo mini_programs_info;

- public string appid;

- public string hall_id;

- public string store_id;

- public string code_display_mode;


## WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonData (class)

- public string belong_merchant;

- public string stock_name;

- public string comment;

- public string goods_name;

- public string stock_type;

- public bool transferable;

- public bool shareable;

- public string coupon_state;

- public WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonDisplayPatternInfo display_pattern_info;

- public WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonCouponUseRule coupon_use_rule;

- public WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonCustomEntrance custom_entrance;

- public string coupon_code;

- public string stock_id;

- public string available_start_time;

- public string expire_time;

- public string receive_time;

- public string send_request_no;

- public string use_request_no;

- public string use_time;


## WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonDiscountCoupon (class)

- public int discount_percent;

- public long transaction_minimum;


## WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonDisplayPatternInfo (class)

- public string merchant_logo_url;

- public string merchant_name;

- public string background_color;

- public string coupon_image_url;


## WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonExchangeCoupon (class)

- public long exchange_price;

- public long transaction_minimum;


## WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonFixedNormalCoupon (class)

- public long discount_amount;

- public long transaction_minimum;


## WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonIrregularyAvaliableTime (class)

- public string begin_time;

- public string end_time;


## WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonMiniProgramsInfo (class)

- public string mini_programs_appid;

- public string mini_programs_path;

- public string entrance_words;

- public string guiding_words;


## WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonAvailableDayTime (class)

- public int begin_time;

- public int end_time;


## WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonAvailableWeek (class)

- public List<int> week_day;

- public List<WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonAvailableDayTime> available_day_time;


## WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonCouponAvailableTime (class)

- public string available_begin_time;

- public string available_end_time;

- public int available_day_after_receive;

- public WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonAvailableWeek available_week;

- public List<WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonIrregularyAvaliableTime> irregulary_avaliable_time;

- public int wait_days_after_receive;


## WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonCouponUseRule (class)

- public WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonCouponAvailableTime coupon_available_time;

- public WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonFixedNormalCoupon fixed_normal_coupon;

- public WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonDiscountCoupon discount_coupon;

- public WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonExchangeCoupon exchange_coupon;

- public string use_method;

- public string mini_programs_appid;

- public string mini_programs_path;


## WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonCustomEntrance (class)

- public WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonMiniProgramsInfo mini_programs_info;

- public string appid;

- public string hall_id;

- public string store_id;

- public string code_display_mode;


## WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonDiscountCoupon (class)

- public int discount_percent;

- public long transaction_minimum;


## WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonDisplayPatternInfo (class)

- public string description;

- public string merchant_logo_url;

- public string merchant_name;

- public string background_color;

- public string coupon_image_url;


## WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonExchangeCoupon (class)

- public long exchange_price;

- public long transaction_minimum;


## WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonFixedNormalCoupon (class)

- public long discount_amount;

- public long transaction_minimum;


## WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonIrregularyAvaliableTime (class)

- public string begin_time;

- public string end_time;


## WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonMiniProgramsInfo (class)

- public string mini_programs_appid;

- public string mini_programs_path;

- public string entrance_words;

- public string guiding_words;


## WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonNotifyConfig (class)

- public string notify_appid;


## WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonStockSendRule (class)

- public long max_amount;

- public int max_coupons;

- public int max_coupons_per_user;

- public int max_amount_by_day;

- public int max_coupons_by_day;

- public bool natural_person_limit;

- public bool prevent_api_abuse;

- public bool transferable;

- public bool shareable;


## WechatPayMarketingReturnJsonQueryCouponReturnJson (class)

- public string stock_creator_mchid;

- public string stock_id;

- public string coupon_id;

- public WechatPayMarketingReturnJsonQueryCouponReturnJsonCutToMessage cut_to_message;

- public string coupon_name;

- public string status;

- public string description;

- public string create_time;

- public string coupon_type;

- public bool no_cash;

- public string available_begin_time;

- public string available_end_time;

- public bool singleitem;

- public WechatPayMarketingReturnJsonQueryCouponReturnJsonNormalCouponInformation normal_coupon_information;

- public WechatPayMarketingReturnJsonQueryCouponReturnJsonConsumeInformation consume_information;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayMarketingReturnJsonQueryCouponReturnJsonConsumeInformation (class)

- public string consume_time;

- public string consume_mchid;

- public string transaction_id;

- public List<WechatPayMarketingReturnJsonQueryCouponReturnJsonGoodsDetail> goods_detail;


## WechatPayMarketingReturnJsonQueryCouponReturnJsonCutToMessage (class)

- public long single_price_max;

- public long cut_to_price;


## WechatPayMarketingReturnJsonQueryCouponReturnJsonGoodsDetail (class)

- public string goods_id;

- public long quantity;

- public long price;

- public long discount_amount;


## WechatPayMarketingReturnJsonQueryCouponReturnJsonNormalCouponInformation (class)

- public long coupon_amount;

- public long transaction_minimum;


## WechatPayMarketingReturnJsonQueryCouponsReturnJsonConsumeInformation (class)

- public string consume_time;

- public string consume_mchid;

- public string transaction_id;

- public List<WechatPayMarketingReturnJsonQueryCouponsReturnJsonGoodsDetail> goods_detail;


## WechatPayMarketingReturnJsonQueryCouponsReturnJsonCutToMessage (class)

- public long single_price_max;

- public long cut_to_price;


## WechatPayMarketingReturnJsonQueryCouponsReturnJsonGoodsDetail (class)

- public string goods_id;

- public long quantity;

- public long price;

- public long discount_amount;


## WechatPayMarketingReturnJsonQueryCouponsReturnJsonNormalCouponInformation (class)

- public long coupon_amount;

- public long transaction_minimum;


## WechatPayMarketingReturnJsonQueryCouponsReturnJsonQueryCouponReturnJson (class)

- public string stock_creator_mchid;

- public string stock_id;

- public string coupon_id;

- public WechatPayMarketingReturnJsonQueryCouponsReturnJsonCutToMessage cut_to_message;

- public string coupon_name;

- public string status;

- public string description;

- public string create_time;

- public string coupon_type;

- public bool no_cash;

- public string available_begin_time;

- public string available_end_time;

- public bool singleitem;

- public WechatPayMarketingReturnJsonQueryCouponsReturnJsonNormalCouponInformation normal_coupon_information;

- public WechatPayMarketingReturnJsonQueryCouponsReturnJsonConsumeInformation consume_information;


## WechatPayMarketingReturnJsonQueryPartnershipsReturnJsonAuthorizedData (class)

- public string business_type;

- public string stock_id;


## WechatPayMarketingReturnJsonQueryPartnershipsReturnJsonData (class)

- public WechatPayMarketingReturnJsonQueryPartnershipsReturnJsonPartner partner;

- public WechatPayMarketingReturnJsonQueryPartnershipsReturnJsonAuthorizedData authorized_data;

- public string build_time;

- public string terminate_time;

- public string create_time;

- public string update_time;


## WechatPayMarketingReturnJsonQueryPartnershipsReturnJsonPartner (class)

- public string type;

- public string appid;

- public string merchant_id;


## WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonActivityBaseInfo (class)

- public string activity_name;

- public string activity_second_title;

- public string merchant_logo_url;

- public string background_color;

- public string begin_time;

- public string end_time;

- public WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonAvailablePeriods available_periods;

- public string out_request_no;

- public string delivery_purpose;

- public string mini_programs_appid;

- public string mini_programs_path;


## WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonAdvancedSetting (class)

- public string delivery_user_category;

- public string merchant_member_appid;

- public List<string> goods_tags;


## WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonAvailableDayTime (class)

- public string begin_day_time;

- public string end_day_time;


## WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonAvailablePeriods (class)

- public List<WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonAvailableTime> available_time;

- public List<WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonAvailableDayTime> available_day_time;


## WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonAvailableTime (class)

- public string begin_time;

- public string end_time;


## WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonAwardList (class)

- public string stock_id;

- public string original_image_url;

- public string thumbnail_url;


## WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonAwardSendRule (class)

- public WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonFullSendRule full_send_rule;


## WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonData (class)

- public string activity_id;

- public string activity_type;

- public WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonActivityBaseInfo activity_base_info;

- public WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonAwardSendRule award_send_rule;

- public WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonAdvancedSetting advanced_setting;

- public string activity_status;

- public string creator_merchant_id;

- public string belong_merchant_id;

- public string create_time;

- public string update_time;


## WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonFullSendRule (class)

- public int transaction_amount_minimum;

- public string send_content;

- public string award_type;

- public List<WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonAwardList> award_list;

- public string merchant_option;

- public List<string> merchant_id_list;


## WechatPayMarketingReturnJsonQueryPaygiftActivityGoodsReturnJsonData (class)

- public string goods_id;

- public string create_time;

- public string update_time;


## WechatPayMarketingReturnJsonQueryPaygiftActivityMerchantsReturnJsonData (class)

- public string mchid;

- public string merchant_name;

- public string create_time;

- public string update_time;


## WechatPayMarketingReturnJsonQueryPaygiftActivityReturnJsonActivityBaseInfo (class)

- public string activity_name;

- public string activity_second_title;

- public string merchant_logo_url;

- public string background_color;

- public string begin_time;

- public string end_time;

- public WechatPayMarketingReturnJsonQueryPaygiftActivityReturnJsonAvailablePeriods available_periods;

- public string out_request_no;

- public string delivery_purpose;

- public string mini_programs_appid;

- public string mini_programs_path;


## WechatPayMarketingReturnJsonQueryPaygiftActivityReturnJsonAdvancedSetting (class)

- public string delivery_user_category;

- public string merchant_member_appid;

- public List<string> goods_tags;


## WechatPayMarketingReturnJsonQueryPaygiftActivityReturnJsonAvailableDayTime (class)

- public string begin_day_time;

- public string end_day_time;


## WechatPayMarketingReturnJsonQueryPaygiftActivityReturnJsonAvailablePeriods (class)

- public List<WechatPayMarketingReturnJsonQueryPaygiftActivityReturnJsonAvailableTime> available_time;

- public List<WechatPayMarketingReturnJsonQueryPaygiftActivityReturnJsonAvailableDayTime> available_day_time;


## WechatPayMarketingReturnJsonQueryPaygiftActivityReturnJsonAvailableTime (class)

- public string begin_time;

- public string end_time;


## WechatPayMarketingReturnJsonQueryPaygiftActivityReturnJsonAwardList (class)

- public string stock_id;

- public string original_image_url;

- public string thumbnail_url;


## WechatPayMarketingReturnJsonQueryPaygiftActivityReturnJsonAwardSendRule (class)

- public WechatPayMarketingReturnJsonQueryPaygiftActivityReturnJsonFullSendRule full_send_rule;


## WechatPayMarketingReturnJsonQueryPaygiftActivityReturnJsonFullSendRule (class)

- public int transaction_amount_minimum;

- public string send_content;

- public string award_type;

- public List<WechatPayMarketingReturnJsonQueryPaygiftActivityReturnJsonAwardList> award_list;

- public string merchant_option;


## WechatPayMarketingReturnJsonStockReturnJsonCutToMessage (class)

- public long single_price_max;

- public long cut_to_price;


## WechatPayMarketingReturnJsonStockReturnJsonFixedNormalCoupon (class)

- public long coupon_amount;

- public long transaction_minimum;


## WechatPayMarketingReturnJsonStockReturnJsonStockUseRule (class)

- public long max_coupons;

- public long max_amount;

- public long max_amount_by_day;

- public WechatPayMarketingReturnJsonStockReturnJsonFixedNormalCoupon fixed_normal_coupon;

- public long max_coupons_per_user;

- public string coupon_type;

- public List<string> goods_tag;

- public List<string> trade_type;

- public bool combine_use;


## WechatPayMarketingReturnJsonUploadImageReturnJson (class)

- public string media_url;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayMarketingTerminatePartnershipsRequestDataAuthorizedData (class)

- public string business_type;

- public string stock_id;


## WechatPayMarketingTerminatePartnershipsRequestDataPartner (class)

- public string type;

- public string appid;

- public string merchant_id;


## WechatPayMedicalInsuranceCashAddDetail (class)

- public long cash_add_fee;

- public string cash_add_type;


## WechatPayMedicalInsuranceCashReduceDetail (class)

- public long cash_reduce_fee;

- public string cash_reduce_type;


## WechatPayMedicalInsuranceIdentity (class)

- public string name;

- public string id_digest;

- public string card_type;


## WechatPayMedicalInsuranceOrderResultJson (class)

- public string mix_trade_no;

- public string mix_pay_status;

- public string self_pay_status;

- public string med_ins_pay_status;

- public string paid_time;

- public string passthrough_response_content;

- public string mix_pay_type;

- public string order_type;

- public string appid;

- public string sub_appid;

- public string sub_mchid;

- public string openid;

- public string sub_openid;

- public bool pay_for_relatives;

- public string out_trade_no;

- public string serial_no;

- public string pay_order_id;

- public string pay_auth_no;

- public string geo_location;

- public string city_id;

- public string med_inst_name;

- public string med_inst_no;

- public string med_ins_order_create_time;

- public long total_fee;

- public long med_ins_gov_fee;

- public long med_ins_self_fee;

- public long med_ins_other_fee;

- public long med_ins_cash_fee;

- public long wechat_pay_cash_fee;

- public List<WechatPayMedicalInsuranceCashAddDetail> cash_add_detail;

- public List<WechatPayMedicalInsuranceCashReduceDetail> cash_reduce_detail;

- public string callback_url;

- public string prepay_id;

- public string passthrough_request_content;

- public string extends;

- public string attach;

- public string channel_no;

- public bool med_ins_test_env;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayMerchantLimitationResultJson (class)

- public string mchid;

- public List<string> limited_functions;

- public string other_limited_functions;

- public List<WechatPayMerchantRecoverySpecification> recovery_specifications;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayMerchantRecoverySpecification (class)

- public string limitation_case_id;

- public string limitation_reason_type;

- public string limitation_reason;

- public string limitation_reason_describe;

- public List<string> relate_limitations;

- public string other_relate_limitations;

- public string recover_way;

- public string recover_way_param;

- public string recover_help_url;

- public string limitation_action_type;

- public string limitation_start_date;

- public string limitation_date;


## WechatPayMeta (class)

- public string filename;

- public string sha256;


## WechatPayModifyApply4SubSettlementReturnJson (class)

- public string sub_mchid;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayModifyBusifavorStockBudgetReturnJson (class)

- public int max_coupons;

- public int max_coupons_by_day;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayModifyComplaintNotifyUrlReturnJson (class)

- public string mchid;

- public string url;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayModifyServiceOrderReturnJson (class)

- public string appid;

- public string mchid;

- public string service_id;

- public string out_order_no;

- public string service_introduction;

- public string state;

- public string state_description;

- public long total_amount;

- public List<WechatPayPayScoreReturnJsonModifyServiceOrderReturnJsonPostPayments> post_payments;

- public List<WechatPayPayScoreReturnJsonModifyServiceOrderReturnJsonPostDiscounts> post_discounts;

- public WechatPayPayScoreReturnJsonModifyServiceOrderReturnJsonRiskFund risk_fund;

- public WechatPayPayScoreReturnJsonModifyServiceOrderReturnJsonTimeRange time_range;

- public WechatPayPayScoreReturnJsonModifyServiceOrderReturnJsonLocation location;

- public string attach;

- public string notify_url;

- public string order_id;

- public bool need_collection;

- public WechatPayPayScoreReturnJsonModifyServiceOrderReturnJsonCollection collection;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayNativeReturnJson (class)

- public string code_url;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayOrderInfo (class)

- public string ret_code;

- public string ret_msg;

- public string input_charset;

- public string trade_state;

- public string trade_mode;

- public string partner;

- public string bank_type;

- public string bank_billno;

- public string total_fee;

- public string fee_type;

- public string transaction_id;

- public string out_trade_no;

- public string is_split;

- public string is_refund;

- public string attach;

- public string time_end;

- public string transport_fee;

- public string product_fee;

- public string discount;

- public string rmb_total_fee;


## WechatPayOrderReturnJson (class)

- public string appid;

- public string mchid;

- public string out_trade_no;

- public string transaction_id;

- public string trade_type;

- public string trade_state;

- public string trade_state_desc;

- public string bank_type;

- public string attach;

- public string success_time;

- public WechatPayBasePayReturnJsonOrderReturnJsonPayer payer;

- public WechatPayBasePayReturnJsonOrderReturnJsonAmount amount;

- public WechatPayBasePayReturnJsonOrderReturnJsonSceneInfo scene_info;

- public List<WechatPayBasePayReturnJsonOrderReturnJsonPromotionDetail> promotion_detail;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayOrderqueryResult (class)

- public int errcode;

- public string errmsg;

- public WechatPayOrderInfo orderInfo;


## WechatPayParkingChargingRule (class)

- public string rule_type;

- public string fee_limit_type;

- public string daily_limit_type;

- public string time_rounding_type;

- public int free_entry_duration;

- public int free_exit_duration;

- public List<WechatPayParkingFixedIntervalRule> fixed_interval_rule;

- public List<WechatPayParkingDurationSegmentRule> duration_segment_rule;

- public List<WechatPayParkingPreEntryRule> pre_entry_rule;

- public string holiday_mode;

- public string aggregate_limit_mode;

- public string first_charge_time_mode;


## WechatPayParkingDurationSegmentRule (class)

- public string day_type;

- public string start_time;

- public string end_time;

- public string vehicle_type;

- public string plate_type;

- public string charge_mode;

- public int duration_from;

- public int duration_to;

- public long fixed_amount;

- public int interval_min;

- public long interval_amount;

- public long interval_max_amount;

- public long max_fee_per_day;


## WechatPayParkingEntryResultJson (class)

- public string serial_number;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayParkingExtensionFeeItem (class)

- public string fee_type;

- public long amount;


## WechatPayParkingFeeResultJson (class)

- public long total_amount;

- public long parking_timestamp;

- public string parking_state;

- public string pay_state;

- public long allowed_exit_timestamp;

- public long next_raise_price;

- public long next_raise_timestamp;

- public long payable_amount;

- public long paid_amount;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayParkingFixedIntervalRule (class)

- public string day_type;

- public string start_time;

- public string end_time;

- public string vehicle_type;

- public string plate_type;

- public int first_duration;

- public long first_amount;

- public int interval_duration;

- public long interval_amount;

- public long interval_max_amount;

- public long max_fee_per_day;

- public string free_period_charging_mode;

- public string free_period_calculation_mode;

- public bool is_green_vehicle_free_parking;

- public string first_duration_mode;


## WechatPayParkingLotApplication (class)

- public string parking_lot_audit_no;

- public string audit_status;

- public long submit_time;

- public WechatPayParkingLotApplicationData parking_lot;

- public WechatPayParkingLotAuditComment audit_comment;

- public string wx_parking_lot_id;


## WechatPayParkingLotApplicationData (class)

- public string parking_lot_name;

- public string out_parking_lot_id;

- public string parking_lot_address;

- public string longitude;

- public string latitude;

- public string parking_lot_type;

- public string phone_number;

- public string parking_sign_url;

- public List<string> notification_text_list;

- public string payment_mini_prog_appid;

- public string payment_path;

- public string parking_order_mini_prog_appid;

- public string parking_order_path;

- public WechatPayParkingChargingRule charging_rule;


## WechatPayParkingLotApplicationListResultJson (class)

- public List<WechatPayParkingLotApplication> application_list;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayParkingLotApplicationResultJson (class)

- public WechatPayParkingLotApplication application;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayParkingLotApplicationSubmitResultJson (class)

- public string parking_lot_audit_no;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayParkingLotAuditComment (class)

- public List<WechatPayParkingLotAuditField> fields;


## WechatPayParkingLotAuditField (class)

- public string field;

- public string comment;

- public string recommendation;


## WechatPayParkingLotInfoResultJson (class)

- public string wx_parking_lot_id;

- public string out_parking_lot_id;

- public string parking_lot_name;

- public string parking_lot_address;

- public string payment_mini_prog_appid;

- public string payment_mini_prog_path;

- public string parking_order_mini_prog_appid;

- public string parking_order_mini_prog_path;

- public string enabled_state;

- public string reason;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayParkingPreEntryRule (class)

- public string day_type;

- public string start_time;

- public string end_time;

- public string vehicle_type;

- public string plate_type;

- public long amount;

- public long max_fee_per_day;


## WechatPayPatternInfo (class)

- public string description;

- public string merchant_logo;

- public string merchant_name;

- public string background_color;

- public string coupon_image;


## WechatPayPauseStockReturnJson (class)

- public string pause_time;

- public string stock_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayPayBusifavorReceiptsReturnJson (class)

- public string subsidy_receipt_id;

- public string stock_id;

- public string coupon_code;

- public string transaction_id;

- public string payer_merchant;

- public string payee_merchant;

- public int amount;

- public string description;

- public string status;

- public string fail_reason;

- public string success_time;

- public string out_subsidy_no;

- public string create_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayPayParkingReturnJson (class)

- public string appid;

- public string sp_mchid;

- public string description;

- public string create_time;

- public string out_trade_no;

- public string transaction_id;

- public string trade_state;

- public string trade_state_description;

- public string success_time;

- public string bank_type;

- public string user_repaid;

- public string attach;

- public string trade_scene;

- public WechatPayVehicleParkingReturnJsonPayParkingReturnJsonParkingInfo parking_info;

- public WechatPayVehicleParkingReturnJsonPayParkingReturnJsonPayer payer;

- public WechatPayVehicleParkingReturnJsonPayParkingReturnJsonAmount amount;

- public List<WechatPayVehicleParkingReturnJsonPayParkingReturnJsonPromotionDetail> promotion_detail;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayPayScoreCompleteServiceOrderRequestDataLocation (class)

- public string end_location;


## WechatPayPayScoreCompleteServiceOrderRequestDataPostDiscount (class)

- public string name;

- public string description;

- public long amount;

- public long count;


## WechatPayPayScoreCompleteServiceOrderRequestDataPostPayment (class)

- public string name;

- public long amount;

- public string description;

- public long count;


## WechatPayPayScoreCompleteServiceOrderRequestDataTimeRange (class)

- public string start_time;

- public string start_time_remark;

- public string end_time;

- public string end_time_remark;


## WechatPayPayScoreCreateDirectCompleteServiceOrderRequestDataLocation (class)

- public string start_location;

- public string end_location;


## WechatPayPayScoreCreateDirectCompleteServiceOrderRequestDataPostDiscount (class)

- public string name;

- public string description;


## WechatPayPayScoreCreateDirectCompleteServiceOrderRequestDataPostPayment (class)

- public string name;

- public long amount;

- public string description;

- public long count;


## WechatPayPayScoreCreateDirectCompleteServiceOrderRequestDataTimeRange (class)

- public string start_time;

- public string start_time_remark;

- public string end_time;

- public string end_time_remark;


## WechatPayPayScoreCreateServiceOrderRequestDataLocation (class)

- public string start_location;

- public string end_location;


## WechatPayPayScoreCreateServiceOrderRequestDataPostDiscount (class)

- public string name;

- public string description;


## WechatPayPayScoreCreateServiceOrderRequestDataPostPayment (class)

- public string name;

- public long amount;

- public string description;

- public long count;


## WechatPayPayScoreCreateServiceOrderRequestDataRiskFund (class)

- public string name;

- public long amount;

- public string description;


## WechatPayPayScoreCreateServiceOrderRequestDataTimeRange (class)

- public string start_time;

- public string start_time_remark;

- public string end_time;

- public string end_time_remark;


## WechatPayPayScoreModifyServiceOrderRequestDataPostDiscount (class)

- public string name;

- public string description;

- public long amount;

- public long count;


## WechatPayPayScoreModifyServiceOrderRequestDataPostPayment (class)

- public string name;

- public long amount;

- public string description;

- public long count;


## WechatPayPayScoreReturnJsonCompleteServiceOrderReturnJsonLocation (class)

- public string start_location;

- public string end_location;


## WechatPayPayScoreReturnJsonCompleteServiceOrderReturnJsonPostDiscounts (class)

- public string name;

- public string description;

- public long amount;

- public long count;


## WechatPayPayScoreReturnJsonCompleteServiceOrderReturnJsonPostPayments (class)

- public string name;

- public long amount;

- public string description;

- public long count;


## WechatPayPayScoreReturnJsonCompleteServiceOrderReturnJsonRiskFund (class)

- public string name;

- public long amount;

- public string description;


## WechatPayPayScoreReturnJsonCompleteServiceOrderReturnJsonTimeRange (class)

- public string start_time;

- public string start_time_remark;

- public string end_time;

- public string end_time_remark;


## WechatPayPayScoreReturnJsonCreateDirectCompleteServiceOrderReturnJsonLocation (class)

- public string start_location;

- public string end_location;


## WechatPayPayScoreReturnJsonCreateDirectCompleteServiceOrderReturnJsonPostDiscounts (class)

- public string name;

- public string description;

- public long amount;

- public long count;


## WechatPayPayScoreReturnJsonCreateDirectCompleteServiceOrderReturnJsonPostPayments (class)

- public string name;

- public long amount;

- public string description;

- public long count;


## WechatPayPayScoreReturnJsonCreateDirectCompleteServiceOrderReturnJsonTimeRange (class)

- public string start_time;

- public string start_time_remark;

- public string end_time;

- public string end_time_remark;


## WechatPayPayScoreReturnJsonCreateServiceOrderReturnJsonLocation (class)

- public string start_location;

- public string end_location;


## WechatPayPayScoreReturnJsonCreateServiceOrderReturnJsonPostDiscounts (class)

- public string name;

- public string description;

- public int amount;

- public long count;


## WechatPayPayScoreReturnJsonCreateServiceOrderReturnJsonPostPayments (class)

- public string name;

- public long amount;

- public string description;

- public long count;


## WechatPayPayScoreReturnJsonCreateServiceOrderReturnJsonRiskFund (class)

- public string name;

- public long amount;

- public string description;


## WechatPayPayScoreReturnJsonCreateServiceOrderReturnJsonTimeRange (class)

- public string start_time;

- public string start_time_remark;

- public string end_time;

- public string end_time_remark;


## WechatPayPayScoreReturnJsonModifyServiceOrderReturnJsonCollection (class)

- public string state;

- public long total_amount;

- public long paying_amount;

- public long paid_amount;

- public List<WechatPayPayScoreReturnJsonModifyServiceOrderReturnJsonDetails> details;


## WechatPayPayScoreReturnJsonModifyServiceOrderReturnJsonDetails (class)

- public long seq;

- public long amount;

- public string paid_type;

- public string paid_time;

- public string transaction_id;


## WechatPayPayScoreReturnJsonModifyServiceOrderReturnJsonLocation (class)

- public string start_location;

- public string end_location;


## WechatPayPayScoreReturnJsonModifyServiceOrderReturnJsonPostDiscounts (class)

- public string name;

- public string description;

- public long amount;

- public long count;


## WechatPayPayScoreReturnJsonModifyServiceOrderReturnJsonPostPayments (class)

- public string name;

- public long amount;

- public string description;

- public long count;


## WechatPayPayScoreReturnJsonModifyServiceOrderReturnJsonRiskFund (class)

- public string name;

- public long amount;

- public string description;


## WechatPayPayScoreReturnJsonModifyServiceOrderReturnJsonTimeRange (class)

- public string start_time;

- public string start_time_remark;

- public string end_time;

- public string end_time_remark;


## WechatPayPayScoreReturnJsonQueryGuideReturnJsonData (class)

- public string guide_id;

- public int store_id;

- public string name;

- public string mobile;

- public string userid;

- public string work_id;


## WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonCollection (class)

- public string state;

- public long total_amount;

- public long paying_amount;

- public long paid_amount;

- public List<WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonDetails> details;


## WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonDetails (class)

- public int seq;

- public long amount;

- public string paid_type;

- public string paid_time;

- public string transaction_id;

- public List<WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonPromotionDetail> promotion_detail;


## WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonGoodsDetail (class)

- public string goods_id;

- public long quantity;

- public long unit_price;

- public long discount_amount;

- public string goods_remark;


## WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonLocation (class)

- public string start_location;

- public string end_location;


## WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonPostDiscounts (class)

- public string name;

- public string description;

- public long amount;

- public long count;


## WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonPostPayments (class)

- public string name;

- public long amount;

- public string description;

- public long count;


## WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonPromotionDetail (class)

- public string coupon_id;

- public string name;

- public string scope;

- public string type;

- public int amount;

- public string stock_id;

- public int wechatpay_contribute;

- public long merchant_contribute;

- public long other_contribute;

- public string currency;

- public List<WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonGoodsDetail> goods_detail;


## WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonRiskFund (class)

- public string name;

- public long amount;

- public string description;


## WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonTimeRange (class)

- public string start_time;

- public string start_time_remark;

- public string end_time;

- public string end_time_remark;


## WechatPayPayScoreReturnJsonSyncPayServiceOrderReturnJsonCollection (class)

- public string state;

- public long total_amount;

- public long paying_amount;

- public long paid_amount;

- public List<WechatPayPayScoreReturnJsonSyncPayServiceOrderReturnJsonDetails> details;


## WechatPayPayScoreReturnJsonSyncPayServiceOrderReturnJsonDetails (class)

- public long amount;

- public string paid_type;

- public string paid_time;

- public string transaction_id;


## WechatPayPayScoreReturnJsonSyncPayServiceOrderReturnJsonLocation (class)

- public string start_location;

- public string end_location;


## WechatPayPayScoreReturnJsonSyncPayServiceOrderReturnJsonPostDiscounts (class)

- public string name;

- public string description;

- public long amount;


## WechatPayPayScoreReturnJsonSyncPayServiceOrderReturnJsonPostPayments (class)

- public string name;

- public long amount;

- public string description;

- public long count;


## WechatPayPayScoreReturnJsonSyncPayServiceOrderReturnJsonRiskFund (class)

- public string name;

- public long amount;

- public string description;


## WechatPayPayScoreReturnJsonSyncPayServiceOrderReturnJsonTimeRange (class)

- public string start_time;

- public string start_time_remark;

- public string end_time;

- public string end_time_remark;


## WechatPayPayScoreSyncPayServiceOrderRequestDataDetail (class)

- public string paid_time;


## WechatPayPayServiceOrderReturnJson (class)

- public string appid;

- public string mchid;

- public string out_order_no;

- public string service_id;

- public string order_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayProductCouponAssociatedOrderInfo (class)

- public string transaction_id;

- public string out_trade_no;

- public string mchid;

- public string sub_mchid;


## WechatPayProductCouponAssociatedPayScoreOrderInfo (class)

- public string order_id;

- public string out_order_no;


## WechatPayProductCouponAvailablePeriod (class)

- public string available_begin_time;

- public string available_end_time;

- public long available_days;

- public long wait_days_after_receive;

- public WechatPayProductCouponWeeklyAvailablePeriod weekly_available_period;

- public List<WechatPayProductCouponIrregularAvailablePeriod> irregular_available_period_list;

- public long available_seconds;


## WechatPayProductCouponAvailableStoreInfo (class)

- public string description;

- public string mini_program_appid;

- public string mini_program_path;

- public string app_jump_type;

- public string passcode_link;


## WechatPayProductCouponCodeCountInfo (class)

- public long total_count;

- public long available_count;


## WechatPayProductCouponCodeUploadResultJson (class)

- public long total_count;

- public List<string> success_code_list;

- public List<WechatPayProductCouponFailedCodeInfo> failed_code_list;

- public List<string> already_exist_code_list;

- public List<string> duplicate_code_list;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayProductCouponCombineImageRequestData (class)

- public string scope;

- public string type;

- public string usage_mode;

- public WechatPayProductCouponNormalRule normal_coupon;

- public WechatPayProductCouponDiscountRule discount_coupon;

- public WechatPayProductCouponExchangeRule exchange_coupon;

- public string background_color;


## WechatPayProductCouponComboChoice (class)

- public string name;

- public long price;

- public long count;

- public string image_url;

- public string mini_program_appid;

- public string mini_program_path;


## WechatPayProductCouponComboPackage (class)

- public string name;

- public long pick_count;

- public List<WechatPayProductCouponComboChoice> choice_list;


## WechatPayProductCouponCouponDisplayInfo (class)

- public string code_display_mode;

- public string background_color;

- public WechatPayProductCouponMiniProgramEntrance entrance_mini_program;

- public WechatPayProductCouponOfficialAccountEntrance entrance_official_account;

- public WechatPayProductCouponFinderEntrance entrance_finder;


## WechatPayProductCouponCutOutRequestData (class)

- public string image_url;


## WechatPayProductCouponDayPeriod (class)

- public long begin_time;

- public long end_time;


## WechatPayProductCouponDiscountRule (class)

- public long threshold;

- public long percent_off;


## WechatPayProductCouponDisplayConfiguration (class)

- public string code_display_mode;

- public string background_color;

- public WechatPayProductCouponMiniProgramEntrance entrance_mini_program;

- public WechatPayProductCouponOfficialAccountEntrance entrance_official_account;

- public WechatPayProductCouponFinderEntrance entrance_finder;


## WechatPayProductCouponDisplayInfo (class)

- public string name;

- public string image_url;

- public string background_url;

- public List<string> detail_image_url_list;

- public long original_price;

- public List<WechatPayProductCouponComboPackage> combo_package_list;


## WechatPayProductCouponExchangeRule (class)

- public long threshold;

- public long exchange_price;


## WechatPayProductCouponFailedCodeInfo (class)

- public string coupon_code;

- public string code;

- public string message;


## WechatPayProductCouponFailedStoreInfo (class)

- public string code;

- public string message;

- public string store_id;


## WechatPayProductCouponFinderEntrance (class)

- public string finder_id;

- public string finder_video_id;

- public string finder_video_cover_image_url;


## WechatPayProductCouponImageGenerationTaskResultJson (class)

- public string brand_id;

- public string task_id;

- public string image_generation_type;

- public string task_state;

- public WechatPayProductCouponImageResult combine_image_result;

- public WechatPayProductCouponImageResult cut_out_result;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayProductCouponImageResult (class)

- public string image_url;


## WechatPayProductCouponImageUploadResultJson (class)

- public string image_url;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayProductCouponIrregularAvailablePeriod (class)

- public string begin_time;

- public string end_time;


## WechatPayProductCouponMemberTagInfo (class)

- public string member_card_id;


## WechatPayProductCouponMiniProgramEntrance (class)

- public string appid;

- public string path;

- public string entrance_wording;

- public string guidance_wording;


## WechatPayProductCouponNormalRule (class)

- public long threshold;

- public long discount_amount;


## WechatPayProductCouponNotifyConfigResultJson (class)

- public string notify_url;

- public string update_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayProductCouponNotifyConfiguration (class)

- public string notify_appid;


## WechatPayProductCouponOfficialAccountEntrance (class)

- public string appid;


## WechatPayProductCouponPreSendResultJson (class)

- public string token;

- public string expire_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayProductCouponProgressiveBundleUsageDetail (class)

- public WechatPayProductCouponUserBundleInfo user_product_coupon_bundle_info;

- public long total_count;

- public long used_count;


## WechatPayProductCouponProgressiveBundleUsageInfo (class)

- public int count;

- public int interval_days;


## WechatPayProductCouponProgressiveBundleUsageRule (class)

- public WechatPayProductCouponAvailablePeriod coupon_available_period;

- public WechatPayProductCouponNormalRule normal_coupon;

- public WechatPayProductCouponDiscountRule discount_coupon;

- public WechatPayProductCouponExchangeRule exchange_coupon;

- public List<WechatPayProductCouponNormalRule> normal_coupon_list;

- public List<WechatPayProductCouponDiscountRule> discount_coupon_list;

- public List<WechatPayProductCouponExchangeRule> exchange_coupon_list;


## WechatPayProductCouponResultJson (class)

- public string product_coupon_id;

- public string scope;

- public string type;

- public string usage_mode;

- public WechatPayProductCouponSingleUsageInfo single_usage_info;

- public WechatPayProductCouponProgressiveBundleUsageInfo progressive_bundle_usage_info;

- public WechatPayProductCouponDisplayInfo display_info;

- public string out_product_no;

- public string state;

- public string deactivate_request_no;

- public string deactivate_time;

- public string deactivate_reason;

- public string brand_id;

- public WechatPayProductCouponStockResultJson stock;

- public WechatPayProductCouponStockBundleResultJson stock_bundle;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayProductCouponSentCountInfo (class)

- public long total_count;

- public long today_count;


## WechatPayProductCouponSingleUsageDetail (class)

- public string use_request_no;

- public string use_time;

- public string return_request_no;

- public string return_time;

- public WechatPayProductCouponAssociatedOrderInfo associated_order_info;

- public WechatPayProductCouponAssociatedPayScoreOrderInfo associated_pay_score_order_info;

- public long saved_amount;


## WechatPayProductCouponSingleUsageInfo (class)

- public WechatPayProductCouponNormalRule normal_coupon;

- public WechatPayProductCouponDiscountRule discount_coupon;


## WechatPayProductCouponSingleUsageRule (class)

- public WechatPayProductCouponAvailablePeriod coupon_available_period;

- public WechatPayProductCouponNormalRule normal_coupon;

- public WechatPayProductCouponDiscountRule discount_coupon;

- public WechatPayProductCouponExchangeRule exchange_coupon;


## WechatPayProductCouponStockBundleCreateInfo (class)

- public string remark;

- public string coupon_code_mode;

- public WechatPayProductCouponStockSendRule stock_send_rule;

- public WechatPayProductCouponProgressiveBundleUsageRule progressive_bundle_usage_rule;

- public WechatPayProductCouponUsageRuleDisplayInfo usage_rule_display_info;

- public WechatPayProductCouponDisplayConfiguration coupon_display_info;

- public WechatPayProductCouponNotifyConfiguration notify_config;

- public string store_scope;


## WechatPayProductCouponStockBundleInfo (class)

- public string stock_bundle_id;

- public long stock_bundle_index;


## WechatPayProductCouponStockBundleResultJson (class)

- public string stock_bundle_id;

- public List<WechatPayProductCouponStockResultJson> stock_list;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayProductCouponStockListResultJson (class)

- public long total_count;

- public List<WechatPayProductCouponStockResultJson> stock_list;

- public string next_page_token;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayProductCouponStockNotifyConfig (class)

- public string notify_appid;


## WechatPayProductCouponStockRequestData (class)

- public string remark;

- public string coupon_code_mode;

- public WechatPayProductCouponStockSendRule stock_send_rule;

- public WechatPayProductCouponSingleUsageRule single_usage_rule;

- public WechatPayProductCouponUsageRuleDisplayInfo usage_rule_display_info;

- public WechatPayProductCouponCouponDisplayInfo coupon_display_info;

- public WechatPayProductCouponStockNotifyConfig notify_config;

- public string store_scope;


## WechatPayProductCouponStockResultJson (class)

- public string product_coupon_id;

- public string stock_id;

- public string remark;

- public string coupon_code_mode;

- public WechatPayProductCouponCodeCountInfo coupon_code_count_info;

- public WechatPayProductCouponStockSendRule stock_send_rule;

- public WechatPayProductCouponSingleUsageRule single_usage_rule;

- public WechatPayProductCouponProgressiveBundleUsageRule progressive_bundle_usage_rule;

- public WechatPayProductCouponStockBundleInfo stock_bundle_info;

- public WechatPayProductCouponUsageRuleDisplayInfo usage_rule_display_info;

- public WechatPayProductCouponCouponDisplayInfo coupon_display_info;

- public WechatPayProductCouponStockNotifyConfig notify_config;

- public string store_scope;

- public WechatPayProductCouponSentCountInfo sent_count_info;

- public string state;

- public string deactivate_request_no;

- public string deactivate_time;

- public string deactivate_reason;

- public string brand_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayProductCouponStockSendRule (class)

- public long max_count;

- public long max_count_per_day;

- public long max_count_per_user;


## WechatPayProductCouponStoreInfo (class)

- public string store_id;


## WechatPayProductCouponStoreListResultJson (class)

- public long total_count;

- public List<WechatPayProductCouponStoreInfo> store_list;

- public string next_page_token;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayProductCouponStoreOperationResultJson (class)

- public long total_count;

- public List<WechatPayProductCouponStoreInfo> success_store_list;

- public List<WechatPayProductCouponFailedStoreInfo> failed_store_list;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayProductCouponTagInfo (class)

- public List<string> coupon_tag_list;


## WechatPayProductCouponUsageRuleDisplayInfo (class)

- public List<string> coupon_usage_method_list;

- public string mini_program_appid;

- public string mini_program_path;

- public string app_path;

- public string usage_description;

- public WechatPayProductCouponAvailableStoreInfo coupon_available_store_info;


## WechatPayProductCouponUserBundleInfo (class)

- public string user_coupon_bundle_id;

- public long user_coupon_bundle_index;


## WechatPayProductCouponUserCouponBundleResultJson (class)

- public string user_coupon_bundle_id;

- public List<WechatPayProductCouponUserCouponResultJson> user_product_coupon_list;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayProductCouponUserCouponListResultJson (class)

- public long total_count;

- public List<WechatPayProductCouponUserResultJson> user_coupon_list;

- public string next_page_token;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayProductCouponUserCouponResultJson (class)

- public string coupon_code;

- public string coupon_state;

- public string valid_begin_time;

- public string valid_end_time;

- public string receive_time;

- public string send_request_no;

- public string send_channel;

- public string confirm_request_no;

- public string confirm_time;

- public string deactivate_request_no;

- public string deactivate_time;

- public string deactivate_reason;

- public WechatPayProductCouponSingleUsageDetail single_usage_detail;

- public WechatPayProductCouponProgressiveBundleUsageDetail progressive_bundle_usage_detail;

- public WechatPayProductCouponResultJson product_coupon;

- public WechatPayProductCouponStockResultJson stock;

- public string attach;

- public WechatPayProductCouponTagInfo coupon_tag_info;

- public WechatPayProductCouponMemberTagInfo member_tag_info;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayProductCouponUserResultJson (class)

- public string coupon_code;

- public string coupon_state;

- public string valid_begin_time;

- public string valid_end_time;

- public string receive_time;

- public string send_request_no;

- public string send_channel;

- public string confirm_request_no;

- public string confirm_time;

- public string deactivate_request_no;

- public string deactivate_time;

- public string deactivate_reason;

- public WechatPayProductCouponSingleUsageDetail single_usage_detail;

- public WechatPayProductCouponProgressiveBundleUsageDetail progressive_bundle_usage_detail;

- public WechatPayProductCouponResultJson product_coupon;

- public WechatPayProductCouponStockResultJson stock;

- public string attach;

- public WechatPayProductCouponTagInfo coupon_tag_info;

- public WechatPayProductCouponMemberTagInfo member_tag_info;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayProductCouponWeeklyAvailablePeriod (class)

- public List<string> day_list;

- public List<WechatPayProductCouponDayPeriod> day_period_list;


## WechatPayProfitsharingCreateProfitsharingRequestDataReceiver (class)

- public string type;

- public string account;

- public string name;

- public int amount;

- public string description;


## WechatPayProfitsharingReturnJsonCreateProfitsharingReturnJsonReceiver (class)

- public int amount;

- public string description;

- public string type;

- public string account;

- public string result;

- public string fail_reason;

- public string create_time;

- public string finish_time;

- public string detail_id;


## WechatPayProfitsharingReturnJsonQueryProfitsharingReturnJsonReceivers (class)

- public int amount;

- public string description;

- public string type;

- public string account;

- public string result;

- public string fail_reason;

- public string detail_id;

- public string create_time;

- public string finish_time;


## WechatPayProfitsharingReturnJsonUnfreezeProfitsharingReturnJsonReceivers (class)

- public int amount;

- public string description;

- public string type;

- public string account;

- public string result;

- public string fail_reason;

- public string create_time;

- public string finish_time;

- public string detail_id;


## WechatPayProvinceInfo (class)

- public string province_code;

- public string province_name;


## WechatPayPublicKeyCollection (class)

- public JsonValue Value;


## WechatPayQueryApply4SubApplymentReturnJson (class)

- public string applyment_id;

- public string out_request_no;

- public string applyment_state;

- public string applyment_state_msg;

- public string sign_url;

- public WechatPayApply4SubAccountVerificationInfo account_validation;

- public List<WechatPayApply4SubAuditDetail> audit_detail;

- public string legal_validation_url;

- public string sub_mchid;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryApply4SubSettlementReturnJson (class)

- public string sub_mchid;

- public WechatPayApply4SubAccountInfo account_info;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryApply4SubjectApplymentReturnJson (class)

- public string applyment_id;

- public string out_request_no;

- public string applyment_state;

- public string applyment_state_msg;

- public string create_time;

- public string update_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryBankListReturnJson (class)

- public List<WechatPayBankInfo> data;

- public int total_count;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryBankReturnJson (class)

- public string bank_alias_code;

- public string bank_alias;

- public string account_type;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryBranchListReturnJson (class)

- public List<WechatPayBranchInfo> data;

- public int total_count;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryBusifavorCouponReturnJson (class)

- public string belong_merchant;

- public string stock_name;

- public string comment;

- public string goods_name;

- public string stock_type;

- public bool transferable;

- public bool shareable;

- public string coupon_state;

- public WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonDisplayPatternInfo display_pattern_info;

- public WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonCouponUseRule coupon_use_rule;

- public WechatPayMarketingReturnJsonQueryBusifavorCouponReturnJsonCustomEntrance custom_entrance;

- public string coupon_code;

- public string stock_id;

- public string available_start_time;

- public string expire_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryBusifavorCouponsReturnJson (class)

- public List<WechatPayMarketingReturnJsonQueryBusifavorCouponsReturnJsonData> data;

- public int total_count;

- public int offset;

- public int limit;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryBusifavorNotifyUrlReturnJson (class)

- public string notify_url;

- public string mchid;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryBusifavorPayReceiptsReturnJson (class)

- public string subsidy_receipt_id;

- public string stock_id;

- public string coupon_code;

- public string transaction_id;

- public string payer_merchant;

- public string payee_merchant;

- public int amount;

- public string description;

- public string status;

- public string fail_reason;

- public string success_time;

- public string out_subsidy_no;

- public string create_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryBusifavorStockReturnJson (class)

- public string stock_name;

- public string belong_merchant;

- public string comment;

- public string goods_name;

- public string stock_type;

- public WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonCouponUseRule coupon_use_rule;

- public WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonStockSendRule stock_send_rule;

- public WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonCustomEntrance custom_entrance;

- public WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonDisplayPatternInfo display_pattern_info;

- public string stock_state;

- public string coupon_code_mode;

- public string stock_id;

- public WechatPayCouponCodeCount coupon_code_count;

- public WechatPayMarketingReturnJsonQueryBusifavorStockReturnJsonNotifyConfig notify_config;

- public WechatPaySendCountInformation send_count_information;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryCityListReturnJson (class)

- public List<WechatPayCityInfo> data;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryCombineTransactionsReturnJson (class)

- public string combine_appid;

- public string combine_mchid;

- public string combine_out_trade_no;

- public JsonValue scene_info;

- public List<WechatPaySubOrderResult> sub_orders;

- public JsonValue combine_payer_info;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryComplaintNotifyUrlReturnJson (class)

- public string mchid;

- public string url;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryComplaintReturnJson (class)

- public string complaint_id;

- public string complaint_time;

- public string complaint_detail;

- public string complainted_mchid;

- public string complaint_state;

- public string payer_phone;

- public string payer_openid;

- public List<WechatPayComplaintReturnJsonQueryComplaintReturnJsonComplaintMediaList> complaint_media_list;

- public List<WechatPayComplaintReturnJsonQueryComplaintReturnJsonComplaintOrderInfo> complaint_order_info;

- public bool complaint_full_refunded;

- public bool incoming_user_response;

- public string problem_description;

- public int user_complaint_times;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryComplaintsReturnJson (class)

- public List<WechatPayComplaintReturnJsonQueryComplaintsReturnJsonData> data;

- public int limit;

- public int offset;

- public int total_count;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryCouponsReturnJson (class)

- public List<WechatPayMarketingReturnJsonQueryCouponsReturnJsonQueryCouponReturnJson> data;

- public int total_count;

- public int limit;

- public int offset;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryElecsignReturnJson (class)

- public string state;

- public string create_time;

- public string download_url;

- public string hash_value;

- public string hash_type;

- public string fail_reason;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryFapiaoReturnJson (class)

- public string fapiao_apply_id;

- public string status;

- public string fapiao_number;

- public string fapiao_code;

- public string fapiao_time;

- public int fapiao_amount;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryGuideReturnJson (class)

- public List<WechatPayPayScoreReturnJsonQueryGuideReturnJsonData> data;

- public int total_count;

- public int limit;

- public int offset;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryItemsReturnJson (class)

- public long total_count;

- public List<string> data;

- public long offset;

- public long limit;

- public int stock_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryMerchantRiskNotifyUrlReturnJson (class)

- public string notify_url;

- public string create_time;

- public string update_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryMerchantsReturnJson (class)

- public long total_count;

- public List<string> data;

- public long offset;

- public long limit;

- public string stock_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryNegotiationHistorysReturnJson (class)

- public List<WechatPayComplaintReturnJsonQueryNegotiationHistorysReturnJsonData> data;

- public int limit;

- public int offset;

- public int total_count;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryParkingReturnJson (class)

- public string appid;

- public string sp_mchid;

- public string description;

- public string create_time;

- public string out_trade_no;

- public string transaction_id;

- public string trade_state;

- public string trade_state_description;

- public string success_time;

- public string bank_type;

- public string user_repaid;

- public string attach;

- public string trade_scene;

- public WechatPayVehicleParkingReturnJsonQueryParkingReturnJsonParkingInfo parking_info;

- public WechatPayVehicleParkingReturnJsonQueryParkingReturnJsonPayer payer;

- public WechatPayVehicleParkingReturnJsonQueryParkingReturnJsonAmount amount;

- public List<WechatPayVehicleParkingReturnJsonQueryParkingReturnJsonPromotionDetail> promotion_detail;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryPartnershipsReturnJson (class)

- public List<WechatPayMarketingReturnJsonQueryPartnershipsReturnJsonData> data;

- public long limit;

- public long offset;

- public long total_count;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryPaygiftActivitiesReturnJson (class)

- public List<WechatPayMarketingReturnJsonQueryPaygiftActivitiesReturnJsonData> data;

- public int total_count;

- public int offset;

- public int limit;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryPaygiftActivityGoodsReturnJson (class)

- public List<WechatPayMarketingReturnJsonQueryPaygiftActivityGoodsReturnJsonData> data;

- public long total_count;

- public long offset;

- public long limit;

- public string activity_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryPaygiftActivityMerchantsReturnJson (class)

- public List<WechatPayMarketingReturnJsonQueryPaygiftActivityMerchantsReturnJsonData> data;

- public long total_count;

- public long offset;

- public long limit;

- public string activity_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryPaygiftActivityReturnJson (class)

- public string activity_id;

- public string activity_type;

- public WechatPayMarketingReturnJsonQueryPaygiftActivityReturnJsonActivityBaseInfo activity_base_info;

- public WechatPayMarketingReturnJsonQueryPaygiftActivityReturnJsonAwardSendRule award_send_rule;

- public WechatPayMarketingReturnJsonQueryPaygiftActivityReturnJsonAdvancedSetting advanced_setting;

- public string activity_status;

- public string creator_merchant_id;

- public string belong_merchant_id;

- public string pause_time;

- public string recovery_time;

- public string create_time;

- public string update_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryPermissionByAuthorizationCodeReturnJson (class)

- public string service_id;

- public string appid;

- public string mchid;

- public string openid;

- public string authorization_code;

- public string authorization_state;

- public string notify_url;

- public string cancel_authorization_time;

- public string authorization_success_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryPermissionByOpenidReturnJson (class)

- public string service_id;

- public string appid;

- public string mchid;

- public string openid;

- public string authorization_code;

- public string authorization_state;

- public string cancel_authorization_time;

- public string authorization_success_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryProfitsharingAmountsReturnJson (class)

- public string transaction_id;

- public int unsplit_amount;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryProfitsharingBillsReturnJson (class)

- public string hash_type;

- public string hash_value;

- public string download_url;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryProfitsharingConfigsReturnJson (class)

- public string brand_mchid;

- public string sub_mchid;

- public int max_ratio;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryProfitsharingReturnJson (class)

- public string sub_mchid;

- public string transaction_id;

- public string out_order_no;

- public string order_id;

- public string state;

- public List<WechatPayProfitsharingReturnJsonQueryProfitsharingReturnJsonReceivers> receivers;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryProvinceListReturnJson (class)

- public List<WechatPayProvinceInfo> data;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryReturnProfitsharingReturnJson (class)

- public string sub_mchid;

- public string order_id;

- public string out_order_no;

- public string out_return_no;

- public string return_id;

- public string return_mchid;

- public int amount;

- public string description;

- public string result;

- public string fail_reason;

- public string create_time;

- public string finish_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryServiceOrderReturnJson (class)

- public string appid;

- public string mchid;

- public string service_id;

- public string out_order_no;

- public string service_introduction;

- public string state;

- public string state_description;

- public long total_amount;

- public List<WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonPostPayments> post_payments;

- public List<WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonPostDiscounts> post_discounts;

- public WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonRiskFund risk_fund;

- public WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonTimeRange time_range;

- public WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonLocation location;

- public string attach;

- public string notify_url;

- public string order_id;

- public bool need_collection;

- public WechatPayPayScoreReturnJsonQueryServiceOrderReturnJsonCollection collection;

- public string openid;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryServiceReturnJson (class)

- public string plate_number;

- public string plate_color;

- public string service_open_time;

- public string openid;

- public string service_state;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQuerySmartGuideReturnJson (class)

- public List<WechatPaySmartGuideInfo> data;

- public int total_count;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryStocksReturnJson (class)

- public long total_count;

- public List<WechatPayStockReturnJson> data;

- public long limit;

- public long offset;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQuerySubMerchantApplymentReturnJson (class)

- public string applyment_id;

- public string out_request_no;

- public string applyment_state;

- public string applyment_state_msg;

- public string sign_url;

- public WechatPayAccountVerificationInfo account_validation;

- public List<WechatPayAuditDetail> audit_detail;

- public string legal_validation_url;

- public string sub_mchid;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryTransferReturnJson (class)

- public string transfer_bill_no;

- public string out_bill_no;

- public string appid;

- public string transfer_scene_id;

- public string openid;

- public string user_name;

- public int transfer_amount;

- public string transfer_remark;

- public string create_time;

- public string state;

- public string fail_reason;

- public string update_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayQueryUserAuthorizationReturnJson (class)

- public string openid;

- public string authorize_state;

- public string authorize_time;

- public string deauthorize_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayRefundReturnJson (class)

- public string refund_id;

- public string out_refund_no;

- public string transaction_id;

- public string out_trade_no;

- public string channel;

- public string user_received_account;

- public string success_time;

- public string create_time;

- public string status;

- public string funds_account;

- public WechatPayBasePayReturnJsonRefundReturnJsonAmount amount;

- public List<WechatPayBasePayReturnJsonRefundReturnJsonPromotionDetail> promotion_detail;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayRegisterGuideReturnJson (class)

- public string guide_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayRegisterSmartGuideReturnJson (class)

- public string guide_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayRestartStockReturnJson (class)

- public string restart_time;

- public string stock_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayReturnBusifavorCouponReturnJson (class)

- public string wechatpay_return_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayReturnJsonBase (class)

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayReturnProfitsharingReturnJson (class)

- public string sub_mchid;

- public string order_id;

- public string out_order_no;

- public string out_return_no;

- public string return_id;

- public string return_mchid;

- public int amount;

- public string description;

- public string result;

- public string fail_reason;

- public string create_time;

- public string finish_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayReverseFapiaoReturnJson (class)

- public string fapiao_apply_id;

- public string reverse_apply_id;

- public string reverse_status;

- public string reverse_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPaySalesSceneInfo (class)

- public string store_name;

- public string store_url;

- public string store_qr_code;

- public string mini_program_sub_appid;


## WechatPaySendCardReturnJson (class)

- public string card_code;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPaySendCountInformation (class)

- public long total_send_num;

- public long total_send_amount;

- public long today_send_num;

- public long today_send_amount;


## WechatPayServiceOrderRequestDevice (class)

- public string start_device_id;

- public string end_device_id;

- public string materiel_no;


## WechatPaySetBusifavorCouponCodesReturnJson (class)

- public string stock_id;

- public long total_count;

- public long success_count;

- public List<string> success_codes;

- public string success_time;

- public long fail_count;

- public List<WechatPayFailCodes> fail_codes;

- public List<string> exist_codes;

- public List<string> duplicate_codes;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPaySetBusifavorSetNotifyUrlReturnJson (class)

- public string update_time;

- public string notify_url;

- public string mchid;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPaySetNotifyUrlReturnJson (class)

- public string update_time;

- public string notify_url;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPaySmartGuideInfo (class)

- public string guide_id;

- public string store_id;

- public string userid;

- public string name;

- public string mobile;

- public string work_id;

- public string create_time;

- public string update_time;


## WechatPayStartStockReturnJson (class)

- public string start_time;

- public string stock_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayStockReturnJson (class)

- public string stock_id;

- public string stock_creator_mchid;

- public string stock_name;

- public string status;

- public string create_time;

- public string description;

- public WechatPayMarketingReturnJsonStockReturnJsonStockUseRule stock_use_rule;

- public string available_begin_time;

- public string available_end_time;

- public long distributed_coupons;

- public bool no_cash;

- public string start_time;

- public string stop_time;

- public WechatPayMarketingReturnJsonStockReturnJsonCutToMessage cut_to_message;

- public bool singleitem;

- public string stock_type;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPaySubMerchantApplymentReturnJson (class)

- public string applyment_id;

- public string out_request_no;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPaySubOrderAmountInfo (class)

- public int total_amount;

- public string currency;

- public int payer_amount;

- public string payer_currency;


## WechatPaySubOrderResult (class)

- public string mchid;

- public string trade_type;

- public string trade_state;

- public string bank_type;

- public string attach;

- public string success_time;

- public string transaction_id;

- public string out_trade_no;

- public WechatPaySubOrderAmountInfo amount;


## WechatPaySubmerchantBillDownloadItem (class)

- public int bill_sequence;

- public string download_url;

- public string encrypt_key;

- public string hash_type;

- public string hash_value;

- public string nonce;


## WechatPaySubmerchantBillReturnJson (class)

- public int download_bill_count;

- public List<WechatPaySubmerchantBillDownloadItem> download_bill_list;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPaySyncPayServiceOrderReturnJson (class)

- public string appid;

- public string mchid;

- public string out_order_no;

- public string service_id;

- public string service_introduction;

- public string openid;

- public string state;

- public string state_description;

- public long total_amount;

- public List<WechatPayPayScoreReturnJsonSyncPayServiceOrderReturnJsonPostPayments> post_payments;

- public List<WechatPayPayScoreReturnJsonSyncPayServiceOrderReturnJsonPostDiscounts> post_discounts;

- public WechatPayPayScoreReturnJsonSyncPayServiceOrderReturnJsonRiskFund risk_fund;

- public WechatPayPayScoreReturnJsonSyncPayServiceOrderReturnJsonTimeRange time_range;

- public WechatPayPayScoreReturnJsonSyncPayServiceOrderReturnJsonLocation location;

- public string attach;

- public string notify_url;

- public string order_id;

- public bool need_collection;

- public WechatPayPayScoreReturnJsonSyncPayServiceOrderReturnJsonCollection collection;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayTenPayApiResultCode (class)

- public bool Success;

- public string StateCode;

- public string ErrorCode;

- public string ErrorMessage;

- public string Solution;

- public string Additional;


## WechatPayTenPayV3UnifiedorderRequestDataSceneInfo (class)

- public WechatPayUniversalTenPayV3UnifiedorderRequestDataSceneInfoStoreInfo store_info;

- public JsonValue h5_info;


## WechatPayTenpayDateTime (class)

- public JsonValue Value;


## WechatPayTerminatePartnershipsReturnJson (class)

- public string terminate_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayTerminatePaygiftActivityReturnJson (class)

- public string terminate_time;

- public string activity_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayTransferBillReturnJson (class)

- public string out_bill_no;

- public string transfer_bill_no;

- public string create_time;

- public string state;

- public string package_info;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayTransferDetailList (class)

- public string out_detail_no;

- public int transfer_amount;

- public string transfer_remark;

- public string openid;

- public string user_name;


## WechatPayTransferSceneReportInfo (class)

- public string info_type;

- public string info_content;


## WechatPayUboInfo (class)

- public string id_type;

- public string id_card_copy;

- public string id_card_national;

- public string name;

- public string id_number;

- public string id_period_begin;

- public string id_period_end;


## WechatPayUnfreezeProfitsharingReturnJson (class)

- public string transaction_id;

- public string out_order_no;

- public string order_id;

- public string state;

- public List<WechatPayProfitsharingReturnJsonUnfreezeProfitsharingReturnJsonReceivers> receivers;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayUniversalTenPayV3UnifiedorderRequestDataSceneInfoStoreInfo (class)

- public string id;

- public string name;

- public string area_code;

- public string address;


## WechatPayUpdateMerchantRiskNotifyUrlReturnJson (class)

- public string notify_url;

- public string update_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayUpdateSmartGuideReturnJson (class)

- public string guide_id;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayUseBusifavorCouponReturnJson (class)

- public string stock_id;

- public string openid;

- public string wechatpay_use_time;

- public WechatPayTenPayApiResultCode ResultCode;

- public bool VerifySignSuccess;


## WechatPayVehicleParkingPayParkingRequestDataAmount (class)

- public int total;

- public string currency;


## WechatPayVehicleParkingPayParkingRequestDataParkingInfo (class)

- public string parking_id;

- public string plate_number;

- public string plate_color;

- public string start_time;

- public string end_time;

- public string parking_name;

- public int charging_duration;

- public string device_id;


## WechatPayVehicleParkingReturnJsonPayParkingReturnJsonAmount (class)

- public int total;

- public string currency;

- public int payer_total;

- public int discount_total;


## WechatPayVehicleParkingReturnJsonPayParkingReturnJsonParkingInfo (class)

- public string parking_id;

- public string plate_number;

- public string plate_color;

- public string start_time;

- public string end_time;

- public string parking_name;

- public int charging_duration;

- public string device_id;


## WechatPayVehicleParkingReturnJsonPayParkingReturnJsonPayer (class)

- public string openid;


## WechatPayVehicleParkingReturnJsonPayParkingReturnJsonPromotionDetail (class)

- public string coupon_id;

- public string name;

- public string scope;

- public string type;

- public string stock_id;

- public int amount;

- public int wechatpay_contribute;

- public int merchant_contribute;

- public int other_contribute;

- public string currency;


## WechatPayVehicleParkingReturnJsonQueryParkingReturnJsonAmount (class)

- public int total;

- public string currency;

- public int payer_total;

- public int discount_total;


## WechatPayVehicleParkingReturnJsonQueryParkingReturnJsonParkingInfo (class)

- public string parking_id;

- public string plate_number;

- public string plate_color;

- public string start_time;

- public string end_time;

- public string parking_name;

- public int charging_duration;

- public string device_id;


## WechatPayVehicleParkingReturnJsonQueryParkingReturnJsonPayer (class)

- public string openid;

- public string sub_openid;


## WechatPayVehicleParkingReturnJsonQueryParkingReturnJsonPromotionDetail (class)

- public string coupon_id;

- public string name;

- public string scope;

- public string type;

- public string stock_id;

- public int amount;

- public int wechatpay_contribute;

- public int merchant_contribute;

- public int other_contribute;

- public string currency;
