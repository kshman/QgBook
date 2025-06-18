#include "pch.h"
#include "configs.h"
#include "book.h"
#include "doumi.h"

#define NOTIFY_TIMEOUT 2000

// 앞서 선언
typedef struct ReadWindow ReadWindow;
typedef void (*ShortcutFunc)(ReadWindow*);

#pragma region 스냅샷용 그리기 위젯
// 그리기 위젯 정의
typedef struct
{
	GtkDrawingArea parent;
	ReadWindow* read_window;
} ReadDraw;

typedef struct
{
	GtkDrawingAreaClass parent_class;
} ReadDrawClass;

G_DEFINE_TYPE(ReadDraw, read_draw, GTK_TYPE_DRAWING_AREA)

static void read_draw_snapshot(GtkWidget* widget, GtkSnapshot* snapshot);
static gboolean read_draw_focus(GtkWidget* widget, GtkDirectionType direction);

static GtkWidget* read_draw_new(ReadWindow* rw)
{
	GtkWidget* widget = GTK_WIDGET(g_object_new(read_draw_get_type(), NULL));
	((ReadDraw*)widget)->read_window = rw;
	return widget;
}

static void read_draw_class_init(ReadDrawClass* klass)
{
	GtkWidgetClass* widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->snapshot = read_draw_snapshot;
	widget_class->focus = read_draw_focus;
}

static void read_draw_init(ReadDraw* self)
{
	gtk_widget_set_focusable(GTK_WIDGET(self), true);
}

static gboolean read_draw_focus(GtkWidget* widget, GtkDirectionType direction)
{
	// 필요시 내부 상태 갱신 등
	gtk_widget_queue_draw(widget);
	return true; // 포커스 받음
}
#pragma endregion


// 읽기 윈도우
struct ReadWindow
{
	// 윈도우
	GtkWidget* window;

	GtkWidget* title_label;
	GtkWidget* info_label;

	GtkWidget* menu_file_close;
	GtkWidget* menu_zoom_check;
	GtkWidget* menu_vmode_image;
	GtkWidget* menu_vmode_radios[VIEW_MODE_MAX_VALUE];
	GtkWidget* menu_vquality_radios[VIEW_QUALITY_MAX_VALUE];

	GtkWidget* draw;

	GHashTable* scmd;
	guint key_val;
	GdkModifierType key_state;

	// 알림
	guint32 notify_id;
	char* notify_text;
	PangoFontDescription* notify_font;

	// 책 상태
	bool view_zoom;
	ViewMode view_mode;
	ViewQuality view_quality;

	// 책
	Book* book;
	GdkTexture* page_left;
	GdkTexture* page_right;
};

#pragma region 알림 메시지
// 알림 메시지 타이머 콜백
static gboolean notify_timeout_callback(gpointer data)
{
	ReadWindow* rw = (ReadWindow*)data;
	if (rw->notify_text)
	{
		g_free(rw->notify_text);
		rw->notify_text = NULL;
	}
	rw->notify_id = 0;
	gtk_widget_queue_draw(rw->draw);
	return false; // 타이머 제거
}

// 알림 메시지
static void set_notify(ReadWindow* rw, const char* text, int timeout)
{
	if (rw->notify_text)
		g_free(rw->notify_text);
	rw->notify_text = text ? g_strdup(text) : NULL;

	if (rw->notify_id != 0)
	{
		g_source_remove(rw->notify_id);
		rw->notify_id = 0;
	}

	if (text != NULL)
		rw->notify_id = g_timeout_add(timeout > 0 ? timeout : NOTIFY_TIMEOUT, notify_timeout_callback, rw);
	gtk_widget_queue_draw(rw->draw);
}

