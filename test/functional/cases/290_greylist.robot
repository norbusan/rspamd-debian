*** Settings ***
Suite Setup     Greylist Setup
Suite Teardown  Greylist Teardown
Library         ${TESTDIR}/lib/rspamd.py
Resource        ${TESTDIR}/lib/rspamd.robot
Variables       ${TESTDIR}/lib/vars.py

*** Variables ***
${CONFIG}       ${TESTDIR}/configs/plugins.conf
${MESSAGE}      ${TESTDIR}/messages/spam_message.eml
${REDIS_SCOPE}  Suite
${RSPAMD_SCOPE}  Suite
${URL_TLD}      ${TESTDIR}/../lua/unit/test_tld.dat

*** Test Cases ***
GREYLIST NEW
  Scan File  ${MESSAGE}
  Expect Symbol With Option  GREYLIST  greylisted

GREYLIST EARLY
  Scan File  ${MESSAGE}
  Expect Symbol With Option  GREYLIST  greylisted

GREYLIST PASS
  Sleep  4s  Wait greylisting timeout
  Scan File  ${MESSAGE}
  Expect Symbol With Option  GREYLIST  pass

*** Keywords ***
Greylist Setup
  ${PLUGIN_CONFIG} =  Get File  ${TESTDIR}/configs/greylist.conf
  Set Suite Variable  ${PLUGIN_CONFIG}
  Generic Setup  PLUGIN_CONFIG
  Run Redis

Greylist Teardown
  Normal Teardown
  Shutdown Process With Children  ${REDIS_PID}
  Terminate All Processes    kill=True
