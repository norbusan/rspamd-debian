##Rapid DNS resolver

Asynchronous pluggable DNS resolver with many features.

##Features list

- can work with libev and libevent asynchronous cores (and this could be extended in future)
- follows secure resolver principles:
	+ randomize source port for outgoing requests (based on periodic IO sockets updates)
	+ use secure random generator for DNS ID
	+ compare request and reply
	+ support pool of sockets per server
- supports multiple DNS servers (unlimited amount actually)
- can parse resolv.conf file
- supports edns0 by default
- can include multiple queries in a packet
- RDNS supports plugins and [DNSCurve](http://dnscurve.org) in particular to encrypt DNS traffic
- can automatically handle IDN queries (encoded in UTF8)


##TODO list

- DNSSec support
- Recursion support
- TCP fallback
- Documentation and tests

## Typical usage example

Here is a simple example of using RDNS library with libev backend.

~~~c
#include <stdlib.h>
#include <stdio.h>
#include "rdns.h"
#include "rdns_curve.h"
#include "rdns_ev.h"

static int remain_tests = 0;

static void
rdns_regress_callback (struct rdns_reply *reply, void *arg)
{
	printf ("got result for host: %s\n", (const char *)arg);

	if (--remain_tests == 0) {
		rdns_resolver_release (reply->resolver);
	}
}

static void
rdns_test_a (struct rdns_resolver *resolver)
{
	char *names[] = {
			"google.com",
			"github.com",
			"freebsd.org",
			"kernel.org",
			"www.ник.рф",
			NULL
	};
	char **cur;

	for (cur = names; *cur != NULL; cur ++) {
		rdns_make_request_full (resolver, rdns_regress_callback, *cur, 1.0, 2, 1, *cur, RDNS_REQUEST_A);
		remain_tests ++;
	}
}

int
main(int argc, char **argv)
{
	struct rdns_resolver *resolver_ev;
	struct ev_loop *loop;

	loop = ev_default_loop (0);
	resolver_ev = rdns_resolver_new ();
	rdns_bind_libev (resolver_ev, loop);

	rdns_resolver_add_server (resolver_ev, argv[1], strtoul (argv[2], NULL, 10), 0, 8);

	rdns_resolver_init (resolver_ev);

	rdns_test_a (resolver_ev);
	ev_loop (loop, 0);

	return 0;
}
~~~