// 알림 메시지 그리기
static void paint_notify(ReadWindow* rw, GtkSnapshot* s, float width, float height)
{
	if (!rw->notify_text || rw->notify_text[0] == '\0')
		return;

	// 글꼴과 그릴 위치의 크기 계산
	PangoLayout* layout = gtk_widget_create_pango_layout(GTK_WIDGET(rw->draw), rw->notify_text);
	pango_layout_set_font_description(layout, rw->notify_font);
	int text_width, text_height;
	pango_layout_get_size(layout, &text_width, &text_height);
	text_width /= PANGO_SCALE;
	text_height /= PANGO_SCALE;

	const int padding = 18;
	const int tex_w = text_width + padding * 2;
	const int tex_h = text_height + padding * 2;

	// 카이로 서피스에 그리기
	cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tex_w, tex_h);
	cairo_t* cr = cairo_create(surface);

	// 배경 및 테두리 그리기
	cairo_set_source_rgba(cr, 0.12, 0.12, 0.8, 0.9);
	cairo_rectangle(cr, 1, 1, tex_w - 1, tex_h - 1);
	cairo_fill_preserve(cr);
	cairo_set_line_width(cr, 5);
	cairo_set_source_rgba(cr, 1, 1, 0, 1.0);
	cairo_stroke(cr);

	// 텍스트 그리기
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, padding, padding);
	pango_cairo_show_layout(cr, layout);

	cairo_destroy(cr);
	g_object_unref(layout);

	// 서피스를 텍스쳐로 바꾸고 스냅샷에 그리기
	GdkTexture* texture = doumi_texture_from_surface(surface);
	cairo_surface_destroy(surface);

	if (texture)
	{
		float x = (width - (float)tex_w) / 2.0f;
		float y = (height - (float)tex_h) / 2.0f;
		gtk_snapshot_append_texture(s, texture, &GRAPHENE_RECT_INIT(x, y, (float)tex_w, (float)tex_h));
		g_object_unref(texture);
	}
}
#pragma endregion

#pragma region 윈도우 기능
// 포커스 리셋
static void reset_focus(ReadWindow* rw)
{
	// 그리기 위젯에 포커스 주기
	gtk_widget_grab_focus(rw->window);
	gtk_widget_grab_focus(rw->draw);
}

// 풀스크린!
static void toggle_fullscreen(ReadWindow* rw)
{
	if (gtk_window_is_fullscreen(GTK_WINDOW(rw->window)))
		gtk_window_unfullscreen(GTK_WINDOW(rw->window));
	else
		gtk_window_fullscreen(GTK_WINDOW(rw->window));
}

// 늘려보기 설정과 메뉴 처리
static void update_view_zoom(ReadWindow* rw, bool zoom, bool redraw)
{
	if (rw->view_zoom == zoom)
		return;

	rw->view_zoom = zoom;
	config_set_bool(CONFIG_VIEW_ZOOM, zoom, false);

	gtk_check_button_set_active(GTK_CHECK_BUTTON(rw->menu_zoom_check), zoom);

	if (redraw)
		gtk_widget_queue_draw(rw->draw);
}

// 보기 모드 설정과 메뉴 처리
static void update_view_mode(ReadWindow* rw, ViewMode mode, bool redraw)
{
	if (rw->view_mode == mode)
		return;

	rw->view_mode = mode;
	config_set_int(CONFIG_VIEW_MODE, mode, false);

	GdkPaintable* paintable = GDK_PAINTABLE(res_get_view_mode_texture(mode));
	gtk_image_set_from_paintable(GTK_IMAGE(rw->menu_vmode_image), paintable);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(rw->menu_vmode_radios[mode]), true);

	if (redraw)
	{
		// 책 준비
		// 책 그리기
		gtk_widget_queue_draw(rw->draw);
	}
}

// 보기 품질 설정과 메뉴 처리
static void update_view_quality(ReadWindow* rw, ViewQuality quality, bool redraw)
{
	if (rw->view_quality == quality)
		return;

	rw->view_quality = quality;
	config_set_int(CONFIG_VIEW_QUALITY, quality, false);

	gtk_check_button_set_active(GTK_CHECK_BUTTON(rw->menu_vquality_radios[quality]), true);

	if (redraw)
	{
		// 책 그리기
		gtk_widget_queue_draw(rw->draw);
	}
}

