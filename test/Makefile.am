check_PROGRAMS =

# Run all 'check' test programs
TESTS = $(check_PROGRAMS)

#Build/add config-test to test suite
check_PROGRAMS += config-test
config_test_CPPFLAGS = -Igtest $(GTEST_CPPFLAGS) \
	-DCONFIG_TEST -DSYSCONFDIR=\"\"
config_test_LDFLAGS = -lgtest_main -lgtest $(PTHREAD_LIBS) $(OESDK_TESTCASE_FLAGS)
config_test_SOURCES = config.c
config_test_LDADD = $(top_builddir)/config.o
