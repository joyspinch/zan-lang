#!/usr/bin/env bash
# Build the Zan-implemented runtime object (runtime_core.zan, batches 1-14)
# with the native bootstrap compiler and localize its non-API symbols.
#
#   SEED=/path/to/stage2 bash crt-transition/build_zan_core.sh [out.o]
#
# Produces an object exporting exactly the 152 allowlisted zan_* symbols
# (monotonic/sha256/sha512/alloc/free/crc32 + pkg_fopen/file_*/plat_*/
# io_* + atomic/audio/monitor/dispatch/eh/shared_table + io reactor
# co helpers + set_ready_hook/thread_start/thread_current_id + mem ops
# copy/fill/compare/find/get_string/put_string + UTF-16 text converters
# multi_byte_to_wide_char/wide_char_to_multi_byte + weakened embed_*;
# batch 14 adds md5/sha1/hmac_sha256/urandom/vm_read/lock_enter/
# lock_exit/exe_path — the target-agnostic faces the compiler's
# emitters and stdlib AppPath now call).
# Since batch 13 this object ALONE is the whole darwin-lane runtime (the
# -DZAN_RT_CORE_ZAN builds of zanstubs.c/zanhost.c are empty and kept
# only as the recorded C remainder):
#
#   python3 scripts/native_regression.py --seed "$SEED" \
#     --runtime "crt-transition/runtime_core.o" \
#     tests/selfhost/native_rt_core.zan
#
# The pure-C baseline for A/B regression is built alongside:
#   zanstubs_full.o + zanhost_full.o (both without the define).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SEED="${SEED:?Set SEED=/path/to/native-compiler-or-gen1}"
OUT="${1:-$ROOT/crt-transition/runtime_core.o}"

