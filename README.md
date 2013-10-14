mcstatus-miner
==============

The config looks fairly simple as you can see in sample.config, looking at this file should probably explain more than enough about how it should look. The string entered in format may contain the following parameters.

* %hostname The hostname it'll connect to
* %motd The message of the day of the server
* %version The minecraft version of the server
* %numplayers The currently amount of online people
* %maxplayers The maximum amount of players
* %time A unix timestamp

As of 13w41a we can also receive a sample of online players, mcstatus-miner now supports this as well. As you can see in sample.config you just add a players_format, which will get printed for every player. There are 2 possible parameters next to all the ones listed above.

* %name The name of the player
* %id I honestly have no idea what this is, but it is returned with each player so we support it.

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
