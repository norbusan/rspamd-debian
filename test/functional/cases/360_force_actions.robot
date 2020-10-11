*** Settings ***
Suite Setup     Force Actions Setup
Suite Teardown  Force Actions Teardown
Library         ${TESTDIR}/lib/rspamd.py
Resource        ${TESTDIR}/lib/rspamd.robot
Variables       ${TESTDIR}/lib/vars.py

*** Variables ***
${CONFIG}       ${TESTDIR}/configs/plugins.conf
${URL_TLD}      ${TESTDIR}/../lua/unit/test_tld.dat
${MESSAGE} 		${TESTDIR}/messages/url7.eml
${RSPAMD_SCOPE}  Suite

*** Test Cases ***
FORCE ACTIONS from reject to add header
  Scan File  ${MESSAGE}  Settings-Id=id_reject
  Expect Action  add header
  Expect Symbol  FORCE_ACTION_FORCE_REJECT_TO_ADD_HEADER

FORCE ACTIONS from reject to no action
  Scan File  ${MESSAGE}  Settings-Id=id_reject_no_action
  Expect Action  no action
  Expect Symbol  FORCE_ACTION_FORCE_REJECT_TO_NO_ACTION

FORCE ACTIONS from no action to reject
  Scan File  ${MESSAGE}  Settings-Id=id_no_action
  Expect Action  reject
  Expect Symbol  FORCE_ACTION_FORCE_NO_ACTION_TO_REJECT

FORCE ACTIONS from no action to add header
  Scan File  ${MESSAGE}  Settings-Id=id_no_action_to_add_header
  Expect Action  add header
  Expect Symbol  FORCE_ACTION_FORCE_NO_ACTION_TO_ADD_HEADER

FORCE ACTIONS from add header to no action
  Scan File  ${MESSAGE}  Settings-Id=id_add_header
  Expect Action  no action
  Expect Symbol  FORCE_ACTION_FORCE_ADD_HEADER_TO_NO_ACTION

FORCE ACTIONS from add header to reject
  Scan File  ${MESSAGE}  Settings-Id=id_add_header_to_reject
  Expect Action  reject
  Expect Symbol  FORCE_ACTION_FORCE_ADD_HEADER_TO_REJECT


*** Keywords ***
Force Actions Setup
  ${PLUGIN_CONFIG} =  Get File  ${TESTDIR}/configs/force_actions.conf
  Set Suite Variable  ${PLUGIN_CONFIG}
  Generic Setup  PLUGIN_CONFIG

Force Actions Teardown
  Normal Teardown
  Terminate All Processes    kill=True
