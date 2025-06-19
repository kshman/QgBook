#include "pch.h"
#include "configs.h"

// 단축키 이름을 정의합니다. 이 배열은 단축키의 순서를 정의하며, 인덱스는 ...아직 안만들엇다능.
ShortcutDefinition shortcut_defs[] =
{
	{ "test", "F8" },
	{ "exit", "<Alt>x" },
	{ "escape", "Escape" },
	{ "file_open", "F3" },
	{ "file_close", "F4" },
	{ "file_close", "<Control>w" },
	{ "settings", "F12" },
	{ "fullscreen", "f" },
	{ "fullscreen", "<Alt>Return" },
	{ "open_last_book", "<Control><Shift>z" },
	{ "open_last_book", "<Shift>F3" },
	{ "open_remember", "<Control><Shift>r" },
	{ "save_remember", "<Control><Shift>s" },
	{ "delete_book", "Delete" },
	{ "rename_book", "F2" },
	{ "move_book", "Insert" },
	{ "page_prev", "Left" },
	{ "page_prev", "KP_Decimal" },
	{ "page_next", "Right" },
	{ "page_next", "space" },
	{ "page_next", "KP_0" },
	{ "page_first", "Home" },
	{ "page_last", "End" },
	{ "page_10_prev", "Page_Up" },
	{ "page_10_next", "Page_Down" },
	{ "page_10_next", "BackSpace" },
	{ "page_minus", "Up" },
	{ "page_plus", "Down" },
	{ "page_select", "Enter" },
	{ "scan_book_prev", "bracketleft" },
	{ "scan_book_next", "bracketright" },
	{ "scan_book_random", "backslash" },
	{ "view_mode_left_right", "Tab" },
	// 아래 널이 없으면 프로그램은 죽소
	{ NULL, NULL }
};

// <Control> <Alt> <Shift> <Meta>
// C+X : <Control>x
// S+X : <Shift>x
// C+S+X : <Control><Shift>x
// Home End Page_Up Page_Down Delete Insert KP_0 KP_Decimal
// GDK_KEY_키이름 GDK_KEY_F3
