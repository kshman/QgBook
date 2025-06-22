#include "pch.h"
#include "configs.h"
#include "doumi.h"

/**
 * @file renex_dialog.c
 * @brief 책 파일 이름 바꾸기(확장 이름 포함) 대화상자 구현 파일입니다.
 *        파일명 분석, 입력 필드 관리, 콜백 처리 등 대화상자 동작을 담당합니다.
 */

/**
 * @brief 확장 파일 이름 바꾸기 대화상자 구조체
 *        각종 입력 위젯, 파일명 정보, 콜백, 상태 플래그를 포함합니다.
 */
typedef struct RenExDialog
{
	GtkWindow* window;	///< 대화상자 윈도우 (상속)

	GtkWidget* name;	///< 만들어진 파일 이름 표시 라벨
	GtkWidget* author;	///< 작가 입력 필드
	GtkWidget* title;	///< 책 제목 입력 필드
	GtkWidget* index;	///< 순번 입력 필드
	GtkWidget* extra;	///< 추가 정보 입력 필드

	char extension[64];	///< 파일 확장자 (.zip 등)
	char filename[2048];///< 최종 생성된 파일명

	RenameCallback callback; ///< 이름 바꾸기 완료 시 호출될 콜백
	gpointer user_data;      ///< 콜백에 전달할 사용자 데이터

	bool initialized;	///< 파일 이름이 초기화 되었는지 여부
	bool reopen;		///< 다시 열기 플래그
	bool result;		///< 결과(성공 여부)
} RenExDialog;

/**
 * @brief 입력 필드 값으로부터 파일 이름을 생성하여 name 라벨에 표시합니다.
 * @param self RenExDialog 포인터
 */
static void make_file_name(RenExDialog* self)
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

/**
 * @brief 파일 이름을 분석하여 각 입력 필드에 값을 분리하여 설정합니다.
 * @param self RenExDialog 포인터
 * @param filename 분석할 파일명
 */
static void parse_file_name(RenExDialog* self, const char* filename)
{
	// 필드 초기화
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

	// 작가 추출
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

	// 추가 정보 추출
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

	// 순번 추출
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

	// 책 제목 추출
	g_strstrip(ws);
	gtk_editable_set_text(GTK_EDITABLE(self->title), ws);

	// 원래 이름 표시
	gtk_label_set_text(GTK_LABEL(self->name), filename);
	g_strlcpy(self->filename, filename, sizeof(self->filename));

	// 초기화 완료
	self->initialized = true;
}

/**
 * @brief 취소 버튼 클릭 시 호출되는 콜백
 * @param widget 클릭된 위젯
 * @param self RenExDialog 포인터
 */
static void cancel_callback(GtkWidget* widget, RenExDialog* self)
{
	self->result = false; // 취소됨
	gtk_window_close(self->window);
}

/**
 * @brief OK 버튼 클릭 시 호출되는 콜백
 * @param widget 클릭된 위젯
 * @param self RenExDialog 포인터
 */
static void ok_callback(GtkWidget* widget, RenExDialog* self)
{
	self->result = true;
	gtk_window_close(self->window);
}

/**
 * @brief 다시 열기 버튼 클릭 시 호출되는 콜백
 * @param widget 클릭된 위젯
 * @param self RenExDialog 포인터
 */
static void reopen_callback(GtkWidget* widget, RenExDialog* self)
{
	self->result = true;
	self->reopen = true; // 다시 열기 플래그 설정
	gtk_window_close(self->window);
}

/**
 * @brief 엔트리에서 키 입력 시 호출되는 콜백 (ESC 처리)
 * @param controller 키 이벤트 컨트롤러
 * @param keyval 입력된 키 값
 * @param keycode 키 코드
 * @param state modifier 상태
 * @param self RenExDialog 포인터
 * @return true면 이벤트 중단, false면 기본 동작
 */
static gboolean entry_key_pressed(
	GtkEventControllerKey* controller,
	guint keyval,
	guint keycode,
	GdkModifierType state,
	RenExDialog* self)
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

/**
 * @brief 엔트리 값이 바뀔 때마다 호출되는 콜백
 * @param editable 변경된 GtkEditable
 * @param self RenExDialog 포인터
 */
static void entry_changed(GtkEditable* editable, RenExDialog* self)
{
	make_file_name(self);
}

/**
 * @brief 엔트리 활성화(Enter) 시 호출되는 콜백
 *        입력 순서에 따라 다음 필드로 포커스 이동 또는 OK 처리
 * @param entry 활성화된 GtkEntry
 * @param self RenExDialog 포인터
 */
