#include "pch.h"
#include "configs.h"

/**
 * @file configs_ss.c
 * @brief 프로그램에서 사용하는 단축키(ShortcutDefinition) 목록을 정의한 파일입니다.
 *        각 단축키는 명령어(action)와 키 조합(alias)로 구성되며, 배열의 마지막은 {NULL, NULL}로 종료됩니다.
 */

/**
 * @var shortcut_defs
 * @brief 단축키 명령(action)과 키 조합(alias)을 정의하는 ShortcutDefinition 배열
 *
 * - action: 내부적으로 사용할 명령어 이름(문자열)
 * - alias:  단축키 조합(문자열, 예: "F3", "<Control>w", "<Alt>x" 등)
 *
 * 한 action에 여러 alias를 지정할 수 있습니다(동일 명령에 여러 단축키 허용).
 * 배열의 마지막은 반드시 {NULL, NULL}로 종료해야 합니다.
 */
ShortcutDefinition shortcut_defs[] =
{
	// 테스트 및 종료
	{"test", "F8"}, ///< 테스트용 단축키
	{"exit", "<Alt>x"}, ///< 프로그램 종료(Alt+X)
	{"escape", "Escape"}, ///< ESC 키(종료 등)

	// 파일 관련
	{"file_open", "F3"}, ///< 파일 열기
	{"file_close", "F4"}, ///< 파일 닫기
	{"file_close", "<Control>w"}, ///< 파일 닫기(Ctrl+W)
	{"settings", "F12"}, ///< 설정 열기

	// 전체화면
	{"fullscreen", "f"}, ///< 전체화면 전환(f)
	{"fullscreen", "<Alt>Return"}, ///< 전체화면 전환(Alt+Enter)

	// 최근/기억 파일
	{"open_last_book", "<Control><Shift>z"}, ///< 마지막 책 열기(Ctrl+Shift+Z)
	{"open_last_book", "<Shift>F3"}, ///< 마지막 책 열기(Shift+F3)
	{"open_remember", "<Control><Shift>r"}, ///< 기억 파일 열기(Ctrl+Shift+R)
	{"save_remember", "<Control><Shift>s"}, ///< 기억 파일 저장(Ctrl+Shift+S)

	// 파일 관리
	{"delete_book", "Delete"}, ///< 책 삭제
	{"rename_book", "F2"}, ///< 책 이름 바꾸기
	{"move_book", "Insert"}, ///< 책 이동

	// 페이지 이동
	{"page_prev", "Left"}, ///< 이전 페이지(←)
	{"page_prev", "KP_Decimal"}, ///< 이전 페이지(키패드 .)
	{"page_next", "Right"}, ///< 다음 페이지(→)
	{"page_next", "space"}, ///< 다음 페이지(Space)
	{"page_next", "KP_0"}, ///< 다음 페이지(키패드 0)
	{"page_first", "Home"}, ///< 첫 페이지
	{"page_last", "End"}, ///< 마지막 페이지
	{"page_10_prev", "Page_Up"}, ///< 10페이지 이전
	{"page_10_next", "Page_Down"}, ///< 10페이지 다음
	{"page_10_next", "BackSpace"}, ///< 10페이지 다음(Backspace)
	{"page_minus", "Up"}, ///< 페이지 -1(↑)
	{"page_plus", "Down"}, ///< 페이지 +1(↓)
	{"page_select", "Return"}, ///< 페이지 선택(Enter)

	// 책 스캔/탐색
	{"scan_book_prev", "bracketleft"}, ///< 이전 책([)
	{"scan_book_next", "bracketright"}, ///< 다음 책(])
	{"scan_book_random", "backslash"}, ///< 랜덤 책(\)

	// 보기/정렬
	{"view_zoom_toggle", "z"}, ///< 보기 확대/축소(z)
	{"view_mode_left_right", "Tab"}, ///< 좌우 보기 모드 전환(Tab)
	{"view_align_center", "<Control>End"}, ///< 가운데 정렬(Ctrl+End)
	{"view_align_left", "<Control>Delete"}, ///< 왼쪽 정렬(Ctrl+Delete)
	{"view_align_right", "<Control>Page_Down"}, ///< 오른쪽 정렬(Ctrl+PageDown)
	{"view_align_toggle", "quoteleft"}, ///< 정렬 토글(`)

	// 배열 종료(필수)
	{NULL, NULL}
};

/**
 * @note
 * - <Control>, <Alt>, <Shift>, <Meta> 등은 키 조합에 사용 가능합니다.
 * - 예시: C+X → <Control>x, S+X → <Shift>x, C+S+X → <Control><Shift>x
 * - Home, End, Page_Up, Page_Down, Delete, Insert, KP_0, KP_Decimal 등 특수키 지원
 * - GDK_KEY_키이름, GDK_KEY_F3 등 GDK 키 이름도 사용 가능
 */
