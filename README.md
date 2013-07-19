rlm_couchbase
=============

Stores radius accounting data directly into couchbase. You can use any radius attribute as a document key.  The default will try to use Acct-Unique-Session-Id
and fallback to Acct-Session-Id if Acct-Unique-Session-Id is not present (needs acct_unique policy in preacct to generate the unique id).
Different status types (start/stop/update) are merged into a single document for easy view writing.  To generate the calledStationSSID fields you will need to
use the rewrite_called_station_id policy in the preacct section of your config.  The couchbase module will attempt to produce the Stripped-User-Name and
Stripped-Domain-Name attributes if used in the preacct section.

Example from an Aerohive Wireless Access Point:

    {
      "docType": "radacct",
      "startTimestamp": "Jul 15 2013 13:22:07 CDT",
      "stopTimestamp": "null",
      "sessionId": "51D241D3-0000047A",
      "lastStatus": 3,
      "authentic": 1,
      "userName": "mruser@blargs.com",
      "nasIpAddress": "172.28.4.150",
      "nasIdentifier": "air4.corp.blargs.com",
      "nasPort": 0,
      "calledStationId": "40-18-b1-01-3c-54",
      "framedIpAddress": "172.27.2.87",
      "callingStationId": "8C-2D-AA-72-36-BA",
      "nasPortType": 19,
      "connectInfo": "11ng",
      "class": "0x494a05120000013700010200ac1b020300000000000000000000000001ce741530c34784000000000001be35",
      "sessionTime": 5821,
      "inputPackets": 5591,
      "inputOctets": 681742,
      "inputGigawords": 0,
      "outputOctets": 536306,
      "outputGigawords": 0,
      "outputPackets": 1087,
      "lastUpdated": "Jul 15 2013 14:59:08 CDT",
      "uniqueId": "029d975fc48ecb41444da52a65e62a55",
      "calledStationSSID": "BLARGS-WIFI",
      "strippedUserName": "mruser",
      "strippedUserDomain": "blargs.com"
    }

The module is also capable of authorizing users via documents stored in couchbase.  The document keys should be returned via a simple view like the following:

    function (doc, meta) {
      if (doc.docType && doc.docType == "raduser" && doc.userName) {
        emit(doc.userName, null);
      }
    }

The document structure is straight forward and flexible:

    {
      "docType": "raduser",
      "userName": "test",
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
        # Couchbase document key (unlang supported)
        key = "radacct_%{%{Acct-Unique-Session-Id}:-%{Acct-Session-Id}}"

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
        authview = "_design/user/_view/by_name"

        #
        # Map attribute names to json element names for accounting
        # documents.  Atrributes noot in this map will not be recorded.
        # This map should be a valid JSON document.
        #
        map = "{ \
                \"Acct-Session-Id\": \"sessionId\", \
                \"Acct-Unique-Session-Id\": \"uniqueId\", \
                \"Acct-Status-Type\": \"lastStatus\", \
                \"Acct-Authentic\": \"authentic\", \
                \"User-Name\": \"userName\", \
                \"Stripped-User-Name\": \"strippedUserName\", \
                \"Stripped-User-Domain\": \"strippedUserDomain\", \
                \"Realm\": \"realm\", \
                \"NAS-IP-Address\": \"nasIpAddress\", \
                \"NAS-Identifier\": \"nasIdentifier\", \
                \"NAS-Port\": \"nasPort\", \
                \"Called-Station-Id\": \"calledStationId\", \
                \"Called-Station-SSID\": \"calledStationSSID\", \
                \"Calling-Station-Id\": \"callingStationId\", \
                \"Framed-IP-Address\": \"framedIpAddress\", \
                \"NAS-Port-Type\": \"nasPortType\", \
                \"Connect-Info\": \"connectInfo\", \
                \"Class\": \"class\", \
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
to a FreeRADIUS installation for accounting and authorization.  You should list the couchbase module in both the accounting, preacct and authorization sections
of your site configuration if you are planning to use it for both purposes.
You should also have PAP enabled for authenticating users based on cleartext or hashed password attributes.
As always YMMV.

This module was built and tested against the FreeRADIUS 3.0.0 release branch (FreeRADIUS/freeradius-server/tree/release_branch_3.0.0) as of the latest commit to this branch.
