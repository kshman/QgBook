#include "pch.h"
#include "configs.h"
#include "book.h"
#include "doumi.h"

/**
 * @file page_dialog.c
 * @brief 책의 쪽(페이지) 선택을 위한 다이얼로그 및 관련 객체 구현 파일입니다.
 *        페이지 목록 표시, 선택, 콜백 처리 등 쪽 이동 UI를 담당합니다.
 */

/**
 * @brief 쪽(페이지) 정보를 나타내는 객체 타입 선언
 *        GObject 기반으로 페이지 번호, 파일명, 날짜, 크기 정보를 가집니다.
 */
#define TYPE_PAGE_OBJECT (page_object_get_type())
G_DECLARE_FINAL_TYPE(PageObject, page_object, , PAGE_OBJECT, GObject)

/**
 * @brief 쪽(페이지) 정보 구조체
 */
typedef struct _PageObject
{
	GObject parent_instance; ///< GObject 상속
	int no;                  ///< 페이지 번호 (0부터 시작)
	gchar* name;             ///< 파일 이름
	time_t date;             ///< 파일 수정 날짜
	int64_t size;            ///< 파일 크기
} PageObject;

G_DEFINE_TYPE(PageObject, page_object, G_TYPE_OBJECT)

/**
 * @brief PageObject의 메모리 해제(파이널라이즈) 함수
 * @param object GObject 포인터
 */
static void page_object_finalize(GObject* object)
{
	PageObject* self = (PageObject*)object;
	g_free(self->name);
	G_OBJECT_CLASS(page_object_parent_class)->finalize(object);
}

/**
 * @brief PageObject 클래스 초기화 함수
 * @param klass PageObjectClass 포인터
 */
static void page_object_class_init(PageObjectClass* klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = page_object_finalize;
}

/**
 * @brief PageObject 인스턴스 초기화 함수
 * @param self PageObject 포인터
 */
static void page_object_init(PageObject* self)
{
	self->name = NULL;
	self->date = 0;
	self->size = 0;
}

/**
 * @brief 쪽(페이지) 선택 다이얼로그 구조체
 *        페이지 목록, 선택 모델, 콜백, 상태 플래그 등을 포함합니다.
 */
typedef struct PageDialog
{
	GtkWindow* window;           ///< 다이얼로그 윈도우 (상속)

	GtkWidget* page_info;        ///< 페이지 정보 레이블
	GtkWidget* page_list;        ///< 페이지 목록 뷰(GtkColumnView)
	GListStore* list_store;      ///< 페이지 데이터 저장소(GListStore<PageObject>)
	GtkSelectionModel* selection;///< 선택 모델(GtkSingleSelection)

	PageSelectCallback callback; ///< 페이지 선택 콜백
	gpointer user_data;          ///< 콜백에 전달할 사용자 데이터

	int selected;                ///< 선택된 페이지 인덱스
	bool disposed;               ///< 다이얼로그가 dispose 되었는지 여부
} PageDialog;

/**
 * @brief 선택 결과를 처리하고 콜백을 호출합니다.
 * @param self PageDialog 포인터
 * @param selected 선택된 페이지 인덱스(-1: 취소)
 */
static void response_selection(PageDialog* self, int selected)
{
	if (self->disposed)
		return; // 이미 dispose 되었으면 아무것도 안함
	self->selected = selected;
	gtk_widget_set_visible(GTK_WIDGET(self->window), false);
	if (self->callback == NULL)
		return; // 콜백이 없으면 아무것도 안함
	self->callback(self->user_data, selected);
}

/**
 * @brief 창 닫기 요청 시 호출되는 콜백
 * @param window 닫히는 GtkWindow
 * @param self PageDialog 포인터
 * @return true면 닫기 진행, false면 무시
 */
static gboolean on_window_close_request(GtkWindow* window, PageDialog* self)
{
	if (self->disposed)
		return false; // 이미 dispose 되었으면 닫기 요청 무시
	// 창 닫기(X) 동작을 막고 싶으면 TRUE 반환
	response_selection(self, -1);
	return true;
}

/**
 * @brief 페이지 목록에서 행 더블클릭(활성화) 시 호출되는 콜백
 * @param view GtkColumnView
 * @param position 선택된 행 인덱스
 * @param self PageDialog 포인터
 */
