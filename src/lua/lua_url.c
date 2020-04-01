/*-
 * Copyright 2016 Vsevolod Stakhov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "lua_common.h"
#include "contrib/uthash/utlist.h"

/***
 * @module rspamd_url
 * This module provides routines to handle URL's and extract URL's from the text.
 * Objects of this class are returned, for example, by `task:get_urls()` or `task:get_emails()`.
 * You can also create `rspamd_url` from any text.
 * @example
local url = require "rspamd_url"
local mpool = require "rspamd_mempool"

url.init("/usr/share/rspamd/effective_tld_names.dat")
local pool = mpool.create()
local res = url.create(pool, 'Look at: http://user@test.example.com/test?query")
local t = res:to_table()
-- Content of t:
-- url = ['http://test.example.com/test?query']
-- host = ['test.example.com']
-- user = ['user']
-- path = ['test']
-- tld = ['example.com']

pool:destroy() -- res is destroyed here, so you should not use it afterwards

local mistake = res:to_table() -- INVALID! as pool is destroyed
 */

/* URL methods */
LUA_FUNCTION_DEF (url, get_length);
LUA_FUNCTION_DEF (url, get_host);
LUA_FUNCTION_DEF (url, get_port);
LUA_FUNCTION_DEF (url, get_user);
LUA_FUNCTION_DEF (url, get_path);
LUA_FUNCTION_DEF (url, get_query);
LUA_FUNCTION_DEF (url, get_fragment);
LUA_FUNCTION_DEF (url, get_text);
LUA_FUNCTION_DEF (url, tostring);
LUA_FUNCTION_DEF (url, get_raw);
LUA_FUNCTION_DEF (url, get_tld);
LUA_FUNCTION_DEF (url, get_flags);
LUA_FUNCTION_DEF (url, get_protocol);
LUA_FUNCTION_DEF (url, to_table);
LUA_FUNCTION_DEF (url, is_phished);
LUA_FUNCTION_DEF (url, is_redirected);
LUA_FUNCTION_DEF (url, is_obscured);
LUA_FUNCTION_DEF (url, is_html_displayed);
LUA_FUNCTION_DEF (url, is_subject);
LUA_FUNCTION_DEF (url, get_phished);
LUA_FUNCTION_DEF (url, set_redirected);
LUA_FUNCTION_DEF (url, get_count);
LUA_FUNCTION_DEF (url, get_visible);
LUA_FUNCTION_DEF (url, create);
LUA_FUNCTION_DEF (url, init);
LUA_FUNCTION_DEF (url, all);

static const struct luaL_reg urllib_m[] = {
	LUA_INTERFACE_DEF (url, get_length),
	LUA_INTERFACE_DEF (url, get_host),
	LUA_INTERFACE_DEF (url, get_port),
	LUA_INTERFACE_DEF (url, get_user),
	LUA_INTERFACE_DEF (url, get_path),
	LUA_INTERFACE_DEF (url, get_query),
	LUA_INTERFACE_DEF (url, get_fragment),
	LUA_INTERFACE_DEF (url, get_text),
	LUA_INTERFACE_DEF (url, get_tld),
	LUA_INTERFACE_DEF (url, get_raw),
	LUA_INTERFACE_DEF (url, get_protocol),
	LUA_INTERFACE_DEF (url, to_table),
	LUA_INTERFACE_DEF (url, is_phished),
	LUA_INTERFACE_DEF (url, is_redirected),
	LUA_INTERFACE_DEF (url, is_obscured),
	LUA_INTERFACE_DEF (url, is_html_displayed),
	LUA_INTERFACE_DEF (url, is_subject),
	LUA_INTERFACE_DEF (url, get_phished),

	LUA_INTERFACE_DEF (url, get_visible),
	LUA_INTERFACE_DEF (url, get_count),
	LUA_INTERFACE_DEF (url, get_flags),
	{"get_redirected", lua_url_get_phished},
	LUA_INTERFACE_DEF (url, set_redirected),
	{"__tostring", lua_url_tostring},
	{NULL, NULL}
};

static const struct luaL_reg urllib_f[] = {
	LUA_INTERFACE_DEF (url, init),
	LUA_INTERFACE_DEF (url, create),
	LUA_INTERFACE_DEF (url, all),
	{NULL, NULL}
};

struct rspamd_lua_url *
lua_check_url (lua_State * L, gint pos)
{
	void *ud = rspamd_lua_check_udata (L, pos, "rspamd{url}");
	luaL_argcheck (L, ud != NULL, pos, "'url' expected");
	return ud ? ((struct rspamd_lua_url *)ud) : NULL;
}

