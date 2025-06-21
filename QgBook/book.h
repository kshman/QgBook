#pragma once

#include "defs.h"

typedef struct Book Book;

// 페이지 엔트리
typedef struct PageEntry
{
	int page;			// 페이지 번호 (0부터 시작)
	int manage;			// 관리용 번호 (0부터 시작, -1은 관리 안함)
	char* name;			// 파일 이름
	time_t date;		// 파일 최종 수정 날짜
	int64_t size;		// 페이지 크기
	int64_t comp;		// 압축된 크기 (0은 압축 안함)
} PageEntry;

// 책 함수
typedef struct BookFunc
{
	void (*dispose)(Book*);

	GBytes* (*read_data)(Book*, int page);
	// get_page_infos;

	bool (*can_delete)(Book*);
	bool (*delete)(Book*);
	bool (*move)(Book*, const char* move_filename);
	gchar* (*rename)(Book*, const char* new_filename);

	// 정적 함수
	bool (*ext_compare)(const char* filename);
} BookFunc;

// 책
struct Book
{
	BookFunc func;

	//GHashTable* cache; // 일단 캐시 사용 안함
	GPtrArray* entries;

	gchar* full_name;	// 전체 경로
	gchar* base_name;	// 파일 이름만
	gchar* dir_name;	// 디렉토리 이름

	int cur_page;
	int total_page;
};

extern void book_base_init(Book* book, const char* filename);
extern void book_base_dispose(Book* book);

extern GdkTexture* book_read_page(Book* book, int page);
extern bool book_move_next(Book* book, ViewMode mode);
extern bool book_move_prev(Book* book, ViewMode mode);
extern bool book_move_page(Book* book, int page);
extern const PageEntry* book_get_entry(Book* book, int page);

static inline void book_dispose(Book* book) { book->func.dispose(book); }
static inline GBytes* book_read_data(Book* book, int page) { return book->func.read_data(book, page); }
static inline bool book_can_delete(Book* book) { return book->func.can_delete(book); }
static inline bool book_delete(Book* book) { return book->func.delete(book); }
static inline bool book_move(Book* book, const char* move_filename) { return book->func.move(book, move_filename); }
static inline gchar* book_rename(Book* book, const char* new_filename) { return book->func.rename(book, new_filename); }

extern Book *book_zip_new(const char* zip_path);


// 이름 바꾸기 구조체
typedef struct RenameData
{
	bool result;			// 이름 바꾸기 성공 여부
	bool reopen;			// 다시 열기 플래그
	char filename[2048];	// 새 파일 이름 (최대 1024자)
} RenameData;

// 이름 바꾸기 콜백
typedef void (*RenameCallback)(gpointer sender, RenameData* data);

// 쪽 선택 콜백
typedef void (*PageSelectCallback)(gpointer sender, int page);
