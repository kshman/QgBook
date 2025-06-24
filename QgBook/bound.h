#pragma once

#include "defs.h"

// 사각형
typedef struct BoundRect
{
	int left, top; // 왼쪽, 위쪽 좌표
	int right, bottom; // 오른쪽, 아래쪽 좌표
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
	return (BoundRect){x, y, x + width, y + height};
}

static inline BoundRect bound_rect_init(int left, int top, int right, int bottom)
{
	return (BoundRect){left, top, right, bottom};
}

static inline int bound_rect_width(const BoundRect* r)
{
	return r->right - r->left;
}

static inline int bound_rect_height(const BoundRect* r)
{
	return r->bottom - r->top;
}

static inline BoundRect bound_rect_move(const BoundRect* r, int dx, int dy)
{
	return (BoundRect){
		r->left + dx, r->top + dy,
		r->right + dx, r->bottom + dy
	};
}

static inline BoundRect bound_rect_inflate(const BoundRect* r, int dx, int dy)
{
	return (BoundRect){
		r->left - dx, r->top - dy,
		r->right + dx, r->bottom + dy
	};
}

static inline BoundPoint bound_point(int x, int y)
{
	return (BoundPoint){x, y};
}

static inline BoundSize bound_size(int width, int height)
{
	return (BoundSize){width, height};
}

#define BOUND_RECT_TO_GRAPHENE_RECT(rt) \
	GRAPHENE_RECT_INIT((rt)->left, (rt)->top, bound_rect_width(rt), bound_rect_height(rt))


// 대상 사각형을 계산합니다
static inline BoundRect bound_rect_calc_rect(HorizAlign align, int tw, int th, int dw, int dh)
{
	BoundRect rt = {0, 0, dw, dh};
	if (align == HORIZ_ALIGN_LEFT)
	{
		// 왼쪽 정렬
		// ..은 할게 없다
	}
	else
	{
		// 오른쪽 + 가운데 정렬
		if (dw < tw)
		{
			rt.left = tw - dw;

			// 가운데
			if (align == HORIZ_ALIGN_CENTER)
				rt.left /= 2;
		}
	}
	if (dh < th)
		rt.top = (th - dh) / 2;
	rt.right += rt.left;
	rt.bottom += rt.top;
	return rt;
}

// 원본과 대상의 크기 및 확대 여부에 따라 최적의 크기를 계산합니다
static inline BoundSize bound_size_calc_dest(bool zoom, int dw, int dh, int sw, int sh)
{
	const double dst_aspect = dw / (double)dh;
	const double src_aspect = sw / (double)sh;
	int nw = dw, nh = dh;

	if (zoom)
	{
		if (src_aspect > 1)
		{
			// 세로로 긴 그림
			if (dst_aspect < src_aspect)
				nh = (int)(dw / src_aspect);
			else
				nw = (int)(dh * src_aspect);
		}
		else
		{
			// 가로로 긴 그림
			if (dst_aspect > src_aspect)
				nw = (int)(dh * src_aspect);
			else
				nh = (int)(dw / src_aspect);
		}
	}
	else
	{
		// 가로로 맞춘다... 스크롤은 쌩깜
		nh = (int)(dw / src_aspect);
	}

	return (BoundSize){nw, nh};
}
