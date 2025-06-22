#include "pch.h"
#include <time.h>
#include "configs.h"
#include "doumi.h"

/**
 * @file configs.c
 * @brief 프로그램의 전역 설정, 캐시, 단축키, 최근 파일, 이동 위치, 언어 등
 *        다양한 환경설정 및 관련 데이터베이스 연동을 담당하는 구현 파일입니다.
 */

// 외부 변수 선언
extern ConfigDefinition config_defs[CONFIG_MAX_VALUE];
extern ShortcutDefinition shortcut_defs[];

/**
 * @brief 전역 설정 자료 구조체
 *        실행 시간, 경로, 언어, 단축키, 캐시, 이동 위치, 근처 파일 등 관리
 */
static struct Configs
{
	time_t launched;           ///< 프로그램 실행 시각

	char* app_path;            ///< 설정 파일 저장 경로
	char* cfg_path;            ///< 설정 파일 전체 경로

	GHashTable* lang;          ///< 언어 문자열 해시
	GHashTable* shortcut;      ///< 단축키 해시
	GHashTable* cache;         ///< 설정 캐시 해시
	GPtrArray* moves;          ///< 책 이동 위치 배열

	GPtrArray* nears;          ///< 근처 파일 배열
	NearExtentionCompare near_compare; ///< 근처 파일 비교 함수
	char* near_dir;            ///< 근처 파일 디렉토리
} cfgs =
{
	.app_path = NULL,
	.cfg_path = NULL,
};

/**
 * @brief 설정 캐시 아이템 구조체
 *        다양한 타입의 값을 저장할 수 있도록 union 사용
 */
typedef struct ConfigCacheItem
{
	union
	{
		gint32 n;   ///< 정수형
		gint64 l;   ///< 긴 정수형
		bool b;     ///< 불린형
		double d;   ///< 실수형
		char* s;    ///< 문자열
	};

	CacheType type; ///< 값의 타입
} ConfigCacheItem;

/**
 * @brief 캐시 아이템 메모리 해제 함수
 * @param ptr ConfigCacheItem 포인터
 */
static void cache_item_free(gpointer ptr)
{
	ConfigCacheItem* item = ptr;
	g_return_if_fail(item != NULL);
	if (item->type == CACHE_TYPE_STRING && item->s)
		g_free(item->s);
	g_free(item);
}

/**
 * @brief 캐시에서 아이템을 얻어옵니다.
 * @param key 설정 키
 * @return ConfigCacheItem 포인터(없으면 NULL)
 */
static ConfigCacheItem *cache_get_item(const ConfigKeys key)
{
	const struct ConfigDefinition* def = &config_defs[key];
	return g_hash_table_lookup(cfgs.cache, def->name);
}

/**
 * @brief 캐시에 아이템을 복제하여 저장합니다.
 * @param key 설정 키
 * @param item 저장할 값
 */
static void cache_set_item(const ConfigKeys key, const ConfigCacheItem* item)
{
	ConfigCacheItem* new_item = g_new(ConfigCacheItem, 1);
	memcpy(new_item, item, sizeof(ConfigCacheItem));

	const struct ConfigDefinition* def = &config_defs[key];
	g_hash_table_insert(cfgs.cache, g_strdup(def->name), new_item);
}

/**
 * @brief 타입에 따라 캐시에서 값을 문자열로 얻어옵니다.
 * @param key 설정 키
 * @param value 결과 버퍼
 * @param value_size 버퍼 크기
 * @return 문자열 포인터(문자열 타입이면 내부 포인터, 아니면 value)
 */
static const char *cache_auto_get_item(const ConfigKeys key, char* value, const size_t value_size)
{
	const struct ConfigDefinition* def = &config_defs[key];
	const ConfigCacheItem* item = g_hash_table_lookup(cfgs.cache, def->name);
	g_return_val_if_fail(item != NULL, NULL);

	switch (def->type)
	{
		case CACHE_TYPE_INT:
			g_snprintf(value, (gulong)value_size, "%d", item->n);
			break;
		case CACHE_TYPE_LONG:
			g_snprintf(value, (gulong)value_size, "%lld", item->l);
			break;
		case CACHE_TYPE_BOOL:
			g_strlcpy(value, item->b ? "true" : "false", value_size);
			break;
		case CACHE_TYPE_DOUBLE:
			g_snprintf(value, (gulong)value_size, "%f", item->d);
			break;
		case CACHE_TYPE_STRING:
			return item->s;
		case CACHE_TYPE_UNKNOWN:
			return NULL;
	}

	return value;
}

/**
 * @brief 타입에 따라 문자열 값을 캐시에 저장합니다.
 * @param key 설정 키
 * @param value 저장할 문자열 값
 */
static void cache_auto_set_item(const ConfigKeys key, const char* value)
{
	const struct ConfigDefinition* def = &config_defs[key];
	ConfigCacheItem* item = g_new0(ConfigCacheItem, 1);

	switch (def->type)
	{
		case CACHE_TYPE_INT:
			item->n = (gint32)g_ascii_strtoll(value, NULL, 10);
			break;
		case CACHE_TYPE_LONG:
			item->l = g_ascii_strtoll(value, NULL, 10);
			break;
		case CACHE_TYPE_BOOL:
			item->b = doumi_atob(value);
			break;
		case CACHE_TYPE_DOUBLE:
			item->d = g_ascii_strtod(value, NULL);
			break;
		case CACHE_TYPE_STRING:
			item->s = g_strdup(value);
			break;
		case CACHE_TYPE_UNKNOWN:
			g_free(item);
			return;
	}

	item->type = def->type;
	g_hash_table_insert(cfgs.cache, g_strdup(def->name), item);
}

/**
 * @brief SQL 오류 메시지 출력 및 DB 닫기
 * @param db sqlite3 포인터
 * @param close_db true면 DB도 닫음
 */
static void sql_error(sqlite3* db, bool close_db)
{
	g_return_if_fail(db != NULL);
	const char* err_msg = sqlite3_errmsg(db);
	if (err_msg)
		g_log("SQL", G_LOG_LEVEL_ERROR, "%s", err_msg);
	if (close_db)
		sqlite3_close(db);
}

