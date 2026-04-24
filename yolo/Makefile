include ../build/Makefile.param

TARGET = yolo_test

# 1. 增加头文件搜索路径，确保能搜到 ../ai 里的 .h
RK_INC := -I../include \
          -I../ai \
          -I${MEDIA_PATH}/include \
          -I${MEDIA_PATH}/include/rga \
          -I${MEDIA_PATH}/include/rockchip

LIBPATH := -L../lib/rv1106 -L../lib/comm
LIBS := -lrknnmrt -lrga -lrockit -lrockchip_mpp -lpthread -lm -ldl -lstdc++

OBJPATH = ./obj_rv1106

# 2. 这里的源文件不仅要包含当前的 main.cpp，还要包含 ai 目录下的实现类
SRCS = ./main.cpp \
       ../ai/yolo26.cpp \
	   ../ai/yolov8.cpp \
       ../ai/rknn_model.cpp \
       ../ai/misc.cpp

# 这里的逻辑是将所有的 .cpp 映射到本地的 obj 文件夹下
OBJS = $(patsubst %.cpp, $(OBJPATH)/%.o, $(notdir $(SRCS)))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CFLAGS) -o $@ $^ $(LIBPATH) $(LIBS)
	$(STRIP) $(TARGET)
	cp $(TARGET) ../build

# 编译本地的 main.cpp
$(OBJPATH)/main.o: ./main.cpp
	@[ -e $(OBJPATH) ] || mkdir $(OBJPATH)
	$(CXX) $(CFLAGS) $(RK_INC) -c $< -o $@

# 编译 ai 目录下的源文件
$(OBJPATH)/%.o: ../ai/%.cpp
	@[ -e $(OBJPATH) ] || mkdir $(OBJPATH)
	$(CXX) $(CFLAGS) $(RK_INC) -c $< -o $@

clean:
	rm -rf $(OBJPATH) $(TARGET)