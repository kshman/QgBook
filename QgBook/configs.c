#include "pch.h"
#include <time.h>
#include "configs.h"

// 설정 자료
static struct Configs
{
	GHashTable* lang;
	GHashTable* cache;
	char* app_path;
	char* cfg_path;
	time_t launched;
}
cfgs =
{
	.lang = NULL,
	.app_path = NULL,
	.cfg_path = NULL,
};

// 설정 캐시 아이템의 변수 타입
typedef enum ConfigCacheType
{
	CACHE_TYPE_UNKNOWN,    // 알 수 없는 타입
	CACHE_TYPE_INT,        // 정수형
	CACHE_TYPE_LONG,       // 긴 정수형
	CACHE_TYPE_BOOL,       // 불린형
	CACHE_TYPE_DOUBLE,     // 실수형
	CACHE_TYPE_STRING,     // 문자열
} ConfigCacheType;

// 설정 캐시 아이템
typedef struct ConfigCacheItem
{
	union
	{
		gint32 n;          // 정수형
		gint64 l;          // 긴 정수형
		bool b;            // 불린형
		double d;          // 실수형
		char* s;           // 문자열
	};
	ConfigCacheType type;  // 타입
} ConfigCacheItem;

// 설정 이름을 정의합니다. 이 배열은 설정의 순서를 정의하며, 인덱스는 ConfigKeys입니다.
static struct ConfigKeysDefinition
{
	const char* name;
	const char* value;
	ConfigCacheType type;
}
s_config_defs[CONFIG_MAX_VALUE] =
{
	{ "", "", CACHE_TYPE_UNKNOWN },
	// 실행
	{ "run_count", "0", CACHE_TYPE_LONG },
	{ "run_duration", "0", CACHE_TYPE_DOUBLE },
	// 윈도우
	{ "window_width", "600", CACHE_TYPE_INT },
	{ "window_height", "400", CACHE_TYPE_INT },
	// 일반
	{ "general_run_once", "1", CACHE_TYPE_BOOL },
	{ "general_esc_exit", "1", CACHE_TYPE_BOOL },
	{ "general_confirm_delete", "1", CACHE_TYPE_BOOL },
	{ "general_max_page_cache", "230", CACHE_TYPE_INT },
	{ "general_external_run", "", CACHE_TYPE_STRING },
	{ "general_reload_after_external", "1", CACHE_TYPE_BOOL },
	// 마우스
	{ "mouse_double_click_fullscreen", "0", CACHE_TYPE_BOOL },
	{ "mouse_click_paging", "0", CACHE_TYPE_BOOL },
	// 보기
	{ "view_zoom", "1", CACHE_TYPE_BOOL },
	{ "view_mode", "0", CACHE_TYPE_INT },
	{ "view_quality", "1", CACHE_TYPE_INT },
	// 보안
	{ "security_use_pass", "0", CACHE_TYPE_BOOL },
	{ "security_pass_code", "", CACHE_TYPE_STRING },
	{ "security_pass_usage", "", CACHE_TYPE_STRING },
	// 파일
	{ "file_last_directory", "", CACHE_TYPE_STRING },
	{ "file_last_file", "", CACHE_TYPE_STRING },
	{ "file_remember", "", CACHE_TYPE_STRING },
};

// 캐시 아이템 해제
static void cache_item_free(gpointer ptr)
{
	ConfigCacheItem* item = ptr;
	g_return_if_fail(item != NULL);
	if (item->type == CACHE_TYPE_STRING && item->s)
		g_free(item->s);
	g_free(item);
}

// 캐시 얻기
// key에 대한 범위 검사를 하지 않습니다.
static ConfigCacheItem* cache_get_item(const ConfigKeys key)
{
	const struct ConfigKeysDefinition* def = &s_config_defs[key];
	return g_hash_table_lookup(cfgs.cache, def->name);
}

// 캐시 설정
// key에 대한 범위 검사를 하지 않습니다.
// 인수인 item은 할당된게 아니므로 여기서 할당하고 복제합니다.
static void cache_set_item(const ConfigKeys key, const ConfigCacheItem* item)
{
	ConfigCacheItem* new_item = g_new(ConfigCacheItem, 1);
	memcpy(new_item, item, sizeof(ConfigCacheItem));

	const struct ConfigKeysDefinition* def = &s_config_defs[key];
	g_hash_table_insert(cfgs.cache, g_strdup(def->name), new_item);
}