static void on_row_activated(GtkColumnView* view, guint position, PageDialog* self)
{
	response_selection(self, (int)position);
}

/**
 * @brief OK 버튼 클릭 시 호출되는 콜백
 * @param button GtkButton
 * @param self PageDialog 포인터
 */
static void on_ok_clicked(GtkButton* button, PageDialog* self)
{
	const guint pos = gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(self->selection));
	response_selection(self, (int)pos);
}

/**
 * @brief 취소 버튼 클릭 시 호출되는 콜백
 * @param button GtkButton
 * @param self PageDialog 포인터
 */
static void on_cancel_clicked(GtkButton* button, PageDialog* self)
{
	response_selection(self, -1);
}

/**
 * @brief 키 입력(ESC 등) 처리 콜백
 * @param controller 키 이벤트 컨트롤러
 * @param keyval 입력된 키 값
 * @param keycode 키 코드
 * @param state modifier 상태
 * @param self PageDialog 포인터
 * @return true면 이벤트 중단, false면 기본 동작
 */
static gboolean on_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, PageDialog* self)
{
	if (keyval == GDK_KEY_Escape)
	{
		response_selection(self, -1);
		return true;
	}
	return false;
}

/**
 * @brief 컬럼 셀 생성: 라벨 위젯을 생성하여 셀에 추가합니다.
 * @param factory GtkListItemFactory
 * @param item GtkListItem
 * @param user_data 사용자 데이터
 */
static void factory_setup_label(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_label_new("");
	gtk_widget_set_margin_start(label, 4); // 좌측 여백
	gtk_widget_set_margin_end(label, 4);   // 우측 여백
	gtk_list_item_set_child(item, label);
}

/**
 * @brief 파일명 컬럼 바인딩: PageObject의 name을 라벨에 표시
 */
static void factory_bind_name(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_list_item_get_child(item);
	PageObject* object = gtk_list_item_get_item(item);
	gtk_label_set_text(GTK_LABEL(label), object->name ? object->name : _("<unknown>"));
	gtk_widget_set_halign(label, GTK_ALIGN_START);
}

/**
 * @brief 페이지 번호 컬럼 바인딩: PageObject의 no를 라벨에 표시(1부터 시작)
 */
static void factory_bind_no(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_list_item_get_child(item);
	PageObject* object = gtk_list_item_get_item(item);

	char sz[64];
	g_snprintf(sz, sizeof(sz), "%d", object->no + 1); // 페이지 번호는 1부터 시작
	gtk_label_set_text(GTK_LABEL(label), sz);
	gtk_widget_set_halign(label, GTK_ALIGN_END);
}

/**
 * @brief 날짜 컬럼 바인딩: PageObject의 date를 라벨에 표시(YYYY-MM-DD)
 */
static void factory_bind_date(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_list_item_get_child(item);
	PageObject* object = gtk_list_item_get_item(item);

	struct tm tm;
#ifdef _MSC_VER
	(void)localtime_s(&tm, &object->date);
#else
	localtime_r(&object->date, &tm);
#endif
	char sz[64];
	// 날짜와 시간: %Y-%m-%d %H:%M:%S
	(void)strftime(sz, sizeof(sz), "%Y-%m-%d", &tm);
	gtk_label_set_text(GTK_LABEL(label), sz);
	gtk_widget_set_halign(label, GTK_ALIGN_END);
}

/**
 * @brief 파일 크기 컬럼 바인딩: PageObject의 size를 라벨에 표시(친화적 단위)
 */
static void factory_bind_size(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_list_item_get_child(item);
	PageObject* object = gtk_list_item_get_item(item);

	char sz[64];
	doumi_format_size_friendly(object->size, sz, sizeof(sz));
	gtk_label_set_text(GTK_LABEL(label), sz);
	gtk_widget_set_halign(label, GTK_ALIGN_END);
}

/**
 * @brief 다이얼로그에 책 정보를 설정(페이지 목록 갱신)
 * @param self PageDialog 포인터
 * @param book Book 객체 포인터
 */
