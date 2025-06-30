#include "pch.h"
#include "configs.h"
#include "book.h"
#include "doumi.h"
#include "bound.h"

#define NOTIFY_TIMEOUT 2000

// 앞서 선언
typedef struct ReadWindow ReadWindow;
typedef struct PageDialog PageDialog;
typedef void (*ShortcutFunc)(ReadWindow*);

// 임시 싱글턴... 이지만 아마 임시가 아닐 것이다
static ReadWindow* s_read_window = NULL;

// 외부 함수
extern void renex_dialog_show_async(GtkWindow* parent, const char* filename, RenameCallback callback,
	gpointer user_data);
extern void move_dialog_show_async(GtkWindow* parent, MoveCallback callback, gpointer user_data);

extern PageDialog* page_dialog_new(GtkWindow* parent, PageSelectCallback callback, gpointer user_data);
extern void page_dialog_dispose(PageDialog* self);
extern void page_dialog_show_async(PageDialog* self, int page);
extern void page_dialog_set_book(PageDialog* self, Book* book);
extern void page_dialog_reset_book(PageDialog* self);

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

static GtkWidget* read_draw_new(ReadWindow* self)
{
	GtkWidget* widget = GTK_WIDGET(g_object_new(read_draw_get_type(), NULL));
	((ReadDraw*)widget)->read_window = self;
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
#ifdef _WIN32
	HWND hwnd;
#endif

	GtkWidget* title_label;
	GtkWidget* info_label;

	GtkWidget* menu_file_close;
	GtkWidget* menu_zoom_check;
	GtkWidget* menu_vmode_image;
	GtkWidget* menu_vmode_radios[VIEW_MODE_MAX_VALUE];
	GtkWidget* menu_vquality_radios[VIEW_QUALITY_MAX_VALUE];
	GtkWidget* menu_valign_radios[HORIZ_ALIGN_MAX_VALUE];
	GtkWidget* menu_vmargin_spin;

	GtkWidget* draw;

	GHashTable* shortcuts;
	guint key_val;
	GdkModifierType key_state;

	// 알림
	guint32 notify_id;
	char notify_text[260];
	PangoFontDescription* notify_font;

	// 책 상태
	HorizAlign view_align;
	int view_pages;

	PageDialog* page_dialog;

	// 책
	Book* book;
	PageData* pages[2]; // 일단 왼쪽/오른쪽 두장
	GdkTexture* keep_texture[2]; // 페이지를 유지하기 위한 텍스쳐

	// 캐시
	PageData** cache_pages; // 페이지 캐시
	GQueue* cache_queue;
	size_t cache_size;
};

// 앞서 선언
static void queue_draw_book(ReadWindow* self);
static void prepare_pages(ReadWindow* self);
static void page_control(ReadWindow* self, BookControl c);

#pragma region 알림 메시지
// 알림 메시지 타이머 콜백
static gboolean cb_notify_timeout(gpointer data)
{
	ReadWindow* self = data;
	self->notify_text[0] = '\0'; // 알림 메시지 초기화
	self->notify_id = 0;
	gtk_widget_queue_draw(self->draw);
	return false; // 타이머 제거
}

// 알림 메시지
static void notify(ReadWindow* self, int timeout, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	g_vsnprintf(self->notify_text, sizeof(self->notify_text), fmt, ap); // NOLINT(clang-diagnostic-format-nonliteral)
	va_end(ap);

	if (self->notify_id != 0)
	{
		g_source_remove(self->notify_id);
		self->notify_id = 0;
	}

	if (self->notify_text[0] != '\0')
		self->notify_id = g_timeout_add(timeout > 0 ? timeout : NOTIFY_TIMEOUT, cb_notify_timeout, self);
	gtk_widget_queue_draw(self->draw);
}
#pragma endregion

#pragma region 윈도우 기능
// 포커스 리셋
static void reset_focus(ReadWindow* self)
{
	// 그리기 위젯에 포커스 주기
	gtk_widget_grab_focus(self->window);
	gtk_widget_grab_focus(self->draw);
}

// 키보드 리셋
// 다이얼로그처럼 키보드 입력이 전환되는 애들은 호출하는게 좋다
static void reset_key(ReadWindow* self)
{
	self->key_val = 0;
	self->key_state = 0;
}

// 풀스크린!
static void toggle_fullscreen(ReadWindow* self)
{
	if (gtk_window_is_fullscreen(GTK_WINDOW(self->window)))
		gtk_window_unfullscreen(GTK_WINDOW(self->window));
	else
		gtk_window_fullscreen(GTK_WINDOW(self->window));
}

// 늘려보기 설정과 메뉴 처리
static void update_view_zoom(ReadWindow* self, bool zoom)
{
	if (config_get_bool(CONFIG_VIEW_ZOOM, true) == zoom)
		return;

	gtk_check_button_set_active(GTK_CHECK_BUTTON(self->menu_zoom_check), zoom);
	config_set_bool(CONFIG_VIEW_ZOOM, zoom, false);

	//queue_draw_book(self);
	// 책을 다시처리할 필요가 없다
	gtk_widget_queue_draw(self->draw);
}

// 읽기 방향 설정과 메뉴 처리
static void update_view_mode(ReadWindow* self, ViewMode mode)
{
	if (config_get_int(CONFIG_VIEW_MODE, true) == mode)
		return;

	GdkPaintable* paintable = GDK_PAINTABLE(res_get_view_mode_texture(mode));
	gtk_image_set_from_paintable(GTK_IMAGE(self->menu_vmode_image), paintable);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(self->menu_vmode_radios[mode]), true);
	config_set_int(CONFIG_VIEW_MODE, mode, false);

	// 책 준비
	queue_draw_book(self);
}

// 보기 품질 설정과 메뉴 처리
static void update_view_quality(ReadWindow* self, ViewQuality quality)
{
	if (config_get_int(CONFIG_VIEW_QUALITY, true) == quality)
		return;

	gtk_check_button_set_active(GTK_CHECK_BUTTON(self->menu_vquality_radios[quality]), true);
	config_set_int(CONFIG_VIEW_QUALITY, quality, false);

	queue_draw_book(self);
}

// 보기 정렬 설정과 메뉴 처리
// 실행 중에만 적용되며 설정에 저장하지 않는다
static void update_view_align(ReadWindow* self, HorizAlign align)
{
	if (self->view_align == align)
		return;

	gtk_check_button_set_active(GTK_CHECK_BUTTON(self->menu_valign_radios[align]), true);
	self->view_align = align;

	queue_draw_book(self);
}

// 보기 여백 설정과 메뉴 처리
static void update_view_margin(ReadWindow* self, int margin)
{
	if (config_get_int(CONFIG_VIEW_MARGIN, true) == margin)
		return;

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->menu_vmargin_spin), margin);
	config_set_int(CONFIG_VIEW_MARGIN, margin, false);

	queue_draw_book(self);
}

// 현재 책 정보를 헤더바에 표시
static void update_book_info(ReadWindow* self)
{
	if (self->book == NULL)
		return;

	char info[256], size[64];
	doumi_format_size_friendly(self->cache_size, size, sizeof(size));
	g_snprintf(info, sizeof(info), "%d/%d [%s]", self->book->cur_page + 1, self->book->total_page, size);
	gtk_label_set_text(GTK_LABEL(self->info_label), info);
	gtk_label_set_text(GTK_LABEL(self->title_label), self->book->base_name);
}

// 책 그리라고 시키기
static void queue_draw_book(ReadWindow* self)
{
	if (self->book)
	{
		prepare_pages(self);
		update_book_info(self);
		// 쪽 정보를 그려야 한다면 여기서 하면 된다
	}

	gtk_widget_queue_draw(self->draw);
}
#pragma endregion

#pragma region 책 처리
// 쪽 정리
static void clear_page(ReadWindow* self)
{
	for (int i = 0; i < 2; i++)
	{
		if (self->keep_texture[i])
		{
			g_object_unref(self->keep_texture[i]);
			self->keep_texture[i] = NULL;
		}
	}

	if (self->pages[0])
	{
		if (self->pages[0]->texture)
			self->keep_texture[0] = g_object_ref(self->pages[0]->texture);

		if (self->pages[0]->anim_timer)
		{
			g_source_remove(self->pages[0]->anim_timer);
			self->pages[0]->anim_timer = 0;
		}
		self->pages[0] = NULL;
	}

	if (self->pages[1])
	{
		if (self->pages[1]->texture)
			self->keep_texture[1] = g_object_ref(self->pages[1]->texture);

		if (self->pages[1]->anim_timer)
		{
			g_source_remove(self->pages[1]->anim_timer);
			self->pages[1]->anim_timer = 0;
		}
		self->pages[1] = NULL;
	}
}

