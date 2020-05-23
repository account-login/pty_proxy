import os
import glob


CXX = 'g++'
CXXFLAGS = '-Wall -Wextra -g -fdiagnostics-color=always'.split()
CXXFLAGS += '-Os'.split()
LD = 'g++'
LD_FLAGS = '-s'.split()


def o(file):
    return '_out/' + file.replace('.cpp', '.o')


def d(file):
    return '_out/' + file.replace('.cpp', '.d')


def rules(ctx):
    lib_files = [
        'pty.cpp',
        'util.cpp',
        'protocol.cpp',
    ]
    c_files = lib_files + [
        'master.cpp',
        'slave.cpp',
    ]

    # compile objects
    for file in c_files:
        cmd = [CXX, *CXXFLAGS, '-o', o(file), '-c', file, '-MD', '-MP']
        ctx.add_rule(o(file), [file], cmd, d_file=d(file))

    # compile binaries
    all_targets = []
    for exe in ['master', 'slave']:
        exe_file = f'pty_proxy_{exe}'
        o_files = [o(file) for file in lib_files] + [o(f'{exe}.cpp')]
        cmd = [LD, *LD_FLAGS, '-o', exe_file, *o_files]
        ctx.add_rule(exe_file, o_files, cmd)
        all_targets.append(exe_file)

    # all
    ctx.add_rule('all', all_targets, ['true'])