static void entry_activate(GtkEntry* entry, RenExDialog* self)
{
	if (entry == GTK_ENTRY(self->title))
		gtk_entry_grab_focus_without_selecting(GTK_ENTRY(self->author));
	else if (entry == GTK_ENTRY(self->author))
		gtk_entry_grab_focus_without_selecting(GTK_ENTRY(self->index));
	else if (entry == GTK_ENTRY(self->index))
		gtk_entry_grab_focus_without_selecting(GTK_ENTRY(self->extra));
	else if (entry == GTK_ENTRY(self->extra))
		ok_callback(NULL, self);
}

/**
 * @brief 입력 상자(GtkEntry) 생성 및 이벤트 연결
 * @param self RenExDialog 포인터
 * @return 생성된 GtkWidget 포인터
 */
static GtkWidget* create_entry(RenExDialog* self)
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

/**
 * @brief 정렬된 라벨(GtkLabel) 생성
 * @param text 라벨에 표시할 텍스트
 * @param align 정렬 방식(GtkAlign)
 * @return 생성된 GtkWidget 포인터
 */
static GtkWidget* create_label(const char* text, GtkAlign align)
{
	GtkWidget* label = gtk_label_new(text);
	gtk_widget_set_halign(label, align);
	gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
	return label;
}

/**
 * @brief 윈도우 종료 시 호출되는 콜백
 *        성공적으로 이름이 바뀐 경우에만 콜백을 호출합니다.
 * @param widget 종료된 위젯
 * @param self RenExDialog 포인터
 */
static void signal_destroy(GtkWidget* widget, RenExDialog* self)
{
	if (self->callback && self->result)
	{
		self->callback(self->user_data, self->filename, self->reopen);	// 콜백 호출
		// 취소 또는 닫기에는 콜백을 호출하지 않음
	}

	g_free(self);
}

/**
 * @brief RenExDialog 객체를 생성하고 UI를 구성합니다.
 * @param parent 부모 윈도우
 * @param filename 원본 파일명
 * @return 생성된 RenExDialog 포인터
 */
static RenExDialog* renex_dialog_new(GtkWindow* parent, const char* filename)
{
	RenExDialog* self = g_new0(RenExDialog, 1);

	self->window = GTK_WINDOW(gtk_window_new());
	gtk_window_set_transient_for(self->window, parent);
	gtk_window_set_title(self->window, _("Rename book"));
	gtk_window_set_resizable(self->window, false);
	gtk_window_set_modal(self->window, true);
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

	// 1열: 원본 파일명
	gtk_grid_attach(GTK_GRID(grid), create_label(_("Original filename"), GTK_ALIGN_END), 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), create_label(filename, GTK_ALIGN_START), 1, 1, 3, 1);
	// 2열: 변경될 파일명
	gtk_grid_attach(GTK_GRID(grid), create_label(_("Rename to"), GTK_ALIGN_END), 0, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), self->name = create_label("--", GTK_ALIGN_START), 1, 2, 3, 1);

	// 3열: 빈줄(스페이서)
	GtkWidget* spacer = gtk_label_new("");
	gtk_widget_set_size_request(spacer, -1, 6);
	gtk_grid_attach(GTK_GRID(grid), spacer, 0, 3, 4, 1);

	// 4열: 제목
	gtk_grid_attach(GTK_GRID(grid), create_label(_("Title"), GTK_ALIGN_END), 0, 4, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), self->title = create_entry(self), 1, 4, 3, 1);
	// 5열: 작가, 순번
	gtk_grid_attach(GTK_GRID(grid), create_label(_("Author"), GTK_ALIGN_END), 0, 5, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), self->author = create_entry(self), 1, 5, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), create_label(_("No."), GTK_ALIGN_END), 2, 5, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), self->index = create_entry(self), 3, 5, 1, 1);
	// 6열: 추가 정보
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

	// 파일명 분석 및 포커스 설정
	parse_file_name(self, filename);
	gtk_entry_grab_focus_without_selecting(GTK_ENTRY(self->title));

	return self;
}

/**
 * @brief 이름 바꾸기 대화상자를 비동기로 실행합니다.
 *        완료 시 콜백이 호출됩니다.
 * @param parent 부모 윈도우
 * @param filename 원본 파일명
 * @param callback 이름 바꾸기 완료 콜백
 * @param user_data 콜백에 전달할 사용자 데이터
 */
void renex_dialog_show_async(GtkWindow* parent, const char* filename, RenameCallback callback, gpointer user_data)
{
	RenExDialog* self = renex_dialog_new(parent, filename);

	self->callback = callback;
	self->user_data = user_data;

	gtk_window_present(self->window);
}

/**
 * @note
 * - RenExDialog는 파일명 분석, 입력 필드 관리, 콜백 처리 등 이름 바꾸기 대화상자의 모든 동작을 담당합니다.
 * - 입력 필드의 값이 바뀔 때마다 파일명이 자동으로 갱신됩니다.
 * - 콜백은 성공적으로 이름이 바뀐 경우에만 호출됩니다.
 */