// 진짜 책 정리
// 원래 close_book에 있던건데 종료할때 GTK 오류 메시지가 속출하여 따로 뺌
static void finalize_book(ReadWindow* self)
{
	clear_page(self);

	for (int i = 0; i < 2; i++)
	{
		if (self->keep_texture[i])
		{
			g_object_unref(self->keep_texture[i]);
			self->keep_texture[i] = NULL;
		}
	}

	if (self->book != NULL)
	{
		if (self->cache_pages)
		{
			for (int i = 0; i < self->book->total_page; i++)
			{
				PageData* data = self->cache_pages[i];
				if (data)
					page_data_free(data);
			}
			g_free((gpointer)self->cache_pages);
		}

		g_queue_free(self->cache_queue);
		self->cache_size = 0;

		const int page = self->book->cur_page - 1 >= self->book->total_page ? 0 : self->book->cur_page;
		recently_set_page(self->book->base_name, page);

		book_dispose(self->book);
		self->book = NULL;
	}
}

// 책 정리
static void close_book(ReadWindow* self)
{
	finalize_book(self);

	gtk_label_set_text(GTK_LABEL(self->info_label), "----");
	gtk_label_set_text(GTK_LABEL(self->title_label), _("[No Book]"));
	gtk_widget_set_sensitive(self->menu_file_close, false);

	if (self->page_dialog)
		page_dialog_reset_book(self->page_dialog);

	queue_draw_book(self);
}

// 책 열기
// page가 0이상이면 해당 페이지로 열기
// file은 호출한 쪽에서 처분할 것
static void open_book(ReadWindow* self, GFile* file)
{
	const GFileType type = doumi_get_file_type_from(file);
	if (type == G_FILE_TYPE_UNKNOWN || type == G_FILE_TYPE_DIRECTORY)
	{
		// 파일이 없거나
		// 디렉토리면... 어떻게 하나
		return;
	}

	gchar* path = g_file_get_path(file);
	Book* book = NULL;

	if (doumi_is_archive_zip(path))
	{
		book = book_zip_new(path);
		if (book == NULL)
			notify(self, 0, _("Unsupported archive file"));
	}
	else
	{
		// 이미지 파일이거나
		// 디렉토리거나
		// 하면 좋겠는데 나중에

		// 일단 오류 뿜뿜
		notify(self, 0, _("Failed to open book"));
	}

	g_free(path);

	if (book == NULL)
		return; // 오류 메시지는 오류 난데서 표시하고 여기서는 그냥 나감

	close_book(self); // 이 안에서 queue_draw가 호출되므로 아래쪽에서 안해도 된다

	config_set_string(CONFIG_FILE_LAST_FILE, book->full_name, false);
	config_set_string(CONFIG_FILE_LAST_DIRECTORY, book->dir_name, false);

	self->book = book;
	book->cur_page = recently_get_page(book->base_name);

	self->cache_pages = g_new0(PageData*, book->total_page);
	self->cache_queue = g_queue_new();
	self->cache_size = 0;

	update_book_info(self);
	gtk_widget_set_sensitive(self->menu_file_close, true);

	prepare_pages(self);

	if (self->page_dialog)
		page_dialog_set_book(self->page_dialog, book);
}

// 책 열기 대화상자 콜백
static void cb_open_book_dialog(GObject* source_object, GAsyncResult* res, gpointer user_data)
{
	ReadWindow* self = user_data;
	GtkFileDialog* dialog = GTK_FILE_DIALOG(source_object);
	GFile* file = gtk_file_dialog_open_finish(dialog, res, NULL);
	if (file)
	{
		open_book(self, file);
		g_object_unref(file);
	}
	g_object_unref(dialog);
}

// 책 열기 대화상자
// 이 함수는 비동기로 동작하므로, 이 함수를 호출한 뒤에 뭐 하면 안된다
static void open_book_dialog(ReadWindow* self)
{
	reset_key(self);

	GtkFileFilter* fall = doumi_file_filter_all();
	GtkFileFilter* fzip = doumi_file_filter_zip();
	GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
	g_list_store_append(filters, fzip);
	g_list_store_append(filters, fall);

	GtkFileDialog* dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, _("Book open"));
	gtk_file_dialog_set_modal(dialog, true);
	gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));

	char last[2048];
	if (config_get_string(CONFIG_FILE_LAST_DIRECTORY, last, sizeof(last), false))
	{
		GFile* initial_folder = g_file_new_for_path(last);
		if (g_file_query_exists(initial_folder, NULL))
			gtk_file_dialog_set_initial_folder(dialog, initial_folder);
		g_object_unref(initial_folder);
	}

	gtk_file_dialog_open(dialog, GTK_WINDOW(self->window), NULL, cb_open_book_dialog, self);

	g_object_unref(filters);
	g_object_unref(fall);
	g_object_unref(fzip);
}

// 애니메이션 콜백
static gboolean cb_page_anim_timeout(gpointer data)
{
	ReadWindow* self = s_read_window;
	PageData* page = data;

	if (!self || !page)
	{
		g_log("BOOK", G_LOG_LEVEL_WARNING, "Invalid data in animation timeout callback");
		return false;
	}

	if (!page->info.has_anim || !page->anim_iter)
		return false; // 애니메이션이 없으면 그냥 나감

	const bool visible = page == self->pages[0] || page == self->pages[1];
	if (!visible)
	{
		page->anim_timer = 0;
		return false;
	}

	if (page->texture)
		g_object_unref(page->texture);

	GdkPixbuf* pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(page->anim_iter);
	page->texture = gdk_texture_new_for_pixbuf(pixbuf);

	gtk_widget_queue_draw(self->draw); // 다시 그리라고 요청

	gdk_pixbuf_animation_iter_advance(page->anim_iter, NULL);

	return true; // 타이머 계속 유지
}

// 비동기 애니메이션 로딩 완료 콜백
static void cb_animation_load_finish(GObject* source_object, GAsyncResult* res, gpointer user_data)
{
	ReadWindow* self = s_read_window;
	PageData* data = user_data;

	if (!self || !data)
	{
		g_log("BOOK", G_LOG_LEVEL_WARNING, "Invalid data in animation load callback");
		return;
	}

	bool data_valid = false;
	if (data->entry && data->entry->page >= 0 && data->entry->page < self->book->total_page)
	{
		const PageData* cached_data = self->cache_pages[data->entry->page];
		if (cached_data == data)
			data_valid = true;
	}

	if (!data_valid)
	{
		g_log("BOOK", G_LOG_LEVEL_DEBUG, "PageData no longer valid, skipping animation load");
		return;
	}

	GError* error = NULL;
	data->animation = gdk_pixbuf_animation_new_from_stream_finish(res, &error);
	data->async_loading = false;

	if (data->anim_timer)
	{
		g_source_remove(data->anim_timer);
		data->anim_timer = 0;
	}

	if (error)
	{
		g_log("BOOK", G_LOG_LEVEL_WARNING, _("Failed to load animation: %s"), error->message);
		g_clear_error(&error);
		data->texture = g_object_ref(res_get_texture(RES_PIX_NO_IMAGE));
	}
	else if (data->animation)
	{
		// 애니메이션 로딩 성공
		data->anim_iter = gdk_pixbuf_animation_get_iter(data->animation, NULL);
		int delay = gdk_pixbuf_animation_iter_get_delay_time(data->anim_iter);
		if (delay <= 0) delay = 100;

		data->anim_timer = g_timeout_add(delay, cb_page_anim_timeout, data);

		// 첫 프레임 텍스처 설정
		GdkPixbuf* pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(data->anim_iter);
		data->texture = gdk_texture_new_for_pixbuf(pixbuf);
	}
	else
	{
		// 어쨋든 애니메이션은 실패
		data->texture = g_object_ref(res_get_texture(RES_PIX_NO_IMAGE));
	}

	data->loaded = true;

	// 버퍼 정리
	if (data->buffer)
	{
		g_bytes_unref(data->buffer);
		data->buffer = NULL;
	}

	// 화면 업데이트
	const bool visible = data == self->pages[0] || data == self->pages[1];
	if (!visible)
		gtk_widget_queue_draw(self->draw);
}

