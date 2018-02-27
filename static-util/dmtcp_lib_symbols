#!/bin/bash

mkdir -p lib_sym

#' ^$    | ^Name | .*\.(c|cpp)$ | ^(\.|_(_)?)  | @(@)? | \. ' > $FILE.sym
#  empty   Name    c or cpp       . or _ or __   @@      .

for FILE in "$@"
do
	readelf -s $FILE \
	| grep 'FUNC' \
	| grep -v -E 'UND' \
	| awk '{print $8}' \
	| sort -u > lib_sym/${FILE##*/}.sym
done

#	| grep 'FUNC' | grep -v 'LOCAL' \
#	| grep -v -E '^$|^Name|.*\.(c|cpp)$|@(@)?|\.' \
