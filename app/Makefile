include ../build/Makefile.param

commLib_obj = rv1106

LIBPATH := ../lib/$(commLib_obj)
INC := -I../inc
INC += -I../include/rtsplib
INC += -I../include

RK_INC :=   -I${MEDIA_PATH}/include/rockchip/  \
			-I${MEDIA_PATH}/include/rkaiq/uAPI/ \
			-I${MEDIA_PATH}/include/rkaiq/uAPI2/ \
			-I${MEDIA_PATH}/include/rkaiq/xcore/ \
			-I${MEDIA_PATH}/include/rkaiq \
			-I${MEDIA_PATH}/include/rkaiq/common \
			-I${MEDIA_PATH}/include/rkaiq/algos/adebayer \
			-I${MEDIA_PATH}/include/rkaiq/algos/ahdr \
			-I${MEDIA_PATH}/include/rkaiq/algos \
			-I${MEDIA_PATH}/include/rkaiq/iq_parser \
			-I${MEDIA_PATH}/include/rkaiq/iq_parser_v2 \
			-I${MEDIA_PATH}/include/rga \
			-I${MEDIA_PATH}/include \
			-I${APP_PATH}/include/freetype2 \
			-I${APP_PATH}/include 
			
CODEC_RKMEDIA_INC :=-I$(commLib_inc)/rkmedia
CODEC_RKMEDIA_INC +=-I${MEDIA_PATH}/include/easymedia 

OBJPATH = ./obj_rv1106

OBJS-COMM += $(OBJPATH)/comm_rtsp.o
OBJS-COMM += $(OBJPATH)/comm_net.o
OBJS-COMM += $(OBJPATH)/comm_gbt.o
OBJS-COMM += $(OBJPATH)/comm_onvif.o
OBJS-COMM += $(OBJPATH)/serial_data_process.o
OBJS-COMM += $(OBJPATH)/comm_app.o
OBJS-COMM += $(OBJPATH)/msg_handle.o
OBJS-COMM += $(OBJPATH)/sample_common.o
OBJS-COMM += $(OBJPATH)/smartir_process.o
OBJS := $(OBJS-COMM) $(OBJS-COMM-CXX)


TARGETLIB :=  $(LIBPATH)/libapp.a
all: $(TARGETLIB)	

$(OBJS-COMM):$(OBJPATH)/%.o:%.c
	@[ -e $(OBJPATH) ] || mkdir $(OBJPATH)
	$(CC) $(CFLAGS) -o $@ -c $< $(RK_INC)  -I$(commLib_inc) -I$(commLib_extra_inc) $(CODEC_RKMEDIA_INC)

$(OBJS-COMM-CXX):$(OBJPATH)/%.o:%.cpp
	@[ -e $(OBJPATH) ] || mkdir $(OBJPATH)
	$(CXX) $(CXXFLAGS) -o $@ -c $< -I$(RK_INC)  -I$(commLib_inc) -I$(commLib_extra_inc) $(CODEC_RKMEDIA_INC)

$(TARGETLIB):$(OBJS)
	@[ -e $(LIBPATH) ] || mkdir $(LIBPATH)
	$(AR) rcv $@ $^
	
clean:
	-rm -rf  $(OBJS) $(TARGETLIB)
	