// 쪽 읽기
static void read_page(ReadWindow* self, PageData* data)
{
	if (data == NULL)
		return; // 페이지가 없으면 그냥 나감

	if (data->loaded)
	{
		// 애니메이션이 있으면 재생
		if (data->info.has_anim && data->animation && !data->anim_timer)
		{
			if (!data->anim_iter)
				data->anim_iter = gdk_pixbuf_animation_get_iter(data->animation, NULL);
			int delay = gdk_pixbuf_animation_iter_get_delay_time(data->anim_iter);
			if (delay <= 0) delay = 100; // 애니메이션 딜레이가 0이면 100ms로 설정
			data->anim_timer = g_timeout_add(delay, cb_page_anim_timeout, data);
		}
		return; // 이미 읽은 페이지면 그냥 나감
	}

	if (data->async_loading)
	{
		// 이미 비동기 로딩 중이면 그냥 나감
		return;
	}

	if (data->anim_timer)
	{
		g_source_remove(data->anim_timer);
		data->anim_timer = 0;
	}

	// 비동기 로딩을 위해 기존 텍스처를 보존하지 않고 즉시 해제
	// (애니메이션의 경우 새로운 텍스처로 교체되어야 함)
	if (data->texture)
	{
		g_object_unref(data->texture);
		data->texture = NULL;
	}

	if (data->buffer == NULL)
	{
		// 페이지 버퍼가 없으면, 즉 파일 오류이거나 처리할 수 없는 그림 형식이면
		data->texture = g_object_ref(res_get_texture(RES_PIX_NO_IMAGE));
	}
	else if (data->info.has_anim)
	{
		data->async_loading = true; // 비동기 로딩 시작

		// 비동기 애니메이션 로딩 시작
		GInputStream* stream = g_memory_input_stream_new_from_bytes(data->buffer);
		gdk_pixbuf_animation_new_from_stream_async(
			stream, NULL, cb_animation_load_finish, data);
		g_object_unref(stream);

		// 즉시 화면 업데이트 (로딩 표시)
		gtk_widget_queue_draw(self->draw);
	}
	else
	{
		GError* error = NULL;
		data->texture = gdk_texture_new_from_bytes(data->buffer, &error);

		g_bytes_unref(data->buffer);
		data->buffer = NULL;

		if (error)
		{
			g_log("BOOK", G_LOG_LEVEL_ERROR, _("Failed to create page %d: %s"),
				data->entry->page + 1, error->message);
			g_clear_error(&error);
		}

		if (!data->texture)
		{
			// 텍스쳐를 못만들었다. 노 이미지로
			data->texture = g_object_ref(res_get_texture(RES_PIX_NO_IMAGE));
		}
	}

	data->loaded = true; // 페이지는 읽은 것으로 표시
}

// 쪽 준비 (여기서 캐시 처리)
static PageData* try_page_read_or_cache_data(ReadWindow* self, const int page)
{
	PageData* data = self->cache_pages[page];

	if (data != NULL)
		return data; // 캐시에 있으면 그냥 반환

	data = book_prepare_page(self->book, page);

	size_t dest_size = self->cache_size + data->info.size;
	const size_t actual_size = config_get_actual_max_page_cache();
	if (dest_size > actual_size)
	{
		// 캐시가 너무 커지면 오래된 페이지를 제거
		while (dest_size > actual_size && !g_queue_is_empty(self->cache_queue))
		{
			const int index = GPOINTER_TO_INT(g_queue_pop_head(self->cache_queue));
			PageData* item = self->cache_pages[index];
			if (item != NULL)
			{
				dest_size -= item->info.size;

				if (item->anim_timer)
				{
					g_source_remove(item->anim_timer);
					item->anim_timer = 0;
				}

				// 비동기 로딩이 진행 중인 경우 플래그 해제
				item->async_loading = false;

				page_data_free(item); // 페이지 데이터 해제
				self->cache_pages[index] = NULL; // 캐시에서 제거
			}
		}

		self->cache_size = dest_size; // 현재 캐시 크기 갱신
	}

	// 혹시나 페이지가 너무 커서 캐시가 넘쳤더라도 지금 만든건 못지운다
	self->cache_pages[page] = data; // 캐시에 넣음
	g_queue_push_tail(self->cache_queue, GINT_TO_POINTER(page)); // 캐시 큐에 넣음
	self->cache_size = dest_size;

	return data;
}

// 쪽 준비
static void prepare_pages(ReadWindow* self)
{
	if (self->book == NULL)
		return; // 책이 없으면 그냥 나감

	clear_page(self);

	const int cur = self->book->cur_page;

	const ViewMode mode = (ViewMode)config_get_int(CONFIG_VIEW_MODE, true);
	switch (mode) // NOLINT(clang-diagnostic-switch-enum)
	{
		case VIEW_MODE_FIT:
			self->pages[0] = try_page_read_or_cache_data(self, cur);
			read_page(self, self->pages[0]);
			self->view_pages = 1;
			break;

		case VIEW_MODE_LEFT_TO_RIGHT:
		case VIEW_MODE_RIGHT_TO_LEFT:
		{
			PageData* l = self->pages[0] = try_page_read_or_cache_data(self, cur);
			read_page(self, l);

			if (l->info.has_anim || l->info.width > l->info.height ||
				self->cache_size >= config_get_actual_max_page_cache())
			{
				// 애니메이션이 있거나 폭이 넓으면 1쪽만
				// 그리고 캐시가 넘쳐도 1쪽만
				self->view_pages = 1;
			}
			else
			{
				const int next = cur + 1;
				if (next < self->book->total_page)
				{
					PageData* r = try_page_read_or_cache_data(self, next);
					if (r->info.has_anim || r->info.width > r->info.height)
					{
						// 다른쪽이 애니메이션이거나 폭이 넓으면 1쪽만
						self->view_pages = 1;
					}
					else
					{
						read_page(self, r);
						self->pages[1] = r; // 오른쪽 페이지도 읽음
						self->view_pages = 2;
					}
				}
			}
			break;
		}

		default:
			g_assert_not_reached(); // 잘못된 모드
	}
}

// 쪽 조정
static void page_control(ReadWindow* self, BookControl c)
{
	if (self->book == NULL)
		return;

	switch (c) // NOLINT(clang-diagnostic-switch-enum)
	{
		case BOOK_CTRL_PREV:
			if (!book_move_prev(self->book, self->view_pages))
				return;
			break;

		case BOOK_CTRL_NEXT:
			if (!book_move_next(self->book, self->view_pages))
				return;
			break;

		case BOOK_CTRL_FIRST:
			book_move_page(self->book, 0);
			break;

		case BOOK_CTRL_LAST:
			book_move_page(self->book, INT_MAX);
			break;

		case BOOK_CTRL_10_PREV:
			book_move_page(self->book, self->book->cur_page - 10);
			break;

		case BOOK_CTRL_10_NEXT:
			book_move_page(self->book, self->book->cur_page + 10);
			break;

		case BOOK_CTRL_MINUS:
			book_move_page(self->book, self->book->cur_page - 1);
			break;

		case BOOK_CTRL_PLUS:
			book_move_page(self->book, self->book->cur_page + 1);
			break;

		case BOOK_CTRL_SCAN_PREV:
		{
			nears_build(self->book->dir_name, self->book->func.ext_compare);
			const char* prev = nears_get_prev(self->book->full_name);
			if (prev == NULL)
				notify(self, 0, _("No previous book found"));
			else
			{
				GFile* file = g_file_new_for_path(prev);
				open_book(self, file);
				g_object_unref(file);
			}
			break;
		}

		case BOOK_CTRL_SCAN_NEXT:
		{
			nears_build(self->book->dir_name, self->book->func.ext_compare);
			const char* next = nears_get_next(self->book->full_name);
			if (next == NULL)
				notify(self, 0, _("No next book found"));
			else
			{
				GFile* file = g_file_new_for_path(next);
				open_book(self, file);
				g_object_unref(file);
			}
			break;
		}

		case BOOK_CTRL_SCAN_RANDOM:
		{
			nears_build(self->book->dir_name, self->book->func.ext_compare);
			const char* random = nears_get_random(self->book->full_name);
			if (random == NULL)
				notify(self, 0, _("No random book found"));
			else
			{
				GFile* file = g_file_new_for_path(random);
				open_book(self, file);
				g_object_unref(file);
			}
			break;
		}

		case BOOK_CTRL_SELECT:
			page_dialog_show_async(self->page_dialog, self->book->cur_page);
			break;

		default:
			g_assert_not_reached(); // 잘못된 컨트롤
	}

	queue_draw_book(self);
}

// 쪽 선택 콜백
void cb_page_dialog(gpointer sender, int page)
{
	if (page < 0)
		return; // 페이지가 잘못됐거나 취소됨

	ReadWindow* self = sender;
	if (book_move_page(self->book, page))
	{
		// 페이지 이동 성공
		prepare_pages(self);
		queue_draw_book(self);
	}
}
#pragma endregion


#pragma region 이벤트 콜백
// 윈도우 종료되고 나서 콜백
static void signal_destroy(GtkWidget* widget, ReadWindow* self)
{
	finalize_book(self);

	// 페이지 다이얼로그 해제

	if (self->notify_font)
		pango_font_description_free(self->notify_font);

	// 여기서 해제하면 된다구
	if (self->shortcuts)
		g_hash_table_destroy(self->shortcuts);

	g_free(self);
}

