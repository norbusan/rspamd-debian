/* Copyright (c) 2014, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "rdns.h"
#include "rdns_ev.h"
#include "rdns_event.h"
#include <stdio.h>
#include <assert.h>

static int remain_tests = 0;



static void
rdns_regress_callback (struct rdns_reply *reply, void *arg)
{
	struct rdns_reply_entry *entry;
	char out[INET6_ADDRSTRLEN + 1];
	const struct rdns_request_name *name;

	if (reply->code == RDNS_RC_NOERROR) {
		entry = reply->entries;
		while (entry != NULL) {
			if (entry->type == RDNS_REQUEST_A) {
				inet_ntop (AF_INET, &entry->content.a.addr, out, sizeof (out));
				printf ("%s has A record %s\n", (char *)arg, out);
			}
			else if (entry->type == RDNS_REQUEST_AAAA) {
				inet_ntop (AF_INET6, &entry->content.aaa.addr, out, sizeof (out));
				printf ("%s has AAAA record %s\n", (char *)arg, out);
			}
			else if (entry->type == RDNS_REQUEST_SOA) {
				printf ("%s has SOA record %s %s %u %d %d %d\n",
						(char *)arg,
						entry->content.soa.mname,
						entry->content.soa.admin,
						entry->content.soa.serial,
						entry->content.soa.refresh,
						entry->content.soa.retry,
						entry->content.soa.expire);
			}
			else if (entry->type == RDNS_REQUEST_TLSA) {
				char *hex, *p;
				unsigned i;

				hex = malloc (entry->content.tlsa.datalen * 2 + 1);
				p = hex;

				for (i = 0; i < entry->content.tlsa.datalen; i ++) {
					sprintf (p, "%02x",  entry->content.tlsa.data[i]);
					p += 2;
				}

				printf ("%s has TLSA record (%d %d %d) %s\n",
						(char *)arg,
						(int)entry->content.tlsa.usage,
						(int)entry->content.tlsa.selector,
						(int)entry->content.tlsa.match_type,
						hex);

				free (hex);
			}
			entry = entry->next;
		}
	}
	else {
		name = rdns_request_get_name (reply->request, NULL);
		printf ("Cannot resolve %s record for %s: %s\n",
				rdns_strtype (name->type),
				(char *)arg,
				rdns_strerror (reply->code));
	}

	if (--remain_tests == 0) {
		printf ("End of test cycle\n");
		rdns_resolver_release (reply->resolver);
	}
}

static void
rdns_test_a (struct rdns_resolver *resolver)
{
	static char *names[] = {
			//"google.com",
			"github.com",
			"freebsd.org",
			//"kernel.org",
			"www.ник.рф",
			NULL
	};
	char **cur;

	for (cur = names; *cur != NULL; cur ++) {
		rdns_make_request_full (resolver, rdns_regress_callback, *cur, 1.0, 2, 1,
				*cur, RDNS_REQUEST_AAAA);
		rdns_make_request_full (resolver, rdns_regress_callback, *cur, 1.0, 2, 1,
				*cur, RDNS_REQUEST_A);
		remain_tests += 2;
	}
}

static void
rdns_test_tlsa (struct rdns_resolver *resolver)
{
	static char *names[] = {
			"_25._tcp.mail6.highsecure.ru",
			"_25._tcp.open.NLnetLabs.nl",
			NULL
	};
	char **cur;

	for (cur = names; *cur != NULL; cur ++) {
		rdns_make_request_full (resolver, rdns_regress_callback, *cur, 1.0, 2, 1,
				*cur, RDNS_REQUEST_TLSA);
		remain_tests ++;
	}
}

int
main (int argc, char **argv)
{
	struct rdns_resolver *resolver_ev, *resolver_event;
	struct ev_loop *loop;
	struct event_base *base;

	loop = ev_default_loop (0);
	base = event_init ();

	resolver_ev = rdns_resolver_new ();
	rdns_bind_libev (resolver_ev, loop);
	rdns_resolver_set_log_level (resolver_ev, RDNS_LOG_DEBUG);
	rdns_resolver_set_max_io_uses (resolver_ev, 1, 0.1);
	assert (rdns_resolver_parse_resolv_conf (resolver_ev, "/etc/resolv.conf"));

	resolver_event = rdns_resolver_new ();
	rdns_bind_libevent (resolver_event, base);
	rdns_resolver_set_log_level (resolver_event, RDNS_LOG_DEBUG);
	/* Google and opendns */
	assert (rdns_resolver_add_server (resolver_event, "127.0.0.1", 53, 0, 8));
	//assert (rdns_resolver_add_server (resolver_event, "8.8.8.8", 53, 0, 1));

	assert (rdns_resolver_init (resolver_ev));
	assert (rdns_resolver_init (resolver_event));

	rdns_test_a (resolver_ev);
	rdns_test_tlsa (resolver_ev);
	ev_loop (loop, 0);
	ev_loop_destroy (loop);

	rdns_test_a (resolver_event);
	rdns_test_tlsa (resolver_event);
	event_base_loop (base, 0);
	event_base_free (base);

	return 0;
}
