##
## PIN tools
##

##############################################################
#
# Here are some things you might want to configure
#
##############################################################

TARGET_COMPILER?=gnu
ifdef OS
    ifeq (${OS},Windows_NT)
        TARGET_COMPILER=ms
    endif
endif

##############################################################
#
# include *.config files
#
##############################################################

ifeq ($(TARGET_COMPILER),gnu)
    include ../makefile.gnu.config
    CXXFLAGS ?= -Wall -std=gnu++0x -fpermissive -Wno-unknown-pragmas -O3  
#$(DBG) $(OPT)
endif

ifeq ($(TARGET_COMPILER),ms)
    include ../makefile.ms.config
    DBG?=
endif

##############################################################
#
# build rules
#
##############################################################

all: tools 

#TOOLS = $(TOOL_ROOTS:%=$(OBJDIR)%$(PINTOOL_SUFFIX))

tools: $(OBJDIR) $(OBJDIR)cachesim.so 
test: $(OBJDIR) $(TOOL_ROOTS:%=%.test)

OBJS = main.o image.o routine.o caches.o instruction.o utils.o XMLParse.o XMLParser.o 
XMLDIR=XML

## build rules
$(OBJDIR)cachesim.so:	$(OBJS) 	
	${PIN_LD} $(PIN_LDFLAGS) $(LINK_DEBUG) ${LINK_OUT}$@ $(OBJS) ${PIN_LPATHS} $(PIN_LIBS) $(DBG)

$(OBJDIR):
	mkdir -p $(OBJDIR)

main.o:	main.cc utils.hh
	$(CXX) -c  $(CXXFLAGS) $(PIN_CXXFLAGS) ${OUTOPT}$@ $<
image.o:	image.cc utils.hh
	$(CXX) -c  $(CXXFLAGS) $(PIN_CXXFLAGS) ${OUTOPT}$@ $<
routine.o:	routine.cc utils.hh
	$(CXX) -c $(CXXFLAGS) $(PIN_CXXFLAGS) ${OUTOPT}$@ $<
instruction.o:	instruction.cc utils.hh 
	$(CXX) -c $(CXXFLAGS) $(PIN_CXXFLAGS) ${OUTOPT}$@ $<
caches.o:	caches.cc caches.hh predictor.hh 
	$(CXX) -c $(CXXFLAGS) $(PIN_CXXFLAGS) ${OUTOPT}$@ $<
utils.o:	utils.cc utils.hh  
	$(CXX) -c $(CXXFLAGS) $(PIN_CXXFLAGS) ${OUTOPT}$@ $<
XMLParse.o:	$(XMLDIR)/XMLParse.cc $(XMLDIR)/XMLParse.h 
	$(CXX) -c $(CXXFLAGS) $(PIN_CXXFLAGS) ${OUTOPT}$@ $<
XMLParser.o:	$(XMLDIR)/XMLParser.cc $(XMLDIR)/XMLParser.h 
	$(CXX) -c $(CXXFLAGS) $(PIN_CXXFLAGS) ${OUTOPT}$@ $<




#$(TOOLS): $(PIN_LIBNAMES)
#$(TOOLS): $(OBJDIR)%$(PINTOOL_SUFFIX) : $(OBJDIR)%.o 
#	${PIN_LD} $(PIN_LDFLAGS) $(LINK_DEBUG) ${LINK_OUT}$@ $< ${PIN_LPATHS} $(PIN_LIBS) $(DBG)

##
## In this test the tool does repeated mallocs in it's Fini function until it gets a NULL return value
## It tests that PIN's malloc supplied to the tool correctly returns NULL when out of memory
## A separate test is still needed to get PIN to internally exhaust memory and see that PIN
## outputs the "Out of memory" message to the pin logfile before exiting.

memalloc.test: $(OBJDIR)memalloc$(PINTOOL_SUFFIX) memalloc.tested memalloc.failed
	touch memalloc.out; rm memalloc.out
	$(PIN) -t $< -o memalloc.out -- $(TESTAPP) makefile $<.makefile.copy
	grep -q NULL memalloc.out
	rm memalloc.failed memalloc.out
	
memalloc2.test: $(OBJDIR)memalloc2$(PINTOOL_SUFFIX) memalloc2.tested memalloc2.failed
	touch memalloc2.out; rm memalloc2.out
	$(PIN) -t $< -o  memalloc2.out -- $(TESTAPP) makefile $<.makefile.copy
	grep -q OutOfMem memalloc2.out
	rm memalloc2.out memalloc2.failed

## cleaning
clean:
	-rm -rf $(OBJS) $(OBJDIR) *.out *.log *.tested *.failed