// 윈도우 닫히기 전에 콜백
static gboolean signal_close_request(GtkWidget* widget, ReadWindow* self)
{
#ifdef _WIN32
	// 좌표 저장
	GtkWindow* window = GTK_WINDOW(widget);
	if (self->hwnd && !gtk_window_is_maximized(window) && !gtk_window_is_fullscreen(window))
	{
		RECT rc;
		if (GetWindowRect(self->hwnd, &rc))
		{
			config_set_int(CONFIG_WINDOW_X, rc.left, false);
			config_set_int(CONFIG_WINDOW_Y, rc.top, false);
		}
	}
#endif
	if (self->page_dialog)
		page_dialog_dispose(self->page_dialog);
	return false;
}

// 맵 콜백
static void signal_map(GtkWidget* widget, ReadWindow* self)
{
#ifdef _WIN32
	GdkSurface* surface = gtk_native_get_surface(GTK_NATIVE(widget));
	if (GDK_IS_WIN32_SURFACE(surface))
	{
		self->hwnd = gdk_win32_surface_get_handle(surface);
		const int x = config_get_int(CONFIG_WINDOW_X, true);
		const int y = config_get_int(CONFIG_WINDOW_Y, true);
		if (self->hwnd && x >= 0 && y >= 0)
			SetWindowPos(self->hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
#endif
	self->page_dialog = page_dialog_new(GTK_WINDOW(self->window), cb_page_dialog, self);
}

// 윈도우 각종 알림 콜백
static void signal_notify(GObject* object, GParamSpec* pspec, ReadWindow* self)
{
	const char* name = g_param_spec_get_name(pspec);
	if (g_strcmp0(name, "default-width") == 0 || g_strcmp0(name, "default-height") == 0)
	{
		if (!gtk_window_is_maximized(GTK_WINDOW(self->window)) && !gtk_window_is_fullscreen(GTK_WINDOW(self->window)))
		{
			// 윈도우 기본 크기 변경
			int width, height;
			gtk_window_get_default_size(GTK_WINDOW(self->window), &width, &height);
			config_set_int(CONFIG_WINDOW_WIDTH, width, true);
			config_set_int(CONFIG_WINDOW_HEIGHT, height, true);
		}
	}
}

// 파일 끌어 놓기
static gboolean signal_file_drop(GtkDropTarget* target, const GValue* value, double x, double y, ReadWindow* self)
{
	// 참고로 G_TYPE_STRING로 다중 파일을 "text/uri-list"로 처리할 수 있으나 안함
	if (!G_VALUE_HOLDS(value, G_TYPE_FILE))
		return false;

	GFile* file = g_value_get_object(value);
	if (file)
	{
		open_book(self, file);
		// !!! 절대로 g_value_get_object로 얻은 값은 해제하면 안된다 !!!
		//g_object_unref(file);
	}

	return true;
}

// 마우스 휠
static gboolean signal_wheel_scroll(GtkEventControllerScroll* controller, double dx, double dy, ReadWindow* self)
{
	if (dy > 0)
	{
		page_control(self, BOOK_CTRL_NEXT);
		return true;
	}
	if (dy < 0)
	{
		page_control(self, BOOK_CTRL_PREV);
		return true;
	}
	return false; // 이벤트를 소모하지 않음
}

// 마우스 버튼
static void signal_mouse_click(GtkGestureClick* gesture, int n_press, double x, double y, ReadWindow* self)
{
	const guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

	if (n_press == 2 && button == GDK_BUTTON_PRIMARY)
	{
		// 두번 클릭
		if (self->book == NULL)
		{
			// 책 열기
		}
	}
	else if (n_press == 3 && button == GDK_BUTTON_SECONDARY)
	{
		// 세번 클릭, GTK4는 창 끌어 이동이 안되므로, 그냥 옵션 무시하고 3번 눌러 전체 화면 전환
		if (self->book != NULL)
			toggle_fullscreen(self);
	}
	else if (n_press == 1)
	{
		// 한번 클릭
		if (button == GDK_BUTTON_PRIMARY)
		{
			if (!config_get_bool(CONFIG_MOUSE_DOUBLE_CLICK_FULLSCREEN, true) &&
				!gtk_window_is_maximized(GTK_WINDOW(self->window)) &&
				!gtk_window_is_fullscreen(GTK_WINDOW(self->window)))
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
			toggle_fullscreen(self);
		}
	}
}

// 키보드 떼임
static gboolean signal_key_released(GtkEventControllerKey* controller,
	guint value,
	guint code,
	GdkModifierType state,
	ReadWindow* self)
{
	reset_key(self);
	return false;
}

// 키보드 눌림
static gboolean signal_key_pressed(GtkEventControllerKey* controller,
	guint value,
	guint code,
	GdkModifierType state,
	ReadWindow* self)
{
	// 보조키는 쉬프트/컨트롤/알트/슈퍼/하이퍼/메타만 남기고 나머지는 제거
	state &= GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK | GDK_META_MASK;
	// 대문자는 소문자로
	value = gdk_keyval_to_lower(value);

	if (self->key_val == value && self->key_state == state)
		return false;

	self->key_val = value;
	self->key_state = state;

	const char* action = shortcut_lookup(value, state);
	if (action != NULL)
	{
		const ShortcutFunc func = (ShortcutFunc)g_hash_table_lookup(self->shortcuts, action);
		if (func != NULL)
		{
			func(self);
			return true; // 이벤트 소모
		}
	}

	return false;
}

// 파일 열기 누르기
static void menu_file_open_clicked(GtkButton* button, ReadWindow* self)
{
	open_book_dialog(self);
}

// 파일 닫기 누르기
static void menu_file_close_clicked(GtkButton* button, ReadWindow* self)
{
	close_book(self);
}

// 설정 누르기
static void menu_settings_click(GtkButton* button, ReadWindow* self)
{
	// 설정은 언제 만드냐...
}

// 끝내기 누르기
static void menu_exit_click(GtkButton* button, ReadWindow* self)
{
	gtk_window_close(GTK_WINDOW(self->window));
}

// 늘려 보기 토글
static void menu_view_zoom_toggled(GtkCheckButton* button, ReadWindow* self)
{
	const gboolean b = gtk_check_button_get_active(GTK_CHECK_BUTTON(self->menu_zoom_check));
	update_view_zoom(self, b);
}

// 보기 모드 누르기
static void menu_view_mode_clicked(GtkButton* button, ReadWindow* self)
{
	const ViewMode cur = (ViewMode)config_get_int(CONFIG_VIEW_MODE, true);
	const ViewMode mode =
		cur == VIEW_MODE_FIT
		? VIEW_MODE_LEFT_TO_RIGHT
		: cur == VIEW_MODE_LEFT_TO_RIGHT
		? VIEW_MODE_RIGHT_TO_LEFT
		: /*cur == VIEW_MODE_RIGHT_TO_LEFT ? VIEW_MODE_FIT :*/ VIEW_MODE_FIT;
	update_view_mode(self, mode);
}

// 보기 방향 선택 바뀜
static void menu_view_mode_toggled(GtkCheckButton* button, ReadWindow* self)
{
	if (!gtk_check_button_get_active(button))
		return;
	ViewMode mode = (ViewMode)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "tag"));
	update_view_mode(self, mode);
}

// 보기 품질 선택 바뀜
static void menu_view_quality_toggled(GtkCheckButton* button, ReadWindow* self)
{
	if (!gtk_check_button_get_active(button))
		return;
	ViewQuality quality = (ViewQuality)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "tag"));
	update_view_quality(self, quality);
}

// 보기 정렬 선택 바뀜
static void menu_view_align_toggled(GtkCheckButton* button, ReadWindow* self)
{
	if (!gtk_check_button_get_active(button))
		return;
	HorizAlign align = (HorizAlign)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "tag"));
	update_view_align(self, align);
}

// 보기 여백 선택 바뀜
static void menu_view_margin_changed(GtkSpinButton* spin, ReadWindow* self)
{
	int margin = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
	update_view_margin(self, margin);
}

// 단축키 - 아무것도 안함
static void shortcut_none(ReadWindow* self)
{
	// 아무것도 안할거다
}

// 단축키 - 테스트
static void shortcut_test(ReadWindow* self)
{
	static int s_index = 0;
	notify(self, 5000, _("TEST phase #%d"), ++s_index);

	reset_key(self);
}

// 단축키 - 끝내기
static void shortcut_exit(ReadWindow* self)
{
	gtk_window_close(GTK_WINDOW(self->window));
}

// 단축키 - 끝내기인데 Escape
static void shortcut_escape(ReadWindow* self)
{
	if (config_get_bool(CONFIG_GENERAL_ESC_EXIT, true))
		gtk_window_close(GTK_WINDOW(self->window));
}

// 단축키 - 파일 열기
static void shortcut_file_open(ReadWindow* self)
{
	open_book_dialog(self);
}

// 단축키 - 파일 닫기
static void shortcut_file_close(ReadWindow* self)
{
	close_book(self);
}

