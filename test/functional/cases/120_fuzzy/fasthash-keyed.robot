*** Settings ***
Suite Setup     Fuzzy Setup Keyed Fasthash
Suite Teardown  Fuzzy Teardown
Resource        lib.robot

*** Test Cases ***
Fuzzy Add
  Fuzzy Multimessage Add Test

Fuzzy Fuzzy
  Fuzzy Multimessage Fuzzy Test

Fuzzy Miss
  Fuzzy Multimessage Miss Test
