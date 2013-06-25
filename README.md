rlm_couchbase
=============

Stores radius accounting data directly into couchbase. You can use any radius attribute as a document key (I suggest Acct-Session-Id or Acct-Unique-Session-Id).
Different status types (start/stop/update) are merged into a single document for easy view writing.

Example from an Aerohive Wireless Access Point:

    {
       "docType": "radacct",
       "startTimestamp": "Jun 24 2013 17:22:06 CDT",
       "connectInfo": "11ng",
       "stopTimestamp": "Jun 24 2013 17:22:26 CDT",
       "sessionId": "5176C004-000018C0",
       "lastStatus": 2,
       "authentic": 1,
       "userName": "DOMAIN\\barney",
       "nasIpAddress": "1.2.3.4",
       "nasIdentifier": "ap1.blargs.net",
       "nasPort": 0,
       "calledStationId": "08-EA-44-3D-AF-94:ENA-WIFI",
       "framedIpAddress": "1.2.3.5",
       "callingStationId": "F0-D1-A9-78-8E-F5",
       "sessionTime": 20,
       "inputPackets": 321,
       "inputOctets": 42074,
       "inputGigawords": 0,
       "outputOctets": 9787,
       "outputGigawords": 0,
       "outputPackets": 64,
       "lastUpdated": "Jun 24 2013 17:22:26 CDT",
       "strippedUserName": "barney",
       "realm": "DOMAIN"
    }

The module is also capable of authorizing users via documents stored in couchbase.  The document keys should be returned via a simple view like the following:

    function (doc, meta) {
      if (doc.docType && doc.docType == "radclient" && doc.clientName) {
        emit(doc.clientName, null);
      }
    }

The document structure is straight forward and flexible:

    {
      "docType": "radclient",
      "clientName": "test",
      "config": {
        "SHA-Password": {
          "value": "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3",
          "op": ":="
        }
      },
      "reply": {
        "Reply-Message": {
          "value": "Hidey Ho!",
          "op": "="
        }
      }
    }

To Use
------

Pull freeradius-server master and clone this module under src/modules.  Then enable and compile as usual.
You will need libcouchbase >= 2.0 installed with a valid libio module.  You will also need the json-c library installed and available.

Configuration
-------------

    couchbase {
        ## couchbase document key - radius xlat supported
        key = "radacct_%{Acct-Session-Id}"

        ## value for the 'docType' element in the json document body
        doctype = "radacct"

        ## couchbase host or hosts - multiple hosts should be semi-colon separated
        ## ports are optional if servers are listening on the standard port
        ## pool addresses may also be used
        #host = "cb01-blargs.com:9091;cb02-blargs.com;cb03-blargs.com"
        host ="http://cb01.blargs.com:8091/pools/;http://cb04.blargs.com:8091/pools/"

        ## couchbase bucket name
        bucket = "radius"

        ## bucket password
        #pass = "password"

        ## document expire time in seconds (0 = never)
        expire = 2592000

        ## view path for radius authentication
        authview = "_design/client/_view/by_name"

        ## map attribute names to json element names
        ## attributes not in this map will not be recorded
        map = "{ \
                \"Acct-Session-Id\": \"sessionId\", \
                \"Acct-Status-Type\": \"lastStatus\", \
                \"Acct-Authentic\": \"authentic\", \
                \"User-Name\": \"userName\", \
                \"User-Name\": \"userName\", \
                \"Stripped-User-Name\": \"strippedUserName\", \
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
to a freeradius installation for accounting and authorization.  You should list the couchbase module in both the configuration and authorization sections
if you are planning to use it for both purposes.  You should also have PAP enabled for authenticating users based on cleartext or hashed password attributes.
As always YMMV.

This module was built and tested against freeradius-server master branch as of the latest commit to this branch.
