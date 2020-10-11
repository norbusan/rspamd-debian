*** Settings ***
Suite Setup     Surbl Setup
Suite Teardown  Surbl Teardown
Library         ${TESTDIR}/lib/rspamd.py
Resource        ${TESTDIR}/lib/rspamd.robot
Variables       ${TESTDIR}/lib/vars.py

*** Variables ***
${CONFIG}       ${TESTDIR}/configs/plugins.conf
${RSPAMD_SCOPE}  Suite
${URL_TLD}      ${TESTDIR}/../lua/unit/test_tld.dat

*** Test Cases ***
SURBL resolve ip
  Scan File  ${TESTDIR}/messages/url7.eml
  Expect Symbol With Exact Options  URIBL_SBL_CSS  8.8.8.9:example.ru:url
  Expect Symbol With Exact Options  URIBL_XBL  8.8.8.8:example.ru:url
  Expect Symbol With Exact Options  URIBL_PBL  8.8.8.8:example.ru:url

SURBL Example.com domain
  Scan File  ${TESTDIR}/messages/url4.eml
  Expect Symbol  RSPAMD_URIBL
  Expect Symbol  DBL_SPAM
  Do Not Expect Symbol  DBL_PHISH
  Do Not Expect Symbol  URIBL_BLACK

SURBL Example.net domain
  Scan File  ${TESTDIR}/messages/url5.eml
  Expect Symbol  DBL_PHISH
  Do Not Expect Symbol  DBL_SPAM
  Do Not Expect Symbol  RSPAMD_URIBL
  Do Not Expect Symbol  URIBL_BLACK

SURBL Example.org domain
  Scan File  ${TESTDIR}/messages/url6.eml
  Expect Symbol  URIBL_BLACK
  Do Not Expect Symbol  DBL_SPAM
  Do Not Expect Symbol  RSPAMD_URIBL
  Do Not Expect Symbol  DBL_PHISH

SURBL Example.ru domain
  Scan File  ${TESTDIR}/messages/url7.eml
  Expect Symbol  URIBL_GREY
  Expect Symbol  URIBL_RED
  Do Not Expect Symbol  DBL_SPAM
  Do Not Expect Symbol  RSPAMD_URIBL
  Do Not Expect Symbol  DBL_PHISH
  Do Not Expect Symbol  URIBL_BLACK

SURBL Example.ru ZEN domain
  Scan File  ${TESTDIR}/messages/url7.eml
  Expect Symbol  URIBL_SBL_CSS
  Expect Symbol  URIBL_XBL
  Expect Symbol  URIBL_PBL
  Do Not Expect Symbol  URIBL_SBL
  Do Not Expect Symbol  DBL_SPAM
  Do Not Expect Symbol  RSPAMD_URIBL
  Do Not Expect Symbol  DBL_PHISH
  Do Not Expect Symbol  URIBL_BLACK

SURBL Example.com domain image false
  Scan File  ${TESTDIR}/messages/urlimage.eml
  Expect Symbol  RSPAMD_URIBL_IMAGES
  Do Not Expect Symbol  DBL_SPAM
  Do Not Expect Symbol  RSPAMD_URIBL
  Do Not Expect Symbol  DBL_PHISH
  Do Not Expect Symbol  URIBL_BLACK

SURBL @example.com mail html
  Scan File  ${TESTDIR}/messages/mailadr.eml
  Expect Symbol  RSPAMD_URIBL
  Expect Symbol With Exact Options  DBL_SPAM  example.com:email
  Do Not Expect Symbol  RSPAMD_URIBL_IMAGES
  Do Not Expect Symbol  DBL_PHISH
  Do Not Expect Symbol  URIBL_BLACK

SURBL @example.com mail text
  Scan File  ${TESTDIR}/messages/mailadr2.eml
  Expect Symbol  RSPAMD_URIBL
  Expect Symbol With Exact Options  DBL_SPAM  example.com:email
  Do Not Expect Symbol  RSPAMD_URIBL_IMAGES
  Do Not Expect Symbol  DBL_PHISH
  Do Not Expect Symbol  URIBL_BLACK

