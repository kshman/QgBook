#pragma once

#include "defs.h"

// 사각형
typedef struct BoundRect
{
	int left, top;		// 왼쪽, 위쪽 좌표
	int right, bottom;	// 오른쪽, 아래쪽 좌표
} BoundRect;

// 점
typedef struct BoundPoint
{
	int x, y; // x, y 좌표
} BoundPoint;

// 크기
typedef struct BoundSize
{
	int width, height; // 너비, 높이
} BoundSize;


static inline BoundRect bound_rect(int x, int y, int width, int height)
{
	return (BoundRect) { x, y, x + width, y + height };
}

static inline BoundRect bound_rect_init(int left, int top, int right, int bottom)
{
	return (BoundRect) { left, top, right, bottom };
}

static inline int bound_rect_width(const BoundRect* r)
{
	return r->right - r->left;
}

static inline int bound_rect_height(const BoundRect* r)
{
	return r->bottom - r->top;
}

static inline BoundPoint bound_point(int x, int y)
{
	return (BoundPoint) { x, y };
}

static inline BoundSize bound_size(int width, int height)
{
	return (BoundSize) { width, height };
}


// 대상 사각형을 계산합니다
static inline BoundRect bound_rect_calc_rect(HorizAlign align, const BoundSize target, const BoundSize dest)
{
	BoundRect r = { 0, 0, dest.width, dest.height };
	if (align == HORIZ_ALIGN_LEFT)
	{
		// 왼쪽 정렬
		// ..은 할게 없다
	}
	else
	{
		// 오른쪽 + 가운데 정렬
		if (dest.width < target.width)
		{
			r.left = target.width - dest.width;
			// 가운데 정렬
			if (align == HORIZ_ALIGN_CENTER)
				r.left /= 2;
		}
	}
	if (dest.height < target.height)
		r.top = (target.height - dest.height) / 2; // 세로는 항상 가운데 정렬
	return r;
}

// 원본과 대상의 크기 및 확대 여부에 따라 최적의 크기를 계산합니다
static inline BoundSize bound_size_calc_optimal(bool zoom, const BoundSize dest, const BoundSize src)
{
	const double src_aspect = src.width / (double)src.height;
	const double dst_aspect = dest.width / (double)dest.height;
	int nw = dest.width, nh = dest.height;
	if (zoom)
	{
		// 확대 모드
		if (src_aspect > 1.0)
		{
			// 너비가 더 넓음
			if (dst_aspect < src_aspect)
			{
				// 너비가 더 좁음
				nw = (int)(dest.height / src_aspect);
			}
			else
			{
				// 높이가 더 넓음
				nh = (int)(dest.width * src_aspect);
			}
		}
		else
		{
			// 높이가 더 넓음
			if (dst_aspect < src_aspect)
			{
				// 너비가 더 좁음
				nw = (int)(dest.height / src_aspect);
			}
			else
			{
				// 높이가 더 좁음
				nh = (int)(dest.width * src_aspect);
			}
		}
	}
	else
	{
		// 축소 모드
		// 너비에 맞춘다
		nh = (int)(dest.width / src_aspect);
	}
	return (BoundSize) { nw, nh };
}
