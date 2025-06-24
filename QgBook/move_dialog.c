#include "pch.h"
#include "configs.h"
#include "doumi.h"

// 이동 선택용 정보
#define TYPE_MOVE_OBJECT (move_object_get_type())
G_DECLARE_FINAL_TYPE(MoveObject, move_object, , move_object, GObject)

typedef struct _MoveObject
{
	GObject parent_instance;
	int no; // 이동 위치 번호
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
	self->no = -1; // 초기 번호는 -1
	self->alias = NULL;
	self->folder = NULL;
}

static MoveObject* move_object_new(const MoveLocation* location)
{
	MoveObject* obj = g_object_new(TYPE_MOVE_OBJECT, NULL);
	obj->no = location->no;
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
	GtkWidget* dest; // 대상 경로 입력창

	MoveCallback callback; // 선택 콜백
	gpointer user_data; // 콜백 사용자 데이터

	guint selected_index;	// 현재 선택된 인덱스

	char filename[2048];

	bool refreshing; // 새로 고침 중인지 여부
	bool modified; // 수정 여부
	bool result; // 결과 (성공 여부)
} MoveDialog;

// 마지막 선택 위치, 창을 닫았다 열어도 유지
static int s_last_selected = -1;

// 목록을 설정에서 가져와서 새로 고침
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

