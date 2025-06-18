#include "pch.h"
#include <time.h>
#include "configs.h"
#include "doumi.h"

extern ConfigDefinition config_defs[CONFIG_MAX_VALUE];
extern ShortcutDefinition shortcut_defs[];

// 설정 자료
static struct Configs
{
	GHashTable* lang;
	GHashTable* cache;
	GHashTable* shortcut;
	char* app_path;
	char* cfg_path;
	time_t launched;
} cfgs =
{
	.lang = NULL,
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
static ConfigCacheItem* cache_get_item(const ConfigKeys key)
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
static const char* cache_auto_get_item(const ConfigKeys key, char* value, const size_t value_size)
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
		!sql_exec_stmt(db, "CREATE TABLE IF NOT EXISTS moves (folder TEXT PRIMARY KEY, alias TEXT);") ||
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

	if (cfgs.cfg_path)
		g_free(cfgs.cfg_path);
	if (cfgs.app_path)
		g_free(cfgs.app_path);
	if (cfgs.cache)
		g_hash_table_destroy(cfgs.cache);
	if (cfgs.shortcut)
		g_hash_table_destroy(cfgs.shortcut);
	if (cfgs.lang)
		g_hash_table_destroy(cfgs.lang);
}

// 설정에서 쓸 값을 캐시합니다.
void config_load_cache(void)
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

// 설정에서 아이템을 가져옵니다
static const ConfigCacheItem* config_get_item(ConfigKeys key, bool cache_only)
{
	if (key <= CONFIG_NONE || key >= CONFIG_MAX_VALUE)
		return NULL;
	if (!cache_only)
		sql_get_config(key);
	return cache_get_item(key);
}

// 설정에서 문자열을 가져오는데, 포인터로 반환합니다.
const char* config_get_string_ptr(ConfigKeys name, bool cache_only)
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
	const ConfigCacheItem item = { .b = value, .type = CACHE_TYPE_BOOL };
	cache_set_item(name, &item);
	if (!cache_only)
		sql_set_config(name);
}

// 설정으로 정수를 넣습니다.
void config_set_int(ConfigKeys name, gint32 value, bool cache_only)
{
	g_return_if_fail(name > CONFIG_NONE && name < CONFIG_MAX_VALUE);
	const ConfigCacheItem item = { .n = value, .type = CACHE_TYPE_INT };
	cache_set_item(name, &item);
	if (!cache_only)
		sql_set_config(name);
}

// 설정으로 긴 정수를 넣습니다.
void config_set_long(ConfigKeys name, gint64 value, bool cache_only)
{
	g_return_if_fail(name > CONFIG_NONE && name < CONFIG_MAX_VALUE);
	const ConfigCacheItem item = { .l = value, .type = CACHE_TYPE_LONG };
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

// 책 이동 위치를 얻습니다. 반환값은 config_free_moves으로 해제해야 합니다.
MoveLocation* movloc_get_all(int* ret_count)
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

		MoveLocation* move = g_new(MoveLocation, 1);
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

	MoveLocation* result = (MoveLocation*)g_ptr_array_free(moves, FALSE);
	*ret_count = (int)moves->len;
	return result;
}

// 책 이동 위치를 추가하거나 바꿉니다.
bool movloc_set(const char* folder, const char* alias)
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
bool movloc_delete(const char* folder)
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

// 책 이동 위치를 해제 합니다. movloc_get_all 함수로 얻은 값을 넣습니다.
void movloc_free(MoveLocation* moves, int count)
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

// 단축키 만들기
static gint64* convert_shortcut(const char* alias)
{
	gint64 ret = 0;
	GtkShortcutTrigger* trigger = gtk_shortcut_trigger_parse_string(alias);
	if (GTK_IS_KEYVAL_TRIGGER(trigger))
	{
		GtkKeyvalTrigger* keyval = GTK_KEYVAL_TRIGGER(trigger);
		const guint key = gtk_keyval_trigger_get_keyval(keyval);
		const GdkModifierType state = gtk_keyval_trigger_get_modifiers(keyval);
		ret = ((gint64)state << 32) | key; // 상위 32비트에 상태, 하위 32비트에 키값
	}
	g_object_unref(trigger);
	if (ret == 0)
		return NULL;
	gint64* p = g_new(gint64, 1);
	*p = ret;
	return p;
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
const char* shortcut_lookup(const guint key_val, const GdkModifierType key_state)
{
	const gint64 key = ((gint64)key_state << 32) | key_val; // 상위 32비트에 상태, 하위 32비트에 키값
	const char* action = g_hash_table_lookup(cfgs.shortcut, &key);
	return action;
}

// 언어 찾아보기
const char* locale_lookup(const char* key)
{
	const char* lookup = g_hash_table_lookup(cfgs.lang, key);
	return lookup ? lookup : key;
}
