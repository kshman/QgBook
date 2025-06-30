#pragma once

#include "defs.h"

/**
 * @brief 책 이름 바꾸기 콜백 함수 타입
 * @param sender 호출자
 * @param filename 새 파일 이름
 * @param reopen 다시 열기 플래그
 */
typedef void (*RenameCallback)(gpointer sender, const char* filename, bool reopen);

/**
 * @brief 쪽 선택 콜백 함수 타입
 * @param sender 호출자
 * @param page 선택된 페이지 번호
 */
typedef void (*PageSelectCallback)(gpointer sender, int page);

/**
 * @brief 책 이동 콜백 함수 타입
 * @param sender 호출자
 * @param directory 이동할 디렉토리 경로
 */
typedef void (*MoveCallback)(gpointer sender, const char* directory);

/**
 * @brief 메시지 박스 콜백 함수 타입
 * @param sender 호출자
 * @param result true면 '예' 또는 '네', false면 '아니오' 또는 '취소' 선택
 */
typedef void (*MesgBoxCallback)(gpointer sender, bool result);

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


// 프로그램 잠금
extern bool doumi_lock_program(void);
extern void doumi_unlock_program(void);

// 검사
extern bool doumi_is_image_file(const char* filename);
extern bool doumi_is_archive_zip(const char* filename);
extern bool doumi_is_file_readonly(const char* path);
extern void doumi_get_extension(const char* filename, char* extension, size_t size);
extern bool doumi_detect_image_info(GBytes* data, ImageInfo* info);

// 변환
extern bool doumi_atob(const char* str);
extern gchar* doumi_string_strip(const char* s);
extern int doumi_format_size_friendly(guint64 size, char* value, size_t value_size);

extern int doumi_encode(const char* input, char* value, size_t value_size);
extern int doumi_decode(const char* input, char* value, size_t value_size);
extern int doumi_base64_encode(const char* input, char* value, size_t value_size);
extern int doumi_base64_decode(const char* input, char* value, size_t value_size);
extern char* doumi_huffman_encode(const char* input);
extern char* doumi_huffman_decode(const char* input);

// GTK
extern const char* doumi_resource_path(const char* path);
extern const char* doumi_resource_path_format(const char* fmt, ...);
extern char* doumi_load_resource_text(const char* resource_path, gsize* out_length);
extern GdkPixbuf* doumi_load_gdk_pixbuf(const void* buffer, size_t size);
extern GdkTexture* doumi_load_gdk_texture(const void* buffer, size_t size);
extern GdkTexture* doumi_texture_from_surface(cairo_surface_t* surface);
extern GtkFileFilter* doumi_file_filter_all(void);
extern GtkFileFilter* doumi_file_filter_image(void);
extern GtkFileFilter* doumi_file_filter_zip(void);
extern GFileType doumi_get_file_type_from(GFile* file);
extern GdkMonitor* doumi_get_primary_monitor(void);
extern bool doumi_get_primary_monitor_dimension(int* width, int* height);

extern void doumi_mesg_ok_show_async(GtkWindow* parent, const char* text, const char* detail, MesgBoxCallback callback, gpointer user_data);
extern void doumi_mesg_yesno_show_async(GtkWindow* parent, const char* text, const char* detail, MesgBoxCallback callback, gpointer user_data);
