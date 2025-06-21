#include "pch.h"
#include <time.h>
#include "configs.h"
#include "doumi.h"

extern ConfigDefinition config_defs[CONFIG_MAX_VALUE];
extern ShortcutDefinition shortcut_defs[];

// 설정 자료
static struct Configs
{
	time_t launched;

	char* app_path;
	char* cfg_path;

	GHashTable* lang;
	GHashTable* shortcut;
	GHashTable* cache;
	GPtrArray* moves;

	GPtrArray* nears;
	NearExtentionCompare near_compare;
	char* near_dir;
} cfgs =
{
	.app_path = NULL,
	.cfg_path = NULL,
};

// 설정 캐시 아이템
typedef struct ConfigCacheItem
{
	union
	{
		gint32 n; // 정수형
		gint64 l; // 긴 정수형
		bool b; // 불린형
		double d; // 실수형
		char* s; // 문자열
	};

	CacheType type; // 타입
} ConfigCacheItem;

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
static ConfigCacheItem *cache_get_item(const ConfigKeys key)
{
	const struct ConfigDefinition* def = &config_defs[key];
	return g_hash_table_lookup(cfgs.cache, def->name);
}

// 캐시 설정
// key에 대한 범위 검사를 하지 않습니다.
// 인수인 item은 할당된게 아니므로 여기서 할당하고 복제합니다.
static void cache_set_item(const ConfigKeys key, const ConfigCacheItem* item)
{
	ConfigCacheItem* new_item = g_new(ConfigCacheItem, 1);
	memcpy(new_item, item, sizeof(ConfigCacheItem));

	const struct ConfigDefinition* def = &config_defs[key];
	g_hash_table_insert(cfgs.cache, g_strdup(def->name), new_item);
}

// 타입을 자동으로 알아내서 캐시 얻기
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

// 타입을 자동으로 알아내서 캐시 설정
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
static sqlite3 *sql_open(void)
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

// SQL Configs 테이블로 Insert or Replace하는데 값을 지정
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

