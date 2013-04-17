rlm_couchbase
=============

Stores radius accounting data directly into couchbase. You can use any radius attribute as a document key (I suggest Acct-Session-Id or Acct-Unique-Session-Id).
Different status types (start/stop/update) are merged into a single document for easy view writing.

Example from an Aerohive Wireless Access Point:

    {
      "startTimestamp": "Apr 16 2013 19:44:13 CDT",
      "connectInfo": "11na",
      "stopTimestamp": "Apr 16 2013 19:58:46 CDT",
      "sessionId": "0000001B-00000059",
      "authentic": "RADIUS",
      "userName": "jappleseed",
      "nasIpAddress": "172.27.3.153",
      "nasIdentifier": "wifi-test.corp.com",
      "nasPort": 0,
      "calledStationId": "08-EA-44-3D-AF-A8:Corp-SSID",
      "callingStationId": "F0-D1-A9-78-8E-F5",
      "framedIpAddress": "10.11.12.13",
      "nasPortType": "Wireless-802.11",
      "sessionTime": 873,
      "inputPackets": 3284,
      "inputOctets": 460810,
      "outputPackets": 826,
      "outputOctets": 356143
    }

This module was built and tested against freeradius release_3_0_0_beta0

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
