build_lib(
  LIBNAME leo-wsn
  SOURCE_FILES
    model/neighbor-table.cc
    model/route-metrics.cc
    model/atpc.cc
    model/wsn-channel.cc
    model/wsn-net-device.cc
    model/wsn-routing-app.cc
  HEADER_FILES
    model/neighbor-table.h
    model/route-metrics.h
    model/atpc.h
    model/wsn-channel.h
    model/wsn-net-device.h
    model/wsn-routing-app.h
    model/nrf52840-current-table.h
  TEST_SOURCES
    test/wsn-routing-app-test-suite.cc
  LIBRARIES_TO_LINK
    ${libcore}
    ${libnetwork}
    ${libapplications}
    ${libenergy}
    ${libmobility}
)

# Note for ns-3 < 3.36 (waf build system, no CMake):
# Copy this directory to <ns-3-dir>/contrib/leo-wsn and instead provide a
# wscript such as:
#
#   def build(bld):
#       module = bld.create_ns3_module('leo-wsn', ['core', 'network',
#                                                   'applications', 'energy',
#                                                   'mobility'])
#       module.source = ['model/neighbor-table.cc', 'model/route-metrics.cc',
#                         'model/atpc.cc', 'model/wsn-channel.cc',
#                         'model/wsn-net-device.cc', 'model/wsn-routing-app.cc']
#       headers = bld(features='ns3header')
#       headers.module = 'leo-wsn'
#       headers.source = ['model/neighbor-table.h', 'model/route-metrics.h',
#                          'model/atpc.h', 'model/wsn-channel.h',
#                          'model/wsn-net-device.h', 'model/wsn-routing-app.h',
#                          'model/nrf52840-current-table.h']