// 단축키 - 설정
static void shortcut_settings(ReadWindow* self)
{
	// 언젠가는 해야하겠지...
}

// 단축키 - 풀스크린
static void shortcut_fullscreen(ReadWindow* self)
{
	toggle_fullscreen(self);
}

// 단축기 - 마지막 책 열기
// 현재 단축키 밖에 없다.
static void shortcut_open_last_book(ReadWindow* self)
{
	const char* last = config_get_string_ptr(CONFIG_FILE_LAST_FILE, false);
	if (last && last[0] != '\0')
	{
		if (self->book != NULL && g_strcmp0(self->book->full_name, last) == 0)
			return; // 이미 열려있는 책이면 그냥 리턴

		GFile* file = g_file_new_for_path(last);
		if (file != NULL && g_file_query_exists(file, NULL))
		{
			open_book(self, file);
			g_object_unref(file);
			return;
		}

		if (file != NULL)
			g_object_unref(file);
	}
	notify(self, 0, _("No last book found"));
}

// 단축키 - 기억한 책 읽기
// 현재 단축키 밖에 없다.
static void shortcut_open_remember(ReadWindow* self)
{
	if (self->book != NULL)
	{
		// 이미 책이 열려있으면 그냥 리턴
		notify(self, 0, _("Close current book for open the remembered"));
		return;
	}

	const char* remember = config_get_string_ptr(CONFIG_FILE_REMEMBER, false);
	if (remember && remember[0] != '\0')
	{
		GFile* file = g_file_new_for_path(remember);
		if (file != NULL && g_file_query_exists(file, NULL))
		{
			open_book(self, file);
			g_object_unref(file);
			return;
		}
		if (file != NULL)
			g_object_unref(file);
	}
	notify(self, 0, _("Cannot open remembered book"));
}

// 단축키 - 기억한 책 저장
// 현재 단축키 밖에 없다.
static void shortcut_save_remember(ReadWindow* self)
{
	if (self->book == NULL)
	{
		notify(self, 0, _("No book to remember"));
		return;
	}
	config_set_string(CONFIG_FILE_REMEMBER, self->book->full_name, false);
	notify(self, 0, _("Remembered current book"));
}

// 책 지우기 콜백, 실제 지우는 일을 한다
static void cb_delete_book_done(gpointer sender, bool ret)
{
	ReadWindow* self = sender;
	if (!ret)
		return; // 취소

	// 책 지우기 전에 근처 파일을 만들어 놔야 한다
	nears_build(self->book->dir_name, self->book->func.ext_compare);

	// 실제 지움
	if (!book_delete(self->book))
	{
		notify(self, 0, _("Failed to delete book"));
		return;
	}

	// 책이 지워졌으므로 페이지는 0으로 초기화.
	// 어짜피 close_book에서 저장하므로 페이지만 0으로 하면 된다
	self->book->cur_page = 0;

	// 다음 책으로 넘어가보자
	const char* next = nears_get_for_remove(self->book->full_name);
	if (next == NULL)
	{
		// 다음 책이 없으면 그냥 닫기
		notify(self, 0, _("No next book found"));
		close_book(self);
	}
	else
	{
		GFile* file = g_file_new_for_path(next);
		open_book(self, file);
		g_object_unref(file);
	}
}

// 단축키 - 책 지우기
// 현재 단축키 밖에 없다.
static void shortcut_delete_book(ReadWindow* self)
{
	if (self->book == NULL)
		return;
	if (!book_can_delete(self->book))
	{
		notify(self, 0, _("This book cannot be deleted"));
		return;
	}

	if (config_get_bool(CONFIG_GENERAL_CONFIRM_DELETE, true))
	{
		reset_key(self); // 다이얼로그가 키 뗌을 막아버리므로 키 입력 초기화
		doumi_mesg_yesno_show_async(
			GTK_WINDOW(self->window),
			_("Delete current book?"), self->book->base_name,
			cb_delete_book_done, self);
	}
	else
	{
		// 묻고 따지지도 않고 바로 콜백
		cb_delete_book_done(self, true);
	}
}

// 책 이름 바꾸기 콜백
// 다이얼로그에서 취소하면 호출되지 않는다
static void cb_rename_book_done(gpointer sender, const char* filename, bool reopen)
{
	ReadWindow* self = sender;

	if (!filename || *filename == '\0')
		return;

	if (g_strcmp0(filename, self->book->base_name) == 0)
		return;

	// 이름 바꾸기 전에 근처 파일을 만들어 놔야 한다
	nears_build(self->book->dir_name, self->book->func.ext_compare);

	char* new_filename = book_rename(self->book, filename);
	if (new_filename == NULL)
	{
		notify(self, 0, _("Failed to rename book"));
		return;
	}

	self->book->cur_page = 0; // 페이지는 초기화

	const char* next = nears_get_for_rename(self->book->full_name, new_filename);
	g_free(new_filename);

	GFile* file = g_file_new_for_path(next);
	open_book(self, file);
	g_object_unref(file);
}

// 단축키 - 책 이름 바꾸기
// 현재 단축키 밖에 없다.
static void shortcut_rename_book(ReadWindow* self)
{
	if (self->book == NULL)
		return;

	reset_key(self); // 다이얼로그가 키를 먹어버리므로 키 입력 초기화

	renex_dialog_show_async(GTK_WINDOW(self->window), self->book->base_name, cb_rename_book_done, self);
}

// 책 옮기기 콜백
// 다이얼로그에서 취소하면 호출되지 않는다
void cb_move_book_done(gpointer sender, const char* directory)
{
	ReadWindow* self = sender;

	if (self->book == NULL)
	{
		// 책이 없으면 안내만 표시하고 나감
		notify(self, 0, _("No book to move"));
		return;
	}

	if (!directory || *directory == '\0')
		return;

	if (g_strcmp0(directory, self->book->dir_name) == 0)
	{
		notify(self, 0, _("Book is already in the selected directory"));
		return;
	}

	gchar* fullname = g_build_filename(directory, self->book->base_name, NULL);
	// 이름 바꾸기 전에 근처 파일을 만들어 놔야 한다
	nears_build(self->book->dir_name, self->book->func.ext_compare);

	if (!book_move(self->book, fullname))
	{
		notify(self, 0, _("Failed to rename book"));
		g_free(fullname);
		return;
	}

	self->book->cur_page = 0; // 페이지는 초기화

	const char* next = nears_get_for_remove(self->book->full_name);
	g_free(fullname);

	GFile* file = g_file_new_for_path(next);
	open_book(self, file);
	g_object_unref(file);
}

// 단축키 - 책 옮기기
// 현재 단축키 밖에 없다.
static void shortcut_move_book(ReadWindow* self)
{
	// 책이 없어도 편집이 가능하도록 책 검사는 안한다
	reset_key(self); // 다이얼로그가 키를 먹어버리므로 키 입력 초기화

	move_dialog_show_async(GTK_WINDOW(self->window), cb_move_book_done, self);
}

// 단축키 - 앞 장으로
static void shortcut_page_prev(ReadWindow* self)
{
	page_control(self, BOOK_CTRL_PREV);
}

// 단축키 - 뒷 장으로
static void shortcut_page_next(ReadWindow* self)
{
	page_control(self, BOOK_CTRL_NEXT);
}

// 단축키 - 첫 쪽으로
static void shortcut_page_first(ReadWindow* self)
{
	page_control(self, BOOK_CTRL_FIRST);
}

// 단축키 - 마지막 쪽으로
static void shortcut_page_last(ReadWindow* self)
{
	page_control(self, BOOK_CTRL_LAST);
}

// 단축키 - 10 쪽 뒤로
static void shortcut_page_10_prev(ReadWindow* self)
{
	page_control(self, BOOK_CTRL_10_PREV);
}

// 단축키 - 10 쪽 앞으로
static void shortcut_page_10_next(ReadWindow* self)
{
	page_control(self, BOOK_CTRL_10_NEXT);
}

// 단축키 - 쪽 -1
static void shortcut_page_minus(ReadWindow* self)
{
	page_control(self, BOOK_CTRL_MINUS);
}

// 단축키 - 쪽 +1
static void shortcut_page_plus(ReadWindow* self)
{
	page_control(self, BOOK_CTRL_PLUS);
}

// 단축키 - 쪽 선택
static void shortcut_page_select(ReadWindow* self)
{
	reset_key(self); // 다이얼로그가 키를 먹어버리므로 키 입력 초기화
	page_control(self, BOOK_CTRL_SELECT);
}

// 단축키 - 이전 책으로
static void shortcut_scan_book_prev(ReadWindow* self)
{
	page_control(self, BOOK_CTRL_SCAN_PREV);
}

// 단축키 - 다음 책으로
static void shortcut_scan_book_next(ReadWindow* self)
{
	page_control(self, BOOK_CTRL_SCAN_NEXT);
}

