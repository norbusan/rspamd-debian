--[[
Copyright (c) 2020, Vsevolod Stakhov <vsevolod@highsecure.ru>

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
]] --

local rspamd_logger = require "rspamd_logger"
local lua_util = require "lua_util"
local dkim_sign_tools = require "lua_dkim_tools"
local rspamd_util = require "rspamd_util"
local rspamd_rsa_privkey = require "rspamd_rsa_privkey"
local rspamd_rsa = require "rspamd_rsa"
local fun = require "fun"
local auth_results = require "lua_auth_results"
local hash = require "rspamd_cryptobox_hash"

if confighelp then
  return
end

local N = 'arc'

if not rspamd_plugins.dkim then
  rspamd_logger.errx(rspamd_config, "cannot enable arc plugin: dkim is disabled")
  return
end

local dkim_verify = rspamd_plugins.dkim.verify
local dkim_sign = rspamd_plugins.dkim.sign
local dkim_canonicalize = rspamd_plugins.dkim.canon_header_relaxed
local redis_params

if not dkim_verify or not dkim_sign or not dkim_canonicalize then
  rspamd_logger.errx(rspamd_config, "cannot enable arc plugin: dkim is disabled")
  return
end

local arc_symbols = {
  allow = 'ARC_ALLOW',
  invalid = 'ARC_INVALID',
  dnsfail = 'ARC_DNSFAIL',
  na = 'ARC_NA',
  reject = 'ARC_REJECT',
}

local symbols = {
  spf_allow_symbol = 'R_SPF_ALLOW',
  spf_deny_symbol = 'R_SPF_FAIL',
  spf_softfail_symbol = 'R_SPF_SOFTFAIL',
  spf_neutral_symbol = 'R_SPF_NEUTRAL',
  spf_tempfail_symbol = 'R_SPF_DNSFAIL',
  spf_permfail_symbol = 'R_SPF_PERMFAIL',
  spf_na_symbol = 'R_SPF_NA',

  dkim_allow_symbol = 'R_DKIM_ALLOW',
  dkim_deny_symbol = 'R_DKIM_REJECT',
  dkim_tempfail_symbol = 'R_DKIM_TEMPFAIL',
  dkim_na_symbol = 'R_DKIM_NA',
  dkim_permfail_symbol = 'R_DKIM_PERMFAIL',
}

local settings = {
  allow_envfrom_empty = true,
  allow_hdrfrom_mismatch = false,
  allow_hdrfrom_mismatch_local = false,
  allow_hdrfrom_mismatch_sign_networks = false,
  allow_hdrfrom_multiple = false,
  allow_username_mismatch = false,
  sign_authenticated = true,
  domain = {},
  path = string.format('%s/%s/%s', rspamd_paths['DBDIR'], 'arc', '$domain.$selector.key'),
  sign_local = true,
  selector = 'arc',
  sign_symbol = 'ARC_SIGNED',
  try_fallback = true,
  use_domain = 'header',
  use_esld = true,
  use_redis = false,
  key_prefix = 'arc_keys', -- default hash name
  reuse_auth_results = false, -- Reuse the existing authentication results
  whitelisted_signers_map = nil, -- Trusted signers domains
}

-- To match normal AR
local ar_settings = auth_results.default_settings

local function parse_arc_header(hdr, target)
  -- Split elements by ';' and trim spaces
  local arr = fun.totable(fun.map(
    function(val)
      return fun.totable(fun.map(lua_util.rspamd_str_trim,
        fun.filter(function(v) return v and #v > 0 end,
          lua_util.rspamd_str_split(val.decoded, ';'))))
    end, hdr
  ))

  -- Now we have two tables in format:
  -- [sigs] -> [{sig1_elts}, {sig2_elts}...]
  for i,elts in ipairs(arr) do
    if not target[i] then target[i] = {} end
    -- Split by kv pair, like k=v
    fun.each(function(v)
      if v[1] and v[2] then
        target[i][v[1]] = v[2]
      end
    end, fun.map(function(elt)
      return lua_util.rspamd_str_split(elt, '=')
    end, elts))
    target[i].header = hdr[i].decoded
    target[i].raw_header = hdr[i].value
  end
