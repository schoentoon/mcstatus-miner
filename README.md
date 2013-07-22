mcstatus-miner
==============

The config looks fairly simple as you can see in sample.config, looking at this file should probably explain more than enough about how it should look. The string entered in format may contain the following parameters.

* %hostname The hostname it'll connect to
* %motd The message of the day of the server
* %version The minecraft version of the server
* %numplayers The currently amount of online people
* %maxplayers The maximum amount of players
* %time A unix timestamp

This format will be printed to stdout on every sucessful reply, which should happend every *interval* seconds. It should be fairly each to simply pipe this directly into another program. Below is an example.

Collectd
========

With a format similar to the one below you can easily keep track of the amount of online players in collectd.

>format = PUTVAL "mcstatus/servername/gauge-players" N:%numplayers

The simply put something like this in your collectd.conf.
```
<Plugin exec>
  Exec "nobody:nogroup" "/usr/local/bin/mcstatus-miner" "-c" "/etc/mcstatus-miner.config"
</Plugin>
```