* Don't have any browser to manage your domains, servers and so on?
* Want to automate/script some tasks?
* Want to quickly make some administrative jobs on a domain/server?

## Disclaimer

* **Never ever** install this tool on servers (dedicated as VPS) you rent to OVH
* I decline all responsabilities of the usage made of this tool, there is no warranty
* I'm not affiliated in any way to the OVH company

## Prerequisites

### Build dependencies

* cmake >= 2.8.8
* (a C compiler)

### Run dependencies

* openssl
* libcurl
* libxml2
* libedit (autocompletion)
* libiconv
* gettext (optional)
* sqlite3

## Install

 Clone/compile/install ovh-cli
```
git clone ...
cd <path/to/build/directory>
cmake <path/to/sources/directory> (-DCMAKE_INSTALL_PREFIX=/usr/local)
make
(sudo) make install
```

## First usage

* OVH Europe
    + [Create your application](https://eu.api.ovh.com/createApp/)
    + [Create all keys at once](https://eu.api.ovh.com/createToken/)
* OVH North America
    + [Create your application](https://ca.api.ovh.com/createApp/)
    + [Create all keys at once](https://ca.api.ovh.com/createToken/)
* So you Start Europe
    + [Create your application](https://eu.api.soyoustart.com/createApp/)
    + [Create all keys at once](https://eu.api.soyoustart.com/createToken/)
* So you Start North America
    + [Create your application](https://ca.api.soyoustart.com/createApp/)
    + [Create all keys at once](https://ca.api.soyoustart.com/createToken/)
* Kimsufi Europe
    + [Create your application](https://eu.api.kimsufi.com/createApp/)
    + [Create all keys at once](https://eu.api.kimsufi.com/createToken/)
* Kimsufi North America
    + [Create your application](https://ca.api.kimsufi.com/createApp/)
    + [Create all keys at once](https://ca.api.kimsufi.com/createToken/)
* Runabove
    + [Create your application](https://api.runabove.com/createApp/)
    + [Create all keys at once](https://api.runabove.com/createToken/)


1. Create and register your application(s):
```
ovh application <endpoint (one of: kimsufi-ca, kimsufi-eu, ovh-ca, ovh-eu, runabove-ca, soyoustart-ca, soyoustart-eu)> add <application key> <application secret>
```

2. Register your account(s):
    * with your password instead of a valid consumer key: `ovh account <nic-handle> add password <password> endpoint <endpoint (one of: kimsufi-ca, kimsufi-eu, ovh-ca, ovh-eu, runabove-ca, soyoustart-ca, soyoustart-eu)>`
    * with a valid consumer key instead of a password (use link "Create all keys at once" above): `ovh account <nic-handle> add password "" key <consumer key> expires in illimited endpoint <endpoint (one of: kimsufi-ca, kimsufi-eu, ovh-ca, ovh-eu, runabove-ca, soyoustart-ca, soyoustart-eu)>` (if your consumer key is not unlimited, replace `illimited` by `"X days"`)
    * without password or consumer key: `ovh account <nic-handle> add password "" endpoint <endpoint (one of: kimsufi-ca, kimsufi-eu, ovh-ca, ovh-eu, runabove-ca, soyoustart-ca, soyoustart-eu)>` and follow instructions to acquire a consumer key

## Commands

```
Available commands:
     account
         list => list registered accounts
         <account>
             add
                 endpoint <endpoint>
                     key <consumer key> expires <in/at> <expiration>
                         password <password>
             default => set the default account
             delete => remove an OVH account
             invalidate => drop consumer key associated to given OVH account
             switch => switch to another OVH account
             update
     application
         list => list registered applications
         <endpoint>
             add <key> <secret>
             delete => remove an application
     cloud
         list => display projects list
         <project>
             image list => list available images
             instance list => list active instances
     complete => show completion command(s) for current shell
     credentials
         flush => revoke all credentials
         list => list all credentials
     dedicated
         check => display servers about to expire
         list => list your dedicated servers
             nocache
         <server>
             boot
                 list => list available boots
                 show => display active boot
                 <boot> => set active boot
             mrtg <types> <period>
             reboot => hard reboot
             reverse
                 delete => remove reverse DNS
                 set <reverse> => define reverse DNS
             task list => list tasks
     domain
         check => display domains about to expire
         list => list your domains
             nocache
         <domain>
             dnssec
                 status => show DNSSEC status (enabled or disabled)
                 <enable/disable>
             export => export zone in DNS format
             record
                 list => display DNS records of *domain*
                     nocache
                         type <type>
                 <record>
                     add <value> => create a DNS record
                         name <name>
                             target <value>
                                 ttl <ttl>
                                 type <type>
                     delete => delete a DNS record
                     update
             refresh => generate a new serial to reflect any change on a DNS zone
     export => export OVH accounts and applications in ovh-cli commands format
     help => show help
     hosting
         list => list hosted website
         <hosting>
             cron list => list cron jobs for hosting
             database
                 list => list databases associated to *hosting*
                 <database>
                     delete => drop database associated to *hosting*
                     dump <date> => dump a database
             domain
                 list => list domains associated to *hosting*
                 <domain>
                     add <path> => link domain to *hosting* through *path*
                     delete => unlink domain to hosting
             user
                 list => list (FTP & SSH) users of *hosting*
                 <user> delete => delete a user from *hosting*
     key
         list => list global SSH keys
         <name>
             add <value> => upload a new global SSH key to OVH
             default <on/off> => (un)define the global SSH key used in rescue mode
             delete => delete a global SSH key
     log <on/off> => enable/disable logging of HTTP requests and responses to OVH API
     me => display your personal informations
         application
             list => list applications you have created
                 nocache
             <application> delete => delete one of your own applications
         contract
             list => list contracts
                 <status>
             <contract>
                 accept => accept a contract
                 show => show a contract
     quit => exit program
     tickets
         create <subject> => open a new support ticket
         list => list support ticket
         <ticket id>
             close => close a support ticket
             read => display an entire support ticket
             reopen => reopen a support ticket
             reply => reply to a support ticket
     vps list => list your VPS

```

For partial bash completion (in current shell), run: `source <(ovh complete)`

## Examples

Hard reboot a dedicated server in rescue mode:
```
ovh dedicated <server name> boot <name of rescue boot>
ovh dedicated <server name> reboot
```
(press tab for list and/or completion of server and boot names)
