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
	{ "settings", "F12" },
	{ "fullscreen", "f" },
	{ "fullscreen", "<Alt>Return" },
	// 아래 널이 없으면 프로그램은 죽소
	{ NULL, NULL }
};

// <Control> <Alt> <Shift> <Meta>
// C+X : <Control>x
// S+X : <Shift>x
// C+S+X : <Control><Shift>x
// Home End Page_Up Page_Down Delete Insert KP_0 KP_Decimal
