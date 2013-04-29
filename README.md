rlm_couchbase
=============

Stores radius accounting data directly into couchbase. You can use any radius attribute as a document key (I suggest Acct-Session-Id or Acct-Unique-Session-Id).
Different status types (start/stop/update) are merged into a single document for easy view writing.

Example from an Aerohive Wireless Access Point:

    {
      "startTimestamp": "Apr 23 2013 17:52:22 CDT",
      "connectInfo": "11ng",
      "stopTimestamp": "Apr 23 2013 18:05:52 CDT",
      "sessionId": "5168E1E1-00000613",
      "lastStatus": "Stop",
      "authentic": "RADIUS",
      "userName": "jappleseed",
      "nasIpAddress": "10.11.12.13",
      "nasIdentifier": "ap01-site.blargs.com",
      "nasPort": 0,
      "calledStationId": "40-18-B1-01-3C-54:Corp-SSID",
      "callingStationId": "C0-9F-42-07-4E-9C",
      "sessionTime": 810,
      "inputPackets": 410,
      "inputOctets": 42348,
      "inputGigawords": 0,
      "outputOctets": 70692,
      "outputGigawords": 0,
      "outputPackets": 108,
      "lastUpdated": "Apr 23 2013 18:05:52 CDT"
    }


To Use
------

Pull freeradius-server master and clone this module under src/modules.  Then enable and compile as usual.
You will need libcouchbase >= 2.0 installed with a valid libio module.  You will also need the json-c library installed and available.

Configuration
-------------

    couchbase {
        ## attribute to use for the document key
        key = "Acct-Session-Id"

        ## couchbase host or hosts - multiple hosts should be semi-colon separated
        ## ports are optional if servers are listening on the standard port
        host = "cb01-blargs.com:9091;cb02-blargs.com;cb03-blargs.com"

        ## couchbase bucket name
        bucket = "radius"

        ## username for bucket
        #user = "username"

        ## bucket password
        #pass = "password"

        ## document expire time in seconds (0 = never)
        expire = 2592000

        ## map attribute names to json element names
        ## attributes not in this map will not be recorded
        map = "{ \
                \"Acct-Session-Id\": \"sessionId\", \
                \"Acct-Status-Type\": \"lastStatus\", \
                \"Acct-Authentic\": \"authentic\", \
                \"User-Name\": \"userName\", \
                \"NAS-IP-Address\": \"nasIpAddress\", \
                \"NAS-Identifier\": \"nasIdentifier\", \
                \"NAS-Port\": \"nasPort\", \
                \"Called-Station-Id\": \"calledStationId\", \
                \"Calling-Station-Id\": \"callingStationId\", \
                \"Framed-IP-Address\": \"framedIpAddress\", \
                \"Nas-Port-Type\": \"nasPortType\", \
                \"Connect-Info\": \"connectInfo\", \
                \"Acct-Session-Time\": \"sessionTime\", \
                \"Acct-Input-Packets\": \"inputPackets\", \
                \"Acct-Output-Packets\": \"outputPackets\", \
                \"Acct-Input-Octets\": \"inputOctets\", \
                \"Acct-Output-Octets\": \"outputOctets\", \
                \"Acct-Input-Gigawords\": \"inputGigawords\", \
                \"Acct-Output-Gigawords\": \"outputGigawords\", \
                \"Event-Timestamp\": \"lastUpdated\" \
        }"
    }

Notes
-----

This module was tested to handle thousands of radius requests in a short period of time from multiple (hundreds) of Aerohive Access Points pointing
to a freeradius installation for accounting.  YMMV.

This module was built and tested against freeradius-server master branch as of the latest commit to this branch.