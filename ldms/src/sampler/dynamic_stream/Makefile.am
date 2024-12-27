sbin_PROGRAMS =
pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@

if ENABLE_DYNAMIC_STREAM

libdynamic_stream_sampler_la_SOURCES = dynamic_stream_sampler.c
libdynamic_stream_sampler_la_LIBADD = $(COMMON_LIBADD) \
			     $(top_builddir)/lib/src/ovis_json/libovis_json.la \
			     $(top_builddir)/ldms/src/sampler/libsampler_base.la
pkglib_LTLIBRARIES += libdynamic_stream_sampler.la
dist_man7_MANS += Plugin_dynamic_stream_sampler.man


sbin_PROGRAMS += ldmsd_stream_publish_iterator
ldmsd_stream_publish_iterator_SOURCES = ldmsd_stream_publish_iterator.c
ldmsd_stream_publish_iterator_LDADD = $(COMMON_LIBADD) \
			$(top_builddir)/lib/src/ovis_json/libovis_json.la \
			$(top_builddir)/lib/src/coll/libcoll.la \
			$(top_builddir)/lib/src/ovis_util/libovis_util.la \
			$(top_builddir)/ldms/src/ldmsd/libldmsd_stream.la
ldmsd_stream_publish_iterator_LDFLAGS = $(AM_LDFLAGS) -pthread
###dist_man7_MANS += ldmsd_stream_publish_iterator.man


endif
