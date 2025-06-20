#include "pch.h"
#include "configs.h"
#include "book.h"

// 확장 파일 이름 바꾸기 대화상자
typedef struct RenEx
{
	GtkWindow* window;	// 상속

	GtkWidget* name;	// 만들어진 파일 이름
	GtkWidget* author;	// 작가
	GtkWidget* title;	// 책 제목
	GtkWidget* index;	// 순번
	GtkWidget* extra;	// 추가 정보

	char extension[64];
	char filename[2048];

	RenameCallback callback;
	gpointer user_data;

	bool initialized;
	bool reopen;

	GtkResponseType response;
} RenEx;

// 파일 이름 만들기
static void make_file_name(RenEx* self)
{
	if (!self->initialized)
		return;

	const char* author = gtk_editable_get_text(GTK_EDITABLE(self->author));
	const char* title = gtk_editable_get_text(GTK_EDITABLE(self->title));
	const char* index = gtk_editable_get_text(GTK_EDITABLE(self->index));
	const char* extra = gtk_editable_get_text(GTK_EDITABLE(self->extra));
	const char* ext = self->extension;

	char* psz = self->filename;
	const gulong size = sizeof(self->filename);
	gulong len = 0;
	if (author && *author)
		len += g_snprintf(psz + len, size - len, "[%s] ", author);
	if (title && *title)
		len += g_snprintf(psz + len, size - len, "%s", title);
	if (index && *index)
		len += g_snprintf(psz + len, size - len, " %s", index);
	if (extra && *extra)
		len += g_snprintf(psz + len, size - len, " (%s)", extra);
	if (*ext)
		g_snprintf(psz + len, size - len, "%s", ext);

	gtk_label_set_text(GTK_LABEL(self->name), self->filename);
}

// 파일 이름 분석
static void parse_file_name(RenEx* self, const char* filename)
{
	// 아래 걍 무시되는 경우가 있으니 미리 초기화
	gtk_label_set_text(GTK_LABEL(self->name), "");
	gtk_editable_set_text(GTK_EDITABLE(self->author), "");
	gtk_editable_set_text(GTK_EDITABLE(self->title), "");
	gtk_editable_set_text(GTK_EDITABLE(self->index), "");
	gtk_editable_set_text(GTK_EDITABLE(self->extra), "");

	if (filename == NULL || *filename == '\0')
		return;

	char ws[1024], temp[1024];
	const char* ext = strrchr(filename, '.');
	if (ext)
	{
		g_strlcpy(self->extension, ext, sizeof(self->extension));
		const size_t len = ext - filename;
		g_strlcpy(ws, filename, len + 1); // len+1: 널 포함
	}
	else
	{
		self->extension[0] = '\0';
		g_strlcpy(ws, filename, sizeof(ws));
	}

	// 작가
	const char* n = strchr(ws, '[');
	if (n)
	{
		const char* l = strchr(n, ']');
		if (l && l > n)
		{
			const size_t len = l - n - 1;
			g_strlcpy(temp, n + 1, len + 1);
			g_strstrip(temp);
			gtk_editable_set_text(GTK_EDITABLE(self->author), temp);
			memmove(ws, l + 1, strlen(l + 1) + 1);
			g_strstrip(ws);
		}
	}

	// 추가 정보
	n = strrchr(ws, '(');
	if (n)
	{
		const char* l = strrchr(n, ')');
		if (l && l > n)
		{
			const size_t len = l - n - 1;
			g_strlcpy(temp, n + 1, len + 1);
			g_strstrip(temp);
			gtk_editable_set_text(GTK_EDITABLE(self->extra), temp);
			ws[n - ws] = '\0';
			g_strstrip(ws);
		}
	}

	// 순번
	n = strrchr(ws, ' ');
	if (n)
	{
		const char* s = n + 1;
		if (g_ascii_strtoll(s, NULL, 10) != 0 || strcmp(s, "0") == 0)
		{
			gtk_editable_set_text(GTK_EDITABLE(self->index), s);
			ws[n - ws] = '\0';
			g_strstrip(ws);
		}
	}

	// 책이름
	g_strstrip(ws);
	gtk_editable_set_text(GTK_EDITABLE(self->title), ws);

	// 원래 이름
	gtk_label_set_text(GTK_LABEL(self->name), filename);
	g_strlcpy(self->filename, filename, sizeof(self->filename));

	// 초기화 했어요
	self->initialized = true;
}

