comm = ../generic/comm/src
FLAGS	+= -DCONFIG_BUILD_COMM
INC	+= -I${comm}
CPATH	+= ${comm}
CFILES	+= comm.c
CFILES	+= comm_phy.c
CFILES	+= comm_lnk.c
CFILES	+= comm_nwk.c
CFILES	+= comm_tra.c
CFILES	+= comm_app.c