SURBL example.com not encoded url in subject
  Scan File  ${TESTDIR}/messages/urlinsubject.eml
  Expect Symbol  RSPAMD_URIBL
  Expect Symbol  DBL_SPAM
  Do Not Expect Symbol  DBL_PHISH
  Do Not Expect Symbol  URIBL_BLACK

SURBL example.com encoded url in subject
  Scan File  ${TESTDIR}/messages/urlinsubjectencoded.eml
  Expect Symbol  RSPAMD_URIBL
  Expect Symbol  DBL_SPAM
  Do Not Expect Symbol  DBL_PHISH
  Do Not Expect Symbol  URIBL_BLACK

WHITELIST
  Scan File  ${TESTDIR}/messages/whitelist.eml
  Do Not Expect Symbol  RSPAMD_URIBL
  Do Not Expect Symbol  DBL_SPAM
  Do Not Expect Symbol  RSPAMD_URIBL_IMAGES

EMAILBL full address & domain only
  Scan File  ${TESTDIR}/messages/emailbltext.eml
  Expect Symbol  RSPAMD_EMAILBL_FULL
  Expect Symbol  RSPAMD_EMAILBL_DOMAINONLY

EMAILBL full subdomain address
  Scan File  ${TESTDIR}/messages/emailbltext2.eml
  Expect Symbol  RSPAMD_EMAILBL_FULL

EMAILBL full subdomain address & domain only
  Scan File  ${TESTDIR}/messages/emailbltext3.eml
  Expect Symbol With Exact Options  RSPAMD_EMAILBL_DOMAINONLY  baddomain.com:email
  Expect Symbol With Exact Options  RSPAMD_EMAILBL_FULL  user.subdomain.baddomain.com:email

EMAILBL REPLY TO full address
  Scan File  ${TESTDIR}/messages/replyto.eml
  Expect Symbol  RSPAMD_EMAILBL_FULL
  Do Not Expect Symbol  RSPAMD_EMAILBL_DOMAINONLY

EMAILBL REPLY TO domain only
  Scan File  ${TESTDIR}/messages/replyto2.eml
  Expect Symbol  RSPAMD_EMAILBL_DOMAINONLY
  Do Not Expect Symbol  RSPAMD_EMAILBL_FULL

EMAILBL REPLY TO full subdomain address
  Scan File  ${TESTDIR}/messages/replytosubdomain.eml
  Expect Symbol  RSPAMD_EMAILBL_FULL
  Do Not Expect Symbol  RSPAMD_EMAILBL_DOMAINONLY

SURBL IDN domain
  Scan File  ${TESTDIR}/messages/url8.eml
  Expect Symbol  RSPAMD_URIBL
  Expect Symbol  DBL_SPAM
  Do Not Expect Symbol  DBL_PHISH
  Do Not Expect Symbol  URIBL_BLACK

SURBL IDN Punycode domain
  Scan File  ${TESTDIR}/messages/url9.eml
  Expect Symbol  RSPAMD_URIBL
  Expect Symbol  DBL_SPAM
  Do Not Expect Symbol  DBL_PHISH
  Do Not Expect Symbol  URIBL_BLACK

SURBL html entity&shy
  Scan File  ${TESTDIR}/messages/url10.eml
  Expect Symbol  RSPAMD_URIBL

SURBL url compose map 1
  Scan File  ${TESTDIR}/messages/url11.eml
  Expect Symbol With Exact Options  BAD_SUBDOMAIN  clean.dirty.sanchez.com:url

SURBL url compose map 2
  Scan File  ${TESTDIR}/messages/url12.eml
  Expect Symbol With Exact Options  BAD_SUBDOMAIN  4.very.dirty.sanchez.com:url

SURBL url compose map 3
  Scan File  ${TESTDIR}/messages/url13.eml
  Expect Symbol With Exact Options  BAD_SUBDOMAIN  41.black.sanchez.com:url

*** Keywords ***
Surbl Setup
  ${PLUGIN_CONFIG} =  Get File  ${TESTDIR}/configs/surbl.conf
  Set Suite Variable  ${PLUGIN_CONFIG}
  Generic Setup  PLUGIN_CONFIG

Surbl Teardown
  Normal Teardown
  Terminate All Processes    kill=True
