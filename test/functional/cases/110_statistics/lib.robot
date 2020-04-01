*** Settings ***
Library         ${TESTDIR}/lib/rspamd.py
Resource        ${TESTDIR}/lib/rspamd.robot
Variables       ${TESTDIR}/lib/vars.py

*** Variables ***
${CONFIG}       ${TESTDIR}/configs/stats.conf
${MESSAGE_SPAM}      ${TESTDIR}/messages/spam_message.eml
${MESSAGE_HAM}      ${TESTDIR}/messages/ham.eml
${REDIS_SCOPE}  Suite
${REDIS_SERVER}  ${EMPTY}
${RSPAMD_SCOPE}  Suite
${STATS_HASH}   ${EMPTY}
${STATS_KEY}    ${EMPTY}

*** Keywords ***
Broken Learn Test
  ${result} =  Run Rspamc  -h  ${LOCAL_ADDR}:${PORT_CONTROLLER}  learn_spam  ${MESSAGE_SPAM}
  Check Rspamc  ${result}  Unknown statistics error

Empty Part Test
  Set Test Variable  ${MESSAGE}  ${TESTDIR}/messages/empty_part.eml
  ${result} =  Run Rspamc  -h  ${LOCAL_ADDR}:${PORT_CONTROLLER}  learn_spam  ${MESSAGE}
  Check Rspamc  ${result}
  ${result} =  Scan Message With Rspamc  ${MESSAGE}
  Check Rspamc  ${result}  BAYES_SPAM

Learn Test
  Set Suite Variable  ${RSPAMD_STATS_LEARNTEST}  0
  ${result} =  Run Rspamc  -h  ${LOCAL_ADDR}:${PORT_CONTROLLER}  learn_spam  ${MESSAGE_SPAM}
  ${result} =  Run Rspamc  -h  ${LOCAL_ADDR}:${PORT_CONTROLLER}  learn_ham  ${MESSAGE_HAM}
  Check Rspamc  ${result}
  ${result} =  Scan Message With Rspamc  ${MESSAGE_SPAM}
  Check Rspamc  ${result}  BAYES_SPAM
  ${result} =  Scan Message With Rspamc  ${MESSAGE_HAM}
  Check Rspamc  ${result}  BAYES_HAM
  Set Suite Variable  ${RSPAMD_STATS_LEARNTEST}  1

Relearn Test
  Run Keyword If  ${RSPAMD_STATS_LEARNTEST} == 0  Fail  "Learn test was not run"
  ${result} =  Run Rspamc  -h  ${LOCAL_ADDR}:${PORT_CONTROLLER}  learn_ham  ${MESSAGE_SPAM}
  Check Rspamc  ${result}
  ${result} =  Scan Message With Rspamc  ${MESSAGE_SPAM}
  ${pass} =  Run Keyword And Return Status  Check Rspamc  ${result}  BAYES_HAM
  Run Keyword If  ${pass}  Pass Execution  What Me Worry
  Should Not Contain  ${result.stdout}  BAYES_SPAM

Redis Statistics Setup
  ${tmpdir} =  Make Temporary Directory
  Set Suite Variable  ${TMPDIR}  ${tmpdir}
  Run Redis
  Generic Setup  TMPDIR=${tmpdir}

Redis Statistics Teardown
  Normal Teardown
  Shutdown Process With Children  ${REDIS_PID}
