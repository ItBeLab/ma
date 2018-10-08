##
# switches:
# DEBUG: enables sanity checks within the code
# WITH_PYTHON: compiles libMA in so that it can be imported in python; requires boost & python
# WITH_GPU_SW: compiles a gpu implementation of the SW algorithm; requires libCuda
#

#@todo use pkg-config to find locations...

# location of the Boost Python include files and library
# $(BOOST_ROOT) must be set in the system environment!
BOOST_LIB_PATH = $(BOOST_ROOT)/stage/lib/
BOOST_LIB = boost_python3

 
# target files
TARGET = $(subst .cpp,,$(subst src/,,$(wildcard src/*/*.cpp)))


TARGET_OBJ= \
	$(addprefix obj/,$(addsuffix .o,$(TARGET))) \
	obj/container/qSufSort.co

# flags
CC=gcc
STD=-std=c++17
#CC=clang++-6.0
# use avx instead of sse
ifeq ($(NO_SSE), 1)
	CCFLAGS= -Wall -Werror -fPIC $(STD) -O3 -g
	CFLAGS= -Wall -Werror -fPIC -O3 -g
else
	CCFLAGS= -Wall -Werror -fPIC $(STD) -O3 -g -msse4.1
	CFLAGS= -Wall -Werror -fPIC -O3 -g -msse4.1
endif
LDFLAGS= $(STD)
LDLIBS= -lm -lpthread -lstdc++ -Lcontrib/kswcpp/lib -lkswcpp
INCLUDES= -Iinc -Icontrib/kswcpp/inc

# this adds debug switches
ifeq ($(DEBUG), 1)
	CCFLAGS = -Wall -Werror -fPIC $(STD) -g -DDEBUG_LEVEL=1 -Og
	# we store release and debug objects in different folders
	# no debug version for the ksw library
	TARGET_OBJ= \
		$(addprefix dbg/,$(addsuffix .o,$(TARGET))) \
		obj/container/qSufSort.co
endif

MA_REQUIREMENT= src/cmdMa.cpp
# add configuration for python
ifeq ($(WITH_PYTHON), 1)
	MA_REQUIREMENT += libMA
	LDFLAGS += -shared -Wl,--export-dynamic
	CCFLAGS += -DWITH_PYTHON -DBOOST_ALL_DYN_LINK
	CFLAGS += -DWITH_PYTHON
	LDLIBS += $(PYTHON_LIB) -L$(BOOST_LIB_PATH)
	LDLIBS += $(addprefix -l,$(addsuffix $(BOOST_SUFFIX),$(BOOST_LIB)))
	INCLUDES += -isystem$(PYTHON_INCLUDE)/ -isystem$(BOOST_ROOT)/
else
	MA_REQUIREMENT += $(TARGET_OBJ)
endif


# compile with postgre support enabled
ifeq ($(WITH_POSTGRES), 1)
	POSTGRE_INC_DIR = $(shell pg_config --includedir)
	POSTGRE_LIB_DIR = $(shell pg_config --libdir)
	CCFLAGS += -DWITH_POSTGRES
	INCLUDES += -isystem$(POSTGRE_INC_DIR)
	LDLIBS += -L$(POSTGRE_LIB_DIR) -lpq
endif


# primary target
all: dirs build_ma

# create build directories if not present
dirs:
	mkdir -p obj obj/module obj/container obj/util obj/ksw obj/sample_consensus \
			 dbg dbg/module dbg/container dbg/util dbg/ksw dbg/sample_consensus

# executable target
build_ma: $(MA_REQUIREMENT)
ifeq ($(WITH_PYTHON), 1)
	$(CC) $(CCFLAGS) src/cmdMa.cpp $(INCLUDES) $(LDLIBS) libMA.so -o ma
else
	$(CC) $(CCFLAGS) $(INCLUDES) -c src/cmdMa.cpp -o obj/cmdMa.o
	$(CC) $(LDFLAGS) $(TARGET_OBJ) obj/cmdMa.o $(LDLIBS) -o ma
endif

# library target
libMA: $(TARGET_OBJ)
	$(CC) $(LDFLAGS) $(TARGET_OBJ) $(LDLIBS) -o libMA.so

# special target for the suffix sort
obj/container/qSufSort.co:src/container/qSufSort.c inc/container/qSufSort.h
	$(CC) -c $(CFLAGS) -Iinc $< -o $@

# target for debug object files
dbg/%.o: src/%.cpp inc/%.h
	$(CC) $(CCFLAGS) $(INCLUDES) -c $< -o $@

# target for object files
obj/%.o: src/%.cpp inc/%.h
	$(CC) $(CCFLAGS) $(INCLUDES) -c $< -o $@

# documentation generation
html/index.html: $(wildcard inc/*) $(wildcard inc/*/*) $(wildcard src/*) $(wildcard src/*/*) $(wildcard MA/*.py) doxygen.config
	doxygen doxygen.config

# currently disabled
#install: all
#	pip3 install . --upgrade --no-cache-dir #no pip installation at the moment
#	cp libMA.so /usr/lib
#	pip3 show MA #no pip installation at the moment
#distrib:
#	python setup.py sdist bdist_egg bdist_wheel


clean:
	rm -f -r obj dbg libMA.so html ma
#	rm -r -f dist *.egg-info build

docs: html/index.html

.Phony: all clean docs vid libMA dirs build_ma # install distrib