cd "$ROOT"
"$SEED" "$OUT" crt-transition/runtime_core.zan
python3 crt-transition/localize_syms.py "$OUT" \
  _zan_monotonic_ns _zan_monotonic_us _zan_sha256 _zan_sha512 \
  _zan_alloc _zan_free _zan_crc32 \
  _zan_pkg_fopen _zan_file_fopen _zan_file_remove _zan_file_rename \
  _zan_file_read_path \
  _zan_file_open _zan_file_read _zan_file_write _zan_file_seek \
  _zan_file_tell _zan_file_flush _zan_file_close _zan_file_eof \
  _zan_file_length _zan_file_attributes _zan_file_time \
  _zan_file_set_readonly _zan_file_set_time \
  _zan_file_try_lock _zan_file_unlock \
  _zan_mmap_create _zan_mmap_open _zan_mmap_from_file _zan_mmap_map \
  _zan_mmap_unmap _zan_mmap_flush _zan_mmap_close _zan_mmap_unlink \
  _zan_plat_net_interfaces _zan_plat_icmp_ping \
  _zan_io_socket_cleanup _zan_io_socket_send _zan_io_socket_recv \
  _zan_io_sockaddr_ip_str _zan_io_socket_peer_ip _zan_io_socket_peer_ip_into \
  _zan_io_sockaddr_ip_str_into _zan_io_resolve_sa _zan_io_resolve_ipv4 \
  _zan_io_sockaddr_family _zan_io_sockaddr_is_safe _zan_io_resolve_all \
  _zan_io_resolve_all_async _zan_io_connect_sa _zan_io_connect_status \
  _zan_io_socket_ready _zan_io_socket_alive \
  _zan_atomic_int_create _zan_atomic_int_destroy _zan_atomic_int_load \
  _zan_atomic_int_store _zan_atomic_int_add _zan_atomic_int_exchange \
  _zan_atomic_int_compare_exchange \
  _zan_monitor_enter _zan_monitor_exit \
  _zan_dispatch_init _zan_dispatch_post _zan_dispatch_take _zan_dispatch_clear \
  _zan_eh_tls_state \
  _zan_shared_table_create _zan_shared_table_create_anon _zan_shared_table_open \
  _zan_shared_table_handle _zan_shared_table_attach _zan_shared_table_close \
  _zan_shared_table_destroy _zan_shared_table_set_int _zan_shared_table_get_int \
  _zan_shared_table_set_float _zan_shared_table_get_float \
  _zan_shared_table_set_string _zan_shared_table_get_string \
  _zan_shared_table_increment _zan_shared_table_expire \
  _zan_shared_table_expire_at _zan_shared_table_expires_at \
  _zan_shared_table_purge_expired _zan_shared_table_rate_allow \
  _zan_shared_table_lock_acquire _zan_shared_table_lock_release \
  _zan_shared_table_delete _zan_shared_table_exists _zan_shared_table_count \
  _zan_shared_table_clear _zan_shared_table_hash _zan_shared_table_stat \
  _zan_shared_table_set_int_at _zan_shared_table_get_int_at \
  _zan_shared_table_increment_at _zan_shared_table_extreme_at \
  _zan_shared_table_set_string_at _zan_shared_table_get_string_at \
  _zan_shared_table_match_at _zan_shared_table_exists_at \
  _zan_shared_table_delete_at \
  _zan_io_wait_co _zan_io_recv_co _zan_io_recv_to_co _zan_io_accept_co \
  _zan_io_poll _zan_io_close_notify _zan_resolve_sa_co _zan_resolve_ipv4_co \
  _zan_set_ready_hook _zan_thread_start _zan_thread_current_id \
  _zan_io_set_nonblocking _zan_io_connect_sa_start \
  _zan_dir_list_into _zan_thread_detach \
  _zan_io_pump _zan_io_pump_timeout \
  _zan_copy _zan_fill _zan_compare _zan_find \
  _zan_get_string _zan_put_string \
  _zan_multi_byte_to_wide_char _zan_wide_char_to_multi_byte \
  _zan_md5 _zan_sha1 _zan_hmac_sha256 _zan_urandom _zan_vm_read \
  _zan_lock_enter _zan_lock_exit _zan_exe_path \
  _zan_file_app_dir _zan_exe_dir_into \
  _zan_timer_after _zan_timer_cancel_delay _zan_timer_clear \
  _zan_timer_clear_all _zan_timer_delay _zan_timer_dispatch_due \
  _zan_timer_info _zan_timer_list_at _zan_timer_list_count \
  _zan_timer_next_timeout _zan_timer_now_ms _zan_timer_pending \
  _zan_timer_runtime_reset _zan_timer_saturating_due \
  _zan_timer_set_ready_hook _zan_timer_stats _zan_timer_tick \
  _swoole_timer_after _swoole_timer_clear _swoole_timer_clear_all \
  _swoole_timer_info _swoole_timer_list_at _swoole_timer_list_count \
  _swoole_timer_stats _swoole_timer_tick \
  _zan_rt_fatal _zan_rt_set_fatal_handler _zan_utf8_argv \
  _zan_ui_thread_set _zan_ui_thread_check _zan_ui_thread_assert \
  _zan_proc_run_safe _zan_proc_start_detached_safe _zan_proc_start_program_safe \
  _zan_proc_capture_safe _zan_proc_free_buf \
  --weaken \
  _zan_gate_new _zan_gate_park _zan_gate_signal _zan_gate_free \
  _zan_co_sched_init _zan_co_ready _zan_co_delay \
  _zan_co_frame_free _zan_co_pending _zan_co_sched_run_until \
  _zan_co_sched_run \
  _zan_embed_has _zan_embed_read _zan_embed_bytes _zan_embed_list \
  _zan_embed_decode _zan_embed_rawlen \
  _zan_rt_dbl_parse _zan_rt_dbl_str _zan_rt_guard_fail3 \
  _zan_rt_set_strict _zan_rt_soft_is_hard \
  _zan_gui_add_hit_guard _zan_gui_adopt_sdl_window _zan_gui_blit_image _zan_gui_blur_partial_miss_take \
  _zan_gui_blur_rect _zan_gui_blur_rect_cached _zan_gui_blur_round_cached _zan_gui_caption_button_width \
  _zan_gui_center_window _zan_gui_clear _zan_gui_clear_hit_guards _zan_gui_clear_rect \
  _zan_gui_client_height _zan_gui_client_width _zan_gui_close_window _zan_gui_create_surface \
  _zan_gui_create_window _zan_gui_destroy_surface _zan_gui_destroy_window _zan_gui_disable_glass \
  _zan_gui_draw3d _zan_gui_draw_circle _zan_gui_draw_line _zan_gui_draw_polybatch \
  _zan_gui_draw_polyline _zan_gui_draw_polyline_fx _zan_gui_draw_rect _zan_gui_draw_rounded_rect \
  _zan_gui_draw_rounded_rect_mask _zan_gui_draw_text _zan_gui_draw_text_bold _zan_gui_draw_text_rot \
  _zan_gui_drop_pending _zan_gui_drop_take _zan_gui_enable_glass _zan_gui_event_button \
  _zan_gui_event_flag _zan_gui_event_hwnd _zan_gui_event_keycode _zan_gui_event_kind \
  _zan_gui_event_mods _zan_gui_event_seq _zan_gui_event_x _zan_gui_event_y \
  _zan_gui_fill_circle _zan_gui_fill_grad_mask _zan_gui_fill_radial _zan_gui_fill_rect \
  _zan_gui_fill_rounded_rect _zan_gui_fill_rounded_rect_mask _zan_gui_fill_sector _zan_gui_fill_vgrad \
  _zan_gui_fill_vgrad_mask _zan_gui_font_ascent _zan_gui_font_height _zan_gui_get_clipboard \
  _zan_gui_get_dpi_scale _zan_gui_get_tick_ms _zan_gui_guard_call _zan_gui_guard_recovered \
  _zan_gui_guard_wndproc _zan_gui_image_evict _zan_gui_image_height _zan_gui_image_load_mem \
  _zan_gui_image_load_svg _zan_gui_image_width _zan_gui_inject_event _zan_gui_inject_pending \
  _zan_gui_is_maximized _zan_gui_measure_text _zan_gui_mesh_create _zan_gui_minimize \
  _zan_gui_poll_event _zan_gui_pop_clip _zan_gui_present _zan_gui_present_dirty_add \
  _zan_gui_present_full _zan_gui_push_clip _zan_gui_read_pixel _zan_gui_render_backend \
  _zan_gui_reset_clip _zan_gui_restore_rect _zan_gui_restore_sub_rect _zan_gui_scene_present \
  _zan_gui_scene_set_renderer _zan_gui_scene_upload _zan_gui_set_caption_buttons _zan_gui_set_clipboard \
  _zan_gui_set_cursor _zan_gui_set_ime_pos _zan_gui_set_opacity _zan_gui_set_render_backend \
  _zan_gui_set_title _zan_gui_set_topmost _zan_gui_set_window_pos _zan_gui_shadow_rounded_rect \
  _zan_gui_show_window _zan_gui_sleep_ms _zan_gui_snapshot_patch_rect _zan_gui_snapshot_rect \
  _zan_gui_stat_read _zan_gui_stat_top _zan_gui_stat_top_fill _zan_gui_surface_dump \
  _zan_gui_surface_height _zan_gui_surface_release _zan_gui_surface_rounded_rect _zan_gui_surface_rounded_rect_mask \
  _zan_gui_surface_width _zan_gui_text_stat_enable _zan_gui_text_stat_read _zan_gui_titlebar_height \
  _zan_gui_toggle_maximize _zan_gui_wait_event _zan_gui_wait_event_timeout _zan_gui_wake \
  _zan_gui_window_focused _zan_gui_window_height _zan_gui_window_visible _zan_gui_window_width \
  _zan_gui_write_pixels _zan_tray_next_event _zan_tray_set_menu _zan_tray_set_tooltip \
  _zan_tray_start _zan_tray_start_pixels _zan_tray_stop \
  _zan_cef_last_error _zan_cef_ready _zan_cef_execute_process _zan_cef_init \
  _zan_cef_work _zan_cef_shutdown _zan_cef_create _zan_cef_close \
  _zan_cef_alive _zan_cef_window _zan_cef_set_bounds _zan_cef_set_visible \
  _zan_cef_set_clip _zan_cef_set_focus _zan_cef_set_zoom _zan_cef_get_zoom \
  _zan_cef_find _zan_cef_stop_find _zan_cef_print _zan_cef_show_devtools \
  _zan_cef_close_devtools _zan_cef_navigate _zan_cef_back _zan_cef_forward \
  _zan_cef_reload _zan_cef_stop _zan_cef_can_go_back _zan_cef_can_go_forward \
  _zan_cef_is_loading _zan_cef_nav_seq _zan_cef_last_status _zan_cef_last_error_code \
  _zan_cef_url _zan_cef_title _zan_cef_execute_js _zan_cef_cdp_send \
  _zan_cef_set_popup_policy _zan_cef_take_popup_url _zan_cef_cdp_pending \
  _zan_cef_cdp_dropped _zan_cef_cdp_attached _zan_cef_cdp_take \
  _zan_game_sprite_handle _zan_game_bake_sprite _zan_game_sprite_batch \
  _zan_game_mesh_create _zan_game_draw3d \
  _zan_gui_internal_metal_supported _zan_gui_internal_metal_install _zan_gui_internal_metal_drop_present \
  _zan_image_width _zan_image_height _zan_image_evict _zan_image_mem_report \
  _zan_image_load_mem _zan_image_load_mem_bytes _zan_image_load_svg \
  _zan_image_get _zan_image_register_argb \
  _zan_audio_open _zan_audio_close _zan_audio_is_open \
  _zan_audio_driver_name _zan_audio_last_error _zan_audio_volume \
  _zan_audio_set_volume _zan_audio_active_voices _zan_audio_stop_all \
  _zan_audio_load_wav _zan_audio_load_ogg _zan_audio_load_wav_mem \
  _zan_audio_load_ogg_mem _zan_audio_free_clip _zan_audio_clip_duration_ms \
  _zan_audio_clip_channels _zan_audio_clip_frequency _zan_audio_play \
  _zan_audio_voice_playing _zan_audio_voice_stop _zan_audio_voice_set_gain \
  _zan_hotkey_register _zan_hotkey_unregister _zan_hotkey_next_event \
  _zan_mouse_is_down _zan_keyboard_is_down _zan_keyboard_is_toggled _zan_keyboard_post \
  _zan_hook_install _zan_hook_uninstall _zan_hook_next_event \
  _zan_gui_webview_create _zan_gui_webview_destroy _zan_gui_webview_set_frame \
  _zan_gui_webview_set_visible _zan_gui_webview_set_clip _zan_gui_webview_navigate \
  _zan_gui_webview_load_html _zan_gui_webview_back _zan_gui_webview_forward \
  _zan_gui_webview_reload _zan_gui_webview_stop _zan_gui_webview_can_go_back \
  _zan_gui_webview_can_go_forward _zan_gui_webview_is_loading _zan_gui_webview_nav_seq \
  _zan_gui_webview_last_status _zan_gui_webview_get_url _zan_gui_webview_get_title \
  _zan_gui_webview_last_request _zan_gui_webview_eval _zan_gui_webview_get_cookies \
  _zan_gui_webview_set_cookie _zan_gui_webview_clear_cookies _zan_gui_webview_add_handler \
  _zan_gui_webview_remove_handler _zan_gui_webview_take_message _zan_gui_webview_msg_pending \
  _zan_gui_webview_msg_dropped _zan_gui_webview_add_script _zan_gui_webview_add_style \
  _zan_gui_webview_remove_scripts _zan_gui_webview_eval_async _zan_gui_webview_clear_data \
  _zan_gui_webview_set_zoom _zan_gui_webview_set_devtools_enabled _zan_gui_webview_set_context_menu_enabled \
  __zan_decimal_add __zan_decimal_sub __zan_decimal_mul __zan_decimal_div __zan_decimal_rem \
  __zan_decimal_neg __zan_decimal_cmp __zan_decimal_from_int64 __zan_decimal_from_uint64 \
  __zan_decimal_from_double __zan_decimal_to_int64 __zan_decimal_to_double \
  __zan_decimal_to_string __zan_decimal_parse \
  _zan_hw_has_popcnt _zan_hw_has_lzcnt _zan_hw_has_sse42 _zan_hw_has_avx2 \
  _zan_hw_has_aesni _zan_hw_has_vaes _zan_hw_has_neon _zan_hw_has_shani \
  _zan_hw_has_pclmul _zan_hw_arm_aes _zan_hw_arm_sha1 _zan_hw_arm_sha2 \
  _zan_hw_arm_pmull _zan_hw_arm_sha512 _zan_hw_arm_sm3 _zan_hw_arm_sm4 \
  _zan_hw_arm_crc32 _zan_cpu_feature _zan_hw_kat_state \
  _zan_hw_sha256 _zan_hw_sha1 _zan_hw_sha512 _zan_hw_sm3 \
  _zan_hw_sm4_cbc_encrypt _zan_hw_sm4_cbc_decrypt \
  _zan_hw_aes_cbc_encrypt _zan_hw_aes_cbc_decrypt \
  _zan_hw_aes_ecb_block _zan_hw_aes_ctr_crypt \
  _zan_hw_ghash_block _zan_hw_ghash_update \
  _zan_hw_aes_gcm_init _zan_hw_aes_gcm_encrypt_ctx _zan_hw_aes_gcm_decrypt_ctx \
  _zan_hw_aes_gcm_encrypt _zan_hw_aes_gcm_decrypt \
  _zan_hw_rsa_mod_pow _zan_hw_rsa_crt \
  _zan_hw_x25519 _zan_hw_crc32c_update \
  _zan_hw_base64_encode _zan_hw_base64_decode \
  _zan_hw_json_skip_whitespace _zan_hw_json_scan_string \
  _zan_hw_pixel_blend_over _zan_hw_pixel_swap_rb _zan_hw_pixel_fill_rect \
  _zan_hw_pixel_resample_bilinear_row \
  _zan_hw_aes_encrypt _zan_hw_aes_encrypt_last \
  _zan_hw_aes_decrypt _zan_hw_aes_decrypt_last \
  _zan_hw_aes_keygenassist _zan_hw_aes_imc \
  _zan_hw_vec128_xor _zan_hw_vec128_load _zan_hw_vec128_store \
  _SQLAllocHandle _SQLSetEnvAttr _SQLDriverConnect _SQLExecDirect \
  _SQLPrepare _SQLBindParameter _SQLExecute _SQLNumResultCols \
  _SQLDescribeCol _SQLFetch _SQLGetData _SQLRowCount _SQLFreeHandle \
  _SQLDisconnect _SQLFreeStmt _SQLGetDiagRec \
  _PQconnectdb _PQstatus _PQfinish _PQerrorMessage _PQexec _PQresultStatus \
  _PQclear _PQntuples _PQnfields _PQfname _PQgetvalue _PQgetisnull \
  _PQexecParams _PQcmdTuples \
  _PQconnectStart _PQconnectPoll _PQconsumeInput _PQflush _PQgetResult \
  _PQisBusy _PQsendQuery _PQsendQueryParams _PQsetnonblocking _PQsocket \
