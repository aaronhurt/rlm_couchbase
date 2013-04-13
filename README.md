rlm_couchbase
=============

Stores radius accounting data directly into couchbase. You can use any radius attribute as a document key.

Built and tested against freeradius release_3_0_0_beta0

To Use
------

Pull freeradius release_3_0_0_beta0 and checkout this module under src/modules.  Then enable and compile as usual.
You will need libcouchbase >= 2.0 installed with a valid libio module.

Configuration
-------------

    couchbase {
        ## attribute to use for the document key
        dockey = "Acct-Unique-Session-Id"

        ## couchbase host
        host = "cbdb-host.com:8091"

        ## couchbase bucket name
        bucket = "radius"

        ## username for bucket
        #user = "username"

        ## bucket password
        #pass = "password"
    }

Disclaimer
----------

This module was written for a specific use case to collect data from thousands of wireless access points.  YMMV.
