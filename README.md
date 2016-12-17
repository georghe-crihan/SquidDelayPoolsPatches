# SQUID DELAY POOL PATCHES

As archived around July 2005.

Copyright Sumith Gamage, Sri Lanka.

I managed to compile and run Squid with the dynamic delay pool
patches under FreeBSD 4.8 from cvsupped ports collection
(/usr/ports/www/squid, port version "1.166 2005/05/22 13:49:22 jylefort Exp")
against squid-2.5.STABLE10.tar.bz2. 

I removed the DBNS shared memory routines, as I need a stable
version in my production applications, the rest seems to be OK.
(the patch version with DBNS has appropriate suffix, so drop in only
one, either patch-zz-src-delay_pools.c or patch-zz-src-delay_pools.c.DBNS).
I also dared to clean up the patches a little to keep the number
of modifications as small as possible. That would also help to
port to new versions of Squid later.
The original source came from http://www.cse.mrt.ac.lk/~sumith/msc/src.htm 

All credits are due to Mr. Sumith Gamage of Sri Lanka.
To build that under FreeBSD I just dropped in the patch-zz-*
files into port's files directory and did usual make/make install.

It prooved to be usefull for me. I hope it might also be of any
use to others.
