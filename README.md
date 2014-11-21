## Disclaimer

* **Never ever** install this tool on servers (dedicated as VPS) you rent to OVH
* I decline all responsabilities of the usage made of this tool, there is no warranty
* I'm not affiliated in any way to the OVH company

## Prerequisites

### Build dependencies

* cmake
* (a C compiler)

### Run dependencies

* openssl
* libcurl
* libxml2
* libedit
* libiconv (not used yet)
* gettext (optional)

## Install

[Create your application](https://eu.api.ovh.com/createApp/), then:
```
git clone ...
cmake . \
    -DAPPLICATION_KEY="<your application key>" \
    -DAPPLICATION_SECRET="<your application secret>" \
    -DAPI_BASE_URL="https://eu.api.ovh.com/1.0"
make
(sudo) make install
```
(for now, these parameters have to be set at compile time)

## Commands

* account
    * list
    * \<nickhandle>
        * add \<password or use empty string - "" - to not record it> (\<consumer key> expires in|at \<expiration date or delay>)
        * delete
        * default
        * switch
* domain
    * list
    * \<domain name>
        * export
        * refresh
        * dnssec
            * status
            * enable
            * disable
        * record
            * list
            * \<record name>
                * add \<target> type \<one of: A, AAAA, CNAME, DKIM, LOC, MX, NAPTR, NS, PTR, SPF, SRV, SSHFP, TXT>
                * delete [type \<one of: A, AAAA, CNAME, DKIM, LOC, MX, NAPTR, NS, PTR, SPF, SRV, SSHFP, TXT>]
* dedicated
    * list
* me
* help
* quit
