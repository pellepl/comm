ifndef comm
$(warn defaulting path to generic comm module, comm variable not set)
comm = ../generic/comm
endif

FLAGS	+= -DCONFIG_BUILD_COMM
INC	+= -I${comm}/src
CPATH	+= ${comm}/src
CFILES	+= comm.c
CFILES	+= comm_phy.c
CFILES	+= comm_lnk.c
CFILES	+= comm_nwk.c
CFILES	+= comm_tra.c
CFILES	+= comm_app.c
