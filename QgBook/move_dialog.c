#include "pch.h"
#include "book.h"
#include "configs.h"

// 이동 선택용 정보
#define TYPE_MOVE_OBJECT (move_object_get_type())
G_DECLARE_FINAL_TYPE(MoveObject, move_object, , move_object, GObject)

typedef struct _MoveObject
{
	GObject parent_instance;
	gchar* alias; // 이동 별칭
	gchar* folder; // 대상 경로
} MoveObject;

G_DEFINE_TYPE(MoveObject, move_object, G_TYPE_OBJECT)

static void move_object_finalize(GObject* object)
{
	MoveObject* self = (MoveObject*)object;
	g_free(self->alias);
	g_free(self->folder);
	G_OBJECT_CLASS(move_object_parent_class)->finalize(object);
}

static void move_object_class_init(MoveObjectClass* klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = move_object_finalize;
}

static void move_object_init(MoveObject* self)
{
	self->alias = NULL;
	self->folder = NULL;
}

static MoveObject *move_object_new(const MoveLocation* location)
{
	MoveObject* obj = g_object_new(TYPE_MOVE_OBJECT, NULL);
	obj->alias = g_strdup(location->alias);
	obj->folder = g_strdup(location->folder);
	return obj;
}

// 이동 다이얼로그
typedef struct MoveDialog
{
	GtkWindow* window; // 상속

	GtkWidget* move_list; // GtkColumnView
	GListStore* list_store; // GListStore<MoveObject>
	GtkSelectionModel* selection; // GtkSingleSelection

	GtkWidget* browser_button; // 브라우저 버튼
	GtkWidget* dest_text; // 대상 경로 입력창

	GtkWidget* move_menu; // 메뉴
	GtkWidget* move_add_menu_item; // 이동 추가 메뉴 아이템
	GtkWidget* move_change_menu_item; // 이동 변경 메뉴 아이템
	GtkWidget* move_alias_menu_item; // 이동 별칭 메뉴 아이템
	GtkWidget* move_delete_menu_item; // 이동 삭제 메뉴 아이템

	char filename[2048];

	MoveCallback callback; // 선택 콜백
	gpointer user_data; // 콜백 사용자 데이터

	bool refreshing; // 새로 고침 중인지 여부
	bool modified; // 수정 여부
	bool result; // 결과 (성공 여부)
} MoveDialog;

// 마지막 선택 위치
static int s_last_selected = -1;

static void refresh_list(MoveDialog* self)
{
	self->refreshing = true;

	g_list_store_remove_all(self->list_store);

	const GPtrArray* locs = movloc_get_all_ptr();
	for (guint i = 0; i < locs->len; i++)
	{
		const MoveLocation* loc = g_ptr_array_index(locs, i);
		MoveObject* obj = move_object_new(loc);
		g_list_store_append(self->list_store, obj);
		g_object_unref(obj);
	}

	if (locs->len > 0)
	{
		if (s_last_selected >= 0 && s_last_selected < (int)locs->len)
			gtk_single_selection_set_selected(GTK_SINGLE_SELECTION(self->selection), s_last_selected);
		else
		{
			gtk_single_selection_set_selected(GTK_SINGLE_SELECTION(self->selection), 0);
			s_last_selected = 0; // 초기 선택 위치
		}

		gtk_column_view_scroll_to(
				GTK_COLUMN_VIEW(self->move_list),
				s_last_selected,
				NULL,
				GTK_LIST_SCROLL_FOCUS | GTK_LIST_SCROLL_SELECT,
				NULL);
	}
	gtk_widget_grab_focus(self->move_list);

	self->refreshing = false;
}

static void on_row_activated(GtkColumnView* view, guint pos, MoveDialog* self)
{
	self->result = true; // 선택됨
	gtk_window_close(self->window);
}

static void on_row_selected_notify(GObject* object, GParamSpec* pspec, MoveDialog* self)
{
	GtkSingleSelection* sel = GTK_SINGLE_SELECTION(self->selection);
	const guint count = g_list_model_get_n_items(G_LIST_MODEL(self->list_store));
	const guint idx = gtk_single_selection_get_selected(sel);
	if (count > 0 && idx != GTK_INVALID_LIST_POSITION)
	{
		MoveObject* obj = g_list_model_get_item(G_LIST_MODEL(self->list_store), idx);
		g_strlcpy(self->filename, obj->folder, sizeof(self->filename));
		g_object_unref(obj);
		gtk_editable_set_text(GTK_EDITABLE(self->dest_text), self->filename);
		if (!self->refreshing)
			s_last_selected = (int)idx; // 마지막 선택 위치 저장
	}
}

