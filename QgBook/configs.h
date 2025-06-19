#pragma once

// 설정 키
typedef enum ConfigKeys
{
	CONFIG_NONE = 0, // 설정 없음
	// 실행
	CONFIG_RUN_COUNT, // 실행 횟수
	CONFIG_RUN_DURATION, // 실행 시간 (초 단위)
	// 윈도우
	CONFIG_WINDOW_X, // 윈도우 X 좌표
	CONFIG_WINDOW_Y, // 윈도우 Y 좌표
	CONFIG_WINDOW_WIDTH, // 윈도우 너비
	CONFIG_WINDOW_HEIGHT, // 윈도우 높이
	// 일반
	CONFIG_GENERAL_RUN_ONCE, // 한 번만 실행
	CONFIG_GENERAL_ESC_EXIT, // ESC 키로 종료
	CONFIG_GENERAL_CONFIRM_DELETE, // 책 삭제 확인
	CONFIG_GENERAL_MAX_PAGE_CACHE, // 최대 캐시 크기
	CONFIG_GENERAL_EXTERNAL_RUN, // 외부 프로그램 실행
	CONFIG_GENERAL_RELOAD_AFTER_EXTERNAL, // 외부 프로그램 실행 후 재시작
	// 마우스
	CONFIG_MOUSE_DOUBLE_CLICK_FULLSCREEN, // 더블 클릭으로 전체 화면 전환
	CONFIG_MOUSE_CLICK_PAGING, // 클릭으로 페이지 넘기기
	// 보기
	CONFIG_VIEW_ZOOM, // 보기 확대
	CONFIG_VIEW_MODE, // 보기 모드
	CONFIG_VIEW_QUALITY, // 보기 품질
	CONFIG_VIEW_MARGIN, // 보기 여백
	// 보안
	CONFIG_SECURITY_USE_PASS, // 비밀번호 보호
	CONFIG_SECURITY_PASS_CODE, // 비밀번호 값
	CONFIG_SECURITY_PASS_USAGE, // 비밀번호 용도
	// 파일
	CONFIG_FILE_LAST_DIRECTORY, // 마지막으로 열었던 디렉토리
	CONFIG_FILE_LAST_FILE, // 마지막으로 열었던 파일
	CONFIG_FILE_REMEMBER, // 기억해둘 파일 이름
	//
	CONFIG_MAX_VALUE, // 최대 값 (마지막 값은 반드시 이 값을 사용해야 함)
} ConfigKeys;

// 설정 캐시 아이템의 변수 타입
typedef enum CacheType
{
	CACHE_TYPE_UNKNOWN,    // 알 수 없는 타입
	CACHE_TYPE_INT,        // 정수형
	CACHE_TYPE_LONG,       // 긴 정수형
	CACHE_TYPE_BOOL,       // 불린형
	CACHE_TYPE_DOUBLE,     // 실수형
	CACHE_TYPE_STRING,     // 문자열
} CacheType;

// 설정 항목 정의
typedef struct ConfigDefinition
{
	const char* name;		// 설정 이름
	const char* value;		// 설정 초기값
	CacheType type;			// 설정 타입
} ConfigDefinition;

// 단축키 정의
typedef struct ShortcutDefinition
{
	const char* action;		// 단축키가 적용되는 액션 이름
	const char* alias;		// 단축키 키 문자열 (예: <Control>x, F8 등)
} ShortcutDefinition;

// 이동 위치의 별명과 디렉토리
typedef struct MoveLocation
{
	char* alias;			// 별명
	char* folder;			// 디렉토리
} MoveLocation;


// 설정 관련 함수들
extern bool config_init(void);
extern void config_load_cache(void);
extern void config_dispose(void);

extern const char* config_get_string_ptr(ConfigKeys name, bool cache_only);
extern bool config_get_string(ConfigKeys name, char* value, size_t value_size, bool cache_only);
extern bool config_get_bool(ConfigKeys name, bool cache_only);
extern gint32 config_get_int(ConfigKeys name, bool cache_only);
extern gint64 config_get_long(ConfigKeys name, bool cache_only);

extern void config_set_string(ConfigKeys name, const char* value, bool cache_only);
extern void config_set_bool(ConfigKeys name, bool value, bool cache_only);
extern void config_set_int(ConfigKeys name, gint32 value, bool cache_only);
extern void config_set_long(ConfigKeys name, gint64 value, bool cache_only);

extern uint64_t config_get_actual_max_page_cache(void);

// 최근 파일
extern int recently_get_page(const char* filename);
extern bool recently_set_page(const char* filename, int page);

// 이동 위치
extern MoveLocation* movloc_get_all(int* ret_count);
extern bool movloc_set(const char* folder, const char* alias);
extern bool movloc_delete(const char* folder);
extern void movloc_free(MoveLocation* moves, int count);

// 단축키
extern void shortcut_register(void);
extern const char* shortcut_lookup(const guint key_val, const GdkModifierType key_state);

// 언어
extern const char* locale_lookup(const char* key);
#define _(x) locale_lookup(x)
