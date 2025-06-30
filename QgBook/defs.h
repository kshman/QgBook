#pragma once

// 이미지 파일 형식
typedef enum ImageFileType
{
	IMAGE_FILE_TYPE_UNKNOWN,
	IMAGE_FILE_TYPE_JPEG,
	IMAGE_FILE_TYPE_PNG,
	IMAGE_FILE_TYPE_GIF,
	IMAGE_FILE_TYPE_BMP,
	IMAGE_FILE_TYPE_TIFF,
	IMAGE_FILE_TYPE_WEBP,
	IMAGE_FILE_TYPE_MAX_VALUE,
} ImageFileType;

// 수평 정렬 방식을 나타내는 열거형입니다.
typedef enum HorizAlign
{
	HORIZ_ALIGN_CENTER,
	HORIZ_ALIGN_LEFT,
	HORIZ_ALIGN_RIGHT,
	HORIZ_ALIGN_MAX_VALUE,
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
	BOOK_CTRL_SCAN_RANDOM,
	BOOK_CTRL_SELECT,
	BOOK_CTRL_MAX_VALUE,
} BookControl;

// 이미지 정보
typedef struct ImageInfo
{
	ImageFileType type;  // 이미지 파일 형식
	int width;           // 이미지 폭
	int height;          // 이미지 높이
	size_t size;         // 이미지 크기(바이트)
	bool has_anim;       // 애니메이션 여부
} ImageInfo;
