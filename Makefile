.PHONY: all clean distclean dep depend server client driver util

include Makefile.in

SRCDIR = .
LDFLAGS =
LIBS = -ldl
SRCS = BonDriverProxy.cpp sample.cpp
SOSRCS = BonDriver_Proxy.cpp
ifneq ($(UNAME), Darwin)
	SOSRCS += BonDriver_LinuxPT.cpp BonDriver_DVB.cpp
endif

ifeq ($(UNAME), Darwin)
all: server client sample util
else
all: server client driver sample util
endif
server: BonDriverProxy
client: BonDriver_Proxy.$(EXT)
driver: BonDriver_LinuxPT.$(EXT) BonDriver_DVB.$(EXT)

BonDriverProxy: BonDriverProxy.o
	$(CXX) $(CXXFLAGS) -rdynamic -o $@ $^ $(LIBS)

BonDriver_Proxy.$(EXT): BonDriver_Proxy.$(EXT).o
	$(CXX) $(SOFLAGS) $(CXXFLAGS) -o $@ $^ $(LIBS)

BonDriver_LinuxPT.$(EXT): BonDriver_LinuxPT.$(EXT).o
	$(CXX) $(SOFLAGS) $(CXXFLAGS) -o $@ $^ $(LIBS)

BonDriver_DVB.$(EXT): BonDriver_DVB.$(EXT).o
	$(CXX) $(SOFLAGS) $(CXXFLAGS) -o $@ $^ $(LIBS)

sample: sample.o
	$(CXX) $(CXXFLAGS) -rdynamic -o $@ $^ $(LIBS)

util:
	@cd util; make

%.$(EXT).o: %.cpp .depend
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(ADDCOMPILEFLAGS) -fPIC -c -o $@ $<

%.o: %.cpp .depend
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(ADDCOMPILEFLAGS) -c -o $@ $<

clean:
	$(RM) *.o *.so *.dylib BonDriverProxy sample .depend
	$(RM) -r *.dSYM
	@cd util; make clean
distclean: clean

dep: .depend
depend: .depend

ifneq ($(wildcard .depend),)
include .depend
endif

.depend:
	@$(RM) .depend
	@$(foreach SRC, $(SRCS:%=$(SRCDIR)/%), $(CXX) -g0 -MT $(basename $(SRC)).o -MM $(SRC) >> .depend;)
	@$(foreach SRC, $(SOSRCS:%=$(SRCDIR)/%), $(CXX) -g0 -MT $(basename $(SRC)).$(EXT).o -MM $(SRC) >> .depend;)
