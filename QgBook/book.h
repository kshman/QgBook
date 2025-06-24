#pragma once

#include "defs.h"

/**
 * @file book.h
 * @brief 책(Book) 객체와 관련된 구조체, 함수, 콜백 타입을 정의하는 헤더 파일입니다.
 *        페이지 정보, 책의 동작(함수 테이블), 콜백 등 다양한 책 관련 인터페이스를 제공합니다.
 */

typedef struct Book Book;

/**
 * @brief 페이지 엔트리(PageEntry) 구조체
 *        한 페이지(이미지 등)의 파일 정보와 메타데이터를 저장합니다.
 */
typedef struct PageEntry
{
	int page;			///< 페이지 번호 (0부터 시작)
	int manage;			///< 관리용 번호 (0부터 시작, -1은 관리 안함)
	char* name;			///< 파일 이름
	time_t date;		///< 파일 최종 수정 날짜
	int64_t size;		///< 페이지 크기(바이트)
	int64_t comp;		///< 압축된 크기(0은 압축 안함)
} PageEntry;

/**
 * @brief 책의 동작을 정의하는 함수 테이블(BookFunc)
 *        다형성을 위해 각 동작을 함수 포인터로 정의합니다.
 */
typedef struct BookFunc
{
	void (*dispose)(Book*);                        ///< 책 해제

	GBytes* (*read_data)(Book*, int page);         ///< 페이지 데이터 읽기

	bool (*can_delete)(Book*);                     ///< 삭제 가능 여부 확인
	bool (*delete)(Book*);                         ///< 책 파일 삭제
	bool (*move)(Book*, const char* move_filename);///< 책 파일 이동
	gchar* (*rename)(Book*, const char* new_filename); ///< 책 파일 이름 바꾸기

	// 정적 함수
	bool (*ext_compare)(const char* filename);     ///< 확장자 비교(책 형식 판별)
} BookFunc;

/**
 * @brief 책(Book) 객체의 구조체
 *        함수 테이블, 페이지 목록, 파일 경로 등 책의 상태와 정보를 포함합니다.
 */
struct Book
{
	BookFunc func;         ///< 동작 함수 테이블

	GPtrArray* entries;    ///< 페이지 엔트리 배열(GPtrArray<PageEntry*>)

	gchar* full_name;      ///< 전체 경로
	gchar* base_name;      ///< 파일 이름만
	gchar* dir_name;       ///< 디렉토리 이름

	int cur_page;          ///< 현재 페이지
	int total_page;        ///< 전체 페이지 수
};

/**
 * @brief Book 객체의 기본 초기화 함수
 * @param book Book 객체 포인터
 * @param filename 책 파일 경로
 */
extern void book_base_init(Book* book, const char* filename);

/**
 * @brief Book 객체의 리소스 해제 함수
 * @param book Book 객체 포인터
 */
extern void book_base_dispose(Book* book);

/**
 * @brief 지정한 페이지의 이미지를 읽어 GdkTexture로 반환합니다.
 * @param book Book 객체 포인터
 * @param page 읽을 페이지 번호
 * @return GdkTexture 포인터(성공 시), 실패 시 기본 이미지
 */
extern GdkTexture* book_read_page(Book* book, int page);

extern bool book_read_anim(Book* book, int page, GdkTexture** out_texture, GdkPixbufAnimation** out_animation);

/**
 * @brief 다음 페이지(또는 쌍페이지)로 이동합니다.
 * @param book Book 객체 포인터
 * @param page_count 페이지 갯수
 * @return 실제로 페이지가 바뀌었으면 true, 아니면 false
 */
extern bool book_move_next(Book* book, int page_count);

/**
 * @brief 이전 페이지(또는 쌍페이지)로 이동합니다.
 * @param book Book 객체 포인터
 * @param page_count 페이지 갯수
 * @return 실제로 페이지가 바뀌었으면 true, 아니면 false
 */
extern bool book_move_prev(Book* book, int page_count);

/**
 * @brief 지정한 페이지로 이동합니다.
 * @param book Book 객체 포인터
 * @param page 이동할 페이지 번호
 * @return 실제로 페이지가 바뀌었으면 true, 아니면 false
 */
extern bool book_move_page(Book* book, int page);

/**
 * @brief 지정한 페이지의 PageEntry 정보를 반환합니다.
 * @param book Book 객체 포인터
 * @param page 페이지 번호
 * @return PageEntry 포인터(존재하지 않으면 NULL)
 */
extern const PageEntry* book_get_entry(Book* book, int page);

/**
 * @brief Book 객체를 해제합니다. (inline)
 * @param book Book 객체 포인터
 */
static inline void book_dispose(Book* book) { book->func.dispose(book); }

/**
 * @brief 지정한 페이지의 데이터를 읽어옵니다. (inline)
 * @param book Book 객체 포인터
 * @param page 페이지 번호
 * @return GBytes 포인터
 */
static inline GBytes* book_read_data(Book* book, int page) { return book->func.read_data(book, page); }

/**
 * @brief 책 파일이 삭제 가능한지 확인합니다. (inline)
 * @param book Book 객체 포인터
 * @return 삭제 가능하면 true
 */
static inline bool book_can_delete(Book* book) { return book->func.can_delete(book); }

/**
 * @brief 책 파일을 삭제합니다. (inline)
 * @param book Book 객체 포인터
 * @return 성공 시 true
 */
static inline bool book_delete(Book* book) { return book->func.delete(book); }

/**
 * @brief 책 파일을 이동합니다. (inline)
 * @param book Book 객체 포인터
 * @param move_filename 이동할 파일명(전체 경로)
 * @return 성공 시 true
 */
static inline bool book_move(Book* book, const char* move_filename) { return book->func.move(book, move_filename); }

/**
 * @brief 책 파일의 이름을 변경합니다. (inline)
 * @param book Book 객체 포인터
 * @param new_filename 새 파일명
 * @return 새 경로 문자열(호출자가 해제 필요), 실패 시 NULL
 */
static inline gchar* book_rename(Book* book, const char* new_filename) { return book->func.rename(book, new_filename); }

/**
 * @brief ZIP 파일로부터 Book 객체를 생성합니다.
 * @param zip_path ZIP 파일 경로
 * @return 생성된 Book 객체 포인터, 실패 시 NULL
 */
extern Book* book_zip_new(const char* zip_path);