/**
 * @brief SQL 오류 메시지 출력 및 메모리 해제
 * @param err_msg 오류 메시지
 */
static void sql_free_error(char* err_msg)
{
	if (err_msg)
	{
		g_log("SQL", G_LOG_LEVEL_ERROR, "%s", err_msg);
		sqlite3_free(err_msg);
	}
}

/**
 * @brief 설정 DB를 엽니다.
 * @return sqlite3 포인터(실패 시 NULL)
 */
static sqlite3 *sql_open(void)
{
	sqlite3* db;
	if (sqlite3_open(cfgs.cfg_path, &db) == SQLITE_OK)
		return db;
	if (db != NULL)
		sql_error(db, true);
	return NULL;
}

/**
 * @brief SQL 문장을 실행합니다.
 * @param db sqlite3 포인터
 * @param sql 실행할 SQL
 * @return 성공 시 true
 */
static bool sql_exec_stmt(sqlite3* db, const char* sql)
{
	char* err_msg = NULL;
	if (sqlite3_exec(db, sql, NULL, NULL, &err_msg) != SQLITE_OK)
	{
		sql_free_error(err_msg);
		return false;
	}
	return true;
}

/**
 * @brief DB에서 설정 값을 읽어 캐시에 저장합니다.
 *        값이 없으면 기본값을 캐시에 저장합니다.
 * @param db sqlite3 포인터
 * @param key 설정 키
 * @return 성공 시 true
 */
static bool sql_select_config(sqlite3* db, const ConfigKeys key)
{
	const struct ConfigDefinition* def = &config_defs[key];

	sqlite3_stmt* stmt;
	const char* sql = "SELECT value FROM configs WHERE key = ? LIMIT 1;";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
	{
		sql_error(db, false);
		cache_auto_set_item(key, def->value);
		return false;
	}

	sqlite3_bind_text(stmt, 1, def->name, -1, SQLITE_STATIC);

	const char* value = sqlite3_step(stmt) != SQLITE_ROW ? NULL : (const char*)sqlite3_column_text(stmt, 0);
	cache_auto_set_item(key, value != NULL ? value : def->value);
	sqlite3_finalize(stmt);

	return true;
}

/**
 * @brief DB에 설정 값을 저장합니다.
 * @param db sqlite3 포인터
 * @param key 설정 키
 * @param value 저장할 값
 * @return 성공 시 true
 */
static bool sql_into_config_value(sqlite3* db, const ConfigKeys key, const char* value)
{
	const struct ConfigDefinition* def = &config_defs[key];

	sqlite3_stmt* stmt;
	const char* sql = "INSERT OR REPLACE INTO configs (key, value) VALUES (?, ?);";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
	{
		sql_error(db, false);
		return false;
	}

	sqlite3_bind_text(stmt, 1, def->name, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, value ? value : "", -1, SQLITE_STATIC);

	bool ret = true;
	if (sqlite3_step(stmt) != SQLITE_DONE)
	{
		sql_error(db, false);
		ret = false;
	}

	sqlite3_finalize(stmt);
	return ret;
}

/**
 * @brief 캐시에서 값을 가져와 DB에 저장합니다.
 * @param db sqlite3 포인터
 * @param key 설정 키
 * @return 성공 시 true
 */
static bool sql_into_config(sqlite3* db, const ConfigKeys key)
{
	char sz[128];
	const char* psz = cache_auto_get_item(key, sz, sizeof(sz));
	return sql_into_config_value(db, key, psz ? psz : config_defs[key].value);
}

/**
 * @brief DB에서 값을 읽어 캐시에 저장합니다.
 * @param key 설정 키
 * @return 성공 시 true
 */
static bool sql_get_config(const ConfigKeys key)
{
	sqlite3* db = sql_open();
	g_return_val_if_fail(db != NULL, false);

	bool ret = sql_select_config(db, key);

	sqlite3_close(db);
	return ret;
}

/**
 * @brief 캐시에서 값을 DB에 저장합니다.
 * @param key 설정 키
 * @return 성공 시 true
 */
static bool sql_set_config(const ConfigKeys key)
{
	sqlite3* db = sql_open();
	g_return_val_if_fail(db != NULL, false);

	bool ret = sql_into_config(db, key);

	sqlite3_close(db);
	return ret;
}

/**
 * @brief 책 이동 위치 메모리 해제 함수
 * @param ptr MoveLocation 포인터
 */
static void move_loc_free(gpointer ptr)
{
	MoveLocation* p = ptr;
	g_free(p->alias);
	g_free(p->folder);
	g_free(p);
}

/**
 * @brief 언어 파일 데이터를 해시 테이블로 파싱합니다.
 * @param lht 해시 테이블
 * @param data 언어 파일 데이터
 */
static void parse_language_hash_table_data(GHashTable* lht, const char* data)
{
	g_return_if_fail(lht != NULL && data != NULL);
	const char* p = data;
	while (*p)
	{
		const char* line_end = strchr(p, '\n');
		const size_t len = line_end ? (size_t)(line_end - p) : strlen(p);
		if (len > 0)
		{
			char* line = g_strndup(p, len);
			char* stripped = doumi_string_strip(line);

			// '#'으로 시작하면 주석이므로 무시
			if (*stripped == '#' || *stripped == '\0')
			{
				g_free(stripped);
				g_free(line);
				p += len;
				if (*p == '\n') p++;
				continue;
			}

			char* key = NULL;
			char* value = NULL;
			const char* s = stripped;
			if (*s == '"')
			{
				s++;
				const char* end_quote = strchr(s, '"');
				if (end_quote)
				{
					key = g_strndup(s, end_quote - s);
					const char* eq = strchr(end_quote + 1, '=');
					if (eq)
						value = doumi_string_strip(eq + 1);
				}
			}
			else
			{
				char* eq = strchr(s, '=');
				if (eq)
				{
					*eq = 0;
					key = doumi_string_strip(s);
					value = doumi_string_strip(eq + 1);
				}
			}

			if (key && *key)
				g_hash_table_insert(lht, key, value ? value : g_strdup(""));
			else
			{
				g_free(key);
				g_free(value);
			}
			g_free(stripped);
			g_free(line);
		}
		p += len;
		if (*p == '\n') p++;
	}
}

