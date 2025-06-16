#include "pch.h"
#include "book.h"
#include "configs.h"

// 앞서 선언
typedef struct ReadWindow ReadWindow;

// 그리기 위젯 정의
typedef struct
{
	GtkWidget parent;
	ReadWindow* read_window;
} ReadDrawWidget;

typedef struct
{
	GtkWidgetClass parent_class;
} ReadDrawWidgetClass;

G_DEFINE_TYPE(ReadDrawWidget, read_draw_widget, GTK_TYPE_WIDGET)

static void read_draw_widget_snapshot(GtkWidget* widget, GtkSnapshot* snapshot);

static GtkWidget* read_draw_widget_new(ReadWindow* rw)
{
	GtkWidget* widget = GTK_WIDGET(g_object_new(read_draw_widget_get_type(), NULL));
	((ReadDrawWidget*)widget)->read_window = rw;
	return widget;
}

static void read_draw_widget_class_init(ReadDrawWidgetClass* klass)
{
	GtkWidgetClass* widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->snapshot = read_draw_widget_snapshot;
}

static void read_draw_widget_init(ReadDrawWidget* self) {}

// 데이터 구조체 예시
struct ReadWindow
{
	// 윈도우
	GtkWidget* window;
	GtkWidget* menu_quality_drop;
	GtkWidget* menu_zoom_check;
	GtkWidget* menu_vmode_image;
	GtkWidget* menu_vmode_radios[VIEW_MODE_MAX_VALUE];
	GtkWidget* menu_vquality_radios[VIEW_QUALITY_MAX_VALUE];

	GtkWidget* draw;
	GtkWidget* title_label;
	GtkWidget* info_label;
	GtkWidget* direction_image;

	// 책 상태
	bool view_zoom;
	ViewMode view_mode;
	ViewQuality view_quality;

	// 알림
	guint32 notify_id;
	char* notify_text;
};

// 시그날 콜백
static void signal_destroy(GtkWidget* widget, ReadWindow* rw);
static gboolean signal_close_request(GtkWindow* window, ReadWindow* rw);
static void signal_notify(GObject* object, GParamSpec* pspec, ReadWindow* rw);

// 인터페이스 콜백
static void menu_file_open_clicked(GtkButton* button, ReadWindow* rw);
static void menu_file_close_clicked(GtkButton* button, ReadWindow* rw);
static void menu_settings_click(GtkButton* button, ReadWindow* rw);
static void menu_exit_click(GtkButton* button, ReadWindow* rw);
static void menu_view_zoom_toggled(GtkCheckButton* button, ReadWindow* rw);
static void menu_view_mode_toggled(GtkCheckButton* button, ReadWindow* rw);
static void menu_view_mode_clicked(GtkButton* button, ReadWindow* rw);
static void menu_view_quality_toggled(GtkCheckButton* button, ReadWindow* rw);

// ReadWindow 함수
static void update_view_zoom(ReadWindow* rw, bool zoom, bool redraw);
static void update_view_mode(ReadWindow* rw, ViewMode mode, bool redraw);
static void update_view_quality(ReadWindow* rw, ViewQuality quality, bool redraw);

//
ReadWindow* read_window_new(GtkApplication* app)
{
	ReadWindow* rw = g_new0(ReadWindow, 1);

	// 설정을 위해 더미값 넣어놓기
	rw->view_zoom = configs_get_bool(CONFIG_VIEW_ZOOM, true);
	rw->view_mode = (ViewMode)configs_get_int(CONFIG_VIEW_MODE, true);
	rw->view_quality = (ViewQuality)configs_get_int(CONFIG_VIEW_QUALITY, true);

	// 윈도우
	int width = configs_get_int(CONFIG_WINDOW_WIDTH, true);
	int height = configs_get_int(CONFIG_WINDOW_HEIGHT, true);

	rw->window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(rw->window), _("QgBook"));
	gtk_window_set_icon_name(GTK_WINDOW(rw->window), _("QgBook"));
	gtk_window_set_default_size(GTK_WINDOW(rw->window), width, height);
	gtk_widget_set_size_request(GTK_WIDGET(rw->window), 600, 400);
	g_signal_connect(rw->window, "destroy", G_CALLBACK(signal_destroy), rw);
	g_signal_connect(rw->window, "close-request", G_CALLBACK(signal_close_request), rw);
	g_signal_connect(rw->window, "notify", G_CALLBACK(signal_notify), rw);

	// 메인 메뉴, 헤더바에서 쓸거라 앞쪽에서 만들어 두자
	GtkWidget* main_popover = gtk_popover_new();
	GtkWidget* menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	// 메뉴 - 파일 열기
	GtkWidget* menu_file_open = gtk_button_new_with_label(_("Open"));
	g_signal_connect(menu_file_open, "clicked", G_CALLBACK(menu_file_open_clicked), rw);
	gtk_box_append(GTK_BOX(menu_box), menu_file_open);

	// 메뉴 - 파일 닫기
	GtkWidget* menu_file_close = gtk_button_new_with_label(_("Close"));
	gtk_widget_set_sensitive(menu_file_close, false); // 첨엔 파일이 없으니깐 비활성화
	g_signal_connect(menu_file_close, "clicked", G_CALLBACK(menu_file_close_clicked), rw);
	gtk_box_append(GTK_BOX(menu_box), menu_file_close);

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
	gtk_menu_button_set_child(GTK_MENU_BUTTON(menu_button), menu_icon);
	//g_object_set(menu_button, "has-arrow", FALSE, NULL);
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu_button), main_popover);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_button);

	// 헤더바 - 보기 메뉴 버튼
	GtkWidget* view_button = gtk_menu_button_new();
	GtkWidget* view_icon = gtk_image_new_from_paintable(GDK_PAINTABLE(res_get_texture(RES_ICON_PAINTING)));
	gtk_menu_button_set_child(GTK_MENU_BUTTON(view_button), view_icon);
	//g_object_set(menu_button, "has-arrow", FALSE, NULL);
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
	rw->draw = read_draw_widget_new(rw);
	gtk_widget_set_hexpand(rw->draw, TRUE);
	gtk_widget_set_vexpand(rw->draw, TRUE);

	// 메인 레이아웃
	GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_append(GTK_BOX(main_box), rw->draw);
	gtk_window_set_child(GTK_WINDOW(rw->window), main_box);

	// 초기화를 끝내면서

	return rw;
}