end

local function arc_validate_seals(task, seals, sigs, seal_headers, sig_headers)
  for i = 1,#seals do
    if (sigs[i].i or 0) ~= i then
      rspamd_logger.infox(task, 'bad i value for signature: %s, expected %s',
        sigs[i].i, i)
      task:insert_result(arc_symbols['invalid'], 1.0, 'invalid count of seals and signatures')
      return false
    end
    if (seals[i].i or 0) ~= i then
      rspamd_logger.infox(task, 'bad i value for seal: %s, expected %s',
        seals[i].i, i)
      task:insert_result(arc_symbols['invalid'], 1.0, 'invalid count of seals and signatures')
      return false
    end

    if not seals[i].cv then
      task:insert_result(arc_symbols['invalid'], 1.0, 'no cv on i=' .. tostring(i))
      return false
    end

    if i == 1 then
      -- We need to ensure that cv of seal is equal to 'none'
      if seals[i].cv ~= 'none' then
        task:insert_result(arc_symbols['invalid'], 1.0, 'cv is not "none" for i=1')
        return false
      end
    else
      if seals[i].cv ~= 'pass' then
        task:insert_result(arc_symbols['reject'], 1.0, string.format('cv is %s on i=%d',
            seals[i].cv, i))
        return false
      end
    end
  end

  return true
end