/**
 * @brief 설정을 초기화합니다. (경로, 언어, 캐시, DB, 이동 위치 등)
 * @return 성공 시 true
 */
bool config_init(void)
{
	// 실행 시간 초기화
	cfgs.launched = time(NULL);

	// 경로 설정
	cfgs.app_path = g_build_filename(g_get_user_config_dir(), "ksh", NULL);
	cfgs.cfg_path = g_build_filename(cfgs.app_path, "QgBook.conf", NULL);
	g_mkdir_with_parents(cfgs.app_path, 0755);

	// 언어 초기화
	cfgs.lang = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

	const char* const* languages = g_get_language_names();
	const char* locale = languages[0];
	char short_locale[3] = "  ";
	const size_t len = strlen(locale);
	if (len > 2)
	{
		short_locale[0] = locale[0];
		short_locale[1] = locale[1];
		locale = short_locale;
	}

	char* data = doumi_load_resource_text(doumi_resource_path_format("lang/%s.txt", locale), NULL);
	if (data != NULL)
	{
		parse_language_hash_table_data(cfgs.lang, data);
		g_free(data);
	}

	// 캐시
	cfgs.cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, cache_item_free);

	// 데이터베이스를 열고, 테이블이 없으면 만듭니다.
	sqlite3* db = sql_open();
	g_return_val_if_fail(db != NULL, false);

	if (!sql_exec_stmt(db, "CREATE TABLE IF NOT EXISTS configs (key TEXT PRIMARY KEY, value TEXT);") ||
		!sql_exec_stmt(db, "CREATE TABLE IF NOT EXISTS moves (no INTEGER PRIMARY KEY, alias TEXT, folder TEXT);") ||
		!sql_exec_stmt(db, "CREATE TABLE IF NOT EXISTS recently (filename TEXT PRIMARY KEY, page INTEGER);") ||
		!sql_exec_stmt(db, "CREATE TABLE IF NOT EXISTS bookmarks (id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT, page INTEGER);") ||
		!sql_exec_stmt(db, "CREATE TABLE IF NOT EXISTS shortcuts (id INTEGER PRIMARY KEY AUTOINCREMENT, action TEXT, alias TEXT);"))
	{
		sqlite3_close(db);
		return false;
	}

	// 실행 횟수 업데이트
	if (sql_select_config(db, CONFIG_RUN_COUNT))
	{
		const ConfigCacheItem* item = cache_get_item(CONFIG_RUN_COUNT);
		char sz[64];
		g_snprintf(sz, sizeof(sz), "%lld", item->l + 1);
		sql_into_config_value(db, CONFIG_RUN_COUNT, sz);
	}

	// 다른 캐시 이전에 가져올 데이터
	sql_select_config(db, CONFIG_RUN_DURATION);
	sql_select_config(db, CONFIG_GENERAL_RUN_ONCE);

	// 단축키, 읽는 거는 register에서 한다
	cfgs.shortcut = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, g_free);

	// 이동 위치
	cfgs.moves = g_ptr_array_new_with_free_func(move_loc_free);

	// 근처 파일
	cfgs.nears = g_ptr_array_new_with_free_func(g_free);

	// ㅇㅋ
	sqlite3_close(db);
	return true;
}

/**
 * @brief 설정을 정리(해제)합니다.
 */
void config_dispose(void)
{
	sqlite3* db = sql_open();
	if (db != NULL)
	{
		const time_t now = time(NULL);
		ConfigCacheItem* run_duration = cache_get_item(CONFIG_RUN_DURATION);
		run_duration->d += difftime(now, cfgs.launched);

		sql_into_config(db, CONFIG_WINDOW_WIDTH);
		sql_into_config(db, CONFIG_WINDOW_HEIGHT);
		sql_into_config(db, CONFIG_RUN_DURATION);

		sqlite3_close(db);
	}

	if (cfgs.near_dir)
		g_free(cfgs.near_dir);
	if (cfgs.nears)
		g_ptr_array_free(cfgs.nears, true);

	if (cfgs.moves)
		g_ptr_array_free(cfgs.moves, true);
	if (cfgs.cache)
		g_hash_table_destroy(cfgs.cache);
	if (cfgs.shortcut)
		g_hash_table_destroy(cfgs.shortcut);
	if (cfgs.lang)
		g_hash_table_destroy(cfgs.lang);

	if (cfgs.cfg_path)
		g_free(cfgs.cfg_path);
	if (cfgs.app_path)
		g_free(cfgs.app_path);
}

/**
 * @brief 설정에서 쓸 값을 캐시로 불러옵니다.
 */
void config_load_cache(void)
{
	sqlite3* db = sql_open();
	g_return_if_fail(db != NULL);

	// 캐시
#ifdef _WIN32
	sql_select_config(db, CONFIG_WINDOW_X);
	sql_select_config(db, CONFIG_WINDOW_Y);
#endif
	sql_select_config(db, CONFIG_WINDOW_WIDTH);
	sql_select_config(db, CONFIG_WINDOW_HEIGHT);

	sql_select_config(db, CONFIG_GENERAL_ESC_EXIT);
	sql_select_config(db, CONFIG_GENERAL_CONFIRM_DELETE);
	sql_select_config(db, CONFIG_GENERAL_MAX_PAGE_CACHE);
	sql_select_config(db, CONFIG_GENERAL_EXTERNAL_RUN);
	sql_select_config(db, CONFIG_GENERAL_RELOAD_AFTER_EXTERNAL);

	sql_select_config(db, CONFIG_MOUSE_DOUBLE_CLICK_FULLSCREEN);
	sql_select_config(db, CONFIG_MOUSE_CLICK_PAGING);

	sql_select_config(db, CONFIG_VIEW_ZOOM);
	sql_select_config(db, CONFIG_VIEW_MODE);
	sql_select_config(db, CONFIG_VIEW_QUALITY);
	sql_select_config(db, CONFIG_VIEW_MARGIN);

	sql_select_config(db, CONFIG_SECURITY_USE_PASS);
	sql_select_config(db, CONFIG_SECURITY_PASS_CODE);
	sql_select_config(db, CONFIG_SECURITY_PASS_USAGE);

	// 이동 디렉토리
	sqlite3_stmt* stmt;
	const char* sql = "SELECT no, alias, folder FROM moves ORDER BY no;";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
	{
		sql_error(db, true);
		return;
	}
	while (sqlite3_step(stmt) == SQLITE_ROW)
	{
		MoveLocation* p = g_new(MoveLocation, 1);
		p->no = sqlite3_column_int(stmt, 0);
		p->alias = g_strdup((const char*)sqlite3_column_text(stmt, 1));
		p->folder = g_strdup((const char*)sqlite3_column_text(stmt, 2));
		g_ptr_array_add(cfgs.moves, p);
	}
	movloc_reindex();

	sqlite3_close(db);
}

