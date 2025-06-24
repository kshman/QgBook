#include "pch.h"
#include "configs.h"
#include "book.h"
#include "doumi.h"

/**
 * @file book.c
 * @brief Book(책) 객체의 기본 동작(초기화, 해제, 페이지 이동 등)을 구현한 파일입니다.
 *        다양한 형식의 책(예: ZIP, 폴더 등)에서 공통적으로 사용하는 함수들을 제공합니다.
 */

/**
 * @brief 페이지 엔트리(PageEntry) 메모리 해제 함수
 *        GPtrArray의 free_func로 사용됩니다.
 * @param ptr PageEntry 포인터
 */
static void page_entry_free(gpointer ptr)
{
	PageEntry* entry = ptr;
	if (entry->name)
		g_free(entry->name);
	g_free(entry);
}

/**
 * @brief Book 객체의 기본 정보를 초기화합니다.
 *        파일 경로, 파일명, 디렉토리명, 페이지 엔트리 배열을 생성합니다.
 * @param book Book 객체 포인터
 * @param filename 책 파일의 전체 경로
 */
void book_base_init(Book* book, const char* filename)
{
	book->entries = g_ptr_array_new_with_free_func(page_entry_free);

	book->full_name = g_strdup(filename);
	book->base_name = g_path_get_basename(filename);
	book->dir_name = g_path_get_dirname(filename);
}

/**
 * @brief Book 객체의 리소스를 해제합니다.
 *        엔트리 배열, 파일명, 디렉토리명 등 동적 할당된 메모리를 모두 해제합니다.
 * @param book Book 객체 포인터
 */
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

/**
 * @brief 지정한 페이지의 이미지를 읽어 GdkTexture로 반환합니다.
 *        페이지 데이터가 없거나 읽기 실패 시 기본 'no image' 이미지를 반환합니다.
 * @param book Book 객체 포인터
 * @param page 읽을 페이지 번호
 * @return GdkTexture 포인터(성공 시), 실패 시 기본 이미지
 */
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

	// TODO: 페이지 캐시 기능이 필요하다면 이곳에 구현

	return texture;

pos_return_no_image:
	GdkTexture* no_image = res_get_texture(RES_PIX_NO_IMAGE);
	// ref 반환이 no_image랑 같은지 확인 필요
	return GDK_TEXTURE(g_object_ref(no_image));
}

/**
 * @brief 다음 페이지(또는 쌍페이지)로 이동합니다.
 * @param book Book 객체 포인터
 * @param page_count 페이지 갯수
 * @return 실제로 페이지가 바뀌었으면 true, 아니면 false
 */
bool book_move_next(Book* book, int page_count)
{
	const int save = book->cur_page;
	const int next = save + page_count;
	if (next < book->total_page)
		book->cur_page = next;
	return save != book->cur_page; // 페이지가 바뀌었으면 true 반환
}

/**
 * @brief 이전 페이지(또는 쌍페이지)로 이동합니다.
 * @param book Book 객체 포인터
 * @param page_count 페이지 갯수
 * @return 실제로 페이지가 바뀌었으면 true, 아니면 false
 */
bool book_move_prev(Book* book, int page_count)
{
	const int save = book->cur_page;
	book->cur_page -= page_count;
	if (book->cur_page < 0)
		book->cur_page = 0; // 페이지가 0보다 작아지면 0으로 고정
	return save != book->cur_page; // 페이지가 바뀌었으면 true 반환
}

/**
 * @brief 지정한 페이지로 이동합니다.
 *        범위를 벗어나면 0 또는 마지막 페이지로 이동합니다.
 * @param book Book 객체 포인터
 * @param page 이동할 페이지 번호
 * @return 실제로 페이지가 바뀌었으면 true, 아니면 false
 */
bool book_move_page(Book* book, int page)
{
	int prev = book->cur_page;
	book->cur_page =
		page < 0 ? 0 : page >= book->total_page ? book->total_page - 1 : page;
	return prev != book->cur_page;
}

/**
 * @brief 지정한 페이지의 PageEntry 정보를 반환합니다.
 * @param book Book 객체 포인터
 * @param page 페이지 번호
 * @return PageEntry 포인터(존재하지 않으면 NULL)
 */
const PageEntry* book_get_entry(Book* book, int page)
{
	if (page < 0 || page >= (int)book->entries->len)
		return NULL;
	return g_ptr_array_index(book->entries, page);
}

/**
 * @note
 * - Book 구조체는 다양한 형식의 책(ZIP, 폴더 등)에 공통적으로 사용됩니다.
 * - 페이지 이동, 엔트리 관리 등 기본 동작을 이 파일에서 구현합니다.
 * - 페이지 캐시 기능은 현재 미구현 상태입니다.
 */
