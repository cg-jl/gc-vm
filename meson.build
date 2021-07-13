project('babys-first-garbage-collector', 'c', default_options : ['c_std=c11'])

gclib_c = [ 'common.c', 'instruction.c' ]
gclib = library('gclib', sources : gclib_c)
gclib_dep = declare_dependency(link_with : [gclib])

executable('gc', 'gc.c', dependencies :[gclib_dep])
executable('asm', 'asm.c', dependencies : [gclib_dep])
executable('dasm', 'dasm.c', dependencies : [gclib_dep])
