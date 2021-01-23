*** Settings ***
Suite Setup     SPF Setup
Suite Teardown  Simple Teardown
Library         ${TESTDIR}/lib/rspamd.py
Resource        ${TESTDIR}/lib/rspamd.robot
Variables       ${TESTDIR}/lib/vars.py

*** Variables ***
${CONFIG}        ${TESTDIR}/configs/plugins.conf
${RSPAMD_SCOPE}  Suite
${URL_TLD}       ${TESTDIR}/../../contrib/publicsuffix/effective_tld_names.dat

*** Test Cases ***
SPF FAIL UNRESOLVEABLE INCLUDE
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=37.48.67.26  From=x@fail3.org.org.za
  Expect Symbol  R_SPF_FAIL

SPF DNSFAIL FAILED INCLUDE UNALIGNED
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=8.8.8.8  From=x@fail2.org.org.za
  Expect Symbol  R_SPF_DNSFAIL
  Expect Symbol  DMARC_POLICY_SOFTFAIL

SPF ALLOW UNRESOLVEABLE INCLUDE
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=8.8.8.8  From=x@fail3.org.org.za
  Expect Symbol  R_SPF_ALLOW

SPF ALLOW FAILED INCLUDE
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=8.8.4.4  From=x@fail2.org.org.za
  Expect Symbol  R_SPF_ALLOW

SPF NA NA
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=8.8.8.8  From=x@za
  Expect Symbol  R_SPF_NA

SPF NA NOREC
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=8.8.8.8  From=x@co.za
  Expect Symbol  R_SPF_NA

SPF NA NXDOMAIN
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=8.8.8.8  From=x@zzzzaaaa
  Expect Symbol  R_SPF_NA

SPF PERMFAIL UNRESOLVEABLE REDIRECT
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=8.8.8.8  From=x@fail4.org.org.za
  Expect Symbol  R_SPF_PERMFAIL

SPF REDIRECT NO USEABLE ELEMENTS
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=8.8.8.8  From=x@fail10.org.org.za
  Expect Symbol  R_SPF_PERMFAIL

SPF DNSFAIL FAILED REDIRECT
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=8.8.8.8  From=x@fail1.org.org.za
  Expect Symbol  R_SPF_DNSFAIL

SPF PERMFAIL NO USEABLE ELEMENTS
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=8.8.8.8  From=x@fail5.org.org.za
  Expect Symbol  R_SPF_PERMFAIL

SPF FAIL
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=8.8.8.8  From=x@example.net
  Expect Symbol  R_SPF_FAIL

SPF FAIL UNRESOLVEABLE MX
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=1.2.3.4  From=x@fail6.org.org.za
  Expect Symbol  R_SPF_FAIL

SPF FAIL UNRESOLVEABLE A
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=1.2.3.4  From=x@fail7.org.org.za
  Expect Symbol  R_SPF_FAIL

SPF DNSFAIL FAILED A
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=1.2.3.4  From=x@fail8.org.org.za
  Expect Symbol  R_SPF_DNSFAIL

SPF DNSFAIL FAILED MX
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=1.2.3.4  From=x@fail9.org.org.za
  Expect Symbol  R_SPF_DNSFAIL

SPF DNSFAIL FAILED RECORD
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=1.2.3.4  From=x@www.dnssec-failed.org
  Expect Symbol  R_SPF_DNSFAIL

SPF PASS INCLUDE
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=8.8.8.8  From=x@pass1.org.org.za
  Expect Symbol  R_SPF_ALLOW

SPF PTRS
  Scan File  /dev/null
  ...  IP=88.99.142.95  From=foo@crazyspf.cacophony.za.org
  Expect Symbol  R_SPF_ALLOW
  Scan File  /dev/null
  ...  IP=128.66.0.1  From=foo@crazyspf.cacophony.za.org
  Expect Symbol  R_SPF_FAIL
  Scan File  /dev/null
  ...  IP=209.85.216.182  From=foo@crazyspf.cacophony.za.org
  Expect Symbol  R_SPF_FAIL
  #Scan File  /dev/null
  #...  IP=98.138.91.166  From=foo@crazyspf.cacophony.za.org
  #Expect Symbol  R_SPF_ALLOW
  #Scan File  /dev/null
  #...  IP=98.138.91.167  From=foo@crazyspf.cacophony.za.org
  #Expect Symbol  R_SPF_ALLOW
  #Scan File  /dev/null
  #...  IP=98.138.91.168  From=foo@crazyspf.cacophony.za.org
  #Expect Symbol  R_SPF_ALLOW

SPF PERMFAIL REDIRECT WITHOUT SPF
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim4.eml
  ...  IP=192.0.2.1  From=a@fail1.org.org.za
  Expect Symbol  R_SPF_DNSFAIL

SPF EXTERNAL RELAY
  Scan File  ${TESTDIR}/messages/external_relay.eml
  Expect Symbol With Score And Exact Options  R_SPF_ALLOW  1.0  +ip4:37.48.67.26

SPF UPPERCASE
  Scan File  ${TESTDIR}/messages/dmarc/bad_dkim1.eml
  ...  IP=8.8.8.8  From=x@fail11.org.org.za
  Expect Symbol  R_SPF_ALLOW

*** Keywords ***
SPF Setup
  ${PLUGIN_CONFIG} =  Get File  ${TESTDIR}/configs/dmarc.conf
  Set Suite Variable  ${PLUGIN_CONFIG}
  Generic Setup  PLUGIN_CONFIG