/**
 * @brief 설정에서 캐시 아이템을 가져옵니다.
 * @param key 설정 키
 * @param cache_only true면 캐시만, false면 DB도 조회
 * @return ConfigCacheItem 포인터
 */
static const ConfigCacheItem *config_get_item(ConfigKeys key, bool cache_only)
{
	if (key <= CONFIG_NONE || key >= CONFIG_MAX_VALUE)
		return NULL;
	if (!cache_only)
		sql_get_config(key);
	return cache_get_item(key);
}

/**
 * @brief 설정에서 문자열 값을 포인터로 가져옵니다.
 * @param name 설정 키
 * @param cache_only true면 캐시만, false면 DB도 조회
 * @return 문자열 포인터(없으면 NULL)
 */
const char *config_get_string_ptr(ConfigKeys name, bool cache_only)
{
	const ConfigCacheItem* item = config_get_item(name, cache_only);
	return item == NULL || item->type != CACHE_TYPE_STRING ? NULL : item->s;
}

/**
 * @brief 설정에서 문자열 값을 복사해 가져옵니다.
 * @param name 설정 키
 * @param value 결과 버퍼
 * @param value_size 버퍼 크기
 * @param cache_only true면 캐시만, false면 DB도 조회
 * @return 성공 시 true
 */
bool config_get_string(ConfigKeys name, char* value, size_t value_size, bool cache_only)
{
	const char* str = config_get_string_ptr(name, cache_only);
	if (str == NULL || *str == '\0')
		return false;
	g_strlcpy(value, str, value_size);
	return true;
}

/**
 * @brief 설정에서 불린 값을 가져옵니다.
 * @param name 설정 키
 * @param cache_only true면 캐시만, false면 DB도 조회
 * @return 불린 값
 */
bool config_get_bool(ConfigKeys name, bool cache_only)
{
	const ConfigCacheItem* item = config_get_item(name, cache_only);
	return item != NULL && item->type == CACHE_TYPE_BOOL ? item->b : false;
}

/**
 * @brief 설정에서 정수 값을 가져옵니다.
 * @param name 설정 키
 * @param cache_only true면 캐시만, false면 DB도 조회
 * @return 정수 값
 */
gint32 config_get_int(ConfigKeys name, bool cache_only)
{
	const ConfigCacheItem* item = config_get_item(name, cache_only);
	return item != NULL && item->type == CACHE_TYPE_INT ? item->n : 0;
}

/**
 * @brief 설정에서 긴 정수 값을 가져옵니다.
 * @param name 설정 키
 * @param cache_only true면 캐시만, false면 DB도 조회
 * @return 긴 정수 값
 */
gint64 config_get_long(ConfigKeys name, bool cache_only)
{
	const ConfigCacheItem* item = config_get_item(name, cache_only);
	return item != NULL && item->type == CACHE_TYPE_LONG ? item->l : 0;
}

/**
 * @brief 설정에 문자열 값을 저장합니다.
 * @param name 설정 키
 * @param value 저장할 문자열
 * @param cache_only true면 캐시만, false면 DB에도 저장
 */
void config_set_string(ConfigKeys name, const char* value, bool cache_only)
{
	g_return_if_fail(name > CONFIG_NONE && name < CONFIG_MAX_VALUE);
	cache_auto_set_item(name, value);
	if (!cache_only)
		sql_set_config(name);
}

/**
 * @brief 설정에 불린 값을 저장합니다.
 * @param name 설정 키
 * @param value 저장할 불린 값
 * @param cache_only true면 캐시만, false면 DB에도 저장
 */
void config_set_bool(ConfigKeys name, bool value, bool cache_only)
{
	g_return_if_fail(name > CONFIG_NONE && name < CONFIG_MAX_VALUE);
	const ConfigCacheItem item = {.b = value, .type = CACHE_TYPE_BOOL};
	cache_set_item(name, &item);
	if (!cache_only)
		sql_set_config(name);
}

/**
 * @brief 설정에 정수 값을 저장합니다.
 * @param name 설정 키
 * @param value 저장할 정수 값
 * @param cache_only true면 캐시만, false면 DB에도 저장
 */
void config_set_int(ConfigKeys name, gint32 value, bool cache_only)
{
	g_return_if_fail(name > CONFIG_NONE && name < CONFIG_MAX_VALUE);
	const ConfigCacheItem item = {.n = value, .type = CACHE_TYPE_INT};
	cache_set_item(name, &item);
	if (!cache_only)
		sql_set_config(name);
}

/**
 * @brief 설정에 긴 정수 값을 저장합니다.
 * @param name 설정 키
 * @param value 저장할 긴 정수 값
 * @param cache_only true면 캐시만, false면 DB에도 저장
 */
void config_set_long(ConfigKeys name, gint64 value, bool cache_only)
{
	g_return_if_fail(name > CONFIG_NONE && name < CONFIG_MAX_VALUE);
	const ConfigCacheItem item = {.l = value, .type = CACHE_TYPE_LONG};
	cache_set_item(name, &item);
	if (!cache_only)
		sql_set_config(name);
}

