default:
	for file in */; do \
	  ( cd $$file && ${MAKE} ); \
	done

clean:
	for file in */; do \
	  ( cd $$file && ${MAKE} clean ); \
	done

distclean:
	for file in */; do \
	  ( cd $$file && ${MAKE} distclean ); \
	done

.PHONY: default clean distclean
