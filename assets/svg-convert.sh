#!/bin/bash
size="$1"
which rsvg-convert > /dev/null 2>&1
if [[ ! $? -eq 0 ]]; then
    echo "rsvg-convert not installed."
    exit 1
elif [[ -z "$size" ]]; then
    echo "Please specify a size."
    exit 1
elif [[ ! "$size" =~ ^[1-9][0-9]*$ ]]; then
    # $size is a pixel dimension, and it is also used as the output DIRECTORY,
    # as the -w/-h value handed to rsvg-convert, and inside a sed pattern. So a
    # value that is not a plain positive integer reaches `rm -rf` and can name
    # any path (`/`, `..`, `../..`), and reaches sed as pattern syntax. Pinning
    # it to digits closes the destructive path, the traversal and the pattern
    # injection at once, and rejects nothing the script can legitimately use.
    echo "Size must be a positive integer number of pixels (got: $size)."
    exit 1
else
    rm -rf "$size"
    mkdir -p "$size"
    find . -maxdepth 1 -type f -name "*.svg" -print0 | while IFS= read -r -d '' file; do
        if [[ ! -h "$file" ]]; then
            comments=$(grep -o "<!--.*-->" "$file")
            rsvg-convert "$file" -w "$size" -h "$size" -f svg -o "$size/$file"
            sed -i -e "s/<[?]\?xml[^>]*>//g" -e "s/${size}pt/${size}px/g" -e "1s/^/${comments}/" "$size/$file"
            sed -i "s/0%,0%,0%/100%,100%,100%/g" "$size/$file"
        else
            cp -rdf "$file" "$size/"
        fi
    done
fi
