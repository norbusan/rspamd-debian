Overview.
=========

This is a simple control interface for `rspamd` spam filtering system.
It provides basic functions for setting metric actions, scores,
viewing statistic and learning.

Rspamd setup.
=============

It is required to configure dynamic settings to store configured values.
Basically this can be done by providing the following line in options settings:

~~~nginx
options {
...
        dynamic_conf = /var/lib/rspamd/rspamd_dynamic;
...
}
~~~

Please note that this path must have write access for `rspamd` user.

Then the controller worker should be configured:

~~~nginx
worker {
        type = "controller";
        bind_socket = "localhost:11334";
        count = 1;
        password = "q1";
        enable_password = "q2";
        secure_ip = "127.0.0.1"; # Allows to use *all* commands from this IP
        static_dir = "${WWWDIR}";
}
~~~

Password option should be changed for sure for your specific configuration.


Interface setup.
================

Interface itself is written in pure HTML5/js and, hence, it requires zero setup.
Just enter a password for webui access and you are ready.

Contact information.
====================

Rspamd interface is distributed under the terms of [Apache 2.0 license](http://www.apache.org/licenses/LICENSE-2.0). For all questions related to this
product please email to vsevolod <at> highsecure.ru.
