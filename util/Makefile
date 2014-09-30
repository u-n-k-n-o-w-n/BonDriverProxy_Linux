.PHONY: all clean distclean dep depend util

SRCDIR = .
LDFLAGS =
LIBS = -ldl
SRCS = test.cpp udpsend.c

include ../Makefile.in

all: test udpsend

test: test.o
ifeq ($(UNAME), Darwin)
	$(CXX) $(CXXFLAGS) -rdynamic -o $@ $^ $(LIBS) -liconv
else
	$(CXX) $(CXXFLAGS) -rdynamic -o $@ $^ $(LIBS)
endif

udpsend: .depend
	$(CC) $(CXXFLAGS) -o $@ udpsend.c

%.$(EXT).o: %.cpp .depend
ifeq ($(UNAME), Darwin)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -pthread -fPIC -c -o $@ $<
else
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<
endif
%.o: %.cpp .depend
ifeq ($(UNAME), Darwin)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -pthread -c -o $@ $<
else
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<
endif

clean:
	$(RM) *.o *.so *.dylib test udpsend .depend
	$(RM) -r *.dSYM
distclean: clean

dep: .depend
depend: .depend

ifneq ($(wildcard .depend),)
include .depend
endif

.depend:
	@$(RM) .depend
	@$(foreach SRC, $(SRCS:%=$(SRCDIR)/%), $(CC) $(SRC) -g0 -MT $(SRC:$(SRCDIR)/%.cpp=%.o) -MM >> .depend;)
