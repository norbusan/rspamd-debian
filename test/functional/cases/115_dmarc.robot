*** Settings ***
Suite Setup     DMARC Setup
Suite Teardown  Simple Teardown
Library         ${TESTDIR}/lib/rspamd.py
Resource        ${TESTDIR}/lib/rspamd.robot
Variables       ${TESTDIR}/lib/vars.py

*** Variables ***
${CONFIG}        ${TESTDIR}/configs/plugins.conf
${RSPAMD_SCOPE}  Suite
${URL_TLD}       ${TESTDIR}/../../contrib/publicsuffix/effective_tld_names.dat

*** Test Cases ***
DMARC NONE PASS DKIM
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/dmarc/pass_none.eml
  Check Rspamc  ${result}  DMARC_POLICY_ALLOW

DMARC NONE PASS SPF
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/dmarc/fail_none.eml
  ...  -i  8.8.4.4  --from  foo@spf.cacophony.za.org
  Check Rspamc  ${result}  DMARC_POLICY_ALLOW

DMARC NONE FAIL
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/dmarc/fail_none.eml
  Check Rspamc  ${result}  DMARC_POLICY_SOFTFAIL

DMARC REJECT FAIL
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/dmarc/fail_reject.eml
  Check Rspamc  ${result}  DMARC_POLICY_REJECT

DMARC QUARANTINE FAIL
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/dmarc/fail_quarantine.eml
  Check Rspamc  ${result}  DMARC_POLICY_QUARANTINE

DMARC SP NONE FAIL
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/dmarc/subdomain_fail_none.eml
  Check Rspamc  ${result}  DMARC_POLICY_SOFTFAIL

DMARC SP REJECT FAIL
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/dmarc/subdomain_fail_reject.eml
  Check Rspamc  ${result}  DMARC_POLICY_REJECT

DMARC SP QUARANTINE FAIL
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/dmarc/subdomain_fail_quarantine.eml
  Check Rspamc  ${result}  DMARC_POLICY_QUARANTINE

DMARC SUBDOMAIN FAIL DKIM STRICT ALIGNMENT
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/dmarc/onsubdomain_fail_alignment.eml
  Check Rspamc  ${result}  DMARC_POLICY_REJECT

DMARC SUBDOMAIN PASS DKIM RELAXED ALIGNMENT
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/dmarc/onsubdomain_pass_relaxed.eml
  Check Rspamc  ${result}  DMARC_POLICY_ALLOW

DMARC SUBDOMAIN PASS SPF STRICT ALIGNMENT
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/dmarc/onsubdomain_fail_alignment.eml
  ...  -i  37.48.67.26  --from  foo@yo.mom.za.org
  Check Rspamc  ${result}  DMARC_POLICY_ALLOW

DMARC SUBDOMAIN FAIL SPF STRICT ALIGNMENT
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/dmarc/onsubdomain_fail_alignment.eml
  ...  -i  37.48.67.26  --from  foo@mom.za.org
  Check Rspamc  ${result}  DMARC_POLICY_REJECT

DMARC SUBDOMAIN PASS SPF RELAXED ALIGNMENT
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/dmarc/onsubdomain_fail.eml
  ...  -i  37.48.67.26  --from  foo@mom.za.org
  Check Rspamc  ${result}  DMARC_POLICY_ALLOW

DMARC DNSFAIL
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/dmarc/dmarc_tmpfail.eml
  ...  -i  37.48.67.26  --from  foo@mom.za.org
  Check Rspamc  ${result}  DMARC_DNSFAIL

DMARC NA NXDOMAIN
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/utf.eml
  ...  -i  37.48.67.26  --from  foo@mom.za.org
  Check Rspamc  ${result}  DMARC_NA

DMARC PCT ZERO REJECT
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/dmarc/pct_none.eml
  ...  -i  37.48.67.26  --from  foo@mom.za.org
  Check Rspamc  ${result}  DMARC_POLICY_QUARANTINE

DMARC PCT ZERO SP QUARANTINE
  ${result} =  Scan Message With Rspamc  ${TESTDIR}/messages/dmarc/pct_none1.eml
  ...  -i  37.48.67.26  --from  foo@mom.za.org
  Check Rspamc  ${result}  DMARC_POLICY_SOFTFAIL

*** Keywords ***
DMARC Setup
  ${PLUGIN_CONFIG} =  Get File  ${TESTDIR}/configs/dmarc.conf
  Set Suite Variable  ${PLUGIN_CONFIG}
  Generic Setup  PLUGIN_CONFIG
