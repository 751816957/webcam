all: webcam_server webcam_shower

CXXFLAGS=	-c -g -O0 -fPIC -fpermissive

OBJS_SERVER=	capture.o vcompress.o sender.o server.o
OBJS_SHOWER= 	vshow.o recver.o shower.o sender.o
LIBS_SERVER=	-lavcodec -lswscale -lavutil -lx264 -lpthread
LIBS_SHOWER=    -lavcodec -lswscale -lavutil -lX11 -lXext

.cpp.o:
	$(CXX) $(CXXFLAGS) $<

webcam_server: $(OBJS_SERVER)
	$(CXX) -o $@ $^ $(LIBS_SERVER)

webcam_shower: $(OBJS_SHOWER)
	$(CXX) -o $@ $^ $(LIBS_SHOWER)

clean:
	rm -f *.o
	rm -f webcam_server
	rm -f webcam_shower
	rm -f *.out
	rm -f *.cpp~

