include ../build/Makefile.param

commLib_obj = rv1106

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

LIBS := -lsys
LIBS += -lapp
LIBS += -lAI
LIBS += -lasound
#LIBS += -lBase
LIBS += -lrtspServ
LIBS += -lcodec2
LIBS += -lnv12jpg
LIBS += -lmcu
LIBS += -ltask
LIBS += -lnetwork
LIBS += -lparam
LIBS += -llog
LIBS += -lgpio
LIBS += -lptz
LIBS += -lmisc
LIBS += -lOnvif
LIBS += -lencnet
#LIBS += -lsetting
LIBS += -latLib
#LIBS += -lpro
LIBS += -lvoip
#cgiparterner uses gb28181(ST_GBT_RegState)
LIBS += -lwebsvc
LIBS += -lgb28181 -leXosip2 -lmxml -losip2 -losipparser2
LIBS += -lresolv
LIBS +=    -lrockit -lrockchip_mpp -lrkaiq -lrga -lrksysutils -lrockiva -lrknnmrt -lfreetype  -lpthread -lm -Wl,-rpath-link -ldl
LIBS +=	 -ljson-c
LIBS += -lhd
LIBS += -lparted
LIBS += -lparted-fs-resize
LIBS += -latLib
LIBS += -lcurl
LIBS += -lcgi_server
LIBS += -lfaac
LIBS += -lwifi
LIBS += -lcurl
LIBS += -lrtmp
LIBS += -lvendor_storage
#LIBS += -lwebrtc_interface
#LIBS += -lwebrtc-stream
LIBS += -lcommLib
LIBS += -lstreamCache
LIBS += -lssl
LIBS += -lcrypto
LIBS += -liwlist
#LIBS += -limu

LIBS += -lrecorder
LIBS += -lminilog
LIBPATH := -L../lib/$(commLib_obj)
LIBPATH += -L../lib/comm
LIBS += -lsmartIr
#LIBPATH += -L/data/rockchip/RV1106G/media/lib
#LIBPATH += -L/data/rockchip/RV1106G/app/lib
INC := ../include
OBJPATH = ./obj_rv1106

$(OBJPATH)/%.o:./%.cpp
	@[ -e $(OBJPATH) ] || mkdir $(OBJPATH)
	@echo "$(commLib_inc)"
	@echo "$(commLib_extra_inc)"
	$(CXX) $(CFLAGS) -o $@ -c $< -I$(INC) $(RK_INC) -I$(commLib_inc) -I$(commLib_extra_inc)

SRCS = $(wildcard ./*.cpp)
OBJS = $(SRCS:./%.cpp=./$(OBJPATH)/%.o)
TARGETLIB :=  $(OBJPATH)/rv1106_ipc_custom

all:$(TARGETLIB)
$(TARGETLIB):$(OBJS)
	@echo "build rv1106"	
	$(CXX) $(CFLAGS) -o $@ $^ $(LIBS) -lm -lpthread -lrt $(LIBPATH) -lstdc++
	$(STRIP) $(TARGETLIB)
	cp $(TARGETLIB)  ../build

clean:
	-rm -rf $(OBJS) $(TARGETLIB)
