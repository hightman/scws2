# The extension builds its own objects and rule table, independently of ../build.
rules_data.h: $(SCWS2_ROOT)/src/rules.ini $(SCWS2_ROOT)/tools/gen_rules.py
	$(SCWS2_PYTHON) -B $(SCWS2_ROOT)/tools/gen_rules.py --output $@

engine/rules.lo: rules_data.h
scws2.lo: $(srcdir)/php_scws2.h $(srcdir)/compat.h

$(shared_objects_scws2): $(SCWS2_ROOT)/include/scws2.h $(SCWS2_ROOT)/src/utf8.h $(SCWS2_ROOT)/src/xdb.h $(SCWS2_ROOT)/src/rules.h

engine/scws2.lo: $(SCWS2_ROOT)/src/scws2.c
engine/utf8.lo: $(SCWS2_ROOT)/src/utf8.c
engine/xdb.lo: $(SCWS2_ROOT)/src/xdb.c
engine/text.lo: $(SCWS2_ROOT)/src/text.c
engine/text_cache.lo: $(SCWS2_ROOT)/src/text_cache.c
engine/rules.lo: $(SCWS2_ROOT)/src/rules.c
