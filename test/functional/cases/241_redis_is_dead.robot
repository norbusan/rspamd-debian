*** Settings ***
Documentation    Test the case when trying to connect to nowhere
...              (i.e. redis is not running)
Test Setup      Redis Setup
Test Teardown   Redis Teardown
Library         Process
Library         ${TESTDIR}/lib/rspamd.py
Resource        ${TESTDIR}/lib/rspamd.robot
Variables       ${TESTDIR}/lib/vars.py
Suite Teardown  Terminate All Processes    kill=True


*** Variables ***
${REDIS_SCOPE}  Test
${RSPAMD_SCOPE}  Test
${CONFIG}       ${TESTDIR}/configs/plugins.conf
${URL_TLD}      ${TESTDIR}/../lua/unit/test_tld.dat
${MESSAGE}      ${TESTDIR}/messages/spam_message.eml


*** Test Cases ***
Dead Redis client
  ${result} =  Scan Message With Rspamc  ${MESSAGE}
  Check Rspamc  ${result}  REDIS_ERROR_3 (0.00)[Connection refused]
  Check Rspamc  ${result}  REDIS_ASYNC201809_ERROR (0.00)[Connection refused]
  Check Rspamc  ${result}  REDIS_ASYNC_ERROR (0.00)[Connection refused]

*** Keywords ***
Lua Setup
  [Arguments]  ${LUA_SCRIPT}
  Set Suite Variable  ${LUA_SCRIPT}
  ${PLUGIN_CONFIG} =  Get File  ${TESTDIR}/configs/redis.conf
  Set Suite Variable  ${PLUGIN_CONFIG}
  Generic Setup  PLUGIN_CONFIG

Redis Setup
  Lua Setup  ${TESTDIR}/lua/redis.lua

Redis Teardown
  Normal Teardown
#  Shutdown Process With Children  ${REDIS_PID}