static gboolean
lua_url_single_inserter (struct rspamd_url *url, gsize start_offset,
						 gsize end_offset, gpointer ud)
{
	lua_State *L = ud;
	struct rspamd_lua_url *lua_url;

	lua_url = lua_newuserdata (L, sizeof (struct rspamd_lua_url));
	rspamd_lua_setclass (L, "rspamd{url}", -1);
	lua_url->url = url;

	return TRUE;
}

/***
 * @method url:get_length()
 * Get length of the url
 * @return {number} length of url in bytes
 */
static gint
lua_url_get_length (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL) {
		lua_pushinteger (L, url->url->urllen);
	}
	else {
		lua_pushnil (L);
	}
	return 1;
}

/***
 * @method url:get_host()
 * Get domain part of the url
 * @return {string} domain part of URL
 */
static gint
lua_url_get_host (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL && url->url && url->url->hostlen > 0) {
		lua_pushlstring (L, rspamd_url_host (url->url), url->url->hostlen);
	}
	else {
		lua_pushnil (L);
	}
	return 1;
}

/***
 * @method url:get_port()
 * Get port of the url
 * @return {number} url port
 */
static gint
lua_url_get_port (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL) {
		lua_pushinteger (L, url->url->port);
	}
	else {
		lua_pushnil (L);
	}
	return 1;
}

/***
 * @method url:get_user()
 * Get user part of the url (e.g. username in email)
 * @return {string} user part of URL
 */
