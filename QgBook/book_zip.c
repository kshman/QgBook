#include "pch.h"
#include "configs.h"
#include "book.h"
#include "doumi.h"

// ZIP으로 된 책
typedef struct BookZip
{
	Book base; // 상속
	zip_t* zip;
} BookZip;

static void bz_dispose(Book* book);
static GBytes *bz_read_data(Book* book, int page);
static bool bz_can_delete(Book* book);
static bool bz_delete(Book* book);
static bool bz_move(Book* book, const char* move_filename);
static gchar *bz_rename(Book* book, const char* new_filename);

// ZIP책 함수 테이블
static BookFunc bz_func =
{
	.dispose = bz_dispose,
	.read_data = bz_read_data,
	.can_delete = bz_can_delete,
	.delete = bz_delete,
	.move = bz_move,
	.rename = bz_rename,
	.ext_compare = doumi_is_archive_zip, // ZIP파일인지 확인하는 함수
};

// ZIP책 만들기
Book *book_zip_new(const char* zip_path)
{
	// 먼저 ZIP파일 부터 확인
	int err = 0;
	zip_t* zip = zip_open(zip_path, ZIP_RDONLY, &err);
	if (zip == NULL)
	{
		const char* errmsg = zip_strerror(zip);
		g_log("BOOK-ZIP", G_LOG_LEVEL_ERROR, _("Failed to open ZIP file '%s': %s(%d)"), zip_path, errmsg, err);
		return NULL; // ZIP파일 열기 실패
	}

	// ZIP책 만들어서 반환
	BookZip* bz = g_new0(BookZip, 1);
	bz->base.func = bz_func;

	book_base_init((Book*)bz, zip_path);

	const zip_int64_t count = zip_get_num_entries(zip, 0);
	for (zip_int64_t i = 0; i < count; i++)
	{
		zip_stat_t s;
		if (zip_stat_index(zip, i, 0, &s) < 0)
			continue; // ZIP 항목 정보 가져오기 실패
		if (s.encryption_method != ZIP_EM_NONE)
			continue; // 암호화된 항목은 지원하지 않음
		if (!doumi_is_image_file(s.name))
			continue; // 이미지 파일이 아님

		// 엔트리를 만들어 넣자고
		PageEntry* e = g_new0(PageEntry, 1);
		e->page = (int)bz->base.entries->len;
		e->manage = (int)i;
		e->name = g_strdup(s.name);
		e->date = s.mtime;
		e->size = (int64_t)s.size;
		e->comp = (int64_t)s.comp_size;
		g_ptr_array_add(bz->base.entries, e);
	}

	bz->zip = zip;
	bz->base.total_page = (int)count;

	return (Book*)bz;
}

static void bz_dispose(Book* book)
{
	BookZip* bz = (BookZip*)book;
	if (bz->zip)
		zip_close(bz->zip); // ZIP파일 닫기
	book_base_dispose(book);
}

static GBytes *bz_read_data(Book* book, int page)
{
	BookZip* bz = (BookZip*)book;

	if (page < 0 || page >= book->total_page)
		return NULL; // 페이지 범위 벗어남

	const PageEntry* entry = g_ptr_array_index(book->entries, page);
	if (entry == NULL || page != entry->page)
		return NULL; // 페이지 항목이 없거나 페이지 번호가 일치하지 않음

	zip_file_t* zf = zip_fopen_index(bz->zip, entry->manage, 0);
	if (zf == NULL)
		return NULL; // ZIP파일에서 항목 열기 실패

	gpointer buf = g_malloc(entry->size);
	zip_int64_t n = zip_fread(zf, buf, entry->size);

	GBytes* ret = NULL;
	if (n != entry->size)
		g_free(buf); // 읽기 실패시 버퍼 해제
	else
	{
		ret = g_bytes_new_take(buf, entry->size); // GBytes로 변환
		if (ret == NULL)
			g_log("BOOK-ZIP", G_LOG_LEVEL_ERROR, _("Failed to create page %d"), page);
	}

	zip_fclose(zf);
	return ret;
}

static bool bz_can_delete(Book* book)
{
	// 원래 파일이 읽기 전용인가만 확인하자.
	return !doumi_is_file_readonly(book->full_name);
}

static bool bz_delete(Book* book)
{
	BookZip* bz = (BookZip*)book;

	if (bz->zip)
	{
		zip_close(bz->zip);
		bz->zip = NULL;
	}

	GFile* file = g_file_new_for_path(book->full_name);
	const bool res = g_file_trash(file, NULL, NULL);

	if (!res)
	{
		// 바로 지워보자
		if (!g_file_delete(file, NULL, NULL))
		{
			g_object_unref(file);
			return false;
		}
	}

	g_object_unref(file);

	return true;
}

static bool bz_common_move(BookZip* bz, const char* src_path, const char* dst_path)
{
	if (bz->zip)
	{
		zip_close(bz->zip);
		bz->zip = NULL;
	}

	GFile* src = g_file_new_for_path(src_path);
	GFile* dst = g_file_new_for_path(dst_path);
	const bool res = g_file_move(src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL);
	g_object_unref(src);
	g_object_unref(dst);

	// 쓸만한 CRT함수가 없어서 이걸로 결과처리
	return res;
}

static bool bz_move(Book* book, const char* move_filename)
{
	if (g_strcmp0(book->full_name, move_filename) == 0)
		return false;

	if (g_file_test(move_filename, G_FILE_TEST_EXISTS))
		return false; // 이미 존재하는 파일

	return bz_common_move((BookZip*)book, book->full_name, move_filename);
}

static gchar *bz_rename(Book* book, const char* new_filename)
{
	gchar* new_path = g_build_filename(book->dir_name, new_filename, NULL);

	if (g_file_test(new_path, G_FILE_TEST_EXISTS))
	{
		g_free(new_path);
		return NULL; // 이미 존재하는 파일
	}

	if (!bz_common_move((BookZip*)book, book->full_name, new_path))
	{
		g_free(new_path);
		return NULL; // 이름 바꾸기 실패
	}

	return new_path; // 새 경로 반환, 호출자가 해제해야 함;
}
