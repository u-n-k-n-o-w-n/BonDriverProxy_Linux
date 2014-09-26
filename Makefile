.PHONY: all clean distclean dep depend server client driver util

SRCDIR = .
LDFLAGS =
LIBS = -ldl
SRCS = BonDriverProxy.cpp BonDriver_Proxy.cpp BonDriver_LinuxPT.cpp BonDriver_DVB.cpp sample.cpp

include Makefile.in

all: server client driver sample util
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

sample: sample.o
	$(CXX) $(CXXFLAGS) -rdynamic -o $@ $^ $(LIBS)

util:
	@cd util; make

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
	@$(foreach SRC, $(SRCS:%=$(SRCDIR)/%), $(CXX) $(SRC) $(CXXFLAGS) -g0 -MT $(SRC:$(SRCDIR)/%.cpp=%.o) -MM >> .depend;)
