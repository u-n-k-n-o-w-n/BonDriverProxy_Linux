.PHONY: all clean distclean dep depend

include ../Makefile.in

SRCDIR = .
LDFLAGS =
LIBS = -ldl
ifeq ($(UNAME), Darwin)
	LIBS += -liconv
endif
SRCS = test.cpp udpsend.c

all: test udpsend

test: test.o
	$(CXX) $(CXXFLAGS) -rdynamic -o $@ $^ $(LIBS)

udpsend: udpsend.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.cpp .depend
ifeq ($(UNAME), Darwin)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -pthread -c -o $@ $<
else
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<
endif

%.o: %.c .depend
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

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
	@$(foreach SRC, $(SRCS:%=$(SRCDIR)/%), $(CC) -g0 -MT $(basename $(SRC)).o -MM $(SRC) >> .depend;)
