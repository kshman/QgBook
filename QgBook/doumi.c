#include "pch.h"
#ifndef _WIN32
#include <sys/file.h>
#include <fcntl.h>
#endif
#include "configs.h"
#include "doumi.h"

// 잠금 뮤텍스
#ifdef _WIN32
static HANDLE doumi_lock;
#else
static int doumi_fd;
#endif

// 잠금 만들기
bool doumi_lock_program(void)
{
#ifdef _WIN32
	doumi_lock = CreateMutexA(NULL, FALSE, "ksh.qg.book.unique");
	if (doumi_lock == NULL)
		return false;
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		// 다른데서 뮤텍스를 만드럿네
		CloseHandle(doumi_lock);
		doumi_lock = NULL;
		return false;
	}
	return true;
#else
	doumi_fd = open("/tmp/qgbook.lock", O_CREAT | O_RDWR, 0666);
	if (doumi_fd < 0)
		return false;
	// 파일 잠금으로 처리
	if (flock(doumi_fd, LOCK_EX | LOCK_NB) < 0)
	{
		// 실행중
		close(doumi_fd);
		doumi_fd = 0;
		return false;
	}
	return true;
#endif
}

// 잠금 풀기
void doumi_unlock_program(void)
{
#ifdef _WIN32
	g_return_if_fail(doumi_lock != NULL);
	CloseHandle(doumi_lock);
	doumi_lock = NULL;
#else
	g_return_if_fail(doumi_fd != 0);
	close(doumi_fd);
	doumi_fd = 0;
#endif
}

// 이미지 파일인가 확장자로 검사
bool doumi_is_image_file(const char* filename)
{
	if (!filename) return false;
	const char* ext = strrchr(filename, '.');
	if (!ext) return false;
	ext++; // '.' 다음부터
	if (g_ascii_strcasecmp(ext, "jpg") == 0 ||
		g_ascii_strcasecmp(ext, "webp") == 0 ||
		g_ascii_strcasecmp(ext, "png") == 0 ||
		g_ascii_strcasecmp(ext, "jpeg") == 0 ||
		g_ascii_strcasecmp(ext, "gif") == 0 ||
		g_ascii_strcasecmp(ext, "bmp") == 0 ||	// 윈도우에서는 BMP를 지원하지 않음
		g_ascii_strcasecmp(ext, "tiff") == 0)
		return true; // 비교 순서는 자주 쓰는 순서로
	return false;

	// 원래 아래 코드로 지원하는 이미지를 얻어와야 한다
	//GSList *formats = gdk_pixbuf_get_formats();
	//for (GSList *l = formats; l != NULL; l = l->next) {
	//	GdkPixbufFormat *fmt = l->data;
	//	const gchar *name = gdk_pixbuf_format_get_name(fmt);
	//	const gchar *desc = gdk_pixbuf_format_get_description(fmt);
	//	g_print("지원 포맷: %s (%s)\n", name, desc);
	//}
	//g_slist_free(formats);
}

// ZIP 압축인가 확장자로 검사
bool doumi_is_archive_zip(const char* filename)
{
	if (!filename) return false;
	const char* ext = strrchr(filename, '.');
	if (!ext) return false;
	ext++; // '.' 다음부터
	if (g_ascii_strcasecmp(ext, "zip") == 0 ||
		g_ascii_strcasecmp(ext, "cbz") == 0)
		return true;
	return false;
}

// 읽기 전용 파일인가 확인
bool doumi_is_file_readonly(const char* path)
{
	GFile* file = g_file_new_for_path(path);
	GError* err = NULL;
	GFileInfo* info = g_file_query_info(file, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE, G_FILE_QUERY_INFO_NONE, NULL, &err);
	bool ret = true;
	if (info)
	{
		bool can_write = g_file_info_get_attribute_boolean(info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE);
		ret = !can_write; // 쓸 수 있으면 읽기 전용 아님
		g_object_unref(info);
	}
	g_object_unref(file);
	if (err) g_error_free(err);
	return ret;
}

// 문자열을 불린으로
bool doumi_atob(const char* str)
{
	if (str == NULL) return false;
	if (g_ascii_strcasecmp(str, "1") == 0 ||
		g_ascii_strcasecmp(str, "true") == 0 ||
		g_ascii_strcasecmp(str, "yes") == 0 ||
		g_ascii_strcasecmp(str, "on") == 0 ||
		g_ascii_strcasecmp(str, "cham") == 0)
		return true;
	return false;
}

