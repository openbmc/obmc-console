project('iniparser', 'c')

inc = include_directories('src/')

iniparser = shared_library('iniparser',
  'src/dictionary.c',
  'src/iniparser.c',
  include_directories : inc,
  install : true)

dep_iniparser = declare_dependency(include_directories : inc,
  link_with : iniparser)


# Provide variable to the main project
dep_iniparser = dep_iniparser

