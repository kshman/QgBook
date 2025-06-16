#pragma once

#include "book.h"

// 설정 키
typedef enum ConfigKeys
{
	CONFIG_NONE = 0, // 설정 없음
	// 실행
	CONFIG_RUN_COUNT, // 실행 횟수
	CONFIG_RUN_DURATION, // 실행 시간 (초 단위)
	// 윈도우
	CONFIG_WINDOW_WIDTH, // 윈도우 너비
	CONFIG_WINDOW_HEIGHT, // 윈도우 높이
	// 일반
	CONFIG_GENERAL_RUN_ONCE, // 한 번만 실행
	CONFIG_GENERAL_ESC_EXIT, // ESC 키로 종료
	CONFIG_GENERAL_CONFIRM_DELETE, // 책 삭제 확인
	CONFIG_GENERAL_MAX_PAGE_CACHE, // 최대 캐시 크기
	CONFIG_GENERAL_EXTERNAL_RUN, // 외부 프로그램 실행
	CONFIG_GENERAL_RELOAD_AFTER_EXTERNAL, // 외부 프로그램 실행 후 재시작
	// 마우스
	CONFIG_MOUSE_DOUBLE_CLICK_FULLSCREEN, // 더블 클릭으로 전체 화면 전환
	CONFIG_MOUSE_CLICK_PAGING, // 클릭으로 페이지 넘기기
	// 보기
	CONFIG_VIEW_ZOOM, // 보기 확대
	CONFIG_VIEW_MODE, // 보기 모드
	CONFIG_VIEW_QUALITY, // 보기 품질
	// 보안
	CONFIG_SECURITY_USE_PASS, // 비밀번호 보호
	CONFIG_SECURITY_PASS_CODE, // 비밀번호 값
	CONFIG_SECURITY_PASS_USAGE, // 비밀번호 용도
	// 파일
	CONFIG_FILE_LAST_DIRECTORY, // 마지막으로 열었던 디렉토리
	CONFIG_FILE_LAST_FILE, // 마지막으로 열었던 파일
	CONFIG_FILE_REMEMBER, // 기억해둘 파일 이름
	//
	CONFIG_MAX_VALUE, // 최대 값 (마지막 값은 반드시 이 값을 사용해야 함)
} ConfigKeys;

// 이동 위치의 별명과 디렉토리
typedef struct ConfigMove
{
	char* alias; // 별명
	char* folder; // 디렉토리
} ConfigMove;

extern bool configs_init(void);
extern void configs_load_cache(void);
extern void configs_dispose(void);

extern const char* configs_lookup_lang(const char* key);
#define _(x) configs_lookup_lang(x)

extern bool configs_get_string(ConfigKeys name, char* value, size_t value_size, bool cache_only);
extern bool configs_get_bool(ConfigKeys name, bool cache_only);
extern gint32 configs_get_int(ConfigKeys name, bool cache_only);
extern gint64 configs_get_long(ConfigKeys name, bool cache_only);

extern void configs_set_string(ConfigKeys name, const char* value, bool cache_only);
extern void configs_set_bool(ConfigKeys name, bool value, bool cache_only);
extern void configs_set_int(ConfigKeys name, gint32 value, bool cache_only);
extern void configs_set_long(ConfigKeys name, gint64 value, bool cache_only);

extern ConfigMove* configs_get_moves(int* ret_count);
extern bool configs_set_move(const char* folder, const char* alias);
extern bool configs_delete_move(const char* folder);
extern void configs_free_moves(ConfigMove* moves, int count);

extern uint64_t configs_get_actual_max_page_cache(void);


// 도우미
extern bool doumi_is_image_file(const char* filename);
extern bool doumi_is_archive_zip(const char* filename);
extern bool doumi_atob(const char* str);
extern bool doumi_is_file_readonly(const char* path);
extern int doumi_encode(const char* input, char* value, size_t value_size);
extern int doumi_decode(const char* input, char* value, size_t value_size);
extern int doumi_base64_encode(const char* input, char* value, size_t value_size);
extern int doumi_base64_decode(const char* input, char* value, size_t value_size);
extern char* doumi_huffman_encode(const char* input);
extern char* doumi_huffman_decode(const char* input);

extern const char* doumi_resource_path(const char* path);
extern const char* doumi_resource_path_format(const char* fmt, ...);
extern char* doumi_load_resource_text(const char* resource_path, gsize* out_length);
extern GdkPixbuf* doumi_load_gdk_pixbuf(const void* buffer, size_t size);
extern GdkTexture* doumi_load_gdk_texture(const void* buffer, size_t size);
extern GdkTexture* doumi_texture_from_surface(cairo_surface_t* surface);

extern bool doumi_lock_program(void);
extern void doumi_unlock_program(void);

extern void doumi_mesg_box(GtkWindow* parent, const char* text, const char* detail);


// 리소스
typedef enum ResKeys
{
	RES_PIX_NO_IMAGE,
	RES_PIX_HOUSEBARI,
	RES_ICON_DIRECTORY,
	RES_ICON_MENUS,
	RES_ICON_MOVE,
	RES_ICON_PAINTING,
	RES_ICON_PURUTU,
	RES_ICON_RENAME,
	RES_ICON_VIEW_MODE_FIT,
	RES_ICON_VIEW_MODE_L2R,
	RES_ICON_VIEW_MODE_R2L,
	RES_MAX_VALUE,
} ResKeys;

extern GdkTexture* res_get_texture(ResKeys key);
extern GdkTexture* res_get_view_mode_texture(ViewMode mode);
