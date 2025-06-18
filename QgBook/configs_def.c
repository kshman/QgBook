#include "pch.h"
#include "configs.h"

// 설정 이름을 정의합니다. 이 배열은 설정의 순서를 정의하며, 인덱스는 ConfigKeys입니다.
ConfigDefinition config_defs[CONFIG_MAX_VALUE] =
{
	{ "", "", CACHE_TYPE_UNKNOWN },
	// 실행
	{ "run_count", "0", CACHE_TYPE_LONG },
	{ "run_duration", "0", CACHE_TYPE_DOUBLE },
	// 윈도우
	{ "window_width", "600", CACHE_TYPE_INT },
	{ "window_height", "400", CACHE_TYPE_INT },
	// 일반
	{ "general_run_once", "1", CACHE_TYPE_BOOL },
	{ "general_esc_exit", "1", CACHE_TYPE_BOOL },
	{ "general_confirm_delete", "1", CACHE_TYPE_BOOL },
	{ "general_max_page_cache", "230", CACHE_TYPE_INT },
	{ "general_external_run", "", CACHE_TYPE_STRING },
	{ "general_reload_after_external", "1", CACHE_TYPE_BOOL },
	// 마우스
	{ "mouse_double_click_fullscreen", "0", CACHE_TYPE_BOOL },
	{ "mouse_click_paging", "0", CACHE_TYPE_BOOL },
	// 보기
	{ "view_zoom", "1", CACHE_TYPE_BOOL },
	{ "view_mode", "0", CACHE_TYPE_INT },
	{ "view_quality", "1", CACHE_TYPE_INT },
	// 보안
	{ "security_use_pass", "0", CACHE_TYPE_BOOL },
	{ "security_pass_code", "", CACHE_TYPE_STRING },
	{ "security_pass_usage", "", CACHE_TYPE_STRING },
	// 파일
	{ "file_last_directory", "", CACHE_TYPE_STRING },
	{ "file_last_file", "", CACHE_TYPE_STRING },
	{ "file_remember", "", CACHE_TYPE_STRING },
};
