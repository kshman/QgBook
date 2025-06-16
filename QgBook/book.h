#pragma once

// 앞서 정의
typedef struct Book Book;

// 수평 정렬 방식을 나타내는 열거형입니다.
typedef enum HorizAlign
{
	HORIZ_ALIGN_LEFT,
	HORIZ_ALIGN_CENTER,
	HORIZ_ALIGN_RIGHT,
} HorizAlign;

// 책 읽는 방향을 나타내는 열거형입니다.
typedef enum ViewMode
{
	VIEW_MODE_FIT,
	VIEW_MODE_LEFT_TO_RIGHT,
	VIEW_MODE_RIGHT_TO_LEFT,
	VIEW_MODE_MAX_VALUE,
} ViewMode;

// 이미지 품질 수준을 나타내는 열거형입니다.
typedef enum ViewQuality
{
	VIEW_QUALITY_FAST,
	VIEW_QUALITY_DEFAULT,
	VIEW_QUALITY_HIGH,
	VIEW_QUALITY_NEAREST,
	VIEW_QUALITY_BILINEAR,
	VIEW_QUALITY_MAX_VALUE,
} ViewQuality;

// 책의 탐색 및 제어 동작을 나타내는 열거형입니다.
typedef enum BookControl
{
	BOOK_CTRL_PREV,
	BOOK_CTRL_NEXT,
	BOOK_CTRL_FIRST,
	BOOK_CTRL_LAST,
	BOOK_CTRL_10_PREV,
	BOOK_CTRL_10_NEXT,
	BOOK_CTRL_MINUS,
	BOOK_CTRL_PLUS,
	BOOK_CTRL_SCAN_PREV,
	BOOK_CTRL_SCAN_NEXT,
	BOOK_CTRL_SELECT,
} BookControl;

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
} BookFunc;

// 책
struct Book
{
	BookFunc func;

	//GHashTable* cache; // 일단 캐시 사용 안함
	GPtrArray* entries;

	gchar* filename;
	gchar* base_name;
	gchar* dir_name;

	int cur_page;
	int total_page;
};

extern void book_base_init(Book* book, const char* filename);
extern void book_base_dispose(Book* book);

extern GdkPaintable* book_read_page(Book* book, int page);
extern bool book_move_next(Book* book, ViewMode mode);
extern bool book_move_prev(Book* book, ViewMode mode);
extern bool book_move_page(Book* book, int page);
extern const PageEntry* book_get_entry(Book* book, int page);

inline void book_dispose(Book* book) { book->func.dispose(book); }
inline GBytes* book_read_data(Book* book, int page) { return book->func.read_data(book, page); }
inline bool book_can_delete(Book* book) { return book->func.can_delete(book); }
inline bool book_delete(Book* book) { return book->func.delete(book); }
inline bool book_move(Book* book, const char* move_filename) { return book->func.move(book, move_filename); }
inline gchar* book_rename(Book* book, const char* new_filename) { return book->func.rename(book, new_filename); }