// 현재 책 정보를 헤더바에 표시
static void update_book_info_with_page(ReadWindow* rw, int page)
{
	if (rw->book == NULL)
		return;

	char info[256], size[64];
	doumi_format_size_friendly(/*rw->book->cache_size*/0, size, sizeof(size));
	g_snprintf(info, sizeof(info), "%d/%d [%s]", rw->book->cur_page + 1, rw->book->total_page, size);
	gtk_label_set_text(GTK_LABEL(rw->info_label), info);
	gtk_label_set_text(GTK_LABEL(rw->title_label), rw->book->base_name);
}

// 현재 책 정보를 헤더바에 표시
static void update_book_info(ReadWindow* rw)
{
	update_book_info_with_page(rw, -1);
}
#pragma endregion

#pragma region 책 처리
// 책 정리
static void close_book(ReadWindow* rw)
{
	if (rw->book != NULL)
	{
		int page = rw->book->cur_page - 1 >= rw->book->total_page ? 0: rw->book->cur_page;
		recently_set_page(rw->book->base_name, page);

		book_dispose(rw->book);
		rw->book = NULL;

		gtk_label_set_text(GTK_LABEL(rw->info_label), "----");
		gtk_label_set_text(GTK_LABEL(rw->title_label), _("[No Book]"));
		gtk_widget_set_sensitive(rw->menu_file_close, false);
	}

	if (rw->page_left)
	{
		g_object_unref(rw->page_left);
		rw->page_left = NULL;
	}

	if (rw->page_right)
	{
		g_object_unref(rw->page_right);
		rw->page_right = NULL;
	}

	gtk_widget_queue_draw(rw->draw);
}

// 책 열기
// page가 0이상이면 해당 페이지로 열기
static void open_book(ReadWindow* rw, GFile* file, int page)
{
}

// 책 열기 대화상자
static void open_book_dialog(ReadWindow* rw)
{

}
#pragma endregion


#pragma region 이벤트 콜백
// 윈도우 종료되고 나서 콜백
static void signal_destroy(GtkWidget* widget, ReadWindow* rw)
{
	// 책 지우기
	// 페이지 다이얼로그 해제

	if (rw->notify_text)
		g_free(rw->notify_text);
	if (rw->notify_font)
		pango_font_description_free(rw->notify_font);

	// 여기서 해제하면 된다구
	if (rw->scmd)
		g_hash_table_destroy(rw->scmd);

	g_free(rw);
}

// 윈도우 각종 알림 콜백
static void signal_notify(GObject* object, GParamSpec* pspec, ReadWindow* rw)
{
	const char* name = g_param_spec_get_name(pspec);
	if (g_strcmp0(name, "default-width") == 0 || g_strcmp0(name, "default-height") == 0)
	{
		if (!gtk_window_is_maximized(GTK_WINDOW(rw->window)) && !gtk_window_is_fullscreen(GTK_WINDOW(rw->window)))
		{
			// 윈도우 기본 크기 변경
			int width, height;
			gtk_window_get_default_size(GTK_WINDOW(rw->window), &width, &height);
			config_set_int(CONFIG_WINDOW_WIDTH, width, true);
			config_set_int(CONFIG_WINDOW_HEIGHT, height, true);
		}
	}
}

// 파일 끌어 놓기
static gboolean signal_file_drop(GtkDropTarget* target, const GValue* value, double x, double y, ReadWindow* rw)
{
	// 참고로 G_TYPE_STRING로 다중 파일을 "text/uri-list"로 처리할 수 있으나 안함
	if (!G_VALUE_HOLDS(value, G_TYPE_FILE))
		return false;

	GFile* file = g_value_get_object(value);
	char* path = g_file_get_path(file);
	if (path)
	{
		set_notify(rw, path, 0);
		g_free(path);
	}

	return true;
}

