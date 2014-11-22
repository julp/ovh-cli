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
    * list => list all known OVH accounts
    * \<nickhandle>
        * add \<password or use empty string - "" - to not record it> (\<consumer key> expires in|at \<expiration date or delay>) => register a new account
        * delete
        * default
        * switch
* domain
    * list => display all domains owned by the current account
    * \<domain name> ("zone" may be more appropriate)
        * export => export *domain*/zone in DNS format
        * refresh => refresh *domain* (generation of a new zone ID?)
        * dnssec
            * status => print if DNSSEC is enabled/disabled for *domain*
            * enable => enable DNSSEC for *domain*
            * disable => disable DNSSEC for *domain*
        * record ("subdomain" may be more appropriate) (TODO: find a more convenient way to manage empty subdomain name - SPF, NS, ... ?)
            * list => display all subdomains of *domain*
            * \<record name>
                * add \<target> type \<one of: A, AAAA, CNAME, DKIM, LOC, MX, NAPTR, NS, PTR, SPF, SRV, SSHFP, TXT> => add *subdomain* to *domain*
                * delete => delete *subdomain* of *domain* (TODO: non unique subdomain names are all deleted)
* dedicated
    * list => list all servers associated to the current account
    * \<server name>
        * reboot => **hard** reboot of *server*
        * boot
            * list => list all available boots for *server*
            * show => display current boot for *server*
            * \<boot name> => set boot for next (re)boots of *server*
* me => display personal informations associated to the current account (name, address, etc)
* help
* quit
