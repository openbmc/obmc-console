check_PROGRAMS = 

TESTS = $(check_PROGRAMS)
#LOG_COMPILER = ./test.sh

#Build/add config-test to test suite
check_PROGRAMS += config-test
config_test_CPPFLAGS = -Igtest $(GTEST_CPPFLAGS) \
	-DCONFIG_TEST -DSYSCONFDIR=\"\"
config_test_SOURCES = config.c
