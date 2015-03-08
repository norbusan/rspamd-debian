SET(OTTERYSRC            chacha_merged.c
                         ottery.c
                         ottery_cpuinfo.c
                         ottery_entropy.c
                         ottery_global.c)

ADD_LIBRARY(ottery OBJECT ${OTTERYSRC})
SET_TARGET_PROPERTIES(ottery PROPERTIES COMPILE_FLAGS "-fPIC")