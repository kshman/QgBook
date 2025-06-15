#include "pch.h"
#include "configs.h"

/* main.c - 큭책 프로그램의 진입점
 *
 * main() 함수 제공
 * 이미지(텍스쳐) 리소스 로딩
 * CSS 스타일 적용
 * 윈도우의 경우 g_log 핸들러 등록
 */

 // GTK 내장 리소스 데이터
extern GResource* resg_get_resource(void);

// 읽기 윈도우
extern void* read_window_new(GtkApplication* app);
extern void read_window_show(const void* rw);

// 텍스쳐 캐시
static GdkTexture* s_textures[RES_MAX_VALUE];

// 텍스쳐 얻기
GdkTexture* res_get_texture(const ResKeys key)
{
	return key < RES_MAX_VALUE ? s_textures[key] : NULL;
}

// 뷰 모드에 따른 텍스쳐 얻기
GdkTexture* res_get_view_mode_texture(ViewMode mode)
{
	const ResKeys key =
		mode == VIEW_MODE_FIT ? RES_ICON_VIEW_MODE_FIT :
		mode == VIEW_MODE_LEFT_TO_RIGHT ? RES_ICON_VIEW_MODE_L2R :
		mode == VIEW_MODE_RIGHT_TO_LEFT ? RES_ICON_VIEW_MODE_R2L : RES_ICON_PURUTU;
	return s_textures[key];
}

// 활기차게 콜백
static void app_activate(GtkApplication* app, gpointer user_data)
{
	// 설정 캐시
	configs_load_cache();

	// 리소스 텍스쳐
	static const char* s_res_filenames[RES_MAX_VALUE] =
	{
		"pix/no_image.png",
		"pix/housebari_head_128.jpg",
		"icon/directory.png",
		"icon/menus.png",
		"icon/move.png",
		"icon/painting.png",
		"icon/purutu.png",
		"icon/rename.png",
		"icon/view-mode-fit.png",
		"icon/view-mode-l2r.png",
		"icon/view-mode-r2l.png",
	};
	for (int i = 0; i < RES_MAX_VALUE; i++)
	{
		const char* psz = doumi_resource_path(s_res_filenames[i]);
		GdkTexture* texture = gdk_texture_new_from_resource(psz);
		s_textures[i] = texture;
	}

	// CSS 등록
	GtkCssProvider* css = gtk_css_provider_new();
	GdkDisplay* display = gdk_display_get_default();
	gtk_css_provider_load_from_resource(css, doumi_resource_path("style.css"));
	gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(css);

	// 윈도우 열기
	const void* rw = read_window_new(app);
	read_window_show(rw);
}

// 셧다운 콜백
static void app_shutdown(GtkApplication* app, gpointer user_data)
{
	// 텍스쳐 해제
	for (int i = 0; i < RES_MAX_VALUE; i++)
	{
		if (s_textures[i])
			g_object_unref(G_OBJECT(s_textures[i]));
	}
}

#if defined(_WIN32)
// 윈도우용 로그 출력기
static void win32_log_handler(const char* domain, GLogLevelFlags level, const char* message, gpointer user_data)
{
	char buf[2048];
	const char* l =
		(level & G_LOG_LEVEL_ERROR) ? "ERROR" :
		(level & G_LOG_LEVEL_CRITICAL) ? "CRITICAL" :
		(level & G_LOG_LEVEL_WARNING) ? "WARNING" :
		(level & G_LOG_LEVEL_MESSAGE) ? "MESSAGE" :
		(level & G_LOG_LEVEL_INFO) ? "INFO" :
		(level & G_LOG_LEVEL_DEBUG) ? "DEBUG" : "LOG";
	g_snprintf(buf, sizeof(buf), "[%s] %s: %s\n", l, domain ? domain : "", message ? message : "<unknown>");
	wchar_t wide[2048];
	MultiByteToWideChar(CP_UTF8, 0, buf, -1, wide, 2048);
	OutputDebugStringW(wide);
}

// 윈도우 메인
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	(void)hInstance; // Unused parameter
	(void)hPrevInstance; // Unused parameter
	(void)lpCmdLine; // Unused parameter
	(void)nCmdShow; // Unused parameter
	int argc = 0;
	char** argv = NULL;

	g_log_set_default_handler(win32_log_handler, NULL);
#else
// 윈도우가 아닌 메인
int main(int argc, char** argv)
{
#endif
	// 여기가 시작
	GtkApplication* app = gtk_application_new("ksh.qg.book", G_APPLICATION_DEFAULT_FLAGS);
	g_resources_register(resg_get_resource());

	// 설정
	if (!configs_init())
		return 1; // 초기 설정 실패 시 종료

	// 한번만 실행 확인
	if (configs_get_bool(CONFIG_GENERAL_RUN_ONCE, true) && !doumi_lock_program())
	{
		configs_dispose();
		return 2;
	}

	// 시그널
	g_signal_connect(app, "activate", G_CALLBACK(app_activate), NULL);
	g_signal_connect(app, "shutdown", G_CALLBACK(app_shutdown), NULL);

	// 시작
	const int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);

	// 정리
	configs_dispose();
	return status;
}