// 문자열 스트립
// 반환값은 g_free로 해제해야 함
gchar* doumi_string_strip(const char* s)
{
	if (!s) return g_strdup("");
	while (g_ascii_isspace(*s)) s++;
	const char* end = s + strlen(s);
	while (end > s && g_ascii_isspace(*(end - 1))) end--;
	return g_strndup(s, end - s);
}

// 바이트 크기를 사람이 읽기 좋은 문자열로 변환
int doumi_format_size_friendly(guint64 size, char* value, size_t value_size)
{
	if (size == 0)
		return g_snprintf(value, (gulong)value_size, "0 B");

	const char* units[] = { "B", "KB", "MB", "GB", "TB", "PB", "EB" };
	int i = 0;
	double d_size = (double)size;

	while (d_size >= 1024.0 && i < (int)G_N_ELEMENTS(units) - 1)
	{
		d_size /= 1024.0;
		i++;
	}

	if (i == 0)
		return g_snprintf(value, (gulong)value_size, "%.0f %s", d_size, units[i]);
	return g_snprintf(value, (gulong)value_size, "%.1f %s", d_size, units[i]);
}

// 문자열을 코드 문자열로 인코딩
// 반환값은 변환된 문자열의 길이, value와 value_size가 유효하지 않으면 필요한 버퍼의 크기를 반환
// 그 밖에 오류는 -1을 반환
int doumi_encode(const char* input, char* value, size_t value_size)
{
	g_return_val_if_fail(input, -1);

	const size_t len = strlen(input);
	const unsigned char* bs = (const unsigned char*)input;

	if (value == NULL || value_size == 0)
		return (int)len * 2 + 1;

	char* p = value;
	size_t written = 0;
	for (size_t i = 0; i < len; ++i)
	{
		if (written + 2 >= value_size) // 남은 공간이 2글자(16진수) + 널 종료자보다 작으면 중단
			break;
		uint8_t inv = 255 - bs[i];
		g_snprintf(p, 3, "%02X", inv);
		p += 2;
		written += 2;
	}
	*p = '\0';
	return (int)written;
}

// 16진수 문자를 숫자로
static uint8_t hex_char_to_byte(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return 0; // 잘못된 문자
}

// 코드 문자열을 문자열로 디코딩
// 반환값: 성공시 0, 실패시 -1, value와 value_size가 NULL/0이면 필요한 버퍼 크기 반환
int doumi_decode(const char* input, char* value, size_t value_size)
{
	g_return_val_if_fail(input, -1);

	size_t encoded_len = strlen(input);
	if (encoded_len % 2 != 0)
		return -1; // 잘못된 길이

	size_t decoded_len = encoded_len / 2;
	if (value == NULL || value_size == 0)
		return (int)decoded_len + 1;

	size_t max_write = value_size - 1; // 널 종료자 공간 확보
	size_t i;
	for (i = 0; i < decoded_len && i < max_write; ++i)
	{
		uint8_t hi = hex_char_to_byte(input[i * 2]);
		uint8_t lo = hex_char_to_byte(input[i * 2 + 1]);
		uint8_t inv = (uint8_t)((hi << 4) | lo);
		value[i] = (char)(255 - inv);
	}
	value[i] = '\0';
	return (int)i;
}

// 베이스64 테이블
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 베이스64 인코딩
int doumi_base64_encode(const char* input, char* value, size_t value_size)
{
	if (!input)
		return -1;

	size_t in_len = strlen(input);
	size_t out_len = ((in_len + 2) / 3) * 4;
	if (value == NULL || value_size == 0)
		return (int)out_len + 1;

	size_t written = 0;
	size_t i = 0;
	while (i < in_len && written + 4 < value_size)
	{
		uint32_t octet_a = i < in_len ? (unsigned char)input[i++] : 0;
		uint32_t octet_b = i < in_len ? (unsigned char)input[i++] : 0;
		uint32_t octet_c = i < in_len ? (unsigned char)input[i++] : 0;

		uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

		if (written + 4 >= value_size)
			break;

		value[written++] = base64_table[(triple >> 18) & 0x3F];
		value[written++] = base64_table[(triple >> 12) & 0x3F];
		// 아래 두줄 bugprone-narrowing-conversions 때문에 캐스팅한거임
		value[written++] = (char)((i - 2 <= in_len) ? base64_table[(triple >> 6) & 0x3F] : '=');
		value[written++] = (char)((i - 1 <= in_len) ? base64_table[triple & 0x3F] : '=');
	}
	if (written < value_size)
		value[written] = '\0';
	return (int)written;
}

