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

// 문자열을 불린으로
bool doumi_atob(const char* str)
{
	if (str == NULL)
		return false;
	if (g_ascii_strcasecmp(str, "1") == 0 ||
		g_ascii_strcasecmp(str, "true") == 0 ||
		g_ascii_strcasecmp(str, "yes") == 0 ||
		g_ascii_strcasecmp(str, "on") == 0 ||
		g_ascii_strcasecmp(str, "cham") == 0)
		return true;
	return false;
}

// 확장자 가져오기
void doumi_get_extension(const char* filename, char* extension, size_t size)
{
	if (!filename || !extension || size == 0)
	{
		if (extension) *extension = '\0'; // 유효하지 않으면 빈 문자열
		return;
	}
	const char* ext = strrchr(filename, '.');
	if (!ext || ext == filename) // '.'이 없거나 파일 이름이 '.'로 시작하면
	{
		*extension = '\0'; // 빈 문자열
		return;
	}
	ext++; // '.' 다음부터
	if (size > 0)
	{
		g_strlcpy(extension, ext, size); // 확장자 복사
		if (extension[size - 1] == '\0') // 널 종료자 확인
			return;
	}
	else
	{
		// size가 0이면 확장자 길이만큼 반환
		const size_t len = strlen(ext);
		if (len < size) // 버퍼 크기보다 작으면
			g_strlcpy(extension, ext, len + 1); // 널 종료자 포함해서 복사
		else
			*extension = '\0'; // 버퍼가 작으면 빈 문자열
	}
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
		g_ascii_strcasecmp(ext, "bmp") == 0 || // 윈도우에서는 BMP를 지원하지 않음
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
GFileType doumi_get_file_type_from(GFile* file)
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

// 이미지 파일 확인
bool doumi_detect_image_info(GBytes* data, ImageInfo* info)
{
	if (!data || !info)
		return false;

	const guint8* bytes = g_bytes_get_data(data, NULL);
	const gsize size = g_bytes_get_size(data);

	// 구조체 초기화
	memset(info, 0, sizeof(ImageInfo));

	if (size > 20 && bytes[0] == 0xFF && bytes[1] == 0xD8) // JPEG
	{
		for (gsize i = 2; i < size - 8; i++)
		{
			if (bytes[i] == 0xFF && (bytes[i + 1] == 0xC0 || bytes[i + 1] == 0xC2))
			{
				info->height = (bytes[i + 5] << 8) | bytes[i + 6];
				info->width = (bytes[i + 7] << 8) | bytes[i + 8];
				if (info->width && info->height)
				{
					info->type = IMAGE_FILE_TYPE_JPEG;
					goto pos_detected;
				}
				break;
			}
		}
	}
	else if (size >= 24 && !memcmp(bytes, "\x89PNG", 4)) // PNG
	{
		info->width = (bytes[16] << 24) | (bytes[17] << 16) | (bytes[18] << 8) | bytes[19];
		info->height = (bytes[20] << 24) | (bytes[21] << 16) | (bytes[22] << 8) | bytes[23];
		info->type = IMAGE_FILE_TYPE_PNG;
		goto pos_detected;
	}
	else if (size >= 10 && !memcmp(bytes, "GIF", 3)) // GIF
	{
		info->width = bytes[6] | (bytes[7] << 8); // Little endian
		info->height = bytes[8] | (bytes[9] << 8);

		gsize pos = 13; // 6바이트 헤더 + 7바이트 Logical Screen Descriptor

		// Global Color Table 존재 시 건너뜀
		if (bytes[10] & 0x80)
		{
			const int gct_size = 3 * (1 << ((bytes[10] & 0x07) + 1));
			pos += gct_size;
		}

		// GIF 데이터 스트림 파싱으로 애니메이션 검출
		int image_count = 0;
		bool has_graphic_control = false;

		while (pos < size - 1)
		{
			if (bytes[pos] == 0x21) // Extension Introducer
			{
				if (pos + 1 < size)
				{
					const guint8 label = bytes[pos + 1];
					pos += 2;

					if (label == 0xF9) // Graphic Control Extension
					{
						has_graphic_control = true;
						// 블록 크기 (보통 4바이트) + 데이터 + Block Terminator
						if (pos < size && bytes[pos] == 4)
						{
							pos += 5; // 블록 크기(1) + 데이터(4)
							if (pos < size && bytes[pos] == 0x00)
								pos++; // Block Terminator
						}
					}
					else if (label == 0xFE || label == 0x01 || label == 0xFF) // Comment, Plain Text, Application
					{
						// Sub-block 데이터 건너뛰기
						while (pos < size && bytes[pos] != 0x00)
						{
							const guint8 block_size = bytes[pos];
							pos += block_size + 1;
						}
						if (pos < size) pos++; // Block Terminator
					}
				}
			}
			else if (bytes[pos] == 0x2C) // Image Separator
			{
				image_count++;
				pos += 10; // Image Descriptor (10바이트)

				if (pos < size)
				{
					// Local Color Table 존재 시 건너뛰기
					if (bytes[pos - 1] & 0x80)
					{
						const int lct_size = 3 * (1 << ((bytes[pos - 1] & 0x07) + 1));
						pos += lct_size;
					}

					// LZW Minimum Code Size
					if (pos < size) pos++;

					// Image Data Sub-blocks 건너뛰기
					while (pos < size && bytes[pos] != 0x00)
					{
						guint8 block_size = bytes[pos];
						pos += block_size + 1;
						if (pos >= size) break;
					}
					if (pos < size) pos++; // Block Terminator
				}

				// 두 번째 이미지가 발견되면 애니메이션
				if (image_count > 1)
				{
					info->has_anim = true;
					break;
				}
			}
			else if (bytes[pos] == 0x3B) // Trailer (종료)
			{
				break;
			}
			else
			{
				// 알 수 없는 데이터, 다음 바이트로
				pos++;
			}
		}

		// Graphic Control Extension이 있고 이미지가 하나뿐이어도 애니메이션일 가능성
		if (!info->has_anim && has_graphic_control && image_count == 1)
		{
			info->has_anim = true;
		}

		info->type = IMAGE_FILE_TYPE_GIF;
		goto pos_detected;
	}
	else if (size >= 30 && !memcmp(bytes + 8, "WEBP", 4) && !memcmp(bytes + 12, "VP8", 3)) // WEBP
	{
		bool need_test_anim;
		if (bytes[15] == 'X')
		{
			need_test_anim = false;
			info->has_anim = (bytes[20] & 0x02) != 0; // ANIMATION 플래그 (비트 1)
			info->width = ((bytes[24] | (bytes[25] << 8) | (bytes[26] << 16)) & 0xFFFFFF) + 1;
			info->height = ((bytes[27] | (bytes[28] << 8) | (bytes[29] << 16)) & 0xFFFFFF) + 1;
		}
		else if (bytes[15] == ' ')
		{
			need_test_anim = true;
			info->width = (bytes[26] | (bytes[27] << 8)) & 0x3FFF;
			info->height = (bytes[28] | (bytes[29] << 8)) & 0x3FFF;
		}
		else if (bytes[15] == 'L')
		{
			need_test_anim = true;
			const guint bits = bytes[21] | (bytes[22] << 8) | (bytes[23] << 16) | (bytes[24] << 24);
			info->width = (int)((bits & 0x3FFF) + 1);
			info->height = (int)(((bits >> 14) & 0x3FFF) + 1);
		}
		else
		{
			// 몰라 모르는 webp
			return false;
		}

		// ANMF 청크로 애니메이션 확인
		if (need_test_anim)
		{
			for (gsize i = 12; i + 8 < size;)
			{
				if (!memcmp(bytes + i, "ANMF", 4))
				{
					info->has_anim = true;
					break;
				}

				const guint32 chunk_size = bytes[i + 4] | (bytes[i + 5] << 8) |
					(bytes[i + 6] << 16) | (bytes[i + 7] << 24);
				i += chunk_size + 8 + (chunk_size % 2); // 청크 크기 + 8바이트 헤더
			}
		}

		info->type = IMAGE_FILE_TYPE_WEBP;
		goto pos_detected;
	}
	else if (size >= 26 && !memcmp(bytes, "BM", 2)) // BMP
	{
		info->width = bytes[18] | (bytes[19] << 8) | (bytes[20] << 16) | (bytes[21] << 24);
		info->height = bytes[22] | (bytes[23] << 8) | (bytes[24] << 16) | (bytes[25] << 24);
		// 높이가 음수일 수 있음 (top-down DIB)
		if (info->height < 0) info->height = -info->height;
		info->type = IMAGE_FILE_TYPE_BMP;
		goto pos_detected;
	}
	else if (size >= 20 && !memcmp(bytes, "II*\0", 4)) // TIFF (Little Endian)
	{
		// IFD 오프셋 읽기
		const guint32 ifd_offset = bytes[4] | (bytes[5] << 8) | (bytes[6] << 16) | (bytes[7] << 24);
		if (ifd_offset < size - 12)
		{
			const guint16 num_entries = (guint16)(bytes[ifd_offset] | (bytes[ifd_offset + 1] << 8));
			// 각 IFD 엔트리 확인 (12바이트씩)
			for (int i = 0; i < num_entries && ifd_offset + 2 + i * 12 + 12 <= size; i++)
			{
				const gsize entry_offset = ifd_offset + 2 + i * 12;
				const guint16 tag = (guint16)(bytes[entry_offset] | (bytes[entry_offset + 1] << 8));
				const guint32 value = bytes[entry_offset + 8] | (bytes[entry_offset + 9] << 8) |
					(bytes[entry_offset + 10] << 16) | (bytes[entry_offset + 11] << 24);
				if (tag == 0x0100)
					info->width = (int)value;
				else if (tag == 0x0101)
					info->height = (int)value;
			}
		}

		if (info->width && info->height)
		{
			info->type = IMAGE_FILE_TYPE_TIFF;
			goto pos_detected;
		}
	}

	return false;

pos_detected:
	info->size = (size_t)info->width * (size_t)info->height * 4;
	return true;
}

// 메지시 박스 데이터
typedef struct MesgBoxData
{
	MesgBoxCallback callback; // 콜백 함수
	gpointer user_data; // 사용자 데이터
} MesgBoxData;

// 메시지 박스 콜백
void cb_mesg_box_choose(GObject* source_object, GAsyncResult* res, gpointer user_data)
{
	MesgBoxData* data = user_data;
	GtkAlertDialog* dialog = GTK_ALERT_DIALOG(source_object);
	const int button = gtk_alert_dialog_choose_finish(dialog, res, NULL);
	if (data->callback)
		data->callback(data->user_data, button == 0);
	g_free(data);
}

// 메시지 박스 공통
static GtkAlertDialog* create_alert_mesg_box(const char* text, const char* detail, const char** buttons)
{
	if (!text || !*text)
		return NULL;
	GtkAlertDialog* dialog = gtk_alert_dialog_new("");
	gtk_alert_dialog_set_modal(dialog, true);
	gtk_alert_dialog_set_message(dialog, text);
	gtk_alert_dialog_set_detail(dialog, detail);
	gtk_alert_dialog_set_buttons(dialog, buttons);
	gtk_alert_dialog_set_default_button(dialog, 0);
	return dialog;
}

// 확인 메시지 박스 보이기
void doumi_mesg_ok_show_async(
	GtkWindow* parent,
	const char* text, const char* detail,
	MesgBoxCallback callback, gpointer user_data)
{
	const char* buttons[] = { _("OK"), NULL };
	GtkAlertDialog* dialog = create_alert_mesg_box(text, detail, buttons);
	if (dialog == NULL)
		return;

	MesgBoxData* data = g_new(MesgBoxData, 1);
	data->callback = callback;
	data->user_data = user_data;

	gtk_alert_dialog_choose(dialog, parent, NULL, cb_mesg_box_choose, data);
	g_object_unref(dialog);
}

// 예/아니오 메시지 박스 보이기
void doumi_mesg_yesno_show_async(
	GtkWindow* parent,
	const char* text, const char* detail,
	MesgBoxCallback callback, gpointer user_data)
{
	const char* buttons[] = { _("Yes"), _("No"), NULL };
	GtkAlertDialog* dialog = create_alert_mesg_box(text, detail, buttons);
	if (dialog == NULL)
		return;

	MesgBoxData* data = g_new(MesgBoxData, 1);
	data->callback = callback;
	data->user_data = user_data;

	gtk_alert_dialog_choose(dialog, parent, NULL, cb_mesg_box_choose, data);
	g_object_unref(dialog);
}
