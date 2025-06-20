#include "pch.h"
#include "configs.h"
#include "book.h"

// 쪽 선택 다이얼로그
typedef struct PageDialog
{
	GtkWindow window; // 상속

	GtkWidget* page_info;      // 페이지 정보 레이블
	GtkWidget* page_list;      // GtkColumnView
	GListStore* list_store;    // GListStore<PageEntry>
	GtkSelectionModel* selection; // GtkSingleSelection

	int selected;
	bool result;
} PageDialog;

static void on_row_activated(GtkColumnView* view, guint position, PageDialog* self)
{
	self->result = true;
	self->selected = (int)position;
	gtk_widget_set_visible(GTK_WIDGET(self), false);
}

static void on_ok_clicked(GtkButton* button, PageDialog* self)
{
	self->result = true;
	const guint pos = gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(self->selection));
	self->selected = (int)pos;
	gtk_widget_set_visible(GTK_WIDGET(self), false);
}

static void on_cancel_clicked(GtkButton* button, PageDialog* self)
{
	self->result = false;
	gtk_widget_set_visible(GTK_WIDGET(self), false);
}

static gboolean on_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, PageDialog* self)
{
	if (keyval == GDK_KEY_Escape)
	{
		self->result = false;
		gtk_widget_set_visible(GTK_WIDGET(self), false);
		return true;
	}
	return false;
}

// 셀 팩토리: 각 컬럼별 텍스트 반환
static void factory_setup_name(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_label_new("");
	gtk_list_item_set_child(item, label);
}

static void factory_bind_name(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_list_item_get_child(item);
	PageEntry* entry = gtk_list_item_get_item(item);
	gtk_label_set_text(GTK_LABEL(label), entry->name ? entry->name : "<unknown>");
}

static void factory_setup_date(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_label_new("");
	gtk_list_item_set_child(item, label);
}

static void factory_bind_date(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_list_item_get_child(item);
	PageEntry* entry = gtk_list_item_get_item(item);
	gtk_label_set_text(GTK_LABEL(label), entry->date ? entry->date : "");
}

static void factory_setup_size(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_label_new("");
	gtk_list_item_set_child(item, label);
}

static void factory_bind_size(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_list_item_get_child(item);
	PageEntry* entry = gtk_list_item_get_item(item);
	gtk_label_set_text(GTK_LABEL(label), entry->size ? entry->size : "");
}

static void page_dialog_init(PageDialog* dlg)
{
	gtk_window_set_title(GTK_WINDOW(dlg), "페이지 선택");
	gtk_window_set_default_size(GTK_WINDOW(dlg), 434, 511);
	gtk_window_set_resizable(GTK_WINDOW(dlg), false);

	GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_window_set_child(GTK_WINDOW(dlg), vbox);

	// 상단 정보 라벨
	dlg->page_info = gtk_label_new("페이지");
	gtk_widget_set_halign(dlg->page_info, GTK_ALIGN_START);
	gtk_widget_set_margin_top(dlg->page_info, 8);
	gtk_widget_set_margin_start(dlg->page_info, 8);
	gtk_widget_set_margin_end(dlg->page_info, 8);
	gtk_box_append(GTK_BOX(vbox), dlg->page_info);

	// GListStore<PageEntry>
	dlg->list_store = g_list_store_new(G_TYPE_POINTER);

	// SingleSelection
	dlg->selection = gtk_single_selection_new(G_LIST_MODEL(dlg->list_store));

	// 컬럼뷰 및 팩토리
	GtkListItemFactory* factory_name = gtk_signal_list_item_factory_new();
	g_signal_connect(factory_name, "setup", G_CALLBACK(factory_setup_name), NULL);
	g_signal_connect(factory_name, "bind", G_CALLBACK(factory_bind_name), NULL);

	GtkListItemFactory* factory_date = gtk_signal_list_item_factory_new();
	g_signal_connect(factory_date, "setup", G_CALLBACK(factory_setup_date), NULL);
	g_signal_connect(factory_date, "bind", G_CALLBACK(factory_bind_date), NULL);

	GtkListItemFactory* factory_size = gtk_signal_list_item_factory_new();
	g_signal_connect(factory_size, "setup", G_CALLBACK(factory_setup_size), NULL);
	g_signal_connect(factory_size, "bind", G_CALLBACK(factory_bind_size), NULL);

	GtkColumnViewColumn* col_name = gtk_column_view_column_new("파일 이름", factory_name);
	GtkColumnViewColumn* col_date = gtk_column_view_column_new("날짜", factory_date);
	GtkColumnViewColumn* col_size = gtk_column_view_column_new("크기", factory_size);

	dlg->page_list = gtk_column_view_new(dlg->selection);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(dlg->page_list), col_name);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(dlg->page_list), col_date);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(dlg->page_list), col_size);

	GtkWidget* scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), dlg->page_list);
	gtk_widget_set_size_request(scrolled, 400, 350);
	gtk_widget_set_margin_start(scrolled, 8);
	gtk_widget_set_margin_end(scrolled, 8);
	gtk_box_append(GTK_BOX(vbox), scrolled);

	// 버튼 박스
	GtkWidget* button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_bottom(button_box, 8);
	gtk_widget_set_margin_start(button_box, 8);
	gtk_widget_set_margin_end(button_box, 8);

	GtkWidget* cancel_btn = gtk_button_new_with_label("취소");
	GtkWidget* ok_btn = gtk_button_new_with_label("이동");
	gtk_box_append(GTK_BOX(button_box), cancel_btn);
	gtk_box_append(GTK_BOX(button_box), ok_btn);
	gtk_box_append(GTK_BOX(vbox), button_box);

	// 이벤트 연결
	g_signal_connect(dlg->page_list, "activate", G_CALLBACK(on_row_activated), dlg);
	g_signal_connect(ok_btn, "clicked", G_CALLBACK(on_ok_clicked), dlg);
	g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_cancel_clicked), dlg);

	GtkEventController* key_controller = gtk_event_controller_key_new();
	g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_press), dlg);
	gtk_widget_add_controller(GTK_WIDGET(dlg), key_controller);
}