void cb_browse_click_finish(GObject* source_object, GAsyncResult* res, gpointer data)
{
	MoveDialog* self = data;
	GtkFileDialog* dlg = GTK_FILE_DIALOG(source_object);
	GFile* folder = gtk_file_dialog_select_folder_finish(dlg, res, NULL);
	if (folder)
	{
		char* path = g_file_get_path(folder);
		gtk_editable_set_text(GTK_EDITABLE(self->dest_text), path);
		g_free(path);
		g_object_unref(folder);
	}
	g_object_unref(dlg);
}

static void on_browse_clicked(GtkButton* btn, MoveDialog* self)
{
	GtkFileDialog* dlg = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dlg, _("Select directory"));
	gtk_file_dialog_set_accept_label(dlg, _("Select"));
	gtk_file_dialog_set_modal(dlg, TRUE);

	const char* last = gtk_editable_get_text(GTK_EDITABLE(self->dest_text));
	if (!last || *last == '\0')
		last = config_get_string_ptr(CONFIG_FILE_LAST_DIRECTORY, false);

	GFile* initial_folder = g_file_new_for_path(last);
	if (g_file_query_exists(initial_folder, NULL))
		gtk_file_dialog_set_initial_folder(dlg, initial_folder);
	else
	{
		// 초기 폴더가 존재하지 않으면 홈 디렉토리로 설정
		GFile* home = g_file_new_for_path(g_get_home_dir());
		gtk_file_dialog_set_initial_folder(dlg, home);
		g_object_unref(home);
	}
	g_object_unref(initial_folder);

	gtk_file_dialog_select_folder(dlg, self->window, NULL, cb_browse_click_finish, self);
}

// OK 콜백
static void ok_callback(GtkWidget* widget, MoveDialog* self)
{
	self->result = true; // 선택됨
	gtk_window_close(self->window);
}

// 다시 열기 콜백
static void cancel_callback(GtkWidget* widget, MoveDialog* self)
{
	self->result = false;
	gtk_window_close(self->window);
}

// 키 눌림
static gboolean signal_key_pressed(
		GtkEventControllerKey* controller,
		guint keyval,
		guint keycode,
		GdkModifierType state,
		MoveDialog* self)
{
	if (keyval == GDK_KEY_Escape)
	{
		// Esc 키 입력 처리
		cancel_callback(NULL, self);
		return true; // 이벤트 중단
	}

	return false; // 기본 동작 계속
}

// 창이 닫힐 때 호출되는 콜백
static void signal_destroy(GtkWindow* window, MoveDialog* self)
{
	if (self->modified)
	{
		// 이동 목록이 수정되었으면 설정에 저장
		// TODO: 이동 목록을 설정에 저장하는 로직 추가
	}

	if (self->callback && self->result && self->filename[0] != '\0')
	{
		self->callback(self->user_data, self->filename); // 콜백 호출
		// 취소나 닫기 시에는 콜백을 호출하지 않음
	}

	g_free(self);
}

static void factory_setup_label(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_label_new("");
	gtk_widget_set_margin_start(label, 4); // 좌측 여백
	gtk_widget_set_margin_end(label, 4); // 우측 여백
	gtk_list_item_set_child(item, label);
}

static void factory_bind_alias(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_list_item_get_child(item);
	MoveObject* object = gtk_list_item_get_item(item);
	gtk_label_set_text(GTK_LABEL(label), object->alias);
	gtk_widget_set_halign(label, GTK_ALIGN_START);
}

static void factory_bind_path(GtkListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
	GtkWidget* label = gtk_list_item_get_child(item);
	MoveObject* object = gtk_list_item_get_item(item);
	gtk_label_set_text(GTK_LABEL(label), object->folder);
	gtk_widget_set_halign(label, GTK_ALIGN_START);
}