// 베이스64 문자 -> 값 변환
static int base64_char_to_val(char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

// 베이스64 디코딩
int doumi_base64_decode(const char* input, char* value, size_t value_size)
{
	if (!input)
		return -1;

	size_t encoded_len = strlen(input);
	if (encoded_len % 4 != 0)
		return -1;

	size_t out_len = (encoded_len / 4) * 3;
	if (encoded_len >= 4)
	{
		if (input[encoded_len - 1] == '=') out_len--;
		if (input[encoded_len - 2] == '=') out_len--;
	}

	if (value == NULL || value_size == 0)
		return (int)out_len + 1;

	size_t written = 0;
	for (size_t i = 0; i < encoded_len; i += 4)
	{
		int v[4];
		for (int j = 0; j < 4; ++j)
		{
			v[j] = base64_char_to_val(input[i + j]);
			if (input[i + j] == '=') v[j] = 0;
			else if (v[j] < 0) return -1;
		}
		if (written < value_size - 1)
			value[written++] = (char)((v[0] << 2) | (v[1] >> 4));
		if (input[i + 2] != '=' && written < value_size - 1)
			value[written++] = (char)(((v[1] & 0xF) << 4) | (v[2] >> 2));
		if (input[i + 3] != '=' && written < value_size - 1)
			value[written++] = (char)(((v[2] & 0x3) << 6) | v[3]);
	}
	if (written < value_size)
		value[written] = '\0';
	return (int)written;
}

// 압축 후 base64 인코딩, 결과를 동적 할당하여 반환
char* doumi_huffman_encode(const char* input)
{
	g_return_val_if_fail(input != NULL, NULL);

	size_t in_len = strlen(input);
	uLongf comp_bound = compressBound((uLong)in_len);
	guint8* comp_buf = g_new(guint8, comp_bound);
	if (!comp_buf) return NULL;

	int res = compress(comp_buf, &comp_bound, (const Bytef*)input, (uLong)in_len);
	if (res != Z_OK)
	{
		g_free(comp_buf);
		return NULL;
	}

	size_t b64_size = ((comp_bound + 2) / 3) * 4 + 1;
	char* b64_buf = g_new(char, b64_size);
	if (!b64_buf)
	{
		g_free(comp_buf);
		return NULL;
	}

	int b64len = doumi_base64_encode((const char*)comp_buf, b64_buf, b64_size);
	g_free(comp_buf);

	if (b64len < 0)
	{
		g_free(b64_buf);
		return NULL;
	}

	return b64_buf;
}

// base64 디코딩 후 압축 해제, 결과를 동적 할당하여 반환
char* doumi_huffman_decode(const char* input)
{
	g_return_val_if_fail(input != NULL, NULL);

	// base64 디코딩
	int comp_size = doumi_base64_decode(input, NULL, 0);
	if (comp_size <= 0) return NULL;
	guint8* comp_buf = g_new(guint8, comp_size);
	if (!comp_buf) return NULL;
	if (doumi_base64_decode(input, (char*)comp_buf, comp_size) < 0)
	{
		g_free(comp_buf);
		return NULL;
	}

	// 압축 해제 버퍼 크기 추정 (초기값: 압축 데이터의 4배, 최소 256)
	uLongf out_size = comp_size * 4;
	if (out_size < 256) out_size = 256;
	char* out_buf = g_new(char, out_size);

	int res = uncompress((Bytef*)out_buf, &out_size, comp_buf, comp_size - 1); // -1: 널 종료자 제외
	if (res == Z_BUF_ERROR)
	{
		// 버퍼가 부족하면 더 크게 재시도
		out_size = comp_size * 16;
		g_free(out_buf);
		out_buf = g_new(char, out_size);
		res = uncompress((Bytef*)out_buf, &out_size, comp_buf, comp_size - 1);
	}
	g_free(comp_buf);

	if (res != Z_OK)
	{
		g_free(out_buf);
		return NULL;
	}

	// 널 종료 보장
	out_buf[out_size] = '\0';
	return out_buf;
}

// 리소스 이름 만들기용 TLS
static GPrivate resource_name_tls = G_PRIVATE_INIT(g_free);

// 리소스 이름 만들기
const char* doumi_resource_path(const char* path)
{
	char* data = g_str_has_prefix(path, "/") ? g_strdup(path) : g_strconcat("/qgbook/data/", path, NULL);
	g_private_set(&resource_name_tls, data);
	return data;
}

// 리소스 이름 만들기 + 포맷
const char* doumi_resource_path_format(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	char* data = g_strdup_vprintf(fmt, args); // NOLINT(clang-diagnostic-format-nonliteral)
	va_end(args);
	if (!g_str_has_prefix(data, "/"))
	{
		char* tmp = g_strconcat("/qgbook/data/", data, NULL);
		g_free(data);
		data = tmp;
	}
	g_private_set(&resource_name_tls, data);
	return data;
}

// 리소스에서 텍스트 파일 읽기. 반환값은 g_free로 해제할 것
char* doumi_load_resource_text(const char* resource_path, gsize* out_length)
{
	GError* err = NULL;
	GBytes* bytes = g_resources_lookup_data(resource_path, G_RESOURCE_LOOKUP_FLAGS_NONE, &err);
	if (!bytes)
	{
		g_log("DOUMI", G_LOG_LEVEL_ERROR, "Text resource: %s", err->message);
		g_clear_error(&err);
		return NULL;
	}
	gsize size = 0;
	const gchar* data = g_bytes_get_data(bytes, &size);
	gchar* result = g_strndup(data, size);
	if (out_length) *out_length = size;
	g_bytes_unref(bytes);
	return result;
}

// 픽스맵 만들기
GdkPixbuf* doumi_load_gdk_pixbuf(const void* buffer, size_t size)
{
	GInputStream* stream = g_memory_input_stream_new_from_data(buffer, (gssize)size, NULL);
	GError* err = NULL;
	GdkPixbuf* pix = gdk_pixbuf_new_from_stream(stream, NULL, &err);
	g_object_unref(stream);
	if (!pix)
	{
		g_log("DOUMI", G_LOG_LEVEL_ERROR, "Pixbuf resource: %s", err->message);
		g_clear_error(&err);
	}
	return pix;
}

// 텍스쳐 만들기
GdkTexture* doumi_load_gdk_texture(const void* buffer, size_t size)
{
	GBytes* bytes = g_bytes_new(buffer, size);
	GError* err = NULL;
	GdkTexture* tex = gdk_texture_new_from_bytes(bytes, &err);
	g_bytes_unref(bytes);
	if (!tex)
	{
		g_log("DOUMI", G_LOG_LEVEL_ERROR, "Texture resource: %s", err->message);
		g_clear_error(&err);
	}
	return tex;
}

// 서피스로 GdkTexture 만들기
GdkTexture* doumi_texture_from_surface(cairo_surface_t* surface)
{
	g_return_val_if_fail(cairo_surface_get_type(surface) == CAIRO_SURFACE_TYPE_IMAGE, NULL);
	g_return_val_if_fail(cairo_image_surface_get_format(surface) == CAIRO_FORMAT_ARGB32, NULL);
	g_return_val_if_fail(cairo_image_surface_get_width(surface) > 0, NULL);
	g_return_val_if_fail(cairo_image_surface_get_height(surface) > 0, NULL);

	GBytes* bytes = g_bytes_new_with_free_func(
		cairo_image_surface_get_data(surface),
		(gsize)cairo_image_surface_get_height(surface)
		* cairo_image_surface_get_stride(surface),
		(GDestroyNotify)cairo_surface_destroy,
		cairo_surface_reference(surface));

	GdkTexture* texture = gdk_memory_texture_new(
		cairo_image_surface_get_width(surface),
		cairo_image_surface_get_height(surface),
		GDK_MEMORY_A8R8G8B8,
		bytes,
		cairo_image_surface_get_stride(surface));

	g_bytes_unref(bytes);

	return texture;
}

// 파일 필터 - 모든 파일
GtkFileFilter* doumi_file_filter_all(void)
{
	GtkFileFilter* filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, _("All files"));
	gtk_file_filter_add_pattern(filter, "*");
	return filter;
}