// 마우스 휠
static gboolean signal_wheel_scroll(GtkEventControllerScroll* controller, double dx, double dy, ReadWindow* rw)
{
	if (dy > 0)
	{
		//set_notify(rw, "Wheel Down", 1000);
		// 예: 다음 페이지로 이동
		// book_next_page(rw->book);
	}
	else if (dy < 0)
	{
		//set_notify(rw, "Wheel Up", 1000);
		// 예: 이전 페이지로 이동
		// book_prev_page(rw->book);
	}
	return TRUE; // 이벤트 소모
}

// 마우스 버튼
static void signal_mouse_click(GtkGestureClick* gesture, int n_press, double x, double y, ReadWindow* rw)
{
	const guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

	if (n_press == 2 && button == GDK_BUTTON_PRIMARY)
	{
		// 두번 클릭
		if (rw->book == NULL)
		{
			// 책 열기
		}
	}
	else if (n_press == 3 && button == GDK_BUTTON_SECONDARY)
	{
		// 세번 클릭, GTK4는 창 끌어 이동이 안되므로, 그냥 옵션 무시하고 3번 눌러 전체 화면 전환
		if (rw->book != NULL)
			toggle_fullscreen(rw);
	}
	else if (n_press == 1)
	{
		// 한번 클릭
		if (button == GDK_BUTTON_PRIMARY)
		{
			if (!config_get_bool(CONFIG_MOUSE_DOUBLE_CLICK_FULLSCREEN, true) &&
				!gtk_window_is_maximized(GTK_WINDOW(rw->window)) &&
				!gtk_window_is_fullscreen(GTK_WINDOW(rw->window)))
			{
				// 드래그
				// ...는 GTK4부터 안됨...
			}
		}
		else if (button == GDK_BUTTON_SECONDARY)
		{
			// 메뉴
		}
		else if (button == GDK_BUTTON_MIDDLE)
		{
			// 전체 화면 토글
			toggle_fullscreen(rw);
		}
	}
}

// 키보드 떼임
static gboolean signal_key_released(GtkEventControllerKey* controller,
	guint value,
	guint code,
	GdkModifierType state,
	ReadWindow* rw)
{
	rw->key_val = 0;
	rw->key_state = GDK_NO_MODIFIER_MASK;

	return false;
}

// 키보드 눌림
static gboolean signal_key_pressed(GtkEventControllerKey* controller,
	guint value,
	guint code,
	GdkModifierType state,
	ReadWindow* rw)
{
	// 보조키는 쉬프트/컨트롤/알트/슈퍼/하이퍼/메타만 남기고 나머지는 제거
	state &= GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK | GDK_META_MASK;
	if (rw->key_val == value && rw->key_state == state)
		return false;

	rw->key_val = value;
	rw->key_state = state;

	const char* action = shortcut_lookup(value, state);
	if (action != NULL)
	{
		ShortcutFunc func = g_hash_table_lookup(rw->scmd, action);
		if (func != NULL)
		{
			func(rw);
			return true; // 이벤트 소모
		}
	}

	return false;
}

// 파일 열기 누르기
static void menu_file_open_clicked(GtkButton* button, ReadWindow* rw)
{
	open_book_dialog(rw);
}

// 파일 닫기 누르기
static void menu_file_close_clicked(GtkButton* button, ReadWindow* rw)
{
	close_book(rw);
}

// 설정 누르기
static void menu_settings_click(GtkButton* button, ReadWindow* rw)
{
	// 설정은 언제 만드냐...
}

// 끝내기 누르기
static void menu_exit_click(GtkButton* button, ReadWindow* rw)
{
	gtk_window_close(GTK_WINDOW(rw->window));
}

// 늘려 보기 토글
static void menu_view_zoom_toggled(GtkCheckButton* button, ReadWindow* rw)
{
	const gboolean b = gtk_check_button_get_active(GTK_CHECK_BUTTON(rw->menu_zoom_check));
	update_view_zoom(rw, b, true);
}

