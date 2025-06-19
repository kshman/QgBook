#include "pch.h"
#include "configs.h"
#include "book.h"
#include "doumi.h"

// 페이지 정보 제거
static void page_entry_free(gpointer ptr)
{
	PageEntry* entry = ptr;
	if (entry->name)
		g_free(entry->name);
	g_free(entry);
}

// 책 초기화
void book_base_init(Book* book, const char* filename)
{
	book->entries = g_ptr_array_new_with_free_func(page_entry_free);

	book->full_name = g_strdup(filename);
	book->base_name = g_path_get_basename(filename);
	book->dir_name = g_path_get_dirname(filename);
}

// 책 해제
void book_base_dispose(Book* book)
{
	if (book->entries)
		g_ptr_array_free(book->entries, TRUE);
	if (book->full_name)
		g_free(book->full_name);
	if (book->base_name)
		g_free(book->base_name);
	if (book->dir_name)
		g_free(book->dir_name);
	g_free(book);
}

// 페이지 읽기
GdkTexture* book_read_page(Book* book, int page)
{
	if (page < 0 || page >= (int)book->entries->len)
		goto pos_return_no_image;

	GBytes* data = book_read_data(book, page);
	if (!data) goto pos_return_no_image;

	GError* err = NULL;
	GdkTexture* texture = gdk_texture_new_from_bytes(data, &err);
	g_bytes_unref(data);

	if (!texture)
	{
		if (err)
		{
			g_log("BOOK", G_LOG_LEVEL_WARNING, _("Failed to create page %d: %s"), page, err->message);
			g_clear_error(&err);
		}
		goto pos_return_no_image;
	}

	// 원래 여기서 캐시에 넣어야 됨

	return texture;

pos_return_no_image:
	GdkTexture* no_image = res_get_texture(RES_PIX_NO_IMAGE);
	// ref 반환이 no_image랑 같은지 확인 필요
	return GDK_TEXTURE(g_object_ref(no_image));
}

// 다음 페이지로 이동
bool book_move_next(Book* book, ViewMode mode)
{
	int prev = book->cur_page;
	switch (mode)
	{
		case VIEW_MODE_FIT:
			if (book->cur_page + 1 < book->total_page)
				book->cur_page++;
			break;
		case VIEW_MODE_LEFT_TO_RIGHT:
		case VIEW_MODE_RIGHT_TO_LEFT:
			if (book->cur_page + 2 < book->total_page)
				book->cur_page += 2;
			break;
		default:
			// 주겨버려
			abort();
	}
	return prev != book->cur_page; // 페이지가 바뀌었으면 true 반환
}

// 이전 페이지로 이동
bool book_move_prev(Book* book, ViewMode mode)
{
	int prev = book->cur_page;
	switch (mode)
	{
		case VIEW_MODE_FIT:
			book->cur_page--;
			break;
		case VIEW_MODE_LEFT_TO_RIGHT:
		case VIEW_MODE_RIGHT_TO_LEFT:
			book->cur_page -= 2;
			break;
		default:
			// 주겨버려
			abort();
	}
	if (book->cur_page < 0)
		book->cur_page = 0; // 페이지가 0보다 작아지면 0으로 고정
	return prev != book->cur_page; // 페이지가 바뀌었으면 true 반환
}

// 지정한 페이지로 이동
bool book_move_page(Book* book, int page)
{
	int prev = book->cur_page;
	book->cur_page =
		page < 0 ? 0 : page >= book->total_page ? book->total_page - 1 : page;
	return prev != book->cur_page;
}

// 엔트리 얻기
const PageEntry* book_get_entry(Book* book, int page)
{
	if (page < 0 || page >= (int)book->entries->len)
		return NULL;
	return g_ptr_array_index(book->entries, page);
}