// 취소 콜백
static void cancel_callback(GtkWidget* widget, RenEx* self)
{
	self->response = GTK_RESPONSE_CANCEL;
	gtk_window_close(self->window);
}

// OK 콜백
static void ok_callback(GtkWidget* widget, RenEx* self)
{
	self->response = GTK_RESPONSE_OK;
	gtk_window_close(self->window);
}

// 다시 열기 콜백
static void reopen_callback(GtkWidget* widget, RenEx* self)
{
	self->response = GTK_RESPONSE_OK;
	self->reopen = true; // 다시 열기 플래그 설정
	gtk_window_close(self->window);
}

// 엔트리 키 눌림
static gboolean entry_key_pressed(
	GtkEventControllerKey* controller,
	guint keyval,
	guint keycode,
	GdkModifierType state,
	RenEx* self)
{
	if (keyval == GDK_KEY_Escape)
	{
		// Esc 키 입력 처리
		GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
		cancel_callback(widget, self);
		return true; // 이벤트 중단
	}

	return false; // 기본 동작 계속
}

// 엔트리 값이 바뀔 때마다 호출되는 콜백
static void entry_changed(GtkEditable* editable, RenEx* self)
{
	//const char* text = gtk_editable_get_text(editable);
	make_file_name(self);
}

// 엔트리 활성화 시 호출되는 콜백
static void entry_activate(GtkEntry* entry, RenEx* self)
{
	if (entry == GTK_ENTRY(self->title))
	{
		// 제목 입력창에서 엔터키를 누르면 다음 입력창으로 포커스를 이동
		gtk_entry_grab_focus_without_selecting(GTK_ENTRY(self->author));
	}
	else if (entry == GTK_ENTRY(self->author))
	{
		// 작가 입력창에서 엔터키를 누르면 순번 입력창으로 포커스를 이동
		gtk_entry_grab_focus_without_selecting(GTK_ENTRY(self->index));
	}
	else if (entry == GTK_ENTRY(self->index))
	{
		// 순번 입력창에서 엔터키를 누르면 추가 정보 입력창으로 포커스를 이동
		gtk_entry_grab_focus_without_selecting(GTK_ENTRY(self->extra));
	}
	else if (entry == GTK_ENTRY(self->extra))
	{
		// 추가 정보 입력창에서 엔터키를 누르면 다이얼로그	를 닫고 OK 콜백 호출
		ok_callback(NULL, self);
	}
}

// 입력 상자 만들기
static GtkWidget* create_entry(RenEx* self)
{
	GtkWidget* entry = gtk_entry_new();
	gtk_widget_set_hexpand(entry, true);
	gtk_widget_set_vexpand(entry, false);
	g_signal_connect(entry, "changed", G_CALLBACK(entry_changed), self);
	g_signal_connect(entry, "activate", G_CALLBACK(entry_activate), self);

	GtkEventController* keyboard = gtk_event_controller_key_new();
	g_signal_connect(keyboard, "key-pressed", G_CALLBACK(entry_key_pressed), self);
	gtk_widget_add_controller(entry, keyboard);

	return entry;
}

// 정렬된 라벨 만들기
static GtkWidget* create_label(const char* text, GtkAlign align)
{
	GtkWidget* label = gtk_label_new(text);
	gtk_widget_set_halign(label, align);
	gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
	return label;
}

// 윈도우 종료되고 나서 콜백
static void signal_destroy(GtkWidget* widget, RenEx* self)
{
	if (self->callback)
	{
		if (self->response != GTK_RESPONSE_OK)
			self->callback(self->user_data, NULL);
		else
		{
			RenameData* data = g_new0(RenameData, 1);
			data->result = true;
			data->reopen = self->reopen;
			g_strlcpy(data->filename, self->filename, sizeof(data->filename));
			self->callback(self->user_data, data);
			g_free(data);
		}
	}
	g_free(self);
}

