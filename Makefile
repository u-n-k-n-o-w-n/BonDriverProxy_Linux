.PHONY: all clean distclean dep depend

SRCDIR = .
LDFLAGS =
LIBS = -ldl
SRCS = BonDriverProxy.cpp BonDriver_Proxy.cpp BonDriver_LinuxPT.cpp BonDriver_DVB.cpp

UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
	CXX = clang++
	CXXFLAGS = -Wall -O2
	SOFLAGS = -dynamiclib
	EXT = dylib
else
	CXX = g++
	CXXFLAGS = -Wall -O2 -pthread
	SOFLAGS = -shared
	EXT = so
endif

all: server client driver
server: BonDriverProxy
client: BonDriver_Proxy.$(EXT)
driver: BonDriver_LinuxPT.$(EXT) BonDriver_DVB.$(EXT)

BonDriverProxy: BonDriverProxy.o
	$(CXX) $(CXXFLAGS) -rdynamic -o $@ $^ $(LIBS)

BonDriver_Proxy.$(EXT): BonDriver_Proxy.$(EXT).o
	$(CXX) $(SOFLAGS) $(CXXFLAGS) -o $@ $^ $(LIBS)

BonDriver_LinuxPT.$(EXT): BonDriver_LinuxPT.$(EXT).o
ifeq ($(UNAME), Darwin)
	$(CXX) $(SOFLAGS) $(CXXFLAGS) -o $@ $^ $(LIBS) -liconv
else
	$(CXX) $(SOFLAGS) $(CXXFLAGS) -o $@ $^ $(LIBS)
endif

BonDriver_DVB.$(EXT): BonDriver_DVB.$(EXT).o
ifeq ($(UNAME), Darwin)
	$(CXX) $(SOFLAGS) $(CXXFLAGS) -o $@ $^ $(LIBS) -liconv
else
	$(CXX) $(SOFLAGS) $(CXXFLAGS) -o $@ $^ $(LIBS)
endif

%.$(EXT).o: %.cpp .depend
ifeq ($(UNAME), Darwin)
	$(CXX) $(CXXFLAGS) -pthread -fPIC -c -o $@ $<
else
	$(CXX) $(CXXFLAGS) -fPIC -c -o $@ $<
endif
%.o: %.cpp .depend
ifeq ($(UNAME), Darwin)
	$(CXX) $(CXXFLAGS) -pthread -c -o $@ $<
else
	$(CXX) $(CXXFLAGS) -c -o $@ $<
endif

clean:
	$(RM) *.o *.so *.dylib BonDriverProxy .depend
distclean: clean

dep: .depend
depend: .depend

ifneq ($(wildcard .depend),)
include .depend
endif

.depend:
	@$(RM) .depend
	@$(foreach SRC, $(SRCS:%=$(SRCDIR)/%), $(CXX) $(SRC) $(CXXFLAGS) -g0 -MT $(SRC:$(SRCDIR)/%.cpp=%.o) -MM >> .depend;)