/**
 * @brief 실제 최대 페이지 캐시 크기를 바이트 단위로 반환합니다.
 * @return 최대 캐시 크기(바이트)
 */
uint64_t config_get_actual_max_page_cache(void)
{
	const ConfigCacheItem* item = cache_get_item(CONFIG_GENERAL_MAX_PAGE_CACHE);
	int mb = item ? item->n : 230; // 원래 defs에서 가져와야 하는데 귀찮다
	return (uint64_t)mb * 1024ULL * 1024ULL; // MB 단위로 변환
}

/**
 * @brief 파일 이름에 해당하는 최근 페이지 번호를 얻습니다.
 * @param filename 파일 이름
 * @return 페이지 번호(없으면 0)
 */
int recently_get_page(const char* filename)
{
	g_return_val_if_fail(filename != NULL, 0);

	sqlite3* db = sql_open();
	g_return_val_if_fail(db != NULL, 0);

	sqlite3_stmt* stmt;
	const char* sql = "SELECT page FROM recently WHERE filename = ? LIMIT 1;";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
	{
		sql_error(db, true);
		return 0;
	}

	sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_STATIC);
	const int page = (sqlite3_step(stmt) == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : 0;
	sqlite3_finalize(stmt);
	sqlite3_close(db);

	return page;
}

/**
 * @brief 파일 이름에 해당하는 최근 페이지 번호를 설정합니다.
 *        page가 0 이하이면 삭제, 1 이상이면 저장
 * @param filename 파일 이름
 * @param page 페이지 번호
 * @return 성공 시 true
 */
bool recently_set_page(const char* filename, int page)
{
	g_return_val_if_fail(filename != NULL, false);

	sqlite3* db = sql_open();
	g_return_val_if_fail(db != NULL, false);

	sqlite3_stmt* stmt;
	if (page <= 0)
	{
		// 삭제
		const char* sql = "DELETE FROM recently WHERE filename = ?;";
		if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
		{
			sql_error(db, true);
			return false;
		}
		sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_STATIC);
	}
	else
	{
		// 업데이트
		const char* sql = "INSERT OR REPLACE INTO recently (filename, page) VALUES (?, ?);";
		if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
		{
			sql_error(db, true);
			return false;
		}
		sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_STATIC);
		sqlite3_bind_int(stmt, 2, page);
	}

	bool ret = true;
	if (sqlite3_step(stmt) != SQLITE_DONE)
	{
		sql_error(db, true);
		ret = false;
	}
	sqlite3_finalize(stmt);
	sqlite3_close(db);

	return ret;
}

/**
 * @brief 책 이동 위치를 추가합니다.
 * @param alias 별칭
 * @param folder 폴더 경로
 * @return 성공 시 true
 */
bool movloc_add(const char* alias, const char* folder)
{
	if (alias == NULL || folder == NULL)
		return false;
	if (cfgs.moves->len >= 100)
	{
		g_log("CONFIG", G_LOG_LEVEL_WARNING, _("Cannot add more than 100 move locations"));
		return false;
	}
	for (guint i = 0; i < cfgs.moves->len; i++)
	{
		const MoveLocation* p = g_ptr_array_index(cfgs.moves, i);
		if (g_strcmp0(p->folder, folder) == 0)
		{
			g_log("CONFIG", G_LOG_LEVEL_WARNING, _("Move location with the same alias or folder already exists"));
			return false;
		}
	}
	MoveLocation* p = g_new(MoveLocation, 1);
	p->no = (int)cfgs.moves->len; // 현재 길이로 번호 설정
	p->alias = g_strdup(alias);
	p->folder = g_strdup(folder);
	g_ptr_array_add(cfgs.moves, p);
	return true;
}

/**
 * @brief 책 이동 위치를 수정합니다.
 * @param no 이동 위치 인덱스
 * @param alias 새 별칭
 * @param folder 새 폴더 경로
 */
void movloc_edit(int no, const char* alias, const char* folder)
{
	if (alias == NULL || folder == NULL)
		return;
	if (no < 0 || no >= (int)cfgs.moves->len)
		return;

	MoveLocation* p = g_ptr_array_index(cfgs.moves, no);
	g_free(p->alias);
	g_free(p->folder);
	p->alias = g_strdup(alias);
	p->folder = g_strdup(folder);
}

/**
 * @brief 책 이동 위치의 순번을 다시 설정합니다.
 */
void movloc_reindex(void)
{
	for (guint i = 0; i < cfgs.moves->len; i++)
	{
		MoveLocation* p = (MoveLocation*)g_ptr_array_index(cfgs.moves, i);
		p->no = (int)i; // 현재 인덱스로 번호 설정
	}
}

/**
 * @brief 책 이동 위치를 삭제합니다.
 * @param no 이동 위치 인덱스
 * @return 성공 시 true
 */
bool movloc_delete(int no)
{
	if (no < 0 || no >= (int)cfgs.moves->len)
		return false;

	g_ptr_array_remove_index(cfgs.moves, no);
	movloc_reindex();

	return true;
}

/**
 * @brief 책 이동 위치의 순서를 바꿉니다.
 * @param from 원래 인덱스
 * @param to 바꿀 인덱스
 * @return 성공 시 true
 */
bool movloc_swap(int from, int to)
{
	if (from < 0 || from >= (int)cfgs.moves->len || to < 0 || to >= (int)cfgs.moves->len || from == to)
		return false;

	MoveLocation* p1 = g_ptr_array_index(cfgs.moves, from);
	MoveLocation* p2 = g_ptr_array_index(cfgs.moves, to);

	g_ptr_array_index(cfgs.moves, from) = p2;
	g_ptr_array_index(cfgs.moves, to) = p1;

	p1->no = to; // 새 인덱스로 번호 설정
	p2->no = from; // 새 인덱스로 번호 설정

	movloc_reindex();

	return true;
}

/**
 * @brief 책 이동 위치 배열을 반환합니다.
 * @return GPtrArray 포인터
 */
