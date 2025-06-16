#include "pch.h"
#include "configs.h"
#include "book.h"

// ZIP으로 된 책
typedef struct BookZip
{
	Book base; // 상속
	zip_t* zip;
} BookZip;

static void bz_dispose(Book* book);
static GBytes* bz_read_data(Book* book, int page);
static bool bz_can_delete(Book* book);
static bool bz_delete(Book* book);
static bool bz_move(Book* book, const char* move_filename);
static gchar* bz_rename(Book* book, const char* new_filename);

// ZIP책 함수 테이블
static BookFunc bz_func =
{
	.dispose = bz_dispose,
	.read_data = bz_read_data,
	.can_delete = bz_can_delete,
	.delete = bz_delete,
	.move = bz_move,
	.rename = bz_rename
};

// ZIP책 만들기
Book* book_zip_new(const char* zip_path)
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

		PageEntry* e = g_new0(PageEntry, 1);
		e->page = (int)i;
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

static GBytes* bz_read_data(Book* book, int page)
{
	BookZip* bz = (BookZip*)book;

	if (page < 0 || page >= book->total_page)
		return NULL; // 페이지 범위 벗어남

	const PageEntry* entry = g_ptr_array_index(book->entries, page);
	if (entry == NULL)
		return NULL; // 페이지 항목이 없음

	zip_file_t* zf = zip_fopen_index(bz->zip, page, 0);
}

static bool bz_can_delete(Book* book)
{
	return false;
}

static bool bz_delete(Book* book)
{
	return false;
}

static bool bz_move(Book* book, const char* move_filename)
{
	return false;
}

static gchar* bz_rename(Book* book, const char* new_filename)
{
	return NULL;
}