// 단축키 - 임의의 책으로
static void shortcut_scan_book_random(ReadWindow* self)
{
	page_control(self, BOOK_CTRL_SCAN_RANDOM);
}

// 단축키 - 크게 보기
static void shortcut_view_zoom_toggle(ReadWindow* self)
{
	const bool v = !config_get_bool(CONFIG_VIEW_ZOOM, true);
	update_view_zoom(self, v);
}

// 단축키 - 읽기 방향 왼쪽/오른쪽 전환
static void shortcut_view_mode_left_right(ReadWindow* self)
{
	const ViewMode cur = (ViewMode)config_get_int(CONFIG_VIEW_MODE, true);
	const ViewMode mode =
		cur == VIEW_MODE_FIT
		? VIEW_MODE_LEFT_TO_RIGHT
		: cur == VIEW_MODE_LEFT_TO_RIGHT
		? VIEW_MODE_RIGHT_TO_LEFT
		: VIEW_MODE_LEFT_TO_RIGHT;
	update_view_mode(self, mode);
}

// 단축키 - 보기 정렬 가운데
static void shortcut_view_align_center(ReadWindow* self)
{
	if (self->view_align == HORIZ_ALIGN_CENTER)
		return; // 이미 가운데 정렬이면 안함
	update_view_align(self, HORIZ_ALIGN_CENTER);
}

// 단축키 - 보기 정렬 왼쪽
static void shortcut_view_align_left(ReadWindow* self)
{
	if (self->view_align == HORIZ_ALIGN_LEFT)
		return; // 이미 왼쪽 정렬이면 안함
	update_view_align(self, HORIZ_ALIGN_LEFT);
}

// 단축키 - 보기 정렬 오른쪽
static void shortcut_view_align_right(ReadWindow* self)
{
	if (self->view_align == HORIZ_ALIGN_RIGHT)
		return; // 이미 오른쪽 정렬이면 안함
	update_view_align(self, HORIZ_ALIGN_RIGHT);
}

// 단축키 - 보기 정렬 순서대로 바꾸기
static void shortcut_view_align_toggle(ReadWindow* self)
{
	const HorizAlign align =
		self->view_align == HORIZ_ALIGN_CENTER
		? HORIZ_ALIGN_RIGHT
		: self->view_align == HORIZ_ALIGN_RIGHT
		? HORIZ_ALIGN_LEFT
		: HORIZ_ALIGN_CENTER;
	update_view_align(self, align);
}
#pragma endregion

#pragma region 스냅샷 그리기
// 페이지 텍스쳐 1장 그리기
static void paint_texture_fit(ReadWindow* self, GtkSnapshot* snapshot, int sw, int sh, GdkTexture* texture, int tw, int th)
{
	const bool zoom = config_get_bool(CONFIG_VIEW_ZOOM, true);
	const BoundSize ns = bound_size_calc_dest(zoom, sw, sh, tw, th);
	BoundRect rt = bound_rect_calc_rect(HORIZ_ALIGN_CENTER, sw, sh, ns.width, ns.height);

	// 마진 적용
	if (self->view_align == HORIZ_ALIGN_LEFT)
	{
		const int w = bound_rect_width(&rt);
		rt.left = config_get_int(CONFIG_VIEW_MARGIN, true);
		rt.right = rt.left + w;
	}
	else if (self->view_align == HORIZ_ALIGN_RIGHT)
	{
		const int w = bound_rect_width(&rt);
		rt.right = sw - config_get_int(CONFIG_VIEW_MARGIN, true);
		rt.left = rt.right - w;
	}

	// 이미지 그리기
	gtk_snapshot_append_texture(snapshot, texture, &BOUND_RECT_TO_GRAPHENE_RECT(&rt));
}

// 페이지 텍스쳐 2장 그리기 + 마진 지원
static void paint_texture_dual(
	ReadWindow* self, GtkSnapshot* snapshot, int sw, int sh,
	GdkTexture* left, int ltw, int lth,
	GdkTexture* right, int rtw, int rth)
{
	const int half = sw / 2;
	const bool zoom = config_get_bool(CONFIG_VIEW_ZOOM, true);

	// 왼쪽 페이지
	const BoundSize ls = bound_size_calc_dest(zoom, half, sh, ltw, lth);
	BoundRect lb = bound_rect_calc_rect(HORIZ_ALIGN_RIGHT, half, sh, ls.width, ls.height);

	// 오른쪽 페이지
	const BoundSize rs = bound_size_calc_dest(zoom, half, sh, rtw, rth);
	const BoundRect ro = bound_rect_calc_rect(HORIZ_ALIGN_LEFT, half, sh, rs.width, rs.height);
	BoundRect rb = bound_rect_delta(&ro, half, 0);

	// 가로 정렬
	if (self->view_align != HORIZ_ALIGN_CENTER)
	{
		const int lw = bound_rect_width(&lb);
		const int rw = bound_rect_width(&rb);
		const int margin = config_get_int(CONFIG_VIEW_MARGIN, true);
		if (self->view_align == HORIZ_ALIGN_LEFT)
		{
			// 왼쪽 페이지를 margin만큼 왼쪽에 붙임
			lb.left = margin;
			lb.right = lb.left + lw;
			// 오른쪽 페이지는 왼쪽 페이지 오른쪽에 바로 붙임
			rb.left = lb.right;
			rb.right = rb.left + rw;
		}
		else if (self->view_align == HORIZ_ALIGN_RIGHT)
		{
			// 오른쪽 페이지를 오른쪽 끝에서 margin만큼 떨어뜨림
			rb.left = sw - margin - rw;
			rb.right = rb.left + rw;
			// 왼쪽 페이지는 오른쪽 페이지 왼쪽에 바로 붙임
			lb.left = rb.left - lw;
			lb.right = lb.left + lw;
		}
	}

	// 그리기
	gtk_snapshot_append_texture(snapshot, left, &BOUND_RECT_TO_GRAPHENE_RECT(&lb));
	gtk_snapshot_append_texture(snapshot, right, &BOUND_RECT_TO_GRAPHENE_RECT(&rb));
}

// 비동기 메시지 및 보관 텍스쳐 그리기
static void paint_async_load_info(ReadWindow* self, GtkSnapshot* snapshot, int width, int height)
{
	GdkTexture* l = self->keep_texture[0];
	GdkTexture* r = self->keep_texture[1];

	if (l != NULL && r != NULL)
	{
		const ViewMode mode = (ViewMode)config_get_int(CONFIG_VIEW_MODE, true);
		if (mode == VIEW_MODE_RIGHT_TO_LEFT)
		{
			GdkTexture* tmp = l;
			l = r;
			r = tmp;
		}
		paint_texture_dual(
			self, snapshot, width, height,
			l, gdk_texture_get_width(l), gdk_texture_get_height(l),
			r, gdk_texture_get_width(r), gdk_texture_get_height(r));
	}
	else
	{
		GdkTexture* t = l ? l : r;
		if (t != NULL)
			paint_texture_fit(
				self, snapshot, width, height, t,
				gdk_texture_get_width(t), gdk_texture_get_height(t));
	}

	const GdkRGBA overlay_color = { 0.0f, 0.0f, 0.0f, 0.5f }; // 30% 반투명 검은색
	gtk_snapshot_append_color(snapshot, &overlay_color, &GRAPHENE_RECT_INIT(0, 0, (float)width, (float)height));

	// 메시지
	char msg[64];
	g_snprintf(msg, sizeof(msg), _("Loading page %d..."), self->book->cur_page + 1);
	PangoLayout* layout = gtk_widget_create_pango_layout(GTK_WIDGET(self->draw), msg);
	pango_layout_set_font_description(layout, self->notify_font);
	int text_width, text_height;
	pango_layout_get_size(layout, &text_width, &text_height);
	text_width /= PANGO_SCALE;
	text_height /= PANGO_SCALE;

	const int padding = 18;
	const int tex_w = text_width + padding * 2;
	const int tex_h = text_height + padding * 2;

	cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tex_w, tex_h);
	cairo_t* cr = cairo_create(surface);
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, padding, padding);
	pango_cairo_show_layout(cr, layout);
	cairo_destroy(cr);
	g_object_unref(layout);

	GdkTexture* texture = doumi_texture_from_surface(surface);
	cairo_surface_destroy(surface);

	if (texture)
	{
		const float x = (float)(width - tex_w) / 2.0f;
		const float y = (float)(height - tex_h) / 2.0f;
		gtk_snapshot_append_texture(snapshot, texture, &GRAPHENE_RECT_INIT(x, y, (float)tex_w, (float)tex_h));
		g_object_unref(texture);
	}
}

// 텍스쳐를 화면에 맞게 그리기
static void paint_page_fit(ReadWindow* self, GtkSnapshot* snapshot, const PageData* page, int width, int height)
{
	paint_texture_fit(self, snapshot, width, height, page->texture, page->info.width, page->info.height);
}