// 디렉토리 이름으로 항목 선택
static bool ensure_folder(MoveDialog* self, const char* folder)
{
	if (!folder || *folder == '\0')
		return false;

	const guint count = g_list_model_get_n_items(G_LIST_MODEL(self->list_store));
	for (guint i = 0; i < count; ++i)
	{
		MoveObject* obj = g_list_model_get_item(G_LIST_MODEL(self->list_store), i);
		if (!obj)
			continue; // 항목이 없으면 건너뜀

		const bool cmp = g_strcmp0(obj->folder, folder) == 0;
		g_object_unref(obj);

		if (!cmp)
			continue; // 일치하지 않으면 다음 항목으로

		// 해당 항목 선택 및 스크롤
		gtk_single_selection_set_selected(GTK_SINGLE_SELECTION(self->selection), i);
		gtk_column_view_scroll_to(
			GTK_COLUMN_VIEW(self->move_list),
			i,
			NULL,
			GTK_LIST_SCROLL_FOCUS | GTK_LIST_SCROLL_SELECT,
			NULL);
		return true;
	}
	return false;
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
	self->selected_index = gtk_single_selection_get_selected(sel);
	if (count > 0 && self->selected_index != GTK_INVALID_LIST_POSITION)
	{
		MoveObject* obj = g_list_model_get_item(G_LIST_MODEL(self->list_store), self->selected_index);
		g_strlcpy(self->filename, obj->folder, sizeof(self->filename));
		g_object_unref(obj);
		gtk_editable_set_text(GTK_EDITABLE(self->dest), self->filename);

		if (!self->refreshing)
			s_last_selected = (int)self->selected_index; // 마지막 선택 위치 저장
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
		gtk_editable_set_text(GTK_EDITABLE(self->dest), path);
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

	const char* last = gtk_editable_get_text(GTK_EDITABLE(self->dest));
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

// 새 위치 추가
static void on_add_location_clicked(GtkButton* btn, MoveDialog* self)
{
	const char* path = gtk_editable_get_text(GTK_EDITABLE(self->dest));
	if (!path || *path == '\0')
		return;

	GFile* folder = g_file_new_for_path(path);
	if (!g_file_query_exists(folder, NULL) ||
		g_file_query_file_type(folder, G_FILE_QUERY_INFO_NONE, NULL) != G_FILE_TYPE_DIRECTORY)
	{
		doumi_mesg_ok_show_async(GTK_WINDOW(self->window),
			_("The specified directory does not exist"), path, NULL, NULL);
		g_object_unref(folder);
		return;
	}
	g_object_unref(folder);

	if (ensure_folder(self, path))
	{
		// 이미 존재하고, 항목을 선택까지 했다
		return;
	}

	gchar* alias = g_path_get_basename(path);
	const bool ret = movloc_add(alias, path);
	g_free(alias);

	if (!ret)
	{
		doumi_mesg_ok_show_async(GTK_WINDOW(self->window), _("Failed to add the location"), path, NULL, NULL);
		return;
	}

	refresh_list(self);
	ensure_folder(self, path); // 새로 추가한 위치 선택

	self->modified = true; // 목록이 수정됨
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

// 항목 삭제 콜백
static void delete_callback(gpointer sender, bool result)
{
	MoveDialog* self = sender;
	if (!result || self->selected_index == GTK_INVALID_LIST_POSITION)
		return; // 취소되었거나 선택된 항목이 없음

	MoveObject* obj = g_list_model_get_item(G_LIST_MODEL(self->list_store), self->selected_index);
	if (!obj)
		return; // 항목이 없으면 그냥 나감

	movloc_delete(obj->no); // 설정에서 삭제
	refresh_list(self);

	self->modified = true; // 목록이 수정됨
}

// 키 눌림
static gboolean signal_key_pressed(
	GtkEventControllerKey* controller,
	guint keyval, guint keycode, GdkModifierType state,
	MoveDialog* self)
{
	if (keyval == GDK_KEY_Escape)
	{
		// Esc 키 입력 처리
		cancel_callback(NULL, self);
		return true; // 이벤트 중단
	}

	if (keyval == GDK_KEY_Delete)
	{
		// 항목 삭제
		if (self->selected_index == GTK_INVALID_LIST_POSITION)
			return true; // 선택된 항목이 없으면 아무것도 안함
		doumi_mesg_yesno_show_async(self->window, _("Delete selected location?"), NULL, delete_callback, self);
		return true;
	}

	if (keyval == GDK_KEY_F2)
	{
		// F2 키 입력 처리 (편집 모드 등)
		if (self->selected_index == GTK_INVALID_LIST_POSITION)
			return true;

		// 선택된 항목이 있으면 편집 모드로 전환
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

	if (self->modified)
		movloc_commit();	// 이동 목록을 설정에 커밋

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
static MoveDialog* move_dialog_new(GtkWindow* parent)
{
	MoveDialog* self = g_new0(MoveDialog, 1);

	self->window = GTK_WINDOW(gtk_window_new());
	gtk_window_set_transient_for(self->window, parent);
	gtk_window_set_title(self->window, _("Move book"));
	gtk_window_set_modal(self->window, TRUE);
#ifdef _WIN32
	gtk_window_set_default_size(self->window, 500, 650);
#else
	gtk_window_set_default_size(self->window, 500, 600);
#endif
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

	GtkWidget* browser = gtk_button_new_with_label(_("Browse"));
	g_signal_connect(browser, "clicked", G_CALLBACK(on_browse_clicked), self);
	gtk_box_append(GTK_BOX(dest_box), browser);

	self->dest = gtk_entry_new();
	gtk_widget_set_hexpand(self->dest, TRUE);
	gtk_box_append(GTK_BOX(dest_box), self->dest);

	GtkWidget* add_button = gtk_button_new_with_label(_("Add location"));
	g_signal_connect(add_button, "clicked", G_CALLBACK(on_add_location_clicked), self);
	gtk_box_append(GTK_BOX(dest_box), add_button);

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
	self->selected_index = GTK_INVALID_LIST_POSITION;
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


#if false
// 셀 편집 기능 만들던거
// 빌드 안됨

// 편집용 Entry를 동적으로 삽입
static void start_edit_alias(MoveDialog* self, guint row)
{
	GtkColumnView* view = GTK_COLUMN_VIEW(self->move_list);
	GtkListItem* item = gtk_column_view_get_row(view, row);
	if (!item) return;

	MoveObject* obj = gtk_list_item_get_item(item);
	if (!obj) return;

	GtkWidget* entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), obj->alias);
	gtk_widget_grab_focus(entry);

	// 엔터/포커스아웃 시 편집 완료
	g_signal_connect(entry, "activate", G_CALLBACK(on_edit_alias_commit), self);
	g_signal_connect(entry, "focus-out-event", G_CALLBACK(on_edit_alias_commit), self);

	gtk_list_item_set_child(item, entry);
}

// 편집 완료 시 호출
static gboolean on_edit_alias_commit(GtkWidget* entry, GdkEvent* event, gpointer user_data)
{
	MoveDialog* self = (MoveDialog*)user_data;
	guint row = self->selected_index;
	GtkColumnView* view = GTK_COLUMN_VIEW(self->move_list);
	GtkListItem* item = gtk_column_view_get_row(view, row);
	if (!item) return FALSE;

	MoveObject* obj = gtk_list_item_get_item(item);
	if (!obj) return FALSE;

	const char* new_alias = gtk_entry_get_text(GTK_ENTRY(entry));
	if (g_strcmp0(obj->alias, new_alias) != 0)
	{
		g_free(obj->alias);
		obj->alias = g_strdup(new_alias);
		self->modified = true;
		// 필요시 movloc_update(obj->no, obj->alias, obj->folder);
	}

	// Entry를 Label로 교체
	GtkWidget* label = gtk_label_new(obj->alias);
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_list_item_set_child(item, label);

	return FALSE;
}

if (keyval == GDK_KEY_F2)
{
	if (self->selected_index != GTK_INVALID_LIST_POSITION)
		start_edit_alias(self, self->selected_index);
	return TRUE;
}
#endif