void page_dialog_set_book(PageDialog* self, Book* book)
{
	char info[64];
	g_snprintf(info, sizeof(info), _("Total page: %d"), book->total_page);
	gtk_label_set_text(GTK_LABEL(self->page_info), info);

	g_list_store_remove_all(self->list_store);

	const GPtrArray* entries = book->entries;
	for (guint i = 0; i < entries->len; ++i)
	{
		PageEntry* e = g_ptr_array_index(entries, i);
		PageObject* o = g_object_new(TYPE_PAGE_OBJECT, NULL);
		o->no = e->page;
		o->name = g_strdup(e->name);
		o->date = e->date;
		o->size = e->size;
		g_list_store_append(self->list_store, o);
		g_object_unref(o); // GListStore가 참조를 가지므로 여기서 해제
	}
}

/**
 * @brief 다이얼로그의 책 정보 리셋(초기화)
 * @param self PageDialog 포인터
 */
void page_dialog_reset_book(PageDialog* self)
{
	gtk_label_set_text(GTK_LABEL(self->page_info), _("[No Book]"));
	g_list_store_remove_all(self->list_store);
}

/**
 * @brief 선택된 페이지를 갱신하고 뷰를 해당 위치로 스크롤
 * @param self PageDialog 포인터
 */
static void page_dialog_refresh_selection(PageDialog* self)
{
	const guint count = g_list_model_get_n_items(G_LIST_MODEL(self->list_store));
	const guint page = self->selected < 0 ? 0 : (self->selected >= (int)count ? count - 1 : self->selected);
	gtk_single_selection_set_selected(GTK_SINGLE_SELECTION(self->selection), page);
	gtk_column_view_scroll_to(
		GTK_COLUMN_VIEW(self->page_list),
		page,                   // 이동할 행 인덱스
		NULL,                   // 전체 행 기준
		GTK_LIST_SCROLL_FOCUS | GTK_LIST_SCROLL_SELECT, // 포커스+선택
		NULL                    // 기본 동작
	);
	gtk_widget_grab_focus(self->page_list);
}

/**
 * @brief 쪽(페이지) 선택 다이얼로그 생성 및 UI 구성
 * @param parent 부모 윈도우
 * @param callback 페이지 선택 콜백
 * @param user_data 콜백에 전달할 사용자 데이터
 * @return 생성된 PageDialog 포인터
 */
PageDialog* page_dialog_new(GtkWindow* parent, PageSelectCallback callback, gpointer user_data)
{
	PageDialog* self = g_new0(PageDialog, 1);

	self->callback = callback;
	self->user_data = user_data;

	self->window = GTK_WINDOW(gtk_window_new());
	gtk_window_set_transient_for(GTK_WINDOW(self->window), parent);
	gtk_window_set_title(self->window, _("Page selection"));
	gtk_window_set_default_size(self->window, 480, 580);
	gtk_widget_set_size_request(GTK_WIDGET(self->window), 400, 350);
	g_signal_connect(self->window, "close-request", G_CALLBACK(on_window_close_request), self);

	GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_window_set_child(self->window, vbox);

	// 상단 정보 라벨
	self->page_info = gtk_label_new("");
	gtk_widget_set_halign(self->page_info, GTK_ALIGN_START);
	gtk_widget_set_margin_top(self->page_info, 8);
	gtk_widget_set_margin_start(self->page_info, 8);
	gtk_widget_set_margin_end(self->page_info, 8);
	gtk_box_append(GTK_BOX(vbox), self->page_info);

	// 리스트와 선택 모델
	self->list_store = g_list_store_new(TYPE_PAGE_OBJECT);
	self->selection = GTK_SELECTION_MODEL(gtk_single_selection_new(G_LIST_MODEL(self->list_store)));

	// 컬럼뷰 및 팩토리
	GtkListItemFactory* factory_no = gtk_signal_list_item_factory_new();
	g_signal_connect(factory_no, "setup", G_CALLBACK(factory_setup_label), NULL);
	g_signal_connect(factory_no, "bind", G_CALLBACK(factory_bind_no), NULL);

	GtkListItemFactory* factory_name = gtk_signal_list_item_factory_new();
	g_signal_connect(factory_name, "setup", G_CALLBACK(factory_setup_label), NULL);
	g_signal_connect(factory_name, "bind", G_CALLBACK(factory_bind_name), NULL);

	GtkListItemFactory* factory_date = gtk_signal_list_item_factory_new();
	g_signal_connect(factory_date, "setup", G_CALLBACK(factory_setup_label), NULL);
	g_signal_connect(factory_date, "bind", G_CALLBACK(factory_bind_date), NULL);

	GtkListItemFactory* factory_size = gtk_signal_list_item_factory_new();
	g_signal_connect(factory_size, "setup", G_CALLBACK(factory_setup_label), NULL);
	g_signal_connect(factory_size, "bind", G_CALLBACK(factory_bind_size), NULL);

	GtkColumnViewColumn* col_no = gtk_column_view_column_new(_("No."), factory_no);
	GtkColumnViewColumn* col_name = gtk_column_view_column_new(_("Filename"), factory_name);
	GtkColumnViewColumn* col_date = gtk_column_view_column_new(_("Date"), factory_date);
	GtkColumnViewColumn* col_size = gtk_column_view_column_new(_("Size"), factory_size);

	gtk_column_view_column_set_expand(col_no, FALSE);
	gtk_column_view_column_set_expand(col_name, TRUE);
	gtk_column_view_column_set_expand(col_date, FALSE);
	gtk_column_view_column_set_expand(col_size, FALSE);

	self->page_list = gtk_column_view_new(self->selection);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(self->page_list), col_no);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(self->page_list), col_name);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(self->page_list), col_date);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(self->page_list), col_size);
	g_signal_connect(self->page_list, "activate", G_CALLBACK(on_row_activated), self);

	GtkWidget* scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), self->page_list);
	gtk_widget_set_hexpand(scrolled, TRUE);   // 가로로 확장
	gtk_widget_set_vexpand(scrolled, TRUE);   // 세로로 확장
	gtk_box_append(GTK_BOX(vbox), scrolled);

	// 버튼 박스
	GtkWidget* button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_bottom(button_box, 8);
	gtk_widget_set_margin_start(button_box, 8);
	gtk_widget_set_margin_end(button_box, 8);

	GtkWidget* cancel_btn = gtk_button_new_with_label(_("Cancel"));
	g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_cancel_clicked), self);

	GtkWidget* ok_btn = gtk_button_new_with_label(_("Go to page"));
	g_signal_connect(ok_btn, "clicked", G_CALLBACK(on_ok_clicked), self);