// 텍스쳐 두장을 나란히 화면 중앙에 붙여서 그리기
static void paint_page_dual(
	ReadWindow* self, GtkSnapshot* snapshot,
	const PageData* left, const PageData* right,
	int width, int height)
{
	paint_texture_dual(
		self, snapshot, width, height,
		left->texture, left->info.width, left->info.height,
		right->texture, right->info.width, right->info.height);
}

// 책 그리기
static void paint_book(ReadWindow* self, GtkSnapshot* snapshot, int width, int height)
{
	if (self->book == NULL)
		return;

	if (self->view_pages == 1)
	{
		// 한장만 그리기
		const PageData* data = self->pages[0] ? self->pages[0] : self->pages[1];
		if (data != NULL)
		{
			if (data->async_loading)
				paint_async_load_info(self, snapshot, width, height);
			else
				paint_page_fit(self, snapshot, data, width, height);
		}
	}
	else if (self->view_pages == 2)
	{
		ViewMode mode = (ViewMode)config_get_int(CONFIG_VIEW_MODE, true);
		if (mode != VIEW_MODE_LEFT_TO_RIGHT && mode != VIEW_MODE_RIGHT_TO_LEFT)
		{
			// 뭐라고? 두장 그려야 하는데 모드가 왼쪽에서 오른쪽도 아니고 오른쪽에서 왼쪽도 아니라고?
			//g_assert_not_reached(); // 잘못된 페이지 개수
			// assert 하지말고 그냥 기본값인 왼쪽에서 오른쪽으로 그리자
			mode = VIEW_MODE_LEFT_TO_RIGHT;
		}

		// 두장 그리기
		const PageData* l;
		const PageData* r;
		if (mode == VIEW_MODE_LEFT_TO_RIGHT)
		{
			l = self->pages[0];
			r = self->pages[1];
		}
		else
		{
			l = self->pages[1];
			r = self->pages[0];
		}

		if (l && r)
		{
			// 양쪽 페이지가 모두 있으면
			paint_page_dual(self, snapshot, l, r, width, height);
		}
		else if (l)
		{
			// 왼쪽 페이지만 있으면
			paint_page_fit(self, snapshot, l, width, height);
		}
		else if (r)
		{
			// 오른쪽 페이지만 있으면
			paint_page_fit(self, snapshot, r, width, height);
		}
		else
		{
			// 헐?
			// 페이지가 없으면 no_image를 그린다
			// ...였는데 귀찮다 그리지 말자
		}
	}
	else
	{
		// 뭐라고? 페이지개수가 1도 아니고 2도 아니라고?
		g_assert_not_reached(); // 잘못된 페이지 개수
	}
}

// 알림 메시지 그리기
static void paint_notify(ReadWindow* self, GtkSnapshot* s, int width, int height)
{
	if (self->notify_text[0] == '\0')
		return;

	// 글꼴과 그릴 위치의 크기 계산
	PangoLayout* layout = gtk_widget_create_pango_layout(GTK_WIDGET(self->draw), self->notify_text);
	pango_layout_set_font_description(layout, self->notify_font);
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
		const float x = (float)(width - tex_w) / 2.0f;
		const float y = (float)(height - tex_h) / 2.0f;
		gtk_snapshot_append_texture(s, texture, &GRAPHENE_RECT_INIT(x, y, (float)tex_w, (float)tex_h));
		g_object_unref(texture);
	}
}

// 스냅샷 재정의로 책 윈도우를 그리자고
static void read_draw_snapshot(GtkWidget* widget, GtkSnapshot* snapshot)
{
	ReadDraw* draw = (ReadDraw*)widget;
	ReadWindow* self = draw->read_window;
	const int width = gtk_widget_get_width(widget);
	const int height = gtk_widget_get_height(widget);

	// 배경
	const GdkRGBA red = { 0.1f, 0.1f, 0.1f, 1.0f };
	gtk_snapshot_append_color(snapshot, &red, &GRAPHENE_RECT_INIT(0, 0, (float)width, (float)height));

	// 카이로로 그릴거
	cairo_t* cr = gtk_snapshot_append_cairo(snapshot, &GRAPHENE_RECT_INIT(0, 0, (float)width, (float)height));
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
		const float x = (float)width - lw - 50.0f;
		const float y = (float)width > lh ? (float)height - lh - 50.0f : 10.0f;
		gtk_snapshot_append_texture(snapshot, logo, &GRAPHENE_RECT_INIT(x, y, lw, lh));
	}

	// 책 그리기
	paint_book(self, snapshot, width, height);

	// 알림 메시지 그리기
	paint_notify(self, snapshot, width, height);
}
#pragma endregion