// 만들기
static MoveDialog *move_dialog_new(GtkWindow* parent)
{
	MoveDialog* self = g_new0(MoveDialog, 1);

	self->window = GTK_WINDOW(gtk_window_new());
	gtk_window_set_transient_for(self->window, parent);
	gtk_window_set_title(self->window, _("Move book"));
	gtk_window_set_modal(self->window, TRUE);
	gtk_window_set_default_size(self->window, 500, 550);
	g_signal_connect(self->window, "destroy", G_CALLBACK(signal_destroy), self);

	// 콘텐츠 영역
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

	// 라벨
	GtkWidget* label = gtk_label_new(_("Select destination for moving book"));
	gtk_widget_set_halign(label, GTK_ALIGN_START);

	// 리스트
	self->list_store = g_list_store_new(TYPE_MOVE_OBJECT);
	self->selection = GTK_SELECTION_MODEL(gtk_single_selection_new(G_LIST_MODEL(self->list_store)));

	GtkListItemFactory* factory_alias = gtk_signal_list_item_factory_new();
	g_signal_connect(factory_alias, "setup", G_CALLBACK(factory_setup_label), NULL);
	g_signal_connect(factory_alias, "bind", G_CALLBACK(factory_bind_alias), NULL);

	GtkListItemFactory* factory_path = gtk_signal_list_item_factory_new();
	g_signal_connect(factory_path, "setup", G_CALLBACK(factory_setup_label), NULL);
	g_signal_connect(factory_path, "bind", G_CALLBACK(factory_bind_path), NULL);

	GtkColumnViewColumn* col_alias = gtk_column_view_column_new(_("Alias"), factory_alias);
	GtkColumnViewColumn* col_path = gtk_column_view_column_new(_("Directory"), factory_path);

	gtk_column_view_column_set_expand(col_alias, FALSE);
	gtk_column_view_column_set_expand(col_path, TRUE);

	self->move_list = gtk_column_view_new(self->selection);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(self->move_list), col_alias);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(self->move_list), col_path);
	g_signal_connect(self->move_list, "activate", G_CALLBACK(on_row_activated), self);
	g_signal_connect(self->selection, "notify::selected", G_CALLBACK(on_row_selected_notify), self);

	GtkWidget* scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), self->move_list);
	gtk_widget_set_hexpand(scrolled, TRUE); // 가로로 확장
	gtk_widget_set_vexpand(scrolled, TRUE); // 세로로 확장

	// 경로 입력
	GtkWidget* dest_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

	self->dest_text = gtk_entry_new();
	gtk_widget_set_hexpand(self->dest_text, TRUE);
	gtk_box_append(GTK_BOX(dest_box), self->dest_text);

	self->browser_button = gtk_button_new_with_label(_("Browse"));
	g_signal_connect(self->browser_button, "clicked", G_CALLBACK(on_browse_clicked), self);
	gtk_box_append(GTK_BOX(dest_box), self->browser_button);

	// 메뉴 (PopoverMenu)
	// ... (page_dialog.c 참고, 메뉴 항목 생성 및 연결)

	// OK/Cancel 버튼
	GtkWidget* button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_top(button_box, 16);
	gtk_widget_set_margin_bottom(button_box, 4);
	gtk_widget_set_halign(button_box, GTK_ALIGN_CENTER);

	GtkWidget* cancel_button = gtk_button_new_with_label(_("Cancel"));
	gtk_widget_set_focus_on_click(cancel_button, true);
	g_signal_connect(cancel_button, "clicked", G_CALLBACK(cancel_callback), self);

	GtkWidget* ok_button = gtk_button_new_with_label(_("OK"));
	gtk_widget_set_focus_on_click(ok_button, true);
	g_signal_connect(ok_button, "clicked", G_CALLBACK(ok_callback), self);

#ifdef _WIN32
	gtk_box_append(GTK_BOX(button_box), ok_button);
	gtk_box_append(GTK_BOX(button_box), cancel_button);
#else
	gtk_box_append(GTK_BOX(button_box), cancel_button);
	gtk_box_append(GTK_BOX(button_box), ok_button);
#endif

	gtk_box_append(GTK_BOX(box), label);
	gtk_box_append(GTK_BOX(box), scrolled);
	gtk_box_append(GTK_BOX(box), dest_box);
	gtk_box_append(GTK_BOX(box), button_box);
	gtk_window_set_child(self->window, box);

	// 컨트롤러
	GtkEventController* key_controller = gtk_event_controller_key_new();
	g_signal_connect(key_controller, "key-pressed", G_CALLBACK(signal_key_pressed), self);
	gtk_widget_add_controller(GTK_WIDGET(self->window), GTK_EVENT_CONTROLLER(key_controller));

	//
	refresh_list(self);

	return self;
}

// 이동 다이얼로그 비동기로 열기
void move_dialog_show_async(GtkWindow* parent, MoveCallback callback, gpointer user_data)
{
	MoveDialog* self = move_dialog_new(parent);

	self->callback = callback;
	self->user_data = user_data;

	gtk_window_present(self->window); // 비동기로 열기
}
