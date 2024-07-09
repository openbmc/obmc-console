project('iniparser', 'c')

# We create a directory for the include files to be able to include
# iniparser/iniparser.h and not just iniparser.h
# Because in case of iniparser being already installed, it is iniparser/iniparser.h

inc = 'iniparser-include'

run_command('sh', '-c', 'mkdir -p iniparser-include/iniparser', check: true)
run_command('sh', '-c', 'cp -r src/* iniparser-include/iniparser/', check: true)

iniparser = shared_library('iniparser',
  'src/dictionary.c',
  'src/iniparser.c',
  include_directories : inc,
  install : true)

iniparser_dep = declare_dependency(include_directories : inc,
  link_with : iniparser)