// SQL Configs 테이블로 캐시에서 값을 가져와 Insert or Replace
static bool sql_into_config(sqlite3* db, const ConfigKeys key)
{
	char sz[128];
	const char* psz = cache_auto_get_item(key, sz, sizeof(sz));
	return sql_into_config_value(db, key, psz ? psz : config_defs[key].value);
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

// move 삭제
static void move_loc_free(gpointer ptr)
{
	MoveLocation* p = ptr;
	g_free(p->alias);
	g_free(p->folder);
	g_free(p);
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

// 설정을 초기화합니다.
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
		!sql_exec_stmt(
				db,
				"CREATE TABLE IF NOT EXISTS bookmarks (id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT, page INTEGER);")
		||
		!sql_exec_stmt(
				db,
				"CREATE TABLE IF NOT EXISTS shortcuts (id INTEGER PRIMARY KEY AUTOINCREMENT, action TEXT, alias TEXT);"))
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

// 설정을 정리힙니다.
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

// 설정에서 쓸 값을 캐시합니다.
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

// 설정에서 아이템을 가져옵니다
static const ConfigCacheItem *config_get_item(ConfigKeys key, bool cache_only)
{
	if (key <= CONFIG_NONE || key >= CONFIG_MAX_VALUE)
		return NULL;
	if (!cache_only)
		sql_get_config(key);
	return cache_get_item(key);
}

// 설정에서 문자열을 가져오는데, 포인터로 반환합니다.
const char *config_get_string_ptr(ConfigKeys name, bool cache_only)
{
	const ConfigCacheItem* item = config_get_item(name, cache_only);
	return item == NULL || item->type != CACHE_TYPE_STRING ? NULL : item->s;
}

// 설정에서 문자열을 가져옵니다.
bool config_get_string(ConfigKeys name, char* value, size_t value_size, bool cache_only)
{
	const char* str = config_get_string_ptr(name, cache_only);
	if (str == NULL || *str == '\0')
		return false;
	g_strlcpy(value, str, value_size);
	return true;
}

// 설정에서 불린을 가져옵니다.
bool config_get_bool(ConfigKeys name, bool cache_only)
{
	const ConfigCacheItem* item = config_get_item(name, cache_only);
	return item != NULL && item->type == CACHE_TYPE_BOOL ? item->b : false;
}

// 설정에서 정수를 가져옵니다.
gint32 config_get_int(ConfigKeys name, bool cache_only)
{
	const ConfigCacheItem* item = config_get_item(name, cache_only);
	return item != NULL && item->type == CACHE_TYPE_INT ? item->n : 0;
}

// 설정에서 긴 정수를 가져옵니다.
gint64 config_get_long(ConfigKeys name, bool cache_only)
{
	const ConfigCacheItem* item = config_get_item(name, cache_only);
	return item != NULL && item->type == CACHE_TYPE_LONG ? item->l : 0;
}

// 설정으로 문자열을 넣습니다
void config_set_string(ConfigKeys name, const char* value, bool cache_only)
{
	g_return_if_fail(name > CONFIG_NONE && name < CONFIG_MAX_VALUE);
	cache_auto_set_item(name, value);
	if (!cache_only)
		sql_set_config(name);
}

// 설정으로 불린을 넣습니다.
void config_set_bool(ConfigKeys name, bool value, bool cache_only)
{
	g_return_if_fail(name > CONFIG_NONE && name < CONFIG_MAX_VALUE);
	const ConfigCacheItem item = {.b = value, .type = CACHE_TYPE_BOOL};
	cache_set_item(name, &item);
	if (!cache_only)
		sql_set_config(name);
}

// 설정으로 정수를 넣습니다.
void config_set_int(ConfigKeys name, gint32 value, bool cache_only)
{
	g_return_if_fail(name > CONFIG_NONE && name < CONFIG_MAX_VALUE);
	const ConfigCacheItem item = {.n = value, .type = CACHE_TYPE_INT};
	cache_set_item(name, &item);
	if (!cache_only)
		sql_set_config(name);
}

// 설정으로 긴 정수를 넣습니다.
void config_set_long(ConfigKeys name, gint64 value, bool cache_only)
{
	g_return_if_fail(name > CONFIG_NONE && name < CONFIG_MAX_VALUE);
	const ConfigCacheItem item = {.l = value, .type = CACHE_TYPE_LONG};
	cache_set_item(name, &item);
	if (!cache_only)
		sql_set_config(name);
}

// 실제 최대 캐시 크기를 얻습니다.
uint64_t config_get_actual_max_page_cache(void)
{
	const ConfigCacheItem* item = cache_get_item(CONFIG_GENERAL_MAX_PAGE_CACHE);
	int mb = item ? item->n : 230; // 원래 defs에서 가져와야 하는데 귀찮다
	return (uint64_t)mb * 1024ULL * 1024ULL; // MB 단위로 변환
}

// 파일 이름에 해당하는 최근 페이지 번호를 얻습니다.
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

// 파일 이름에 해당하는 최근 페이지 번호를 설정합니다.
// page가 0 이하이면 삭제합니다. 1 이상이면 페이지 번호로 설정합니다.
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

// 책 이동 위치를 추가합니다.
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
		MoveLocation* p = (MoveLocation*)g_ptr_array_index(cfgs.moves, i);
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

// 책 이동 위치를 고칩니다.
void movloc_edit(int no, const char* alias, const char* folder)
{
	if (alias == NULL || folder == NULL)
		return;
	if (no < 0 || no >= cfgs.moves->len)
		return;

	MoveLocation* p = (MoveLocation*)g_ptr_array_index(cfgs.moves, no);
	g_free(p->alias);
	g_free(p->folder);
	p->alias = g_strdup(alias);
	p->folder = g_strdup(folder);
}

// 책 이동 위치 순번을 다시 설정합니다.
void movloc_reindex(void)
{
	for (guint i = 0; i < cfgs.moves->len; i++)
	{
		MoveLocation* p = (MoveLocation*)g_ptr_array_index(cfgs.moves, i);
		p->no = (int)i; // 현재 인덱스로 번호 설정
	}
}

// 책 이동 위치를 삭제합니다.
bool movloc_delete(int no)
{
	if (no < 0 || no >= cfgs.moves->len)
		return false;

	g_ptr_array_remove_index(cfgs.moves, no);
	movloc_reindex();

	return true;
}

// 책 이동 위치의 순번을 바꿉니다.
bool movloc_swap(int from, int to)
{
	if (from < 0 || from >= cfgs.moves->len || to < 0 || to >= cfgs.moves->len || from == to)
		return false;

	MoveLocation* p1 = (MoveLocation*)g_ptr_array_index(cfgs.moves, from);
	MoveLocation* p2 = (MoveLocation*)g_ptr_array_index(cfgs.moves, to);

	g_ptr_array_index(cfgs.moves, from) = p2;
	g_ptr_array_index(cfgs.moves, to) = p1;

	movloc_reindex();

	return true;
}

// 책 이동 위치를 모두 가져옵니다.
GPtrArray *movloc_get_all_ptr(void)
{
	return cfgs.moves;
}

// 책 이동 위치를 DB에 저장합니다.
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
		sqlite3_bind_int(stmt, 1, p->no);
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


// 자연스러운 문자열 비교
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

// 근처 파일을 추가합니다.
bool nears_build(const char* dir, NearExtentionCompare compare)
{
	if (dir == NULL || compare == NULL)
		return false; // 디렉토리나 비교 함수가 NULL이면 실패

	if (g_strcmp0(cfgs.near_dir, dir) == 0 && cfgs.near_compare == compare)
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

// 지정한 파일이 없으면 근처 파일을 만듭니다
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
	return nears_build(dir, compare);
}

// 지정 파일의 앞쪽 근처 파일을 얻습니다.
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

// 지정 파일의 뒤쪽 근처 파일을 얻습니다.
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

// 지정 파일을 빼고 임의의 파일을 얻습니다.
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

// 지정 파일을 삭제하고 근처 파일을 얻습니다
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

// 지정 파일을 삭제하고 새로운 항목을 추가하면서, 근처 파일을 얻습니다.
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


// 문자열에서 modifier 파싱
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

// 단축키 만들기
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

// 단축키 얻어오기
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

// 키 입력값으로 단축키 명령 얻기
const char *shortcut_lookup(const guint key_val, const GdkModifierType key_state)
{
	const gint64 key = ((gint64)key_state << 32) | key_val; // 상위 32비트에 상태, 하위 32비트에 키값
	const char* action = g_hash_table_lookup(cfgs.shortcut, &key);
	return action;
}

// 언어 찾아보기
const char *locale_lookup(const char* key)
{
	const char* lookup = g_hash_table_lookup(cfgs.lang, key);
	return lookup ? lookup : key;
}