// 파일 필터 - 이미지 파일
GtkFileFilter* doumi_file_filter_image(void)
{
	GtkFileFilter* filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, _("Image files"));
	gtk_file_filter_add_pattern(filter, "*.jpg");
	gtk_file_filter_add_pattern(filter, "*.jpeg");
	gtk_file_filter_add_pattern(filter, "*.png");
	gtk_file_filter_add_pattern(filter, "*.gif");
	gtk_file_filter_add_pattern(filter, "*.bmp");
	gtk_file_filter_add_pattern(filter, "*.webp");
	gtk_file_filter_add_pattern(filter, "*.tiff");
	gtk_file_filter_add_pattern(filter, "*.tif");
#ifndef _WIN32
	gtk_file_filter_add_mime_type(filter, "image/jpeg");   // .jpg, .jpeg
	gtk_file_filter_add_mime_type(filter, "image/png");    // .png
	gtk_file_filter_add_mime_type(filter, "image/gif");    // .gif
	gtk_file_filter_add_mime_type(filter, "image/bmp");    // .bmp
	gtk_file_filter_add_mime_type(filter, "image/webp");   // .webp
	gtk_file_filter_add_mime_type(filter, "image/tiff");   // .tiff, .tif
#endif
	return filter;
}

// 파일 필터 - ZIP 압축 파일
GtkFileFilter* doumi_file_filter_zip(void)
{
	GtkFileFilter* filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, _("ZIP archives"));
	gtk_file_filter_add_pattern(filter, "*.zip");
	gtk_file_filter_add_pattern(filter, "*.cbz");
