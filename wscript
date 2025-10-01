# -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-

def build(bld):
    module = bld.create_ns3_module('batman', ['internet', 'wifi', 'applications', 'netanim'])
    module.source = [
        'model/batman-routing-protocol.cc',
        'model/batman-packet.cc',
        'helper/batman-helper.cc',
        ]

    module_test = bld.create_ns3_module_test_library('batman')
    module_test.source = [
        'test/batman-test-suite.cc',
        ]
        
    headers = bld(features='ns3header')
    headers.module = 'batman'
    headers.source = [
        'model/batman-routing-protocol.h',
        'model/batman-packet.h',
        'helper/batman-helper.h',
        ]

    if bld.env['ENABLE_EXAMPLES']:
        bld.recurse('examples')

def configure(conf):
    pass