#ifdef _WIN32
	gtk_box_append(GTK_BOX(button_box), ok_btn);
	gtk_box_append(GTK_BOX(button_box), cancel_btn);
#else
	gtk_box_append(GTK_BOX(button_box), cancel_btn);
	gtk_box_append(GTK_BOX(button_box), ok_btn);
#endif

	gtk_widget_set_halign(button_box, GTK_ALIGN_CENTER);
	gtk_widget_set_hexpand(button_box, FALSE); // 가로 확장 안함
	gtk_widget_set_vexpand(button_box, FALSE); // 세로 확장 안함
	gtk_box_append(GTK_BOX(vbox), button_box);

	// 컨트롤러
	GtkEventController* key_controller = gtk_event_controller_key_new();
	g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_press), self);
	gtk_widget_add_controller(GTK_WIDGET(self->window), key_controller);

	//
	return self;
}

/**
 * @brief 쪽(페이지) 선택 다이얼로그를 해제(dispose)합니다.
 * @param self PageDialog 포인터
 */
void page_dialog_dispose(PageDialog* self)
{
	if (self->window)
	{
		self->disposed = true; // 다이얼로그가 dispose 되었음을 표시
		g_signal_handlers_disconnect_by_data(self->window, self);
		gtk_window_close(self->window);
	}
	g_free(self);
}

/**
 * @brief 쪽(페이지) 선택 다이얼로그를 비동기로 실행합니다.
 * @param self PageDialog 포인터
 * @param page 선택할 페이지 인덱스
 */
void page_dialog_show_async(PageDialog* self, int page)
{
	self->selected = page;
	page_dialog_refresh_selection(self);

	gtk_window_set_modal(GTK_WINDOW(self->window), true);
	gtk_window_present(GTK_WINDOW(self->window));
}

/**
 * @note
 * - PageDialog는 책의 페이지 목록을 표시하고, 사용자가 원하는 쪽을 선택할 수 있도록 지원합니다.
 * - GObject 기반의 PageObject를 사용하여 각 페이지 정보를 관리합니다.
 * - ESC, 더블클릭, 버튼 등 다양한 입력 방식으로 쪽 선택이 가능합니다.
 * - 콜백을 통해 선택 결과를 비동기로 전달합니다.
 */
