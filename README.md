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
       "calledStationId": "08-EA-44-3D-AF-94:My-WiFi-Network",
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
        # Couchbase document key (xlat supported)
        key = "radacct_%{Acct-Session-Id}"

        # Value for the 'docType' element in the json document body
        doctype = "radacct"

        #
        # List of Couchbase hosts semi-colon separated.  Ports are optional if servers
        # are listening on the standard port.  Complete pool urls are preferred.
        #
        server = "http://cb01.blargs.com:8091/pools/;http://cb04.blargs.com:8091/pools/"

        # Couchbase bucket name
        bucket = "radius"

        # Couchbase bucket password
        #pass = "password"

        ## Document expire time in seconds (0 = never)
        expire = 2592000

        # View path for radius authentication
        authview = "_design/client/_view/by_name"

        #
        # Map attribute names to json element names for accounting
        # documents.  Atrributes noot in this map will not be recorded.
        # This map should be a valid JSON document.
        #
        map = "{ \
                \"Acct-Session-Id\": \"sessionId\", \
                \"Acct-Status-Type\": \"lastStatus\", \
                \"Acct-Authentic\": \"authentic\", \
                \"User-Name\": \"userName\", \
                \"Stripped-User-Name\": \"strippedUserName\", \
                \"Realm\": \"realm\", \
                \"NAS-IP-Address\": \"nasIpAddress\", \
                \"NAS-Identifier\": \"nasIdentifier\", \
                \"NAS-Port\": \"nasPort\", \
                \"Called-Station-Id\": \"calledStationId\", \
                \"Calling-Station-Id\": \"callingStationId\", \
                \"Framed-IP-Address\": \"framedIpAddress\", \
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

        #
        #  The connection pool is new for 3.0, and will be used in many
        #  modules, for all kinds of connection-related activity.
        #
        pool {
            # Number of connections to start
            start = 5

            # Minimum number of connections to keep open.
            #
            # NOTE: This should be greater than or equal to spare below.
            min = 4

            # Maximum number of connections
            #
            # If these connections are all in use and a new one
            # is requested, the request will NOT get a connection.
            max = 10

            # Spare connections to be left idle
            #
            # NOTE: Idle connections WILL be closed if "idle_timeout"
            # is set.
            spare = 4

            # Number of uses before the connection is closed
            #
            # 0 means "infinite"
            uses = 0

            # The lifetime (in seconds) of the connection
            lifetime = 0

            # The idle timeout (in seconds).  A connection which is
            # unused for this length of time will be closed.
            #
            # NOTE: We will let libcouchbase handle reconnection of
            # idle couchbase instances.
            idle_timeout = 0

            # NOTE: All configuration settings are enforced.  If a
            # connection is closed because of "idle_timeout",
            # "uses", or "lifetime", then the total number of
            # connections MAY fall below "min".  When that
            # happens, it will open a new connection.  It will
            # also log a WARNING message.
            #
            # The solution is to either lower the "min" connections,
            # or increase lifetime/idle_timeout.
        }
    }

Notes
-----

This module was tested to handle thousands of radius requests in a short period of time from multiple (hundreds) of Aerohive Access Points pointing
to a freeradius installation for accounting and authorization.  You should list the couchbase module in both the configuration and authorization sections
if you are planning to use it for both purposes.  You should also have PAP enabled for authenticating users based on cleartext or hashed password attributes.
As always YMMV.

This module was built and tested against freeradius-server master branch as of the latest commit to this branch.
