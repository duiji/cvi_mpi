
ifeq ($(PARAM_FILE), )
	PARAM_FILE:=../../../Makefile.param
	include $(PARAM_FILE)
endif
include ../../sample.mk

TARGET_DIR	:= $(CURDIR)
TARGET		:= $(TARGET_DIR)/sample_uvc_3d

SEXT		:= c
SDIR		:= $(CURDIR)

INCS		+= -I$(CURDIR)/include
CFLAGS		+= -Wno-unused-parameter

UVC_PATH	=	$(CURDIR)/../uvc
UVC_INC		=	$(UVC_PATH)/include/
UVC_LIB		=	$(UVC_PATH)/lib/

INCS		+= -I$(MW_INC) -I$(SENSOR_LIST_INC) -I$(COMM_INC) -I$(ISP_INC) -I$(UVC_INC) -I$(KERNEL_INC) 
LIBS 		+= -L$(UVC_LIB) -lcvi_uvc 
LIBS 		+= -L$(MW_LIB) -Wl,-Bstatic -Wl,--start-group -lsample -lcvi_bin -lcvi_bin_isp -lvi -lvpss -lvo -lvenc -latomic -lcvi_tdl -lcvikernel -lcnpy -lcvimath -lcviruntime  -lcvi_ive -lz
LIBS 		+=  -lsys -lsns_full $(ISP_LIB) -L$(MW_LIB)/3rd/ -lini -Wl,--end-group -Wl,-Bdynamic
LIBS 		+= -lpthread -lm


DYN_LIBS    +=  -lopencv_core -lopencv_imgproc -lopencv_imgcodecs

# assumptions
# - SEXT is defined (c or cpp)
# - SDIR is defined
# - SRCS is optional
# - if INCS is defined, will be add to CFLAGS or CXXFLAGS
# - if LIBS is defined, will be add to LDFLAGS
SRCS ?= $(shell find $(SDIR) -type f -name "*.$(SEXT)")
ODIR := $(patsubst $(SRCTREE)/%,$(OBJTREE)/%,$(SDIR))

OBJS := $(addprefix $(ODIR)/,$(notdir $(SRCS:.$(SEXT)=.o)))
DEPS := $(addprefix $(ODIR)/,$(notdir $(SRCS:.$(SEXT)=.d)))

CFLAGS		+= $(INCS) -fPIC -lstdc++  -D_GNU_SOURCE -g
CXXFLAGS	+= $(INCS) -fPIC -lstdc++  -D_GNU_SOURCE -g

.PHONY : all clean install
all : $(TARGET)

$(ODIR)/%.o : $(SDIR)/%.c
	$(CC) $(CFLAGS) -o $@ -c $<

$(ODIR)/%.o : $(SDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

$(TARGET) : $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ -Wl,-Bstatic $(LIBS) -Wl,-Bdynamic $(DYN_LIBS)

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

$(INSTALL_PATH)/bin :
	mkdir -p $@

install: $(TARGET) | $(INSTALL_PATH)/bin
	cp $^ $(INSTALL_PATH)/bin

-include $(DEPS)
