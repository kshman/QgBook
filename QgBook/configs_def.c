#include "pch.h"
#include "configs.h"

/**
 * @file configs_def.c
 * @brief 프로그램에서 사용하는 각종 설정 항목(ConfigDefinition) 배열을 정의한 파일입니다.
 *        이 배열의 인덱스는 ConfigKeys 열거형과 1:1로 매칭되며, 각 설정의 이름, 기본값, 타입을 지정합니다.
 */

 /**
  * @var config_defs
  * @brief 설정 이름, 기본값, 타입을 정의하는 ConfigDefinition 배열
  *        인덱스는 ConfigKeys와 동일하게 사용됩니다.
  *
  * - name: 설정의 내부 이름(문자열)
  * - value: 기본값(문자열)
  * - type: 값의 타입(CacheType)
  *
  * 예시:
  *   { "WindowWidth", "600", CACHE_TYPE_INT }
  *   → "WindowWidth"라는 이름의 설정, 기본값 600, 타입은 정수
  */
ConfigDefinition config_defs[CONFIG_MAX_VALUE] =
{
	{ "", "", CACHE_TYPE_UNKNOWN },                ///< CONFIG_NONE (사용 안 함/예약)

	// 실행 관련
	{ "RunCount", "0", CACHE_TYPE_LONG },          ///< 프로그램 실행 횟수
	{ "RunDuration", "0", CACHE_TYPE_DOUBLE },     ///< 누적 실행 시간(초)

	// 윈도우 위치/크기
	{ "WindowX", "-1", CACHE_TYPE_INT },           ///< 윈도우 X 좌표
	{ "WindowY", "-1", CACHE_TYPE_INT },           ///< 윈도우 Y 좌표
	{ "WindowWidth", "600", CACHE_TYPE_INT },      ///< 윈도우 너비
	{ "WindowHeight", "400", CACHE_TYPE_INT },     ///< 윈도우 높이

	// 일반 설정
	{ "GeneralRunOnce", "1", CACHE_TYPE_BOOL },            ///< 최초 실행 여부
	{ "GeneralEscExit", "1", CACHE_TYPE_BOOL },            ///< ESC로 종료
	{ "GeneralConfirmDelete", "1", CACHE_TYPE_BOOL },      ///< 삭제 확인
	{ "GeneralMaxPageCache", "230", CACHE_TYPE_INT },      ///< 최대 페이지 캐시(MB)
	{ "GeneralExternalRun", "", CACHE_TYPE_STRING },       ///< 외부 프로그램 실행 명령
	{ "GeneralReloadAfterExternal", "1", CACHE_TYPE_BOOL },///< 외부 실행 후 새로고침

	// 마우스 관련
	{ "MouseDoubleClickFullscreen", "0", CACHE_TYPE_BOOL },///< 더블클릭 전체화면
	{ "MouseClickPaging", "0", CACHE_TYPE_BOOL },          ///< 클릭으로 페이지 넘김

	// 보기 관련
	{ "ViewZoom", "1", CACHE_TYPE_BOOL },          ///< 확대/축소 사용
	{ "ViewMode", "0", CACHE_TYPE_INT },           ///< 보기 모드(0:기본, 1:좌우 등)
	{ "ViewQuality", "1", CACHE_TYPE_INT },        ///< 보기 품질
	{ "ViewMargin", "0", CACHE_TYPE_INT },         ///< 보기 마진

	// 보안 관련
	{ "SecurityUsePass", "0", CACHE_TYPE_BOOL },   ///< 비밀번호 사용 여부
	{ "SecurityPassCode", "", CACHE_TYPE_STRING }, ///< 비밀번호
	{ "SecurityPassUsage", "", CACHE_TYPE_STRING },///< 비밀번호 사용 이력

	// 파일 관련
	{ "FileLastDirectory", "", CACHE_TYPE_STRING },///< 마지막으로 연 디렉토리
	{ "FileLastFile", "", CACHE_TYPE_STRING },     ///< 마지막으로 연 파일
	{ "FileRemember", "", CACHE_TYPE_STRING },     ///< 최근 파일 목록 등

	// 최종값 테스트 (사용 안 함)
	//{ NULL, NULL, 0 },
};