// 책 정보 설정
void page_dialog_set_book(PageDialog* dlg, Book* book)
{
	char info[64];
	snprintf(info, sizeof(info), "총 페이지: %d", book_total_page(book));
	gtk_label_set_text(GTK_LABEL(dlg->page_info), info);

	// 기존 데이터 해제
	guint n_items = g_list_model_get_n_items(G_LIST_MODEL(dlg->list_store));
	for (guint i = 0; i < n_items; ++i)
	{
		PageEntry* entry = g_list_model_get_item(G_LIST_MODEL(dlg->list_store), i);
		page_entry_free(entry);
	}
	g_list_store_remove_all(dlg->list_store);

	int n = 0;
	BookEntryInfo* entries = book_get_entries_info(book);
	int count = book_entry_count(book);
	for (int i = 0; i < count; ++i)
	{
		PageEntry* entry = g_new0(PageEntry, 1);
		entry->name = g_strdup(entries[i].name ? entries[i].name : "<알 수 없음>");
		entry->date = g_strdup(entries[i].date_str);
		entry->size = g_strdup(entries[i].size_str);
		entry->index = n++;
		g_list_store_append(dlg->list_store, entry);
	}
}

// 책 정보 리셋
void page_dialog_reset_book(PageDialog* dlg)
{
	gtk_label_set_text(GTK_LABEL(dlg->page_info), "열린 책 없음");
	guint n_items = g_list_model_get_n_items(G_LIST_MODEL(dlg->list_store));
	for (guint i = 0; i < n_items; ++i)
	{
		PageEntry* entry = g_list_model_get_item(G_LIST_MODEL(dlg->list_store), i);
		page_entry_free(entry);
	}
	g_list_store_remove_all(dlg->list_store);
}

// 선택 갱신
static void page_dialog_refresh_selection(PageDialog* dlg)
{
	guint count = g_list_model_get_n_items(G_LIST_MODEL(dlg->list_store));
	guint page = dlg->selected < 0 ? 0 : (dlg->selected >= (int)count ? count - 1 : dlg->selected);
	gtk_single_selection_set_selected(GTK_SINGLE_SELECTION(dlg->selection), page);
	gtk_widget_grab_focus(dlg->page_list);
}

// 다이얼로그 실행
gboolean page_dialog_run(PageDialog* dlg, GtkWindow* parent, int page)
{
	if (parent)
		gtk_window_set_transient_for(GTK_WINDOW(dlg), parent);

	dlg->selected = page;
	page_dialog_refresh_selection(dlg);

	gtk_window_set_modal(GTK_WINDOW(dlg), true);
	gtk_window_present(GTK_WINDOW(dlg));

	// 메시지 루프: 창이 닫힐 때까지 대기
	while (gtk_widget_get_visible(GTK_WIDGET(dlg)))
	{
		while (g_main_context_iteration(NULL, false));
	}
	return dlg->result;
}