// 만들기
static RenEx* renex_new(GtkWindow* parent, const char* filename)
{
	RenEx* self = g_new0(RenEx, 1);

	self->window = GTK_WINDOW(gtk_window_new());
	gtk_window_set_transient_for(self->window, parent);
	gtk_window_set_title(self->window, _("Rename book"));
	gtk_window_set_resizable(self->window, false);
	gtk_window_set_modal(self->window, true);
	gtk_window_set_default_size(self->window, 670, 350);
	gtk_widget_set_size_request(GTK_WIDGET(self->window), 670, 350);
	g_signal_connect(self->window, "destroy", G_CALLBACK(signal_destroy), self);

	// 콘텐츠 영역
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	// 정보 표시 그리드
	GtkWidget* grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
	gtk_widget_set_margin_start(grid, 16);
	gtk_widget_set_margin_end(grid, 16);
	gtk_widget_set_margin_top(grid, 16);
	gtk_widget_set_margin_bottom(grid, 16);

	// 1열
	gtk_grid_attach(GTK_GRID(grid), create_label(_("Original filename"), GTK_ALIGN_END), 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), create_label(filename, GTK_ALIGN_START), 1, 1, 3, 1);
	// 2열
	gtk_grid_attach(GTK_GRID(grid), create_label(_("Rename to"), GTK_ALIGN_END), 0, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), self->name = create_label("--", GTK_ALIGN_START), 1, 2, 3, 1);

	// 3열 (빈줄)
	GtkWidget* spacer = gtk_label_new("");
	gtk_widget_set_size_request(spacer, -1, 6);
	gtk_grid_attach(GTK_GRID(grid), spacer, 0, 3, 4, 1);

	// 4열
	gtk_grid_attach(GTK_GRID(grid), create_label(_("Title"), GTK_ALIGN_END), 0, 4, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), self->title = create_entry(self), 1, 4, 3, 1);
	// 5열
	gtk_grid_attach(GTK_GRID(grid), create_label(_("Author"), GTK_ALIGN_END), 0, 5, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), self->author = create_entry(self), 1, 5, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), create_label(_("No."), GTK_ALIGN_END), 2, 5, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), self->index = create_entry(self), 3, 5, 1, 1);
	// 6열
	gtk_grid_attach(GTK_GRID(grid), create_label(_("Extra Information"), GTK_ALIGN_END), 0, 6, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), self->extra = create_entry(self), 1, 6, 3, 1);

	// 버튼 박스
	GtkWidget* button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_start(button_box, 16);
	gtk_widget_set_margin_end(button_box, 16);
	gtk_widget_set_margin_top(button_box, 24);
	gtk_widget_set_margin_bottom(button_box, 16);
	gtk_widget_set_halign(button_box, GTK_ALIGN_CENTER);

	GtkWidget* cancel_button = gtk_button_new_with_label(_("Cancel"));
	gtk_widget_set_focus_on_click(cancel_button, true);
	g_signal_connect(cancel_button, "clicked", G_CALLBACK(cancel_callback), self);

	GtkWidget* ok_button = gtk_button_new_with_label(_("OK"));
	gtk_widget_set_focus_on_click(ok_button, true);
	g_signal_connect(ok_button, "clicked", G_CALLBACK(ok_callback), self);

	GtkWidget* reopen_button = gtk_button_new_with_label(_("Reopen"));
	gtk_widget_set_focus_on_click(reopen_button, true);
	g_signal_connect(reopen_button, "clicked", G_CALLBACK(reopen_callback), self);

#ifdef _WIN32
	// 윈도우는 OK 버튼이 왼쪽에
	gtk_box_append(GTK_BOX(button_box), ok_button);
	gtk_box_append(GTK_BOX(button_box), reopen_button);
	gtk_box_append(GTK_BOX(button_box), cancel_button);
#else
	// 그 밖에는 취소 버튼이 왼쪽에
	gtk_box_append(GTK_BOX(button_box), cancel_button);
	gtk_box_append(GTK_BOX(button_box), reopen_button);
	gtk_box_append(GTK_BOX(button_box), ok_button);
#endif

	// 콘텐츠 영역 설정
	gtk_box_append(GTK_BOX(box), grid);
	gtk_box_append(GTK_BOX(box), button_box);
	gtk_window_set_child(self->window, box);

	//
	parse_file_name(self, filename);
	gtk_entry_grab_focus_without_selecting(GTK_ENTRY(self->title));

	self->response = GTK_RESPONSE_NONE;

	return self;
}

// 이름 바꾸기 대화상자 비동기로 실행해보자
void renex_show_async(GtkWindow* parent, const char* filename, RenameCallback callback, gpointer user_data)
{
	RenEx* self = renex_new(parent, filename);

	self->callback = callback;
	self->user_data = user_data;

	gtk_window_present(self->window);
}
