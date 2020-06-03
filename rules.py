import os
import glob


CXX = 'g++'
CXXFLAGS = '-Wall -Wextra -g -pthread -fdiagnostics-color=always'.split()
CXXFLAGS += '-Os'.split()
LD = 'g++'
LD_FLAGS = '-s -pthread'.split()


def o(file):
    return '_out/' + file + '.o'


def d(file):
    return '_out/' + file + '.d'


def rules(ctx):
    lib_files = [
        'pty.cpp',
        'util.cpp',
        'protocol.cpp',
        'base64.c'
    ]
    c_files = lib_files + [
        'master.cpp',
        'slave.cpp',
        'doctest.cpp',
        'test_base64.cpp',
    ]

    # all
    ctx.add_rule('all', ['pty_proxy_master', 'pty_proxy_slave'], ['true'])

    # compile objects
    for file in c_files:
        cmd = [CXX, *CXXFLAGS, '-o', o(file), '-c', file, '-MD', '-MP']
        ctx.add_rule(o(file), [file], cmd, d_file=d(file))

    # compile binaries
    for exe in ['master', 'slave']:
        exe_file = f'pty_proxy_{exe}'
        o_files = [o(file) for file in lib_files] + [o(f'{exe}.cpp')]
        cmd = [LD, *LD_FLAGS, '-o', exe_file, *o_files]
        ctx.add_rule(exe_file, o_files, cmd)

    # tests
    exe_file = 'test_base64'
    o_files = [o('test_base64.cpp'), o('base64.c'), o('doctest.cpp')]
    cmd = [LD, *LD_FLAGS, '-o', exe_file, *o_files]
    ctx.add_rule(exe_file, o_files, cmd)
