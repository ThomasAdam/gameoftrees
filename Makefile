SUBDIR = libexec got tog

.PHONY: release dist

.if make(regress) || make(obj) || make(clean) || make(release)
SUBDIR += regress
.endif

.if make(clean) || make(obj) || make(release)
SUBDIR += gotweb
.endif

.include "got-version.mk"

release: clean
	sed -i -e "s/_RELEASE=No/_RELEASE=Yes/" got-version.mk
	${MAKE} dist
	sed -i -e "s/_RELEASE=Yes/_RELEASE=No/" got-version.mk

dist: clean
	mkdir /tmp/got-${GOT_VERSION}
	pax -rw * /tmp/got-${GOT_VERSION}
	find /tmp/got-${GOT_VERSION} -name obj -type d -delete
	rm /tmp/got-${GOT_VERSION}/got-dist.txt
	tar -C /tmp -zcf got-${GOT_VERSION}.tar.gz got-${GOT_VERSION}
	rm -rf /tmp/got-${GOT_VERSION}
	tar -ztf got-${GOT_VERSION}.tar.gz | sed -e 's/^got-${GOT_VERSION}//' \
		| sort > got-dist.txt.new
	diff -u got-dist.txt got-dist.txt.new
	rm got-dist.txt.new

web:
	${MAKE} -C gotweb

web-install:
	${MAKE} -C gotweb install

.include <bsd.subdir.mk>
