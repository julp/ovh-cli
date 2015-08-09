CREATE TABLE accounts(
    id INTEGER NOT NULL PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    password TEXT, -- nullable
    consumer_key TEXT, -- nullable
    endpoint_id INTEGER NOT NULL,
    is_default INTEGER NOT NULL DEFAULT 0,
    expires_at INTEGER
);

CREATE TABLE applications(
    "key" TEXT NOT NULL,
    secret TEXT NOT NULL,
    endpoint_id INTEGER NOT NULL UNIQUE
);

-- CREATE TABLE datacenters(
--     id INTEGER NOT NULL PRIMARY KEY,
--     shortname TEXT NOT NULL UNIQUE,
--     longname TEXT NOT NULL
-- );

-- INSERT INTO datacenters('bhs1', 'Beauharnois 1');
-- INSERT INTO datacenters('bhs2', 'Beauharnois 2');
-- INSERT INTO datacenters('bhs3', 'Beauharnois 3');
-- INSERT INTO datacenters('bhs4', 'Beauharnois 4');
-- INSERT INTO datacenters('dc1', 'Paris');
-- INSERT INTO datacenters('gra1', 'Gravelines 1');
-- INSERT INTO datacenters('gsw', 'Paris');
-- INSERT INTO datacenters('p19', 'Paris');
-- INSERT INTO datacenters('rbx-hz', 'Roubaix');
-- INSERT INTO datacenters('rbx1', 'Roubaix 1');
-- INSERT INTO datacenters('rbx2', 'Roubaix 2');
-- INSERT INTO datacenters('rbx3', 'Roubaix 3');
-- INSERT INTO datacenters('rbx4', 'Roubaix 4');
-- INSERT INTO datacenters('rbx5', 'Roubaix 5');
-- INSERT INTO datacenters('rbx6', 'Roubaix 6');
-- INSERT INTO datacenters('sbg1', 'Strasbourg 1');
-- INSERT INTO datacenters('sbg2', 'Strasbourg 2');
-- INSERT INTO datacenters('sbg3', 'Strasbourg 3');
-- INSERT INTO datacenters('sbg4', 'Strasbourg 4');

-- bootType:
-- "harddisk"
-- "ipxeCustomerScript"
-- "network"
-- "rescue"

CREATE TABLE boots(
    bootId INTEGER NOT NULL PRIMARY KEY, -- OVH ID (bootId)
    bootType INT NOT NULL, -- enum
    kernel TEXT NOT NULL,
    description TEXT NOT NULL
);

-- supportLevel:
-- "critical"
-- "fastpath"
-- "gs"
-- "pro"
-- 
-- state:
-- "error"
-- "hacked"
-- "hackedBlocked"
-- "ok"
-- 
-- status:
-- "expired"
-- "inCreation"
-- "ok"
-- "unPaid"

CREATE TABLE dedicated(
    accountId INT NOT NULL REFERENCES accounts(id) ON UPDATE CASCADE ON DELETE CASCADE,
    -- From GET /dedicated/server/{serviceName}
    serverId INTEGER NOT NULL, -- OVH ID
    name TEXT NOT NULL UNIQUE,
    datacenter INT NOT NULL, -- enum
    professionalUse INT NOT NULL, -- bool
    supportLevel INT NOT NULL, -- enum
    commercialRange TEXT, -- nullable
    ip TEXT NOT NULL, -- unique ?
    os TEXT NOT NULL,
    state INT NOT NULL, -- enum
    reverse TEXT, -- nullable
    monitoring INT NOT NULL, -- bool
    rack TEXT NOT NULL,
    rootDevice TEXT, -- nullable
    linkSpeed INT, -- nullable
    bootId INT REFERENCES boots(bootId) ON UPDATE CASCADE ON DELETE CASCADE, -- nullable
    -- From GET /dedicated/server/{serviceName}/serviceInfos
    -- status INT NOT NULL, -- enum
    engagedUpTo INT, -- date, nullable
    -- possibleRenewPeriod: array of int (JSON response)
    contactBilling TEXT NOT NULL,
    -- renew: subobject (JSON response)
    -- domain TEXT NOT NULL, -- same as name?
    expiration INT NOT NULL, -- date
    contactTech TEXT NOT NULL,
    contactAdmin TEXT NOT NULL,
    creation INT NOT NULL, -- date
    PRIMARY KEY(serverId)
);

CREATE TABLE boots_dedicated(
    serverId INT NOT NULL REFERENCES dedicated(serverId) ON UPDATE CASCADE ON DELETE CASCADE,
    bootId INT NOT NULL REFERENCES boots(bootId) ON UPDATE CASCADE ON DELETE CASCADE,
    PRIMARY KEY (serverId, bootId)
);

CREATE TABLE fetches(
    accountId INT NOT NULL REFERENCES accounts(id) ON UPDATE CASCADE ON DELETE CASCADE,
    module_name TEXT NOT NULL,
    updated_at INT NOT NULL,
    PRIMARY KEY (account_id, module_name)
);

-- transferLockStatus:
-- "locked"
-- "locking"
-- "unavailable"
-- "unlocked"
-- "unlocking"

-- offer:
-- "diamond"
-- "gold"
-- "platinum"

-- nameServerType:
-- "external"
-- "hosted"

CREATE TABLE domains(
    accountId INT NOT NULL REFERENCES accounts(id) ON UPDATE CASCADE ON DELETE CASCADE,
    name TEXT NOT NULL,
    -- From GET /domain/zone/{serviceName}
    --lastUpdate INT NOT NULL, -- datetime
    hasDnsAnycast INT NOT NULL, -- boolean
    -- name_servers -- string[] in JSON response: 1 to many
    dnssecSupported INT NOT NULL, -- boolean
    -- From GET /domain/{serviceName}
    owoSupported INT NOT NULL, -- boolean
    -- domain TEXT NOT NULL, -- same as name?
    --lastUpdate INT NOT NULL, -- datetime
    transferLockStatus INT NOT NULL, -- enum
    offer INT NOT NULL, -- enum
    nameServerType INT NOT NULL, -- enum
    -- From GET /domain/{serviceName}/serviceInfos
    -- status INT NOT NULL, -- enum
    engagedUpTo INT, -- date, nullable
    -- possibleRenewPeriod: array of int (JSON response)
    contactBilling TEXT NOT NULL,
    -- renew: subobject (JSON response)
    -- domain TEXT NOT NULL, -- same as name?
    expiration INT NOT NULL, -- date
    contactTech TEXT NOT NULL,
    contactAdmin TEXT NOT NULL,
    creation INT NOT NULL, -- date
    PRIMARY KEY (name)
);

CREATE TABLE records(
    target TEXT NOT NULL,
    ttl INT, -- nullable
    zone TEXT NOT NULL REFERENCES domains(name) ON UPDATE CASCADE ON DELETE CASCADE,
    fieldType INT NOT NULL, -- enum
    id INT NOT NULL, -- OVH ID (why did't they call it recordId?)
    subDomain TEXT, -- nullable
    PRIMARY KEY (id)
);

CREATE TABLE nameservers(
    -- ?
);

CREATE TABLE domains_nameservers(
    -- ?
);