// 보기 모드 누르기
static void menu_view_mode_clicked(GtkButton* button, ReadWindow* rw)
{
	const ViewMode mode =
		rw->view_mode == VIEW_MODE_FIT
		? VIEW_MODE_LEFT_TO_RIGHT
		: rw->view_mode == VIEW_MODE_LEFT_TO_RIGHT
		? VIEW_MODE_RIGHT_TO_LEFT
		: /*rw->view_mode == VIEW_MODE_RIGHT_TO_LEFT ? VIEW_MODE_FIT :*/ VIEW_MODE_FIT;
	update_view_mode(rw, mode, true);
}

// 보기 방향 선택 바뀜
static void menu_view_mode_toggled(GtkCheckButton* button, ReadWindow* rw)
{
	if (!gtk_check_button_get_active(button))
		return;
	ViewMode mode = (ViewMode)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "tag"));
	update_view_mode(rw, mode, true);
}

// 보기 품질 선택 바뀜
static void menu_view_quality_toggled(GtkCheckButton* button, ReadWindow* rw)
{
	if (!gtk_check_button_get_active(button))
		return;
	ViewQuality quality = (ViewQuality)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "tag"));
	update_view_quality(rw, quality, true);
}

// 단축키 - 아무것도 안함
static void shortcut_on_none(ReadWindow* rw)
{
#if _DEBUG
	static int s_i = 0;
	g_info(_("no action: %d"), s_i++);
#endif
}

// 단축키 - 테스트
static void shortcut_on_test(ReadWindow* rw)
{
	set_notify(rw, _("TEST TEST TEST"), 5000);
}

// 단축키 - 끝내기
static void shortcut_on_exit(ReadWindow* rw)
{
	gtk_window_close(GTK_WINDOW(rw->window));
}

// 단축키 - 끝내기인데 Escape
static void shortcut_on_escape(ReadWindow* rw)
{
	if (config_get_bool(CONFIG_GENERAL_ESC_EXIT, true))
		gtk_window_close(GTK_WINDOW(rw->window));
}

// 단축키 - 파일 열기
static void shortcut_on_file_open(ReadWindow* rw)
{
	open_book_dialog(rw);
}

// 단축키 - 파일 닫기
static void shortcut_on_file_close(ReadWindow* rw)
{
	close_book(rw);
}

// 단축키 - 설정
static void shortcut_on_settings(ReadWindow* rw)
{
	// 언젠가는 해야하겠지...
}

// 단축키 - 풀스크린
static void shortcut_on_fullscreen(ReadWindow* rw)
{
	toggle_fullscreen(rw);
}
#pragma endregion

#pragma region 스냅샷 그리기
// 스냅샷 재정의로 책 윈도우를 그리자고
static void read_draw_snapshot(GtkWidget* widget, GtkSnapshot* snapshot)
{
	ReadDraw* self = (ReadDraw*)widget;
	ReadWindow* rw = self->read_window;
	float width = (float)gtk_widget_get_width(widget);
	float height = (float)gtk_widget_get_height(widget);

	// 배경
	const GdkRGBA red = { 0.1f, 0.1f, 0.1f, 1.0f };
	gtk_snapshot_append_color(snapshot, &red, &GRAPHENE_RECT_INIT(0, 0, width, height));

	// 카이로로 그릴거
	cairo_t* cr = gtk_snapshot_append_cairo(snapshot, &GRAPHENE_RECT_INIT(0, 0, width, height));
	if (cr)
	{
		cairo_set_source_rgba(cr, 0.2, 0.4, 0.6, 1.0);
		cairo_arc(cr, 200, 150, 100, 0, 2 * G_PI);
		cairo_fill(cr);
		cairo_destroy(cr);
	}

	// 로고 그리기
	GdkTexture* logo = res_get_texture(RES_PIX_HOUSEBARI);
	if (logo)
	{
		const float lw = (float)gdk_texture_get_width(logo);
		const float lh = (float)gdk_texture_get_height(logo);
		const float y = width > lh ? height - lh - 50.0f : 10.0f;
		gtk_snapshot_append_texture(snapshot, logo, &GRAPHENE_RECT_INIT(width - lw - 100.0f, y, lw, lh));
	}

	// 책 그리기

	// 알림 메시지 그리기
	paint_notify(rw, snapshot, width, height);
}
#pragma endregion