#pragma region 책 읽기 클래스
// 만들기
ReadWindow* read_window_new(GtkApplication* app)
{
	ReadWindow* self = g_new0(ReadWindow, 1);
	s_read_window = self; // 전역 변수에 저장

	// 첨에 UI 설정할 때 중복 호출 방지
	bool view_zoom = config_get_bool(CONFIG_VIEW_ZOOM, true);
	ViewMode view_mode = (ViewMode)config_get_int(CONFIG_VIEW_MODE, true);
	ViewQuality view_quality = (ViewQuality)config_get_int(CONFIG_VIEW_QUALITY, true);
	int view_margin = config_get_int(CONFIG_VIEW_MARGIN, true);

	const int width = config_get_int(CONFIG_WINDOW_WIDTH, true);
	const int height = config_get_int(CONFIG_WINDOW_HEIGHT, true);

#pragma region 윈도우 디자인
	// 윈도우
	self->window = gtk_application_window_new(app);

	gtk_window_set_title(GTK_WINDOW(self->window), _("QgBook"));
	gtk_window_set_default_size(GTK_WINDOW(self->window), width, height);
	gtk_widget_set_size_request(GTK_WIDGET(self->window), 600, 400);
	g_signal_connect(self->window, "destroy", G_CALLBACK(signal_destroy), self);
	g_signal_connect(self->window, "notify", G_CALLBACK(signal_notify), self);
	g_signal_connect(self->window, "close-request", G_CALLBACK(signal_close_request), self);
	g_signal_connect(self->window, "map", G_CALLBACK(signal_map), self);

	// 팡고 글꼴
	self->notify_font = pango_font_description_from_string(
		"Malgun Gothic, Apple SD Gothic Neo, Noto Sans CJK KR, Sans 20");

	// 메인 메뉴, 헤더바에서 쓸거라 앞쪽에서 만들어 두자
	GtkWidget* main_popover = gtk_popover_new();
	GtkWidget* menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_margin_start(menu_box, 8);
	gtk_widget_set_margin_end(menu_box, 8);

	// 메뉴 - 파일 열기
	GtkWidget* menu_file_open = gtk_button_new_with_label(_("Book open"));
	g_signal_connect(menu_file_open, "clicked", G_CALLBACK(menu_file_open_clicked), self);
	gtk_box_append(GTK_BOX(menu_box), menu_file_open);

	// 메뉴 - 파일 닫기
	self->menu_file_close = gtk_button_new_with_label(_("Close book"));
	gtk_widget_set_sensitive(self->menu_file_close, false); // 첨엔 파일이 없으니깐 비활성화
	g_signal_connect(self->menu_file_close, "clicked", G_CALLBACK(menu_file_close_clicked), self);
	gtk_box_append(GTK_BOX(menu_box), self->menu_file_close);

	gtk_box_append(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

	// 메뉴 - 설정
	GtkWidget* menu_setting = gtk_button_new_with_label(_("Settings"));
	g_signal_connect(menu_setting, "clicked", G_CALLBACK(menu_settings_click), self);
	gtk_box_append(GTK_BOX(menu_box), menu_setting);

	gtk_box_append(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

	// 메뉴 - 끝내기
	GtkWidget* menu_exit = gtk_button_new_with_label(_("Exit"));
	g_signal_connect(menu_exit, "clicked", G_CALLBACK(menu_exit_click), self);
	gtk_box_append(GTK_BOX(menu_box), menu_exit);

	// 팝오버에 메인 메뉴 박스 넣기
	gtk_popover_set_child(GTK_POPOVER(main_popover), menu_box);

	// 보기 메뉴, 이 것도 앞에서 만들자고
	GtkWidget* view_popover = gtk_popover_new();
	menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	// 보기 메뉴 - 늘려 보기
	self->menu_zoom_check = gtk_check_button_new_with_label(_("Zoom"));
	gtk_check_button_set_active(GTK_CHECK_BUTTON(self->menu_zoom_check), view_zoom);
	g_signal_connect(self->menu_zoom_check, "toggled", G_CALLBACK(menu_view_zoom_toggled), self);
	gtk_box_append(GTK_BOX(menu_box), self->menu_zoom_check);

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
		g_signal_connect(r, "toggled", G_CALLBACK(menu_view_mode_toggled), self);
		if (i > 0)
			gtk_check_button_set_group(GTK_CHECK_BUTTON(r), GTK_CHECK_BUTTON(self->menu_vmode_radios[0]));
		gtk_box_append(GTK_BOX(menu_box), self->menu_vmode_radios[i] = r);
	}
	gtk_check_button_set_active(GTK_CHECK_BUTTON(self->menu_vmode_radios[view_mode]), true);

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
		g_signal_connect(r, "toggled", G_CALLBACK(menu_view_quality_toggled), self);
		if (i > 0)
			gtk_check_button_set_group(GTK_CHECK_BUTTON(r), GTK_CHECK_BUTTON(self->menu_vquality_radios[0]));
		gtk_box_append(GTK_BOX(menu_box), self->menu_vquality_radios[i] = r);
	}
	gtk_check_button_set_active(GTK_CHECK_BUTTON(self->menu_vquality_radios[view_quality]), true);

	gtk_box_append(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

	// 보기 메뉴 - 정렬
	GtkWidget* valign_label = gtk_label_new(_("Horizontal Alignment"));
	gtk_widget_set_halign(valign_label, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(menu_box), valign_label);

	const char* valign_strings[HORIZ_ALIGN_MAX_VALUE] =
	{
		_("Center"),
		_("Left"),
		_("Right"),
	};
	for (int i = 0; i < HORIZ_ALIGN_MAX_VALUE; i++)
	{
		GtkWidget* r = gtk_check_button_new_with_label(valign_strings[i]);
		g_object_set_data(G_OBJECT(r), "tag", GINT_TO_POINTER(i));
		g_signal_connect(r, "toggled", G_CALLBACK(menu_view_align_toggled), self);
		if (i > 0)
			gtk_check_button_set_group(GTK_CHECK_BUTTON(r), GTK_CHECK_BUTTON(self->menu_valign_radios[0]));
		gtk_box_append(GTK_BOX(menu_box), self->menu_valign_radios[i] = r);
	}
	gtk_check_button_set_active(GTK_CHECK_BUTTON(self->menu_valign_radios[self->view_align]), true);

	// 보기 메뉴 - 마진
	GtkWidget* margin_label = gtk_label_new(_("Alignment margin"));
	gtk_widget_set_halign(margin_label, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(menu_box), margin_label);

	int max_margin = 500, monitor_width, monitor_height;
	if (doumi_get_primary_monitor_dimension(&monitor_width, &monitor_height))
	{
		// 모니터 크기에서 최대 마진을 계산
		max_margin = monitor_width / 3; // 모니터 너비의 1/3까지 허용
		max_margin = (max_margin / 50) * 50; // 50 단위로 맞추기
	}

	if (view_margin > max_margin)
		view_margin = max_margin; // 설정값이 너무 크면 최대값으로 제한

	self->menu_vmargin_spin = gtk_spin_button_new_with_range(0, max_margin, 50);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->menu_vmargin_spin), view_margin);
	g_signal_connect(self->menu_vmargin_spin, "value-changed", G_CALLBACK(menu_view_margin_changed), self);
	gtk_box_append(GTK_BOX(menu_box), self->menu_vmargin_spin);

	// 팝오버에 보기 메뉴 박스 넣기
	gtk_popover_set_child(GTK_POPOVER(view_popover), menu_box);

	// 헤더바
	GtkWidget* header = gtk_header_bar_new();

	// 헤더바 - 보기 모드 버튼 + 이미지
	self->menu_vmode_image = gtk_image_new_from_paintable(GDK_PAINTABLE(res_get_view_mode_texture(view_mode)));
	gtk_image_set_pixel_size(GTK_IMAGE(self->menu_vmode_image), 24);

	GtkWidget* menu_mode_button = gtk_button_new();
	gtk_widget_set_can_focus(menu_mode_button, false);
	gtk_button_set_child(GTK_BUTTON(menu_mode_button), self->menu_vmode_image);
	g_signal_connect(menu_mode_button, "clicked", G_CALLBACK(menu_view_mode_clicked), self);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header), menu_mode_button);

	// 헤더바 - 제목
	self->title_label = gtk_label_new(_("[No Book]"));
	gtk_widget_set_halign(self->title_label, GTK_ALIGN_START);
	gtk_widget_set_valign(self->title_label, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_start(self->title_label, 12);
	gtk_widget_set_sensitive(self->title_label, false); // 책이 열려있지 않으니 비활성화
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header), self->title_label);

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
	self->info_label = gtk_label_new("----");
	gtk_widget_set_halign(self->info_label, GTK_ALIGN_END);
	gtk_widget_set_valign(self->info_label, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_start(self->info_label, 12);
	gtk_widget_set_margin_end(self->info_label, 12);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(header), self->info_label);

	// 헤더바 설정
	gtk_window_set_titlebar(GTK_WINDOW(self->window), header);

	// DrawingArea
	self->draw = read_draw_new(self);
	gtk_widget_set_can_focus(self->draw, true);
	gtk_widget_set_hexpand(self->draw, true);
	gtk_widget_set_vexpand(self->draw, true);

	// 메인 레이아웃
	GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_can_focus(main_box, false);
	gtk_box_append(GTK_BOX(main_box), self->draw);
	gtk_window_set_child(GTK_WINDOW(self->window), main_box);
#pragma endregion

#pragma region 컨트롤러
	// 파일 끌어다 놓기
	GtkDropTarget* drop = gtk_drop_target_new(G_TYPE_FILE, GDK_ACTION_COPY);
	g_signal_connect(drop, "drop", G_CALLBACK(signal_file_drop), self);
	gtk_widget_add_controller(self->draw, GTK_EVENT_CONTROLLER(drop));

	// 마우스 휠
	GtkEventController* wheel = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
	g_signal_connect(wheel, "scroll", G_CALLBACK(signal_wheel_scroll), self);
	gtk_widget_add_controller(self->draw, wheel);

	// 마우스 버튼
	GtkGesture* gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
	g_signal_connect(gesture, "pressed", G_CALLBACK(signal_mouse_click), self);
	gtk_widget_add_controller(self->draw, GTK_EVENT_CONTROLLER(gesture));

	// 키보드
	GtkEventController* keyboard = gtk_event_controller_key_new();
	g_signal_connect(keyboard, "key-pressed", G_CALLBACK(signal_key_pressed), self);
	g_signal_connect(keyboard, "key-released", G_CALLBACK(signal_key_released), self);
	gtk_widget_add_controller(self->window, keyboard);

	// 단축키 컨트롤러...인데 이건 내 커스텀
	static const struct ShortcutAction
	{
		char* name;
		ShortcutFunc func;
	} actions[] =
	{
		{"none", shortcut_none},
		{"test", shortcut_test},
		{"exit", shortcut_exit},
		{"escape", shortcut_escape},
		{"file_open", shortcut_file_open},
		{"file_close", shortcut_file_close},
		{"settings", shortcut_settings},
		{"fullscreen", shortcut_fullscreen},
		{"open_last_book", shortcut_open_last_book},
		{"open_remember", shortcut_open_remember},
		{"save_remember", shortcut_save_remember},
		{"delete_book", shortcut_delete_book},
		{"rename_book", shortcut_rename_book},
		{"move_book", shortcut_move_book},
		{"page_prev", shortcut_page_prev},
		{"page_next", shortcut_page_next},
		{"page_first", shortcut_page_first},
		{"page_last", shortcut_page_last},
		{"page_10_prev", shortcut_page_10_prev},
		{"page_10_next", shortcut_page_10_next},
		{"page_minus", shortcut_page_minus},
		{"page_plus", shortcut_page_plus},
		{"page_select", shortcut_page_select},
		{"scan_book_prev", shortcut_scan_book_prev},
		{"scan_book_next", shortcut_scan_book_next},
		{"scan_book_random", shortcut_scan_book_random},
		{"view_zoom_toggle", shortcut_view_zoom_toggle},
		{"view_mode_left_right", shortcut_view_mode_left_right},
		{"view_align_center", shortcut_view_align_center},
		{"view_align_left", shortcut_view_align_left},
		{"view_align_right", shortcut_view_align_right},
		{"view_align_toggle", shortcut_view_align_toggle},
		// 여기까지
		{NULL, NULL}
	};

	self->shortcuts = g_hash_table_new(g_str_hash, g_str_equal);
	for (const struct ShortcutAction* act = actions; act->name; ++act)
		g_hash_table_insert(self->shortcuts, act->name, (gpointer)act->func);

	shortcut_register();
#pragma endregion

	// 초기화를 끝내면서
	reset_focus(self);

	return self;
}

// 창 보이기
void read_window_show(const ReadWindow* self)
{
	gtk_widget_set_visible(self->window, true);
}
#pragma endregion
