.PHONY: all clean distclean dep depend

SRCDIR = .
CXX = g++
CXXFLAGS = -Wall -O2
LDFLAGS =
LIBS = -lpthread -ldl
SRCS = BonDriverProxy.cpp BonDriver_Proxy.cpp BonDriver_LinuxPT.cpp

UNAME := $(shell uname)
ifeq ($(UNAME), Linux)
	SOFLAGS = -shared
	EXT = so
endif
ifeq ($(UNAME), Darwin)
	SOFLAGS = -dynamiclib
	EXT = dylib
endif

all: server client driver
server: BonDriverProxy
client: BonDriver_Proxy.$(EXT)
driver: BonDriver_LinuxPT.$(EXT)

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

%.$(EXT).o: %.cpp .depend
	$(CXX) $(CXXFLAGS) -fPIC -c -o $@ $<
%.o: %.cpp .depend
	$(CXX) $(CXXFLAGS) -c -o $@ $<

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
