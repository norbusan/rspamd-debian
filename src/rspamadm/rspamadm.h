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
#ifndef RSPAMD_RSPAMDADM_H
#define RSPAMD_RSPAMDADM_H

#include "config.h"
#include "ucl.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

extern GHashTable *ucl_vars;
extern lua_State *L;

GQuark rspamadm_error (void);

struct rspamadm_command;
typedef const gchar* (*rspamadm_help_func) (gboolean full_help,
											const struct rspamadm_command *cmd);
typedef void (*rspamadm_run_func) (gint argc, gchar **argv,
								   const struct rspamadm_command *cmd);
typedef void (*rspamadm_lua_exports_func) (gpointer lua_state);

#define RSPAMADM_FLAG_NOHELP (1 << 0)
#define RSPAMADM_FLAG_LUA (1 << 1)

struct rspamadm_command {
	const gchar *name;
	guint flags;
	rspamadm_help_func help;
	rspamadm_run_func run;
	rspamadm_lua_exports_func lua_subrs;
	GPtrArray *aliases;
	gpointer command_data; /* Opaque data */
};

extern const struct rspamadm_command *commands[];
extern struct rspamadm_command help_command;

const struct rspamadm_command *rspamadm_search_command (const gchar *name,
		GPtrArray *all_commands);
void rspamadm_fill_internal_commands (GPtrArray *dest);
void rspamadm_fill_lua_commands (lua_State *L, GPtrArray *dest);

gboolean rspamadm_execute_lua_ucl_subr (gpointer L, gint argc, gchar **argv,
										const ucl_object_t *res,
										const gchar *script_name,
										gboolean rspamadm_subcommand);

#endif