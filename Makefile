MODULE_big = pgyaml
EXTENSION = pgyaml
OBJS = pgyaml.o
DATA = pgyaml--1.0.sql
PGFILEDESC = "transform between yaml and jsonb"

REGRESS = pgyaml
# before compile extension please git clone https://github.com/yaml/libyaml.git and build LibYAML
SHLIB_LINK += -lyaml

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pgyaml
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
