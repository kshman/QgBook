#pragma once

/// <summary>
/// 수평 정렬 방식을 나타내는 열거형입니다.
/// </summary>
typedef enum HorizAlign
{
	HORIZ_ALIGN_LEFT,
	HORIZ_ALIGN_CENTER,
	HORIZ_ALIGN_RIGHT,
} HorizAlign;

/// <summary>
/// 책 읽는 방향을 나타내는 열거형입니다.
/// </summary>
typedef enum ViewMode
{
	VIEW_MODE_FIT,
	VIEW_MODE_LEFT_TO_RIGHT,
	VIEW_MODE_RIGHT_TO_LEFT,
	VIEW_MODE_MAX_VALUE,
} ViewMode;

/// <summary>
/// 이미지 품질 수준을 나타내는 열거형입니다.
/// </summary>
typedef enum ViewQuality
{
	VIEW_QUALITY_FAST,
	VIEW_QUALITY_DEFAULT,
	VIEW_QUALITY_HIGH,
	VIEW_QUALITY_NEAREST,
	VIEW_QUALITY_BILINEAR,
	VIEW_QUALITY_MAX_VALUE,
} ViewQuality;

/// <summary>
/// 책의 탐색 및 제어 동작을 나타내는 열거형입니다.
/// </summary>
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