#pragma region 책 읽기 클래스
// 만들기
ReadWindow* read_window_new(GtkApplication* app)
{
	ReadWindow* rw = g_new0(ReadWindow, 1);

	// 첨에 UI 설정할 때 중복 호출 방지
	rw->view_zoom = config_get_bool(CONFIG_VIEW_ZOOM, true);
	rw->view_mode = (ViewMode)config_get_int(CONFIG_VIEW_MODE, true);
	rw->view_quality = (ViewQuality)config_get_int(CONFIG_VIEW_QUALITY, true);

	const int width = config_get_int(CONFIG_WINDOW_WIDTH, true);
	const int height = config_get_int(CONFIG_WINDOW_HEIGHT, true);

#pragma region 윈도우 디자인
	// 윈도우
	rw->window = gtk_application_window_new(app);

	gtk_window_set_title(GTK_WINDOW(rw->window), _("QgBook"));
	gtk_window_set_default_size(GTK_WINDOW(rw->window), width, height);
	gtk_widget_set_size_request(GTK_WIDGET(rw->window), 600, 400);
	g_signal_connect(rw->window, "destroy", G_CALLBACK(signal_destroy), rw);
	g_signal_connect(rw->window, "notify", G_CALLBACK(signal_notify), rw);

	// 팡고 글꼴
	rw->notify_font = pango_font_description_from_string(
		"Malgun Gothic, Apple SD Gothic Neo, Noto Sans CJK KR, Sans 20");

	// 메인 메뉴, 헤더바에서 쓸거라 앞쪽에서 만들어 두자
	GtkWidget* main_popover = gtk_popover_new();
	GtkWidget* menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_margin_start(menu_box, 8);
	gtk_widget_set_margin_end(menu_box, 8);

	// 메뉴 - 파일 열기
	GtkWidget* menu_file_open = gtk_button_new_with_label(_("Book open"));
	g_signal_connect(menu_file_open, "clicked", G_CALLBACK(menu_file_open_clicked), rw);
	gtk_box_append(GTK_BOX(menu_box), menu_file_open);

	// 메뉴 - 파일 닫기
	rw->menu_file_close = gtk_button_new_with_label(_("Close book"));
	gtk_widget_set_sensitive(rw->menu_file_close, false); // 첨엔 파일이 없으니깐 비활성화
	g_signal_connect(rw->menu_file_close, "clicked", G_CALLBACK(menu_file_close_clicked), rw);
	gtk_box_append(GTK_BOX(menu_box), rw->menu_file_close);

	gtk_box_append(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

	// 메뉴 - 설정
	GtkWidget* menu_setting = gtk_button_new_with_label(_("Settings"));
	g_signal_connect(menu_setting, "clicked", G_CALLBACK(menu_settings_click), rw);
	gtk_box_append(GTK_BOX(menu_box), menu_setting);

	gtk_box_append(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

	// 메뉴 - 끝내기
	GtkWidget* menu_exit = gtk_button_new_with_label(_("Exit"));
	g_signal_connect(menu_exit, "clicked", G_CALLBACK(menu_exit_click), rw);
	gtk_box_append(GTK_BOX(menu_box), menu_exit);

	// 팝오버에 메인 메뉴 박스 넣기
	gtk_popover_set_child(GTK_POPOVER(main_popover), menu_box);

	// 보기 메뉴, 이 것도 앞에서 만들자고
	GtkWidget* view_popover = gtk_popover_new();
	menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	// 보기 메뉴 - 늘려 보기
	rw->menu_zoom_check = gtk_check_button_new_with_label(_("Zoom"));
	gtk_check_button_set_active(GTK_CHECK_BUTTON(rw->menu_zoom_check), rw->view_zoom);
	g_signal_connect(rw->menu_zoom_check, "toggled", G_CALLBACK(menu_view_zoom_toggled), rw);
	gtk_box_append(GTK_BOX(menu_box), rw->menu_zoom_check);

	gtk_box_append(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

	// 보기 메뉴 - 방향
	GtkWidget* vmode_label = gtk_label_new(_("View Mode"));
	gtk_widget_set_halign(vmode_label, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(menu_box), vmode_label);

	const char* vmode_strings[VIEW_MODE_MAX_VALUE] =
	{
		_("Fit Window"),
		_("Left to Right"),
		_("Right to Left"),
	};
	for (int i = 0; i < VIEW_MODE_MAX_VALUE; i++)
	{
		GtkWidget* r = gtk_check_button_new_with_label(vmode_strings[i]);
		g_object_set_data(G_OBJECT(r), "tag", GINT_TO_POINTER(i));
		g_signal_connect(r, "toggled", G_CALLBACK(menu_view_mode_toggled), rw);
		if (i > 0)
			gtk_check_button_set_group(GTK_CHECK_BUTTON(r), GTK_CHECK_BUTTON(rw->menu_vmode_radios[0]));
		gtk_box_append(GTK_BOX(menu_box), rw->menu_vmode_radios[i] = r);
	}
	gtk_check_button_set_active(GTK_CHECK_BUTTON(rw->menu_vmode_radios[rw->view_mode]), true);

	gtk_box_append(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

	// 보기 메뉴 - 품질
	GtkWidget* vquality_label = gtk_label_new(_("Image Quality"));
	gtk_widget_set_halign(vquality_label, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(menu_box), vquality_label);

	const char* vquality_strings[VIEW_QUALITY_MAX_VALUE] =
	{
		_("Fast Quality"),
		_("Normal Quality"),
		_("High Quality"),
		_("Nearest Interpolation"),
		_("Bilinear Interpolation"),
	};
	for (int i = 0; i < VIEW_QUALITY_MAX_VALUE; i++)
	{
		GtkWidget* r = gtk_check_button_new_with_label(vquality_strings[i]);
		g_object_set_data(G_OBJECT(r), "tag", GINT_TO_POINTER(i));
		g_signal_connect(r, "toggled", G_CALLBACK(menu_view_quality_toggled), rw);
		if (i > 0)
			gtk_check_button_set_group(GTK_CHECK_BUTTON(r), GTK_CHECK_BUTTON(rw->menu_vquality_radios[0]));
		gtk_box_append(GTK_BOX(menu_box), rw->menu_vquality_radios[i] = r);
	}
	gtk_check_button_set_active(GTK_CHECK_BUTTON(rw->menu_vquality_radios[rw->view_quality]), true);

	// 팝오버에 보기 메뉴 박스 넣기
	gtk_popover_set_child(GTK_POPOVER(view_popover), menu_box);

	// 헤더바
	GtkWidget* header = gtk_header_bar_new();

	// 헤더바 - 보기 모드 버튼 + 이미지
	rw->menu_vmode_image = gtk_image_new_from_paintable(GDK_PAINTABLE(res_get_view_mode_texture(rw->view_mode)));
	gtk_image_set_pixel_size(GTK_IMAGE(rw->menu_vmode_image), 24);

	GtkWidget* menu_mode_button = gtk_button_new();
	gtk_widget_set_can_focus(menu_mode_button, false);
	gtk_button_set_child(GTK_BUTTON(menu_mode_button), rw->menu_vmode_image);
	g_signal_connect(menu_mode_button, "clicked", G_CALLBACK(menu_view_mode_clicked), rw);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header), menu_mode_button);

	// 헤더바 - 제목
	rw->title_label = gtk_label_new(_("[No Book]"));
	gtk_widget_set_halign(rw->title_label, GTK_ALIGN_START);
	gtk_widget_set_valign(rw->title_label, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_start(rw->title_label, 12);
	gtk_widget_set_sensitive(rw->title_label, false); // 책이 열려있지 않으니 비활성화
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header), rw->title_label);

	// 헤더바 - 메인 메뉴 버튼
	GtkWidget* menu_button = gtk_menu_button_new();
	GtkWidget* menu_icon = gtk_image_new_from_paintable(GDK_PAINTABLE(res_get_texture(RES_ICON_MENUS)));
	gtk_widget_set_can_focus(menu_button, false);
	gtk_menu_button_set_child(GTK_MENU_BUTTON(menu_button), menu_icon);
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu_button), main_popover);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_button);

	// 헤더바 - 보기 메뉴 버튼
	GtkWidget* view_button = gtk_menu_button_new();
	GtkWidget* view_icon = gtk_image_new_from_paintable(GDK_PAINTABLE(res_get_texture(RES_ICON_PAINTING)));
	gtk_widget_set_can_focus(view_button, false);
	gtk_menu_button_set_child(GTK_MENU_BUTTON(view_button), view_icon);
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(view_button), view_popover);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(header), view_button);

	// 헤더바 - 정보 라벨
	rw->info_label = gtk_label_new("----");
	gtk_widget_set_halign(rw->info_label, GTK_ALIGN_END);
	gtk_widget_set_valign(rw->info_label, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_start(rw->info_label, 12);
	gtk_widget_set_margin_end(rw->info_label, 12);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(header), rw->info_label);

	// 헤더바 설정
	gtk_window_set_titlebar(GTK_WINDOW(rw->window), header);

	// DrawingArea
	rw->draw = read_draw_new(rw);
	gtk_widget_set_can_focus(rw->draw, true);
	gtk_widget_set_hexpand(rw->draw, true);
	gtk_widget_set_vexpand(rw->draw, true);

	// 메인 레이아웃
	GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_can_focus(main_box, false);
	gtk_box_append(GTK_BOX(main_box), rw->draw);
	gtk_window_set_child(GTK_WINDOW(rw->window), main_box);