#ifndef _WIN32
	gtk_file_filter_add_mime_type(filter, "application/zip"); // .zip
	gtk_file_filter_add_mime_type(filter, "application/x-zip"); // .zip
	gtk_file_filter_add_mime_type(filter, "application/x-cbz"); // .cbz
	gtk_file_filter_add_mime_type(filter, "application/vnd.comicbook+zip"); // .cbz
#endif
	return filter;
}

// GFile로 이 파일이 어떤건지 알아보기
// 파일이 없어도 G_FILE_TYPE_UNKNOWN 반환
GFileType doumi_get_file_type_from_gfile(GFile* file)
{
	g_return_val_if_fail(file != NULL, G_FILE_TYPE_UNKNOWN);
	if (!g_file_query_exists(file, NULL))
		return G_FILE_TYPE_UNKNOWN; // 파일이 존재하지 않음
	GFileInfo* info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_TYPE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (!info) return G_FILE_TYPE_UNKNOWN;
	GFileType type = g_file_info_get_file_type(info);
	g_object_unref(info);
	return type;
}

// 첫번째 모니터 얻기
// 얻은 모니터는 g_object_unref로 해제해야 함
GdkMonitor* doumi_get_primary_monitor(void)
{
	GdkDisplay* display = gdk_display_get_default();
	if (!display) return NULL;
	GListModel* monitors = gdk_display_get_monitors(display);
	if (!monitors || g_list_model_get_n_items(monitors) == 0)
		return NULL;
	return g_list_model_get_item(monitors, 0);
}

// 첫번째 모니터의 크기 얻기
bool doumi_get_primary_monitor_dimension(int* width, int* height)
{
	GdkMonitor* monitor = doumi_get_primary_monitor();
	if (!monitor) return false;
	GdkRectangle geometry;
	gdk_monitor_get_geometry(monitor, &geometry);
	if (width) *width = geometry.width;
	if (height) *height = geometry.height;
	g_object_unref(monitor);
	return true;
}

// 메시지 박스 선택 콜백 구조체
struct mesg_box_sync
{
	GMainLoop* loop;
	int result;
};

// 메시지 박스 선택 콜백
static void mesg_box_on_choose(GObject* source_object, GAsyncResult* res, gpointer user_data)
{
	GtkAlertDialog* dialog = GTK_ALERT_DIALOG(source_object);
	int response = gtk_alert_dialog_choose_finish(dialog, res, NULL);
	struct mesg_box_sync* sync = user_data;
	sync->result = response;
	g_main_loop_quit(sync->loop);
}

// 메시지 박스
bool doumi_mesg_box(GtkWindow* parent, const char* text, const char* detail, bool false_ok_true_yn)
{
	GtkAlertDialog* dialog = gtk_alert_dialog_new("");
	gtk_alert_dialog_set_modal(dialog, true);
	gtk_alert_dialog_set_message(dialog, text);
	gtk_alert_dialog_set_detail(dialog, detail);

	if (false_ok_true_yn)
	{
		const char* yesno_buttons[] = { _("Yes"), _("No"), NULL };
		gtk_alert_dialog_set_buttons(dialog, yesno_buttons);
	}
	else
	{
		const char* ok_buttons[] = { _("OK"), NULL };
		gtk_alert_dialog_set_buttons(dialog, ok_buttons);
	}

	gtk_alert_dialog_set_default_button(dialog, 0);

	// 비동기 콜백 대신 동기적으로 결과를 받기 위한 구조
	struct mesg_box_sync sync = { g_main_loop_new(NULL, FALSE), -1 };

	// 콜백에서 sync.result에 결과 저장
	gtk_alert_dialog_choose(dialog, parent, NULL, mesg_box_on_choose, &sync);
	g_main_loop_run(sync.loop);
	g_main_loop_unref(sync.loop);
	g_object_unref(dialog);

	return sync.result == 0; // OK/Yes: 0, No: 1
}
