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

static inline BoundRect bound_rect_delta(const BoundRect* r, int dx, int dy)
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
	GRAPHENE_RECT_INIT((float)(rt)->left, (float)(rt)->top, (float)bound_rect_width(rt), (float)bound_rect_height(rt))


// 대상 사각형을 계산합니다
static inline BoundRect bound_rect_calc_rect(HorizAlign align, int tw, int th, int dw, int dh)
{
	int left = 0, top = 0;

	// 가로 정렬
	if (align == HORIZ_ALIGN_RIGHT)
		left = tw > dw ? tw - dw : 0;
	else if (align == HORIZ_ALIGN_CENTER)
		left = tw > dw ? (tw - dw) / 2 : 0;

	// 세로 정렬(항상 중앙)
	if (th > dh)
		top = (th - dh) / 2;

	return bound_rect(left, top, dw, dh);
}

// 원본과 대상의 크기 및 확대 여부에 따라 최적의 크기를 계산합니다
static inline BoundSize bound_size_calc_dest(bool zoom, int dw, int dh, int sw, int sh)
{
	double src_aspect = sw / (double)sh;
	double dst_aspect = dw / (double)dh;
	int nw = dw, nh = dh;

	if (zoom)
	{
		if (src_aspect > dst_aspect)
		{
			// 원본이 더 가로로 김: 너비를 맞추고 높이 조정
			nh = (int)(dw / src_aspect);
		}
		else
		{
			// 원본이 더 세로로 김: 높이를 맞추고 너비 조정
			nw = (int)(dh * src_aspect);
		}
	}
	else
	{
		// 항상 너비 기준으로 맞춤
		//nh = (int)(dw / src_aspect);
		nw = sw;
		nh = sh;
	}

	return (BoundSize){nw, nh};
}