# zanstubs_rest.o 在 ZAN_RT_CORE_ZAN 下已完全由 runtime_core.zan 替代，全局符号清零 (Zero C Remainder)
# 仅保留空目标文件作构建账面兼容
cc -DZAN_RT_CORE_ZAN \
  -I/Users/qq/Desktop/zanlang/zan-lang/src/common \
  -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZIP_APIS -DMINIZ_NO_STDIO \
  -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_WRITERS \
  -c -o "$ROOT/crt-transition/zanstubs_rest.o" \
  "$ROOT/crt-transition/zanstubs.c"
cc -DZAN_RT_CORE_ZAN -c -o "$ROOT/crt-transition/zanhost_rest.o" \
  "$ROOT/crt-transition/zanhost.c"
cc -c -o "$ROOT/crt-transition/zanstubs_full.o" "$ROOT/crt-transition/zanstubs.c"
cc -c -o "$ROOT/crt-transition/zanhost_full.o" "$ROOT/crt-transition/zanhost.c"
# 生成 Windows PE-COFF 跨平台单对象运行时并进行 COFF 结构验证
COFF_OUT="${OUT%.o}.obj"
"$SEED" "$COFF_OUT" crt-transition/runtime_core.zan
if [ -f "$ROOT/scripts/coffcheck.py" ]; then
  python3 "$ROOT/scripts/coffcheck.py" "$COFF_OUT"
fi

printf 'Zan runtime object: %s (PE-COFF: %s)\nC remainder:        %s %s\nC baseline:         %s %s\n' \
  "$OUT" "$COFF_OUT" \
  "$ROOT/crt-transition/zanstubs_rest.o" "$ROOT/crt-transition/zanhost_rest.o" \
  "$ROOT/crt-transition/zanstubs_full.o" "$ROOT/crt-transition/zanhost_full.o"