static gint
lua_url_get_user (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL && rspamd_url_user (url->url) != NULL) {
		lua_pushlstring (L, rspamd_url_user (url->url), url->url->userlen);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

/***
 * @method url:get_path()
 * Get path of the url
 * @return {string} path part of URL
 */
static gint
lua_url_get_path (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL && url->url->datalen > 0) {
		lua_pushlstring (L, rspamd_url_data_unsafe (url->url), url->url->datalen);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

/***
 * @method url:get_query()
 * Get query of the url
 * @return {string} query part of URL
 */
static gint
lua_url_get_query (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL && url->url->querylen > 0) {
		lua_pushlstring (L, rspamd_url_query_unsafe (url->url), url->url->querylen);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

/***
 * @method url:get_fragment()
 * Get fragment of the url
 * @return {string} fragment part of URL
 */
static gint
lua_url_get_fragment (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL && url->url->fragmentlen > 0) {
		lua_pushlstring (L, rspamd_url_fragment_unsafe (url->url), url->url->fragmentlen);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

/***
 * @method url:get_text()
 * Get full content of the url
 * @return {string} url string
 */
static gint
lua_url_get_text (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL) {
		lua_pushlstring (L, url->url->string, url->url->urllen);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

/***
 * @method url:tostring()
 * Get full content of the url or user@domain in case of email
 * @return {string} url as a string
 */
static gint
lua_url_tostring (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL && url->url != NULL) {
		if (url->url->protocol == PROTOCOL_MAILTO) {
			gchar *tmp = g_malloc (url->url->userlen + 1 +
								   url->url->hostlen);
			if (url->url->userlen) {
				memcpy (tmp, url->url->string + url->url->usershift, url->url->userlen);
			}

			tmp[url->url->userlen] = '@';
			memcpy (tmp + url->url->userlen + 1, rspamd_url_host_unsafe (url->url),
					url->url->hostlen);

			lua_pushlstring (L, tmp, url->url->userlen + 1 + url->url->hostlen);
			g_free (tmp);
		}
		else {
			lua_pushlstring (L, url->url->string, url->url->urllen);
		}
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

/***
 * @method url:get_raw()
 * Get full content of the url as it was parsed (e.g. with urldecode)
 * @return {string} url string
 */
static gint
lua_url_get_raw (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL) {
		lua_pushlstring (L, url->url->raw, url->url->rawlen);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

/***
 * @method url:is_phished()
 * Check whether URL is treated as phished
 * @return {boolean} `true` if URL is phished
 */
static gint
lua_url_is_phished (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL) {
		lua_pushboolean (L, url->url->flags & RSPAMD_URL_FLAG_PHISHED);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

/***
 * @method url:is_redirected()
 * Check whether URL was redirected
 * @return {boolean} `true` if URL is redirected
 */
static gint
lua_url_is_redirected (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL) {
		lua_pushboolean (L, url->url->flags & RSPAMD_URL_FLAG_REDIRECTED);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

/***
 * @method url:is_obscured()
 * Check whether URL is treated as obscured or obfusicated (e.g. numbers in IP address or other hacks)
 * @return {boolean} `true` if URL is obscured
 */
static gint
lua_url_is_obscured (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL) {
		lua_pushboolean (L, url->url->flags & RSPAMD_URL_FLAG_OBSCURED);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}


/***
 * @method url:is_html_displayed()
 * Check whether URL is just displayed in HTML (e.g. NOT a real href)
 * @return {boolean} `true` if URL is displayed only
 */
static gint
lua_url_is_html_displayed (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL) {
		lua_pushboolean (L, url->url->flags & RSPAMD_URL_FLAG_HTML_DISPLAYED);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

/***
 * @method url:is_subject()
 * Check whether URL is found in subject
 * @return {boolean} `true` if URL is found in subject
 */
static gint
lua_url_is_subject (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL) {
		lua_pushboolean (L, url->url->flags & RSPAMD_URL_FLAG_SUBJECT);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

/***
 * @method url:get_phished()
 * Get another URL that pretends to be this URL (e.g. used in phishing)
 * @return {url} phished URL
 */
static gint
lua_url_get_phished (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *purl, *url = lua_check_url (L, 1);

	if (url) {
		if (url->url->phished_url != NULL) {
			if (url->url->flags &
					(RSPAMD_URL_FLAG_PHISHED|RSPAMD_URL_FLAG_REDIRECTED)) {
				purl = lua_newuserdata (L, sizeof (struct rspamd_lua_url));
				rspamd_lua_setclass (L, "rspamd{url}", -1);
				purl->url = url->url->phished_url;

				return 1;
			}
		}
	}

	lua_pushnil (L);
	return 1;
}

/***
 * @method url:set_redirected(url,[ pool])
 * Set url as redirected to another url
 * @param {string|url} url new url that is redirecting an old one
 * @param {pool} pool if url is a string this is required for parsing
 * @return {url} parsed redirected url (if needed)
 */
static gint
lua_url_set_redirected (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1), *redir;
	rspamd_mempool_t *pool = NULL;

	if (url == NULL) {
		return luaL_error (L, "url is required as the first argument");
	}

	if (lua_type (L, 2) == LUA_TSTRING) {
		/* Parse url */
		if (lua_type (L, 3) != LUA_TUSERDATA) {
			return luaL_error (L, "mempool is required as the third argument");
		}

		pool = rspamd_lua_check_mempool (L, 3);

		if (pool == NULL) {
			return luaL_error (L, "mempool is required as the third argument");
		}

		gsize len;
		const gchar *urlstr = lua_tolstring (L, 2, &len);

		rspamd_url_find_single (pool, urlstr, len, RSPAMD_URL_FIND_ALL,
				lua_url_single_inserter, L);

		if (lua_type (L, -1) != LUA_TUSERDATA) {
			/* URL is actually not found */
			lua_pushnil (L);
		}
		else {
			redir = lua_check_url (L, -1);

			url->url->flags |= RSPAMD_URL_FLAG_REDIRECTED;
			url->url->phished_url = redir->url;
		}
	}
	else {
		redir = lua_check_url (L, 2);

		if (redir == NULL) {
			return luaL_error (L, "url is required as the second argument");
		}

		url->url->flags |= RSPAMD_URL_FLAG_REDIRECTED;
		url->url->phished_url = redir->url;

		/* Push back on stack */
		lua_pushvalue (L, 2);
	}

	return 1;
}

/***
 * @method url:get_tld()
 * Get effective second level domain part (eSLD) of the url host
 * @return {string} effective second level domain part (eSLD) of the url host
 */
static gint
lua_url_get_tld (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL && url->url->tldlen > 0) {
		lua_pushlstring (L, rspamd_url_tld_unsafe (url->url), url->url->tldlen);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

/***
 * @method url:get_protocol()
 * Get protocol name
 * @return {string} protocol as a string
 */
static gint
lua_url_get_protocol (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL && url->url->protocol != PROTOCOL_UNKNOWN) {
		lua_pushstring (L, rspamd_url_protocol_name (url->url->protocol));
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

/***
 * @method url:get_count()
 * Return number of occurrencies for this particular URL
 * @return {number} number of occurrencies
 */
static gint
lua_url_get_count (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL && url->url != NULL) {
		lua_pushinteger (L, url->url->count);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

 /***
* @method url:get_visible()
* Get visible part of the url with html tags stripped
* @return {string} url string
*/
static gint
lua_url_get_visible (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);

	if (url != NULL && url->url->visible_part) {
		lua_pushstring (L, url->url->visible_part);
	}
	else {
		lua_pushnil (L);
	}

return 1;
}

/***
 * @method url:to_table()
 * Return url as a table with the following fields:
 *
 * - `url`: full content
 * - `host`: hostname part
 * - `user`: user part
 * - `path`: path part
 * - `tld`: top level domain
 * - `protocol`: url protocol
 * @return {table} URL as a table
 */
static gint
lua_url_to_table (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);
	struct rspamd_url *u;

	if (url != NULL) {
		u = url->url;
		lua_createtable (L, 0, 12);
		lua_pushstring (L, "url");
		lua_pushlstring (L, u->string, u->urllen);
		lua_settable (L, -3);

		if (u->hostlen > 0) {
			lua_pushstring (L, "host");
			lua_pushlstring (L, rspamd_url_host_unsafe (u), u->hostlen);
			lua_settable (L, -3);
		}

		if (u->port != 0) {
			lua_pushstring (L, "port");
			lua_pushinteger (L, u->port);
			lua_settable (L, -3);
		}

		if (u->tldlen > 0) {
			lua_pushstring (L, "tld");
			lua_pushlstring (L, rspamd_url_tld_unsafe (u), u->tldlen);
			lua_settable (L, -3);
		}

		if (u->userlen > 0) {
			lua_pushstring (L, "user");
			lua_pushlstring (L, rspamd_url_user (u), u->userlen);
			lua_settable (L, -3);
		}

		if (u->datalen > 0) {
			lua_pushstring (L, "path");
			lua_pushlstring (L, rspamd_url_data_unsafe (u), u->datalen);
			lua_settable (L, -3);
		}

		if (u->querylen > 0) {
			lua_pushstring (L, "query");
			lua_pushlstring (L, rspamd_url_query_unsafe (u), u->querylen);
			lua_settable (L, -3);
		}

		if (u->fragmentlen > 0) {
			lua_pushstring (L, "fragment");
			lua_pushlstring (L, rspamd_url_fragment_unsafe (u), u->fragmentlen);
			lua_settable (L, -3);
		}


		lua_pushstring (L, "protocol");
		lua_pushstring (L, rspamd_url_protocol_name (u->protocol));
		lua_settable (L, -3);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}


/***
 * @function url.create([mempool,] str)
 * @param {rspamd_mempool} memory pool for URL, e.g. `task:get_mempool()`
 * @param {string} text that contains URL (can also contain other stuff)
 * @return {url} new url object that exists as long as the corresponding mempool exists
 */
static gint
lua_url_create (lua_State *L)
{
	LUA_TRACE_POINT;
	rspamd_mempool_t *pool;
	const gchar *text;
	size_t length;
	gboolean own_pool = FALSE;

	if (lua_type (L, 1) == LUA_TUSERDATA) {
		pool = rspamd_lua_check_mempool (L, 1);
		text = luaL_checklstring (L, 2, &length);
	}
	else {
		own_pool = TRUE;
		pool = rspamd_mempool_new (rspamd_mempool_suggest_size (), "url", 0);
		text = luaL_checklstring (L, 1, &length);
	}

	if (pool == NULL || text == NULL) {
		if (own_pool && pool) {
			rspamd_mempool_delete (pool);
		}

		return luaL_error (L, "invalid arguments");
	}
	else {
		rspamd_url_find_single (pool, text, length, RSPAMD_URL_FIND_ALL,
				lua_url_single_inserter, L);

		if (lua_type (L, -1) != LUA_TUSERDATA) {
			/* URL is actually not found */
			lua_pushnil (L);
		}
	}

	if (own_pool && pool) {
		rspamd_mempool_delete (pool);
	}

	return 1;
}

/***
 * @function url.init(tld_file)
 * Initialize url library if not initialized yet by Rspamd
 * @param {string} tld_file path to effective_tld_names.dat file (public suffix list)
 * @return nothing
 */
static gint
lua_url_init (lua_State *L)
{
	const gchar *tld_path;

	tld_path = luaL_checkstring (L, 1);

	rspamd_url_init (tld_path);

	return 0;
}

static gboolean
lua_url_table_inserter (struct rspamd_url *url, gsize start_offset,
		gsize end_offset, gpointer ud)
{
	lua_State *L = ud;
	struct rspamd_lua_url *lua_url;
	gint n;

	n = rspamd_lua_table_size (L, -1);
	lua_url = lua_newuserdata (L, sizeof (struct rspamd_lua_url));
	rspamd_lua_setclass (L, "rspamd{url}", -1);
	lua_url->url = url;
	lua_rawseti (L, -2, n + 1);

	return TRUE;
}


static gint
lua_url_all (lua_State *L)
{
	LUA_TRACE_POINT;
	rspamd_mempool_t *pool = rspamd_lua_check_mempool (L, 1);
	const gchar *text;
	size_t length;

	if (pool == NULL) {
		lua_pushnil (L);
	}
	else {
		text = luaL_checklstring (L, 2, &length);

		if (text != NULL) {
			lua_newtable (L);
			rspamd_url_find_multiple (pool, text, length,
					RSPAMD_URL_FIND_ALL, NULL,
					lua_url_table_inserter, L);

		}
		else {
			lua_pushnil (L);
		}
	}

	return 1;
}

/***
 * @method url:get_flags()
 * Return flags for a specified URL as map 'flag'->true for all flags set,
 * possible flags are:
 *
 * - `phished`: URL is likely phished
 * - `numeric`: URL is numeric (e.g. IP address)
 * - `obscured`: URL was obscured
 * - `redirected`: URL comes from redirector
 * - `html_displayed`: URL is used just for displaying purposes
 * - `text`: URL comes from the text
 * - `subject`: URL comes from the subject
 * - `host_encoded`: URL host part is encoded
 * - `schema_encoded`: URL schema part is encoded
 * - `query_encoded`: URL query part is encoded
 * - `missing_slahes`: URL has some slashes missing
 * - `idn`: URL has international characters
 * - `has_port`: URL has port
 * - `has_user`: URL has user part
 * - `schemaless`: URL has no schema
 * - `unnormalised`: URL has some unicode unnormalities
 * - `zw_spaces`: URL has some zero width spaces
 * - `url_displayed`: URL has some other url-like string in visible part
 * - `image`: URL is from src attribute of img HTML tag
 * @return {table} URL flags
 */
#define PUSH_FLAG(fl, name) do { \
	if (flags & (fl)) { \
		lua_pushstring (L, (name)); \
		lua_pushboolean (L, true); \
		lua_settable (L, -3); \
	} \
} while (0)

static gint
lua_url_get_flags (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_url *url = lua_check_url (L, 1);
	enum rspamd_url_flags flags;

	if (url != NULL) {
		flags = url->url->flags;

		lua_createtable (L, 0, 4);

		PUSH_FLAG (RSPAMD_URL_FLAG_PHISHED, "phished");
		PUSH_FLAG (RSPAMD_URL_FLAG_NUMERIC, "numeric");
		PUSH_FLAG (RSPAMD_URL_FLAG_OBSCURED, "obscured");
		PUSH_FLAG (RSPAMD_URL_FLAG_REDIRECTED, "redirected");
		PUSH_FLAG (RSPAMD_URL_FLAG_HTML_DISPLAYED, "html_displayed");
		PUSH_FLAG (RSPAMD_URL_FLAG_FROM_TEXT, "text");
		PUSH_FLAG (RSPAMD_URL_FLAG_SUBJECT, "subject");
		PUSH_FLAG (RSPAMD_URL_FLAG_HOSTENCODED, "host_encoded");
		PUSH_FLAG (RSPAMD_URL_FLAG_SCHEMAENCODED, "schema_encoded");
		PUSH_FLAG (RSPAMD_URL_FLAG_PATHENCODED, "path_encoded");
		PUSH_FLAG (RSPAMD_URL_FLAG_QUERYENCODED, "query_encoded");
		PUSH_FLAG (RSPAMD_URL_FLAG_MISSINGSLASHES, "missing_slahes");
		PUSH_FLAG (RSPAMD_URL_FLAG_IDN, "idn");
		PUSH_FLAG (RSPAMD_URL_FLAG_HAS_PORT, "has_port");
		PUSH_FLAG (RSPAMD_URL_FLAG_HAS_USER, "has_user");
		PUSH_FLAG (RSPAMD_URL_FLAG_SCHEMALESS, "schemaless");
		PUSH_FLAG (RSPAMD_URL_FLAG_UNNORMALISED, "unnormalised");
		PUSH_FLAG (RSPAMD_URL_FLAG_ZW_SPACES, "zw_spaces");
		PUSH_FLAG (RSPAMD_URL_FLAG_DISPLAY_URL, "url_displayed");
		PUSH_FLAG (RSPAMD_URL_FLAG_IMAGE, "image");
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

#undef PUSH_FLAG

static gint
lua_load_url (lua_State * L)
{
	lua_newtable (L);
	luaL_register (L, NULL, urllib_f);

	return 1;
}

void
luaopen_url (lua_State * L)
{
	rspamd_lua_new_class (L, "rspamd{url}", urllib_m);
	lua_pop (L, 1);

	rspamd_lua_add_preload (L, "rspamd_url", lua_load_url);
}