local function arc_callback(task)
  local arc_sig_headers = task:get_header_full('ARC-Message-Signature')
  local arc_seal_headers = task:get_header_full('ARC-Seal')

  if not arc_sig_headers or not arc_seal_headers then
    task:insert_result(arc_symbols['na'], 1.0)
    return
  end

  if #arc_sig_headers ~= #arc_seal_headers then
    -- We mandate that count of seals is equal to count of signatures
    rspamd_logger.infox(task, 'number of seals (%s) is not equal to number of signatures (%s)',
        #arc_seal_headers, #arc_sig_headers)
    task:insert_result(arc_symbols['invalid'], 1.0, 'invalid count of seals and signatures')
    return
  end

  local cbdata = {
    seals = {},
    sigs = {},
    checked = 0,
    res = 'success',
    errors = {},
    allowed_by_trusted = false
  }

  parse_arc_header(arc_seal_headers, cbdata.seals)
  parse_arc_header(arc_sig_headers, cbdata.sigs)

  -- Fix i type
  fun.each(function(hdr)
    hdr.i = tonumber(hdr.i) or 0
  end, cbdata.seals)

  fun.each(function(hdr)
    hdr.i = tonumber(hdr.i) or 0
  end, cbdata.sigs)

  -- Now we need to sort elements according to their [i] value
  table.sort(cbdata.seals, function(e1, e2)
    return (e1.i or 0) < (e2.i or 0)
  end)
  table.sort(cbdata.sigs, function(e1, e2)
    return (e1.i or 0) < (e2.i or 0)
  end)

  lua_util.debugm(N, task, 'got %s arc sections', #cbdata.seals)

  -- Now check sanity of what we have
  if not arc_validate_seals(task, cbdata.seals, cbdata.sigs,
    arc_seal_headers, arc_sig_headers) then
    return
  end

  task:cache_set('arc-sigs', cbdata.sigs)
  task:cache_set('arc-seals', cbdata.seals)

  local function gen_arc_seal_cb(sig)
    return function (_, res, err, domain)
      cbdata.checked = cbdata.checked + 1
      lua_util.debugm(N, task, 'checked arc seal: %s(%s), %s processed',
          res, err, cbdata.checked)

      if not res then
        cbdata.res = 'fail'
        if err and domain then
          table.insert(cbdata.errors, string.format('sig:%s:%s', domain, err))
        end
      end

      if settings.whitelisted_signers_map and cbdata.res == 'success' then
        if settings.whitelisted_signers_map:get_key(sig.d) then
          -- Whitelisted signer has been found in a valid chain
          task:insert_result(arc_symbols.trusted_allow, 1.0,
              string.format('%s:s=%s:i=%d', domain, sig.s, cbdata.checked))
        end
      end

      if cbdata.checked == #arc_sig_headers then
        if cbdata.res == 'success' then
          task:insert_result(arc_symbols.allow, 1.0, string.format('%s:s=%s:i=%d',
              domain, sig.s, cbdata.checked))
        else
          task:insert_result(arc_symbols.reject, 1.0,
              rspamd_logger.slog('seal check failed: %s, %s', cbdata.res,
                  cbdata.errors))
        end
      end
    end
  end

  local function arc_signature_cb(_, res, err, domain)
    lua_util.debugm(N, task, 'checked arc signature %s: %s(%s), %s processed',
      domain, res, err, cbdata.checked)

    if not res then
      cbdata.res = 'fail'
      if err and domain then
        table.insert(cbdata.errors, string.format('sig:%s:%s', domain, err))
      end
    end
    if cbdata.res == 'success' then
      -- Verify seals
      cbdata.checked = 0
      fun.each(
        function(sig)
          local ret, lerr = dkim_verify(task, sig.header, gen_arc_seal_cb(sig), 'arc-seal')
          if not ret then
            cbdata.res = 'fail'
            table.insert(cbdata.errors, string.format('seal:%s:s=%s:i=%s:%s',
                sig.d or '', sig.s or '', sig.i or '', lerr))
            cbdata.checked = cbdata.checked + 1
            lua_util.debugm(N, task, 'checked arc seal %s: %s(%s), %s processed',
              sig.d, ret, lerr, cbdata.checked)
          end
        end, cbdata.seals)
    else
      task:insert_result(arc_symbols['reject'], 1.0,
        rspamd_logger.slog('signature check failed: %s, %s', cbdata.res,
          cbdata.errors))
    end
  end

  --[[
  1.  Collect all ARC Sets currently attached to the message.  If there
       are none, the Chain Validation Status is "none" and the algorithm
       stops here.  The maximum number of ARC Sets that can be attached
       to a message is 50.  If more than the maximum number exist the
       Chain Validation Status is "fail" and the algorithm stops here.
       In the following algorithm, the maximum ARC instance value is
       referred to as "N".

   2.  If the Chain Validation Status of the highest instance value ARC
       Set is "fail", then the Chain Validation status is "fail" and the
       algorithm stops here.

   3.  Validate the structure of the Authenticated Received Chain.  A
       valid ARC has the following conditions:

       1.  Each ARC Set MUST contain exactly one each of the three ARC
           header fields (AAR, AMS, and AS).

       2.  The instance values of the ARC Sets MUST form a continuous
           sequence from 1..N with no gaps or repetition.

       3.  The "cv" value for all ARC-Seal header fields must be non-
           failing.  For instance values > 1, the value must be "pass".
           For instance value = 1, the value must be "none".

       *  If any of these conditions are not met, the Chain Validation
          Status is "fail" and the algorithm stops here.

   4.  Validate the AMS with the greatest instance value (most recent).
       If validation fails, then the Chain Validation Status is "fail"
       and the algorithm stops here.

   5 - 7. Optional, not implemented
   8.  Validate each AS beginning with the greatest instance value and
       proceeding in decreasing order to the AS with the instance value
       of 1.  If any AS fails to validate, the Chain Validation Status
       is "fail" and the algorithm stops here.
   9.  If the algorithm reaches this step, then the Chain Validation
       Status is "pass", and the algorithm is complete.
  ]]--

  local processed = 0
  local sig = cbdata.sigs[#cbdata.sigs] -- last AMS
  local ret,err = dkim_verify(task, sig.header, arc_signature_cb, 'arc-sign')

  if not ret then
    cbdata.res = 'fail'
    table.insert(cbdata.errors, string.format('sig:%s:%s', sig.d or '', err))
  else
    processed = processed + 1
    lua_util.debugm(N, task, 'processed arc signature %s[%s]: %s(%s), %s processed',
      sig.d, sig.i, ret, err, cbdata.checked)
  end

  if processed == 0 then
    task:insert_result(arc_symbols['reject'], 1.0,
      rspamd_logger.slog('cannot verify %s of %s signatures: %s',
        #arc_sig_headers - processed, #arc_sig_headers, cbdata.errors))
  end
end

local opts = rspamd_config:get_all_opt('arc')
if not opts or type(opts) ~= 'table' then
  return
end

if opts['symbols'] then
  for k,_ in pairs(arc_symbols) do
    if opts['symbols'][k] then
      arc_symbols[k] = opts['symbols'][k]
    end
  end
end


local id = rspamd_config:register_symbol({
  name = 'ARC_CALLBACK',
  type = 'callback',
  group = 'policies',
  groups = {'arc'},
  callback = arc_callback
})

rspamd_config:register_symbol({
  name = arc_symbols['allow'],
  parent = id,
  type = 'virtual',
  score = -1.0,
  group = 'policies',
  groups = {'arc'},
})
rspamd_config:register_symbol({
  name = arc_symbols['reject'],
  parent = id,
  type = 'virtual',
  score = 2.0,
  group = 'policies',
  groups = {'arc'},
})
rspamd_config:register_symbol({
  name = arc_symbols['invalid'],
  parent = id,
  type = 'virtual',
  score = 1.0,
  group = 'policies',
  groups = {'arc'},
})
rspamd_config:register_symbol({
  name = arc_symbols['dnsfail'],
  parent = id,
  type = 'virtual',
  score = 0.0,
  group = 'policies',
  groups = {'arc'},
})
rspamd_config:register_symbol({
  name = arc_symbols['na'],
  parent = id,
  type = 'virtual',
  score = 0.0,
  group = 'policies',
  groups = {'arc'},
})

if settings.whitelisted_signers_map then
  local lua_maps = require "lua_maps"
  settings.whitelisted_signers_map = lua_maps.map_add_from_ucl(settings.whitelisted_signers_map,
      'set',
      'ARC trusted signers domains')
  if settings.whitelisted_signers_map then
    arc_symbols.trusted_allow = arc_symbols.trusted_allow or 'ARC_ALLOW_TRUSTED'
    rspamd_config:register_symbol({
      name = arc_symbols.trusted_allow,
      parent = id,
      type = 'virtual',
      score = -2.0,
      group = 'policies',
      groups = {'arc'},
    })
  end
end

rspamd_config:register_dependency('ARC_CALLBACK', symbols['spf_allow_symbol'])
rspamd_config:register_dependency('ARC_CALLBACK', symbols['dkim_allow_symbol'])

local function arc_sign_seal(task, params, header)
  local arc_sigs = task:cache_get('arc-sigs')
  local arc_seals = task:cache_get('arc-seals')
  local arc_auth_results = task:get_header_full('ARC-Authentication-Results') or {}
  local cur_auth_results
  local privkey

  if params.rawkey then
    -- Distinguish between pem and base64
    if string.match(params.rawkey, '^-----BEGIN') then
      privkey = rspamd_rsa_privkey.load_pem(params.rawkey)
    else
      privkey = rspamd_rsa_privkey.load_base64(params.rawkey)
    end
  elseif params.key then
    privkey = rspamd_rsa_privkey.load_file(params.key)
  end

  if not privkey then
    rspamd_logger.errx(task, 'cannot load private key for signing')
    return
  end

  if settings.reuse_auth_results then
    local ar_header = task:get_header('Authentication-Results')

    if ar_header then
      rspamd_logger.debugm(N, task, 'reuse authentication results header for ARC')
      cur_auth_results = ar_header
    else
      rspamd_logger.debugm(N, task, 'cannot reuse authentication results, header is missing')
      cur_auth_results = auth_results.gen_auth_results(task, ar_settings) or ''
    end
  else
    cur_auth_results = auth_results.gen_auth_results(task, ar_settings) or ''
  end

  local sha_ctx = hash.create_specific('sha256')

  -- Update using previous seals + sigs + AAR
  local cur_idx = 1
  if arc_seals then
    cur_idx = #arc_seals + 1
    for i = (cur_idx - 1), 1, (-1) do
      if arc_auth_results[i] then
        local s = dkim_canonicalize('ARC-Authentication-Results',
          arc_auth_results[i].value)
        sha_ctx:update(s)
        lua_util.debugm(N, task, 'update signature with header: %s', s)
      end
      if arc_sigs[i] then
        local s = dkim_canonicalize('ARC-Message-Signature',
          arc_sigs[i].raw_header)
        sha_ctx:update(s)
        lua_util.debugm(N, task, 'update signature with header: %s', s)
      end
      if arc_seals[i] then
        local s = dkim_canonicalize('ARC-Seal', arc_seals[i].raw_header)
        sha_ctx:update(s)
        lua_util.debugm(N, task, 'update signature with header: %s', s)
      end
    end
  end

  header = lua_util.fold_header(task,
    'ARC-Message-Signature',
    header)

  cur_auth_results = string.format('i=%d; %s', cur_idx, cur_auth_results)
  cur_auth_results = lua_util.fold_header(task,
      'ARC-Authentication-Results',
      cur_auth_results, ';')

  local s = dkim_canonicalize('ARC-Authentication-Results',
    cur_auth_results)
  sha_ctx:update(s)
  lua_util.debugm(N, task, 'update signature with header: %s', s)
  s = dkim_canonicalize('ARC-Message-Signature', header)
  sha_ctx:update(s)
  lua_util.debugm(N, task, 'update signature with header: %s', s)

  local cur_arc_seal = string.format('i=%d; s=%s; d=%s; t=%d; a=rsa-sha256; cv=%s; b=',
      cur_idx,
      params.selector,
      params.domain,
      math.floor(rspamd_util.get_time()), params.arc_cv)
  s = string.format('%s:%s', 'arc-seal', cur_arc_seal)
  sha_ctx:update(s)
  lua_util.debugm(N, task, 'initial update signature with header: %s', s)

  local nl_type
  if task:has_flag("milter") then
    nl_type = "lf"
  else
    nl_type = task:get_newlines_type()
  end

  local sig = rspamd_rsa.sign_memory(privkey, sha_ctx:bin())
  cur_arc_seal = string.format('%s%s', cur_arc_seal,
    sig:base64(70, nl_type))

  task:set_milter_reply({
    add_headers = {
      ['ARC-Authentication-Results'] = {order = 1, value = cur_auth_results},
      ['ARC-Message-Signature'] = {order = 1, value = header},
      ['ARC-Seal'] = {order = 1, value = lua_util.fold_header(task,
        'ARC-Seal', cur_arc_seal) }
    }
  })
  task:insert_result(settings.sign_symbol, 1.0, string.format('i=%d', cur_idx))
end

local function prepare_arc_selector(task, sel)
  local arc_seals = task:cache_get('arc-seals')

  sel.arc_cv = 'none'
  sel.arc_idx = 1
  sel.no_cache = true
  sel.sign_type = 'arc-sign'

  if arc_seals then
    sel.arc_idx = #arc_seals + 1

    if task:has_symbol(arc_symbols.allow) then
      sel.arc_cv = 'pass'
    else
      sel.arc_cv = 'fail'
    end
  end
end

local function do_sign(task, p)
  if p.alg and p.alg ~= 'rsa' then
    -- No support for ed25519 keys
    return
  end

  prepare_arc_selector(task, p)

  if settings.check_pubkey then
    local resolve_name = p.selector .. "._domainkey." .. p.domain
    task:get_resolver():resolve_txt({
      task = task,
      name = resolve_name,
      callback = function(_, _, results, err)
        if not err and results and results[1] then
          p.pubkey = results[1]
          p.strict_pubkey_check = not settings.allow_pubkey_mismatch
        elseif not settings.allow_pubkey_mismatch then
          rspamd_logger.errx('public key for domain %s/%s is not found: %s, skip signing',
              p.domain, p.selector, err)
          return
        else
          rspamd_logger.infox('public key for domain %s/%s is not found: %s',
              p.domain, p.selector, err)
        end

        local dret, hdr = dkim_sign(task, p)
        if dret then
          local sret, _ = arc_sign_seal(task, p, hdr)
          if sret then
            task:insert_result(settings.sign_symbol, 1.0)
          end
        end

      end,
      forced = true
    })
  else
    local dret, hdr = dkim_sign(task, p)
    if dret then
      local sret, _ = arc_sign_seal(task, p, hdr)
      if sret then
        task:insert_result(settings.sign_symbol, 1.0)
      end
    end
  end
end

local function sign_error(task, msg)
  rspamd_logger.errx(task, 'signing failure: %s', msg)
end

local function arc_signing_cb(task)
  local ret, selectors = dkim_sign_tools.prepare_dkim_signing(N, task, settings)

  if not ret then
    return
  end

  if settings.use_redis then
    dkim_sign_tools.sign_using_redis(N, task, settings, selectors, do_sign, sign_error)
  else
    if selectors.vault then
      dkim_sign_tools.sign_using_vault(N, task, settings, selectors, do_sign, sign_error)
    else
      -- TODO: no support for multiple sigs
      local p = selectors[1]
      prepare_arc_selector(task, p)
      if ((p.key or p.rawkey) and p.selector) then
        if p.key then
          p.key = lua_util.template(p.key, {
            domain = p.domain,
            selector = p.selector
          })

          local exists,err = rspamd_util.file_exists(p.key)
          if not exists then
            if err and err == 'No such file or directory' then
              lua_util.debugm(N, task, 'cannot read key from %s: %s', p.key, err)
            else
              rspamd_logger.warnx(task, 'cannot read key from %s: %s', p.key, err)
            end
            return false
          end
        end

        do_sign(task, p)
      else
        rspamd_logger.infox(task, 'key path or dkim selector unconfigured; no signing')
        return false
      end
    end
  end
end

dkim_sign_tools.process_signing_settings(N, settings, opts)

if not dkim_sign_tools.validate_signing_settings(settings) then
  rspamd_logger.infox(rspamd_config, 'mandatory parameters missing, disable arc signing')
  return
end

local ar_opts = rspamd_config:get_all_opt('milter_headers')

if ar_opts and ar_opts.routines then
  local routines = ar_opts.routines

  if routines['authentication-results'] then
    ar_settings = lua_util.override_defaults(ar_settings,
        routines['authentication-results'])
  end
end

if settings.use_redis then
  redis_params = rspamd_parse_redis_server('arc')

  if not redis_params then
    rspamd_logger.errx(rspamd_config, 'no servers are specified, '..
        'but module is configured to load keys from redis, disable arc signing')
    return
  end

  settings.redis_params = redis_params
end

rspamd_config:register_symbol({
  name = settings['sign_symbol'],
  callback = arc_signing_cb,
  groups = {"policies", "arc"},
  score = 0.0,
})

-- Do not sign unless valid
rspamd_config:register_dependency(settings['sign_symbol'], 'ARC_CALLBACK')
