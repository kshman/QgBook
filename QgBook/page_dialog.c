#include "pch.h"
#include "configs.h"
#include "book.h"
#include "doumi.h"

// 쪽 선택용 쪽 정보
#define TYPE_PAGE_OBJECT (page_object_get_type())
G_DECLARE_FINAL_TYPE(PageObject, page_object, , PAGE_OBJECT, GObject)

typedef struct _PageObject
{
	GObject parent_instance;
	int no; // 페이지 번호 (0부터 시작)
	gchar* name; // 파일 이름
	time_t date; // 파일 수정 날짜
	int64_t size; // 파일 크기
} PageObject;

G_DEFINE_TYPE(PageObject, page_object, G_TYPE_OBJECT)

static void page_object_finalize(GObject* object)
{
	PageObject* self = (PageObject*)object;
	g_free(self->name);
	G_OBJECT_CLASS(page_object_parent_class)->finalize(object);
}

static void page_object_class_init(PageObjectClass* klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = page_object_finalize;
}

static void page_object_init(PageObject* self)
{
	self->name = NULL;
	self->date = 0;
	self->size = 0;
}


// 쪽 선택 다이얼로그
typedef struct PageDialog
{
	GtkWindow* window; // 상속

	GtkWidget* page_info;      // 페이지 정보 레이블
	GtkWidget* page_list;      // GtkColumnView
	GListStore* list_store;    // GListStore<PageEntry>
	GtkSelectionModel* selection; // GtkSingleSelection

	PageSelectCallback callback; // 선택 콜백
	gpointer user_data; // 콜백 사용자 데이터

	int selected;
	bool disposed;
} PageDialog;

static void reponse_selection(PageDialog* self, int selected)
{
	if (self->disposed)
		return; // 이미 dispose 되었으면 아무것도 안함
	self->selected = selected;
	gtk_widget_set_visible(GTK_WIDGET(self->window), false);
	if (self->callback == NULL)
		return; // 콜백이 없으면 아무것도 안함
	self->callback(self->user_data, selected);
}

static gboolean on_window_close_request(GtkWindow* window, PageDialog* self)
{
	if (self->disposed)
		return false; // 이미 dispose 되었으면 닫기 요청 무시
	// 창 닫기(X) 동작을 막고 싶으면 TRUE 반환
	reponse_selection(self, -1);
	return true;
}

static void on_row_activated(GtkColumnView* view, guint position, PageDialog* self)
{
	reponse_selection(self, (int)position);
}

static void on_ok_clicked(GtkButton* button, PageDialog* self)
{
	const guint pos = gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(self->selection));
	reponse_selection(self, (int)pos);
}

static void on_cancel_clicked(GtkButton* button, PageDialog* self)
{
	reponse_selection(self, -1);
}

static gboolean on_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, PageDialog* self)
{
	if (keyval == GDK_KEY_Escape)
	{
		reponse_selection(self, -1);
		return true;
	}
	return false;
}

// 셀 팩토리: 각 컬럼별 텍스트 반환
static void factory_setup_label(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_label_new("");
	gtk_list_item_set_child(item, label);
}

static void factory_bind_name(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_list_item_get_child(item);
	PageObject* object = gtk_list_item_get_item(item);
	gtk_label_set_text(GTK_LABEL(label), object->name ? object->name : _("<unknown>"));
	gtk_widget_set_halign(label, GTK_ALIGN_START);
}

static void factory_bind_no(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_list_item_get_child(item);
	PageObject* object = gtk_list_item_get_item(item);

	char sz[64];
	g_snprintf(sz, sizeof(sz), "%d", object->no + 1); // 페이지 번호는 1부터 시작
	gtk_label_set_text(GTK_LABEL(label), sz);
	gtk_widget_set_halign(label, GTK_ALIGN_END);
}

static void factory_bind_date(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_list_item_get_child(item);
	PageObject* object = gtk_list_item_get_item(item);

	struct tm tm;
#ifdef _MSC_VER
	(void)localtime_s(&tm, &object->date);
#else
	localtime_r(&entry->date, &tm);
#endif
	char sz[64];
	// 날짜와 시간: %Y-%m-%d %H:%M:%S
	(void)strftime(sz, sizeof(sz), "%Y-%m-%d", &tm);
	gtk_label_set_text(GTK_LABEL(label), sz);
	gtk_widget_set_halign(label, GTK_ALIGN_END);
}

static void factory_bind_size(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_list_item_get_child(item);
	PageObject* object = gtk_list_item_get_item(item);

	char sz[64];
	doumi_format_size_friendly(object->size, sz, sizeof(sz));
	gtk_label_set_text(GTK_LABEL(label), sz);
	gtk_widget_set_halign(label, GTK_ALIGN_END);
}

// 책 정보 설정
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

// 책 정보 리셋
void page_dialog_reset_book(PageDialog* self)
{
	gtk_label_set_text(GTK_LABEL(self->page_info), _("[No Book]"));
	g_list_store_remove_all(self->list_store);
}

// 선택 갱신
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

// 쪽 다이얼로그 만들기ㅣ
PageDialog* page_dialog_new(GtkWindow* parent, PageSelectCallback callback, gpointer user_data)
{
	PageDialog* self = g_new0(PageDialog, 1);

	self->callback = callback;
	self->user_data = user_data;

	self->window = GTK_WINDOW(gtk_window_new());
	gtk_window_set_transient_for(GTK_WINDOW(self->window), parent);
	gtk_window_set_title(self->window, _("Page selection"));
	gtk_window_set_default_size(self->window, 480, 580);
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

// 다이얼로그 지우기
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

// 다이얼로그 실행
void page_dialog_show_async(PageDialog* self, int page)
{
	self->selected = page;
	page_dialog_refresh_selection(self);

	gtk_window_set_modal(GTK_WINDOW(self->window), true);
	gtk_window_present(GTK_WINDOW(self->window));
}