GPtrArray *movloc_get_all_ptr(void)
{
	return cfgs.moves;
}

/**
 * @brief 책 이동 위치를 DB에 저장(커밋)합니다.
 */
void movloc_commit(void)
{
	sqlite3* db = sql_open();
	g_return_if_fail(db != NULL);

	// 기존 데이터 삭제
	if (!sql_exec_stmt(db, "DELETE FROM moves;"))
	{
		sqlite3_close(db);
		return;
	}

	// 현재 이동 위치를 DB에 저장
	for (guint i = 0; i < cfgs.moves->len; i++)
	{
		MoveLocation* p = (MoveLocation*)g_ptr_array_index(cfgs.moves, i);
		const char* sql = "INSERT INTO moves (no, alias, folder) VALUES (?, ?, ?);";
		sqlite3_stmt* stmt;
		if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
		{
			sql_error(db, true);
			return;
		}
		sqlite3_bind_int(stmt, 1, i);
		sqlite3_bind_text(stmt, 2, p->alias, -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 3, p->folder, -1, SQLITE_STATIC);

		if (sqlite3_step(stmt) != SQLITE_DONE)
		{
			sql_error(db, true);
			sqlite3_finalize(stmt);
			return;
		}
		sqlite3_finalize(stmt);
	}

	sqlite3_close(db);
}

/**
 * @brief 자연스러운 문자열 비교 함수(숫자/문자 구분, OS별 처리)
 * @param pa 문자열 포인터 a
 * @param pb 문자열 포인터 b
 * @return 비교 결과(-1, 0, 1)
 */
static gint compare_natural_filename(const void* pa, const void* pb)
{
	const char* a = *(const char**)pa; // NOLINT(clang-diagnostic-cast-qual)
	const char* b = *(const char**)pb; // NOLINT(clang-diagnostic-cast-qual)

#ifndef _WIN32
	// 윈도우가 아닌 OS의 비교. UTF-8, 숫자 인식
	const char *pa_ptr = a, *pb_ptr = b;
	while (*pa_ptr && *pb_ptr)
	{
		// 숫자 구간 비교
		if (g_ascii_isdigit(*pa_ptr) && g_ascii_isdigit(*pb_ptr))
		{
			// 숫자 파싱
			char* end_a;
			char* end_b;
			const gint64 va = g_ascii_strtoll(pa_ptr, &end_a, 10);
			const gint64 vb = g_ascii_strtoll(pb_ptr, &end_b, 10);
			if (va != vb)
				return (va > vb) - (va < vb);
			pa_ptr = end_a;
			pb_ptr = end_b;
		}
		else
		{
			// 문자 구간 비교 (UTF-8 단위)
			const gunichar ca = g_utf8_get_char(pa_ptr);
			const gunichar cb = g_utf8_get_char(pb_ptr);
			if (ca != cb)
				return (ca > cb) - (ca < cb);
			pa_ptr = g_utf8_next_char(pa_ptr);
			pb_ptr = g_utf8_next_char(pb_ptr);
		}
	}
	// 남은 길이 비교
	return g_utf8_collate(a, b);
#else
	// 윈도우 전용, 탐색기와 같은 정렬 방식
	const char* pa_ptr = a, * pb_ptr = b;
	while (*pa_ptr && *pb_ptr)
	{
		if (g_ascii_isdigit(*pa_ptr) && g_ascii_isdigit(*pb_ptr))
		{
			char* end_a;
			char* end_b;
			const gint64 va = g_ascii_strtoll(pa_ptr, &end_a, 10);
			const gint64 vb = g_ascii_strtoll(pb_ptr, &end_b, 10);
			if (va != vb)
				return (va > vb) - (va < vb);
			pa_ptr = end_a;
			pb_ptr = end_b;
		}
		else
		{
			break; // 숫자 구간이 아니면 전체 문자열 비교로 넘어감
		}
	}

	// UTF-8 → UTF-16 변환
	const int wlen_a = MultiByteToWideChar(CP_UTF8, 0, a, -1, NULL, 0);
	const int wlen_b = MultiByteToWideChar(CP_UTF8, 0, b, -1, NULL, 0);
	WCHAR* wa = g_malloc(sizeof(WCHAR) * wlen_a);
	WCHAR* wb = g_malloc(sizeof(WCHAR) * wlen_b);
	MultiByteToWideChar(CP_UTF8, 0, a, -1, wa, wlen_a);
	MultiByteToWideChar(CP_UTF8, 0, b, -1, wb, wlen_b);

	// 윈도우 탐색기와 유사하게: 숫자 인식, 대소문자 구분 없음, 로케일 기본
	const int result = CompareStringEx(
		LOCALE_NAME_USER_DEFAULT,
		NORM_IGNORECASE | SORT_DIGITSASNUMBERS,
		wa, -1,
		wb, -1,
		NULL, NULL, 0);

	g_free(wa);
	g_free(wb);

	// CompareStringEx는 1(less), 2(equal), 3(greater) 반환
	if (result == CSTR_LESS_THAN) return -1;
	if (result == CSTR_GREATER_THAN) return 1;
	return 0;
#endif
}

/**
 * @brief 근처 파일 목록을 빌드합니다.
 * @param dir 디렉토리 경로
 * @param compare 확장자 비교 함수
 * @return 성공 시 true
 */
bool nears_build(const char* dir, NearExtentionCompare compare)
{
	if (dir == NULL || compare == NULL)
		return false; // 디렉토리나 비교 함수가 NULL이면 실패

	if (cfgs.near_compare == compare && g_strcmp0(cfgs.near_dir, dir) == 0)
		return true; // 같은 디렉토리 & 비교 함수가 같으면 아무것도 안함

	GDir* gd = g_dir_open(dir, 0, NULL);
	if (gd == NULL)
		return false; // 디렉토리 열기 실패

	if (cfgs.near_dir)
		g_free(cfgs.near_dir);
	cfgs.near_dir = g_strdup(dir);
	cfgs.near_compare = compare;
	g_ptr_array_set_size(cfgs.nears, 0);

	const char* name;
	while ((name = g_dir_read_name(gd)) != NULL)
	{
		if (name[0] == '.' || name[0] == '\0') // 숨김 파일이나 빈 이름은 무시
			continue;
		char* fullpath = g_build_filename(dir, name, NULL);
		if (g_file_test(fullpath, G_FILE_TEST_IS_REGULAR) && compare(name))
			g_ptr_array_add(cfgs.nears, fullpath);
		else
			g_free(fullpath);
	}

	g_dir_close(gd);

	// 자연스러운 정렬 적용
	g_ptr_array_sort(cfgs.nears, compare_natural_filename);

	return true;
}