// 창 보이기
void read_window_show(const ReadWindow* rw)
{
	gtk_widget_set_visible(rw->window, true);
}

// 윈도우 종료되고 나서 콜백
static void signal_destroy(GtkWidget* widget, ReadWindow* rw)
{
	if (rw->notify_text)
		g_free(rw->notify_text);

	// 여기서 해제하면 된다구
	g_free(rw);
}

// 윈도우 닫기 요청 콜백
static gboolean signal_close_request(GtkWindow* window, ReadWindow* rw)
{
	return false; // 닫힘 허용, true를 반환하면 윈도우가 안 닫힘
}

// 윈도우 각종 알림 콜백
static void signal_notify(GObject* object, GParamSpec* pspec, ReadWindow* rw)
{
	const char* name = g_param_spec_get_name(pspec);
	if (g_strcmp0(name, "default-width") == 0 || g_strcmp0(name, "default-height"))
	{
		// 윈도우 기본 크기 변경
		int width, height;
		gtk_window_get_default_size(GTK_WINDOW(rw->window), &width, &height);
		configs_set_int(CONFIG_WINDOW_WIDTH, width, true);
		configs_set_int(CONFIG_WINDOW_HEIGHT, height, true);
	}
	else
	{
		// 그 밖에 이벤트가 뭔지 좀 봅시다
		g_print(_("Property changed: %s\n"), name);
	}
}

// 늘려보기 설정과 메뉴 처리
static void update_view_zoom(ReadWindow* rw, bool zoom, bool redraw)
{
	if (rw->view_zoom == zoom)
		return;

	rw->view_zoom = zoom;
	configs_set_bool(CONFIG_VIEW_ZOOM, zoom, false);

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
	configs_set_int(CONFIG_VIEW_MODE, mode, false);

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
	configs_set_int(CONFIG_VIEW_QUALITY, quality, false);

	gtk_check_button_set_active(GTK_CHECK_BUTTON(rw->menu_vquality_radios[quality]), true);

	if (redraw)
	{
		// 책 그리기
		gtk_widget_queue_draw(rw->draw);
	}
}

// 파일 열기 누르기
static void menu_file_open_clicked(GtkButton* button, ReadWindow* rw)
{
	// 메시지 박스 테스트
	doumi_mesg_box(GTK_WINDOW(rw->window), "Only 1 instance allow", "Program will be exit");
}

// 파일 닫기 누르기
static void menu_file_close_clicked(GtkButton* button, ReadWindow* rw)
{}

// 설정 누르기
static void menu_settings_click(GtkButton* button, ReadWindow* rw)
{}

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
		rw->view_mode == VIEW_MODE_FIT ? VIEW_MODE_LEFT_TO_RIGHT :
		rw->view_mode == VIEW_MODE_LEFT_TO_RIGHT ? VIEW_MODE_RIGHT_TO_LEFT :
		/*rw->view_mode == VIEW_MODE_RIGHT_TO_LEFT ? VIEW_MODE_FIT :*/ VIEW_MODE_FIT;
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

// 스냅샷 재정의로 책 윈도우를 그리자고
static void read_draw_widget_snapshot(GtkWidget* widget, GtkSnapshot* snapshot)
{
	ReadDrawWidget* self = (ReadDrawWidget*)widget;
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
}
