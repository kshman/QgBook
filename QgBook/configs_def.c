#include "pch.h"
#include "configs.h"

// 설정 이름을 정의합니다. 이 배열은 설정의 순서를 정의하며, 인덱스는 ConfigKeys입니다.
ConfigDefinition config_defs[CONFIG_MAX_VALUE] =
{
	{ "", "", CACHE_TYPE_UNKNOWN },
	// 실행
	{ "RunCount", "0", CACHE_TYPE_LONG },
	{ "RunDuration", "0", CACHE_TYPE_DOUBLE },
	// 윈도우
	{ "WindowX", "-1", CACHE_TYPE_INT },
	{ "WindowY", "-1", CACHE_TYPE_INT },
	{ "WindowWidth", "600", CACHE_TYPE_INT },
	{ "WindowHeight", "400", CACHE_TYPE_INT },
	// 일반
	{ "GeneralRunOnce", "1", CACHE_TYPE_BOOL },
	{ "GeneralEscExit", "1", CACHE_TYPE_BOOL },
	{ "GeneralConfirmDelete", "1", CACHE_TYPE_BOOL },
	{ "GeneralMaxPageCache", "230", CACHE_TYPE_INT },
	{ "GeneralExternalRun", "", CACHE_TYPE_STRING },
	{ "GeneralReloadAfterExternal", "1", CACHE_TYPE_BOOL },
	// 마우스
	{ "MouseDoubleClickFullscreen", "0", CACHE_TYPE_BOOL },
	{ "MouseClickPaging", "0", CACHE_TYPE_BOOL },
	// 보기
	{ "ViewZoom", "1", CACHE_TYPE_BOOL },
	{ "ViewMode", "0", CACHE_TYPE_INT },
	{ "ViewQuality", "1", CACHE_TYPE_INT },
	{ "ViewMargin", "0", CACHE_TYPE_INT, },
	// 보안
	{ "SecurityUsePass", "0", CACHE_TYPE_BOOL },
	{ "SecurityPassCode", "", CACHE_TYPE_STRING },
	{ "SecurityPassUsage", "", CACHE_TYPE_STRING },
	// 파일
	{ "FileLastDirectory", "", CACHE_TYPE_STRING },
	{ "FileLastFile", "", CACHE_TYPE_STRING },
	{ "FileRemember", "", CACHE_TYPE_STRING },
	// 최종값 테스트
	//{ NULL, NULL, 0 },
};