/**
 * @brief 지정한 파일이 없으면 근처 파일을 빌드합니다.
 * @param dir 디렉토리 경로
 * @param compare 확장자 비교 함수
 * @param fullpath 확인할 파일 경로
 * @return 성공 시 true
 */
bool nears_build_if(const char* dir, NearExtentionCompare compare, const char* fullpath)
{
	if (fullpath && *fullpath != '\0')
	{
		for (guint i = 0; i < cfgs.nears->len; ++i)
		{
			const char* item = g_ptr_array_index(cfgs.nears, i);
			if (g_strcmp0(item, fullpath) == 0)
				return true; // 이미 근처 파일에 있으면 아무것도 안함
		}
	}

	cfgs.near_compare = NULL;	// 현재 비교 함수 초기화 -> 강제로 빌드
	return nears_build(dir, compare);
}

/**
 * @brief 지정 파일의 앞쪽 근처 파일을 얻습니다.
 * @param fullpath 기준 파일 경로
 * @return 이전 파일 경로(없으면 NULL)
 */
const char *nears_get_prev(const char* fullpath)
{
	g_return_val_if_fail(fullpath != NULL, NULL);
	for (guint i = 0; i < cfgs.nears->len; ++i)
	{
		const char* near_file = g_ptr_array_index(cfgs.nears, i);
		if (g_strcmp0(near_file, fullpath) != 0)
			continue; // 자기 자신이 아니면 계속
		if (i == 0)
			return NULL; // 첫번째면 이전이 없음
		return g_ptr_array_index(cfgs.nears, i - 1);
	}
	return NULL;
}

/**
 * @brief 지정 파일의 뒤쪽 근처 파일을 얻습니다.
 * @param fullpath 기준 파일 경로
 * @return 다음 파일 경로(없으면 NULL)
 */
const char *nears_get_next(const char* fullpath)
{
	g_return_val_if_fail(fullpath != NULL, NULL);
	for (guint i = 0; i < cfgs.nears->len; ++i)
	{
		const char* near_file = g_ptr_array_index(cfgs.nears, i);
		if (g_strcmp0(near_file, fullpath) != 0)
			continue; // 자기 자신이 아니면 계속
		if (i + 1 >= cfgs.nears->len)
			return NULL; // 마지막이면 다음이 없음
		return g_ptr_array_index(cfgs.nears, i + 1);
	}
	return NULL;
}

/**
 * @brief 지정 파일을 제외한 임의의 근처 파일을 얻습니다.
 * @param fullpath 기준 파일 경로
 * @return 임의의 파일 경로(없으면 NULL)
 */
const char *nears_get_random(const char* fullpath)
{
	g_return_val_if_fail(fullpath != NULL, NULL);
	if (cfgs.nears->len <= 1) // 자기 자신만 있거나 근처 파일이 없으면
		return NULL;
	guint index = g_random_int_range(0, (gint)cfgs.nears->len);
	while (true)
	{
		const char* near_file = g_ptr_array_index(cfgs.nears, index);
		if (g_strcmp0(near_file, fullpath) != 0) // 자기 자신이 아니면 반환
			return near_file;
		index = (index + 1) % cfgs.nears->len; // 다음 인덱스로 이동
	}
}

/**
 * @brief 지정 파일을 삭제하고 근처 파일을 얻습니다.
 * @param fullpath 기준 파일 경로
 * @return 남은 파일 경로(없으면 NULL)
 */
const char *nears_get_for_remove(const char* fullpath)
{
	g_return_val_if_fail(fullpath != NULL, NULL);
	if (cfgs.nears->len == 1)
	{
		g_ptr_array_set_size(cfgs.nears, 0); // 자기 자신만 있으면 비움
		return NULL;
	}
	for (guint i = 0; i < cfgs.nears->len; ++i)
	{
		const char* near_file = g_ptr_array_index(cfgs.nears, i);
		if (g_strcmp0(near_file, fullpath) != 0)
			continue; // 자기 자신이 아니면 계속
		const char* ret;
		if (i == 0)
			ret = g_ptr_array_index(cfgs.nears, 1); // 첫번째면 두번째를 넘김
		else if (i + 1 < cfgs.nears->len)
			ret = g_ptr_array_index(cfgs.nears, i + 1); // 다음 파일을 넘김
		else
			ret = g_ptr_array_index(cfgs.nears, i - 1); // 마지막이면 이전 파일을 넘김
		g_ptr_array_remove_index(cfgs.nears, i); // 자기 자신을 제거
		return ret;
	}
	return NULL;
}

/**
 * @brief 지정 파일을 삭제하고 새 항목을 추가하며 근처 파일을 얻습니다.
 * @param fullpath 기준 파일 경로
 * @param new_filename 새 파일 이름
 * @return 남은 파일 경로(없으면 NULL)
 */