// 타입을 자동으로 알아내서 캐시 얻기
static const char* cache_auto_get_item(const ConfigKeys key, char* value, size_t value_size)
{
	const struct ConfigKeysDefinition* def = &s_config_defs[key];
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

// 타입을 자동으로 알아내서 캐시 설정
static void cache_auto_set_item(const ConfigKeys key, const char* value)
{
	const struct ConfigKeysDefinition* def = &s_config_defs[key];
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

// SQL 오류 메시지를 얻어서 출력하고 디비를 닫습니다.
static void sql_error(sqlite3* db, bool close_db)
{
	g_return_if_fail(db != NULL);
	const char* err_msg = sqlite3_errmsg(db);
	if (err_msg)
		g_log("SQL", G_LOG_LEVEL_ERROR, "%s", err_msg);
	if (close_db)
		sqlite3_close(db);
}

// SQL 오류 메시지를 출력하고 메모리 해제
static void sql_free_error(char* err_msg)
{
	if (err_msg)
	{
		g_log("SQL", G_LOG_LEVEL_ERROR, "%s", err_msg);
		sqlite3_free(err_msg);
	}
}

// SQL을 엽니다
static sqlite3* sql_open(void)
{
	sqlite3* db;
	if (sqlite3_open(cfgs.cfg_path, &db) == SQLITE_OK)
		return db;
	if (db != NULL)
		sql_error(db, true);
	return NULL;
}

// SQL 문구 실행
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

// SQL Configs 테이블에서 Select해서 캐시로 넣기. 캐시에 값이 없으면 기본값 넣음
static bool sql_select_config(sqlite3* db, const ConfigKeys key)
{
	const struct ConfigKeysDefinition* def = &s_config_defs[key];

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

// SQL Configs 테이블로 Insert or Replace하는데 값을 지정
static bool sql_into_config_value(sqlite3* db, const ConfigKeys key, const char* value)
{
	const struct ConfigKeysDefinition* def = &s_config_defs[key];

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

// SQL Configs 테이블로 캐시에서 값을 가져와 Insert or Replace
static bool sql_into_config(sqlite3* db, const ConfigKeys key)
{
	char sz[128];
	const char* psz = cache_auto_get_item(key, sz, sizeof(sz));
	return sql_into_config_value(db, key, psz ? psz : s_config_defs[key].value);
}

// SQL Configs 테이블에서 가져와 캐시에 넣기
static bool sql_get_config(const ConfigKeys key)
{
	sqlite3* db = sql_open();
	g_return_val_if_fail(db != NULL, false);

	bool ret = sql_select_config(db, key);

	sqlite3_close(db);
	return ret;
}

// SQL Configs 테이블에 캐시에 있는 값 넣기
static bool sql_set_config(const ConfigKeys key)
{
	sqlite3* db = sql_open();
	g_return_val_if_fail(db != NULL, false);

	bool ret = sql_into_config(db, key);

	sqlite3_close(db);
	return ret;
}

// 문자열 스트립
static char* string_strip(const char* s)
{
	if (!s) return g_strdup("");
	while (g_ascii_isspace(*s)) s++;
	const char* end = s + strlen(s);
	while (end > s && g_ascii_isspace(*(end - 1))) end--;
	return g_strndup(s, end - s);
}

// 언어 처리 데이터 분석
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
			char* stripped = string_strip(line);

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
						value = string_strip(eq + 1);
				}
			}
			else
			{
				char* eq = strchr(s, '=');
				if (eq)
				{
					*eq = 0;
					key = string_strip(s);
					value = string_strip(eq + 1);
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

// 설정을 초기화합니다.
bool configs_init(void)
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
		!sql_exec_stmt(db, "CREATE TABLE IF NOT EXISTS moves (folder TEXT PRIMARY KEY, alias TEXT);") ||
		!sql_exec_stmt(db, "CREATE TABLE IF NOT EXISTS recently (filename TEXT PRIMARY KEY, page INTEGER);") ||
		!sql_exec_stmt(db, "CREATE TABLE IF NOT EXISTS bookmarks (id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT, page INTEGER);"))
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

	sqlite3_close(db);
	return true;
}

// 설정을 정리힙니다.
void configs_dispose(void)
{
	sqlite3* db = sql_open();
	if (db != NULL)
	{
		time_t now = time(NULL);
		ConfigCacheItem* run_duration = cache_get_item(CONFIG_RUN_DURATION);
		run_duration->d += difftime(now, cfgs.launched);

		sql_into_config(db, CONFIG_WINDOW_WIDTH);
		sql_into_config(db, CONFIG_WINDOW_HEIGHT);
		sql_into_config(db, CONFIG_RUN_DURATION);

		sqlite3_close(db);
	}

	if (cfgs.cfg_path)
		g_free(cfgs.cfg_path);
	if (cfgs.app_path)
		g_free(cfgs.app_path);
	if (cfgs.cache)
		g_hash_table_destroy(cfgs.cache);
	if (cfgs.lang)
		g_hash_table_destroy(cfgs.lang);
}

// 설정에서 쓸 값을 캐시합니다.
void configs_load_cache(void)
{
	sqlite3* db = sql_open();
	g_return_if_fail(db != NULL);

	// 캐시
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

	sql_select_config(db, CONFIG_SECURITY_USE_PASS);
	sql_select_config(db, CONFIG_SECURITY_PASS_CODE);
	sql_select_config(db, CONFIG_SECURITY_PASS_USAGE);

	// 이동 디렉토리 여기서 해야하나/

	sqlite3_close(db);
}

// 언어 찾아보기
const char* configs_lookup_lang(const char* key)
{
	const char* lookup = g_hash_table_lookup(cfgs.lang, key);
	return lookup ? lookup : key;
}

// 설정에서 아이템을 가져옵니다
static const ConfigCacheItem* config_get_item(ConfigKeys key, bool cache_only)
{
	if (key <= CONFIG_NONE || key >= CONFIG_MAX_VALUE)
		return NULL;
	if (!cache_only)
		sql_get_config(key);
	return cache_get_item(key);
}

// 설정에서 문자열을 가져옵니다.
bool configs_get_string(ConfigKeys name, char* value, size_t value_size, bool cache_only)
{
	const ConfigCacheItem* item = config_get_item(name, cache_only);
	if (item == NULL || item->type != CACHE_TYPE_STRING)
		return false;
	g_strlcpy(value, item->s, value_size);
	return true;
}

// 설정에서 불린을 가져옵니다.
bool configs_get_bool(ConfigKeys name, bool cache_only)
{
	const ConfigCacheItem* item = config_get_item(name, cache_only);
	return item != NULL && item->type == CACHE_TYPE_BOOL ? item->b : false;
}

// 설정에서 정수를 가져옵니다.
gint32 configs_get_int(ConfigKeys name, bool cache_only)
{
	const ConfigCacheItem* item = config_get_item(name, cache_only);
	return item != NULL && item->type == CACHE_TYPE_INT ? item->n : 0;
}

// 설정에서 긴 정수를 가져옵니다.
gint64 configs_get_long(ConfigKeys name, bool cache_only)
{
	const ConfigCacheItem* item = config_get_item(name, cache_only);
	return item != NULL && item->type == CACHE_TYPE_LONG ? item->l : 0;
}

// 설정으로 문자열을 넣습니다
void configs_set_string(ConfigKeys name, const char* value, bool cache_only)
{
	g_return_if_fail(name > CONFIG_NONE && name < CONFIG_MAX_VALUE);
	cache_auto_set_item(name, value);
	if (!cache_only)
		sql_set_config(name);
}

// 설정으로 불린을 넣습니다.
void configs_set_bool(ConfigKeys name, bool value, bool cache_only)
{
	g_return_if_fail(name > CONFIG_NONE && name < CONFIG_MAX_VALUE);
	const ConfigCacheItem item = { .b = value, .type = CACHE_TYPE_BOOL };
	cache_set_item(name, &item);
	if (!cache_only)
		sql_set_config(name);
}

// 설정으로 정수를 넣습니다.
void configs_set_int(ConfigKeys name, gint32 value, bool cache_only)
{
	g_return_if_fail(name > CONFIG_NONE && name < CONFIG_MAX_VALUE);
	const ConfigCacheItem item = { .n = value, .type = CACHE_TYPE_INT };
	cache_set_item(name, &item);
	if (!cache_only)
		sql_set_config(name);
}

// 설정으로 긴 정수를 넣습니다.
void configs_set_long(ConfigKeys name, gint64 value, bool cache_only)
{
	g_return_if_fail(name > CONFIG_NONE && name < CONFIG_MAX_VALUE);
	const ConfigCacheItem item = { .l = value, .type = CACHE_TYPE_LONG };
	cache_set_item(name, &item);
	if (!cache_only)
		sql_set_config(name);
}

// 책 이동 위치를 얻습니다. 반환값은 configs_free_moves으로 해제해야 합니다.
ConfigMove* configs_get_moves(int* ret_count)
{
	g_return_val_if_fail(ret_count != NULL, NULL);

	sqlite3* db = sql_open();
	g_return_val_if_fail(db != NULL, NULL);

	sqlite3_stmt* stmt;
	const char* sql = "SELECT folder, alias FROM moves;";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
	{
		sql_error(db, true);
		*ret_count = 0;
		return NULL;
	}

	GPtrArray* moves = g_ptr_array_new_with_free_func(g_free);
	while (sqlite3_step(stmt) == SQLITE_ROW)
	{
		const char* folder = (const char*)sqlite3_column_text(stmt, 0);
		const char* alias = (const char*)sqlite3_column_text(stmt, 1);

		ConfigMove* move = g_new(ConfigMove, 1);
		move->folder = g_strdup(folder ? folder : "");
		move->alias = g_strdup(alias ? alias : "");
		g_ptr_array_add(moves, move);
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);

	if (moves->len == 0)
	{
		g_ptr_array_free(moves, TRUE);
		*ret_count = 0;
		return NULL;
	}

	ConfigMove* result = (ConfigMove*)g_ptr_array_free(moves, FALSE);
	if (ret_count)
		*ret_count = (int)moves->len;
	return result;
}

// 책 이동 위치를 추가하거나 바꿉니다.
bool configs_set_move(const char* folder, const char* alias)
{
	g_return_val_if_fail(folder != NULL && alias != NULL, false);

	sqlite3* db = sql_open();
	g_return_val_if_fail(db != NULL, false);

	sqlite3_stmt* stmt;
	const char* sql = "INSERT OR REPLACE INTO moves (folder, alias) VALUES (?, ?);";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
	{
		sql_error(db, true);
		return false;
	}

	sqlite3_bind_text(stmt, 1, folder, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, alias, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) != SQLITE_DONE)
	{
		sqlite3_finalize(stmt);
		sql_error(db, true);
		return false;
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	return true;
}

// 책 이동 위치를 삭제합니다.
bool configs_delete_move(const char* folder)
{
	g_return_val_if_fail(folder != NULL, false);

	sqlite3* db = sql_open();
	g_return_val_if_fail(db != NULL, false);

	sqlite3_stmt* stmt;
	const char* sql = "DELETE FROM moves WHERE folder = ?;";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
	{
		sql_error(db, true);
		return false;
	}

	sqlite3_bind_text(stmt, 1, folder, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) != SQLITE_DONE)
	{
		sqlite3_finalize(stmt);
		sql_error(db, true);
		return false;
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	return true;
}

// 책 이동 위치를 해제 합니다. configs_get_moves 함수로 얻은 값을 넣습니다.
void configs_free_moves(ConfigMove* moves, int count)
{
	if (!moves || count <= 0)
		return;
	for (int i = 0; i < count; ++i)
	{
		g_free(moves[i].folder);
		g_free(moves[i].alias);
	}
	g_free(moves);
}

// 실제 최대 캐시 크기를 얻습니다.
uint64_t configs_get_actual_max_page_cache(void)
{
	const ConfigCacheItem* item = cache_get_item(CONFIG_GENERAL_MAX_PAGE_CACHE);
	int mb = item ? item->n : 230; // 원래 defs에서 가져와야 하는데 귀찮다
	return (uint64_t)mb * 1024ULL * 1024ULL; // MB 단위로 변환
}