#pragma endregion

#pragma region 컨트롤러
	// 파일 끌어다 놓기
	GtkDropTarget* drop = gtk_drop_target_new(G_TYPE_FILE, GDK_ACTION_COPY);
	g_signal_connect(drop, "drop", G_CALLBACK(signal_file_drop), rw);
	gtk_widget_add_controller(rw->draw, GTK_EVENT_CONTROLLER(drop));

	// 마우스 휠
	GtkEventController* wheel = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
	g_signal_connect(wheel, "scroll", G_CALLBACK(signal_wheel_scroll), rw);
	gtk_widget_add_controller(rw->draw, wheel);

	// 마우스 버튼
	GtkGesture* gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
	g_signal_connect(gesture, "pressed", G_CALLBACK(signal_mouse_click), rw);
	gtk_widget_add_controller(rw->draw, GTK_EVENT_CONTROLLER(gesture));

	// 키보드
	GtkEventController* keyboard = gtk_event_controller_key_new();
	g_signal_connect(keyboard, "key-pressed", G_CALLBACK(signal_key_pressed), rw);
	g_signal_connect(keyboard, "key-released", G_CALLBACK(signal_key_released), rw);
	gtk_widget_add_controller(rw->window, keyboard);

	// 단축키 컨트롤러...인데 이건 내 커스텀
	static const struct ShortcutAction
	{
		char* name;
		ShortcutFunc func;
	} actions[] =
	{
		{ "test", shortcut_on_test },
		{ "exit", shortcut_on_exit },
		{ "escape", shortcut_on_escape },
		{ "file_open", shortcut_on_file_open },
		{ "file_close", shortcut_on_file_close },
		{ "settings", shortcut_on_settings },
		{ "fullscreen", shortcut_on_fullscreen },
		{ NULL, NULL }
	};

	rw->scmd = g_hash_table_new(g_str_hash, g_str_equal);
	for (const struct ShortcutAction* act = actions; act->name; ++act)
		g_hash_table_insert(rw->scmd, act->name, act->func);

	shortcut_register();
#pragma endregion

	// 초기화를 끝내면서
	reset_focus(rw);

	return rw;
}

// 창 보이기
void read_window_show(const ReadWindow* rw)
{
	gtk_widget_set_visible(rw->window, true);
}
#pragma endregion