const char *nears_get_for_rename(const char* fullpath, const char* new_filename)
{
	g_return_val_if_fail(fullpath != NULL && new_filename != NULL, NULL);
	// 먼저 새 파일 이름을 추가하고
	char* dup_filename = g_strdup(new_filename);
	g_ptr_array_add(cfgs.nears, dup_filename);
	// 이제 항목이 2개일 테니 루프로 확인
	for (guint i = 0; i < cfgs.nears->len; ++i)
	{
		const char* near_file = g_ptr_array_index(cfgs.nears, i);
		if (g_strcmp0(near_file, fullpath) != 0)
			continue; // 자기 자신이 아니면 계속
		const char* ret;
		if (i == 0)
			ret = g_ptr_array_index(cfgs.nears, 1); // 첫번째면 두번째를 넘김
		else if (i + 1 < cfgs.nears->len)
			ret = g_ptr_array_index(cfgs.nears, i + 1); // 다음 파일을 넘김
		else
			ret = g_ptr_array_index(cfgs.nears, i - 1); // 마지막이면 이전 파일을 넘김
		g_ptr_array_remove_index(cfgs.nears, i); // 자기 자신을 제거
		return ret;
	}
	// 여기까지.. 온다고? 그냥 새 파일 이름을 반환
	return dup_filename;
}

/**
 * @brief 문자열에서 modifier(Shift, Ctrl 등) 파싱
 * @param name modifier 이름
 * @return GdkModifierType 값
 */
static GdkModifierType parse_modifier(const char* name)
{
	if (g_ascii_strcasecmp(name, "Shift") == 0)
		return GDK_SHIFT_MASK;
	if (g_ascii_strcasecmp(name, "Control") == 0 || g_ascii_strcasecmp(name, "Ctrl") == 0)
		return GDK_CONTROL_MASK;
	if (g_ascii_strcasecmp(name, "Alt") == 0)
		return GDK_ALT_MASK;
	if (g_ascii_strcasecmp(name, "Super") == 0 || g_ascii_strcasecmp(name, "Win") == 0)
		return GDK_SUPER_MASK;
	if (g_ascii_strcasecmp(name, "Meta") == 0)
		return GDK_META_MASK;
	if (g_ascii_strcasecmp(name, "Hyper") == 0)
		return GDK_HYPER_MASK;
	return 0;
}

/**
 * @brief 단축키 문자열을 내부 값으로 변환
 * @param alias 단축키 문자열
 * @return 변환된 값(gint64 포인터, 실패 시 NULL)
 */
static gint64 *convert_shortcut(const char* alias)
{
	if (!alias || !*alias)
		return NULL;

	GdkModifierType mods = 0;
	const char* p = alias;
	char keyname[64] = {0};
	size_t keyname_len = 0;

	// 파싱: <Modifier>들 먼저
	while (*p)
	{
		if (*p == '<')
		{
			const char* end = strchr(p, '>');
			if (!end) break;
			const size_t len = end - (p + 1);
			if (len > 0 && len < 32)
			{
				char modname[32];
				memcpy(modname, p + 1, len);
				modname[len] = 0;
				mods |= parse_modifier(modname);
			}
			p = end + 1;
		}
		else
		{
			// 키 이름 시작
			break;
		}
	}

	// 남은 부분이 키 이름
	while (*p && keyname_len < sizeof(keyname) - 1)
		keyname[keyname_len++] = *p++;
	keyname[keyname_len] = 0;

	// 키값 변환
	const guint keyval = gdk_keyval_from_name(keyname);
	if (keyval == 0)
		return NULL;

	gint64* result = g_new(gint64, 1);
	*result = ((gint64)mods << 32) | keyval;
	return result;
}

/**
 * @brief 단축키를 등록합니다. (기본값 및 DB에서 읽기)
 */
void shortcut_register(void)
{
	// 먼저 기본값을 넣자고
	for (const ShortcutDefinition* sc = shortcut_defs; sc->action; sc++)
	{
		gint64* key = convert_shortcut(sc->alias);
		if (key)
			g_hash_table_insert(cfgs.shortcut, key, g_strdup(sc->action));
#if defined(_DEBUG) && false
		if (key == NULL)
			g_log("SHORTCUT", G_LOG_LEVEL_ERROR, "%s -> %s", sc->action, sc->alias);
		else
		{
			const guint key_val = (guint)(*key & 0xFFFFFFFF); // 하위 32비트가 키값
			const guint key_state = (guint)(*key >> 32); // 상위 32비트가 상태
			g_log("SHORTCUT", G_LOG_LEVEL_DEBUG, "%s -> %s (%X, %X)",
				sc->action, sc->alias, key_state, key_val);
		}
#endif
	}

	// SQL에서 단축키를 가져와서 넣기
	sqlite3* db = sql_open();
	g_return_if_fail(db != NULL);

	sqlite3_stmt* stmt;
	const char* sql = "SELECT action, alias FROM shortcuts;";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
	{
		sql_error(db, false);
		return;
	}

	while (sqlite3_step(stmt) == SQLITE_ROW)
	{
		const char* action = (const char*)sqlite3_column_text(stmt, 0);
		const char* alias = (const char*)sqlite3_column_text(stmt, 1);

		if (action && alias)
		{
			gint64* key = convert_shortcut(alias);
			if (key)
				g_hash_table_insert(cfgs.shortcut, key, g_strdup(action));
		}
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);
}

/**
 * @brief 키 입력값으로 단축키 명령을 조회합니다.
 * @param key_val 키 값
 * @param key_state modifier 상태
 * @return 단축키 명령 문자열(없으면 NULL)
 */
const char *shortcut_lookup(const guint key_val, const GdkModifierType key_state)
{
	const gint64 key = ((gint64)key_state << 32) | key_val; // 상위 32비트에 상태, 하위 32비트에 키값
	const char* action = g_hash_table_lookup(cfgs.shortcut, &key);
	return action;
}

/**
 * @brief 언어 문자열을 조회합니다.
 * @param key 언어 키
 * @return 번역 문자열(없으면 key 자체 반환)
 */
const char *locale_lookup(const char* key)
{
	const char* lookup = g_hash_table_lookup(cfgs.lang, key);
	return lookup ? lookup : key;
}

/**
 * @note
 * - 이 파일은 프로그램의 환경설정, 캐시, 단축키, 최근 파일, 이동 위치, 언어 등
 *   다양한 설정 및 데이터베이스 연동을 담당합니다.
 * - 각 함수는 메모리 관리, DB 연동, 캐시 동기화에 주의해야 합니다.
 * - 구조체, 함수, 주요 블록에 Doxygen 스타일 주석을 추가하였습니다.
 */
